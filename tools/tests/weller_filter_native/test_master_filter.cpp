#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

static constexpr uint32_t CAP_JBC_BUS = 1, CAP_FILTER_SENSOR = 2, CAP_WELLER_INTERFACE = 4;
static constexpr uint8_t ADDR_JBC_MIN = 0x10, ADDR_MODBUS_MAX = 0x6F, MODULE_UNKNOWN = 0;
struct ModuleRecord {
  bool online = true;
  uint8_t addr = 0, type = 5, weller_filter_status = 0;
  uint32_t caps = 0;
  uint16_t output_fault_mask = 0, io_fault_mask = 0, weller_uart_age_sec = 0;
  uint16_t weller_filter_runtime_minutes = 0, weller_programmed_filter_minutes = 0;
};
struct Registry {
  ModuleRecord records[2];
  uint8_t count() const { return 2; }
  const ModuleRecord& at(uint8_t index) const { return records[index]; }
  const ModuleRecord* find(uint8_t addr) const {
    for (const auto& record : records) if (record.addr == addr) return &record;
    return nullptr;
  }
};
class MasterScheduler {
public:
  Registry registry_;
  uint8_t active_output_addr_ = 0x30;
  uint16_t systemJbcError() const;
  uint16_t systemJbcFilterLife() const;
  uint16_t systemJbcFilterSaturation() const;
};

#include "generated_master_filter.inc.h"

int main() {
  MasterScheduler scheduler;
  auto& input = scheduler.registry_.records[0];
  input.addr = 0x10; input.type = 1; input.caps = CAP_JBC_BUS;
  auto& out = scheduler.registry_.records[1];
  out.addr = 0x30; out.caps = CAP_FILTER_SENSOR | CAP_WELLER_INTERFACE;
  for (uint8_t last_status : {uint8_t(10), uint8_t(100)}) {
    out.weller_filter_status = last_status;
    out.io_fault_mask = 0;
    out.weller_uart_age_sec = 0;
    assert(scheduler.systemJbcError() == 0);
    assert(scheduler.systemJbcFilterLife() == 0);
    assert(scheduler.systemJbcFilterSaturation() == 0);
    out.io_fault_mask = last_status == 10 ? 2 : 4;
    assert(scheduler.systemJbcError() == (last_status == 10 ? 2 : 1));
    assert(scheduler.systemJbcFilterLife() == (last_status == 10 ? 900 : 1000));
    assert(scheduler.systemJbcFilterSaturation() == (last_status == 10 ? 900 : 1000));
    for (uint16_t age : {uint16_t(11), uint16_t(65534), uint16_t(65535)}) {
      out.weller_uart_age_sec = age;
      assert(scheduler.systemJbcError() == 2048);
      assert(scheduler.systemJbcFilterLife() == 0);
      assert(scheduler.systemJbcFilterSaturation() == 0);
    }
  }
  out.io_fault_mask = 0; out.weller_uart_age_sec = 65534;
  out.weller_filter_runtime_minutes = 60; out.weller_programmed_filter_minutes = 600;
  assert(scheduler.systemJbcFilterLife() == 100);
  out.weller_uart_age_sec = 0; out.weller_filter_status = 1;
  assert(scheduler.systemJbcError() == 0);
  out.caps = CAP_FILTER_SENSOR; out.weller_uart_age_sec = 65535;
  out.io_fault_mask = 4;
  assert(scheduler.systemJbcError() == 1 && scheduler.systemJbcFilterSaturation() == 1000);
  out.io_fault_mask = 0x100;
  assert(scheduler.systemJbcError() == 64);
  out.online = false;
  assert(scheduler.systemJbcError() == 2048 && scheduler.systemJbcFilterSaturation() == 0);
  std::puts("PASS: production Master filter policy ignores cached Weller display values, preserves fresh alarms/runtime and non-Weller faults");
}
