#include "ModuleRegistry.h"
#include "DisplayHomeMetrics.h"
#include "DisplayValueSnapshot.h"
#include "EntityPresentation.h"
#include "OfeJbcDetailWire.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
using namespace jbc_rs485;
using namespace ofe_home_metrics;
namespace jbc_rs485 { uint16_t get_u16_le(const uint8_t* p) { return ofe_home_metrics::get16(p); } }
#include "builder.inc.h"
#include "fanio_live.inc.h"

static void fanio_live_tests(ModuleRecord& m) {
  m.type = MODULE_FAN_IO_PRO; m.universal_entities_valid = false;
  m.universal_entity_count = 17;
  for (uint8_t i = 0; i < 17; ++i) {
    auto& e = m.universal_entities[i];
    e.id = i < 8 ? 20 + i : (i < 16 ? 40 + i - 8 : 64);
    e.len = 4; e.age_ms = 123; e.data[0] = 99;
  }
  // Every 60 ms fast read observes a 500 ms ON / 1000 ms OFF OUT2,
  // even without another slow bulk ENTITY_GET during the entire sequence.
  for (uint32_t now = 0; now < 15000; now += 60) {
    const bool on = now % 1500 < 500;
    m.io_output_mask = on ? 2 : 0;
    sync_fanio_gpio_entities(m, 0x81);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const auto& in = m.universal_entities[bit];
      const auto& out = m.universal_entities[8 + bit];
      assert(in.len == 1 && in.age_ms == 0 && in.data[0] == ((0x81 >> bit) & 1));
      assert(out.len == 1 && out.age_ms == 0 && out.data[0] == (bit == 1 && on));
    }
    assert(m.universal_entities[16].data[0] == 99 && m.universal_entities[16].age_ms == 123);
    assert(!m.universal_entities_valid && m.universal_entity_count == 17);
  }
  m.type = MODULE_UNIVERSAL_RS232; m.io_output_mask = 0xFFFF;
  sync_fanio_gpio_entities(m, 0xFFFF);
  assert(m.universal_entities[8].data[0] == 0); // same IDs on other module types are untouched
  m.type = MODULE_FAN_IO; m.universal_entity_count = 0;
  sync_fanio_gpio_entities(m, 0xFFFF); assert(m.universal_entity_count == 0);
  m = ModuleRecord{};
}
enum { DISPLAY_LIVE_IO_NONE=0, DISPLAY_LIVE_IO_ANY=1 };
static Cache display_home_metrics_cache;
static int display_home_metrics_mux=0,other_records=0;
static uint32_t now_ms=100;
static uint32_t millis(){return now_ms;}
static ModuleRecord* io_record = nullptr;
static struct { ModuleRecord* find(uint8_t addr) { return io_record && io_record->addr == addr ? io_record : nullptr; } } registry_;
static uint32_t full_io_poll_total_ = 0, compact_io_poll_total_ = 0;
static unsigned routing_updates = 0;
static void updateInputRouting(){++routing_updates;}
static Frame io_reply;
static bool io_reply_ok = true;
static constexpr uint32_t MODULE_IO_TIMEOUT_MS = 70;
static bool request(uint8_t addr, uint8_t command, const uint8_t* flags, uint8_t len, Frame& response, uint32_t) {
  assert(addr == io_record->addr && command == CMD_GET_IO && flags && len == 1);
  response = io_reply; return io_reply_ok;
}
#include "fanio_readback.inc.h"
static void fanio_readback_tests(ModuleRecord& m) {
  io_record = &m; m.addr = 0x20; m.type = MODULE_FAN_IO_PRO;
  m.universal_entity_count = 2; m.universal_entities_valid = true;
  m.universal_entities[0].id = 41; m.universal_entities[0].len = 1;
  m.universal_entities[1].id = 20; m.universal_entities[1].len = 1;
  io_reply.cmd = CMD_GET_IO | 0x80; io_reply.len = 9;
  io_reply.payload[0] = STATUS_OK;
  // Event-only input goes to routing, not the physical entity readback.
  put16(io_reply.payload + 7, 1);
  for (now_ms = 0; now_ms < 15000; now_ms += 60) {
    const bool on = now_ms % 1500 < 500;
    put16(io_reply.payload + 3, on ? 2 : 0);
    assert(readIoStatus(0x20, false));
    assert(m.universal_entities[0].data[0] == on && m.universal_entities[0].age_ms == 0);
    assert(m.io_input_mask == 1 && m.universal_entities[1].data[0] == 0);
  }
  const auto old = m.universal_entities[0].data[0];
  put16(io_reply.payload + 3, old ? 0 : 2);
  io_reply_ok = false; assert(!readIoStatus(0x20, false));
  assert(m.universal_entities[0].data[0] == old);
  io_reply_ok = true; io_reply.payload[0] = STATUS_BAD_VALUE;
  assert(!readIoStatus(0x20, false)); assert(m.universal_entities[0].data[0] == old);
  m = ModuleRecord{}; io_record = nullptr;
}

static void snapshot_tests(ModuleRecord& m) {
  using namespace ofe_display_values;
  SnapshotCache cache;
  m.addr = 0x50; m.type = MODULE_UNIVERSAL_RS232; m.uid = 123;
  m.online = m.universal_entities_valid = true;
  m.caps = CAP_ENTITY_CONTROL; m.universal_descriptor_crc = 1234;
  m.universal_entity_count = 1; auto& e = m.universal_entities[0];
  e.id = 20; e.len = 1; e.data[0] = 1;
  auto key = snapshotKey(m, 1);
  assert(!cache.find(key, 100));
  auto& snapshot = cache.begin(key, 100);
  assert(!cache.find(key, 100)); cache.commit(snapshot, 1, 0x19);
  assert(cache.find(key, 101) == &snapshot);
  e.age_ms = 65535; m.universal_entities_last_ms = 10000;
  assert(snapshotKey(m, 1) == key); // no needless reformat after identical poll
  assert(!(snapshotKey(m, 0) == key));
  ++e.data[0]; assert(!cache.find(snapshotKey(m, 1), 102)); --e.data[0];
  m.online = false; assert(!(snapshotKey(m, 1) == key)); m.online = true;
  m.universal_entities_valid = false; assert(!(snapshotKey(m, 1) == key)); m.universal_entities_valid = true;
  ++m.uid; assert(!(snapshotKey(m, 1) == key)); --m.uid;
  ++m.universal_descriptor_crc; assert(!(snapshotKey(m, 1) == key)); --m.universal_descriptor_crc;
  assert(!cache.find(key, 1100));
  auto& rollover = cache.begin(key, UINT32_MAX - 99); cache.commit(rollover, 1, 0x19);
  assert(cache.find(key, 50) && !cache.find(key, 1000));
  auto a = key, b = key, c = key; ++b.addr; c.addr += 2;
  cache.commit(cache.begin(a, 100), 1, 0); cache.commit(cache.begin(b, 101), 1, 0);
  assert(cache.find(a, 102)); cache.commit(cache.begin(c, 103), 1, 0);
  assert(cache.find(a, 104) && cache.find(c, 104) && !cache.find(b, 104));
  cache.commit(cache.begin(c, 105), MAX_ENTITIES + 1, 0); assert(!cache.find(c, 105));
  m = ModuleRecord{};
}
static void portENTER_CRITICAL(int* lock){assert(!*lock);*lock=1;}
static void portEXIT_CRITICAL(int* lock){assert(*lock);*lock=0;}
static uint8_t apply_display_live_native(uint8_t,uint8_t,uint16_t,uint16_t,uint16_t){++other_records;return DISPLAY_LIVE_IO_ANY;}
static uint8_t apply_display_live_jbc(uint8_t,uint8_t,uint8_t,uint8_t){++other_records;return DISPLAY_LIVE_IO_ANY;}
static uint8_t apply_display_live_entity(uint8_t,uint8_t,uint8_t){++other_records;return DISPLAY_LIVE_IO_ANY;}
#include "live_parser.inc.h"

static unsigned jbc_requests = 0;
static bool consumeDisplayJbcExtension(ModuleRecord&, const Frame&, uint8_t&, uint8_t) {
  ++jbc_requests; return false;
}
#include "negotiation.inc.h"

static void negotiation_tests(ModuleRecord& record) {
  static_assert(CAPABILITY != ofe_jbc_detail::LEGACY_REQUEST &&
                CAPABILITY != ofe_jbc_detail::REQUEST && CAPABILITY != ofe_jbc_detail::COMMAND);
  using Advertise = void (*)(Frame&);
  using Parse = void (*)(ModuleRecord&, const Frame&);
  for (Advertise advertise : {advertise_DisplayModule_320x480, advertise_DisplayModule_800x480,
                              advertise_DisplayModule_ST7796_320x480}) {
    for (Parse parse : {negotiate_async, negotiate_sync}) {
      Frame frame; frame.len = 6;
      // Coexist with background list, detail and profile page requests.
      for (uint8_t byte : {0xAE, 0, 0xAF, 0x20, 0xAD, 0x60, 4}) frame.payload[frame.len++] = byte;
      const auto before = frame.len; advertise(frame);
      assert(frame.len == before + 4 && frame.payload[before] == CAPABILITY);
      record.display_home_metrics_supported = false; record.display_values_supported = false; parse(record, frame);
      assert(record.display_home_metrics_supported && jbc_requests == 0);
      assert(record.display_values_supported);
      const auto complete = frame;
      frame.len = before + 1; record.display_home_metrics_supported = false; parse(record, frame);
      assert(!record.display_home_metrics_supported);
      frame = complete; frame.payload[before + 1] = 255; parse(record, frame);
      assert(!record.display_home_metrics_supported);
      frame = complete;
      ofe_display_values::Request values;
      values.valid = true; values.addr = 0x60; values.crc = 0x1234; values.count = 32; values.priority = 51;
      values.known[31] = 0xABCD;
      frame.len += ofe_display_values::encodeRequest(frame.payload + frame.len, MAX_PAYLOAD - frame.len, values);
      parse(record, frame);
      assert(record.display_value_request.valid && record.display_value_request.addr == 0x60 &&
             record.display_value_request.known[31] == 0xABCD && record.display_value_request.priority == 51);
      frame.len = 8; frame.payload[6] = 0xAC; frame.payload[7] = 0; parse(record, frame);
      assert(!record.display_value_request.valid);
      record.display_home_metrics_supported = false;
      frame.len = MAX_PAYLOAD - 1; advertise(frame); assert(frame.len == MAX_PAYLOAD - 1);
      frame.len = 8; frame.payload[6] = ofe_jbc_detail::LEGACY_REQUEST; frame.payload[7] = 1;
      parse(record, frame); assert(!record.display_home_metrics_supported && jbc_requests == 1);
      jbc_requests = 0;
    }
  }
}

static void packet_tests(const State& state){
  Frame frame;memset(frame.payload,0,sizeof(frame.payload));frame.payload[55]=5;
  unsigned p=59;frame.payload[p++]=0xA4;const unsigned length=p++;
  frame.payload[p++]=1;frame.payload[p++]=1;frame.payload[p++]=KIND;
  assert(encode(frame.payload+p,MAX_PAYLOAD-p,state));p+=WIRE_SIZE;
  frame.payload[length]=uint8_t(p-length-1);frame.len=uint8_t(p);
  const Frame complete=frame;
  assert(parse_display_live_io(frame)==DISPLAY_LIVE_IO_ANY);
  assert(parse_display_live_io(frame)==DISPLAY_LIVE_IO_NONE);
  State seen;uint32_t received=0;
  assert(display_home_metrics_cache.get(state.addr,state.type,seen,received)&&seen.power==state.power);
  for(unsigned len=0;len<complete.len;++len){frame=complete;frame.len=uint8_t(len);assert(parse_display_live_io(frame)==DISPLAY_LIVE_IO_NONE);}
  frame=complete;frame.payload[62]=2;frame.payload[frame.len++]=3;
  frame.payload[frame.len++]=0x60;frame.payload[frame.len++]=20;frame.payload[frame.len++]=1;
  frame.payload[length]+=4;assert(parse_display_live_io(frame)==DISPLAY_LIVE_IO_ANY&&other_records==1);
  frame=complete;put16(frame.payload+64+4,1001);assert(parse_display_live_io(frame)==DISPLAY_LIVE_IO_NONE);
  assert(display_home_metrics_cache.get(state.addr,state.type,seen,received)&&seen.power==state.power);
  now_ms+=11001;assert(parse_display_live_io(complete)==DISPLAY_LIVE_IO_ANY);
  assert(!display_home_metrics_mux);
}

int main() {
  auto record = std::make_unique<ModuleRecord>();
  auto& m = *record;
  fanio_live_tests(m);
  fanio_readback_tests(m);
  snapshot_tests(m);
  negotiation_tests(m);
  m.addr=0x22;m.type=MODULE_FAN_IO_PRO;m.online=true;
  m.output_status_valid=true;m.output_enabled=true;m.output_power=670;m.output_rpm=2300;
  m.caps=CAP_PWM_OUTPUT|CAP_TACHO_INPUT|CAP_FILTER_SENSOR;
  m.fanio_filter_runtime_valid=true;m.fanio_filter_mode=3;m.fanio_filter_flags=2;
  m.fanio_filter_saturation_permille=120;m.fanio_filter_pressure_raw=-123;
  m.fanio_filter_runtime_minutes=100000;m.fanio_filter_lifetime_minutes=200000;
  State original=display_home_metrics(m,true);
  packet_tests(original);
  assert((original.flags&(HAS_POWER|HAS_RPM|HAS_FILTER|HAS_TIME|HAS_PRESSURE|OUTPUT_ON))==
    (HAS_POWER|HAS_RPM|HAS_FILTER|HAS_TIME|HAS_PRESSURE|OUTPUT_ON));
  assert(original.filter==1&&original.power==670&&original.rpm==2300&&remaining(original)==100000);
  uint8_t wire[WIRE_SIZE+2];memset(wire,0xA5,sizeof(wire));
  assert(encode(wire+1,WIRE_SIZE,original)&&wire[0]==0xA5&&wire[WIRE_SIZE+1]==0xA5);
  State decoded;assert(decode(wire+1,WIRE_SIZE,decoded));
  assert(decoded.runtime==100000&&decoded.lifetime==200000&&decoded.pressure==-123);
  assert(decoded.flags==original.flags&&decoded.power==original.power&&decoded.addr==0x22);
  assert(wire[1]==0x22&&get16(wire+5)==670);
  for(size_t size=0;size<WIRE_SIZE;++size) {
    State sentinel;sentinel.addr=0x99;
    assert(!decode(wire+1,size,sentinel)&&sentinel.addr==0x99);
    assert(!encode(wire+1,size,original));
  }
  uint8_t invalid[WIRE_SIZE];encode(invalid,sizeof(invalid),original);
  put16(invalid+4,1001);assert(!decode(invalid,sizeof(invalid),decoded));
  encode(invalid,sizeof(invalid),original);put16(invalid+10,1001);assert(!decode(invalid,sizeof(invalid),decoded));
  assert(!decode(nullptr,WIRE_SIZE,decoded)&&!encode(nullptr,WIRE_SIZE,original));
  auto exhausted=original;exhausted.runtime=0xFFFFFFFFU;assert(remaining(exhausted)==0);
  auto maximum=original;maximum.runtime=0;maximum.lifetime=0xFFFFFFFFU;assert(remaining(maximum)==0xFFFFFFFFU);

  for(uint8_t mode=0;mode<=3;++mode) {
    m.fanio_filter_mode=mode;const State state=display_home_metrics(m,true);
    assert(bool(state.flags&HAS_FILTER)==bool(mode));
    assert(bool(state.flags&HAS_TIME)==bool(mode&1));
    assert(bool(state.flags&HAS_PRESSURE)==bool(mode&2));
  }
  m.fanio_filter_mode=2;m.fanio_filter_flags=0;
  auto unavailable=display_home_metrics(m,true);
  assert(!(unavailable.flags&HAS_PRESSURE)&&unavailable.filter==255);
  m.fanio_filter_runtime_valid=false;m.universal_descriptor_valid=true;
  strcpy(m.universal_descriptor,"module=Fan/IO Pro\nfilter_mode=0\n");
  assert(!(display_home_metrics(m,true).flags&(HAS_FILTER|HAS_TIME|HAS_PRESSURE)));
  m.type=MODULE_FAN_IO;m.caps=0;
  assert(!(display_home_metrics(m,false).flags&(HAS_OUTPUT|HAS_POWER|OUTPUT_VALID|HAS_RPM|HAS_FILTER|HAS_TIME|HAS_PRESSURE)));
  m.type=MODULE_WELLER_ZERO_SMOG;m.telemetry_valid=true;m.weller_uart_age_sec=65535;
  m.weller_speed_percent=33;m.weller_fan_rpm=1250;m.weller_filter_status=10;
  m.weller_filter_runtime_minutes=75;m.weller_programmed_filter_minutes=960;
  const auto weller=display_home_metrics(m,true);
  assert((weller.flags&LINK_KNOWN)&&!(weller.flags&LINKED)&&weller.filter==10);
  assert(weller.runtime==75&&weller.power==330&&weller.rpm==1250&&remaining(weller)==885);
  m.weller_programmed_filter_minutes=0;assert(!(display_home_metrics(m,true).flags&HAS_TIME));
  m.type=MODULE_MODBUS_RTU;m.universal_entity_count=1;m.universal_entities[0].id=20;
  m.universal_entities[0].len=1;m.universal_entities[0].data[0]=1;
  strcpy(m.universal_descriptor,"20 binary_sensor input ro role=main_input\n21 sensor rpm ro role=main_output_rpm\n");
  const auto modbus=display_home_metrics(m,true);
  assert((modbus.flags&(HAS_INPUT|INPUT_ON|HAS_RPM))==(HAS_INPUT|INPUT_ON|HAS_RPM));
  assert(modbus.rpm==2300); // Already-processed Master feedback must not be scaled again.
  m.universal_entities[0].data[0]=0;assert(!(display_home_metrics(m,true).flags&INPUT_ON));

  Cache cache;uint32_t received=0;State found;
  assert(!cache.get(original.addr,original.type,found,received));
  assert(cache.put(original,0xFFFFFFF0U)&&cache.get(original.addr,original.type,found,received));
  assert(!cache.put(original,5)&&!cache.get(original.addr,MODULE_WELLER_ZERO_SMOG,found,received));
  assert(cache.put(original,11006)); // Identical data becoming fresh must redraw.
  State other=original;other.addr=0x23;other.power=100;cache.put(other,11007);
  assert(cache.get(0x22,original.type,found,received)&&found.power==670);
  assert(cache.get(0x23,original.type,found,received)&&found.power==100);
  other.type=MODULE_WELLER_ZERO_SMOG;cache.put(other,11008);
  assert(!cache.get(0x23,original.type,found,received));
  for(unsigned i=0;i<17;++i) {State s=original;s.addr=uint8_t(0x40+i);cache.put(s,12000+i);}
  assert(!cache.get(0x22,original.type,found,received));
  assert(cache.get(0x50,original.type,found,received));cache.clear();
  assert(!cache.get(0x50,original.type,found,received));
  std::cout<<"PASS: actual Master metrics builder, feature modes, processed role values, Weller disconnected cache, bounded wire codec, per-address cache, 32-bit times, wrap and eviction\n";
}
