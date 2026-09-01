#include <cassert>
#include <cstdio>
#include <string>
#include "Rs485PeripheralBus.h"
using namespace jbc_rs485;

// Arduino/LVGL/task adapters only; parsers/formatters/accounting are extracted
// unchanged from each firmware's actual source.
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
};
static uint32_t now_ms=0;
static uint32_t millis() { return now_ms; }
static int jbc_station_mux=0;
static void portENTER_CRITICAL(int* m) { assert(!*m); ++*m; }
static void portEXIT_CRITICAL(int* m) { assert(*m==1); --*m; }
static const char* tr(const char*,const char* de) { return de; }
static const char* jbc_error_name(uint16_t) { return "Fehler"; }
static bool screensaver_has_alarm_code(uint8_t) { return false; }
static constexpr uint8_t DISPLAY_ALARM_JBC_STATION=1;
struct DisplayStatus {
  struct JbcStation { uint8_t flags=0; char model[5]={}; };
  JbcStation jbc_stations[16];
  uint8_t jbc_station_count=0, station_addr=0, jbc_inputs=1, work_mask=0;
  uint16_t jbc_stat_error=0;
  bool jbc_present=true, jbc_connected=true;
};
static DisplayStatus status;
#include "generated_display_jbc.inc.h"

struct ModuleRecord {
  uint8_t type=MODULE_DISPLAY, consecutive_timeouts=0, last_timeout_cmd=0;
  uint32_t caps=CAP_DISPLAY_HYBRID, last_seen_ms=0, miss_count=0, last_timeout_ms=0;
  uint16_t timeout_count=0;
  bool online=false;
};
static struct Registry {
  ModuleRecord rec;
  ModuleRecord* find(uint8_t addr) { return addr==0x40 ? &rec : nullptr; }
} registry_;
#include "generated_module_timeout.inc.h"

int main() {
  Frame frame; frame.len=12; frame.payload[0]=0xA7; frame.payload[1]=2;
  frame.payload[2]=2; memcpy(frame.payload+3,"DDE",4);
  frame.payload[7]=1; memcpy(frame.payload+8,"JTSE",4);
  assert(update_jbc_station_list(frame,1));
  assert(home_jbc_station_models()=="DDE, JTSE");
  for (uint8_t legacy : {0,0x10,0x12,0x18}) {
    status.station_addr=legacy;
    assert(home_jbc_station_models()=="DDE, JTSE");
  }
  now_ms=0; assert(screensaver_jbc_text()=="JBC Station 1: DDE Stand OK");
  now_ms=5000; assert(screensaver_jbc_text()=="JBC Station 2: JTSE Work OK");
  --frame.len; assert(!update_jbc_station_list(frame,1));
  assert(home_jbc_station_models()=="DDE, JTSE"); // truncated frame cannot clear the cache
  frame.payload[1]=17; assert(!update_jbc_station_list(frame,1));
  assert(!update_jbc_station_list(frame,frame.len));
  frame.len=7; frame.payload[1]=1; frame.payload[2]=0; memcpy(frame.payload+3,"CLM",4);
  assert(update_jbc_station_list(frame,1));
  assert(home_jbc_station_models()=="CLM");
  assert(status.jbc_stations[1].model[0]==0); // removed station really cleared
  frame.len=82; frame.payload[1]=16;
  for (int i=0;i<16;++i) { frame.payload[2+i*5]=0; memcpy(frame.payload+3+i*5,"JTSE",4); }
  assert(update_jbc_station_list(frame,1));
  assert(home_jbc_station_models()=="JTSE, JTSE +14");
  now_ms=75000; assert(screensaver_jbc_text()=="JBC Station 16: JTSE Bereit OK");
  frame.len=2; frame.payload[1]=0;
  assert(update_jbc_station_list(frame,1));
  status.jbc_connected=false; assert(screensaver_jbc_text()=="JBC Station offline");
  status.station_addr=0; assert(home_jbc_station_models()=="JBC");
  status.station_addr=0x18; assert(home_jbc_station_models()=="DDE"); // legacy FAE support
  assert(jbc_station_mux==0);

  auto& rec=registry_.rec;
  now_ms=12000;
  for (int i=0;i<10;++i) record_timeout(0x40,CMD_DISPLAY_STATUS);
  assert(!rec.online && rec.miss_count==10 && rec.timeout_count==0); // WLAN startup
  rec={}; rec.online=true; rec.last_seen_ms=100;
  for (int i=0;i<5;++i) { now_ms=1000+i*200; record_timeout(0x40,CMD_DISPLAY_STATUS); }
  assert(rec.online && rec.timeout_count==0 && rec.miss_count==5); // handover
  now_ms=8099; record_timeout(0x40,CMD_DISPLAY_STATUS); assert(rec.online);
  now_ms=8100; record_timeout(0x40,CMD_DISPLAY_STATUS);
  assert(!rec.online && rec.timeout_count==1); // real loss still detected, even after the fifth miss
  for (int i=0;i<5;++i) record_timeout(0x40,CMD_DISPLAY_STATUS);
  assert(rec.timeout_count==1);
  rec.online=true; rec.last_seen_ms=now_ms; rec.consecutive_timeouts=0;
  now_ms+=8000;
  for (int i=0;i<5;++i) record_timeout(0x40,CMD_DISPLAY_STATUS);
  assert(rec.timeout_count==2);
  rec={}; rec.type=MODULE_FAN_IO; rec.online=true; now_ms=100;
  for (int i=0;i<4;++i) record_timeout(0x40,CMD_FAST_POLL);
  assert(rec.online); record_timeout(0x40,CMD_FAST_POLL);
  assert(!rec.online && rec.timeout_count==1); // other modules keep their timing
  rec={}; rec.online=true; rec.last_seen_ms=UINT32_MAX-3000; now_ms=3000;
  for (int i=0;i<5;++i) record_timeout(0x40,CMD_DISPLAY_STATUS);
  assert(rec.online); now_ms=5000; record_timeout(0x40,CMD_DISPLAY_STATUS);
  assert(!rec.online && rec.timeout_count==1); // millis rollover
  rec.online=true; rec.last_seen_ms=0; now_ms=9000; rec.timeout_count=UINT16_MAX;
  record_timeout(0x40,CMD_DISPLAY_STATUS); assert(rec.timeout_count==UINT16_MAX);
  record_timeout(0x41,CMD_DISPLAY_STATUS); // unknown address
  puts("PASS: production JBC station lists, stable USB/FAE labels, screensaver rotation, malformed frames, startup/handover/loss accounting and rollover.");
}
