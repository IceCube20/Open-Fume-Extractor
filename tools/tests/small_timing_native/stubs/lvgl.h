#pragma once
#include <cstdint>
struct lv_timer_t { uint32_t period = 33; };
struct lv_display_t { lv_timer_t* timer = nullptr; };
struct lv_indev_t {
  lv_timer_t* timer = nullptr;
  lv_display_t* display = nullptr;
  int scroll_limit = 10, scroll_throw = 10;
};
inline lv_timer_t* lv_display_get_refr_timer(lv_display_t* display) { return display->timer; }
inline lv_timer_t* lv_indev_get_read_timer(lv_indev_t* input) { return input->timer; }
inline void lv_indev_set_display(lv_indev_t* input, lv_display_t* display) { input->display = display; }
inline void lv_timer_set_period(lv_timer_t* timer, uint32_t period) { timer->period = period; }
