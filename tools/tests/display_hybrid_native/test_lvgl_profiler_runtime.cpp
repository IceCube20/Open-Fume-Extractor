#include <cassert>
#include <cstdio>
#include "../../../Module/DisplayModule_800x480/ofe_lv_profiler.h"
static int64_t clock_us=0;
static unsigned timer_reads=0;
extern "C" int64_t esp_timer_get_time(void) { ++timer_reads; return clock_us; }
int main() {
  OfeLvProfileSample s[OFE_LV_PROFILE_COUNT]{};
  uint32_t errors=0;
  assert(!ofe_lv_profile_enabled);
  OFE_LV_PROFILE_BEGIN_TAG("lv_display_refr_timer");
  OFE_LV_PROFILE_END_TAG("lv_display_refr_timer");
  ofe_lv_profile_begin(OFE_LV_DRAW); ofe_lv_profile_end(OFE_LV_DRAW);
  assert(!timer_reads);  // Ordinary use and OTA do not sample the clock.
  ofe_lv_profile_start();
  OFE_LV_PROFILE_BEGIN_TAG("unselected_hot_function");
  OFE_LV_PROFILE_END_TAG("unselected_hot_function");
  assert(!timer_reads);
  clock_us=100; OFE_LV_PROFILE_BEGIN_TAG("lv_display_refr_timer");
  clock_us=110; OFE_LV_PROFILE_BEGIN_TAG("layout");
  assert(!ofe_lv_profile_take(s,&errors));
  clock_us=120; OFE_LV_PROFILE_END_TAG("layout");
  clock_us=130; OFE_LV_PROFILE_BEGIN_TAG("lv_draw_sw_label");
  clock_us=140; OFE_LV_PROFILE_BEGIN_TAG("lv_draw_sw_blend");
  clock_us=150; OFE_LV_PROFILE_END_TAG("lv_draw_sw_blend");
  clock_us=160; OFE_LV_PROFILE_END_TAG("lv_draw_sw_label");
  clock_us=200; OFE_LV_PROFILE_END_TAG("lv_display_refr_timer");
  assert(ofe_lv_profile_stop(s,&errors) && !errors && !ofe_lv_profile_enabled);
  assert(s[OFE_LV_REFRESH].us==100 && s[OFE_LV_LAYOUT].us==10);
  assert(s[OFE_LV_TEXT].us==30 && s[OFE_LV_BLEND].us==10);
  unsigned stopped_reads=timer_reads;
  OFE_LV_PROFILE_BEGIN_TAG("execute_drawing"); OFE_LV_PROFILE_END_TAG("execute_drawing");
  assert(timer_reads==stopped_reads);
  ofe_lv_profile_start();
  clock_us=1000; OFE_LV_PROFILE_BEGIN_TAG("lv_indev_read");
  ofe_lv_profile_cancel();  // A cancel callback can be inside the input hook.
  OFE_LV_PROFILE_END_TAG("lv_indev_read");
  assert(ofe_lv_profile_take(s,&errors) && !errors && !s[OFE_LV_INPUT].calls);
  ofe_lv_profile_start();
  clock_us=UINT32_MAX-9LL; OFE_LV_PROFILE_BEGIN_TAG("lv_indev_read");
  clock_us=UINT32_MAX+11LL; OFE_LV_PROFILE_END_TAG("lv_indev_read");
  assert(ofe_lv_profile_stop(s,&errors) && !errors && s[OFE_LV_INPUT].us==20);
  ofe_lv_profile_start(); OFE_LV_PROFILE_END_TAG("execute_drawing");
  assert(ofe_lv_profile_stop(s,&errors) && errors==1);
  ofe_lv_profile_start();
  assert(ofe_lv_profile_stop(s,&errors) && !errors && !s[OFE_LV_INPUT].calls);
  puts("PASS: actual profiler lifecycle, inactive/no-clock overhead, filter, nested inclusive timing, partial snapshots, callback cancellation, clean restart and rollover");
}
