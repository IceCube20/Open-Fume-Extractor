#pragma once
#include "OfeDisplayUi.h"
#include "OfeDisplayNumericUi.h"
#include "OfeJbcActivityUi.h"
// Included after the sketch's standard label, theme and detail helpers.
static lv_obj_t* jbc_detail_root = nullptr;
static lv_obj_t* jbc_detail_category = nullptr;
static lv_obj_t* jbc_detail_port = nullptr;
static lv_obj_t* jbc_detail_page = nullptr;
static lv_obj_t* jbc_detail_hint = nullptr;
static lv_obj_t* jbc_detail_prev = nullptr;
static lv_obj_t* jbc_detail_next = nullptr;
static lv_obj_t* jbc_detail_list = nullptr;
static lv_obj_t* jbc_detail_identity = nullptr;
static lv_obj_t* jbc_detail_link = nullptr;
static lv_obj_t* jbc_detail_row[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_label[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_activity[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_text[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_spin[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_minus[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_plus[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_switch[ofe_jbc_detail::WINDOW] = {};
static lv_obj_t* jbc_detail_select[ofe_jbc_detail::WINDOW] = {};
static bool jbc_detail_refreshing = false;
static void jbc_detail_noop(lv_event_t*) {}
static lv_obj_t* jbc_detail_editor = nullptr;
static lv_obj_t* jbc_detail_input = nullptr;
static lv_obj_t* jbc_detail_error = nullptr;
static uint32_t jbc_detail_edit_revision = 0;
static uint8_t jbc_detail_edit_slot = 0, jbc_detail_edit_id = 0, jbc_detail_edit_addr = 0;

static void jbc_detail_editor_event(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_READY && display_jbc_state) {
    const char* text = lv_textarea_get_text(jbc_detail_input);
    char* end = nullptr; const float value = strtof(text, &end);
    const float scaled = value * 10;
    bool ok = !master_link_lost() && text[0] && end && !*end && isfinite(scaled) && scaled >= INT16_MIN && scaled <= INT16_MAX &&
      fabsf(scaled - roundf(scaled)) < 0.001f;
    portENTER_CRITICAL(&display_jbc_mux);
    const auto& s = *display_jbc_state;
    ok = ok && s.addr == jbc_detail_edit_addr && s.revision == jbc_detail_edit_revision &&
      s.section == ofe_jbc_detail::SETTINGS && s.rows[jbc_detail_edit_slot].id == jbc_detail_edit_id;
    if (ok) ok = display_jbc_state->queue(jbc_detail_edit_slot, int16_t(lroundf(scaled)), millis());
    portEXIT_CRITICAL(&display_jbc_mux);
    if (!ok) { lv_set_text_c(jbc_detail_error, tr("Invalid value or station changed", "Ung\303\274ltiger Wert oder Station gewechselt")); return; }
  } else if (lv_event_get_code(event) != LV_EVENT_CANCEL) return;
  lv_obj_delete(jbc_detail_editor); jbc_detail_editor = nullptr;
  lv_update_module_detail();
}
static void jbc_detail_edit_event(lv_event_t* event) {
  if (!display_jbc_state || jbc_detail_editor || master_link_lost()) return;
  const uint8_t slot = uintptr_t(lv_event_get_user_data(event));
  if (slot >= ofe_jbc_detail::WINDOW) return;
  ofe_jbc_detail::Row row;
  portENTER_CRITICAL(&display_jbc_mux);
  const auto& s = *display_jbc_state;
  row = s.rows[slot];
  const bool allowed = !s.pending && (s.flags & 3) == 3 && row.writable && row.kind == ofe_jbc_detail::NUMBER;
  jbc_detail_edit_slot = slot; jbc_detail_edit_id = row.id; jbc_detail_edit_addr = s.addr; jbc_detail_edit_revision = s.revision;
  portEXIT_CRITICAL(&display_jbc_mux);
  if (!allowed) return;
  char value[24]; snprintf(value, sizeof(value), "%.1f", row.value / 10.0);
  const auto dialog = ofe_display_ui::numeric_dialog(ui_module_detail_screen, row.label,
    value, "0123456789.-", 7, jbc_detail_editor_event);
  jbc_detail_editor = dialog.overlay; jbc_detail_input = dialog.input; jbc_detail_error = dialog.error;
}

static void jbc_detail_nav_event(lv_event_t* event) {
  if (jbc_detail_refreshing || !display_jbc_state) return;
  portENTER_CRITICAL(&display_jbc_mux);
  auto& s = *display_jbc_state;
  const uintptr_t action = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (!s.pending) {
    uint8_t section = s.section, port = s.port, base = 0;
    if (action == 0) section = lv_dropdown_get_selected(jbc_detail_category);
    else if (action == 1) port = lv_dropdown_get_selected(jbc_detail_port);
    else if (action == 2) base = s.base >= ofe_jbc_detail::WINDOW ? s.base - ofe_jbc_detail::WINDOW : 0;
    else if (action == 3) base = s.base + ofe_jbc_detail::WINDOW < s.total ? s.base + ofe_jbc_detail::WINDOW : s.base;
    s.select(s.addr, section, port, base);
  }
  portEXIT_CRITICAL(&display_jbc_mux);
  lv_obj_scroll_to_y(jbc_detail_list, 0, LV_ANIM_OFF);
  lv_update_module_detail();
}
static void jbc_detail_control_event(lv_event_t* event) {
  if (jbc_detail_refreshing || !display_jbc_state || master_link_lost()) return;
  const uintptr_t action = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  const uint8_t slot = action / 4, kind = action % 4;
  if (slot >= ofe_jbc_detail::WINDOW) return;
  portENTER_CRITICAL(&display_jbc_mux);
  auto& s = *display_jbc_state;
  int32_t value = s.rows[slot].value;
  if (kind < 2) {
    value += kind ? s.rows[slot].step : -s.rows[slot].step;
    if (value < s.rows[slot].minimum) value = s.rows[slot].minimum;
    if (value > s.rows[slot].maximum) value = s.rows[slot].maximum;
  }
  else if (kind == 2) value = lv_obj_has_state(jbc_detail_switch[slot], LV_STATE_CHECKED) ? 10 : 0;
  else value = lv_dropdown_get_selected(jbc_detail_select[slot]) * 10;
  if (value >= INT16_MIN && value <= INT16_MAX) s.queue(slot, int16_t(value), millis());
  portEXIT_CRITICAL(&display_jbc_mux);
  lv_update_module_detail();
}
static void jbc_detail_reset_ui() { jbc_detail_root = nullptr; jbc_detail_editor = nullptr; }

static int jbc_detail_text_height(const char* text, const lv_font_t* font, int width) {
  lv_point_t measured;
  lv_text_get_size(&measured, text, font, 0, 0, width, LV_TEXT_FLAG_NONE);
  return measured.y;
}
static void jbc_detail_delete(lv_obj_t*& obj) {
  if (obj) lv_obj_delete(obj);
  obj = nullptr;
}
static void jbc_detail_row_controls(uint8_t slot, uint8_t kind) {
  using namespace ofe_jbc_detail;
  if (kind != NUMBER) {
    jbc_detail_delete(jbc_detail_spin[slot]); jbc_detail_delete(jbc_detail_minus[slot]); jbc_detail_delete(jbc_detail_plus[slot]);
  }
  if (kind != SWITCH) jbc_detail_delete(jbc_detail_switch[slot]);
  if (kind != SELECT) jbc_detail_delete(jbc_detail_select[slot]);
  lv_obj_t* row = jbc_detail_row[slot];
  if (kind == NUMBER && !jbc_detail_spin[slot]) {
    jbc_detail_spin[slot] = lv_label(row, "", 0, 0, ofe_display_ui::accent(), UI_FONT_18, 112);
    lv_obj_set_height(jbc_detail_spin[slot], 44);
    lv_obj_set_style_text_align(jbc_detail_spin[slot], LV_TEXT_ALIGN_CENTER, 0);
    ofe_display_ui::numeric_field(jbc_detail_spin[slot]);
    lv_obj_set_style_text_align(jbc_detail_spin[slot], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_left(jbc_detail_spin[slot], 12, 0);
    lv_obj_add_flag(jbc_detail_spin[slot], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(jbc_detail_spin[slot], jbc_detail_edit_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(uintptr_t(slot)));
    jbc_detail_minus[slot] = ofe_display_ui::button(row, 0, 0, 44, LV_SYMBOL_MINUS, jbc_detail_noop);
    jbc_detail_plus[slot] = ofe_display_ui::button(row, 0, 0, 44, LV_SYMBOL_PLUS, jbc_detail_noop);
    lv_obj_set_height(jbc_detail_minus[slot], 44); lv_obj_set_height(jbc_detail_plus[slot], 44);
    lv_obj_add_event_cb(jbc_detail_minus[slot], jbc_detail_control_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(uintptr_t(slot * 4)));
    lv_obj_add_event_cb(jbc_detail_plus[slot], jbc_detail_control_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(uintptr_t(slot * 4 + 1)));
  } else if (kind == SWITCH && !jbc_detail_switch[slot]) {
    jbc_detail_switch[slot] = lv_switch_create(row);
    lv_obj_set_size(jbc_detail_switch[slot], 64, 34);
    ofe_display_ui::toggle(jbc_detail_switch[slot]);
    lv_obj_add_event_cb(jbc_detail_switch[slot], jbc_detail_control_event, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(uintptr_t(slot * 4 + 2)));
  } else if (kind == SELECT && !jbc_detail_select[slot]) {
    jbc_detail_select[slot] = lv_dropdown_create(row);
    lv_obj_set_size(jbc_detail_select[slot], 144, 44);
    ofe_display_ui::dropdown(jbc_detail_select[slot]);
    lv_dropdown_set_options(jbc_detail_select[slot], "Level 1\nLevel 2\nLevel 3");
    lv_obj_add_event_cb(jbc_detail_select[slot], jbc_detail_control_event, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(uintptr_t(slot * 4 + 3)));
  }
}

static void lv_update_jbc_detail(bool visible) {
  using namespace ofe_jbc_detail;
  if (!visible) {
    if (jbc_detail_root) lv_obj_add_flag(jbc_detail_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lv_obj_get_parent(ui_detail_values), LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_detail_title, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!display_jbc_state) {
    void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory) {
      State* next = new (memory) State();
      portENTER_CRITICAL(&display_jbc_mux); display_jbc_state = next; portEXIT_CRITICAL(&display_jbc_mux);
    }
  }
  if (!display_jbc_state) {
    lv_update_jbc_detail(false);
    lv_detail_values_hide();
    lv_obj_remove_flag(ui_detail_values, LV_OBJ_FLAG_HIDDEN);
    lv_set_text_c(ui_detail_values, tr("Not enough PSRAM", "Nicht genug PSRAM")); return;
  }
  State snapshot;
  portENTER_CRITICAL(&display_jbc_mux);
  if (display_jbc_state->addr != selected_module.addr) {
    display_jbc_state->pending = false;
    display_jbc_state->select(selected_module.addr, STATION, 0);
  }
  snapshot = *display_jbc_state;
  portEXIT_CRITICAL(&display_jbc_mux);
  if (master_link_lost()) snapshot.flags &= ~2U;
  if (jbc_detail_editor && (snapshot.addr != jbc_detail_edit_addr || snapshot.revision != jbc_detail_edit_revision)) {
    lv_obj_delete(jbc_detail_editor); jbc_detail_editor = nullptr;
  }
  if (!snapshot.received && snapshot.started_ms && uint32_t(millis() - snapshot.started_ms) >= 7000) {
    lv_detail_values_hide();
    if (jbc_detail_root) lv_obj_add_flag(jbc_detail_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lv_obj_get_parent(ui_detail_values), LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_detail_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_detail_values, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
    lv_set_text(ui_detail_values, jbc_usb_core_detail_text(selected_module) + "\n" +
      tr("Extended details require a current Master connection.", "Erweiterte Details ben\303\266tigen eine aktuelle Master-Verbindung."));
    return;
  }
  if (!jbc_detail_root) {
    lv_obj_update_layout(ui_module_detail_screen);
    const int width = lv_obj_get_width(ui_module_detail_screen) - 2 * ofe_display_ui::MARGIN;
    const int height = lv_obj_get_height(ui_module_detail_screen) - ofe_display_ui::CONTENT_TOP - ofe_display_ui::GAP;
    jbc_detail_root = lv_obj_create(ui_module_detail_screen);
    lv_obj_set_pos(jbc_detail_root, ofe_display_ui::MARGIN, ofe_display_ui::CONTENT_TOP); lv_obj_set_size(jbc_detail_root, width, height);
    lv_obj_set_style_pad_all(jbc_detail_root, 0, 0); lv_obj_set_style_border_width(jbc_detail_root, 0, 0);
    ofe_display_ui::flat(jbc_detail_root, ofe_display_ui::background(), 0);
    lv_obj_remove_flag(jbc_detail_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* back = ofe_display_ui::button(jbc_detail_root, 0, 0, 44, LV_SYMBOL_LEFT, lv_back_to_modules_event);
    lv_obj_set_height(back, 44);
    jbc_detail_category = lv_dropdown_create(jbc_detail_root);
    lv_obj_set_pos(jbc_detail_category, 54, 0); lv_obj_set_size(jbc_detail_category, width - 166, 44);
    ofe_display_ui::dropdown(jbc_detail_category);
    lv_obj_add_event_cb(jbc_detail_category, jbc_detail_nav_event, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(0));
    jbc_detail_port = lv_dropdown_create(jbc_detail_root);
    lv_obj_set_pos(jbc_detail_port, width - 104, 0); lv_obj_set_size(jbc_detail_port, 104, 44);
    ofe_display_ui::dropdown(jbc_detail_port);
    lv_obj_add_event_cb(jbc_detail_port, jbc_detail_nav_event, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(1));
    jbc_detail_identity = lv_label(jbc_detail_root, "", 0, 49, ofe_display_ui::text(), UI_FONT_DEFAULT, width - 100);
    lv_obj_set_height(jbc_detail_identity, 18); lv_label_set_long_mode(jbc_detail_identity, LV_LABEL_LONG_DOT);
    jbc_detail_link = lv_label(jbc_detail_root, "", width - 92, 49, ofe_display_ui::muted(), UI_FONT_DEFAULT, 92);
    lv_obj_set_height(jbc_detail_link, 18); lv_label_set_long_mode(jbc_detail_link, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(jbc_detail_link, LV_TEXT_ALIGN_RIGHT, 0);
    jbc_detail_list = lv_obj_create(jbc_detail_root);
    lv_obj_set_pos(jbc_detail_list, 0, 76); lv_obj_set_size(jbc_detail_list, width, height - 126);
    lv_obj_set_style_pad_all(jbc_detail_list, 0, 0); lv_obj_set_style_border_width(jbc_detail_list, 0, 0);
    lv_obj_set_style_bg_opa(jbc_detail_list, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(jbc_detail_list, LV_DIR_VER); lv_obj_set_scrollbar_mode(jbc_detail_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(jbc_detail_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    jbc_detail_prev = ofe_display_ui::button(jbc_detail_root, 0, height - 44, 44, LV_SYMBOL_LEFT, jbc_detail_noop);
    lv_obj_set_height(jbc_detail_prev, 44);
    lv_obj_add_event_cb(jbc_detail_prev, jbc_detail_nav_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(2));
    jbc_detail_next = ofe_display_ui::button(jbc_detail_root, width - 44, height - 44, 44, LV_SYMBOL_RIGHT, jbc_detail_noop);
    lv_obj_set_height(jbc_detail_next, 44);
    lv_obj_add_event_cb(jbc_detail_next, jbc_detail_nav_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(3));
    jbc_detail_page = lv_label(jbc_detail_root, "", 56, height - 32, ofe_display_ui::text(), UI_FONT_DEFAULT, width - 112);
    lv_obj_set_style_text_align(jbc_detail_page, LV_TEXT_ALIGN_CENTER, 0);
    jbc_detail_hint = lv_label(jbc_detail_root, "", 0, height - 70, ofe_display_ui::accent(), UI_FONT_DEFAULT, width);
    for (uint8_t i = 0; i < WINDOW; ++i) {
      jbc_detail_row[i] = lv_obj_create(jbc_detail_list);
      auto* row = jbc_detail_row[i];
      lv_obj_set_style_pad_all(row, 0, 0); lv_obj_set_style_border_width(row, 0, 0);
      ofe_display_ui::flat(row, ofe_display_ui::surface()); lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
      jbc_detail_label[i] = lv_label(row, "", 0, 0, ofe_display_ui::muted(), UI_FONT_DEFAULT, width);
      lv_label_set_long_mode(jbc_detail_label[i], LV_LABEL_LONG_WRAP);
      jbc_detail_text[i] = lv_label(row, "", 0, 0, ofe_display_ui::text(), UI_FONT_18, width);
      lv_label_set_long_mode(jbc_detail_text[i], LV_LABEL_LONG_WRAP);
      jbc_detail_spin[i] = jbc_detail_minus[i] = jbc_detail_plus[i] = nullptr;
      jbc_detail_switch[i] = jbc_detail_select[i] = nullptr;
      jbc_detail_activity[i] = nullptr;
    }
  }
  jbc_detail_refreshing = true;
  lv_obj_remove_flag(jbc_detail_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lv_obj_get_parent(ui_detail_values), LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_detail_title, LV_OBJ_FLAG_HIDDEN);
  if (!lv_dropdown_is_open(jbc_detail_category)) {
    const char* options = tr("Station\nPorts\nCounters\nSettings\nDiagnostics", "Station\nPorts\nZ\303\244hler\nEinstellungen\nDiagnose");
    if (strcmp(lv_dropdown_get_options(jbc_detail_category), options)) lv_dropdown_set_options(jbc_detail_category, options);
    lv_dropdown_set_selected(jbc_detail_category, snapshot.section);
  }
  if (!lv_dropdown_is_open(jbc_detail_port)) {
    String ports = "Port 1";
    for (uint8_t i = 1; i < snapshot.ports; ++i) ports += "\nPort " + String(i + 1);
    if (strcmp(lv_dropdown_get_options(jbc_detail_port), ports.c_str())) lv_dropdown_set_options(jbc_detail_port, ports.c_str());
    lv_dropdown_set_selected(jbc_detail_port, snapshot.port);
  }
  const bool has_port = snapshot.section == PORT || snapshot.section == COUNTERS || snapshot.section == SETTINGS;
  lv_obj_update_layout(jbc_detail_root);
  const int width = lv_obj_get_width(jbc_detail_root), height = lv_obj_get_height(jbc_detail_root);
  const bool wide = ofe_display_ui::wide(width);
  lv_obj_set_flag(jbc_detail_port, LV_OBJ_FLAG_HIDDEN, !has_port);
  lv_obj_set_width(jbc_detail_category, width - (has_port ? 166 : 54));
  lv_obj_set_state(jbc_detail_port, LV_STATE_DISABLED, !has_port || snapshot.pending);
  lv_obj_set_state(jbc_detail_category, LV_STATE_DISABLED, snapshot.pending);
  lv_obj_set_state(jbc_detail_prev, LV_STATE_DISABLED, !snapshot.base || snapshot.pending);
  lv_obj_set_state(jbc_detail_next, LV_STATE_DISABLED, snapshot.base + WINDOW >= snapshot.total || snapshot.pending);
  const String summary = String(selected_module.name[0] ? selected_module.name : "JBC USB") + "  |  " +
    (selected_module.jbc_usb_core.model[0] ? selected_module.jbc_usb_core.model : "-");
  lv_set_text_c(jbc_detail_link, (snapshot.flags & 2) ? tr("Connected", "Verbunden") : tr("Disconnected", "Getrennt"));
  char firmware[32], identity[80];
  snprintf(firmware, sizeof(firmware), "%u.%u.%u%s", unsigned(selected_module.fw_major),
    unsigned(selected_module.fw_minor), unsigned(selected_module.fw_patch), selected_module.fw_suffix);
  snprintf(identity, sizeof(identity), "0x%02X  FW %s  |  ", unsigned(selected_module.addr),
    selected_module.valid ? firmware : "...");
  lv_set_text(jbc_detail_identity, String(identity) + summary);
  const String page = String(snapshot.total ? snapshot.base / WINDOW + 1 : 0) + " / " + String((snapshot.total + WINDOW - 1) / WINDOW) +
    "    " + String(snapshot.total ? snapshot.base + 1 : 0) + " - " + String((snapshot.base + WINDOW < snapshot.total) ? snapshot.base + WINDOW : snapshot.total) + " / " + String(snapshot.total);
  lv_set_text(jbc_detail_page, page);
  lv_set_text_c(jbc_detail_hint, snapshot.pending ? (snapshot.acknowledged ? tr("Reading back...", "Wird zur\303\274ckgelesen...") : tr("Sending...", "Wird gesendet...")) : snapshot.result != 0xFF && snapshot.result != 0
    ? tr("Change rejected or timed out", "\303\204nderung abgelehnt oder Zeitlimit") : !snapshot.received
    ? tr("Loading...", "Wird geladen...") : !snapshot.total
    ? tr("No supported values", "Keine unterst\303\274tzten Werte") : !(snapshot.flags & 2)
    ? tr("Station disconnected", "Station getrennt") : "");
  const bool hint_visible = lv_label_get_text(jbc_detail_hint)[0] != 0;
  lv_obj_set_flag(jbc_detail_hint, LV_OBJ_FLAG_HIDDEN, !hint_visible);
  lv_obj_set_height(jbc_detail_list, height - 126 - (hint_visible ? 24 : 0));
  const uint8_t columns = wide || snapshot.section != SETTINGS ? 2 : 1;
  const int row_width = (width - (columns - 1) * 8) / columns;
  int y = 0, line_height = 0;
  uint8_t visible_slot = 0;
  for (uint8_t i = 0; i < WINDOW; ++i) {
    const Row& row = snapshot.rows[i];
    const bool shown = row.label[0] != 0 && snapshot.base + i < snapshot.total;
    lv_obj_set_flag(jbc_detail_row[i], LV_OBJ_FLAG_HIDDEN, !shown);
    if (!shown) { jbc_detail_row_controls(i, TEXT); continue; }
    const bool number = row.writable && row.kind == NUMBER;
    const bool toggle = row.writable && row.kind == SWITCH;
    const bool select = row.writable && row.kind == SELECT;
    jbc_detail_row_controls(i, number ? NUMBER : toggle ? SWITCH : select ? SELECT : TEXT);
    const char* label = row.label;
    if (snapshot.section == SETTINGS && !strncmp(label, "Port ", 5) && strlen(label) > 7 && label[6] == ' ') label += 7;
    lv_set_text_c(jbc_detail_label[i], label); lv_set_text_c(jbc_detail_text[i], row.text);
    const bool stacked = !wide && snapshot.section != SETTINGS;
    const auto activity = snapshot.section == STATION ? ofe_jbc_activity_ui::kind(label) : ofe_jbc_activity_ui::NONE;
    const int icon_space = activity == ofe_jbc_activity_ui::NONE ? 0 : 28;
    if (activity != ofe_jbc_activity_ui::NONE && !jbc_detail_activity[i]) {
      jbc_detail_activity[i] = lv_image_create(jbc_detail_row[i]);
      lv_obj_set_size(jbc_detail_activity[i], 20, 20);
    }
    if (jbc_detail_activity[i]) ofe_jbc_activity_ui::update(jbc_detail_activity[i], activity, row.text);
    const int label_width = (number ? row_width - 232 : toggle ? row_width - 88 : select ? row_width - 168 : stacked ? row_width - 16 : (row_width - 24) / 2) - icon_space;
    lv_obj_set_width(jbc_detail_label[i], label_width);
    const int label_height = jbc_detail_text_height(label, UI_FONT_DEFAULT, label_width);
    int row_height = label_height + 12; if (row_height < 52) row_height = 52;
    const lv_font_t* value_font = strlen(row.text) > 18 ? UI_FONT_DEFAULT : UI_FONT_18;
    lv_obj_set_style_text_font(jbc_detail_text[i], value_font, 0);
    lv_obj_set_flag(jbc_detail_text[i], LV_OBJ_FLAG_HIDDEN, number || toggle || select);
    int value_height = 0, value_y = 0;
    if (!number && !toggle && !select) {
      const int value_width = stacked ? row_width - 16 : row_width - label_width - icon_space - 24;
      lv_obj_set_width(jbc_detail_text[i], value_width);
      value_height = jbc_detail_text_height(row.text, value_font, value_width);
      if (stacked) { value_y = label_height + 10; row_height = value_y + value_height + 8; }
      else if (value_height + 12 > row_height) row_height = value_height + 12;
      lv_obj_set_style_text_align(jbc_detail_text[i], LV_TEXT_ALIGN_LEFT, 0);
    }
    row_height = (row_height + 3) & ~3;
    const uint8_t column = visible_slot++ % columns;
    lv_obj_set_pos(jbc_detail_row[i], column * (row_width + 8), y); lv_obj_set_size(jbc_detail_row[i], row_width, row_height);
    lv_obj_set_pos(jbc_detail_label[i], 8 + icon_space, stacked ? 6 : (row_height - label_height) / 2); lv_obj_set_height(jbc_detail_label[i], label_height);
    if (jbc_detail_activity[i]) lv_obj_set_pos(jbc_detail_activity[i], 8, stacked ? 6 : (row_height - 20) / 2);
    if (!number && !toggle && !select) {
      lv_obj_set_pos(jbc_detail_text[i], stacked ? 8 : label_width + icon_space + 16, stacked ? value_y : (row_height - value_height) / 2);
      lv_obj_set_height(jbc_detail_text[i], value_height);
    }
    if (number) {
      lv_set_text_c(jbc_detail_spin[i], row.text);
      lv_obj_set_pos(jbc_detail_minus[i], row_width - 216, (row_height - 44) / 2);
      lv_obj_set_pos(jbc_detail_spin[i], row_width - 168, (row_height - 44) / 2);
      lv_obj_set_pos(jbc_detail_plus[i], row_width - 48, (row_height - 44) / 2);
      lv_obj_set_state(jbc_detail_minus[i], LV_STATE_DISABLED, snapshot.pending || !(snapshot.flags & 2) || row.value <= row.minimum);
      lv_obj_set_state(jbc_detail_plus[i], LV_STATE_DISABLED, snapshot.pending || !(snapshot.flags & 2) || row.value >= row.maximum);
    } else if (toggle) {
      lv_obj_set_pos(jbc_detail_switch[i], row_width - 76, (row_height - 34) / 2);
      lv_obj_set_state(jbc_detail_switch[i], LV_STATE_CHECKED, row.value != 0);
      lv_obj_set_state(jbc_detail_switch[i], LV_STATE_DISABLED, snapshot.pending || !(snapshot.flags & 2));
    } else if (select) {
      lv_obj_set_pos(jbc_detail_select[i], row_width - 156, (row_height - 44) / 2);
      if (!lv_dropdown_is_open(jbc_detail_select[i])) lv_dropdown_set_selected(jbc_detail_select[i], row.value / 10);
      lv_obj_set_state(jbc_detail_select[i], LV_STATE_DISABLED, snapshot.pending || !(snapshot.flags & 2));
    }
    if (row_height > line_height) line_height = row_height;
    if (column == columns - 1) { y += line_height + 8; line_height = 0; }
  }
  jbc_detail_refreshing = false;
}
