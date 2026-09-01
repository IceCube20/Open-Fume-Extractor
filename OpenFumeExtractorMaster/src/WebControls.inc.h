#pragma once

// Web control handlers for extractor settings, routing, module actions and restart.

static void web_handle_scan() {
  heap_diag_set_context(HEAP_DIAG_CTX_WEB, "module_scan");
  heap_diag_sample("module_scan_begin");
  if (web.hasArg("addr")) {
    if (!master_cmd_probe_module((uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0))) {
      web.send(503, "text/plain; charset=utf-8", "scan command queue busy");
      return;
    }
  } else {
    const bool address_bus = web.hasArg("address") || web.hasArg("auto_address");

    // Explicit Web actions intentionally have different semantics:
    // - Module scannen: online discovery is authoritative -> prune missing.
    // - Adressvergabe: assign/repair addresses -> preserve offline modules.
    // JOIN/hotplug discovery is also non-pruning.
    const bool prune_missing = !address_bus;

    if (!master_cmd_request_scan(address_bus, prune_missing)) {
      web.send(503, "text/plain; charset=utf-8", "scan command queue busy");
      return;
    }
  }
  heap_diag_sample("module_scan_end");
  heap_diag_clear_context(HEAP_DIAG_CTX_WEB);
  web.send(200, "application/json", "{\"ok\":true}");
}

static void web_handle_module_label() {
  if (!web.hasArg("uid")) {
    web.send(400, "text/plain; charset=utf-8", "missing uid");
    return;
  }
  const uint64_t uid = strtoull(web.arg("uid").c_str(), nullptr, 16);
  if (!uid) {
    web.send(400, "text/plain; charset=utf-8", "bad uid");
    return;
  }
  String label = clean_module_label(web.arg("label"));
  ModuleRecord* target = nullptr;
  if (web.hasArg("addr")) {
    ModuleRecord* by_addr = registry.find((uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0));
    if (by_addr && by_addr->uid == uid) target = by_addr;
  }
  if (!target) {
    for (uint8_t i = 0; i < registry.count(); ++i) {
      ModuleRecord& m = registry.at(i);
      if (m.uid == uid) { target = &m; break; }
    }
  }
  if (!target) {
    web.send(404, "text/plain; charset=utf-8", "module not found; label must be written into the module");
    return;
  }
  if (target->uid != uid) {
    web.send(409, "text/plain; charset=utf-8", "module identity mismatch");
    return;
  }
  if (!target->online) {
    web.send(404, "text/plain; charset=utf-8", "module offline; label must be written into the module");
    return;
  }
  if (!master_cmd_set_module_label(target->addr, uid, label.c_str())) {
    web.send(503, "text/plain; charset=utf-8", "module did not accept label; check module firmware/NVS");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", label.length() ? "label saved in module" : "label cleared in module");
}
static void web_handle_io_alias() {
  if (!web.hasArg("uid") || !web.hasArg("key")) {
    web.send(400, "text/plain; charset=utf-8", "missing uid or key");
    return;
  }
  const uint64_t uid = strtoull(web.arg("uid").c_str(), nullptr, 16);
  if (!uid) {
    web.send(400, "text/plain; charset=utf-8", "bad uid");
    return;
  }
  String key = web.arg("key");
  if (key != "main" && key != "in1" && key != "in2" && key != "out1" && key != "out2") {
    web.send(400, "text/plain; charset=utf-8", "bad key");
    return;
  }
  ModuleRecord* target = nullptr;
  if (web.hasArg("addr")) {
    ModuleRecord* by_addr = registry.find((uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0));
    if (by_addr && by_addr->uid == uid) target = by_addr;
  }
  if (!target) {
    for (uint8_t i = 0; i < registry.count(); ++i) {
      ModuleRecord& m = registry.at(i);
      if (m.uid == uid) { target = &m; break; }
    }
  }
  if (!target) {
    web.send(404, "text/plain; charset=utf-8", "module not found");
    return;
  }
  const uint8_t channel = key == "main" ? 4 : (key == "in1" ? 0 : (key == "in2" ? 1 : (key == "out1" ? 2 : 3)));
  String alias = clean_io_alias(web.arg("alias"));
  if (!master_cmd_set_io_alias(target->addr, channel, alias.c_str())) {
    web.send(500, "text/plain; charset=utf-8", "module did not accept alias");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", alias.length() ? "alias saved" : "alias cleared");
}

static bool web_jbc_station_name_valid(const String& name) {
  static const char allowed[] = " 0123456789QWERTYUIOPASDFGHJKLMNBVCXZ'!?$%&@-=,.;()[]";
  if (name.length() > 16U) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const unsigned char raw = (unsigned char)name[i];
    if (raw == 0U || raw >= 0x80U || !strchr(allowed, toupper(raw))) return false;
  }
  return true;
}

static void web_handle_jbc_usb_station_name() {
  if (!web.hasArg("addr") || !web.hasArg("name")) {
    web.send(400, "text/plain; charset=utf-8",
             web_text("Adresse oder Stationsname fehlt", "Address or station name missing"));
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  ModuleRecord* target = registry.find(addr);
  if (!target || !target->online ||
      !(target->type == MODULE_JBC_USB || (target->caps & CAP_JBC_USB))) {
    web.send(404, "text/plain; charset=utf-8",
             web_text("JBC-USB-Modul nicht online", "JBC USB module is not online"));
    return;
  }
  const String name = web.arg("name");
  if (!web_jbc_station_name_valid(name)) {
    web.send(400, "text/plain; charset=utf-8",
             web_text("Ungültiger Stationsname: maximal 16 unterstützte JBC-Zeichen",
                      "Invalid station name: use at most 16 supported JBC characters"));
    return;
  }
  if (!master_cmd_set_jbc_usb_station_name(addr, name.c_str())) {
    web.send(409, "text/plain; charset=utf-8",
             web_text("Stationsname konnte nicht geschrieben werden; Verbindung oder Modul ist beschäftigt",
                      "Station name could not be written; link or module is busy"));
    return;
  }
  web.send(202, "text/plain; charset=utf-8",
           web_text("Stationsname wird geschrieben und anschließend geprüft",
                    "Station name is being written and verified"));
}

static void web_handle_jbc_usb_config() {
  if (!web.hasArg("addr") || !web.hasArg("action")) {
    web.send(400, "text/plain; charset=utf-8",
             web_text("Adresse oder Aktion fehlt", "Address or action missing"));
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  ModuleRecord* target = registry.find(addr);
  if (!target || !target->online ||
      !(target->type == MODULE_JBC_USB || (target->caps & CAP_JBC_USB))) {
    web.send(404, "text/plain; charset=utf-8",
             web_text("JBC-USB-Modul nicht online", "JBC USB module is not online"));
    return;
  }

  uint8_t payload[23] = {0};
  uint8_t len = 0;
  const String action = web.arg("action");
  const uint8_t port = web.hasArg("port")
                         ? (uint8_t)strtoul(web.arg("port").c_str(), nullptr, 10) : 0;
  if (port >= 4U) {
    web.send(400, "text/plain; charset=utf-8", "bad port");
    return;
  }
  auto arg_u16 = [&](const String& key, uint16_t& value) -> bool {
    if (!web.hasArg(key)) return false;
    const String raw = web.arg(key);
    char* end = nullptr;
    const unsigned long parsed = strtoul(raw.c_str(), &end, 10);
    if (!end || *end || parsed > 65534UL) return false;
    value = (uint16_t)parsed;
    return true;
  };

  if (action == "temperature" || action == "flow") {
    uint16_t value = 0;
    if (!arg_u16("value", value)) {
      web.send(400, "text/plain; charset=utf-8",
               web_text("Ungültiger Wert", "Invalid value"));
      return;
    }
    payload[0] = action == "temperature" ? JBC_USB_CONFIG_SELECTED_TEMP
                                          : JBC_USB_CONFIG_SELECTED_FLOW;
    payload[1] = port;
    put_u16_le(payload + 2, value);
    len = 4;
  } else if (action == "levels") {
    if (!web.hasArg("enabled") || !web.hasArg("selected") || !web.hasArg("mask")) {
      web.send(400, "text/plain; charset=utf-8", "level fields missing");
      return;
    }
    const uint8_t enabled = web.arg("enabled").toInt() ? 1U : 0U;
    const uint8_t selected = (uint8_t)strtoul(web.arg("selected").c_str(), nullptr, 10);
    const uint8_t mask = (uint8_t)strtoul(web.arg("mask").c_str(), nullptr, 10);
    if (selected > 2U || mask > 7U) {
      web.send(400, "text/plain; charset=utf-8", "bad level selection");
      return;
    }
    payload[0] = JBC_USB_CONFIG_LEVELS;
    payload[1] = port;
    payload[2] = enabled;
    payload[3] = selected;
    payload[4] = mask;
    uint8_t o = 5;
    for (uint8_t group = 0; group < 3; ++group) {
      for (uint8_t level = 0; level < 3; ++level) {
        const char prefix = group == 0 ? 't' : (group == 1 ? 'f' : 'e');
        String key; key += prefix; key += level;
        uint16_t value = 0;
        if (!arg_u16(key, value)) {
          web.send(400, "text/plain; charset=utf-8", "bad level value");
          return;
        }
        put_u16_le(payload + o, value);
        o += 2;
      }
    }
    len = o;
  } else {
    web.send(400, "text/plain; charset=utf-8",
             web_text("Unbekannte Aktion", "Unknown action"));
    return;
  }

  if (!master_cmd_set_jbc_usb_config(addr, payload, len)) {
    web.send(409, "text/plain; charset=utf-8",
             web_text("Wert konnte nicht geschrieben werden; Station oder Modul ist beschäftigt",
                      "Value could not be written; station or module is busy"));
    return;
  }
  web.send(202, "text/plain; charset=utf-8",
           web_text("Wert wird geschrieben und anschließend geprüft",
                    "Value is being written and verified"));
}

static void web_handle_not_found() {
  if (captive_active) web_redirect_config();
  else web.send(404, "text/plain; charset=utf-8", "Not found");
}

static uint16_t web_arg_u16(const char* name, uint16_t fallback, uint16_t max_value) {
  if (!web.hasArg(name)) return fallback;
  uint32_t v = (uint32_t)strtoul(web.arg(name).c_str(), nullptr, 10);
  if (v > max_value) v = max_value;
  return (uint16_t)v;
}

static uint16_t customPowerFromSelectFlow(uint16_t select_flow) {
  uint16_t percent = select_flow;
  // select_flow is stored as permille-like JBC flow (10% = 100, 100% = 1000).
  // The exact boundary value 100 therefore means 10%, not 100%.
  if (percent >= 100) percent = (percent + 5U) / 10U;
  if (percent < 10) percent = 10;
  if (percent > 100) percent = 100;
  return percent;
}

static String route_key(uint8_t addr, uint8_t bit) {
  return MasterSettingsStore::routeKey(addr, bit);
}

static String rule_key(uint8_t index, const char* suffix) {
  return MasterSettingsStore::ruleKey(index, suffix);
}

static void load_input_action_rules() {
  for (uint8_t i = 0; i < MasterScheduler::MAX_INPUT_RULES; ++i) {
    const MasterSettingsStore::InputActionRuleSettings stored =
      MasterSettingsStore::loadRuleOpen(master_prefs, i, MasterScheduler::INPUT_SRC_NONE, MasterScheduler::INPUT_TGT_NONE);
    MasterScheduler::InputActionRule rule;
    rule.enabled = stored.enabled;
    rule.source_type = stored.source_type;
    rule.source_addr = stored.source_addr;
    rule.source_bit = stored.source_bit;
    rule.target_type = stored.target_type;
    rule.target_addr = stored.target_addr;
    rule.target_bit = stored.target_bit;
    scheduler.setInputRule(i, rule);
  }
}

static void save_input_action_rule(uint8_t index, const MasterScheduler::InputActionRule& rule) {
  MasterSettingsStore::InputActionRuleSettings stored;
  stored.enabled = rule.enabled;
  stored.source_type = rule.source_type;
  stored.source_addr = rule.source_addr;
  stored.source_bit = rule.source_bit;
  stored.target_type = rule.target_type;
  stored.target_addr = rule.target_addr;
  stored.target_bit = rule.target_bit;
  MasterSettingsStore::saveRuleOpen(master_prefs, index, stored);
}

static void load_control_settings() {
  const MasterSettingsStore::ControlSettings s = MasterSettingsStore::loadControl(master_prefs);
  scheduler.setControlSettings(s.suction, s.select_flow, s.delay_work_sec, s.delay_stand_sec, s.stand_intakes, s.continuous, false);
  scheduler.setAfterrunPowerProfile(s.afterrun_power_enabled, s.afterrun_power, false);
}

static void apply_control_settings(uint8_t suction, uint16_t select_flow, uint16_t delay_work,
                                   uint16_t delay_stand, bool stand_intakes, bool continuous,
                                   bool persist) {
  if (suction > 3) suction = 3;
  uint16_t percent = customPowerFromSelectFlow(select_flow);
  select_flow = percent * 10U;
  if (delay_work > 3600) delay_work = 3600;
  if (delay_stand > 3600) delay_stand = 3600;

  master_cmd_apply_control_settings(
    suction, select_flow, delay_work, delay_stand,
    stand_intakes, continuous, persist);
}
static void load_routing_config() {
  const MasterSettingsStore::MainInputSettings main_input =
    MasterSettingsStore::loadMainInput(master_prefs, MasterScheduler::INPUT_SRC_JBC_WORK, MasterScheduler::INPUT_SRC_NONE);
  scheduler.setMainInputSource(main_input.type, main_input.addr, main_input.bit, false);
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  load_input_action_rules();
  for (uint8_t i = 0; i < registry.count(); ++i) {
    ModuleRecord& m = registry.at(i);
    if (!(m.caps & CAP_INPUT_KEYS)) continue;
    scheduler.setIoInputRoute(m.addr, 0, master_prefs.getUChar(route_key(m.addr, 0).c_str(), 0) != 0);
    scheduler.setIoInputRoute(m.addr, 1, master_prefs.getUChar(route_key(m.addr, 1).c_str(), 0) != 0);
  }
  master_prefs.end();
}

static bool persist_control_settings_now() {
  const JbcModuleState& cs = scheduler.controlSettings();
  MasterSettingsStore::ControlSettings s;
  s.suction = cs.suction_level;
  s.select_flow = cs.select_flow;
  s.delay_work_sec = cs.delay_work_sec;
  s.delay_stand_sec = cs.delay_stand_sec;
  s.stand_intakes = cs.stand_intakes != 0;
  s.afterrun_power_enabled = scheduler.afterrunPowerProfileEnabled();
  s.afterrun_power = scheduler.afterrunPower();
  Preferences prefs;
  return MasterSettingsStore::saveControl(prefs, s);
}
static void web_handle_jbc_settings() {
  const JbcModuleState& cs = scheduler.controlSettings();
  const uint8_t suction = (uint8_t)web_arg_u16("suction", cs.suction_level, 3);
  uint16_t custom_percent = web_arg_u16("select", customPowerFromSelectFlow(cs.select_flow), 100);
  if (custom_percent < 10) custom_percent = 10;
  if (custom_percent > 100) custom_percent = 100;
  const uint16_t select_flow = custom_percent * 10U;
  const uint16_t delay_work = web_arg_u16("delay_work", cs.delay_work_sec, 3600);
  const uint16_t delay_stand = web_arg_u16("delay_stand", cs.delay_stand_sec, 3600);
  const bool stand_intakes = web.hasArg("stand_intakes") ? web.arg("stand_intakes") != "0" : cs.stand_intakes;
  const bool continuous = web.hasArg("continuous") ? web.arg("continuous") != "0" : cs.continuous;
  const bool afterrun_power_enabled = web.hasArg("afterrun_power_enabled") ? web.arg("afterrun_power_enabled") != "0" : scheduler.afterrunPowerProfileEnabled();
  uint16_t afterrun_power_pct = web_arg_u16("afterrun_power", (scheduler.afterrunPower() + 5U) / 10U, 100);
  const uint16_t afterrun_min_pct = scheduler.activeOutputMinSelectFlow() / 10U;
  if (afterrun_power_pct < afterrun_min_pct) afterrun_power_pct = afterrun_min_pct;
  if (afterrun_power_pct > 100) afterrun_power_pct = 100;

  apply_control_settings(suction, select_flow, delay_work, delay_stand, stand_intakes, continuous, false);
  master_cmd_set_afterrun_power_profile(afterrun_power_enabled, afterrun_power_pct * 10U, false);
  if (!persist_control_settings_now()) {
    web.send(507, "text/plain; charset=utf-8", "NVS full or settings write failed");
    return;
  }
  mqtt_last_publish_ms = 0;
  web.send(200, "text/plain; charset=utf-8", web_text("Einstellungen gespeichert", "Settings saved"));
}
static void web_handle_io_set() {
  if (!web.hasArg("addr") || !web.hasArg("bit") || !web.hasArg("value")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr/bit/value");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  const uint8_t bit = (uint8_t)web.arg("bit").toInt();
  if (bit > 15) {
    web.send(400, "text/plain; charset=utf-8", "bad bit");
    return;
  }
  const uint16_t mask = (uint16_t)(1U << bit);
  const uint16_t value = web.arg("value").toInt() ? mask : 0;
  if (!master_cmd_set_io_output(addr, mask, value)) {
    web.send(500, "text/plain; charset=utf-8", "io set failed");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_module_output_set() {
  if (!web.hasArg("addr")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  bool enabled = false;
  uint16_t power = 0;
  const ModuleRecord* rec = registry.find(addr);
  if (rec && rec->output_status_valid) {
    enabled = rec->output_enabled;
    power = rec->output_power;
  }
  const bool has_enable = web.hasArg("enable");
  const bool has_power = web.hasArg("power");
  if (has_enable) enabled = web.arg("enable") != "0";
  if (has_power) {
    uint32_t pct = (uint32_t)strtoul(web.arg("power").c_str(), nullptr, 10);
    if (pct > 100UL) pct = 100UL;
    if (pct < 10UL) pct = 10UL;
    power = (uint16_t)(pct * 10UL);
  }
  bool ok = false;
  if (has_power && !has_enable) ok = master_cmd_set_module_power(addr, power);
  else ok = master_cmd_set_module_output(addr, enabled, power);
  if (!ok) {
    web.send(500, "text/plain; charset=utf-8", "module output set failed");
    return;
  }
  mqtt_last_publish_ms = 0;
  web.send(200, "text/plain; charset=utf-8", "ok");
}
static void web_handle_fanio_calibration() {
  if (!web.hasArg("addr") || !web.hasArg("action")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr/action");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  const uint8_t action = (uint8_t)strtoul(web.arg("action").c_str(), nullptr, 0);
  uint16_t warn_raw = 0;
  uint16_t full_raw = 0;
  if (action == 3) {
    if (!web.hasArg("warn") || !web.hasArg("full")) {
      web.send(400, "text/plain; charset=utf-8", "missing warn/full");
      return;
    }
    warn_raw = (uint16_t)strtoul(web.arg("warn").c_str(), nullptr, 10);
    full_raw = (uint16_t)strtoul(web.arg("full").c_str(), nullptr, 10);
    if (full_raw <= warn_raw) {
      web.send(400, "text/plain; charset=utf-8", "full must be greater than warn");
      return;
    }
  } else if (action == 5) {
    if (!web.hasArg("enabled")) {
      web.send(400, "text/plain; charset=utf-8", "missing enabled");
      return;
    }
    warn_raw = web.arg("enabled").toInt() ? 1 : 0;
  }
  if (action < 1 || action > 5) {
    web.send(400, "text/plain; charset=utf-8", "bad action");
    return;
  }
  if (!master_cmd_calibrate_fanio_filter(addr, action, warn_raw, full_raw)) {
    web.send(500, "text/plain; charset=utf-8", "filter calibration failed");
    return;
  }
  mqtt_last_publish_ms = 0;
  web.send(200, "text/plain; charset=utf-8", "ok");
}
static void web_handle_weller_set() {
  if (!web.hasArg("addr")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  if (web.hasArg("speed")) {
    uint8_t speed = (uint8_t)web.arg("speed").toInt();
    if (!master_cmd_set_weller_speed(addr, speed)) {
      web.send(500, "text/plain; charset=utf-8", "speed set failed");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "ok");
    return;
  }
  if (web.hasArg("reset_filter")) {
    if (!master_cmd_reset_weller_filter(addr)) {
      web.send(500, "text/plain; charset=utf-8", "filter reset failed");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "ok");
    return;
  }
  if (web.hasArg("filter_minutes")) {
    uint32_t minutes = strtoul(web.arg("filter_minutes").c_str(), nullptr, 10);
    if (minutes < 60UL) minutes = 60UL;
    if (minutes > 9990UL) minutes = 9990UL;
    if (!master_cmd_set_weller_filter_runtime(addr, (uint16_t)minutes)) {
      web.send(500, "text/plain; charset=utf-8", "filter runtime set failed");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "ok");
    return;
  }
  web.send(400, "text/plain; charset=utf-8", "missing command");
}

static void web_handle_display_set() {
  if (!web.hasArg("addr")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  const uint8_t brightness = web.hasArg("brightness") ? (uint8_t)web.arg("brightness").toInt() : 0xFF;
  const uint8_t language = web.hasArg("language") ? (uint8_t)web.arg("language").toInt() : 0xFF;
  const uint8_t theme = web.hasArg("theme") ? (uint8_t)web.arg("theme").toInt() : 0xFF;
  const uint8_t screensaver = web.hasArg("screensaver") ? (uint8_t)web.arg("screensaver").toInt() : 0xFF;
  if (!master_cmd_set_display_settings(addr, brightness, language, theme, screensaver)) {
    web.send(500, "text/plain; charset=utf-8", "display settings failed");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_universal_profile() {
  if (!web.hasArg("addr")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  ModuleRecord* rec = registry.find(addr);
  if (!rec || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) {
    web.send(404, "text/plain; charset=utf-8", "universal module not found");
    return;
  }
  String profile = clean_profile_value(web.arg("profile"), 31);
  String station = clean_profile_value(web.arg("station"), 31);
  String baud = clean_profile_value(web.arg("baud"), 12);
  String frame = clean_profile_value(web.arg("frame"), 12);
  String protocol = clean_profile_value(web.arg("protocol"), 16);
  String checksum = clean_profile_value(web.arg("checksum"), 24);
  String line_end = clean_profile_value(web.arg("line_end"), 8);
  const bool profile_text_arg = web.hasArg("profile_text");
  String profile_text = profile_text_arg ? web.arg("profile_text") : String();
  if (profile_text.length() > 8192) {
    web.send(400, "text/plain; charset=utf-8", "profile text too large");
    return;
  }
  const bool is_modbus = rec->type == MODULE_MODBUS_RTU;
  if (!profile.length()) profile = is_modbus ? "Generic Modbus RTU" : "Generic RS232";
  if (!station.length()) station = is_modbus ? "Modbus device" : "Community device";
  if (!baud.length()) baud = "9600";
  if (!frame.length()) frame = "8N1";
  if (!protocol.length()) protocol = is_modbus ? "MODBUS_RTU" : "ASCII";
  if (!checksum.length()) checksum = is_modbus ? "CRC16_MODBUS_LE" : "NONE";
  if (!line_end.length()) line_end = is_modbus ? "NONE" : "CR";
  frame.toUpperCase();
  checksum.toUpperCase();
  line_end.toUpperCase();
  if (frame != "8N1" && frame != "8E1" && frame != "8O1" && frame != "7E1") {
    web.send(400, "text/plain; charset=utf-8", "bad serial frame");
    return;
  }
  if (line_end != "NONE" && line_end != "CR" && line_end != "LF" && line_end != "CRLF") {
    web.send(400, "text/plain; charset=utf-8", "bad line ending");
    return;
  }
  if (checksum != "NONE" && checksum != "WELLER_SUM8" && checksum != "XOR8_HEX" &&
      checksum != "SUM8_HEX" && checksum != "CRC16_MODBUS_LE" &&
      checksum != "XOR8_RAW" && checksum != "SUM8_RAW" && checksum != "CRC16_CCITT_BE" && checksum != "CRC16_CCITT_LE") {
    web.send(400, "text/plain; charset=utf-8", "bad checksum preset");
    return;
  }
  const uint32_t baud_value = (uint32_t)strtoul(baud.c_str(), nullptr, 10);
  if (baud_value < 300UL || baud_value > 1000000UL) {
    web.send(400, "text/plain; charset=utf-8", "bad baudrate");
    return;
  }

  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  master_prefs.putString(universal_profile_key(addr, "prof").c_str(), profile);
  master_prefs.putString(universal_profile_key(addr, "stat").c_str(), station);
  master_prefs.putString(universal_profile_key(addr, "baud").c_str(), String(baud_value));
  master_prefs.putString(universal_profile_key(addr, "frm").c_str(), frame);
  master_prefs.putString(universal_profile_key(addr, "proto").c_str(), protocol);
  master_prefs.putString(universal_profile_key(addr, "csum").c_str(), checksum);
  master_prefs.putString(universal_profile_key(addr, "lend").c_str(), line_end);
  master_prefs.end();

  if (rec->online && !master_cmd_set_universal_profile(addr, profile.c_str(), station.c_str(), baud_value, frame.c_str(), protocol.c_str(), checksum.c_str(), line_end.c_str(), profile_text_arg ? profile_text.c_str() : nullptr)) {
    web.send(503, "text/plain; charset=utf-8", "profile saved in master, but module did not acknowledge profile update");
    return;
  }
  web.send(rec->online ? 200 : 202, "text/plain; charset=utf-8", rec->online ? "profile saved" : "profile saved in master");
}

static int universal_hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void web_handle_universal_profile_read() {
  if (!web.hasArg("addr")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  ModuleRecord* rec = registry.find(addr);
  if (!rec || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) {
    web.send(404, "text/plain; charset=utf-8", "universal module not found");
    return;
  }
  if (!rec->online) {
    web.send(503, "text/plain; charset=utf-8", "module offline");
    return;
  }
  static char profile_text[8193];
  uint32_t crc = 0;
  bool truncated = false;
  if (!master_cmd_read_universal_profile(addr, profile_text, sizeof(profile_text), &crc, &truncated)) {
    web.send(503, "text/plain; charset=utf-8", "module did not return profile text");
    return;
  }
  web.sendHeader("X-OFE-Profile-CRC", String(crc, HEX));
  web.sendHeader("X-OFE-Truncated", truncated ? "1" : "0");
  web.send(200, "text/plain; charset=utf-8", profile_text);
}

static void web_handle_universal_entity() {
  if (!web.hasArg("addr") || !web.hasArg("id")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr or id");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  const uint8_t entity_id = (uint8_t)strtoul(web.arg("id").c_str(), nullptr, 0);
  ModuleRecord* rec = registry.find(addr);
  if (!rec || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) {
    web.send(404, "text/plain; charset=utf-8", "universal module not found");
    return;
  }
  if (!rec->online) {
    web.send(503, "text/plain; charset=utf-8", "universal module offline");
    return;
  }

  // Respect the profile access mode server-side as well as in the UI. The
  // fourth descriptor token is the normalized RO/RW/WO mode emitted by the
  // Universal/Modbus bridge. Unknown legacy descriptors fall through to the
  // module, which performs the same authoritative check.
  bool access_known = false;
  bool access_writable = false;
  if (rec->universal_descriptor_valid && rec->universal_descriptor[0]) {
    const char* scan = rec->universal_descriptor;
    while (scan && *scan) {
      const char* next = strchr(scan, '\n');
      char line[256];
      size_t n = next ? (size_t)(next - scan) : strlen(scan);
      if (n >= sizeof(line)) n = sizeof(line) - 1;
      memcpy(line, scan, n); line[n] = 0;
      unsigned id = 0; char type[20] = {0}, key[32] = {0}, mode[8] = {0};
      if (sscanf(line, "%u %19s %31s %7s", &id, type, key, mode) == 4 && id == entity_id) {
        for (char* q = mode; *q; ++q) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        if (!strcmp(mode, "ro") || !strcmp(mode, "rw") || !strcmp(mode, "wo")) {
          access_known = true;
          access_writable = !strcmp(mode, "rw") || !strcmp(mode, "wo");
        }
        break;
      }
      scan = next ? next + 1 : nullptr;
    }
  }
  if (access_known && !access_writable) {
    web.send(403, "text/plain; charset=utf-8", "entity is read-only");
    return;
  }

  uint8_t payload[MAX_PAYLOAD - 2];
  uint8_t len = 0;
  if (web.hasArg("hex")) {
    String hex = web.arg("hex");
    hex.trim();
    int high = -1;
    for (uint16_t i = 0; i < hex.length(); ++i) {
      const char c = hex[i];
      if (c == ' ' || c == ':' || c == '-' || c == '_' || c == ',' || c == '\n' || c == '\r' || c == '\t') continue;
      int v = universal_hex_nibble(c);
      if (v < 0) {
        web.send(400, "text/plain; charset=utf-8", "bad hex payload");
        return;
      }
      if (high < 0) high = v;
      else {
        if (len >= sizeof(payload)) {
          web.send(413, "text/plain; charset=utf-8", "payload too large");
          return;
        }
        payload[len++] = (uint8_t)((high << 4) | v);
        high = -1;
      }
    }
    if (high >= 0) {
      web.send(400, "text/plain; charset=utf-8", "odd hex length");
      return;
    }
  } else {
    String value = web.hasArg("value") ? web.arg("value") : String("");
    if (value.length() > sizeof(payload)) value = value.substring(0, sizeof(payload));
    len = (uint8_t)value.length();
    for (uint8_t i = 0; i < len; ++i) payload[i] = (uint8_t)value[i];
  }

  if (!master_cmd_set_universal_entity(addr, entity_id, payload, len)) {
    web.send(503, "text/plain; charset=utf-8", "entity set failed");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_main_input_select() {
  const uint8_t source_type = web.hasArg("st") ? (uint8_t)strtoul(web.arg("st").c_str(), nullptr, 0) : MasterScheduler::INPUT_SRC_NONE;
  const uint8_t source_addr = web.hasArg("sa") ? (uint8_t)strtoul(web.arg("sa").c_str(), nullptr, 0) : 0;
  const uint8_t source_bit = web.hasArg("sb") ? (uint8_t)strtoul(web.arg("sb").c_str(), nullptr, 0) : 0;
  if (!master_cmd_set_main_input(source_type, source_addr, source_bit)) {
    web.send(400, "text/plain; charset=utf-8", "bad main input");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", "ok");
}
static void web_handle_output_select() {
  uint8_t addr = 0;
  if (web.hasArg("addr")) addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  if (addr != 0 && (addr < 0x10 || addr > 0x6F)) {
    web.send(400, "text/plain; charset=utf-8", "bad addr");
    return;
  }
  master_cmd_set_preferred_output(addr);
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_routing_set() {
  const bool enabled = web.hasArg("value") && web.arg("value") != "0";
  if (web.hasArg("jbc")) {
    master_cmd_set_jbc_input_enabled(enabled);
    master_prefs.begin(MasterSettingsStore::NS_CFG, false);
    master_prefs.putUChar(MasterSettingsStore::KEY_LEGACY_JBC_INPUT, enabled ? 1 : 0);
    master_prefs.end();
    web.send(200, "text/plain; charset=utf-8", "ok");
    return;
  }
  if (!web.hasArg("addr") || !web.hasArg("bit")) {
    web.send(400, "text/plain; charset=utf-8", "missing route");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  const uint8_t bit = (uint8_t)strtoul(web.arg("bit").c_str(), nullptr, 0);
  if (bit > 1 || !master_cmd_set_io_input_route(addr, bit, enabled)) {
    web.send(400, "text/plain; charset=utf-8", "bad route");
    return;
  }
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  master_prefs.putUChar(route_key(addr, bit).c_str(), enabled ? 1 : 0);
  master_prefs.end();
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_routing_rule() {
  if (!web.hasArg("idx")) {
    web.send(400, "text/plain; charset=utf-8", "missing rule index");
    return;
  }
  const uint8_t index = (uint8_t)strtoul(web.arg("idx").c_str(), nullptr, 0);
  if (index >= MasterScheduler::MAX_INPUT_RULES) {
    web.send(400, "text/plain; charset=utf-8", "bad rule index");
    return;
  }

  MasterScheduler::InputActionRule rule;
  rule.source_type = web.hasArg("st") ? (uint8_t)strtoul(web.arg("st").c_str(), nullptr, 0) : MasterScheduler::INPUT_SRC_NONE;
  rule.source_addr = web.hasArg("sa") ? (uint8_t)strtoul(web.arg("sa").c_str(), nullptr, 0) : 0;
  rule.source_bit = web.hasArg("sb") ? (uint8_t)strtoul(web.arg("sb").c_str(), nullptr, 0) : 0;
  rule.target_type = web.hasArg("tt") ? (uint8_t)strtoul(web.arg("tt").c_str(), nullptr, 0) : MasterScheduler::INPUT_TGT_NONE;
  rule.target_addr = web.hasArg("ta") ? (uint8_t)strtoul(web.arg("ta").c_str(), nullptr, 0) : 0;
  rule.target_bit = web.hasArg("tb") ? (uint8_t)strtoul(web.arg("tb").c_str(), nullptr, 0) : 0;
  rule.enabled = rule.source_type != MasterScheduler::INPUT_SRC_NONE && rule.target_type != MasterScheduler::INPUT_TGT_NONE;

  if (!master_cmd_set_input_rule(index, rule)) {
    web.send(400, "text/plain; charset=utf-8", "bad rule");
    return;
  }
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  save_input_action_rule(index, rule);
  master_prefs.end();
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_language_set() {
  String lang = web.hasArg("lang") ? web.arg("lang") : String("de");
  if (lang != "en") lang = "de";
  lang.toCharArray(web_lang, sizeof(web_lang));
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  master_prefs.putString(MasterSettingsStore::KEY_LANG, lang);
  master_prefs.end();
  mqtt_discovery_published = false;
  mqtt_next_discovery_check_ms = 0;
  mqtt_last_publish_ms = 0;
  web.send(200, "text/plain; charset=utf-8", "ok");
}


static void web_handle_module_reboot() {
  if (!web.hasArg("addr")) {
    web.send(400, "text/plain; charset=utf-8", "missing addr");
    return;
  }
  const uint8_t addr = (uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0);
  ModuleRecord* m = registry.find(addr);
  if (!m || !m->online) {
    web.send(404, "text/plain; charset=utf-8", "module offline");
    return;
  }
  if (!master_cmd_module_reboot(addr)) {
    web.send(500, "text/plain; charset=utf-8", "module did not acknowledge reboot");
    return;
  }
  web.send(200, "text/plain; charset=utf-8", "module rebooting");
}

static void prepare_controlled_restart() {
  master_cmd_persist_control_settings();
  mqtt_publish_all_availability_offline();
}

static void web_handle_restart() {
  prepare_controlled_restart();
  web.send(200, "text/plain; charset=utf-8", "restarting");
  delay(150);
  ESP.restart();
}
