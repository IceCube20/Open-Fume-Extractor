#pragma once
// Shared construction only; writes, pending values and readback stay in the sketch.
static void lv_build_detail_controls() {
  if (!ui_detail_controls || !selected_module.valid) return;
  const uint16_t controls_y = lv_detail_controls_y();
  const uint16_t controls_h = lv_detail_controls_h();
  lv_obj_set_y(ui_detail_controls, controls_y);
  lv_obj_set_height(ui_detail_controls, controls_h ? controls_h : 58);
  const bool controls_hidden = lv_obj_has_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
  const uint32_t next_controls_signature = selected_universal_controls_signature();
  if (!controls_hidden && detail_controls_addr == selected_module.addr &&
      detail_controls_type == selected_module.type && detail_controls_caps == selected_module.caps &&
      detail_controls_signature == next_controls_signature) return;
  detail_controls_addr = selected_module.addr;
  detail_controls_type = selected_module.type;
  detail_controls_caps = selected_module.caps;
  detail_controls_signature = next_controls_signature;
  lv_obj_clean(ui_detail_controls);
  ui_detail_weller_filter_dropdown = nullptr;
  ui_detail_jbc_mode_dropdown = nullptr;
  ui_detail_jbc_power_input = nullptr;
  ui_detail_jbc_delay_work_input = nullptr;
  ui_detail_jbc_delay_stand_input = nullptr;
  ui_detail_weller_speed_slider = nullptr;
  ui_detail_display_brightness_slider = nullptr;
  ui_detail_display_brightness_value = nullptr;
  ui_detail_display_language = nullptr;
  ui_detail_display_theme = nullptr;
  ui_detail_stand_button = nullptr;
  ui_detail_continuous_button = nullptr;
  ui_detail_fan_button = nullptr;
  ui_detail_out1_button = nullptr;
  ui_detail_out2_button = nullptr;
  ui_detail_output_power_slider = nullptr;
  ui_detail_output_power_value = nullptr;
  ui_detail_weller_fan_button = nullptr;
  ui_detail_weller_light_button = nullptr;
  for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
    ui_detail_universal_sliders[i] = nullptr;
    ui_detail_universal_switches[i] = nullptr;
    ui_detail_universal_values[i] = nullptr;
    ui_detail_universal_buttons[i] = nullptr;
    ui_detail_universal_buttons_b[i] = nullptr;
    ui_detail_universal_selects[i] = nullptr;
  }
  lv_obj_clear_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(ui_detail_controls);
  const int width = lv_obj_get_width(ui_detail_controls);
  const int half = (width - 24) / 2, second = half + 16;

  if (selected_detail_is_jbc()) {
    lv_label(ui_detail_controls, tr("Mode", "Stufe"), 8, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_jbc_mode_dropdown = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_jbc_mode_dropdown, 8, 30);
    lv_obj_set_size(ui_detail_jbc_mode_dropdown, half, 44);
    lv_dropdown_set_options(ui_detail_jbc_mode_dropdown,
      display_language == 1 ? "Hoch 100%\nMittel 60%\nNiedrig 30%\nBenutzer" : "High 100%\nMedium 60%\nLow 30%\nCustom");
    lv_dropdown_set_selected(ui_detail_jbc_mode_dropdown, selected_module.suction > 3 ? 3 : selected_module.suction);
    lv_obj_set_style_radius(ui_detail_jbc_mode_dropdown, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_jbc_mode_dropdown, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_border_color(ui_detail_jbc_mode_dropdown, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
    lv_obj_set_style_text_color(ui_detail_jbc_mode_dropdown, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_jbc_mode_dropdown);
    lv_obj_add_event_cb(ui_detail_jbc_mode_dropdown, lv_detail_jbc_mode_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_label(ui_detail_controls, tr("Power %", "Leistung %"), second, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_jbc_power_input = lv_numeric_field(ui_detail_controls, second, 30, half, selected_module.select_flow / 10U, NUMERIC_FIELD_JBC_POWER);
    ui_detail_stand_button = lv_small_button(ui_detail_controls, 8, 90, half, tr("Stand", "Stand"), lv_stand_intakes_toggle_event);
    ui_detail_continuous_button = lv_small_button(ui_detail_controls, second, 90, half, tr("Continuous", "Dauerlauf"), lv_continuous_toggle_event);

    lv_label(ui_detail_controls, tr("Work delay (s)", "Work Nachlauf (s)"), 8, 150, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_jbc_delay_work_input = lv_numeric_field(ui_detail_controls, 8, 174, half, selected_module.delay_work, NUMERIC_FIELD_JBC_DELAY_WORK);
    lv_label(ui_detail_controls, tr("Stand delay (s)", "Stand Nachlauf (s)"), second, 150, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_jbc_delay_stand_input = lv_numeric_field(ui_detail_controls, second, 174, half, selected_module.delay_stand, NUMERIC_FIELD_JBC_DELAY_STAND);
  } else if (selected_detail_is_fan_io()) {
    const bool main_configured = selected_fan_io_main_output_configured();
    const String main_label = detail_alias_or(selected_module.io_main_alias, tr("Relay/Fan", "Relais/L\303\274fter"));
    if (main_configured) {
      ui_detail_fan_button = lv_small_button(ui_detail_controls, 8, 28, half, main_label.c_str(), lv_detail_fan_event);
      lv_label(ui_detail_controls, tr("Power", "Leistung"), second, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
      ui_detail_output_power_value = lv_label(ui_detail_controls, "-", second + half - 72, 36, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 72);
      lv_obj_set_style_text_align(ui_detail_output_power_value, LV_TEXT_ALIGN_CENTER, 0);
      ui_detail_output_power_slider = lv_slider_create(ui_detail_controls);
      lv_obj_set_pos(ui_detail_output_power_slider, second + 12, 88);
      lv_obj_set_size(ui_detail_output_power_slider, half - 24, 12);
      lv_slider_set_range(ui_detail_output_power_slider, 10, 100);
      lv_slider_set_value(ui_detail_output_power_slider, constrain(selected_module.output_power / 10, 10, 100), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(ui_detail_output_power_slider, ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_detail_output_power_slider, lv_color_hex(0x2997FF), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(ui_detail_output_power_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
      lv_obj_add_event_cb(ui_detail_output_power_slider, lv_detail_output_power_slider_event, LV_EVENT_PRESSED, NULL);
      lv_obj_add_event_cb(ui_detail_output_power_slider, lv_detail_output_power_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_add_event_cb(ui_detail_output_power_slider, lv_detail_output_power_slider_event, LV_EVENT_RELEASED, NULL);
    }

    // Additional Fan/IO GPIO outputs are ordinary descriptor switch entities
    // (40..47). Build every configured output dynamically and use the same
    // entity write/readback path as Universal modules. Disabled channels are
    // absent from the descriptor and therefore cannot leave phantom controls.
    uint8_t row = 0;
    const uint16_t gpio_y0 = main_configured ? 120U : 0U;
    for (uint8_t i = 0; i < selected_module.universal_entity_count &&
         i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX && row < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      const DisplayUniversalEntity& e = selected_module.universal_entities[i];
      if (!fan_io_gpio_output_entity(e)) continue;
      const uint8_t slot = row;
      auto* cell = ofe_display_ui::control_cell(ui_detail_controls, row, 104, gpio_y0);
      const int cell_width = lv_obj_get_style_width(cell, LV_PART_MAIN);
      const String label = fan_io_entity_label(e, true);
      auto* caption = lv_label(cell, label.c_str(), 12, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, cell_width - 24);
      lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP); lv_obj_set_height(caption, 36);
      const int16_t actual_value = e.value ? 1 : 0;
      int16_t control_value = actual_value;
      universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_SWITCH, control_value);
      ui_detail_universal_values[slot] = lv_label(cell, on_off(control_value != 0),
        12, 58, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 100);
      lv_obj_set_style_text_align(ui_detail_universal_values[slot], LV_TEXT_ALIGN_CENTER, 0);
      ui_detail_universal_switches[slot] = lv_switch_create(cell);
      lv_obj_set_pos(ui_detail_universal_switches[slot], cell_width - 86, 50);
      lv_obj_set_size(ui_detail_universal_switches[slot], 74, 34);
      lv_obj_set_user_data(ui_detail_universal_switches[slot], (void*)(uintptr_t)e.id);
      lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], lv_color_hex(0x167A4A), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], lv_color_hex(0xFFFFFF), LV_PART_KNOB);
      lv_obj_set_style_border_width(ui_detail_universal_switches[slot], 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(ui_detail_universal_switches[slot], ui_theme_color(0x3B5570, 0xB5C5D5), LV_PART_MAIN);
      if (control_value) lv_obj_add_state(ui_detail_universal_switches[slot], LV_STATE_CHECKED);
      else lv_obj_clear_state(ui_detail_universal_switches[slot], LV_STATE_CHECKED);
      lv_obj_add_event_cb(ui_detail_universal_switches[slot], lv_detail_universal_switch_event, LV_EVENT_VALUE_CHANGED, NULL);
      ++row;
    }
    if (!main_configured && !row) {
      lv_label(ui_detail_controls, tr("No configured outputs", "Keine konfigurierten Ausg\303\244nge"),
        10, 18, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 380);
    }
  } else if (selected_detail_is_universal()) {
    uint8_t row = 0;
    for (uint8_t i = 0; i < selected_module.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX && row < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      const DisplayUniversalEntity& e = selected_module.universal_entities[i];
      if (!e.valid || !universal_entity_writable(e)) continue;
      if (!(e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BUTTON || e.type == DISPLAY_UNI_NUMBER || e.type == DISPLAY_UNI_SELECT)) continue;
      const uint8_t slot = row;
      auto* cell = ofe_display_ui::control_cell(ui_detail_controls, row, 152);
      const int cell_width = lv_obj_get_style_width(cell, LV_PART_MAIN);
      auto* caption = lv_label(cell, e.label[0] ? e.label : "Entity", 12, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, cell_width - 24);
      lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP); lv_obj_set_height(caption, 36);
      if (e.type == DISPLAY_UNI_NUMBER) {
        const bool readback_available = universal_entity_readable(e);
        const int16_t actual_value = e.value;
        int16_t control_value = actual_value;
        const bool has_pending = universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_NUMBER, control_value);
        String value_text = (!readback_available && !has_pending) ? String("-") : String(readback_available ? actual_value : control_value);
        if (value_text != "-" && e.unit[0]) value_text += String(" ") + e.unit;
        ui_detail_universal_values[slot] = lv_label(cell, value_text.c_str(), 12, 52, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_18, cell_width - 24);
        lv_obj_set_style_text_align(ui_detail_universal_values[slot], LV_TEXT_ALIGN_CENTER, 0);
        ui_detail_universal_sliders[slot] = lv_slider_create(cell);
        lv_obj_set_pos(ui_detail_universal_sliders[slot], 24, 112);
        lv_obj_set_size(ui_detail_universal_sliders[slot], cell_width - 48, 12);
        int16_t min_v = e.min_value;
        int16_t max_v = e.max_value;
        if (max_v <= min_v) { min_v = 0; max_v = 100; }
        if (min_v < 0) min_v = 0;
        if (max_v > 255) max_v = 255;
        lv_slider_set_range(ui_detail_universal_sliders[slot], min_v, max_v);
        lv_slider_set_value(ui_detail_universal_sliders[slot], constrain(control_value, min_v, max_v), LV_ANIM_OFF);
        lv_obj_set_user_data(ui_detail_universal_sliders[slot], (void*)(uintptr_t)e.id);
        lv_obj_set_style_bg_color(ui_detail_universal_sliders[slot], ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_detail_universal_sliders[slot], lv_color_hex(0x2997FF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_detail_universal_sliders[slot], lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_add_event_cb(ui_detail_universal_sliders[slot], lv_detail_universal_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(ui_detail_universal_sliders[slot], lv_detail_universal_slider_event, LV_EVENT_RELEASED, NULL);
      } else if (e.type == DISPLAY_UNI_SWITCH) {
        const bool readback_available = universal_entity_readable(e);
        const int16_t actual_value = e.value ? 1 : 0;
        int16_t control_value = actual_value;
        const bool has_pending = universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_SWITCH, control_value);

        const char* shown_text = (!readback_available && !has_pending) ? "-" : on_off((readback_available ? actual_value : control_value) != 0);
        ui_detail_universal_values[slot] = lv_label(
          cell, shown_text,
          12, 80, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 100);
        lv_obj_set_style_text_align(ui_detail_universal_values[slot], LV_TEXT_ALIGN_CENTER, 0);
        ui_detail_universal_switches[slot] = lv_switch_create(cell);
        lv_obj_set_pos(ui_detail_universal_switches[slot], cell_width - 86, 72);
        lv_obj_set_size(ui_detail_universal_switches[slot], 74, 34);
        lv_obj_set_user_data(ui_detail_universal_switches[slot], (void*)(uintptr_t)e.id);
        lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], lv_color_hex(0x167A4A), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_border_width(ui_detail_universal_switches[slot], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(ui_detail_universal_switches[slot], ui_theme_color(0x3B5570, 0xB5C5D5), LV_PART_MAIN);
        if (control_value) lv_obj_add_state(ui_detail_universal_switches[slot], LV_STATE_CHECKED);
        else lv_obj_clear_state(ui_detail_universal_switches[slot], LV_STATE_CHECKED);
        lv_obj_add_event_cb(ui_detail_universal_switches[slot], lv_detail_universal_switch_event, LV_EVENT_VALUE_CHANGED, NULL);
      } else if (e.type == DISPLAY_UNI_SELECT) {
        int16_t shown_index = e.value;
        const bool readback_available = universal_entity_readable(e);
        const bool has_pending = universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_SELECT, shown_index);
        String current_text = readback_available
          ? universal_entity_value_text(e)
          : (has_pending ? universal_select_option_text(e, shown_index) : String("-"));
        ui_detail_universal_values[slot] = lv_label(cell, current_text.c_str(), 12, 48, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, cell_width - 24);
        lv_label_set_long_mode(ui_detail_universal_values[slot], LV_LABEL_LONG_WRAP);
        lv_obj_set_height(ui_detail_universal_values[slot], 36);
        lv_obj_set_style_text_align(ui_detail_universal_values[slot], LV_TEXT_ALIGN_CENTER, 0);
        ui_detail_universal_selects[slot] = lv_dropdown_create(cell);
        lv_obj_set_pos(ui_detail_universal_selects[slot], 12, 92);
        lv_obj_set_size(ui_detail_universal_selects[slot], cell_width - 24, 44);
        String profile_opts = e.options[0] ? String(e.options) : current_text;
        profile_opts.replace("|", "\n");
        profile_opts.replace(";", "\n");
        String opts = String(tr("Please choose option", "Bitte Option w\303\244hlen"));
        if (profile_opts.length()) opts += "\n" + profile_opts;
        lv_dropdown_set_options(ui_detail_universal_selects[slot], opts.c_str());
        // Index 0 stays a neutral command placeholder. RW readback is shown
        // in the separate value column beside the control.
        ui_detail_universal_select_refreshing = true;
        lv_dropdown_set_selected(ui_detail_universal_selects[slot], 0);
        ui_detail_universal_select_refreshing = false;
        lv_obj_set_user_data(ui_detail_universal_selects[slot], (void*)(uintptr_t)e.id);
        lv_obj_set_style_radius(ui_detail_universal_selects[slot], 8, 0);
        lv_obj_set_style_bg_color(ui_detail_universal_selects[slot], ui_theme_color(0x1A2633, 0xF8FAFC), 0);
        lv_obj_set_style_border_color(ui_detail_universal_selects[slot], ui_theme_color(0x3A5068, 0xB1C1D0), 0);
        lv_obj_set_style_text_color(ui_detail_universal_selects[slot], ui_theme_color(0xFFFFFF, 0x17212B), 0);
        lv_apply_dropdown_theme(ui_detail_universal_selects[slot]);
        lv_obj_add_event_cb(ui_detail_universal_selects[slot], lv_detail_universal_select_event, LV_EVENT_VALUE_CHANGED, NULL);
      } else {
        ui_detail_universal_buttons[slot] = lv_small_button(cell, 12, 80, cell_width - 24, tr("Send", "Senden"), lv_detail_universal_button_event);
        lv_obj_set_user_data(ui_detail_universal_buttons[slot], (void*)(uintptr_t)e.id);
      }
      ++row;
    }
    if (!row) {
      const bool descriptor_known = selected_module.universal_descriptor_crc != 0 ||
        selected_module.universal_entity_total != 0 || selected_module.universal_entity_count != 0;
      const bool descriptor_incomplete = selected_module.universal_entity_total != 0 &&
        selected_module.universal_entity_count < selected_module.universal_entity_total;
      const char* msg = (!descriptor_known || descriptor_incomplete)
        ? tr("Loading profile controls...", "Profil-Controls werden geladen...")
        : tr("Profile has no writable display controls", "Profil hat keine schreibbaren Display-Controls");
      lv_label(ui_detail_controls, msg, 14, 18,
        ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 398);
    }
  } else if (selected_detail_is_weller()) {
    ui_detail_weller_fan_button = lv_small_button(ui_detail_controls, 8, 8, half, tr("Fan", "L\303\274fter"), lv_detail_weller_fan_event);
    ui_detail_weller_light_button = lv_small_button(ui_detail_controls, second, 8, half, tr("Light", "Licht"), lv_detail_weller_light_event);
    lv_label(ui_detail_controls, tr("Speed", "Drehzahl"), 8, 68, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, width - 16);
    ui_detail_weller_speed_slider = lv_slider_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_weller_speed_slider, 24, 108);
    lv_obj_set_size(ui_detail_weller_speed_slider, width - 48, 12);
    lv_slider_set_range(ui_detail_weller_speed_slider, 30, 100);
    lv_slider_set_value(ui_detail_weller_speed_slider,
      constrain(selected_module.weller_speed, 30, 100), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui_detail_weller_speed_slider, ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_detail_weller_speed_slider, lv_color_hex(0x2997FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_detail_weller_speed_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(ui_detail_weller_speed_slider,
      lv_detail_weller_speed_slider_event, LV_EVENT_RELEASED, NULL);
    lv_label(ui_detail_controls, tr("Filter interval", "Filterintervall"), 8, 140, ofe_display_ui::muted(), UI_FONT_DEFAULT, half);
    ui_detail_weller_filter_dropdown = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_weller_filter_dropdown, 8, 164);
    lv_obj_set_size(ui_detail_weller_filter_dropdown, half, 44);
    lv_dropdown_set_options(ui_detail_weller_filter_dropdown, weller_filter_options());
    lv_dropdown_set_selected(ui_detail_weller_filter_dropdown,
      weller_filter_preset_index(selected_module.weller_programmed));
    lv_obj_set_style_radius(ui_detail_weller_filter_dropdown, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_weller_filter_dropdown, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_border_color(ui_detail_weller_filter_dropdown, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
    lv_obj_set_style_text_color(ui_detail_weller_filter_dropdown, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_weller_filter_dropdown);
    lv_obj_add_event_cb(ui_detail_weller_filter_dropdown,
      lv_detail_weller_filter_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_small_button(ui_detail_controls, second, 164, half, tr("Reset filter", "Filter zur\303\274cksetzen"), lv_detail_weller_reset_event);
  } else if (selected_module.type == MODULE_DISPLAY && selected_module.addr == module_addr) {
    lv_label(ui_detail_controls, tr("Brightness", "Helligkeit"), 8, 8,
      ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_display_brightness_value = lv_label(ui_detail_controls, "", width - 80, 8,
      ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 54);
    lv_set_text(ui_detail_display_brightness_value, String(display_brightness_pct) + "%");
    ui_detail_display_brightness_slider = lv_slider_create(ui_detail_controls);
    lv_obj_set_style_text_align(ui_detail_display_brightness_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui_detail_display_brightness_slider, 24, 48);
    lv_obj_set_size(ui_detail_display_brightness_slider, width - 48, 12);
    lv_slider_set_range(ui_detail_display_brightness_slider, 10, 100);
    lv_slider_set_value(ui_detail_display_brightness_slider, display_brightness_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui_detail_display_brightness_slider, ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_detail_display_brightness_slider, lv_color_hex(0x2997FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_detail_display_brightness_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(ui_detail_display_brightness_slider, lv_detail_display_brightness_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_detail_display_brightness_slider, lv_detail_display_brightness_event, LV_EVENT_RELEASED, NULL);

    lv_label(ui_detail_controls, tr("Language", "Sprache"), 8, 88,
      ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_display_language = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_display_language, 8, 112);
    lv_obj_set_size(ui_detail_display_language, half, 44);
    lv_dropdown_set_options(ui_detail_display_language, "English\nDeutsch");
    lv_dropdown_set_selected(ui_detail_display_language, display_language);
    lv_obj_set_style_radius(ui_detail_display_language, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_display_language, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_text_color(ui_detail_display_language, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_display_language);
    lv_obj_add_event_cb(ui_detail_display_language, lv_language_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_label(ui_detail_controls, tr("Theme", "Farben"), second, 88,
      ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, half);
    ui_detail_display_theme = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_display_theme, second, 112);
    lv_obj_set_size(ui_detail_display_theme, half, 44);
    lv_dropdown_set_options(ui_detail_display_theme,
      display_language == 1 ? "Dunkel\nHell" : "Dark\nLight");
    lv_dropdown_set_selected(ui_detail_display_theme, display_theme);
    lv_obj_set_style_radius(ui_detail_display_theme, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_display_theme, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_text_color(ui_detail_display_theme, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_display_theme);
    lv_obj_add_event_cb(ui_detail_display_theme, lv_theme_event, LV_EVENT_VALUE_CHANGED, NULL);
  } else {
    lv_obj_add_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
  }
  ofe_display_ui::controls(ui_detail_controls);
  ofe_display_ui::detail_alignment(ui_detail_controls);
}
