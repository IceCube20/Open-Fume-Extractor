#pragma once
#include "DisplayModuleHealth.h"

namespace ofe_module_cards {
enum Severity : uint8_t { Normal, Warning, Fault };

static Severity fault_severity(uint16_t faults) {
  return (faults & uint16_t(~0x0002U)) ? Fault : faults ? Warning : Normal;
}

static Severity severity(const DisplayModuleSummary& module, bool master_ok) {
  if (!master_ok || !module.valid) return Normal;
  if (!(module.flags & 1)) return Fault;
  using namespace ofe_module_health;
  const uint32_t now = millis();
  const bool live_fresh = module.live_io_valid && uint32_t(now - module.live_io_ms) <= 10000U;
  ofe_home_metrics::State state;
  uint32_t received = 0;
  const bool metrics_fresh = home_metrics_get(module.addr, module.type, state, received) &&
    uint32_t(now - received) <= 10000U;
  // The MCU knows output roles and muted warnings; local link flags do not.
  if (live_fresh && (module.type == MODULE_JBC_BUS || module.type == MODULE_JBC_USB) &&
      (module.live_jbc_flags & JBC_KNOWN))
    return (module.live_jbc_flags & JBC_ERROR) ? Fault : (module.live_jbc_flags & JBC_WARNING) ? Warning : Normal;
  if (metrics_fresh && (state.flags & METRICS_KNOWN) &&
      !(live_fresh && (module.live_io_flags & LIVE_KNOWN) &&
        uint32_t(now - module.live_io_ms) < uint32_t(now - received)))
    return (state.flags & METRICS_ERROR) ? Fault : (state.flags & METRICS_WARNING) ? Warning : Normal;
  if (live_fresh && (module.live_io_flags & LIVE_KNOWN))
    return (module.live_io_flags & LIVE_ERROR) ? Fault : (module.live_io_flags & LIVE_WARNING) ? Warning : Normal;
  if (module.flags & SUMMARY_KNOWN)
    return (module.flags & SUMMARY_ERROR) ? Fault : (module.flags & SUMMARY_WARNING) ? Warning : Normal;

  // Compatibility with older MCUs: a pending alarm is not a severity bit.
  Severity result = Normal;
  if (live_fresh) {
    result = fault_severity(module.live_io_faults);
    if ((module.type == MODULE_JBC_BUS || module.type == MODULE_JBC_USB) &&
        !(module.live_jbc_flags & FAST_FLAG_CONNECTED) && result < Warning) result = Warning;
  }
  if (metrics_fresh) {
    using namespace ofe_home_metrics;
    if ((state.flags & LINK_KNOWN) && !(state.flags & LINKED)) {
      const Severity link = status.output_addr == module.addr ? Fault : Warning;
      if (link > result) result = link;
    }
    const Severity faults = fault_severity(state.faults);
    if (faults > result) result = faults;
    if (state.flags & HAS_FILTER) {
      if (state.filter == 100) return Fault;
      if (state.filter == 10 && result < Warning) result = Warning;
    }
  }
  if (status.master_alarm_valid) {
    const unsigned capacity = sizeof(status.master_alarm_addr) / sizeof(status.master_alarm_addr[0]);
    for (unsigned i = 0; i < status.master_alarm_item_count && i < capacity; ++i) {
      if (status.master_alarm_addr[i] != module.addr) continue;
      if (status.master_alarm_critical_mask & (1U << i)) return Fault;
      if (result < Warning) result = Warning;
    }
  }
  return result;
}

static lv_color_t border(Severity state, lv_color_t normal, bool updating = false) {
  return updating ? ui_theme_color(0x2997FF, 0x1570EF) :
    state == Fault ? ui_theme_color(0xFF4D5E, 0xD92D20) :
    state == Warning ? ui_theme_color(0xFFB020, 0xB54708) : normal;
}

static void apply_border(lv_obj_t* card, Severity state, lv_color_t normal, bool updating = false) {
  if (!card) return;
  const lv_color_t color = border(state, normal, updating);
  if (!lv_color_eq(lv_obj_get_style_border_color(card, LV_PART_MAIN), color))
    lv_obj_set_style_border_color(card, color, 0);
}

static void create_row(uint8_t slot) {
  if (slot >= DISPLAY_MODULE_CAPACITY || ui_module_rows[slot] || !ui_module_list) return;
  const int width = lv_obj_get_style_width(ui_module_list, LV_PART_MAIN) - 22;
  lv_obj_t* row = lv_button_create(ui_module_list);
  if (!row) return;
  ui_module_rows[slot] = row;
  lv_obj_set_pos(row, 0, slot * 54);
  lv_obj_set_size(row, width, 48);
  lv_stabilize_button(row);
  lv_obj_set_style_radius(row, ofe_display_ui::RADIUS, 0);
  lv_obj_set_style_bg_color(row, ui_theme_color(0x151D27, 0xF7FAFC), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, ui_theme_color(0x2C3B4A, 0xC4D0DB), 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_user_data(row, (void*)(uintptr_t)slot);
  lv_obj_add_event_cb(row, lv_module_row_event, LV_EVENT_CLICKED, nullptr);
  auto* stripe = lv_obj_create(row);
  ui_module_row_status_stripes[slot] = stripe;
  lv_obj_set_pos(stripe, 4, 8);
  lv_obj_set_size(stripe, 6, 32);
  lv_obj_set_style_radius(stripe, 3, 0);
  lv_obj_set_style_bg_color(stripe, ui_theme_color(0x47515D, 0x9AA8B6), 0);
  lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(stripe, 0, 0);
  lv_obj_set_style_pad_all(stripe, 0, 0);
  lv_obj_remove_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
  ui_module_row_names[slot] = lv_label(row, "", 18, 5, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, width - 32);
  ui_module_row_meta[slot] = lv_label(row, "", 18, 27, lv_color_hex(0x96A0AA), UI_FONT_DEFAULT, width - 32);
  ofe_display_ui::labels(row);
}


static int row_height(bool updating) { return updating ? 60 : 48; }

static void row_progress(lv_obj_t*& bar, lv_obj_t* row, bool updating, uint8_t progress) {
  // Allocate only for rows that actually receive an update, not for every module.
  if (updating && !bar) {
    bar = lv_bar_create(row);
    if (!bar) return;
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, ui_theme_color(0x283241, 0xC4D8EC), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, ui_theme_color(0x58B8FF, 0x1570EF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  }
  if (!bar) return;
  lv_obj_set_flag(bar, LV_OBJ_FLAG_HIDDEN, !updating);
  if (!updating) return;
  const int width = lv_obj_get_style_width(row, LV_PART_MAIN);
  lv_obj_set_pos(bar, 18, 48);
  lv_obj_set_size(bar, width > 36 ? width - 36 : 1, 5);
  lv_bar_set_value(bar, progress > 100 ? 100 : progress, LV_ANIM_OFF);
}
} // namespace ofe_module_cards
