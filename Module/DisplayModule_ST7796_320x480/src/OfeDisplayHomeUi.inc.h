#pragma once
#include "OfeDisplayHomeActivityUi.h"
static lv_obj_t* ui_home_module_content = nullptr;
static bool lv_home_group_match(const DisplayModuleSummary& module, uint8_t group) {
  if (group == MODULE_FAN_IO)
    return module.valid && (module.type == MODULE_FAN_IO || module.type == MODULE_FAN_IO_PRO);
  return module_summary_matches_group(module, group);
}
static uint8_t lv_home_module_count(uint8_t group, bool online) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < module_total && i < 17; ++i)
    if (lv_home_group_match(module_summaries[i], group) && (!online || (module_summaries[i].flags & 1))) ++count;
  return count;
}

static ofe_home_activity::Counts lv_home_jbc_counts(bool master_ok) {
  ofe_home_activity::Counts counts;
  if (!master_ok) { counts.known = false; return counts; }
  unsigned modules = 0, fresh = 0;
  bool single_fae = false;
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    const auto& module = module_summaries[i];
    if (!module_summary_matches_group(module, MODULE_JBC_BUS) || !(module.flags & 0x01)) continue;
    ++modules;
    single_fae = module.type == MODULE_JBC_BUS || (module.caps & CAP_JBC_BUS);
    if (!module.live_io_valid || uint32_t(millis() - module.live_io_ms) > 10000U) continue;
    ++fresh;
    if (!(module.live_jbc_flags & FAST_FLAG_CONNECTED)) continue;
    if (module.type == MODULE_JBC_BUS || (module.caps & CAP_JBC_BUS)) {
      counts.work += module.live_jbc_work ? 1 : 0;
      counts.stand += ofe_jbc_activity_ui::fae_stand(true, module.live_jbc_work) ? 1 : 0;
    } else {
      counts.work += ofe_home_activity::bits(module.live_jbc_work);
      counts.stand += ofe_home_activity::bits(module.live_jbc_stand);
    }
  }
  if (!fresh && modules <= 1) {
    counts.work = status.jbc_connected ? (single_fae ? (status.jbc_work_mask ? 1 : 0) : ofe_home_activity::bits(status.jbc_work_mask)) : 0;
    counts.stand = status.jbc_connected ? (single_fae ? (status.jbc_work_mask ? 0 : 1) : ofe_home_activity::bits(status.jbc_stand_mask)) : 0;
  } else if (fresh != modules) counts.known = false;
  return counts;
}

static ofe_home_activity::OutputState lv_home_fan_state(bool master_ok) {
  ofe_home_activity::OutputState state;
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    const auto& module = module_summaries[i];
    if (!lv_home_group_match(module, MODULE_FAN_IO) ||
        !(module.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT))) continue;
    state.capable = true;
    if (!master_ok || !(module.flags & 0x01)) continue;
    const bool active = module.addr == status.output_addr ? status.output_enabled :
      module.live_io_valid && uint32_t(millis() - module.live_io_ms) <= 10000U && (module.live_io_flags & 0x06) == 0x06;
    state.active |= active;
    const unsigned percent = module.addr == status.output_addr ? status.output_power / 10U :
      universal_output_power_percent_for_ui(module.addr, 100);
    if (active && percent > state.percent) state.percent = percent;
  }
  return state;
}

static int16_t lv_home_card_height(lv_obj_t* title, lv_obj_t* state, lv_obj_t* detail, int minimum) {
  if (!title || !state || !detail) return minimum;
  const auto height = [](lv_obj_t* label) {
    lv_point_t measured;
    const int width = lv_obj_get_style_width(label, LV_PART_MAIN);
    lv_text_get_size(&measured, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
      lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
      width > 0 ? width : lv_obj_get_width(label), LV_TEXT_FLAG_NONE);
    return int(measured.y);
  };
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_label_set_long_mode(state, LV_LABEL_LONG_WRAP);
  const int title_height = height(title), state_height = height(state);
  const int detail_y = 9 + (title_height > state_height ? title_height : state_height) + 8;
  if (lv_obj_get_style_y(detail, LV_PART_MAIN) != detail_y) lv_obj_set_y(detail, detail_y);
  const int bottom = detail_y + height(detail) + 12;
  return int16_t(bottom > minimum ? bottom : minimum);
}
static int16_t lv_fit_home_fan_card() {
  return lv_home_card_height(ui_home_suction_title, ui_home_suction, ui_home_fan_detail, 112);
}
static void lv_home_accent_layout(lv_obj_t* stripe, int height) {
  if (!stripe) return;
  lv_obj_set_pos(stripe, 4, 12);
  lv_obj_set_size(stripe, 3, height - 24);
  lv_obj_set_style_radius(stripe, 0, 0);
  lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
}
#include "OfeDisplayBridgeHomeUi.inc.h"

static void lv_style_home_page() {
  using namespace ofe_display_ui;
  lv_obj_update_layout(ui_dashboard_screen);
  flat(ui_dashboard_screen, background(), 0);
  unframed(ui_home_content);
  lv_obj_set_pos(ui_home_content, 8, 48);
  lv_obj_set_height(ui_home_content, wide(lv_obj_get_width(ui_dashboard_screen)) ? 376 : 216);
  ui_home_module_content = ui_home_content;
  for (lv_obj_t* card : {ui_home_output_card, ui_home_jbc_card, ui_home_suction_card,
                         ui_home_weller_card, ui_home_fault_card}) {
    lv_obj_set_style_radius(card, RADIUS, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 1, 0);
  }
  for (lv_obj_t* card : {ui_home_jbc_card, ui_home_suction_card, ui_home_weller_card, ui_home_fault_card}) {
    lv_obj_set_style_bg_color(card, surface(), 0);
    lv_obj_set_style_border_color(card, outline(), 0);
  }
  lv_obj_set_style_bg_color(ui_home_jbc_stripe, lv_color_hex(0x39C779), 0);
  lv_obj_set_style_bg_color(ui_home_suction_stripe, lv_color_hex(ofe_home_activity::accent(1)), 0);
  lv_obj_set_style_bg_color(ui_home_weller_stripe, lv_color_hex(ofe_home_activity::accent(2)), 0);
  unframed(ui_home_settings_card);
  lv_obj_set_y(ui_home_settings_card, 112);
  lv_obj_set_height(ui_home_output_card, 100);
  lv_obj_set_y(ui_home_power_bar, 86);
  if (wide(lv_obj_get_width(ui_dashboard_screen))) {
    lv_obj_set_pos(ui_home_content, 8, 48);
    lv_obj_set_height(ui_home_content, 376);
    lv_obj_set_height(ui_home_output_card, 92);
    lv_obj_set_y(ui_home_power_bar, 80);
    lv_obj_set_y(ui_home_settings_card, 104);
    lv_obj_set_height(ui_home_settings_card, 272);
    lv_obj_remove_flag(ui_home_content, LV_OBJ_FLAG_SCROLLABLE);
    ui_home_module_content = lv_obj_create(ui_home_content);
    unframed(ui_home_module_content);
    lv_obj_set_pos(ui_home_module_content, 392, 0); lv_obj_set_size(ui_home_module_content, 376, 376);
    lv_obj_set_scroll_dir(ui_home_module_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_home_module_content, LV_SCROLLBAR_MODE_ACTIVE);
    for (auto* card : {ui_home_jbc_card, ui_home_suction_card, ui_home_weller_card, ui_home_fault_card})
      lv_obj_set_parent(card, ui_home_module_content);
  }
  lv_obj_set_width(ui_home_output, wide(lv_obj_get_width(ui_dashboard_screen)) ? 100 : 116);
  lv_obj_set_height(ui_home_output, 20);
  lv_label_set_long_mode(ui_home_output, LV_LABEL_LONG_CLIP);
  if (!wide(lv_obj_get_width(ui_dashboard_screen))) {
    lv_obj_set_width(ui_home_power, 58);
    lv_obj_update_layout(ui_home_content);
    const int offset = lv_obj_get_y(ui_home_settings_card) + lv_obj_get_height(ui_home_settings_card) + 10
      - lv_obj_get_y(ui_home_jbc_card);
    for (lv_obj_t* card : {ui_home_jbc_card, ui_home_suction_card, ui_home_weller_card, ui_home_fault_card})
      lv_obj_set_y(card, lv_obj_get_y(card) + offset);
  }
  controls(ui_home_content);
  for (lv_obj_t* dropdown : {ui_home_mode_dropdown, ui_home_input_dropdown, ui_home_output_dropdown})
    lv_obj_set_style_pad_right(dropdown, 12, 0);
  // Numeric fields use the button class but keep their accent treatment.
  for (lv_obj_t* field : {ui_home_power_input, ui_home_delay_input, ui_home_afterrun_power_input})
    numeric_field(field);
  ofe_home_activity::create(ui_dashboard_screen, ui_home_jbc_card, ui_home_work_icon, ui_home_work,
    ui_home_output_card, ui_home_suction_card, ui_home_suction_title, ui_home_weller_card);
  lv_create_bridge_home_cards();
}
