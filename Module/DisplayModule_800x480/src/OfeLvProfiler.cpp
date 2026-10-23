#include "OfeLvProfileCounters.h"
#include <esp_timer.h>

static OfeLvProfileCounters counters;
extern "C" {
uint8_t ofe_lv_profile_enabled = 0;
}

extern "C" void ofe_lv_profile_start(void) {
  counters.reset();
  ofe_lv_profile_enabled = 1;
}

extern "C" void ofe_lv_profile_cancel(void) {
  ofe_lv_profile_enabled = 0;
  counters.reset();
}

extern "C" void ofe_lv_profile_begin(unsigned id) {
  if (!ofe_lv_profile_enabled) return;
  counters.begin(id, (uint32_t)esp_timer_get_time());
}

extern "C" void ofe_lv_profile_end(unsigned id) {
  if (!ofe_lv_profile_enabled) return;
  counters.end(id, (uint32_t)esp_timer_get_time());
}

extern "C" int ofe_lv_profile_take(OfeLvProfileSample* samples, uint32_t* errors) {
  return counters.take(samples, errors);
}

extern "C" int ofe_lv_profile_stop(OfeLvProfileSample* samples, uint32_t* errors) {
  ofe_lv_profile_enabled = 0;
  return counters.take(samples, errors);
}
