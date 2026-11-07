#include "ModuleRegistry.h"
#include "DisplayHomeMetrics.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <set>
using namespace jbc_rs485;
struct Registry {
  std::vector<std::unique_ptr<ModuleRecord>> records;
  uint8_t count() const { return uint8_t(records.size()); }
  ModuleRecord& at(uint8_t i) { return *records[i]; }
} registry_;
static bool moduleProvidesExtractorOutput(const ModuleRecord&) { return false; }
static ofe_home_metrics::State display_home_metrics(const ModuleRecord& m, bool) {
  ofe_home_metrics::State s; s.addr = m.addr; s.type = m.type; return s;
}
static void build(ModuleRecord* display, uint8_t* payload, uint8_t& payload_len) {
  auto payload_write_u8 = [&](uint8_t v) { payload[payload_len++] = v; };
  auto payload_write_u16 = [&](uint16_t v) {
    payload_write_u8(uint8_t(v)); payload_write_u8(uint8_t(v >> 8));
  };
#include "live_builder.inc.h"
}
static ModuleRecord& add(uint8_t addr, uint8_t type, uint32_t caps) {
  registry_.records.emplace_back(new ModuleRecord);
  auto& m = *registry_.records.back(); m.addr = addr; m.type = type; m.caps = caps; m.online = true;
  return m;
}
int main() {
  auto display = std::make_unique<ModuleRecord>(); display->display_home_metrics_supported = true;
  add(0x10, MODULE_JBC_BUS, CAP_JBC_ACTIVITY);
  add(0x11, MODULE_JBC_USB, CAP_JBC_ACTIVITY);
  auto& fan = add(0x20, MODULE_FAN_IO_PRO, CAP_DIGITAL_OUTPUT | CAP_INPUT_KEYS);
  auto& fan2 = add(0x21, MODULE_FAN_IO, CAP_DIGITAL_OUTPUT | CAP_INPUT_KEYS);
  add(0x30, MODULE_WELLER_ZERO_SMOG, CAP_DIGITAL_OUTPUT);
  auto& uni = add(0x50, MODULE_UNIVERSAL_RS232, CAP_ENTITY_CONTROL);
  uni.universal_entities_valid = true; uni.universal_entity_count = 32;
  for (unsigned i=0;i<32;++i) { uni.universal_entities[i].id=uint8_t(i+20); uni.universal_entities[i].len=1; }
  add(0x60, MODULE_MODBUS_RTU, CAP_ENTITY_CONTROL);
  add(0x40, MODULE_DISPLAY, CAP_DISPLAY);
  for (uint8_t prefix : {uint8_t(80),uint8_t(125)}) {
    std::set<uint8_t> metric_modules;
    std::set<uint8_t> entities;
    unsigned fan_seen=0, fan2_seen=0;
    for (unsigned packet=0;packet<40;++packet) {
      const bool on = (packet*200)%1500 < 500;
      fan.io_output_mask = on ? 2 : 0; fan2.io_input_mask = on ? 8 : 0;
      uint8_t payload[MAX_PAYLOAD+1] = {}; payload[MAX_PAYLOAD]=0xA5;
      uint8_t len=prefix; build(display.get(),payload,len);
      assert(len<=MAX_PAYLOAD-9 && payload[MAX_PAYLOAD]==0xA5);
      assert(payload[prefix]==0xA4 && payload[prefix+2]==1);
      unsigned p=prefix+4, metrics=0; bool saw1=false,saw2=false;
      for (unsigned i=0;i<payload[prefix+3];++i) {
        const auto kind=payload[p++];
        if(kind==1) {
          const auto addr=payload[p];
          if(addr==fan.addr) { assert(ofe_home_metrics::get16(payload+p+4)==(on?2:0)); saw1=true; ++fan_seen; }
          if(addr==fan2.addr) { assert(ofe_home_metrics::get16(payload+p+2)==(on?8:0)); saw2=true; ++fan2_seen; }
          p+=8;
        } else if(kind==2) p+=4;
        else if(kind==3) { entities.insert(payload[p+1]); p+=3; }
        else if(kind==4) { ++metrics; metric_modules.insert(payload[p]); p+=ofe_home_metrics::WIRE_SIZE; }
        else assert(false);
      }
      assert(p==len && metrics==1);
      if(prefix==80) assert(saw1 && saw2); // 500-ms edges are in every 200-ms sample
    }
    assert(metric_modules.size()==5 && entities.size()==32);
    assert(fan_seen>=15 && fan2_seen>=15); // even under a large alarm/prefix load
  }
  display->display_home_metrics_supported=false;
  uint8_t bytes[MAX_PAYLOAD]={}; uint8_t len=80; build(display.get(),bytes,len);
  assert(bytes[80]==0xA4); // old displays retain the same v1 format
  std::cout<<"PASS: actual live builder, hot masks first, fair metrics/entities, tight payloads and legacy format\n";
}
