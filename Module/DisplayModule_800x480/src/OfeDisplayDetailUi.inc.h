#pragma once
#include "OfeJbcActivityUi.h"

static lv_obj_t* ui_detail_value_grid = nullptr;
static bool detail_value_grid_jbc = false;
static void lv_detail_text_slice(lv_obj_t* label, const char* text, size_t length) {
  const char* previous = lv_label_get_text(label);
  if (strlen(previous) != length || memcmp(previous, text, length))
    lv_label_set_text_fmt(label, "%.*s", int(length), text);
}
static void lv_detail_values_reset() {
  ui_detail_value_grid = nullptr;
}
static void lv_detail_values_hide() {
  if (ui_detail_value_grid) lv_obj_add_flag(ui_detail_value_grid, LV_OBJ_FLAG_HIDDEN);
}
static int lv_detail_values_bottom() {
  lv_obj_t* values = ui_detail_value_grid && !lv_obj_has_flag(ui_detail_value_grid, LV_OBJ_FLAG_HIDDEN)
    ? ui_detail_value_grid : ui_detail_values;
  lv_obj_update_layout(values);
  return lv_obj_get_y(values) + lv_obj_get_height(values);
}
static void lv_detail_values_view(const String& values, bool jbc) {
  using namespace ofe_display_ui;
  const bool unchanged = ui_detail_value_grid && detail_value_grid_jbc == jbc &&
    !strcmp(lv_label_get_text(ui_detail_values), values.c_str());
  lv_set_text(ui_detail_values, values);
  lv_obj_add_flag(ui_detail_values, LV_OBJ_FLAG_HIDDEN);
  detail_value_grid_jbc = jbc;
  if (!ui_detail_value_grid) {
    lv_obj_update_layout(lv_obj_get_parent(ui_detail_values));
    ui_detail_value_grid = lv_obj_create(lv_obj_get_parent(ui_detail_values));
    unframed(ui_detail_value_grid);
    lv_obj_remove_flag(ui_detail_value_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui_detail_value_grid, 14, 72);
    lv_obj_set_width(ui_detail_value_grid, lv_obj_get_width(lv_obj_get_parent(ui_detail_values)) - 28);
  }
  lv_obj_remove_flag(ui_detail_value_grid, LV_OBJ_FLAG_HIDDEN);
  if (unchanged) return;
  lv_obj_update_layout(ui_detail_value_grid);
  const int width = lv_obj_get_width(ui_detail_value_grid), row_width = (width - GAP) / 2;
  unsigned index = 0;
  int y = 0, line_height = 0;
  for (const char* start = values.c_str(); *start;) {
    const char* end = strchr(start, '\n');
    if (!end) end = start + strlen(start);
    const char* group = strstr(start, "   ");
    const bool grouped = group && group < end;
    if (grouped) end = group;
    const char* next = end + (*end ? (grouped ? 3 : 1) : 0);
    while (start < end && *start == ' ') ++start;
    while (end > start && end[-1] == ' ') --end;
    if (start != end) {
      lv_obj_t* row = lv_obj_get_child(ui_detail_value_grid, index);
      if (!row) {
        row = lv_obj_create(ui_detail_value_grid);
        flat(row, surface());
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_label_create(row); lv_label_create(row);
        auto* icon = lv_image_create(row); lv_obj_set_size(icon, 20, 20);
      }
      lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
      auto* label = lv_obj_get_child(row, 0);
      auto* value = lv_obj_get_child(row, 1);
      auto* icon = lv_obj_get_child(row, 2);
      const char* colon = static_cast<const char*>(memchr(start, ':', size_t(end - start)));
      const char* value_start = colon ? colon + 1 : start;
      while (value_start < end && *value_start == ' ') ++value_start;
      lv_detail_text_slice(label, start, colon ? size_t(colon - start) : 0);
      lv_detail_text_slice(value, value_start, size_t(end - value_start));
      const auto activity = jbc ? ofe_jbc_activity_ui::kind(lv_label_get_text(label)) : ofe_jbc_activity_ui::NONE;
      ofe_jbc_activity_ui::update(icon, activity, lv_label_get_text(value));
      const int inset = activity == ofe_jbc_activity_ui::NONE ? 8 : 36;
      lv_obj_set_pos(icon, 8, 8);
      lv_obj_set_pos(label, inset, 8); lv_obj_set_width(label, row_width - inset - 8);
      lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(label, UI_FONT_DEFAULT, 0);
      lv_obj_set_style_text_color(label, muted(), 0);
      lv_obj_set_width(value, row_width - 16);
      lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(value, UI_FONT_DEFAULT, 0);
      lv_obj_set_style_text_color(value, text(), 0);
      lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_update_layout(row);
      const int value_y = colon ? lv_obj_get_height(label) + 12 : 8;
      lv_obj_set_pos(value, 8, value_y);
      const int height = (value_y + lv_obj_get_height(value) + 11) & ~3;
      lv_obj_set_pos(row, (index % 2) * (row_width + GAP), y);
      lv_obj_set_size(row, row_width, height < 52 ? 52 : height);
      if (lv_obj_get_style_height(row, LV_PART_MAIN) > line_height) line_height = lv_obj_get_style_height(row, LV_PART_MAIN);
      if (++index % 2 == 0) { y += line_height + GAP; line_height = 0; }
    }
    start = next;
  }
  for (unsigned i = index; i < lv_obj_get_child_count(ui_detail_value_grid); ++i)
    lv_obj_add_flag(lv_obj_get_child(ui_detail_value_grid, i), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(ui_detail_value_grid, y + line_height);
}
