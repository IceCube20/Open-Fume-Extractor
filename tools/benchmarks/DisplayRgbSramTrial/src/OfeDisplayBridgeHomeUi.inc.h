#pragma once
namespace ofe_module_home {
struct Card {
  lv_obj_t *root = nullptr, *stripe = nullptr, *title = nullptr, *identity = nullptr;
  lv_obj_t *power_label = nullptr, *power = nullptr, *rpm_label = nullptr, *rpm = nullptr, *detail = nullptr;
  uint8_t addr = 0, type = 0;
  int16_t height = 0;
};
static Card cards[17];
static uint8_t count = 0;
static bool supported(uint8_t type) {
  return type == MODULE_FAN_IO || type == MODULE_FAN_IO_PRO || type == MODULE_WELLER_ZERO_SMOG ||
    type == MODULE_UNIVERSAL_RS232 || type == MODULE_MODBUS_RTU;
}
static unsigned category(uint8_t type) {
  return type == MODULE_WELLER_ZERO_SMOG ? 2 : type == MODULE_UNIVERSAL_RS232 ? 3 :
    type == MODULE_MODBUS_RTU ? 4 : 1;
}
static int measure(lv_obj_t* label) {
  lv_point_t size;
  const int width = lv_obj_get_style_width(label, LV_PART_MAIN);
  lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
    0, lv_obj_get_style_text_line_space(label, LV_PART_MAIN), width, LV_TEXT_FLAG_NONE);
  return size.y;
}
static void open(lv_event_t* event) {
  const auto* card = static_cast<Card*>(lv_event_get_user_data(event));
  if (!card) return;
  for (uint8_t i = 0; i < module_total && i < 17; ++i)
    if (module_summaries[i].valid && module_summaries[i].addr == card->addr &&
        module_summaries[i].type == card->type) { lv_open_module_summary(i); return; }
}
static void create(Card& card, unsigned slot) {
  using namespace ofe_display_ui;
  card.root = lv_obj_create(ui_home_module_content);
  flat(card.root, surface()); lv_obj_set_style_pad_all(card.root, 0, 0);
  lv_obj_set_style_border_width(card.root, 1, 0); lv_obj_set_style_border_color(card.root, outline(), 0);
  lv_obj_remove_flag(card.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(card.root, open, LV_EVENT_CLICKED, &card);
  lv_obj_add_flag(card.root, LV_OBJ_FLAG_CLICKABLE);
  card.stripe = lv_obj_create(card.root); flat(card.stripe, accent(), 0);
  lv_obj_remove_flag(card.stripe, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_pos(card.stripe, 4, 12);
  card.title = lv_label(card.root, "", 48, 12, text(), UI_FONT_18, 280);
  card.identity = lv_label(card.root, "", 16, 40, muted(), UI_FONT_DEFAULT, 300);
  card.power_label = lv_label(card.root, tr("Power", "Leistung"), 16, 64, muted(), UI_FONT_DEFAULT, 120);
  card.power = lv_label(card.root, "--", 16, 84, text(), UI_FONT_22, 120);
  card.rpm_label = lv_label(card.root, tr("Speed", "Drehzahl"), 180, 64, muted(), UI_FONT_DEFAULT, 120);
  card.rpm = lv_label(card.root, "--", 180, 84, text(), UI_FONT_22, 150);
  card.detail = lv_label(card.root, "", 16, 116, muted(), UI_FONT_DEFAULT, 300);
  for (auto* label : {card.title, card.identity, card.detail}) lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  for (auto* label : {card.power, card.rpm}) lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  auto& fan = ofe_home_activity::fans[5 + slot]; fan = {};
  fan.image = lv_image_create(card.root); ofe_home_activity::prepare_fan(fan.image);
  lv_obj_set_pos(fan.image, 16, 12);
}
static void metric(lv_obj_t* label, lv_obj_t* value, bool visible, int x, int y, int width, bool columns) {
  lv_obj_set_flag(label, LV_OBJ_FLAG_HIDDEN, !visible);
  lv_obj_set_flag(value, LV_OBJ_FLAG_HIDDEN, !visible);
  if (!visible) return;
  lv_obj_set_pos(label, x, y); lv_obj_set_width(label, columns ? width : width / 2);
  lv_obj_set_pos(value, columns ? x : x + width / 2, columns ? y + 20 : y - 2);
  lv_obj_set_width(value, columns ? width : width / 2);
  lv_obj_set_style_text_align(value, columns ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
}
static void update(Card& card, unsigned slot, const DisplayModuleSummary& module, bool master_ok) {
  using namespace ofe_home_metrics;
  using namespace ofe_display_ui;
  const bool columns = wide(lv_obj_get_width(ui_dashboard_screen));
  const int width = columns ? 376 : 440;
  if (!card.root) create(card, slot);
  card.addr = module.addr; card.type = module.type;
  const uint32_t color = ofe_home_activity::accent(category(module.type));
  lv_obj_set_width(card.root, width);
  lv_obj_set_width(card.title, width - 64); lv_obj_set_width(card.identity, width - 32);
  lv_obj_set_width(card.detail, width - 32);
  const char* name = module.name[0] ? module.name : display_module_type_name(module.type);
  lv_set_text_c(card.title, name);
  State state; uint32_t received = 0;
  const bool have = home_metrics_get(module.addr, module.type, state, received);
  const bool online = master_ok && (module.flags & 1);
  const bool fresh = online && have && uint32_t(millis() - received) <= 10000U;
  const bool linked = !have || !(state.flags & LINK_KNOWN) || (state.flags & LINKED);
  const bool live = fresh && linked;
  // With an older Master only the selected main output has authoritative Home feedback.
  if (!have && online && module.addr == status.output_addr) {
    state.addr = module.addr; state.type = module.type;
    state.flags = HAS_OUTPUT | OUTPUT_VALID | HAS_POWER | HAS_RPM;
    if (status.output_enabled) state.flags |= OUTPUT_ON;
    state.power = status.output_power; state.rpm = status.fan_rpm;
  }
  const bool output = module_summary_provides_extractor_output(module) || (have && (state.flags & HAS_OUTPUT));
  const bool output_known = (live || (!have && online && module.addr == status.output_addr)) && (state.flags & OUTPUT_VALID);
  char identity[160];
  snprintf(identity, sizeof(identity), "%s · 0x%02X%s%s · %s", display_module_type_name(module.type),
    module.addr, module.addr == status.output_addr ? " · " : "",
    module.addr == status.output_addr ? tr("Main output", "Hauptausgang") : "",
    !online ? tr("Offline", "Offline") : !linked ? tr("Device disconnected", "Gerät getrennt") :
    !fresh && have ? tr("Cached", "Letzter Stand") :
    output_known ? (state.flags & OUTPUT_ON ? tr("On", "An") : tr("Off", "Aus")) : tr("Online", "Online"));
  lv_set_text_c(card.identity, identity);
  const int identity_y = 12 + measure(card.title) + 6;
  lv_obj_set_y(card.identity, identity_y);
  int y = identity_y + measure(card.identity) + 14;
  const bool show_power = output || (state.flags & HAS_POWER);
  const bool show_rpm = state.flags & HAS_RPM;
  char text_value[40];
  if ((live || (!have && online && module.addr == status.output_addr)) && (state.flags & HAS_POWER))
    snprintf(text_value, sizeof(text_value), "%u %%", unsigned(state.power / 10U));
  else strcpy(text_value, "--");
  lv_set_text_c(card.power, text_value);
  if ((live || (!have && online && module.addr == status.output_addr)) && show_rpm)
    snprintf(text_value, sizeof(text_value), "%u rpm", unsigned(state.rpm));
  else strcpy(text_value, "--");
  lv_set_text_c(card.rpm, text_value);
  if (columns) {
    metric(card.power_label, card.power, show_power, 16, y, 154, true);
    metric(card.rpm_label, card.rpm, show_rpm, show_power ? 188 : 16, y, 172, true);
    if (show_power || show_rpm) y += 54;
  } else {
    metric(card.power_label, card.power, show_power, 16, y, width - 32, false);
    if (show_power) y += 34;
    metric(card.rpm_label, card.rpm, show_rpm, 16, y, width - 32, false);
    if (show_rpm) y += 34;
  }
  String detail;
  auto line = [&](const String& value) { if (detail.length()) detail += "\n"; detail += value; };
  if (have && (state.flags & HAS_FILTER)) {
    const char* filter = state.filter == 1 ? tr("Very good", "Sehr gut") :
      state.filter == 10 ? tr("Replace soon", "Bald wechseln") :
      state.filter == 100 ? tr("Replace filter", "Filter wechseln") : tr("Unknown", "Unbekannt");
    line(String("Filter: ") + filter);
  }
  if (have && (state.flags & HAS_TIME)) {
    line(String(state.lifetime ? tr("Remaining: ", "Restlaufzeit: ") : tr("Filter runtime: ", "Filterbetrieb: ")) +
      fmt_dhm(state.lifetime ? ofe_home_metrics::remaining(state) : state.runtime));
  }
  if (have && (state.flags & HAS_PRESSURE)) {
    char pressure[64]; snprintf(pressure, sizeof(pressure), "%s: %d ADC · Filter %u %%",
      tr("Pressure", "Druck"), int(state.pressure), unsigned(state.saturation / 10U));
    line(pressure);
  }
  if (have && (state.flags & HAS_INPUT))
    line(String(tr("Main input: ", "Haupteingang: ")) + (fresh ? on_off(state.flags & INPUT_ON) : "--"));
  if (have && state.faults) {
    line(String(tr("Fault: ", "Fehler: ")) + fault_name(state.faults, module.type));
  }
  const bool profile = module.type == MODULE_UNIVERSAL_RS232 || module.type == MODULE_MODBUS_RTU;
  if (profile) {
    const auto* cache = universal_cache_find(module.addr, false);
    if (cache && cache->type == module.type) {
      unsigned shown = 0;
      for (uint8_t i = 0; i < cache->universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
        const auto& entity = cache->universal_entities[i];
        if (!entity.valid || entity.id < 20) continue;
        String value = (entity.flags & 1) ? universal_entity_value_text(entity) : tr("Control", "Steuerung");
        if (!online) value = "--";
        line(String(entity.label[0] ? entity.label : "Entity") + ": " + value);
        ++shown;
      }
      if (cache->universal_entity_count < cache->universal_entity_total)
        line(tr("Loading profile values", "Profilwerte werden geladen"));
      else if (!shown) line(tr("No profile entities configured", "Keine Profil-Entitäten konfiguriert"));
    } else line(tr("Loading profile values", "Profilwerte werden geladen"));
  }
  if (!have && !output && !profile) line(tr("Waiting for operating data", "Betriebsdaten werden geladen"));
  if (have && !live && (state.flags & (HAS_FILTER | HAS_TIME | HAS_PRESSURE)))
    line(tr("Last known filter values", "Letzte bekannte Filterwerte"));
  if (have && !profile && !(state.flags & (HAS_OUTPUT | HAS_INPUT | HAS_FILTER | HAS_TIME | HAS_PRESSURE)))
    line(tr("No main role configured", "Keine Hauptrolle konfiguriert"));
  lv_set_text(card.detail, detail);
  lv_obj_set_flag(card.detail, LV_OBJ_FLAG_HIDDEN, !detail.length());
  lv_obj_set_y(card.detail, y);
  card.height = int16_t(y + (detail.length() ? measure(card.detail) : 0) + 14);
  const bool warning = have && (state.flags & HAS_FILTER) && state.filter == 10;
  const bool fault = have && ((state.faults && !(warning && state.faults == 0x0002U)) ||
    ((state.flags & HAS_FILTER) && state.filter == 100));
  const lv_color_t border = fault ? lv_color_hex(0xFF4D5E) : warning ? lv_color_hex(0xFFB020) : outline();
  if (!lv_color_eq(lv_obj_get_style_border_color(card.root, LV_PART_MAIN), border))
    lv_obj_set_style_border_color(card.root, border, 0);
  if (!lv_color_eq(lv_obj_get_style_bg_color(card.stripe, LV_PART_MAIN), lv_color_hex(color)))
    lv_obj_set_style_bg_color(card.stripe, lv_color_hex(color), 0);
  ofe_home_activity::set_fan(5 + slot, output, output_known && (state.flags & OUTPUT_ON),
    state.flags & HAS_POWER ? state.power / 10U : 100, color);
}
} // namespace ofe_module_home

static void lv_create_bridge_home_cards() {
  ofe_module_home::count = 0;
  for (auto& card : ofe_module_home::cards) card = {};
  for (unsigned i = 5; i < 22; ++i) ofe_home_activity::fans[i] = {};
}
static void lv_update_bridge_home_cards(bool master_ok) {
  const DisplayModuleSummary* sorted[17]; unsigned count = 0;
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    const auto& module = module_summaries[i];
    if (!module.valid || !ofe_module_home::supported(module.type)) continue;
    unsigned at = count;
    while (at && (ofe_module_home::category(sorted[at - 1]->type) > ofe_module_home::category(module.type) ||
        (ofe_module_home::category(sorted[at - 1]->type) == ofe_module_home::category(module.type) &&
         (sorted[at - 1]->type > module.type ||
          (sorted[at - 1]->type == module.type && sorted[at - 1]->addr > module.addr))))) {
      sorted[at] = sorted[at - 1]; --at;
    }
    sorted[at] = &module; ++count;
  }
  ofe_module_home::count = uint8_t(count);
  for (unsigned i = 0; i < 17; ++i) {
    auto& card = ofe_module_home::cards[i];
    if (i < count) {
      ofe_module_home::update(card, i, *sorted[i], master_ok);
      lv_obj_remove_flag(card.root, LV_OBJ_FLAG_HIDDEN);
    } else if (card.root) {
      lv_obj_add_flag(card.root, LV_OBJ_FLAG_HIDDEN);
      ofe_home_activity::set_fan(5 + i, false, false);
    }
  }
}
static int16_t lv_layout_module_home_cards(int16_t y, int width, int x) {
  for (unsigned i = 0; i < ofe_module_home::count; ++i) {
    auto& card = ofe_module_home::cards[i];
    lv_obj_set_pos(card.root, x, y); lv_obj_set_size(card.root, width, card.height);
    lv_home_accent_layout(card.stripe, card.height);
    y += card.height + 8;
  }
  return y;
}
