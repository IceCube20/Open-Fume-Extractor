#include "ofe_lv_profiler.h"
void unselected_hot_function(void) {
  OFE_LV_PROFILE_BEGIN_TAG(__func__);
  OFE_LV_PROFILE_END_TAG(__func__);
}
void lv_draw_sw_label(void) {
  OFE_LV_PROFILE_BEGIN_TAG(__func__);
  OFE_LV_PROFILE_END_TAG(__func__);
}
