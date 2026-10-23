#pragma once
#include "OfeDisplayUi.h"
namespace ofe_display_ui {
struct NumericDialog { lv_obj_t *overlay, *input, *error, *keyboard; };
inline NumericDialog numeric_dialog(lv_obj_t* screen, const char* caption,
                                   const char* initial, const char* accepted,
                                   unsigned max_length, lv_event_cb_t callback) {
  lv_obj_update_layout(screen);
  const int screen_width = lv_obj_get_width(screen), body_height = lv_obj_get_height(screen) - 44;
  const bool large = wide(screen_width);
  const int width = large ? 400 : 360;
  lv_point_t measured;
  lv_text_get_size(&measured, caption, UI_FONT_DEFAULT, 0, 0, width - 24, LV_TEXT_FLAG_NONE);
  const int limit = 2 * UI_FONT_DEFAULT->line_height, title_height = measured.y < limit ? measured.y : limit;
  const int input_y = 14 + title_height, error_y = input_y + 44, keyboard_y = error_y + 22;
  const int keyboard_height = large ? 176 : 152, height = keyboard_y + keyboard_height + 8;
  NumericDialog result{};
  result.overlay = lv_obj_create(screen);
  lv_obj_set_pos(result.overlay, 0, 44); lv_obj_set_size(result.overlay, screen_width, body_height);
  unframed(result.overlay); lv_obj_remove_flag(result.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(result.overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(result.overlay, lv_color_hex(0), 0);
  lv_obj_set_style_bg_opa(result.overlay, LV_OPA_50, 0);
  auto* dialog = lv_obj_create(result.overlay);
  lv_obj_set_pos(dialog, (screen_width - width) / 2, (body_height - height) / 2);
  lv_obj_set_size(dialog, width, height); lv_obj_set_style_pad_all(dialog, 0, 0);
  flat(dialog, surface(), 8); lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
  auto* title = lv_label(dialog, caption, 12, 8, text(), UI_FONT_DEFAULT, width - 24);
  lv_label_set_long_mode(title, measured.y > limit ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
  lv_obj_set_height(title, title_height);
  result.input = lv_textarea_create(dialog);
  lv_obj_set_pos(result.input, 12, input_y); lv_obj_set_size(result.input, width - 24, 40);
  lv_textarea_set_one_line(result.input, true);
  lv_textarea_set_accepted_chars(result.input, accepted); lv_textarea_set_max_length(result.input, max_length);
  lv_obj_set_style_text_font(result.input, UI_FONT_18, 0);
  lv_obj_set_style_text_align(result.input, LV_TEXT_ALIGN_CENTER, 0);
  const int pad = (40 - UI_FONT_18->line_height) / 2;
  lv_obj_set_style_pad_top(result.input, pad, 0); lv_obj_set_style_pad_bottom(result.input, pad, 0);
  lv_obj_set_height(result.input, 40); flat(result.input, field());
  lv_obj_set_style_text_color(result.input, text(), 0);
  lv_textarea_set_text(result.input, initial);
  lv_textarea_set_cursor_pos(result.input, LV_TEXTAREA_CURSOR_LAST);
  result.error = lv_label(dialog, "", 12, error_y, ui_theme_color(0xFF5B5B, 0x9D1D2F), UI_FONT_DEFAULT, width - 24);
  result.keyboard = lv_keyboard_create(dialog);
  lv_keyboard_set_mode(result.keyboard, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(result.keyboard, result.input);
  lv_obj_set_align(result.keyboard, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(result.keyboard, 8, keyboard_y); lv_obj_set_size(result.keyboard, width - 16, keyboard_height);
  keyboard(result.keyboard);
  lv_obj_add_event_cb(result.keyboard, callback, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(result.keyboard, callback, LV_EVENT_CANCEL, nullptr);
  return result;
}
}
