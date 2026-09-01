#include "OfeLvProfileCounters.h"
#include <esp_timer.h>

static OfeLvProfileCounters counters;

extern "C" void ofe_lv_profile_begin(unsigned id) {
  counters.begin(id, (uint32_t)esp_timer_get_time());
}

extern "C" void ofe_lv_profile_end(unsigned id) {
  counters.end(id, (uint32_t)esp_timer_get_time());
}

extern "C" int ofe_lv_profile_take(OfeLvProfileSample* samples, uint32_t* errors) {
  return counters.take(samples, errors);
}
