#pragma once
#include "OfeJbcActivityUi.h"
#include "OfeFanIcon.h"

namespace ofe_home_activity {
struct Counts { unsigned work = 0, stand = 0; bool known = true; };
struct OutputState { bool capable = false, active = false; unsigned percent = 0; };
inline unsigned bits(uint8_t mask) {
  unsigned count = 0;
  for (; mask; mask >>= 1) count += mask & 1U;
  return count;
}
struct Fan { lv_obj_t* image = nullptr; bool active = false, running = false; unsigned percent = 0, period = 0; };
static Fan fans[22]; // Extractor, legacy tiles and up to 17 addressed module cards.
static Fan saver_fan;
static bool saver_lit = true;
static lv_obj_t *work_image = nullptr, *stand_image = nullptr, *work_count = nullptr, *stand_count = nullptr;
inline void rotate(void* image, int32_t angle) { lv_image_set_rotation(static_cast<lv_obj_t*>(image), angle); }
inline unsigned fan_period(unsigned percent) { return 6000U - (percent > 100U ? 100U : percent) * 45U; }
inline void prepare_fan(lv_obj_t* image) {
  lv_image_set_src(image, &fan_icon);
  lv_obj_set_size(image, 21, 21); lv_obj_set_style_pad_all(image, 0, 0);
  // Odd-sized raster: the SVG hub and LVGL's integer pivot coincide exactly.
  lv_image_set_pivot(image, 10, 10); lv_image_set_inner_align(image, LV_IMAGE_ALIGN_TOP_LEFT);
  lv_image_set_antialias(image, true); lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
}
inline void animate(Fan& fan, bool visible) {
  if (!fan.image) return;
  const bool running = fan.active && visible;
  const unsigned period = fan_period(fan.percent);
  if (fan.running == running && (!running || fan.period == period)) return;
  const int angle = lv_image_get_rotation(fan.image);
  if (fan.running) lv_anim_delete(fan.image, rotate);
  fan.running = running;
  fan.period = period;
  if (!running) {
    lv_anim_delete(fan.image, rotate);
    lv_image_set_rotation(fan.image, 0);
    return;
  }
  lv_anim_t animation;
  lv_anim_init(&animation); lv_anim_set_var(&animation, fan.image);
  lv_anim_set_exec_cb(&animation, rotate); lv_anim_set_values(&animation, angle, angle + 3600);
  lv_anim_set_path_cb(&animation, lv_anim_path_linear);
  lv_anim_set_duration(&animation, period); lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&animation);
}
inline void visibility(lv_event_t* event) {
  const bool visible = lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED;
  for (auto& fan : fans) animate(fan, visible);
}
inline uint32_t accent(unsigned index) {
  // Module accents match WebStatus.inc.h; the extractor keeps its status green.
  const uint32_t colors[] = {0x2DFF88, 0x4A90D9, 0x55B9CA, 0xF1B84B, 0xE28B52};
  return colors[index];
}
inline void set_fan(unsigned index, bool enabled, bool active, unsigned percent = 100, uint32_t color = 0) {
  Fan& fan = fans[index];
  if (!fan.image) return;
  lv_obj_set_flag(fan.image, LV_OBJ_FLAG_HIDDEN, !enabled);
  fan.percent = percent > 100 ? 100 : percent;
  fan.active = enabled && active && fan.percent;
  const lv_color_t tint = fan.active ? lv_color_hex(color ? color : accent(index < 5 ? index : 0)) : ofe_display_ui::muted();
  if (!lv_color_eq(lv_obj_get_style_image_recolor(fan.image, LV_PART_MAIN), tint))
    lv_obj_set_style_image_recolor(fan.image, tint, 0);
  animate(fan, lv_obj_is_visible(fan.image) && lv_obj_get_screen(fan.image) == lv_screen_active());
}
inline void create(lv_obj_t* screen, lv_obj_t* jbc, lv_obj_t* iron, lv_obj_t* station,
                   lv_obj_t* output, lv_obj_t* fan_io, lv_obj_t* fan_title, lv_obj_t* weller) {
  work_image = iron;
  lv_obj_set_pos(iron, 76, 8);
  lv_obj_set_pos(station, 26, 35); lv_obj_set_width(station, lv_obj_get_width(jbc) - 50);
  lv_obj_set_height(station, 18); lv_label_set_long_mode(station, LV_LABEL_LONG_DOT);
  stand_image = lv_image_create(jbc); lv_obj_set_pos(stand_image, 140, 8); lv_obj_set_size(stand_image, 20, 20);
  work_count = lv_label(jbc, "0", 102, 11, ofe_display_ui::muted(), UI_FONT_DEFAULT, 32);
  stand_count = lv_label(jbc, "0", 166, 11, ofe_display_ui::muted(), UI_FONT_DEFAULT, 32);
  lv_obj_t* title = lv_obj_get_child(jbc, 1);
  lv_set_text_c(title, "JBC"); lv_obj_set_width(title, 40);
  lv_obj_t* cards[] = {output, fan_io, weller};
  for (unsigned i = 0; i < 3; ++i) {
    fans[i] = {};
    fans[i].image = lv_image_create(cards[i]);
    prepare_fan(fans[i].image);
    lv_obj_set_pos(fans[i].image, i ? 26 : 16, i ? 8 : 6);
    lv_obj_t* title = i == 1 ? fan_title : lv_obj_get_child(cards[i], i == 2 ? 1 : 0);
    lv_obj_set_x(title, lv_obj_get_x(title) + 28); lv_obj_set_width(title, lv_obj_get_width(title) + (i ? 8 : -28));
    set_fan(i, true, false);
  }
  lv_obj_add_event_cb(screen, visibility, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_obj_add_event_cb(screen, visibility, LV_EVENT_SCREEN_UNLOAD_START, nullptr);
}
inline void set_counts(Counts counts) {
  if (!work_count) return;
  ofe_jbc_activity_ui::update(work_image, ofe_jbc_activity_ui::WORK, counts.known && counts.work ? "On" : "Off");
  ofe_jbc_activity_ui::update(stand_image, ofe_jbc_activity_ui::STAND, counts.known && counts.stand ? "On" : "Off");
  char text[12];
  if (counts.known) snprintf(text, sizeof(text), "%u", counts.work); else strcpy(text, "--");
  lv_set_text_c(work_count, text);
  if (counts.known) snprintf(text, sizeof(text), "%u", counts.stand); else strcpy(text, "--");
  lv_set_text_c(stand_count, text);
  lv_obj_set_style_text_color(work_count, lv_obj_get_style_image_recolor(work_image, LV_PART_MAIN), 0);
  lv_obj_set_style_text_color(stand_count, lv_obj_get_style_image_recolor(stand_image, LV_PART_MAIN), 0);
}
inline void saver_visibility(lv_event_t* event) {
  animate(saver_fan, saver_lit && lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED);
}
inline void create_saver(lv_obj_t* screen, lv_obj_t* power) {
  saver_fan = {};
  saver_fan.image = lv_image_create(screen);
  prepare_fan(saver_fan.image);
  lv_obj_set_pos(saver_fan.image, lv_obj_get_style_x(power, LV_PART_MAIN) - 28, lv_obj_get_style_y(power, LV_PART_MAIN) - 3);
  lv_obj_set_style_image_recolor(saver_fan.image, ofe_display_ui::muted(), 0);
  lv_obj_set_style_image_recolor_opa(saver_fan.image, LV_OPA_COVER, 0);
  lv_obj_remove_flag(saver_fan.image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, saver_visibility, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_obj_add_event_cb(screen, saver_visibility, LV_EVENT_SCREEN_UNLOAD_START, nullptr);
}
inline void update_saver(bool active, bool lit, unsigned percent = 100) {
  percent = percent > 100 ? 100 : percent;
  active = active && percent;
  if (!saver_fan.image || (saver_fan.active == active && saver_lit == lit && saver_fan.percent == percent)) return;
  saver_fan.active = active; saver_lit = lit; saver_fan.percent = percent;
  lv_obj_set_style_image_recolor(saver_fan.image, active ? lv_color_hex(0x55D98A) : ofe_display_ui::muted(), 0);
  animate(saver_fan, lit && lv_obj_get_screen(saver_fan.image) == lv_screen_active());
}
inline void move_saver(lv_obj_t* power) {
  auto* image = saver_fan.image;
  const int x = lv_obj_get_style_x(power, LV_PART_MAIN) - 28, y = lv_obj_get_style_y(power, LV_PART_MAIN) - 3;
  if (lv_obj_get_style_x(image, LV_PART_MAIN) != x || lv_obj_get_style_y(image, LV_PART_MAIN) != y) lv_obj_set_pos(image, x, y);
}
}
