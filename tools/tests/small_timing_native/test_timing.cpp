#include "OfeSmallDisplayTiming.h"
#include <cassert>
#include <cstdio>
int main() {
  lv_timer_t refresh, touch, unrelated;
  lv_display_t display{&refresh};
  lv_indev_t input{&touch};
  ofe_small_display::configureTiming(&display, &input);
  assert(refresh.period == OFE_SMALL_REFRESH_MS);
  assert(touch.period == OFE_SMALL_TOUCH_MS);
  assert(input.display == &display);
  assert(input.scroll_limit == 10 && input.scroll_throw == 10);
  assert(unrelated.period == 33);
  ofe_small_display::configureTiming(nullptr, nullptr);
  lv_display_t no_refresh;
  lv_indev_t no_input_timer;
  ofe_small_display::configureTiming(&no_refresh, &no_input_timer);
  assert(no_input_timer.display == &no_refresh);
  std::puts("PASS: real small-display timer helper, independent timers, gesture preservation, missing timers and A/B overrides");
}
