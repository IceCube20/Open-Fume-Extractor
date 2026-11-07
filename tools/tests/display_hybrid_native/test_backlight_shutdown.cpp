#include "../../../Module/DisplayModule_320x480/src/OfeDisplayBacklight.h"
#include <assert.h>

int main() {
  OfeDisplayBacklight light;
  assert(light.mode() == 0 && light.minutes() == 5 && !light.isOff());
  light.tick(100, false, false);
  assert(!light.tick(4000000, true, false) && !light.isOff());
  assert(!light.configure(3, 5) && !light.configure(2, 0) && !light.configure(2, 61));
  assert(light.settings() == (5U << 8));

  assert(light.configure(2, 1));
  light.tick(1000, false, false);
  assert(!light.tick(60999, false, false));
  assert(light.tick(61000, false, false) && light.isOff());
  assert(light.wake(62000) && !light.isOff());
  assert(!light.tick(121999, false, false));
  assert(light.tick(122000, false, false));
  light.requestWake();
  assert(light.tick(122001, false, false) && !light.isOff());

  // Repeated unchanged configuration does not postpone the timeout.
  assert(light.configure(2, 1));
  assert(light.tick(182001, false, false) && light.isOff());
  assert(light.tick(182002, false, true) && !light.isOff()); // OTA/setup
  assert(!light.tick(240000, false, true));
  assert(!light.tick(299999, false, false));
  assert(light.tick(300000, false, false));

  assert(light.configure(1, 1));
  light.tick(400000, false, false);
  assert(!light.tick(460000, false, false) && !light.isOff());
  assert(!light.tick(460001, true, false) && !light.isOff());
  assert(!light.tick(520000, true, false));
  assert(light.tick(520001, true, false) && light.isOff());
  assert(light.tick(520002, false, false) && !light.isOff());

  // A long normal/extraction interval must not consume the next Eco delay.
  light.configure(1, 60);
  light.tick(600000, true, false);
  assert(light.tick(4200000, true, false) && light.isOff());
  assert(light.tick(4200001, false, false) && !light.isOff());
  assert(!light.tick(8000000, false, false));
  assert(!light.tick(8060000, true, false) && !light.isOff());
  assert(!light.tick(11659999, true, false));
  assert(light.tick(11660000, true, false) && light.isOff());

  // Always mode keeps its timeout independent of Eco transitions.
  light.configure(2, 1);
  light.tick(12000000, false, false);
  assert(!light.tick(12030000, true, false));
  assert(light.tick(12060000, false, false) && light.isOff());

  OfeDisplayBacklight wrapped;
  wrapped.configure(2, 1);
  const uint32_t start = 0xFFFFFF00U;
  wrapped.tick(start, false, false);
  assert(!wrapped.tick(uint32_t(start + 59999U), false, false));
  assert(wrapped.tick(uint32_t(start + 60000U), false, false) && wrapped.isOff());
  assert(!wrapped.tick(start, false, false) && wrapped.isOff()); // another complete wrap while off
  wrapped.configure(0, 1);
  assert(wrapped.tick(100000, true, false) && !wrapped.isOff());
  return 0;
}
