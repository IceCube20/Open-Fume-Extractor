#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "LocalBusDiagnostics.h"

static uint32_t now_ms=0, local_baud=9600, last_profile_poll_ms=0, profile_poll_pause_until_ms=0;
static uint8_t profile_entity_count=1, profile_poll_cursor=0, profile_readback_entity_id=0;
struct ProfileEntity {
  bool used=true, readable=true, response_pending=false;
  uint8_t id=20;
  char match[4]="OK", poll[4]="GET";
  uint32_t last_poll_ms=0, response_started_ms=0, poll_ms=100;
};
static ProfileEntity profile_entities[1];
static ofe_local_diag::State local_diagnostics;
static bool binary=false, build_ok=true;
static unsigned sends=0;
static uint32_t millis() {return now_ms;}
static bool is_binary_protocol() {return binary;}
static bool binary_build_template(const char*,int,uint8_t*,unsigned,uint8_t& len) {len=3;return build_ok;}
static void local_send_line(const uint8_t*,uint8_t) {++sends;}
static bool profile_entity_readable(const ProfileEntity& e) {return e.readable;}
static ProfileEntity* profile_find_entity(uint8_t id) {return id==20?profile_entities:nullptr;}
#include "universal_poll.inc.h"

int main() {
  for(uint32_t start:{0U,1000U,0xFFFFFF00U}) {
    ofe_local_diag::State state;
    state.timeout.fail(start);state.timeout.fail(start+1);
    assert(state.faults(start+1)==0);
    state.timeout.fail(start+2);assert(state.faults(start+2)==0x0800);
    state.response(start+3);state.response(start+4);assert(state.faults(start+4)==0x0800);
    state.response(start+5);assert(!state.faults(start+5));
    state.checksum.fail(start);state.checksum.fail(start+1);state.checksum.fail(start+2);
    assert(state.faults(start+2)==0x1000);
    assert(!state.faults(start+30002));
    for(unsigned i=0;i<3;++i)state.response(start+i,2);
    assert(state.faults(start+2)==0x2000);
    state.reset();assert(!state.faults(start+3));
    state.timeout.fail(start);state.timeout.fail(start+10001);state.timeout.fail(start+10002);
    assert(!state.faults(start+10002)); // Old isolated error is outside the window.
  }
  auto& e=profile_entities[0];
  now_ms=100;profile_poll_tick();assert(sends==1&&e.response_pending);
  now_ms=200;profile_poll_tick();assert(e.response_started_ms==100);
  now_ms=1099;profile_poll_tick();assert(!local_diagnostics.timeout.failures);
  now_ms=1100;profile_poll_tick();assert(local_diagnostics.timeout.failures==1);
  e.response_pending=false;e.match[0]=0;now_ms=1200;profile_poll_tick();assert(!e.response_pending);
  std::strcpy(e.match,"OK");binary=true;build_ok=false;now_ms=1300;profile_poll_tick();assert(!e.response_pending);
  build_ok=true;now_ms=1400;profile_poll_tick();assert(e.response_pending);
  e.readable=false;e.response_pending=false;now_ms=1500;profile_poll_tick();assert(!e.response_pending);
  local_baud=300;assert(local_response_timeout_ms()>7000);
  local_baud=9600;assert(local_response_timeout_ms()==1000);
  std::puts("PASS: diagnostic debounce/recovery/expiry/rollover, actual Universal pending polls, fixed deadlines, passive/WO and failed-template exclusions");
}
