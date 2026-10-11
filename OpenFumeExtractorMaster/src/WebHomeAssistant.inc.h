#pragma once

// HA reads the existing cache-only /state endpoint; no additional bus polling.
static void web_handle_ha_session() {
  web_csrf_init();
  String json;
  json.reserve(192);
  json += "{\"api_version\":1,\"master_uid\":\"";
  json += uid_hex(ESP.getEfuseMac());
  json += "\",\"firmware\":\"" MASTER_FW_VERSION "\",\"csrf\":\"";
  json += web_csrf_token;
  json += "\"}";
  web.send(200, "application/json", json);
}

static void web_handle_ha_control() {
  const String action = web.arg("action");
  const bool master_action = action == "extractor" || action == "restart" || action == "status_led" || action == "main_input" || action == "main_output";
  if (!master_action) {
    char* end = nullptr;
    const String raw_addr = web.arg("addr");
    const unsigned long addr = strtoul(raw_addr.c_str(), &end, 0);
    if (!raw_addr.length() || !end || *end || addr > 255UL) {
      web.send(400, "text/plain; charset=utf-8", "invalid module address");
      return;
    }
    const ModuleRecord* module = registry.find((uint8_t)addr);
    String requested_uid = web.arg("uid");
    requested_uid.toUpperCase();
    if (!module || !module->uid || !module->online || requested_uid != uid_hex(module->uid)) {
      web.send(409, "text/plain; charset=utf-8", "module identity changed or module offline");
      return;
    }
    if (scheduler.moduleFirmwareUpdateActive() && scheduler.moduleFirmwareUpdateTarget() == addr) {
      web.send(409, "text/plain; charset=utf-8", "module update in progress");
      return;
    }
  }
  if (action == "status_led") {
    bool enabled = status_led_enabled;
    uint8_t brightness = status_led_brightness_pct;
    const String setting = web.arg("setting");
    const String value = web.arg("value");
    if (setting == "enabled" && (value == "0" || value == "1")) enabled = value == "1";
    else if (setting == "brightness") {
      char* end = nullptr;
      const unsigned long parsed = strtoul(value.c_str(), &end, 10);
      if (!value.length() || !end || *end || parsed < 10 || parsed > 100) {
        web.send(400, "text/plain", "invalid brightness"); return;
      }
      brightness = (uint8_t)parsed;
    } else { web.send(400, "text/plain", "invalid LED setting"); return; }
    if (!save_status_led_config(enabled, brightness)) { web.send(507, "text/plain", "failed to persist LEDs"); return; }
    mqtt_last_publish_ms = 0;
    web.send(200, "text/plain", "ok");
  }
  else if (action == "main_input") {
    uint8_t type = 0, addr = 0, bit = 0;
    const String value = web.arg("value");
    if (!mqtt_find_main_input_by_label(value, type, addr, bit) || value != mqtt_main_input_label(type, addr, bit)) {
      web.send(409, "text/plain", "input selection changed"); return;
    }
    if (!master_cmd_set_main_input(type, addr, bit)) { web.send(503, "text/plain", "failed to select input"); return; }
    mqtt_last_publish_ms = 0;
    web.send(200, "text/plain", "ok");
  }
  else if (action == "main_output") {
    uint8_t addr = 0;
    const String value = web.arg("value");
    const bool found = mqtt_find_main_output_by_label(value, addr);
    const ModuleRecord* module = addr ? registry.find(addr) : nullptr;
    if (!found || value != mqtt_main_output_label(addr) || (addr && (!module || !mqtt_module_provides_extractor_output(*module)))) {
      web.send(409, "text/plain", "output selection changed"); return;
    }
    if (!master_cmd_set_preferred_output(addr)) { web.send(503, "text/plain", "failed to select output"); return; }
    mqtt_last_publish_ms = 0;
    web.send(200, "text/plain", "ok");
  }
  else if (action == "extractor") web_handle_jbc_settings();
  else if (action == "entity") web_handle_universal_entity();
  else if (action == "display") web_handle_display_set();
  else if (action == "io") web_handle_io_set();
  else if (action == "output") web_handle_module_output_set();
  else if (action == "weller") web_handle_weller_set();
  else if (action == "fanio_reset_filter") {
    const ModuleRecord* module = registry.find((uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0));
    const uint8_t mode = module ? mqtt_fanio_filter_mode(*module) : 0;
    if (!module || module->type != MODULE_FAN_IO_PRO || (mode != 1 && mode != 3)) {
      web.send(409, "text/plain", "filter runtime tracking is disabled"); return;
    }
    if (!master_cmd_calibrate_fanio_filter(module->addr, 7, 0, 0)) {
      web.send(503, "text/plain", "filter runtime reset failed"); return;
    }
    mqtt_last_publish_ms = 0;
    web.send(200, "text/plain", "ok");
  }
  else if (action == "jbc_usb") {
    const ModuleRecord* module = registry.find((uint8_t)strtoul(web.arg("addr").c_str(), nullptr, 0));
    const String id_text = web.arg("control"), value_text = web.arg("value");
    char* end = nullptr;
    const unsigned long id = strtoul(id_text.c_str(), &end, 10);
    if (!id_text.length() || !end || *end || id >= HA_JBC_CONTROL_COUNT) {
      web.send(400, "text/plain", "invalid JBC control"); return;
    }
    const float value = strtof(value_text.c_str(), &end);
    uint8_t payload[23], len = 0;
    if (!value_text.length() || !end || *end || !module || !ha_jbc_control_payload(*module, id, value, payload, len)) {
      web.send(409, "text/plain", "JBC parameter unavailable or out of range"); return;
    }
    if (!master_cmd_set_jbc_usb_config(module->addr, payload, len)) {
      web.send(409, "text/plain", "JBC station or module busy"); return;
    }
    web.send(202, "text/plain", "JBC parameter queued for write and verification");
  }
  else if (action == "module_reboot") web_handle_module_reboot();
  else if (action == "restart") web_handle_restart();
  else web.send(400, "text/plain; charset=utf-8", "unsupported HA action");
}
