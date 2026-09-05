#pragma once

// Network setup, backup/restore and captive-portal configuration handlers.
// Included from the master sketch so it can use the existing WebServer,
// Preferences, scheduler and WiFi state objects.
static void web_redirect_config() {
  web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/config", true);
  web.send(302, "text/plain; charset=utf-8", "");
}


static void json_add_string_field(String& json, const char* key, const String& value) {
  json += "\""; json += key; json += "\":\""; json += json_escape(value.c_str()); json += "\"";
}
static void json_add_bool_field(String& json, const char* key, bool value) {
  json += "\""; json += key; json += "\":"; json += value ? "true" : "false";
}
static void json_add_u32_field(String& json, const char* key, uint32_t value) {
  json += "\""; json += key; json += "\":"; json += value;
}
static String string_to_hex(const String& value) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 2);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t b = (uint8_t)value[i];
    out += hex[b >> 4];
    out += hex[b & 0x0F];
  }
  return out;
}
static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static bool hex_to_string(const String& hex, String& out) {
  String clean;
  clean.reserve(hex.length());
  for (size_t i = 0; i < hex.length(); ++i) {
    const char c = hex[i];
    if (!isspace((unsigned char)c)) clean += c;
  }
  if (clean.length() % 2) return false;
  out = "";
  out.reserve(clean.length() / 2);
  for (size_t i = 0; i < clean.length(); i += 2) {
    const int hi = hex_nibble(clean[i]);
    const int lo = hex_nibble(clean[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out += (char)((hi << 4) | lo);
  }
  return true;
}
static int json_value_pos(const String& src, const char* key) {
  String pat = "\"" + String(key) + "\"";
  int p = src.indexOf(pat);
  if (p < 0) return -1;
  p = src.indexOf(':', p + pat.length());
  if (p < 0) return -1;
  ++p;
  while (p < (int)src.length() && isspace((unsigned char)src[p])) ++p;
  return p;
}
static String json_section(const String& doc, const char* key) {
  int p = json_value_pos(doc, key);
  if (p < 0 || p >= (int)doc.length() || doc[p] != '{') return String();
  int depth = 0;
  bool in_string = false, esc = false;
  for (int i = p; i < (int)doc.length(); ++i) {
    const char c = doc[i];
    if (in_string) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') in_string = true;
    else if (c == '{') ++depth;
    else if (c == '}' && --depth == 0) return doc.substring(p, i + 1);
  }
  return String();
}
static String json_get_string_field(const String& src, const char* key, const String& fallback) {
  int p = json_value_pos(src, key);
  if (p < 0 || p >= (int)src.length() || src[p] != '"') return fallback;
  ++p;
  String out;
  bool esc = false;
  for (; p < (int)src.length(); ++p) {
    const char c = src[p];
    if (esc) {
      if (c == 'n') out += '\n';
      else if (c == 'r') out += '\r';
      else if (c == 't') out += '\t';
      else out += c;
      esc = false;
    } else if (c == '\\') esc = true;
    else if (c == '"') return out;
    else out += c;
  }
  return fallback;
}
static bool json_get_bool_field(const String& src, const char* key, bool fallback) {
  int p = json_value_pos(src, key);
  if (p < 0) return fallback;
  if (src.substring(p, p + 4) == "true") return true;
  if (src.substring(p, p + 5) == "false") return false;
  return fallback;
}
static uint32_t json_get_u32_field(const String& src, const char* key, uint32_t fallback) {
  int p = json_value_pos(src, key);
  if (p < 0 || p >= (int)src.length() || (!((src[p] >= '0' && src[p] <= '9') || src[p] == '-'))) return fallback;
  return (uint32_t)strtoul(src.c_str() + p, nullptr, 10);
}
static uint32_t json_get_u32_or_string_field(const String& src, const char* key, uint32_t fallback) {
  int p = json_value_pos(src, key);
  if (p < 0 || p >= (int)src.length()) return fallback;
  if (src[p] == '"') {
    String value = json_get_string_field(src, key, String(""));
    value.trim();
    if (!value.length()) return fallback;
    return (uint32_t)strtoul(value.c_str(), nullptr, 0);
  }
  if (!((src[p] >= '0' && src[p] <= '9') || src[p] == '-')) return fallback;
  return (uint32_t)strtoul(src.c_str() + p, nullptr, 0);
}
static String addr_hex(uint8_t addr) {
  char buf[5];
  snprintf(buf, sizeof(buf), "0x%02X", addr);
  return String(buf);
}
static String build_config_backup_json() {
  const JbcModuleState& cs = scheduler.controlSettings();
  String json;
  json.reserve(14000 + mqtt_ca_cert.length() * 3);
  json += "{\n  \"schema\":1,\n  \"product\":\"Open Fume Extractor\",\n  \"fw\":\"" MASTER_FW_VERSION "\",\n";
  json += "  \"network\":{\n";
  char display_root[33];
  if (master_display_wifi.rootText(display_root)) {
    json += "    "; json_add_string_field(json, "display_pairing_root", String(display_root)); json += ",\n";
  }
  json += "    "; json_add_string_field(json, "ssid", String(wifi_ssid)); json += ",\n";
  json += "    "; json_add_string_field(json, "pass", String(wifi_password)); json += ",\n";
  json += "    "; json_add_string_field(json, "hostname", String(master_hostname)); json += ",\n";
  json += "    "; json_add_string_field(json, "web_user", String(web_auth_user)); json += ",\n";
  json += "    "; json_add_string_field(json, "web_pass", String(web_auth_password)); json += ",\n";
  json += "    "; json_add_bool_field(json, "static", wifi_static_enabled); json += ",\n";
  json += "    "; json_add_string_field(json, "ip", wifi_static_ip.toString()); json += ",\n";
  json += "    "; json_add_string_field(json, "gateway", wifi_static_gateway.toString()); json += ",\n";
  json += "    "; json_add_string_field(json, "subnet", wifi_static_subnet.toString()); json += ",\n";
  json += "    "; json_add_string_field(json, "dns1", wifi_static_dns1.toString()); json += ",\n";
  json += "    "; json_add_string_field(json, "dns2", wifi_static_dns2.toString()); json += "\n  },\n";
  json += "  \"mqtt\":{\n";
  json += "    "; json_add_bool_field(json, "enabled", mqtt_enabled); json += ",\n";
  json += "    "; json_add_bool_field(json, "tls", mqtt_tls_enabled); json += ",\n";
  json += "    "; json_add_bool_field(json, "ha", mqtt_ha_discovery); json += ",\n";
  json += "    "; json_add_string_field(json, "host", String(mqtt_host)); json += ",\n";
  json += "    "; json_add_u32_field(json, "port", mqtt_port); json += ",\n";
  json += "    "; json_add_string_field(json, "user", String(mqtt_user)); json += ",\n";
  json += "    "; json_add_string_field(json, "pass", String(mqtt_password)); json += ",\n";
  json += "    "; json_add_string_field(json, "base_topic", String(mqtt_base_topic)); json += ",\n";
  json += "    "; json_add_string_field(json, "discovery_prefix", String(mqtt_discovery_prefix)); json += ",\n";
  json += "    "; json_add_string_field(json, "ca", mqtt_ca_cert); json += ",\n";
  json += "    "; json_add_string_field(json, "ca_hex", string_to_hex(mqtt_ca_cert)); json += "\n  },\n";
  json += "  \"master\":{\n";
  json += "    "; json_add_string_field(json, "language", String(web_lang)); json += ",\n";
  json += "    "; json_add_bool_field(json, "status_led_enabled", status_led_enabled); json += ",\n";
  json += "    "; json_add_u32_field(json, "status_led_brightness", status_led_brightness_pct); json += ",\n";
  json += "    "; json_add_u32_field(json, "suction", cs.suction_level); json += ",\n";
  json += "    "; json_add_u32_field(json, "select_flow", cs.select_flow); json += ",\n";
  json += "    "; json_add_u32_field(json, "delay_work", cs.delay_work_sec); json += ",\n";
  json += "    "; json_add_u32_field(json, "delay_stand", cs.delay_stand_sec); json += ",\n";
  json += "    "; json_add_bool_field(json, "stand_intakes", cs.stand_intakes != 0); json += ",\n";
  json += "    "; json_add_bool_field(json, "afterrun_power_enabled", scheduler.afterrunPowerProfileEnabled()); json += ",\n";
  json += "    "; json_add_u32_field(json, "afterrun_power", scheduler.afterrunPower()); json += ",\n";
  json += "    "; json_add_u32_field(json, "main_input_type", scheduler.mainInputSourceType()); json += ",\n";
  json += "    "; json_add_string_field(json, "main_input_addr", addr_hex(scheduler.mainInputSourceAddr())); json += ",\n";
  json += "    "; json_add_u32_field(json, "main_input_bit", scheduler.mainInputSourceBit()); json += ",\n";
  json += "    "; json_add_string_field(json, "output_addr", addr_hex(scheduler.preferredOutputAddr())); json += ",\n";
  json += "    "; json_add_string_field(json, "logic_json", load_logic_json()); json += ",\n";
  json += "    "; json_add_u32_field(json, "logic_active", active_logic_slot()); json += ",\n";
  json += "    \"logic_slots\":{\n";
  bool first_logic_slot = true;
  for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) {
    if (!logic_slot_exists(i)) continue;
    if (!first_logic_slot) json += ",\n";
    first_logic_slot = false;
    json += "      ";
    String key = String("slot") + String(i);
    json_add_string_field(json, key.c_str(), load_logic_slot_json(i));
  }
  json += "\n    }\n  }\n}\n";
  return json;
}
static bool apply_config_backup_json(const String& backup, String& error) {
  String net = json_section(backup, "network"), mqtt = json_section(backup, "mqtt"), master = json_section(backup, "master");
  if (!net.length() && !mqtt.length() && !master.length()) { error = "backup JSON has no network, mqtt or master section"; return false; }
  String ssid = json_get_string_field(net, "ssid", String(wifi_ssid)); ssid.trim();
  String pass = json_get_string_field(net, "pass", String(wifi_password));
  String hostname = normalized_hostname(json_get_string_field(net, "hostname", String(master_hostname)));
  if (!hostname.length()) hostname = String(master_hostname);
  String web_user_arg = json_get_string_field(net, "web_user", String(web_auth_user)); web_user_arg.trim();
  if (!web_user_arg.length()) web_user_arg = WEB_AUTH_USER;
  String web_pass_arg = json_get_string_field(net, "web_pass", String(web_auth_password));
  if (web_pass_arg.length() < 8) { error = "web password must be at least 8 characters"; return false; }
  const bool use_static = json_get_bool_field(net, "static", wifi_static_enabled);
  IPAddress ip = wifi_static_ip, gateway = wifi_static_gateway, subnet = wifi_static_subnet, dns1 = wifi_static_dns1, dns2 = wifi_static_dns2;
  if (!parse_ipv4(json_get_string_field(net, "ip", ip.toString()), ip)) ip = IPAddress(0, 0, 0, 0);
  if (!parse_ipv4(json_get_string_field(net, "gateway", gateway.toString()), gateway)) gateway = IPAddress(0, 0, 0, 0);
  if (!parse_ipv4(json_get_string_field(net, "subnet", subnet.toString()), subnet)) subnet = IPAddress(255, 255, 255, 0);
  if (!parse_ipv4(json_get_string_field(net, "dns1", dns1.toString()), dns1)) dns1 = gateway;
  if (!parse_ipv4(json_get_string_field(net, "dns2", dns2.toString()), dns2)) dns2 = IPAddress(0, 0, 0, 0);
  if (use_static && (ip == IPAddress(0, 0, 0, 0) || gateway == IPAddress(0, 0, 0, 0))) { error = "static IP backup is incomplete"; return false; }
  String mqtt_host_arg = json_get_string_field(mqtt, "host", String(mqtt_host)); mqtt_host_arg.trim();
  String mqtt_user_arg = json_get_string_field(mqtt, "user", String(mqtt_user)); mqtt_user_arg.trim();
  String mqtt_pass_arg = json_get_string_field(mqtt, "pass", String(mqtt_password));
  String mqtt_topic_arg = json_get_string_field(mqtt, "base_topic", String(mqtt_base_topic)); mqtt_topic_arg.trim(); if (!mqtt_topic_arg.length()) mqtt_topic_arg = "open-fume-extractor";
  String mqtt_disc_arg = json_get_string_field(mqtt, "discovery_prefix", String(mqtt_discovery_prefix)); mqtt_disc_arg.trim(); if (!mqtt_disc_arg.length()) mqtt_disc_arg = "homeassistant";
  String mqtt_ca_arg = json_get_string_field(mqtt, "ca", mqtt_ca_cert);
  String mqtt_ca_hex_arg = json_get_string_field(mqtt, "ca_hex", String(""));
  if (mqtt_ca_hex_arg.length()) {
    String decoded_ca;
    if (!hex_to_string(mqtt_ca_hex_arg, decoded_ca)) { error = "invalid MQTT CA certificate hex"; return false; }
    mqtt_ca_arg = decoded_ca;
  }
  mqtt_ca_arg.replace("\r\n", "\n"); mqtt_ca_arg.trim();
  const bool mqtt_tls_arg = json_get_bool_field(mqtt, "tls", mqtt_tls_enabled);
  if (mqtt_tls_arg && mqtt_ca_arg.length() && (mqtt_ca_arg.indexOf("-----BEGIN CERTIFICATE-----") < 0 || mqtt_ca_arg.indexOf("-----END CERTIFICATE-----") < 0)) { error = "invalid MQTT CA certificate PEM"; return false; }
  uint32_t mqtt_port_arg32 = json_get_u32_field(mqtt, "port", mqtt_tls_arg ? 8883 : 1883);
  if (!mqtt_port_arg32 || mqtt_port_arg32 > 65535UL) mqtt_port_arg32 = mqtt_tls_arg ? 8883 : 1883;
  const String display_root = json_get_string_field(net, "display_pairing_root", String(""));
  if (display_root.length() && !master_display_wifi.restoreRoot(display_root.c_str())) {
    error = "invalid display pairing key or failed to persist it"; return false;
  }
  if (!netcfg_save(ssid, pass, hostname, web_user_arg, web_pass_arg, use_static, ip, gateway, subnet, dns1, dns2,
              json_get_bool_field(mqtt, "enabled", mqtt_enabled), mqtt_tls_arg, mqtt_host_arg, (uint16_t)mqtt_port_arg32,
              mqtt_user_arg, mqtt_pass_arg, mqtt_topic_arg, json_get_bool_field(mqtt, "ha", mqtt_ha_discovery), mqtt_disc_arg, mqtt_ca_arg,
              json_get_bool_field(master, "status_led_enabled", status_led_enabled),
              (uint8_t)json_get_u32_field(master, "status_led_brightness", status_led_brightness_pct))) {
    error = "failed to persist network/MQTT settings";
    return false;
  }
  if (master.length()) {
    String lang = json_get_string_field(master, "language", String(web_lang)); lang = (lang == "en") ? "en" : "de"; lang.toCharArray(web_lang, sizeof(web_lang));
    apply_control_settings((uint8_t)json_get_u32_field(master, "suction", scheduler.controlSettings().suction_level),
      (uint16_t)json_get_u32_field(master, "select_flow", scheduler.controlSettings().select_flow),
      (uint16_t)json_get_u32_field(master, "delay_work", scheduler.controlSettings().delay_work_sec),
      (uint16_t)json_get_u32_field(master, "delay_stand", scheduler.controlSettings().delay_stand_sec),
      json_get_bool_field(master, "stand_intakes", scheduler.controlSettings().stand_intakes != 0), false, true);
    const bool backup_afterrun_power_enabled = json_get_bool_field(master, "afterrun_power_enabled", scheduler.afterrunPowerProfileEnabled());
    const uint16_t backup_afterrun_power = (uint16_t)json_get_u32_field(master, "afterrun_power", scheduler.afterrunPower());
    master_cmd_set_afterrun_power_profile(backup_afterrun_power_enabled, backup_afterrun_power, false);
    MasterSettingsStore::ControlSettings restored_control;
    restored_control.suction = scheduler.controlSettings().suction_level;
    restored_control.select_flow = scheduler.controlSettings().select_flow;
    restored_control.delay_work_sec = scheduler.controlSettings().delay_work_sec;
    restored_control.delay_stand_sec = scheduler.controlSettings().delay_stand_sec;
    restored_control.stand_intakes = scheduler.controlSettings().stand_intakes != 0;
    restored_control.afterrun_power_enabled = scheduler.afterrunPowerProfileEnabled();
    restored_control.afterrun_power = scheduler.afterrunPower();
    if (!MasterSettingsStore::saveControl(master_prefs, restored_control)) { error = "failed to write master control settings"; return false; }
    if (!MasterSettingsStore::saveMainInput(master_prefs,
      (uint8_t)json_get_u32_field(master, "main_input_type", scheduler.mainInputSourceType()),
      (uint8_t)json_get_u32_or_string_field(master, "main_input_addr", scheduler.mainInputSourceAddr()),
      (uint8_t)json_get_u32_field(master, "main_input_bit", scheduler.mainInputSourceBit()))) { error = "failed to write main input settings"; return false; }
    if (!MasterSettingsStore::savePreferredOutput(master_prefs,
      (uint8_t)json_get_u32_or_string_field(master, "output_addr", scheduler.preferredOutputAddr()))) { error = "failed to write main output settings"; return false; }

    master_prefs.begin(MasterSettingsStore::NS_CFG, false);
    master_prefs.putString(MasterSettingsStore::KEY_LANG, lang);
    String backup_logic_json = json_get_string_field(master, "logic_json", String(""));
    String backup_logic_slots = json_section(master, "logic_slots");
    if (backup_logic_slots.length()) {
      for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) remove_logic_slot_storage_open(i);
      for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) {
        String key = String("slot") + String(i);
        String slot_json = json_get_string_field(backup_logic_slots, key.c_str(), String(""));
        if (slot_json.length() && slot_json.indexOf("\"nodes\"") >= 0 && slot_json.indexOf("\"links\"") >= 0) {
          if (!logic_save_fs(i, slot_json)) logic_save_legacy_nvs_open(i, slot_json);
        }
      }
      uint8_t restored_active = (uint8_t)json_get_u32_field(master, "logic_active", 0);
      master_prefs.putUChar(MasterSettingsStore::KEY_LOGIC_ACTIVE, restored_active < LOGIC_DEF_MAX ? restored_active : 0);
    } else if (backup_logic_json.length() && backup_logic_json.indexOf("\"nodes\"") >= 0 && backup_logic_json.indexOf("\"links\"") >= 0) {
      remove_logic_slot_storage_open(0);
      if (!logic_save_fs(0, backup_logic_json)) logic_save_legacy_nvs_open(0, backup_logic_json);
      master_prefs.putUChar(MasterSettingsStore::KEY_LOGIC_ACTIVE, 0);
    }
    master_prefs.end();
    logic_cache_reload_all();
  }
  return true;
}
static void web_handle_config_export() {
  heap_diag_set_context(HEAP_DIAG_CTX_WEB, "backup_export");
  heap_diag_sample("backup_export_begin");
  String json = build_config_backup_json();
  heap_diag_sample("backup_export_built");
  String filename = "open-fume-extractor-" + String(master_hostname) + "-backup.json";
  web.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json; charset=utf-8", json);
  heap_diag_sample("backup_export_sent");
  heap_diag_clear_context(HEAP_DIAG_CTX_WEB);
}
static void web_handle_config_import() {
  heap_diag_set_context(HEAP_DIAG_CTX_WEB, "backup_import");
  heap_diag_sample("backup_import_begin");
  String backup = web.hasArg("plain") ? web.arg("plain") : web.arg("backup_json"); backup.trim();
  if (!backup.length()) { heap_diag_clear_context(HEAP_DIAG_CTX_WEB); web.send(400, "text/plain; charset=utf-8", "missing backup JSON"); return; }
  if (backup.length() > 65536) { heap_diag_clear_context(HEAP_DIAG_CTX_WEB); web.send(413, "text/plain; charset=utf-8", "backup JSON too large"); return; }
  String error;
  if (!apply_config_backup_json(backup, error)) { heap_diag_clear_context(HEAP_DIAG_CTX_WEB); web.send(400, "text/plain; charset=utf-8", error.length() ? error : String("backup import failed")); return; }
  heap_diag_sample("backup_import_applied");
  heap_diag_clear_context(HEAP_DIAG_CTX_WEB);
  String html; html.reserve(3000);
  web_shell_begin(html, web_text("Backup importiert", "Backup imported"), web_text("Netzwerk", "Network"), "config");
  html += F("<section class='panel'><h2>"); html += web_text("Backup importiert", "Backup imported"); html += F("</h2><p>");
  html += web_text("Die Einstellungen wurden wiederhergestellt. Der Master startet neu und übernimmt WLAN, MQTT und Master-Einstellungen.", "Settings were restored. The master restarts and applies WiFi, MQTT and master settings.");
  html += F("</p><div class='progress'><div class='bar' style='width:100%'></div></div><p class='muted'>"); html += web_text("Weiterleitung in wenigen Sekunden...", "Redirecting in a few seconds..."); html += F("</p></section><script>setTimeout(function(){location.href='/';},12000);setTimeout(function(){location.reload();},18000);</script>");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
  prepare_controlled_restart();
  delay(900);
  ESP.restart();
}

static void web_handle_initial_network_setup() {
  int n = WiFi.scanNetworks(false, true);
  String html;
  html.reserve(5200);
  web_shell_begin(html, web_text("Netzwerk einrichten", "Set up network"), web_text("Ersteinrichtung", "Initial setup"), "config");
  web_csrf_init();
  html += F("<p class='muted'>");
  html += web_text("Der Master ist im Setup-Modus. Wähle jetzt das WLAN aus, mit dem er sich verbinden soll.", "The master is in setup mode. Choose the WiFi network it should connect to.");
  html += F("</p><form class='panel' method='post' action='/config/save'><input type='hidden' name='csrf' value='");
  html += web_csrf_token;
  html += F("'><h2>");
  html += web_text("WLAN-Verbindung", "WiFi connection");
  html += F("</h2><label>");
  html += web_text("Gefundene Netzwerke", "Detected networks");
  html += F("</label><select name='ssid'>");
  bool current_listed = false;
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    html += F("<option value='");
    html += html_escape(ssid);
    html += "'";
    if (ssid == String(wifi_ssid)) {
      html += F(" selected");
      current_listed = true;
    }
    html += ">";
    html += html_escape(ssid);
    html += F(" (");
    html += WiFi.RSSI(i);
    html += F(" dBm)</option>");
  }
  if (strlen(wifi_ssid) && !current_listed) {
    html += F("<option selected value='");
    html += html_escape(String(wifi_ssid));
    html += F("'>");
    html += html_escape(String(wifi_ssid));
    html += F("</option>");
  }
  if (n <= 0 && !strlen(wifi_ssid)) {
    html += F("<option value=''>"); html += web_text("Keine Netzwerke gefunden - SSID manuell eingeben", "No networks found - enter SSID manually"); html += F("</option>");
  }
  html += F("</select><div class='actions' style='margin-top:10px'><a class='btn secondary' href='/config'>");
  html += web_text("Netzwerke neu scannen", "Scan networks");
  html += F("</a></div><label>");
  html += web_text("SSID manuell eingeben", "Enter SSID manually");
  html += F("</label><input name='ssid_manual' maxlength='32' placeholder='SSID'><label>");
  html += web_text("WLAN-Passwort", "WiFi password");
  html += F("</label><input name='pass' maxlength='64' type='password' autocomplete='new-password' placeholder='");
  html += web_text("Leer lassen, um das gespeicherte Passwort zu behalten", "Leave empty to keep the saved password");
  html += F("'>");
  if (wifi_password[0]) { html += F("<div class='muted' style='font-size:12px;margin-top:5px'>"); html += web_text("WLAN-Passwort gespeichert", "WiFi password saved"); html += F("</div>"); }
  html += F("<label>Hostname</label><input name='hostname' maxlength='31' value='");
  html += html_escape(String(master_hostname));
  html += F("' placeholder='open-fume-extractor'><label>");
  html += web_text("IP-Konfiguration", "IP configuration");
  html += F("</label><select id='initial_ip_mode' name='ip_mode' onchange='toggleInitialStaticIp()'><option value='dhcp'");
  if (!wifi_static_enabled) html += F(" selected");
  html += F(">DHCP</option><option value='static'");
  if (wifi_static_enabled) html += F(" selected");
  html += F(">");
  html += web_text("Statische IP", "Static IP");
  html += F("</option></select><div id='initial_static_ip_fields' class='grid' style='margin-top:12px'><div><label>IP</label><input name='ip' inputmode='decimal' value='");
  html += wifi_static_ip.toString();
  html += F("' placeholder='192.168.1.50'></div><div><label>Gateway</label><input name='gateway' inputmode='decimal' value='");
  html += wifi_static_gateway.toString();
  html += F("' placeholder='192.168.1.1'></div><div><label>");
  html += web_text("Netzmaske", "Subnet mask");
  html += F("</label><input name='subnet' inputmode='decimal' value='");
  html += wifi_static_subnet.toString();
  html += F("' placeholder='255.255.255.0'></div><div><label>DNS 1</label><input name='dns1' inputmode='decimal' value='");
  html += wifi_static_dns1.toString();
  html += F("' placeholder='192.168.1.1'></div><div><label>DNS 2</label><input name='dns2' inputmode='decimal' value='");
  html += wifi_static_dns2.toString();
  html += F("' placeholder='8.8.8.8'></div></div><div class='actions' style='margin-top:16px'><button type='submit'>");
  html += web_text("WLAN speichern und verbinden", "Save WiFi and connect");
  html += F("</button></div></form><script>function toggleInitialStaticIp(){var m=document.getElementById('initial_ip_mode'),f=document.getElementById('initial_static_ip_fields');if(m&&f)f.style.display=m.value==='static'?'grid':'none';}toggleInitialStaticIp();</script>");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
}

static void web_handle_initial_network_save() {
  if (web_password_change_required) {
    web.sendHeader("Location", "/config", true);
    web.send(303, "text/plain; charset=utf-8", "");
    return;
  }

  String ssid = web.arg("ssid_manual");
  ssid.trim();
  if (!ssid.length()) ssid = web.arg("ssid");
  ssid.trim();
  String pass = web.arg("pass");
  if (!pass.length() || pass == "********") pass = String(wifi_password);

  String hostname = normalized_hostname(web.arg("hostname"));
  if (!hostname.length() || hostname == "open-fume-extractor") {
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "open-fume-extractor-%08lx", (uint32_t)ESP.getEfuseMac());
    hostname = fallback;
  }

  const bool use_static = web.arg("ip_mode") == "static";
  IPAddress ip(0, 0, 0, 0), gateway(0, 0, 0, 0), subnet(255, 255, 255, 0);
  IPAddress dns1(0, 0, 0, 0), dns2(0, 0, 0, 0);
  String error;
  if (!ssid.length()) {
    error = web_text("Bitte eine SSID auswählen oder manuell eingeben.", "Select a network or enter an SSID manually.");
  } else if (use_static) {
    if (!parse_ipv4(web.arg("ip"), ip) || !parse_ipv4(web.arg("gateway"), gateway) ||
        !parse_ipv4(web.arg("subnet"), subnet)) {
      error = web_text("IP-Adresse, Gateway oder Netzmaske ist ungültig.", "The IP address, gateway or subnet mask is invalid.");
    }
    if (!error.length() && !parse_ipv4(web.arg("dns1"), dns1)) dns1 = gateway;
    if (!error.length()) parse_ipv4(web.arg("dns2"), dns2);
  }
  if (error.length()) {
    String html;
    html.reserve(1800);
    web_shell_begin(html, web_text("Netzwerk einrichten", "Set up network"), web_text("Ersteinrichtung", "Initial setup"), "config");
    html += F("<section class='panel'><h2>");
    html += web_text("Eingabe prüfen", "Check your input");
    html += F("</h2><p class='msg error'>");
    html += html_escape(error);
    html += F("</p><div class='actions'><a class='btn' href='/config'>");
    html += web_text("Zurück zur WLAN-Einrichtung", "Back to WiFi setup");
    html += F("</a></div></section>");
    web_shell_end(html);
    web.send(400, "text/html; charset=utf-8", html);
    return;
  }

  if (!netcfg_save_wifi(ssid, pass, hostname, use_static, ip, gateway, subnet, dns1, dns2)) {
    String html;
    html.reserve(1900);
    web_shell_begin(html, web_text("Netzwerk einrichten", "Set up network"), web_text("Ersteinrichtung", "Initial setup"), "config");
    html += F("<section class='panel'><h2>");
    html += web_text("Speichern fehlgeschlagen", "Could not save settings");
    html += F("</h2><p class='msg error'>");
    html += web_text("Die WLAN-Einstellungen konnten nicht im Speicher abgelegt werden. Bitte erneut versuchen.", "The WiFi settings could not be stored. Please try again.");
    html += F("</p><div class='actions'><a class='btn' href='/config'>");
    html += web_text("Erneut versuchen", "Try again");
    html += F("</a></div></section>");
    web_shell_end(html);
    web.send(507, "text/html; charset=utf-8", html);
    return;
  }

  String html;
  html.reserve(2600);
  web_shell_begin(html, web_text("WLAN gespeichert", "WiFi saved"), web_text("Ersteinrichtung", "Initial setup"), "config");
  html += F("<section class='panel'><h2>");
  html += web_text("WLAN-Einstellungen übernommen", "WiFi settings saved");
  html += F("</h2><p>");
  html += web_text("Der Master startet neu und verbindet sich jetzt mit dem ausgewählten WLAN. Danach ist die normale Netzwerkseite verfügbar.", "The master is restarting and connecting to the selected WiFi network. The normal network page will be available afterwards.");
  html += F("</p><div class='progress'><div class='bar' style='width:100%'></div></div><p class='muted'>");
  html += web_text("Weiterleitung nach dem Neustart...", "Redirecting after restart...");
  html += F("</p></section><script>setTimeout(function(){location.href='http://");
  html += html_escape(hostname);
  html += F(".local/';},9000);</script>");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
  delay(100);
  ESP.restart();
}

static void web_handle_config() {
  if (captive_active && !web_password_change_required) {
    web_handle_initial_network_setup();
    return;
  }
  int n = WiFi.scanNetworks(false, true);
  const String password_error = web.arg("password_error");
  const bool password_error_known = password_error == "mismatch" || password_error == "length" || password_error == "default" || password_error == "storage";
  String html;
  html.reserve(15000 + mqtt_ca_cert.length() * 2);
  web_shell_begin(html, web_text("Netzwerk Setup", "Network Setup"), web_text("Netzwerk", "Network"), "config");
  web_csrf_init();
  html += F("<p class='muted'>");
  html += web_text("Master mit deinem lokalen WLAN verbinden oder die Zugangsdaten ändern.", "Connect the master to your local WiFi or change its credentials.");
  html += F("</p>");
  if (web_password_change_required) {
    web_csrf_init();
    html += F("<style>.password-modal-backdrop{position:fixed;inset:0;z-index:20;background:rgba(7,9,12,.82);display:flex;align-items:center;justify-content:center;padding:18px}.password-modal{width:min(460px,100%);background:var(--card);border:1px solid #4b5968;border-radius:10px;box-shadow:0 18px 60px rgba(0,0,0,.45);padding:24px}.password-modal h2{margin:0 0 8px}.password-modal p{margin:0 0 16px}.password-modal .actions{margin-top:18px;justify-content:flex-end}</style>");
    html += F("<div class='password-modal-backdrop'><div class='password-modal' role='dialog' aria-modal='true' aria-labelledby='password_modal_title'><h2 id='password_modal_title'>");
    html += web_text("Webpasswort festlegen", "Set web password");
    html += F("</h2><p class='muted'>");
    html += web_text("Das Standardpasswort ist nur für die Ersteinrichtung gültig. Bitte lege jetzt ein eigenes Passwort fest.", "The default password is only valid for initial setup. Set your own password now.");
    html += F("</p><div id='password_feedback' class='msg' role='alert' aria-live='polite' style='margin-bottom:14px;display:");
    html += password_error_known ? F("block") : F("none");
    html += F("'>");
    if (password_error == "mismatch") html += web_text("Die Passwörter stimmen nicht überein.", "The passwords do not match.");
    else if (password_error == "length") html += web_text("Das Passwort muss 8 bis 64 Zeichen enthalten.", "The password must contain 8 to 64 characters.");
    else if (password_error == "default") html += web_text("Bitte ein anderes Passwort als das Standardpasswort wählen.", "Choose a password different from the default password.");
    else if (password_error == "storage") html += web_text("Das Passwort konnte nicht gespeichert werden. Bitte erneut versuchen.", "The password could not be saved. Please try again.");
    html += F("</div><form method='post' action='/config/password' onsubmit='return validateWebPasswordForm(this)'><input type='hidden' name='csrf' value='");
    html += web_csrf_token;
    html += F("'><label for='web_pass_new'>");
    html += web_text("Neues Passwort", "New password");
    html += F("</label><input id='web_pass_new' name='web_pass' type='password' minlength='8' maxlength='64' autocomplete='new-password' required oninput='clearWebPasswordError()'><label for='web_pass_confirm'>");
    html += web_text("Passwort bestätigen", "Confirm password");
    html += F("</label><input id='web_pass_confirm' name='web_pass_confirm' type='password' minlength='8' maxlength='64' autocomplete='new-password' required oninput='clearWebPasswordError()'><div class='actions'><button type='submit'>");
    html += web_text("Passwort speichern", "Save password");
    html += F("</button></div></form></div></div><script>function clearWebPasswordError(){var f=document.getElementById('password_feedback'),c=document.getElementById('web_pass_confirm');if(f){f.style.display='none';f.textContent=''}if(c)c.setCustomValidity('');}function validateWebPasswordForm(form){var p=document.getElementById('web_pass_new'),c=document.getElementById('web_pass_confirm'),f=document.getElementById('password_feedback');if(!p||!c)return true;if(p.value!==c.value){var m=");
    html += web_is_german() ? F("'Die Passwörter stimmen nicht überein.'") : F("'The passwords do not match.'");
    html += F(";if(f){f.textContent=m;f.style.display='block'}c.setCustomValidity(m);c.focus();return false}c.setCustomValidity('');return true}document.getElementById('web_pass_new').focus();</script>");
  }
  html += F("<section class='panel connection-status'><div style='display:flex;align-items:baseline;justify-content:space-between;gap:12px;flex-wrap:wrap'><h2 style='margin:0'>");
  html += web_text("Verbindungsstatus", "Connection status");
  html += F("</h2><span class='muted' style='font-size:12px'>");
  html += web_text("Aktuelle Netzwerkverbindung", "Current network connection");
  html += F("</span></div><div class='stat-grid' style='margin-top:14px;grid-template-columns:repeat(auto-fit,minmax(150px,1fr))'><div class='stat'><div class='k'>");
  html += web_text("Modus", "Mode");
  html += F("</div><div class='v'>");
  if (WiFi.status() == WL_CONNECTED) html += wifi_static_enabled ? F("Station / Static IP") : F("Station / DHCP"); else html += F("Access Point");
  html += F("</div></div><div class='stat'><div class='k'>IP</div><div class='v'>");
  html += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  html += F("</div></div><div class='stat'><div class='k'>SSID</div><div class='v'>");
  html += (WiFi.status() == WL_CONNECTED) ? html_escape(WiFi.SSID()) : String(master_ap_ssid);
  html += F("</div></div><div class='stat'><div class='k'>");
  html += web_text("Signal", "Signal");
  html += F("</div><div class='v'>");
  if (WiFi.status() == WL_CONNECTED) { html += WiFi.RSSI(); html += F(" dBm"); } else html += F("-");
  html += F("</div></div><div class='stat'><div class='k'>Hostname</div><div class='v'>");
  html += html_escape(String(master_hostname));
  html += F("</div></div></div></section><form id='configForm' class='panel' method='post' action='/config/save' onsubmit='syncMqttChecks();syncLedChecks()'><input type='hidden' name='csrf' value='");
  html += web_csrf_token;
  html += F("'><h2>");
  html += web_text("WLAN-Zugang", "WiFi credentials");
  html += F("</h2><label>");
  html += web_text("Gefundene Netzwerke", "Detected networks");
  html += F("</label><select name='ssid'>");
  bool current_listed = false;
  for (int i = 0; i < n; ++i) {
    String scanned_ssid = WiFi.SSID(i);
    if (!scanned_ssid.length()) continue;
    html += F("<option value='"); html += html_escape(scanned_ssid); html += "'";
    if (scanned_ssid == String(wifi_ssid)) { html += F(" selected"); current_listed = true; }
    html += F(">"); html += html_escape(scanned_ssid); html += F(" ("); html += WiFi.RSSI(i); html += F(" dBm)</option>");
  }
  if (strlen(wifi_ssid) && !current_listed) {
    html += F("<option selected value='"); html += html_escape(String(wifi_ssid)); html += F("'>"); html += html_escape(String(wifi_ssid)); html += F("</option>");
  }
  if (n <= 0 && !strlen(wifi_ssid)) {
    html += F("<option value=''>"); html += web_text("Keine Netzwerke gefunden - SSID manuell eingeben", "No networks found - enter SSID manually"); html += F("</option>");
  }
  html += F("</select><div class='actions' style='margin-top:10px'><a class='btn secondary' href='/config'>");
  html += web_text("Netzwerke neu scannen", "Scan networks");
  html += F("</a></div><label>");
  html += web_text("SSID manuell eingeben", "Enter SSID manually");
  html += F("</label><input name='ssid_manual' maxlength='32' placeholder='SSID'><label>");
  html += web_text("WLAN-Passwort", "WiFi password");
  html += F("</label><input name='pass' maxlength='64' type='password' value='********' autocomplete='current-password'><label>Hostname</label><input name='hostname' maxlength='31' value='");
  html += html_escape(String(master_hostname));
  html += F("' placeholder='open-fume-extractor'><label>");
  html += web_text("IP-Konfiguration", "IP configuration");
  html += F("</label><select id='ip_mode' name='ip_mode' onchange='toggleStaticIp()'><option value='dhcp'");
  if (!wifi_static_enabled) html += F(" selected");
  html += F(">DHCP</option><option value='static'");
  if (wifi_static_enabled) html += F(" selected");
  html += F(">"); html += web_text("Statische IP", "Static IP");
  html += F("</option></select><div id='static_ip_fields' class='grid' style='margin-top:12px'><div><label>IP</label><input name='ip' inputmode='decimal' value='");
  html += wifi_static_ip.toString();
  html += F("' placeholder='192.168.1.50'></div><div><label>Gateway</label><input name='gateway' inputmode='decimal' value='");
  html += wifi_static_gateway.toString();
  html += F("' placeholder='192.168.1.1'></div><div><label>"); html += web_text("Netzmaske", "Subnet mask");
  html += F("</label><input name='subnet' inputmode='decimal' value='"); html += wifi_static_subnet.toString();
  html += F("' placeholder='255.255.255.0'></div><div><label>DNS 1</label><input name='dns1' inputmode='decimal' value='"); html += wifi_static_dns1.toString();
  html += F("' placeholder='192.168.1.1'></div><div><label>DNS 2</label><input name='dns2' inputmode='decimal' value='"); html += wifi_static_dns2.toString();
  html += F("' placeholder='8.8.8.8'></div></div><div class='actions' style='margin-top:16px'><button type='submit'>");
  html += web_text("Netzwerk speichern und neu starten", "Save network and reboot");
  html += F("</button></div><hr style='border:0;border-top:1px solid var(--line);margin:18px 0'><h2>Web Login</h2><p class='muted'>");
  html += web_text("Benutzer und Passwort für Web-UI, API und OTA. Falls vergessen: seriell 'webauth reset' senden.", "Username and password for web UI, API and OTA. If forgotten, send 'webauth reset' over Serial.");
  html += F("</p><div class='grid'><div><label>"); html += web_text("Benutzer", "Username"); html += F("</label><input name='web_user' maxlength='32' value='"); html += html_escape(String(web_auth_user));
  html += F("' placeholder='admin'></div><div><label>"); html += web_text("Neues Passwort", "New password"); html += F("</label><input name='web_pass' maxlength='64' type='password' placeholder='");
  html += web_text("Leer lassen, um das aktuelle Passwort zu behalten", "Leave empty to keep the current password");
  html += F("'></div></div><hr style='border:0;border-top:1px solid var(--line);margin:18px 0'><h2>MQTT</h2><p class='muted'>");
  html += web_text("MQTT-Basis für Home Assistant oder eigene Automationen. TLS nutzt Port 8883, ohne TLS normalerweise 1883.", "MQTT foundation for Home Assistant or custom automations. TLS usually uses port 8883, plain MQTT usually 1883.");
  html += F("</p><input type='hidden' id='mqtt_enabled_value' name='mqtt_enabled_value' value='0'><input type='hidden' id='mqtt_tls_value' name='mqtt_tls_value' value='0'><input type='hidden' id='mqtt_ha_value' name='mqtt_ha_value' value='0'><div class='grid'><div><label><input type='checkbox' id='mqtt_enabled' name='mqtt_enabled' value='1' onchange='syncMqttChecks()' style='width:auto;min-height:0;margin-right:8px'");
  if (mqtt_enabled) html += F(" checked");
  html += F(">"); html += web_text("MQTT aktivieren", "Enable MQTT"); html += F("</label></div><div><label><input type='checkbox' id='mqtt_tls' name='mqtt_tls' value='1' onchange='mqttTlsChanged();syncMqttChecks()' style='width:auto;min-height:0;margin-right:8px'");
  if (mqtt_tls_enabled) html += F(" checked");
  html += F(">TLS</label></div></div><div class='grid'><div><label>Broker Host</label><input name='mqtt_host' maxlength='64' value='"); html += html_escape(String(mqtt_host));
  html += F("' placeholder='homeassistant.local'></div><div><label>Port</label><input id='mqtt_port' name='mqtt_port' type='number' min='1' max='65535' value='"); html += mqtt_port;
  html += F("'></div><div><label>Benutzer</label><input name='mqtt_user' maxlength='64' value='"); html += html_escape(String(mqtt_user));
  html += F("'></div><div><label>Passwort</label><input name='mqtt_pass' maxlength='64' type='password' value='********'></div><div><label>Base Topic</label><input name='mqtt_topic' maxlength='64' value='"); html += html_escape(String(mqtt_base_topic));
  html += F("' placeholder='open-fume-extractor'></div><div><label>Discovery Prefix</label><input name='mqtt_disc' maxlength='32' value='"); html += html_escape(String(mqtt_discovery_prefix));
  html += F("' placeholder='homeassistant'></div></div><label><input type='checkbox' id='mqtt_ha' name='mqtt_ha' value='1' onchange='syncMqttChecks()' style='width:auto;min-height:0;margin-right:8px'");
  if (mqtt_ha_discovery) html += F(" checked");
  html += F(">"); html += web_text("Home Assistant Discovery vorbereiten", "Prepare Home Assistant discovery");
  html += F("</label><label>MQTT TLS CA Zertifikat / CA certificate (PEM)</label><textarea name='mqtt_ca' rows='8' style='font-family:ui-monospace,Consolas,monospace' placeholder='-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----'>");
  html += html_escape(mqtt_ca_cert);
  html += F("</textarea><p class='muted' style='font-size:12px;margin-top:5px'>");
  html += web_text("Wenn dieses Feld leer ist, nutzt TLS weiterhin den Kompatibilitätsmodus ohne Zertifikatsprüfung.", "When this field is empty, TLS continues in compatibility mode without certificate verification.");
  html += F("</p><div class='actions' style='margin-top:12px'><button type='button' class='secondary' onclick='saveMqtt()'>");
  html += web_text("MQTT speichern", "Save MQTT");
  html += F("</button><span id='mqtt_msg' class='muted'></span></div><hr style='border:0;border-top:1px solid var(--line);margin:18px 0'><h2>");
  html += web_text("Status-LEDs", "Status LEDs");
  html += F("</h2><p class='muted'>");
  html += web_text("Globale Helligkeit für Master- und Modul-Status-LEDs. Die Module folgen dem Master per OFE-Bus.", "Global brightness for master and module status LEDs. Modules follow the master over the OFE bus.");
  html += F("</p><input type='hidden' id='status_led_enabled_value' name='status_led_enabled_value' value='0'><div class='grid'><div><label><input type='checkbox' id='status_led_enabled' name='status_led_enabled' value='1' onchange='syncLedChecks()' style='width:auto;min-height:0;margin-right:8px'");
  if (status_led_enabled) html += F(" checked");
  html += F(">"); html += web_text("Status-LEDs aktivieren", "Enable status LEDs");
  html += F("</label></div><div><label>"); html += web_text("Status-LED Helligkeit (%)", "Status LED brightness (%)");
  html += F("</label><div style='display:grid;grid-template-columns:minmax(0,1fr) 64px;gap:10px;align-items:center'><input id='status_led_brightness_range' type='range' min='10' max='100' step='1' value='"); html += status_led_brightness_pct;
  html += F("' oninput='statusLedBrightnessChanged(this.value)'><input id='status_led_brightness' name='status_led_brightness' type='hidden' value='"); html += status_led_brightness_pct;
  html += F("'><span id='status_led_brightness_text' class='v' style='text-align:right'>"); html += status_led_brightness_pct; html += F("%</span></div></div></div><div class='actions' style='margin-top:12px'><button type='button' class='secondary' onclick='saveStatusLeds()'>");
  html += web_text("LEDs übernehmen", "Apply LEDs");
  html += F("</button><span id='status_led_msg' class='muted'></span></div></form><section class='panel'><h2>");
  html += web_text("Display-Verbindung", "Display connection");
  html += F("</h2><a class='btn' href='/display-link'>"); html += web_text("Displays koppeln", "Pair displays"); html += F("</a></section><section class='panel'><h2>Backup / Restore</h2><p class='muted'>");
  html += web_text("Sichert WLAN, Web-Login, MQTT und wichtige Master-Einstellungen als JSON. Achtung: Passwörter und Zertifikate sind enthalten.", "Exports WiFi, web login, MQTT and important master settings as JSON. Warning: passwords and certificates are included.");
  html += F("</p><div class='actions'><a class='btn' href='/config/export'>"); html += web_text("Backup herunterladen", "Download backup");
  html += F("</a></div><form method='post' action='/config/import' style='margin-top:14px' onsubmit='return submitBackupImport(event)'><input type='hidden' name='csrf' value='"); html += web_csrf_token;
  html += F("'><label>"); html += web_text("Backup-Datei", "Backup file"); html += F("</label><input id='backup_file' type='file' accept='.json,application/json' onchange='loadBackupFile(this.files&&this.files[0])'><input id='backup_json' name='backup_json' type='hidden'><div id='backup_status' class='muted' style='margin-top:10px'>");
  html += web_text("Noch keine Backup-Datei ausgewählt.", "No backup file selected yet.");
  html += F("</div><div class='actions' style='margin-top:12px'><button id='backup_import_btn' class='secondary' type='submit' disabled>"); html += web_text("Backup importieren und neu starten", "Import backup and reboot");
  html += F("</button></div></form></section><script>function syncLedChecks(){var c=document.getElementById('status_led_enabled'),h=document.getElementById('status_led_enabled_value');if(c&&h)h.value=c.checked?'1':'0';}function statusLedBrightnessChanged(v){v=Math.max(10,Math.min(100,parseInt(v||20,10)));var r=document.getElementById('status_led_brightness_range'),n=document.getElementById('status_led_brightness'),txt=document.getElementById('status_led_brightness_text');if(r&&String(r.value)!==String(v))r.value=v;if(n&&String(n.value)!==String(v))n.value=v;if(txt)txt.textContent=v+'%';}function syncMqttChecks(){['mqtt_enabled','mqtt_tls','mqtt_ha'].forEach(function(id){var c=document.getElementById(id),h=document.getElementById(id+'_value');if(c&&h)h.value=c.checked?'1':'0';});}function mqttTlsChanged(){var p=document.getElementById('mqtt_port'),tls=document.getElementById('mqtt_tls');if(p&&tls&&(p.value==='1883'||p.value==='8883'||p.value===''))p.value=tls.checked?'8883':'1883';}function setCfgMsg(id,ok,msg){var e=document.getElementById(id);if(e){e.textContent=msg;e.style.color=ok?'#40d37a':'#ffb86b';}}async function postConfigPart(url,msgId,prepare){try{if(prepare)prepare();var f=document.getElementById('configForm'),r=await fetch(url,{method:'POST',body:new FormData(f),cache:'no-store'}),t=await r.text();if(!r.ok)throw new Error(t||('HTTP '+r.status));setCfgMsg(msgId,true,'Gespeichert');}catch(e){setCfgMsg(msgId,false,'Fehler: '+(e&&e.message?e.message:e));}}function saveStatusLeds(){postConfigPart('/config/leds','status_led_msg',syncLedChecks);}function saveMqtt(){postConfigPart('/config/mqtt','mqtt_msg',syncMqttChecks);}function toggleStaticIp(){var e=document.getElementById('static_ip_fields');if(e)e.style.display=document.getElementById('ip_mode').value==='static'?'grid':'none';}function setBackupStatus(ok,msg){var s=document.getElementById('backup_status'),b=document.getElementById('backup_import_btn');if(s){s.textContent=msg;s.style.color=ok?'#40d37a':'#ffb86b';}if(b)b.disabled=!ok;}function validateBackupText(t){var o=JSON.parse(t);if(!o||o.product!=='Open Fume Extractor')throw new Error('Not an Open Fume Extractor backup');if(o.schema!==1)throw new Error('Unsupported backup version');if(!o.network&&!o.mqtt&&!o.master)throw new Error('Backup has no known settings');return o;}async function loadBackupFile(f){var h=document.getElementById('backup_json');if(h)h.value='';setBackupStatus(false,'');if(!f)return;try{var t=await f.text();validateBackupText(t);if(h)h.value=t;setBackupStatus(true,'Backup-Datei gültig. Import ist bereit.');}catch(e){setBackupStatus(false,'Backup ungültig: '+(e&&e.message?e.message:e));}}async function submitBackupImport(ev){if(ev)ev.preventDefault();var h=document.getElementById('backup_json');if(!h||!h.value){setBackupStatus(false,'Bitte zuerst eine gültige Backup-Datei auswählen.');return false;}if(!confirm('Backup wirklich importieren? Der Master startet danach neu.'))return false;try{var r=await fetch('/config/import',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':'"); html += web_csrf_token;
  html += F("'},body:h.value,cache:'no-store'}),txt=await r.text();if(!r.ok)throw new Error(txt||('HTTP '+r.status));document.open();document.write(txt);document.close();}catch(e){setBackupStatus(false,'Import fehlgeschlagen: '+(e&&e.message?e.message:e));}return false;}toggleStaticIp();syncMqttChecks();syncLedChecks();</script>");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
}

static void web_password_change_error_redirect(const char* reason) {
  web.sendHeader("Location", String("/config?password_error=") + reason, true);
  web.send(303, "text/plain; charset=utf-8", "");
}

static void web_handle_config_password() {
  String password = web.arg("web_pass");
  String confirmation = web.arg("web_pass_confirm");
  if (password.length() < 8 || password.length() > 64) {
    web_password_change_error_redirect("length");
    return;
  }
  if (password != confirmation) {
    web_password_change_error_redirect("mismatch");
    return;
  }
  if (password == MASTER_DEFAULT_PASSWORD) {
    web_password_change_error_redirect("default");
    return;
  }

  Preferences auth_prefs;
  if (!auth_prefs.begin(MasterSettingsStore::NS_NET, false)) {
    web_password_change_error_redirect("storage");
    return;
  }
  const bool written = auth_prefs.putString(MasterSettingsStore::KEY_WEB_USER, String(web_auth_user)) > 0 &&
                       auth_prefs.putString(MasterSettingsStore::KEY_WEB_PASS, password) > 0;
  auth_prefs.end();
  if (!written) {
    web_password_change_error_redirect("storage");
    return;
  }

  password.toCharArray(web_auth_password, sizeof(web_auth_password));
  web_password_change_required = false;
  String html;
  html.reserve(2200);
  web_shell_begin(html, web_text("Passwort gespeichert", "Password saved"), web_text("Netzwerk", "Network"), "config");
  html += F("<section class='panel'><h2>");
  html += web_text("Passwort gespeichert", "Password saved");
  html += F("</h2><p>");
  html += web_text("Das neue Webpasswort wurde gespeichert. Bitte melde dich mit dem neuen Passwort erneut an.", "The new web password was saved. Please sign in again with the new password.");
  html += F("</p><div class='actions'><a class='btn' href='/config'>");
  html += web_text("Zur Netzwerkseite", "Open network setup");
  html += F("</a></div></section><script>setTimeout(function(){location.href='/config';},1800);</script>");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
}

static void web_handle_config_leds() {
  const bool status_led_enabled_form = web.hasArg("status_led_enabled_value") ? web.arg("status_led_enabled_value") == "1" : web.hasArg("status_led_enabled");
  uint32_t status_led_brightness_form32 = (uint32_t)strtoul(web.arg("status_led_brightness").c_str(), nullptr, 10);
  if (status_led_brightness_form32 < 10UL) status_led_brightness_form32 = 10UL;
  if (status_led_brightness_form32 > 100UL) status_led_brightness_form32 = 100UL;

  if (!netcfg_save(String(wifi_ssid), String(wifi_password), String(master_hostname), String(web_auth_user), String(web_auth_password),
              wifi_static_enabled, wifi_static_ip, wifi_static_gateway, wifi_static_subnet, wifi_static_dns1, wifi_static_dns2,
              mqtt_enabled, mqtt_tls_enabled, String(mqtt_host), mqtt_port, String(mqtt_user), String(mqtt_password),
              String(mqtt_base_topic), mqtt_ha_discovery, String(mqtt_discovery_prefix), mqtt_ca_cert,
              status_led_enabled_form, (uint8_t)status_led_brightness_form32)) {
    web.send(507, "text/plain; charset=utf-8", "Failed to persist settings");
    return;
  }
  mqtt_last_publish_ms = 0;
  web.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

static void web_handle_config_mqtt() {
  String mqtt_host_arg = web.arg("mqtt_host"); mqtt_host_arg.trim();
  String mqtt_user_arg = web.arg("mqtt_user"); mqtt_user_arg.trim();
  String mqtt_pass_arg = web.arg("mqtt_pass");
  if (!mqtt_pass_arg.length() || mqtt_pass_arg == "********") mqtt_pass_arg = String(mqtt_password);
  String mqtt_topic_arg = web.arg("mqtt_topic"); mqtt_topic_arg.trim();
  if (!mqtt_topic_arg.length()) mqtt_topic_arg = "open-fume-extractor";
  String mqtt_disc_arg = web.arg("mqtt_disc"); mqtt_disc_arg.trim();
  if (!mqtt_disc_arg.length()) mqtt_disc_arg = "homeassistant";
  String mqtt_ca_arg = web.arg("mqtt_ca");
  mqtt_ca_arg.replace("\r\n", "\n");
  mqtt_ca_arg.trim();
  const bool mqtt_enabled_form = web.hasArg("mqtt_enabled_value") ? web.arg("mqtt_enabled_value") == "1" : web.hasArg("mqtt_enabled");
  const bool mqtt_tls_form = web.hasArg("mqtt_tls_value") ? web.arg("mqtt_tls_value") == "1" : web.hasArg("mqtt_tls");
  const bool mqtt_ha_form = web.hasArg("mqtt_ha_value") ? web.arg("mqtt_ha_value") == "1" : web.hasArg("mqtt_ha");
  if (mqtt_tls_form && mqtt_ca_arg.length() &&
      (mqtt_ca_arg.indexOf("-----BEGIN CERTIFICATE-----") < 0 || mqtt_ca_arg.indexOf("-----END CERTIFICATE-----") < 0)) {
    web.send(400, "text/plain; charset=utf-8", "Invalid MQTT CA certificate PEM");
    return;
  }
  uint32_t mqtt_port_arg32 = (uint32_t)strtoul(web.arg("mqtt_port").c_str(), nullptr, 10);
  if (!mqtt_port_arg32 || mqtt_port_arg32 > 65535UL) mqtt_port_arg32 = mqtt_tls_form ? 8883 : 1883;

  if (!netcfg_save(String(wifi_ssid), String(wifi_password), String(master_hostname), String(web_auth_user), String(web_auth_password),
              wifi_static_enabled, wifi_static_ip, wifi_static_gateway, wifi_static_subnet, wifi_static_dns1, wifi_static_dns2,
              mqtt_enabled_form, mqtt_tls_form, mqtt_host_arg, (uint16_t)mqtt_port_arg32,
              mqtt_user_arg, mqtt_pass_arg, mqtt_topic_arg, mqtt_ha_form, mqtt_disc_arg, mqtt_ca_arg,
              status_led_enabled, status_led_brightness_pct)) {
    web.send(507, "text/plain; charset=utf-8", "Failed to persist settings");
    return;
  }
  web.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}
static void web_handle_config_save() {
  if (captive_active && !web_password_change_required) {
    web_handle_initial_network_save();
    return;
  }
  String ssid = web.arg("ssid_manual");
  ssid.trim();
  if (!ssid.length()) ssid = web.arg("ssid");
  String pass = web.arg("pass");
  if (!pass.length() || pass == "********") pass = String(wifi_password);
  if (!ssid.length()) {
    web.send(400, "text/plain; charset=utf-8", "Missing SSID");
    return;
  }

  String hostname = normalized_hostname(web.arg("hostname"));
  if (!hostname.length()) {
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "open-fume-extractor-%08lx", (uint32_t)ESP.getEfuseMac());
    hostname = fallback;
  }

  String web_user_arg = web.arg("web_user");
  web_user_arg.trim();
  if (!web_user_arg.length()) web_user_arg = WEB_AUTH_USER;
  String web_pass_arg = web.arg("web_pass");
  if (!web_pass_arg.length() || web_pass_arg == "********") web_pass_arg = String(web_auth_password);
  if (web_pass_arg.length() < 8) {
    web.send(400, "text/plain; charset=utf-8", "Web password must be at least 8 characters");
    return;
  }

  const bool use_static = web.arg("ip_mode") == "static";
  IPAddress ip(0, 0, 0, 0), gateway(0, 0, 0, 0), subnet(255, 255, 255, 0);
  IPAddress dns1(0, 0, 0, 0), dns2(0, 0, 0, 0);
  if (use_static) {
    if (!parse_ipv4(web.arg("ip"), ip) || !parse_ipv4(web.arg("gateway"), gateway) ||
        !parse_ipv4(web.arg("subnet"), subnet)) {
      web.send(400, "text/plain; charset=utf-8", "Invalid static IP, gateway or subnet mask");
      return;
    }
    if (!parse_ipv4(web.arg("dns1"), dns1)) dns1 = gateway;
    parse_ipv4(web.arg("dns2"), dns2);
  }

  String mqtt_host_arg = web.arg("mqtt_host"); mqtt_host_arg.trim();
  String mqtt_user_arg = web.arg("mqtt_user"); mqtt_user_arg.trim();
  String mqtt_pass_arg = web.arg("mqtt_pass");
  if (!mqtt_pass_arg.length() || mqtt_pass_arg == "********") mqtt_pass_arg = String(mqtt_password);
  String mqtt_topic_arg = web.arg("mqtt_topic"); mqtt_topic_arg.trim();
  if (!mqtt_topic_arg.length()) mqtt_topic_arg = "open-fume-extractor";
  String mqtt_disc_arg = web.arg("mqtt_disc"); mqtt_disc_arg.trim();
  if (!mqtt_disc_arg.length()) mqtt_disc_arg = "homeassistant";
  String mqtt_ca_arg = web.arg("mqtt_ca");
  mqtt_ca_arg.replace("\r\n", "\n");
  mqtt_ca_arg.trim();
  const bool mqtt_enabled_form = web.hasArg("mqtt_enabled_value") ? web.arg("mqtt_enabled_value") == "1" : web.hasArg("mqtt_enabled");
  const bool mqtt_tls_form = web.hasArg("mqtt_tls_value") ? web.arg("mqtt_tls_value") == "1" : web.hasArg("mqtt_tls");
  const bool mqtt_ha_form = web.hasArg("mqtt_ha_value") ? web.arg("mqtt_ha_value") == "1" : web.hasArg("mqtt_ha");
  const bool status_led_enabled_form = web.hasArg("status_led_enabled_value") ? web.arg("status_led_enabled_value") == "1" : web.hasArg("status_led_enabled");
  uint32_t status_led_brightness_form32 = (uint32_t)strtoul(web.arg("status_led_brightness").c_str(), nullptr, 10);
  if (status_led_brightness_form32 < 10UL) status_led_brightness_form32 = 10UL;
  if (status_led_brightness_form32 > 100UL) status_led_brightness_form32 = 100UL;
  if (mqtt_tls_form && mqtt_ca_arg.length() &&
      (mqtt_ca_arg.indexOf("-----BEGIN CERTIFICATE-----") < 0 || mqtt_ca_arg.indexOf("-----END CERTIFICATE-----") < 0)) {
    web.send(400, "text/plain; charset=utf-8", "Invalid MQTT CA certificate PEM");
    return;
  }
  uint32_t mqtt_port_arg32 = (uint32_t)strtoul(web.arg("mqtt_port").c_str(), nullptr, 10);
  if (!mqtt_port_arg32 || mqtt_port_arg32 > 65535UL) mqtt_port_arg32 = mqtt_tls_form ? 8883 : 1883;

  if (hostname == "open-fume-extractor") {
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "open-fume-extractor-%08lx", (uint32_t)ESP.getEfuseMac());
    hostname = fallback;
  }
  if (!netcfg_save(ssid, pass, hostname, web_user_arg, web_pass_arg, use_static, ip, gateway, subnet, dns1, dns2,
              mqtt_enabled_form, mqtt_tls_form, mqtt_host_arg, (uint16_t)mqtt_port_arg32,
              mqtt_user_arg, mqtt_pass_arg, mqtt_topic_arg, mqtt_ha_form, mqtt_disc_arg, mqtt_ca_arg,
              status_led_enabled_form, (uint8_t)status_led_brightness_form32)) {
    web.send(507, "text/plain; charset=utf-8", "Failed to persist settings");
    return;
  }
  String html;
  html.reserve(4300);
  web_shell_begin(html, web_text("WLAN gespeichert", "WiFi saved"), web_text("Netzwerk", "Network"), "config");
  html += F("<section class='panel'><h2>"); html += web_text("Einstellungen übernommen", "Settings saved"); html += F("</h2><p>"); html += web_text("Der Master startet neu und verbindet sich mit dem ausgewählten WLAN. Diese Seite wechselt danach automatisch zur Statusseite.", "The master is restarting and connecting to the selected WiFi network. This page will automatically switch back to the status page."); html += F("</p><div class='progress'><div class='bar' style='width:100%'></div></div><p class='muted' id='redir'>"); html += web_text("Weiterleitung in wenigen Sekunden...", "Redirecting in a few seconds..."); html += F("</p></section><script>setTimeout(function(){location.href='http://"); html += html_escape(hostname); html += F(".local/';},9000);setTimeout(function(){location.href='http://"); html += use_static ? ip.toString() : WiFi.localIP().toString(); html += F("/';},15000);</script>");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
  prepare_controlled_restart();
  delay(900);
  ESP.restart();
}
