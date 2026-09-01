#include "../../../Module/DisplayModule_800x480/src/OfeLvProfileCounters.h"
#include <cassert>
#include <cstdio>

static unsigned begins, ends;
extern "C" void ofe_lv_profile_begin(unsigned id) { assert(id == OFE_LV_DRAW); ++begins; }
extern "C" void ofe_lv_profile_end(unsigned id) { assert(id == OFE_LV_DRAW); ++ends; }

int main() {
  OfeLvProfileCounters p;
  OfeLvProfileSample samples[OFE_LV_PROFILE_COUNT] = {};
  uint32_t errors = 0;
  p.begin(OFE_LV_RENDER, 100);
  p.begin(OFE_LV_DRAW, 110);
  p.begin(OFE_LV_TEXT, 120);
  p.begin(OFE_LV_TEXT, 125); // recursion is not counted twice
  p.end(OFE_LV_TEXT, 135);
  assert(!p.take(samples, &errors)); // no partial/reset snapshot
  p.end(OFE_LV_TEXT, 150);
  p.end(OFE_LV_DRAW, 190);
  p.end(OFE_LV_RENDER, 200);
  assert(p.take(samples, &errors));
  assert(errors == 0);
  assert(samples[OFE_LV_RENDER].us == 100);
  assert(samples[OFE_LV_DRAW].us == 80);
  assert(samples[OFE_LV_TEXT].us == 30 && samples[OFE_LV_TEXT].calls == 1);

  p.begin(OFE_LV_INPUT, UINT32_MAX - 9);
  p.end(OFE_LV_INPUT, 10); // microsecond counter rollover
  p.begin(OFE_LV_INPUT, 50);
  p.end(OFE_LV_INPUT, 55);
  p.end(OFE_LV_INPUT, 56); // unmatched end is observable
  p.begin(OFE_LV_PROFILE_COUNT, 60); // unknown hooks ignored
  p.end(OFE_LV_PROFILE_COUNT, 70);
  assert(p.take(samples, &errors) && errors == 1);
  assert(samples[OFE_LV_INPUT].us == 25 && samples[OFE_LV_INPUT].calls == 2);
  assert(samples[OFE_LV_INPUT].max_us == 20);
  assert(samples[OFE_LV_DRAW].us == 0);
  assert(p.take(samples, &errors) && errors == 0);
  assert(samples[OFE_LV_INPUT].calls == 0);

  OFE_LV_PROFILE_BEGIN_TAG("execute_drawing");
  OFE_LV_PROFILE_END_TAG("execute_drawing");
  OFE_LV_PROFILE_BEGIN_TAG("unselected_hot_function");
  OFE_LV_PROFILE_END_TAG("unselected_hot_function");
  assert(begins == 1 && ends == 1);
  assert(OFE_LV_PROFILE_ID("lv_obj_redraw") == OFE_LV_OBJECTS);
  assert(OFE_LV_PROFILE_ID("layout") == OFE_LV_LAYOUT);
  puts("LVGL profile: inclusive nesting, reset, rollover, invalid hooks and filtering passed");
}
