#pragma once
#include "OfeDisplayUi.h"
// Included after the sketch's theme helpers and settings event handlers.
static lv_obj_t* lv_settings_group(lv_obj_t* parent, int16_t x, int16_t y,
                                  int16_t width, int16_t height, const char* title) {
  lv_obj_t* group = lv_obj_create(parent);
  lv_obj_set_pos(group, x, y);
  lv_obj_set_size(group, width, height);
  ofe_display_ui::unframed(group);
  lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_label(group, title, 0, 0, ofe_display_ui::accent(), UI_FONT_18, width);
  return group;
}

static lv_obj_t* lv_settings_dropdown(lv_obj_t* parent, const char* title,
                                     const char* options, int16_t x, int16_t y,
                                     int16_t width, lv_event_cb_t callback) {
  lv_label(parent, title, x, y, ofe_display_ui::muted(), UI_FONT_DEFAULT, width);
  lv_obj_t* dropdown = lv_dropdown_create(parent);
  lv_obj_set_pos(dropdown, x, y + 24);
  lv_obj_set_size(dropdown, width, ofe_display_ui::CONTROL_HEIGHT);
  lv_dropdown_set_options(dropdown, options);
  ofe_display_ui::dropdown(dropdown);
  lv_obj_add_event_cb(dropdown, callback, LV_EVENT_VALUE_CHANGED, NULL);
  return dropdown;
}

static void lv_create_display_settings(lv_obj_t* parent, int16_t width) {
  const bool wide = ofe_display_ui::wide(width);
  const int16_t gap = 2 * ofe_display_ui::GAP;
  const int16_t column = wide ? (width - gap) / 2 : width;
  const int16_t field = (column - ofe_display_ui::GAP) / 2;
#if defined(ST7796_PERCEPTUAL_BACKLIGHT)
  const int16_t appearance_height = 280;
#else
  const int16_t appearance_height = 192;
#endif
  lv_obj_t* appearance = lv_settings_group(parent, 0, 0, column, appearance_height,
                                           tr("Display", "Anzeige"));
  lv_label(appearance, tr("Brightness", "Helligkeit"), 0, 32,
           ofe_display_ui::muted(), UI_FONT_DEFAULT, column);
  ofe_display_ui::button(appearance, 0, 56, 52, LV_SYMBOL_MINUS, lv_brightness_down_event);
  ofe_display_ui::button(appearance, column - 52, 56, 52, LV_SYMBOL_PLUS, lv_brightness_up_event);
  ui_system_brightness = lv_label(appearance, (String(display_brightness_pct) + "%").c_str(),
                                  64, 56, ofe_display_ui::text(), UI_FONT_18, column - 128);
  lv_obj_set_style_text_align(ui_system_brightness, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_height(ui_system_brightness, 44);
  const int brightness_pad = (44 - lv_font_get_line_height(UI_FONT_18)) / 2;
  lv_obj_set_style_pad_top(ui_system_brightness, brightness_pad, 0);
  lv_obj_set_style_pad_bottom(ui_system_brightness, brightness_pad, 0);
  ui_system_language = lv_settings_dropdown(appearance, tr("Language", "Sprache"),
    "English\nDeutsch", 0, 112, field, lv_language_event);
  ui_system_theme = lv_settings_dropdown(appearance, tr("Theme", "Farbschema"),
    display_language == 1 ? "Dunkel\nHell" : "Dark\nLight",
    field + ofe_display_ui::GAP, 112, field, lv_theme_event);
  lv_dropdown_set_selected(ui_system_language, display_language);
  lv_dropdown_set_selected(ui_system_theme, display_theme);
#if defined(ST7796_PERCEPTUAL_BACKLIGHT)
  ui_system_color_profile = lv_settings_dropdown(appearance, tr("LCD color", "LCD-Farben"),
    display_language == 1 ? "Standard\nKontrast\nSatt" : "Standard\nContrast\nVivid",
    0, 200, column, lv_st7796_color_profile_event);
  lv_dropdown_set_selected(ui_system_color_profile, display_color_profile > 2 ? 2 : display_color_profile);
#endif

  const int16_t rest_y = wide ? 0 : appearance_height + gap;
  lv_obj_t* rest = lv_settings_group(parent, wide ? column + gap : 0, rest_y,
                                    column, 280, tr("Sleep & backlight", "Ruhe & Beleuchtung"));
  ui_system_screensaver = lv_settings_dropdown(rest, tr("Screensaver", "Ruhemodus"),
    display_language == 1 ? "Aus\n1 min\n2 min\n5 min\n10 min" : "Off\n1 min\n2 min\n5 min\n10 min",
    0, 32, column, lv_screensaver_timeout_event);
  lv_dropdown_set_selected(ui_system_screensaver, screensaver_selection_from_timeout(screensaver_timeout_min));
  lv_create_backlight_controls(rest, column, 112);

  const int16_t bottom = rest_y + 280;
  lv_obj_t* divider = lv_obj_create(parent);
  lv_obj_set_pos(divider, 0, bottom + 8);
  lv_obj_set_size(divider, width, 1);
  lv_obj_set_style_pad_all(divider, 0, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_bg_color(divider, ofe_display_ui::outline(), 0);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  ofe_display_ui::button(parent, wide ? column + gap : 0, bottom + 24,
    column, tr("Connection settings", "Verbindung einrichten"), OfeDisplayWifiUi::openEvent);
  ui_system_benchmark_button = ofe_display_ui::button(parent, 0, bottom + (wide ? 24 : 80),
    column, tr("Display benchmark", "Display-Benchmark"), display_benchmark_open_event);
}
