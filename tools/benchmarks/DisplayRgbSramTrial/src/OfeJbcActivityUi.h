#pragma once
#include "OfeDisplayUi.h"
#include "../SolderIronIcon.h"
#include "OfeParkingIcon.h"

namespace ofe_jbc_activity_ui {
enum Kind : uint8_t { NONE, WORK, STAND };
inline bool fae_stand(bool linked, uint8_t work) { return linked && !work; }
inline Kind kind(const char* label) {
  return !strcmp(label, "Work") ? WORK : !strcmp(label, "Stand") ? STAND : NONE;
}
inline void update(lv_obj_t* image, Kind activity, const char* value) {
  lv_obj_set_flag(image, LV_OBJ_FLAG_HIDDEN, activity == NONE);
  if (activity == NONE) return;
  const lv_image_dsc_t* source = activity == WORK ? &solder_iron_icon : &parking_icon;
  if (lv_image_get_src(image) != source) lv_image_set_src(image, source);
  const bool active = !strcmp(value, "on") || !strcmp(value, "an") ||
    !strcmp(value, "On") || !strcmp(value, "An") || !strcmp(value, "ON") || !strcmp(value, "AN");
  const lv_color_t color = !active ? ofe_display_ui::muted() : activity == STAND
    ? lv_color_hex(0xFFB020) : ui_theme_color(0x2DFF88, 0x146B3A);
  if (!lv_color_eq(lv_obj_get_style_image_recolor(image, LV_PART_MAIN), color))
    lv_obj_set_style_image_recolor(image, color, 0);
  lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
}
}
