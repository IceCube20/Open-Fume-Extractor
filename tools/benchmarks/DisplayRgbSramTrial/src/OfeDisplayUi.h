#pragma once
// Shared prototype components for System and JBC USB details. No transport state.
namespace ofe_display_ui {
constexpr int16_t GAP = 8, MARGIN = 12, CONTENT_TOP = 48;
constexpr int16_t CONTROL_HEIGHT = 44, RADIUS = 6;
inline bool wide(int width) { return width >= 640; }
inline lv_color_t background() { return ui_theme_color(0x11171D, 0xF5F7F9); }
inline lv_color_t surface() { return ui_theme_color(0x1C252D, 0xEAF0F4); }
inline lv_color_t field() { return ui_theme_color(0x25313C, 0xFFFFFF); }
inline lv_color_t text() { return ui_theme_color(0xF5F8FA, 0x17212B); }
inline lv_color_t muted() { return ui_theme_color(0xA8B8C5, 0x526575); }
inline lv_color_t outline() { return ui_theme_color(0x425360, 0xB8C7D3); }
inline lv_color_t accent() { return ui_theme_color(0x58B8FF, 0x1756A9); }
inline void flat(lv_obj_t* obj, lv_color_t color, int radius = RADIUS) {
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, radius, 0);
}
inline void unframed(lv_obj_t* obj) {
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
}
inline lv_obj_t* content(lv_obj_t* screen) {
  lv_obj_update_layout(screen);
  flat(screen, background(), 0);
  lv_obj_t* body = lv_obj_create(screen);
  lv_obj_set_pos(body, MARGIN, CONTENT_TOP);
  lv_obj_set_size(body, lv_obj_get_width(screen) - 2 * MARGIN,
    lv_obj_get_height(screen) - CONTENT_TOP - 56);
  unframed(body);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);
  return body;
}
inline void skin_button(lv_obj_t* obj) {
  if (lv_obj_get_style_height(obj, LV_PART_MAIN) < CONTROL_HEIGHT) lv_obj_set_height(obj, CONTROL_HEIGHT);
  flat(obj, field());
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, outline(), 0);
  lv_obj_set_style_bg_color(obj, ui_theme_color(0x34506A, 0xD9EBFA), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(obj, ui_theme_color(0x182027, 0xE9EDF1), LV_STATE_DISABLED);
  lv_obj_t* label = lv_obj_get_child(obj, 0);
  lv_obj_set_style_text_color(label, text(), 0);
  lv_obj_set_style_text_font(label, UI_FONT_DEFAULT, 0);
  lv_obj_set_width(label, lv_obj_get_style_width(obj, LV_PART_MAIN) - 16);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}
inline lv_obj_t* button(lv_obj_t* parent, int x, int y, int width, const char* title, lv_event_cb_t callback) {
  lv_obj_t* obj = lv_small_button(parent, x, y, width, title, callback);
  lv_obj_set_height(obj, CONTROL_HEIGHT);
  skin_button(obj);
  return obj;
}
inline void popup(lv_obj_t* dropdown) {
  lv_obj_t* list = lv_dropdown_get_list(dropdown);
  if (!list) return;
  flat(list, field());
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_border_color(list, outline(), 0);
  lv_obj_set_style_text_font(list, UI_FONT_DEFAULT, 0);
  lv_obj_set_style_text_color(list, text(), 0);
  lv_obj_set_style_bg_color(list, ui_theme_color(0x246B9F, 0xD9ECFF), LV_PART_SELECTED);
  lv_obj_set_style_text_color(list, text(), LV_PART_SELECTED);
}
inline void navigation(lv_obj_t* screen, uint8_t active, const char* const* labels, const lv_event_cb_t* callbacks) {
  lv_obj_update_layout(screen);
  const int width = (lv_obj_get_width(screen) - 5 * GAP) / 4;
  for (uint8_t i = 0; i < 4; ++i) {
    lv_obj_t* obj = button(screen, GAP + i * (width + GAP), lv_obj_get_height(screen) - 48,
      width, labels[i], callbacks[i]);
    if (i == active) {
      lv_obj_set_style_bg_color(obj, ui_theme_color(0x246B9F, 0xD9ECFF), 0);
      lv_obj_set_style_border_color(obj, accent(), 0);
    }
  }
}
inline void dropdown_event(lv_event_t* event) {
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_READY || code == LV_EVENT_CLICKED || code == LV_EVENT_VALUE_CHANGED)
    popup(lv_event_get_target_obj(event));
}
inline void dropdown(lv_obj_t* obj) {
  flat(obj, field());
  lv_obj_set_height(obj, CONTROL_HEIGHT);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, outline(), 0);
  lv_obj_set_style_text_color(obj, text(), 0);
  lv_obj_set_style_text_font(obj, UI_FONT_DEFAULT, 0);
  lv_obj_set_style_bg_color(obj, ui_theme_color(0x34506A, 0xD9EBFA), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(obj, ui_theme_color(0x182027, 0xE9EDF1), LV_STATE_DISABLED);
  lv_obj_set_style_text_color(obj, muted(), LV_STATE_DISABLED);
  lv_obj_set_style_border_color(obj, accent(), LV_STATE_FOCUSED);
  popup(obj);
  lv_obj_add_event_cb(obj, dropdown_event, LV_EVENT_ALL, nullptr);
}
inline void toggle(lv_obj_t* obj) {
  flat(obj, field(), LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_color(obj, accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(obj, ui_theme_color(0xF5F8FA, 0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_ext_click_area(obj, 6);
}
inline void numeric_field(lv_obj_t* obj) {
  flat(obj, field());
  lv_obj_set_style_text_color(obj, accent(), 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const int pad = (CONTROL_HEIGHT - lv_font_get_line_height(UI_FONT_18)) / 2;
    lv_obj_set_style_pad_top(obj, pad, 0); lv_obj_set_style_pad_bottom(obj, pad, 0);
  }
}
inline int control_columns(int width) { return wide(width) ? 2 : 1; }
inline int control_grid_height(int width, unsigned count, int row_height) {
  const int columns = control_columns(width);
  return ((count + columns - 1) / columns) * row_height;
}
inline lv_obj_t* control_cell(lv_obj_t* parent, unsigned index, int row_height, int top = 0) {
  lv_obj_update_layout(parent);
  const int width = lv_obj_get_width(parent), columns = control_columns(width);
  const int cell_width = (width - (columns - 1) * GAP) / columns;
  auto* cell = lv_obj_create(parent);
  lv_obj_set_pos(cell, (index % columns) * (cell_width + GAP), top + (index / columns) * row_height);
  lv_obj_set_size(cell, cell_width, row_height - GAP);
  flat(cell, surface()); lv_obj_set_style_pad_all(cell, 0, 0);
  lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  return cell;
}
inline void keyboard(lv_obj_t* obj) {
  flat(obj, surface());
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_pad_row(obj, 4, 0);
  lv_obj_set_style_pad_column(obj, 4, 0);
  lv_obj_set_style_text_font(obj, UI_FONT_18, LV_PART_ITEMS);
  lv_obj_set_style_text_color(obj, text(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(obj, field(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(obj, ui_theme_color(0x34506A, 0xD9EBFA), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_radius(obj, RADIUS, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_ITEMS);
}
inline void labels(lv_obj_t* parent) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    lv_obj_t* obj = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(obj, &lv_label_class)) {
      const auto color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
      if (lv_color_eq(color, lv_color_hex(0x96A0AA)) || lv_color_eq(color, lv_color_hex(0x8F9BA8)) ||
          lv_color_eq(color, lv_color_hex(0x5E6D7C))) lv_obj_set_style_text_color(obj, muted(), 0);
      else if (lv_color_eq(color, lv_color_hex(0xFFFFFF)) || lv_color_eq(color, lv_color_hex(0xDDE4EC)) ||
               lv_color_eq(color, lv_color_hex(0x17212B))) lv_obj_set_style_text_color(obj, text(), 0);
    } else labels(obj);
  }
}
// Called once after constructing non-Home controls; never from a live-value refresh.
inline void controls(lv_obj_t* parent) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    lv_obj_t* obj = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(obj, &lv_button_class)) skin_button(obj);
    else if (lv_obj_check_type(obj, &lv_dropdown_class)) dropdown(obj);
    else if (lv_obj_check_type(obj, &lv_switch_class)) toggle(obj);
    else if (lv_obj_check_type(obj, &lv_textarea_class)) {
      flat(obj, field()); lv_obj_set_height(obj, CONTROL_HEIGHT);
      lv_obj_set_style_text_color(obj, text(), 0);
      const int pad = (CONTROL_HEIGHT - lv_font_get_line_height(UI_FONT_DEFAULT)) / 2;
      lv_obj_set_style_pad_top(obj, pad, 0); lv_obj_set_style_pad_bottom(obj, pad, 0);
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
      lv_obj_set_style_bg_color(obj, field(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(obj, accent(), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(obj, text(), LV_PART_KNOB);
    } else controls(obj);
  }
  labels(parent);
}
// Detail values follow their captions; Home and the numeric dialog opt out.
inline void detail_alignment(lv_obj_t* parent) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    auto* obj = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
      lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_pad_left(obj, 12, 0); lv_obj_set_style_pad_right(obj, 12, 0);
    } else if (lv_obj_check_type(obj, &lv_label_class)) lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
    else if (lv_obj_check_type(obj, &lv_dropdown_class)) lv_obj_set_style_pad_right(obj, 12, 0);
    else if (lv_obj_check_type(obj, &lv_obj_class)) detail_alignment(obj);
  }
}
}
