#pragma once
#include <lvgl.h>

#ifndef OFE_SMALL_REFRESH_MS
#define OFE_SMALL_REFRESH_MS 16
#endif
#ifndef OFE_SMALL_TOUCH_MS
#define OFE_SMALL_TOUCH_MS 16
#endif
static_assert(OFE_SMALL_REFRESH_MS > 0 && OFE_SMALL_TOUCH_MS > 0,
              "Small-display timer periods must be positive");

namespace ofe_small_display {
inline void configureTiming(lv_display_t* display, lv_indev_t* input) {
  // The GUI task's 5 ms service interval does not change these LVGL timers.
  if (display) {
    if (auto* timer = lv_display_get_refr_timer(display))
      lv_timer_set_period(timer, OFE_SMALL_REFRESH_MS);
  }
  if (input) {
    lv_indev_set_display(input, display);
    if (auto* timer = lv_indev_get_read_timer(input))
      lv_timer_set_period(timer, OFE_SMALL_TOUCH_MS);
  }
}
}
