#include <cassert>
#include <cstdio>
#include "OfeDisplayOtaTiming.h"

int main() {
  static_assert(sizeof(OfeDisplayOtaTiming)<=64, "OTA timing must not consume a large internal buffer");
  OfeDisplayOtaTiming timing;
  timing.poll(0); timing.poll(2000); timing.poll(63000);
  timing.received(); timing.checked(45,true); timing.handled(62000,false); timing.acknowledged(200,true);
  timing.received(); timing.checked(25,true); timing.handled(1,true); timing.acknowledged(300,false);
  timing.received(); timing.checked(80,false);
  auto s=timing.snapshot();
  assert(s.packets==3 && s.duplicates==1 && s.rejected==1 && s.ack_failures==1);
  assert(s.poll_max_us==61000 && s.check_max_us==80 && s.handle_max_us==62000 && s.ack_max_us==300);
  timing.reset(); s=timing.snapshot();
  assert(s.packets==0 && s.duplicates==0 && s.rejected==0 && s.ack_failures==0);
  assert(!s.poll_max_us && !s.check_max_us && !s.handle_max_us && !s.ack_max_us);
  timing.poll(0xfffffff0U); timing.poll(0x20U);
  assert(timing.snapshot().poll_max_us==48);
  puts("PASS: actual WLAN OTA stage counters, no large allocation, session reset and microsecond rollover.");
}
