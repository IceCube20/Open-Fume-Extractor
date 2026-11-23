#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/bus/Rs485PeripheralBus.h"
using namespace jbc_rs485;

static uint32_t clock_ms = 100;
uint32_t millis() { return clock_ms; }
struct ModuleRecord {
  uint8_t addr = 0, type = 0;
  bool online = true, eco_mode = false, eco_state_known = false, light_sleep = false;
  uint32_t caps = CAP_POWER_SAVE, eco_since_ms = 0, module_uptime_s = 0;
  bool telemetry_valid = false, output_status_valid = false, output_enabled = false;
  uint16_t io_output_mask = 0;
};
struct Registry {
  std::vector<ModuleRecord> records;
  uint8_t count() const { return uint8_t(records.size()); }
  ModuleRecord& at(uint8_t i) { return records.at(i); }
  const ModuleRecord& at(uint8_t i) const { return records.at(i); }
};
struct Extractor {
  bool running = false, continuous_mode = false, external = false;
  uint8_t work = 0;
  uint32_t afterrun = 0;
  bool outputEnabled() const { return running; }
  bool continuous() const { return continuous_mode; }
  bool externalInputActive() const { return external; }
  uint8_t workMask() const { return work; }
  uint32_t afterrunLeftMs() const { return afterrun; }
};
struct MasterScheduler {
  Registry registry_;
  Extractor extractor_;
  bool power_save_enabled_ = true, power_save_active_ = false, inhibited = false;
  bool module_fw_active_ = false, display_update_active_ = false, scan_job_active_ = false;
  uint8_t module_fw_target_ = 0, next_power_save_sync_index_ = 0;
  uint16_t power_save_idle_minutes_ = 1;
  uint32_t power_save_idle_since_ms_ = 100, last_power_save_apply_ms_ = 0;
  struct { bool active = false; } trace_stats_;
  struct Call { uint8_t addr; bool enabled; };
  std::vector<Call> calls;
  std::array<bool,256> physical_eco{};
  std::array<uint8_t,256> errors{};
  bool automationEcoInhibited() const { return inhibited; }
  bool request(uint8_t addr, uint8_t cmd, const uint8_t* data, uint8_t len, Frame& resp, uint32_t timeout) {
    assert(cmd == CMD_POWER_SAVE && len == 3 && timeout == 160);
    assert(data[1] == 20 && data[2] == 0); // No real Light Sleep or broadcast ACK storm.
    calls.push_back({addr, data[0] != 0});
    if (errors[addr] == 1) return false;
    resp.cmd = errors[addr] == 2 ? CMD_PING : uint8_t(CMD_POWER_SAVE | 0x80);
    resp.len = errors[addr] == 3 ? 1 : 3;
    resp.payload[0] = errors[addr] == 4 ? STATUS_BAD_LEN : STATUS_OK;
    if (!errors[addr]) physical_eco[addr] = data[0] != 0;
    resp.payload[1] = physical_eco[addr] ? 1 : 0;
    return true;
  }
  bool setModulePowerSave(ModuleRecord&, bool);
  bool wakeModule(ModuleRecord&);
  void wakeAllModules();
  void refreshPowerSaveActive();
  bool powerSaveIdle() const;
  void updatePowerSave(uint32_t);
};
#include "generated_power_save.inc.h"

static void tick(MasterScheduler& s) {
  clock_ms += 20;
  const auto before = s.calls.size();
  s.updatePowerSave(clock_ms);
  assert(s.calls.size() <= before + 1);
}

static void test_mcu_swap() {
  MasterScheduler s;
  clock_ms = 100;
  for (uint8_t addr : {0x10,0x11,0x20,0x30,0x40,0x41,0x50,0x60}) {
    ModuleRecord m;
    m.addr = addr;
    s.registry_.records.push_back(m);
    s.physical_eco[m.addr] = true; // Modules stay powered; the new MCU starts with zeroed state.
  }
  for (int i = 0; i < 8; ++i) tick(s);
  assert(s.calls.size() == 8);
  for (const auto& m : s.registry_.records) {
    assert(!s.physical_eco[m.addr] && !m.eco_mode && m.eco_state_known);
  }
  for (int i = 0; i < 100; ++i) tick(s);
  assert(s.calls.size() == 8 && !s.power_save_active_); // No periodic wake traffic after ACK.

  clock_ms = 60100;
  for (int i = 0; i < 8; ++i) tick(s);
  assert(s.calls.size() == 16 && s.power_save_active_);
  for (size_t i = 0; i < s.calls.size(); ++i) assert(s.calls[i].enabled == (i >= 8));
  for (int i = 0; i < 100; ++i) tick(s);
  assert(s.calls.size() == 16);
  s.extractor_.running = true;
  for (int i = 0; i < 8; ++i) tick(s);
  assert(s.calls.size() == 24 && !s.power_save_active_);
}

static void test_disabled_and_idle_eco() {
  for (bool enabled : {false,true}) {
    MasterScheduler s;
    ModuleRecord m;
    m.addr = 0x20;
    s.registry_.records.push_back(m);
    s.physical_eco[m.addr] = true;
    s.power_save_enabled_ = enabled;
    clock_ms = enabled ? 61000 : 100;
    tick(s);
    assert(s.calls.size() == 1 && s.calls[0].enabled == enabled);
    assert(s.registry_.records[0].eco_state_known);
    tick(s);
    assert(s.calls.size() == 1);
  }
}

static void test_failures_and_fairness() {
  for (uint8_t error : {1,2,3,4}) {
    MasterScheduler s;
    clock_ms = 100;
    for (uint8_t addr : {0x10,0x20}) {
      ModuleRecord m;
      m.addr = addr;
      s.registry_.records.push_back(m);
      s.physical_eco[addr] = true;
    }
    s.errors[0x10] = error;
    tick(s);
    assert(!s.registry_.records[0].eco_state_known);
    tick(s);
    assert(s.registry_.records[1].eco_state_known && !s.physical_eco[0x20]);
    s.errors[0x10] = 0;
    tick(s);
    assert(s.registry_.records[0].eco_state_known && !s.physical_eco[0x10]);
  }
  MasterScheduler s;
  ModuleRecord m;
  m.addr = 0x10;
  s.registry_.records.push_back(m);
  s.physical_eco[0x10] = true;
  s.errors[0x10] = 5; // Valid ACK but state still wrong: the next pass must retry.
  clock_ms = 100;
  tick(s);
  assert(s.registry_.records[0].eco_state_known && s.registry_.records[0].eco_mode);
  s.errors[0x10] = 0;
  tick(s);
  assert(s.calls.size() == 2 && !s.physical_eco[0x10]);
}

static void test_exclusions_and_reboot() {
  MasterScheduler s;
  for (uint8_t addr : {0x10,0x20,0x30,0x40}) {
    ModuleRecord m;
    m.addr = addr;
    s.registry_.records.push_back(m);
    s.physical_eco[addr] = true;
  }
  s.registry_.records[0].online = false;
  s.registry_.records[1].caps = 0;
  s.module_fw_active_ = true;
  s.module_fw_target_ = 0x30;
  clock_ms = 100;
  tick(s);
  tick(s);
  assert(s.calls.size() == 1 && s.calls[0].addr == 0x40);
  assert(!s.setModulePowerSave(s.registry_.records[2],false));
  auto& m = s.registry_.records[3];
  m.telemetry_valid = true;
  m.module_uptime_s = 120;
  assert(!module_update_uptime(m,121) && m.eco_state_known);
  assert(module_update_uptime(m,0) && !m.eco_state_known);
  tick(s);
  assert(m.eco_state_known && s.calls.size() == 2);
  m.eco_state_known = false; // Reconnect invalidation, including a millis rollover.
  s.power_save_idle_since_ms_ = UINT32_MAX - 500;
  clock_ms = 100;
  tick(s);
  assert(m.eco_state_known && !s.calls.back().enabled);
}

int main() {
  test_mcu_swap();
  test_disabled_and_idle_eco();
  test_failures_and_fairness();
  test_exclusions_and_reboot();
  std::puts("PASS: actual Eco reconciliation after powered-module MCU swap, ACK validation/retry, one request per pass, steady-state silence, OTA/offline/capability exclusion, restart and rollover");
}
