#ifndef OFE_LV_PROFILER_H
#define OFE_LV_PROFILER_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  OFE_LV_REFRESH, OFE_LV_LAYOUT, OFE_LV_RENDER, OFE_LV_DRAW,
  OFE_LV_TEXT, OFE_LV_BLEND, OFE_LV_OBJECTS, OFE_LV_INPUT,
  OFE_LV_PROFILE_COUNT
};

typedef struct {
  uint64_t us;
  uint32_t calls;
  uint32_t max_us;
} OfeLvProfileSample;

void ofe_lv_profile_begin(unsigned id);
void ofe_lv_profile_end(unsigned id);
int ofe_lv_profile_take(OfeLvProfileSample* samples, uint32_t* errors);

#ifdef __cplusplus
}
#endif

// LVGL passes string literals / __func__: GCC folds unselected hooks away.
#define OFE_LV_PROFILE_ID(tag) \
  (!strcmp(tag, "lv_display_refr_timer") ? OFE_LV_REFRESH : \
   !strcmp(tag, "layout") ? OFE_LV_LAYOUT : \
   !strcmp(tag, "refr_invalid_areas") ? OFE_LV_RENDER : \
   !strcmp(tag, "execute_drawing") ? OFE_LV_DRAW : \
   (!strcmp(tag, "lv_draw_sw_label") || !strcmp(tag, "lv_draw_sw_letter")) ? OFE_LV_TEXT : \
   !strcmp(tag, "lv_draw_sw_blend") ? OFE_LV_BLEND : \
   !strcmp(tag, "lv_obj_redraw") ? OFE_LV_OBJECTS : \
   !strcmp(tag, "lv_indev_read") ? OFE_LV_INPUT : OFE_LV_PROFILE_COUNT)

#define OFE_LV_PROFILE_BEGIN_TAG(tag) do { \
  const unsigned id = OFE_LV_PROFILE_ID(tag); \
  if (id < OFE_LV_PROFILE_COUNT) ofe_lv_profile_begin(id); \
} while (0)
#define OFE_LV_PROFILE_END_TAG(tag) do { \
  const unsigned id = OFE_LV_PROFILE_ID(tag); \
  if (id < OFE_LV_PROFILE_COUNT) ofe_lv_profile_end(id); \
} while (0)

#endif
