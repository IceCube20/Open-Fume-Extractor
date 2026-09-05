#pragma once

// Shared Web UI helpers, /state JSON and the main status page.
// Included from the master sketch so the existing static globals remain in one
// Arduino translation unit.
static const char* module_type_name(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "JBC FAE Bus";
    case MODULE_JBC_USB: return "JBC USB";
    case MODULE_FAN_IO: return "Fan/IO";
    case MODULE_FAN_IO_PRO: return "Fan/IO Pro";
    case MODULE_SENSOR_RESERVED: return "Sensor";
    case MODULE_WELLER_ZERO_SMOG: return "Weller Zero Smog Bus";
    case MODULE_DISPLAY: return "Display";
    case MODULE_UNIVERSAL_RS232: return "Universal RS232 Bridge";
    case MODULE_MODBUS_RTU: return "Modbus RTU Bridge";
    default: return "Unknown";
  }
}

static const char* module_type_name_for(const ModuleRecord& m) {
  if (m.type == MODULE_DISPLAY || (m.caps & CAP_DISPLAY)) {
    if (m.caps & CAP_DISPLAY_800X480) return "Display 800x480";
    if (m.caps & CAP_DISPLAY_320X480) return "Display 320x480";
    return "Display";
  }
  return module_type_name(m.type);
}
static String json_escape(const char* s) {
  String out;
  if (!s) return out;
  while (*s) {
    const char c = *s++;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += F("\\n");
    } else if (c == '\r') {
      out += F("\\r");
    } else if (c == '\t') {
      out += F("\\t");
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

static String current_master_ip_string() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  IPAddress ap = WiFi.softAPIP();
  if (ap != IPAddress(0, 0, 0, 0)) return ap.toString();
  return String("0.0.0.0");
}

static String uid_hex(uint64_t uid) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lX%08lX", (uint32_t)(uid >> 32), (uint32_t)uid);
  return String(buf);
}

static String u64_dec(uint64_t value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return String(buf);
}

#if WEB_ENABLE
static String module_label_key(uint64_t uid) {
  return MasterSettingsStore::moduleLabelKey(uid);
}

static String clean_module_label(String label) {
  label.trim();
  if (label.length() > 23) label = label.substring(0, 23);
  for (uint8_t i = 0; i < label.length(); ++i) {
    char c = label[i];
    if ((uint8_t)c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>') label.setCharAt(i, ' ');
  }
  label.trim();
  return label;
}

static String clean_io_alias(String label) {
  label = clean_module_label(label);
  if (label.length() > 18) label = label.substring(0, 18);
  label.trim();
  return label;
}

static String module_label_for(const ModuleRecord& m) {
  // Labels are loaded into ModuleRecord::label by apply_module_labels() at boot,
  // after scans and after label changes. Do not hit NVS from /state, MQTT, CLI,
  // alarm/update pages or other hot read paths.
  return m.label[0] ? String(m.label) : String("");
}

static String module_snapshot_key(uint8_t index, char suffix) {
  return MasterSettingsStore::moduleSnapshotKey(index, suffix);
}

static void load_module_snapshot() {
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  const uint8_t raw_count = master_prefs.getUChar(MasterSettingsStore::KEY_MODULE_SNAPSHOT_COUNT, 0);
  const uint8_t count = raw_count > ModuleRegistry::MAX_MODULES ? ModuleRegistry::MAX_MODULES : raw_count;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t addr = master_prefs.getUChar(module_snapshot_key(i, 'a').c_str(), jbc_rs485::ADDR_INVALID);
    const uint8_t type = master_prefs.getUChar(module_snapshot_key(i, 't').c_str(), jbc_rs485::MODULE_UNKNOWN);
    const String uid_s = master_prefs.getString(module_snapshot_key(i, 'u').c_str(), String(""));
    const uint64_t uid = strtoull(uid_s.c_str(), nullptr, 16);
    if (addr < 0x10 || addr > 0x6F || !uid) continue;
    ModuleRecord* rec = registry.upsert(addr);
    if (!rec) continue;
    rec->addr = addr;
    rec->type = type;
    rec->uid = uid;
    rec->online = false;
    rec->seen_in_scan = false;
  }
  master_prefs.end();
  apply_module_labels();
}

static void save_module_snapshot() {
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  uint8_t saved = 0;
  for (uint8_t i = 0; i < registry.count() && saved < ModuleRegistry::MAX_MODULES; ++i) {
    const ModuleRecord& m = registry.at(i);
    if (!m.uid || m.addr < 0x10 || m.addr > 0x6F) continue;
    master_prefs.putUChar(module_snapshot_key(saved, 'a').c_str(), m.addr);
    master_prefs.putUChar(module_snapshot_key(saved, 't').c_str(), m.type);
    master_prefs.putString(module_snapshot_key(saved, 'u').c_str(), uid_hex(m.uid));
    ++saved;
  }
  const uint8_t old_count = master_prefs.getUChar(MasterSettingsStore::KEY_MODULE_SNAPSHOT_COUNT, 0);
  master_prefs.putUChar(MasterSettingsStore::KEY_MODULE_SNAPSHOT_COUNT, saved);
  for (uint8_t i = saved; i < old_count && i < ModuleRegistry::MAX_MODULES; ++i) {
    master_prefs.remove(module_snapshot_key(i, 'a').c_str());
    master_prefs.remove(module_snapshot_key(i, 't').c_str());
    master_prefs.remove(module_snapshot_key(i, 'u').c_str());
  }
  master_prefs.end();
}

static String module_display_name(const ModuleRecord& m) {
  String label = module_label_for(m);
  if (label.length()) {
    // Keep user labels, but never hide the physical display resolution.
    if (m.type == MODULE_DISPLAY || (m.caps & CAP_DISPLAY)) {
      const char* type_name = module_type_name_for(m);
      if ((m.caps & (CAP_DISPLAY_320X480 | CAP_DISPLAY_800X480)) != 0) {
        String out = label;
        out += " (";
        out += type_name;
        out += ")";
        return out;
      }
    }
    return label;
  }
  return String(module_type_name_for(m));
}

static String module_addr_display_name(const ModuleRecord& m) {
  char addr[5];
  snprintf(addr, sizeof(addr), "0x%02X", m.addr);
  String out = addr;
  out += ' ';
  out += module_display_name(m);
  return out;
}

static String universal_profile_key(uint8_t addr, const char* suffix) {
  char key[15];
  snprintf(key, sizeof(key), "u%02X_%s", addr, suffix);
  return String(key);
}

static String clean_profile_value(String value, uint8_t max_len) {
  value.trim();
  if (value.length() > max_len) value = value.substring(0, max_len);
  for (uint8_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if ((uint8_t)c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>') value.setCharAt(i, ' ');
  }
  value.trim();
  return value;
}

static String universal_profile_value(uint8_t addr, const char* suffix, const char* fallback) {
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  String value = master_prefs.getString(universal_profile_key(addr, suffix).c_str(), String(fallback));
  master_prefs.end();
  return value;
}
static void apply_module_labels() {
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  for (uint8_t i = 0; i < registry.count(); ++i) {
    ModuleRecord& m = registry.at(i);
    m.label[0] = 0;
    if (!m.uid) continue;
    String label = master_prefs.getString(module_label_key(m.uid).c_str(), String(""));
    label.toCharArray(m.label, sizeof(m.label));
  }
  master_prefs.end();
}
#endif

static String bytes_hex(const uint8_t* data, uint8_t len) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve((size_t)len * 2U);
  for (uint8_t i = 0; i < len; ++i) {
    out += hex[data[i] >> 4];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

static String bytes_ascii(const uint8_t* data, uint8_t len) {
  String out;
  out.reserve(len);
  for (uint8_t i = 0; i < len; ++i) {
    const char c = (char)data[i];
    if (c >= 32 && c < 127) out += c;
    else if (c != 0) out += '.';
  }
  out.trim();
  return out.length() ? out : String("-");
}

static String duration_text_minutes(uint32_t minutes) {
  const uint32_t days = minutes / 1440UL;
  minutes %= 1440UL;
  const uint32_t hours = minutes / 60UL;
  minutes %= 60UL;
  String out;
  if (days) out += String(days) + "d";
  if (hours) {
    if (out.length()) out += ' ';
    out += String(hours) + "h";
  }
  if (minutes || !out.length()) {
    if (out.length()) out += ' ';
    out += String(minutes) + "m";
  }
  return out;
}

static String duration_text_seconds(uint32_t seconds) {
  if (seconds < 60UL) return String(seconds) + "s";
  return duration_text_minutes((seconds + 30UL) / 60UL);
}


static String jbc_error_text(uint16_t mask) {
  if (!mask) return String("OK");
  const bool de = strcmp(web_lang, "de") == 0;
  String out;
  auto add = [&](const char* text) {
    if (out.length()) out += F(", ");
    out += text;
  };
  if (mask & 0x0001U) add(de ? "Filterlaufzeit abgelaufen" : "Filter lifetime expired");
  if (mask & 0x0002U) add(de ? "Filterlaufzeit endet bald" : "Filter lifetime ending");
  if (mask & 0x0004U) add(de ? "Filter verstopft" : "Filter clogged");
  if (mask & 0x0008U) add(de ? "Filter fast verstopft" : "Filter almost clogged");
  if (mask & 0x0010U) add(de ? "Kein Filter" : "No filter");
  if (mask & 0x0020U) add(de ? "Abdeckung offen" : "Cover open");
  if (mask & 0x0040U) add(de ? "Lüfter defekt" : "Blower damaged");
  if (mask & 0x0100U) add(de ? "Ventilfehler" : "Valve error");
  if (mask & 0x0200U) add(de ? "Aux Überstrom" : "Aux overcurrent");
  if (mask & 0x0400U) add(de ? "Pedalfehler" : "Pedal error");
  if (mask & 0x0800U) add(de ? "FAE Systemfehler" : "FAE system error");
  if (mask & 0x1000U) add(de ? "FAE Systemfehler 2" : "FAE system error 2");
  const uint16_t known = 0x1F7FU;
  if (mask & ~known) {
    char buf[12];
    snprintf(buf, sizeof(buf), "0x%X", mask & ~known);
    add(buf);
  }
  return out;
}

static String output_fault_text(uint16_t mask) {
  return output_fault_text_for_module(mask, MODULE_UNKNOWN);
}

static String output_fault_text_for_module(uint16_t mask, uint8_t module_type) {
  if (!mask) return String("OK");
  const bool de = strcmp(web_lang, "de") == 0;
  const bool weller = module_type == MODULE_WELLER_ZERO_SMOG;
  const bool local_bridge = module_type == MODULE_UNIVERSAL_RS232 || module_type == MODULE_MODBUS_RTU;
  String out;
  auto add = [&](const char* text) {
    if (out.length()) out += F(", ");
    out += text;
  };
  if (mask & 0x0001U) add(weller ? (de ? "Weller Gerätebus Fehler" : "Weller device bus error") : (local_bridge ? (de ? "Lokaler Gerätebus inaktiv" : "Local device bus inactive") : (de ? "Drehzahlrückmeldung fehlt" : "No speed feedback")));
  if (mask & 0x0002U) add(de ? "Filterwarnung" : "Filter warning");
  if (mask & 0x0004U) add(de ? "Filter voll" : "Filter full");
  if (mask & 0x0008U) add(de ? "Filter fehlt" : "Filter missing");
  if (mask & 0x0010U) add(de ? "Sensorfehler" : "Sensor fault");
  if (mask & 0x0100U) add(de ? "Drehzahlrückmeldung fehlt" : "No speed feedback");
  if (mask & 0x0200U) add(de ? "Master Timeout" : "Master timeout");
  if (mask & 0x0400U) add(de ? "Drehzahl zu niedrig" : "Low RPM");
  const uint16_t known = 0x071FU;
  if (mask & ~known) {
    char buf[12];
    snprintf(buf, sizeof(buf), "0x%X", mask & ~known);
    add(buf);
  }
  char raw[14];
  snprintf(raw, sizeof(raw), " (0x%X)", mask);
  out += raw;
  return out;
}

static MasterAlarmJson build_master_alarm_json() {
  const bool de = strcmp(web_lang, "de") == 0;
  MasterAlarmJson out;
  String strings = "[";
  String items = "[";
  bool first = true;
  bool active_output_fault_alarm = false;
  auto add_alarm = [&](const String& title, const String& detail, bool critical) {
    if (!title.length() && !detail.length()) return;
    String line = title;
    if (detail.length()) {
      if (line.length()) line += F(": ");
      line += detail;
    }
    if (out.text.length()) out.text += F("; ");
    out.text += line;
    if (!first) {
      strings += ',';
      items += ',';
    }
    first = false;
    strings += '"'; strings += json_escape(line.c_str()); strings += '"';
    items += F("{\"title\":\""); items += json_escape(title.c_str());
    items += F("\",\"detail\":\""); items += json_escape(detail.c_str());
    items += F("\",\"critical\":"); items += critical ? F("true") : F("false");
    items += '}';
    if (out.count < 255) out.count++;
  };

  if (!scheduler.mainInputSourceAvailable()) {
    add_alarm(de ? F("Haupteingang Absaugung") : F("Main extractor input"),
              de ? F("Kein Haupteingang Absaugung gewählt") : F("No main extractor input selected"),
              false);
  }
  if (scheduler.outputAddr() == 0) {
    add_alarm(de ? F("Hauptausgang Absaugung") : F("Main extractor output"),
              de ? F("Kein Hauptausgang Absaugung gewählt") : F("No main extractor output selected"),
              false);
  }

  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    const String title = module_addr_display_name(m);
    if (!m.online) {
      add_alarm(title, de ? F("Modul offline") : F("Module offline"), true);
      continue;
    }
    uint16_t faults = m.io_fault_mask | m.output_fault_mask;
    const bool bridge_link_fault = (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) && (faults & 0x0001U);
    const uint16_t output_fault_bits = 0x071FU;
    if (m.addr != scheduler.outputAddr() && !m.role_output && !m.output_enabled) {
      faults = (uint16_t)((faults & (uint16_t)~output_fault_bits) | (bridge_link_fault ? 0x0001U : 0));
    }
    if (faults) {
      if (m.addr == scheduler.outputAddr()) active_output_fault_alarm = true;
      add_alarm(title, output_fault_text_for_module(faults, m.type), faults != 0x0002U);
      continue;
    }
    if ((m.caps & CAP_JBC_BUS) && (!(m.jbc_link_flags & FAST_FLAG_CONNECTED) || !m.station_addr)) {
      add_alarm(title, de ? F("JBC-Station getrennt") : F("JBC station disconnected"), false);
      continue;
    }
    if ((m.caps & CAP_JBC_USB) && !(m.jbc_link_flags & FAST_FLAG_CONNECTED)) {
      add_alarm(title, de ? F("JBC-USB getrennt") : F("JBC USB disconnected"), false);
      continue;
    }
    if ((m.caps & CAP_WELLER_INTERFACE) && (m.weller_uart_age_sec == 0xFFFF || m.weller_uart_age_sec > 10)) {
      add_alarm(title, de ? F("Weller-Verbindung getrennt") : F("Weller link disconnected"), false);
      continue;
    }
  }

  bool has_jbc_module = false;
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (m.online && (m.caps & CAP_JBC_BUS)) has_jbc_module = true;
  }
  const uint16_t system_jbc_error = scheduler.systemJbcError();
  if (scheduler.outputAddr() && has_jbc_module && !active_output_fault_alarm && system_jbc_error) {
    add_alarm(de ? F("JBC-Fehler gesendet") : F("JBC error sent"), jbc_error_text(system_jbc_error), (system_jbc_error & (uint16_t)~0x0002U) != 0);
  }

  strings += ']';
  items += ']';
  out.strings_json = strings;
  out.items_json = items;
  if (!out.text.length()) out.text = F("OK");
  return out;
}static const char* station_type_name(uint8_t addr) {
  if (addr >= 0x18 && addr <= 0x21) return "DDE";
  if (addr >= 0x12 && addr <= 0x15) return "JTSE";
  if (addr == 0) return "-";
  return "JBC";
}

#if WEB_ENABLE
static String html_escape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}
static void load_routing_config();
static void load_control_settings();
static uint16_t customPowerFromSelectFlow(uint16_t select_flow);
static void apply_control_settings(uint8_t suction, uint16_t select_flow, uint16_t delay_work,
                                   uint16_t delay_stand, bool stand_intakes, bool continuous,
                                   bool persist);
static bool mqtt_apply_module_command(const String& leaf, const String& value);
#endif


static uint8_t module_comm_quality_code(const ModuleRecord& m) {
  if (!m.online) return 3;
  if (m.consecutive_timeouts >= 3) return 2;
  if (m.consecutive_timeouts > 0) return 1;
  return 0;
}

static const char* module_comm_quality_text(const ModuleRecord& m) {
  const bool wifi = m.type == MODULE_DISPLAY && master_display_wifi.active(m.addr);
  switch (module_comm_quality_code(m)) {
    case 0: return wifi ? "WLAN OK" : "RS485 OK";
    case 1: return wifi ? "WLAN Miss" : "RS485 Miss";
    case 2: return wifi ? "WLAN instable" : "RS485 instable";
    default: return wifi ? "WLAN offline" : "RS485 offline";
  }
}

static String descriptor_meta_value(const char* line, const char* key) {
  if (!line || !key || !*key) return String();
  String needle = String(key) + "=";
  const char* p = line;
  while ((p = strstr(p, needle.c_str())) != nullptr) {
    if (p == line || p[-1] == ' ' || p[-1] == '\t') {
      p += needle.length();
      const char* end = p;
      while (*end) {
        if ((*end == ' ' || *end == '\t')) {
          const char* q = end + 1;
          while (*q == ' ' || *q == '\t') ++q;
          const char* r = q;
          while ((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z') || (*r >= '0' && *r <= '9') || *r == '_') ++r;
          if (r > q && *r == '=') break;
        }
        ++end;
      }
      char tmp[96];
      size_t n = (size_t)(end - p);
      if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
      memcpy(tmp, p, n);
      tmp[n] = 0;
      String out(tmp);
      out.trim();
      return out;
    }
    p += needle.length();
  }
  return String();
}

static bool descriptor_first_tokens(const char* line, uint8_t& id, String& type, String& key, String& mode) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') ++line;
  if (*line < '0' || *line > '9') return false;
  uint16_t v = 0;
  while (*line >= '0' && *line <= '9') {
    v = (uint16_t)(v * 10U + (uint8_t)(*line - '0'));
    if (v > 255) return false;
    ++line;
  }
  id = (uint8_t)v;
  auto next_tok = [&](String& out) -> bool {
    while (*line == ' ' || *line == '\t') ++line;
    if (!*line) return false;
    const char* start = line;
    while (*line && *line != ' ' && *line != '\t') ++line;
    char tmp[48];
    size_t n = (size_t)(line - start);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, start, n);
    tmp[n] = 0;
    out = String(tmp);
    out.trim();
    return out.length() > 0;
  };
  return next_tok(type) && next_tok(key) && next_tok(mode);
}

static void append_universal_entity_defs_json(String& json, const ModuleRecord& m) {
  json += ",\"universal_entity_defs\":[";
  if (m.universal_descriptor_valid && m.universal_descriptor[0]) {
    const char* p = m.universal_descriptor;
    bool first = true;
    while (p && *p) {
      const char* line = p;
      const char* next = strchr(p, '\n');
      char buf[260];
      size_t n = next ? (size_t)(next - line) : strlen(line);
      if (n >= sizeof(buf)) n = sizeof(buf) - 1;
      memcpy(buf, line, n);
      buf[n] = 0;
      uint8_t id = 0;
      String type, key, mode;
      if (descriptor_first_tokens(buf, id, type, key, mode) && id >= 20) {
        String source = descriptor_meta_value(buf, "source");
        if (!source.length() || source.equalsIgnoreCase("profile")) {
          String role = descriptor_meta_value(buf, "role");
          String label = descriptor_meta_value(buf, strcmp(web_lang, "de") == 0 ? "de" : "en");
          if (!label.length()) label = descriptor_meta_value(buf, "de");
          if (!label.length()) label = descriptor_meta_value(buf, "en");
          if (!label.length()) label = key;
          String unit = descriptor_meta_value(buf, "unit");
          String access = descriptor_meta_value(buf, "access");
          String minv = descriptor_meta_value(buf, "min");
          String maxv = descriptor_meta_value(buf, "max");
          String step = descriptor_meta_value(buf, "step");
          String idx = descriptor_meta_value(buf, "idx");
          String group = descriptor_meta_value(buf, "group");
          String ui = descriptor_meta_value(buf, "ui");
          if (!first) json += ',';
          first = false;
          json += "{\"id\":"; json += id;
          json += ",\"type\":\""; json += json_escape(type.c_str()); json += "\"";
          json += ",\"key\":\""; json += json_escape(key.c_str()); json += "\"";
          json += ",\"mode\":\""; json += json_escape(mode.c_str()); json += "\"";
          json += ",\"label\":\""; json += json_escape(label.c_str()); json += "\"";
          json += ",\"meta\":{\"source\":\"profile\"";
          if (role.length()) { json += ",\"role\":\""; json += json_escape(role.c_str()); json += "\""; }
          if (unit.length()) { json += ",\"unit\":\""; json += json_escape(unit.c_str()); json += "\""; }
          if (access.length()) { json += ",\"access\":\""; json += json_escape(access.c_str()); json += "\""; }
          if (minv.length()) { json += ",\"min\":\""; json += json_escape(minv.c_str()); json += "\""; }
          if (maxv.length()) { json += ",\"max\":\""; json += json_escape(maxv.c_str()); json += "\""; }
          if (step.length()) { json += ",\"step\":\""; json += json_escape(step.c_str()); json += "\""; }
          if (idx.length()) { json += ",\"idx\":\""; json += json_escape(idx.c_str()); json += "\",\"profile_index\":\""; json += json_escape(idx.c_str()); json += "\""; }
          if (group.length()) { json += ",\"group\":\""; json += json_escape(group.c_str()); json += "\""; }
          if (ui.length()) { json += ",\"ui\":\""; json += json_escape(ui.c_str()); json += "\""; }
          json += "}}";
        }
      }
      p = next ? next + 1 : nullptr;
    }
  }
  json += "]";
}

static String build_state_json(bool include_universal_descriptor, bool include_heap_diag) {
  const JbcModuleState& js = extractor.jbcState();
  const JbcModuleState& cs = scheduler.controlSettings();
  const uint16_t system_jbc_error = scheduler.systemJbcError();
  const uint16_t system_jbc_filter_life = scheduler.systemJbcFilterLife();
  const uint16_t system_jbc_filter_sat = scheduler.systemJbcFilterSaturation();
  const OutputModuleState& os = extractor.outputState();
  String json;
  // The production ESP32-S3 Master has PSRAM and master_setup() routes ordinary
  // allocations >= MASTER_EXTMEM_MALLOC_THRESHOLD to external RAM. Reserve a
  // 16-module-sized state buffer up front so String never has to walk through
  // a chain of progressively larger reallocations while the web state is built.
  const size_t state_json_reserve = master_extmem_malloc_enabled
      ? (include_universal_descriptor ? (size_t)MASTER_STATE_JSON_DESC_RESERVE_PSRAM
                                      : (size_t)MASTER_STATE_JSON_RESERVE_PSRAM)
      : (include_universal_descriptor ? (size_t)MASTER_STATE_JSON_DESC_RESERVE_INTERNAL
                                      : (size_t)MASTER_STATE_JSON_RESERVE_INTERNAL);
  if (!json.reserve(state_json_reserve) && master_extmem_malloc_enabled) {
    // Keep non-PSRAM/fault fallback behavior graceful instead of returning an
    // empty response if the large external allocation ever cannot be satisfied.
    json.reserve(include_universal_descriptor ? MASTER_STATE_JSON_DESC_RESERVE_INTERNAL
                                              : MASTER_STATE_JSON_RESERVE_INTERNAL);
  }
  json += "{";
  json += "\"uptime_ms\":"; json += millis();
  json += ",\"master_fw\":\""; json += MASTER_FW_VERSION; json += "\"";
  json += ",\"master_name\":\""; json += MASTER_FW_NAME; json += "\"";
  json += ",\"ui_lang\":\""; json += web_lang; json += "\"";
  json += ",\"heap_free\":"; json += ESP.getFreeHeap();
  json += ",\"psram_free\":"; json += ESP.getFreePsram();
  json += ",\"psram_total\":"; json += ESP.getPsramSize();
  json += ",\"extmem_threshold\":"; json += master_extmem_malloc_enabled ? master_extmem_malloc_threshold : 0;
  json += ",\"heap_min\":"; json += ESP.getMinFreeHeap();
  json += ",\"developer_mode\":"; json += developer_mode_enabled ? "true" : "false";
  json += ",\"cpu_load_pct\":"; json += cpu_load_pct;
  json += ",\"loop_max_ms\":"; json += loop_max_ms;
  json += ",\"status_led_enabled\":"; json += status_led_enabled ? "true" : "false";
  json += ",\"status_led_brightness\":"; json += status_led_brightness_pct;
  json += ",\"master_led_ofe_event\":"; json += (uint8_t)ofe_status_leds.busEvent();
  json += ",\"master_led_evt_event\":"; json += (uint8_t)ofe_status_leds.moduleEvent();
  const BusStats& bus_stats = rs485_link.stats();
  json += ",\"bus_rx_frames\":"; json += bus_stats.rx_frames;
  json += ",\"bus_tx_frames\":"; json += bus_stats.tx_frames;
  json += ",\"bus_crc_errors\":"; json += bus_stats.crc_errors;
  json += ",\"bus_bad_length\":"; json += bus_stats.bad_length;
  json += ",\"bus_bad_version\":"; json += bus_stats.bad_version;
  json += ",\"bus_escape_errors\":"; json += bus_stats.escape_errors;
  json += ",\"bus_overflow_errors\":"; json += bus_stats.overflow_errors;
  json += ",\"bus_short_frames\":"; json += bus_stats.short_frames;
  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  json += ",\"wifi_connected\":"; json += wifi_connected ? "true" : "false";
  json += ",\"wifi_rssi\":"; json += wifi_connected ? WiFi.RSSI() : 0;
  String current_ssid = wifi_connected ? WiFi.SSID() : String("");
  json += ",\"wifi_ssid\":\""; json += json_escape(current_ssid.c_str()); json += "\"";
  json += ",\"wifi_hostname\":\""; json += master_hostname; json += "\"";
  json += ",\"master_ip\":\""; json += json_escape(current_master_ip_string().c_str()); json += "\"";
  json += ",\"mqtt_enabled\":"; json += mqtt_enabled ? "true" : "false";
  json += ",\"mqtt_tls\":"; json += mqtt_tls_enabled ? "true" : "false";
  json += ",\"mqtt_tls_verified\":"; json += mqtt_tls_verify_enabled ? "true" : "false";
  json += ",\"mqtt_tls_ca_set\":"; json += mqtt_ca_cert.length() ? "true" : "false";
  json += ",\"mqtt_state\":"; json += mqtt_last_state;
  json += ",\"mqtt_state_text\":\""; json += mqtt_state_text(mqtt_last_state); json += "\"";
  time_t now_ts = time(nullptr);
  struct tm now_tm;
  if (now_ts > 1700000000 && localtime_r(&now_ts, &now_tm)) {
    char dt[24];
    snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d", now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
    json += ",\"datetime\":\""; json += dt; json += "\"";
  } else {
    json += ",\"datetime\":\"\"";
  }
  uint8_t jbc_inputs_count = 0;
  uint8_t order[ModuleRegistry::MAX_MODULES];
  const uint8_t count = mqtt_sorted_module_indices(order, sizeof(order));
  for (uint8_t oi = 0; oi < count; ++oi) {
    const ModuleRecord& m = registry.at(order[oi]);
    if (m.online && (m.caps & CAP_JBC_ACTIVITY)) jbc_inputs_count++;
  }
  json += ",\"jbc_inputs_count\":"; json += jbc_inputs_count;
  json += ",\"modules_count\":"; json += registry.count();
  json += ",\"active_jbc_addr\":"; json += scheduler.jbcAddr();
  json += ",\"active_output_addr\":"; json += scheduler.outputAddr();
  const ModuleRecord* active_output_record = registry.find(scheduler.outputAddr());
  const uint8_t active_output_type = active_output_record ? active_output_record->type : MODULE_UNKNOWN;
  json += ",\"active_output_type\":"; json += active_output_type;
  json += ",\"preferred_output_addr\":"; json += scheduler.preferredOutputAddr();
  json += ",\"auto_output_addr\":"; json += scheduler.autoOutputCandidateAddr();
  json += ",\"main_input_source_type\":"; json += scheduler.mainInputSourceType();
  json += ",\"main_input_source_addr\":"; json += scheduler.mainInputSourceAddr();
  json += ",\"main_input_source_bit\":"; json += scheduler.mainInputSourceBit();
  json += ",\"route_jbc_output\":"; json += scheduler.jbcInputEnabled() ? "true" : "false";
  json += ",\"external_input_active\":"; json += extractor.externalInputActive() ? "true" : "false";
  json += ",\"input_rules\":[";
  for (uint8_t i = 0; i < MasterScheduler::MAX_INPUT_RULES; ++i) {
    const MasterScheduler::InputActionRule& r = scheduler.inputRule(i);
    if (i) json += ',';
    json += "{";
    json += "\"enabled\":"; json += r.enabled ? "true" : "false";
    json += ",\"source_type\":"; json += r.source_type;
    json += ",\"source_addr\":"; json += r.source_addr;
    json += ",\"source_bit\":"; json += r.source_bit;
    json += ",\"target_type\":"; json += r.target_type;
    json += ",\"target_addr\":"; json += r.target_addr;
    json += ",\"target_bit\":"; json += r.target_bit;
    json += "}";
  }
  json += "]";
  const MasterAlarmJson alarms = build_master_alarm_json();
  json += ",\"alarm_count\":"; json += alarms.count;
  json += ",\"alarm_text\":\""; json += json_escape(alarms.text.c_str()); json += "\"";
  json += ",\"alarms\":"; json += alarms.strings_json;
  json += ",\"alarm_items\":"; json += alarms.items_json;
  json += ",\"work_mask\":"; json += extractor.workMask();
  json += ",\"continuous\":"; json += extractor.continuous() ? "true" : "false";
  json += ",\"output_enabled\":"; json += extractor.outputEnabled() ? "true" : "false";
  json += ",\"output_power\":"; json += extractor.outputPower();
  if (developer_mode_enabled) {
    bool jbc_fae_output_tx = extractor.outputEnabled();
    const ModuleRecord* jbc_fae_output_rec = registry.find(scheduler.outputAddr());
    if (jbc_fae_output_rec && jbc_fae_output_rec->online && jbc_fae_output_rec->output_status_valid) {
      jbc_fae_output_tx = jbc_fae_output_rec->output_enabled;
    }
    json += ",\"jbc_fae_output_tx\":"; json += jbc_fae_output_tx ? "true" : "false";
  }
  json += ",\"afterrun_ms\":"; json += extractor.afterrunLeftMs();
  json += ",\"afterrun_power_enabled\":"; json += scheduler.afterrunPowerProfileEnabled() ? "true" : "false";
  json += ",\"afterrun_power\":"; json += scheduler.afterrunPower();
  json += ",\"afterrun_power_min\":"; json += scheduler.activeOutputMinSelectFlow();
  json += ",\"output_module\":{";
  json += "\"valid\":"; json += os.valid ? "true" : "false";
  json += ",\"enabled\":"; json += os.enabled ? "true" : "false";
  json += ",\"power\":"; json += os.power;
  json += ",\"rpm\":"; json += os.rpm;
  json += ",\"fault_mask\":"; json += os.fault_mask;
  json += ",\"fault_text\":\""; json += json_escape(output_fault_text_for_module(os.fault_mask, active_output_type).c_str()); json += "\"";
  json += ",\"age_ms\":"; json += os.valid ? (uint32_t)(millis() - os.last_update_ms) : 0;
  json += "}";
  json += ",\"jbc\":{";
  json += "\"valid\":"; json += js.valid ? "true" : "false";
  json += ",\"connected\":"; json += (js.link_flags & FAST_FLAG_CONNECTED) ? "true" : "false";
  json += ",\"jbc_addr\":"; json += js.jbc_addr;
  json += ",\"station_addr\":"; json += js.station_addr;
  json += ",\"station_type\":\""; json += station_type_name(js.station_addr); json += "\"";
  json += ",\"base_state\":"; json += js.base_state;
  json += ",\"suction_level\":"; json += cs.suction_level;
  json += ",\"select_flow\":"; json += cs.select_flow;
  json += ",\"actual_flow\":"; json += js.actual_flow;
  json += ",\"speed_rpm\":"; json += js.speed_rpm;
  json += ",\"delay_work_sec\":"; json += cs.delay_work_sec;
  json += ",\"delay_stand_sec\":"; json += cs.delay_stand_sec;
  json += ",\"stand_intakes\":"; json += cs.stand_intakes;
  json += ",\"continuous\":"; json += cs.continuous ? "true" : "false";
  json += ",\"filter_life\":"; json += system_jbc_filter_life;
  json += ",\"filter_sat\":"; json += system_jbc_filter_sat;
  json += ",\"stat_error\":"; json += system_jbc_error;
  json += ",\"stat_error_text\":\""; json += json_escape(jbc_error_text(system_jbc_error).c_str()); json += "\"";
  json += ",\"usb_connect\":"; json += js.usb_connect;
  json += ",\"device_id_len\":"; json += js.device_id_len;
  json += ",\"device_id\":\""; json += bytes_hex(js.device_id, js.device_id_len); json += "\"";
  json += "},\"modules\":[";
  uint8_t module_order[ModuleRegistry::MAX_MODULES];
  const uint8_t module_count = mqtt_sorted_module_indices(module_order, sizeof(module_order));
  for (uint8_t oi = 0; oi < module_count; ++oi) {
    const ModuleRecord& m = registry.at(module_order[oi]);
    if (oi) json += ',';
    json += "{";
    json += "\"addr\":"; json += m.addr;
    json += ",\"type\":"; json += m.type;
    json += ",\"type_name\":\""; json += module_type_name_for(m); json += "\"";
    json += ",\"name\":\""; json += json_escape(m.name); json += "\"";
    json += ",\"label\":\""; json += json_escape(m.label); json += "\"";
    json += ",\"display_name\":\""; json += json_escape(module_display_name(m).c_str()); json += "\"";
    json += ",\"uid\":\""; json += uid_hex(m.uid); json += "\"";
    json += ",\"fw\":\""; json += m.fw_major; json += '.'; json += m.fw_minor; json += '.'; json += m.fw_patch; if (m.fw_suffix[0]) json += m.fw_suffix; json += "\"";
    json += ",\"caps\":"; json += m.caps;
    json += ",\"online\":"; json += m.online ? "true" : "false";
    json += ",\"transport\":\""; json += master_display_wifi.active(m.addr) ? "wifi" : "rs485"; json += "\"";
    json += ",\"role_jbc\":"; json += m.role_jbc ? "true" : "false";
    json += ",\"role_output\":"; json += m.role_output ? "true" : "false";
    json += ",\"jbc_addr\":"; json += m.jbc_addr;
    json += ",\"station_addr\":"; json += m.station_addr;
    json += ",\"station_type\":\""; json += station_type_name(m.station_addr); json += "\"";
    json += ",\"jbc_link_flags\":"; json += m.jbc_link_flags;
    json += ",\"jbc_work_mask\":"; json += m.jbc_work_mask;
    json += ",\"jbc_stand_mask\":"; json += m.jbc_stand_mask;
    json += ",\"jbc_filter_life_rx\":"; json += m.jbc_filter_life;
    json += ",\"jbc_filter_sat_rx\":"; json += m.jbc_filter_sat;
    json += ",\"jbc_stat_error_rx\":"; json += m.jbc_stat_error;
    if (developer_mode_enabled && (m.type == MODULE_JBC_BUS || (m.caps & CAP_JBC_BUS))) {
      json += ",\"jbc_dbg_settings_valid\":"; json += m.jbc_settings_valid ? "true" : "false";
      json += ",\"jbc_dbg_suction_level_rx\":"; json += m.jbc_dbg_suction_level_rx;
      json += ",\"jbc_dbg_select_flow_rx\":"; json += m.jbc_dbg_select_flow_rx;
      json += ",\"jbc_dbg_delay_work_sec_rx\":"; json += m.jbc_dbg_delay_work_sec_rx;
      json += ",\"jbc_dbg_delay_stand_sec_rx\":"; json += m.jbc_dbg_delay_stand_sec_rx;
      json += ",\"jbc_dbg_stand_intakes_rx\":"; json += m.jbc_dbg_stand_intakes_rx;
      json += ",\"jbc_dbg_continuous_rx\":"; json += m.jbc_dbg_continuous_rx;
      json += ",\"jbc_dbg_actual_flow_rx\":"; json += m.jbc_actual_flow;
      json += ",\"jbc_dbg_speed_rpm_rx\":"; json += m.jbc_speed_rpm;
      json += ",\"jbc_dbg_extractor_output_valid\":"; json += m.jbc_extractor_output_valid ? "true" : "false";
      json += ",\"jbc_dbg_extractor_output_rx\":"; json += m.jbc_extractor_output_active;
    }
    json += ",\"jbc_device_id_len\":"; json += m.jbc_device_id_len;
    json += ",\"jbc_device_id\":\""; json += bytes_hex(m.jbc_device_id, m.jbc_device_id_len); json += "\"";
    // JBC-USB diagnostics are large. Never serialize them into unrelated
    // Fan/IO/Weller/Display module objects; 1.8.48 duplicated four huge JBC
    // port objects for every module and could force String reallocations on a
    // fragmented heap. Only the JBC-USB module owns these fields.
    if (m.type == MODULE_JBC_USB || (m.caps & CAP_JBC_USB)) {
    json += ",\"jbc_usb_link_state\":"; json += m.jbc_usb_link_state;
    json += ",\"jbc_usb_frame_protocol\":"; json += m.jbc_usb_frame_protocol;
    json += ",\"jbc_usb_command_protocol\":"; json += m.jbc_usb_command_protocol;
    json += ",\"jbc_usb_protocol_text\":\""; json += json_escape(m.jbc_usb_protocol_text); json += "\"";
    json += ",\"jbc_usb_model_raw\":\""; json += json_escape(m.jbc_usb_model_raw); json += "\"";
    json += ",\"jbc_usb_model\":\""; json += json_escape(m.jbc_usb_model); json += "\"";
    json += ",\"jbc_usb_model_type\":\""; json += json_escape(m.jbc_usb_model_type); json += "\"";
    json += ",\"jbc_usb_model_version\":"; json += m.jbc_usb_model_version;
    json += ",\"jbc_usb_sw_version\":\""; json += json_escape(m.jbc_usb_sw_version); json += "\"";
    json += ",\"jbc_usb_hw_version\":\""; json += json_escape(m.jbc_usb_hw_version); json += "\"";
    json += ",\"jbc_usb_station_name\":\""; json += json_escape(m.jbc_usb_station_name); json += "\"";
    const char* jbc_json_model = m.jbc_usb_model;
    const bool jbc_json_is_ha =
      strcmp(jbc_json_model, "JT") == 0 || strcmp(jbc_json_model, "JTSE") == 0;
    const bool jbc_json_is_cl =
      strcmp(jbc_json_model, "CLM") == 0 || strcmp(jbc_json_model, "CLMU") == 0;
    const bool jbc_json_is_fe =
      strcmp(jbc_json_model, "F1") == 0 || strcmp(jbc_json_model, "F2W") == 0 ||
      strcmp(jbc_json_model, "F2") == 0 || strcmp(jbc_json_model, "F4W") == 0;
    const bool jbc_json_is_sf = strcmp(jbc_json_model, "SF") == 0;
    const bool jbc_json_is_ph =
      strcmp(jbc_json_model, "PH") == 0 || strcmp(jbc_json_model, "PHBE") == 0 ||
      strcmp(jbc_json_model, "PHNE") == 0 || strcmp(jbc_json_model, "PHSE") == 0 ||
      strcmp(jbc_json_model, "PHXL") == 0;
    const bool jbc_json_is_sold =
      strcmp(jbc_json_model, "CA") == 0 || strcmp(jbc_json_model, "CDCF") == 0 ||
      strcmp(jbc_json_model, "CDN") == 0 || strcmp(jbc_json_model, "CP") == 0 ||
      strcmp(jbc_json_model, "CSCV") == 0 || strcmp(jbc_json_model, "CDE") == 0 ||
      strcmp(jbc_json_model, "CAE") == 0 || strcmp(jbc_json_model, "CPE") == 0 ||
      strcmp(jbc_json_model, "CSVE") == 0 || strcmp(jbc_json_model, "DD") == 0 ||
      strcmp(jbc_json_model, "DDE") == 0 || strcmp(jbc_json_model, "DDR") == 0 ||
      strcmp(jbc_json_model, "DI") == 0 || strcmp(jbc_json_model, "DM") == 0 ||
      strcmp(jbc_json_model, "DME") == 0 || strcmp(jbc_json_model, "HD") == 0 ||
      strcmp(jbc_json_model, "HDE") == 0 || strcmp(jbc_json_model, "HDR") == 0 ||
      strcmp(jbc_json_model, "LC") == 0 || strcmp(jbc_json_model, "NA") == 0 ||
      strcmp(jbc_json_model, "NAE") == 0 || strcmp(jbc_json_model, "PSE") == 0 ||
      strcmp(jbc_json_model, "SM") == 0 || strcmp(jbc_json_model, "WS") == 0 ||
      strcmp(jbc_json_model, "ALE") == 0;

    // Keep /state compact: SOLD and HA diagnostics are mutually exclusive.
    // Emit optional data only when its validity flag says the UI can use it.
    if (jbc_json_is_sold) {
      json += ",\"jbc_usb_qst_valid_flags\":"; json += m.jbc_usb_qst_valid_flags;
      json += ",\"jbc_usb_qst_state_flags\":"; json += m.jbc_usb_qst_state_flags;
      const uint8_t sdf = m.jbc_usb_sold_station_diag_flags;
      json += ",\"jbc_usb_sold_station_diag_flags\":"; json += sdf;
      if (sdf & 0x01U) { json += ",\"jbc_usb_sold_trafo_temp\":"; json += m.jbc_usb_sold_trafo_temp; }
      if (sdf & 0x02U) { json += ",\"jbc_usb_sold_control_mode\":"; json += m.jbc_usb_sold_control_mode ? "true" : "false"; }
      if (sdf & 0x04U) { json += ",\"jbc_usb_sold_trafo_error_temp\":"; json += m.jbc_usb_sold_trafo_error_temp; }
      if (sdf & 0x08U) { json += ",\"jbc_usb_sold_mos_error_temp\":"; json += m.jbc_usb_sold_mos_error_temp; }

      const uint16_t sef = m.jbc_usb_sold_extra_station_flags;
      json += ",\"jbc_usb_sold_extra_station_flags\":"; json += sef;
      if (sef & 0x0010U) { json += ",\"jbc_usb_sold_min_temp\":"; json += m.jbc_usb_sold_min_temp; }
      if (sef & 0x0020U) { json += ",\"jbc_usb_sold_max_temp\":"; json += m.jbc_usb_sold_max_temp; }
      if (sef & 0x0040U) {
        json += ",\"jbc_usb_sold_robot_config\":[";
        for (uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_sold_robot_config[ri];}
        json += "]";
      }
      if (sef & 0x0080U) { json += ",\"jbc_usb_sold_robot_status\":"; json += (sef & 0x0100U) ? "true" : "false"; }
      if (sef & 0x0200U) {
        json += ",\"jbc_usb_sold_peripheral_count\":"; json += m.jbc_usb_sold_peripheral_count;
        json += ",\"jbc_usb_sold_peripherals\":[";
        for (uint8_t spi=0; spi<m.jbc_usb_sold_peripheral_transmitted && spi<4; ++spi) {
          if(spi)json+=',';
          const JbcUsbSoldPeripheralState& sp=m.jbc_usb_sold_peripherals[spi];
          json += "{\"flags\":";json+=sp.flags;
          json+=",\"version\":";json+=sp.version;
          json+=",\"type\":";json+=sp.type;
          json+=",\"port\":";json+=sp.port;
          json+=",\"function\":";json+=sp.function;
          json+=",\"activation\":";json+=sp.activation;
          json+=",\"delay\":";json+=sp.delay;
          json+=",\"pd_status\":";json+=sp.pd_status;
          json+=",\"hash_mcu_uid\":\"";json+=json_escape(sp.hash_mcu_uid);json+="\"";
          json+=",\"datetime\":\"";json+=json_escape(sp.datetime);json+="\"";
          json+='}';
        }
        json += "]";
      }
      if (developer_mode_enabled && (m.jbc_usb_sold_extra_station_flags & 0x0004U)) {
        json += ",\"jbc_usb_sold_pin\":\""; json += json_escape(m.jbc_usb_sold_pin); json += "\"";
      }

      const uint32_t srf = m.jbc_usb_sold_readonly_flags;
      json += ",\"jbc_usb_sold_readonly_flags\":"; json += srf;
      if (srf & 0x00000001UL) { json += ",\"jbc_usb_sold_remote_mode\":"; json += m.jbc_usb_sold_remote_mode ? "true" : "false"; }
      if (srf & 0x00000004UL) { json += ",\"jbc_usb_sold_temp_unit\":"; json += m.jbc_usb_sold_temp_unit; }
      if (srf & 0x00000008UL) { json += ",\"jbc_usb_sold_n2_mode\":"; json += m.jbc_usb_sold_n2_mode ? "true" : "false"; }
      if (srf & 0x00000020UL) { json += ",\"jbc_usb_sold_help_text\":"; json += m.jbc_usb_sold_help_text ? "true" : "false"; }
      if (srf & 0x00000080UL) { json += ",\"jbc_usb_sold_power_limit\":"; json += m.jbc_usb_sold_power_limit; }
      if (srf & 0x00000100UL) { json += ",\"jbc_usb_sold_beep\":"; json += m.jbc_usb_sold_beep ? "true" : "false"; }
      if (srf & 0x00000400UL) {
        json += ",\"jbc_usb_sold_interface\":[";
        for(uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_sold_interface[ri];}
        json += "]";
        json += ",\"jbc_usb_sold_graph_temp_max\":"; json += m.jbc_usb_sold_graph_temp_max;
        json += ",\"jbc_usb_sold_graph_temp_min\":"; json += m.jbc_usb_sold_graph_temp_min;
        json += ",\"jbc_usb_sold_graph_temp_range\":"; json += m.jbc_usb_sold_graph_temp_range;
        json += ",\"jbc_usb_sold_graph_power_max\":"; json += m.jbc_usb_sold_graph_power_max;
        json += ",\"jbc_usb_sold_graph_power_min\":"; json += m.jbc_usb_sold_graph_power_min;
      }
      if (srf & 0x00000800UL) {
        json += ",\"jbc_usb_sold_autoclean\":"; json += m.jbc_usb_sold_autoclean ? "true" : "false";
        json += ",\"jbc_usb_sold_autoclean_temp\":"; json += m.jbc_usb_sold_autoclean_temp;
        json += ",\"jbc_usb_sold_autoclean_seconds\":"; json += m.jbc_usb_sold_autoclean_seconds;
      }
      if (srf & 0x00001000UL) { json += ",\"jbc_usb_sold_ground_type\":"; json += m.jbc_usb_sold_ground_type; }
      if (srf & 0x00002000UL) {
        json += ",\"jbc_usb_sold_datetime\":[";
        for(uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_sold_datetime[ri];}
        json += "]";
      }
      if (srf & 0x00004000UL) { json += ",\"jbc_usb_sold_frontal\":\""; json += json_escape(m.jbc_usb_sold_frontal); json += "\""; }
      if (srf & 0x00008000UL) {
        json += ",\"jbc_usb_sold_ethernet\":[";
        for(uint8_t ri=0;ri<23;++ri){if(ri)json+=',';json+=m.jbc_usb_sold_ethernet[ri];}
        json += "]";
      }
      if (srf & 0x00010000UL) {
        json += ",\"jbc_usb_sold_station_interface\":[";
        for(uint8_t ri=0;ri<4;++ri){if(ri)json+=',';json+=m.jbc_usb_sold_station_interface[ri];}
        json += "]";
      }
    }

    if (jbc_json_is_ha) {
      const uint16_t hsf = m.jbc_usb_ha_station_diag_flags;
      json += ",\"jbc_usb_ha_station_diag_flags\":"; json += hsf;
      if (hsf & 0x0001U) { json += ",\"jbc_usb_ha_remote_mode\":"; json += m.jbc_usb_ha_remote_mode ? "true" : "false"; }
      if (hsf & 0x0002U) { json += ",\"jbc_usb_ha_temp_unit\":"; json += m.jbc_usb_ha_temp_unit; }
      if (hsf & 0x0004U) {
        json += ",\"jbc_usb_ha_max_temp\":"; json += m.jbc_usb_ha_max_temp;
        json += ",\"jbc_usb_ha_min_temp\":"; json += m.jbc_usb_ha_min_temp;
      }
      if (hsf & 0x0008U) {
        json += ",\"jbc_usb_ha_max_flow\":"; json += m.jbc_usb_ha_max_flow;
        json += ",\"jbc_usb_ha_min_flow\":"; json += m.jbc_usb_ha_min_flow;
      }
      if (hsf & 0x0010U) {
        json += ",\"jbc_usb_ha_max_ext_temp\":"; json += m.jbc_usb_ha_max_ext_temp;
        json += ",\"jbc_usb_ha_min_ext_temp\":"; json += m.jbc_usb_ha_min_ext_temp;
      }
      if (hsf & 0x0020U) { json += ",\"jbc_usb_ha_selected_profile\":\""; json += json_escape(m.jbc_usb_ha_selected_profile); json += "\""; }
      if (hsf & 0x0040U) {
        json += ",\"jbc_usb_ha_robot_config\":[";
        for (uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_ha_robot_config[ri];}
        json += "]";
      }
      if (hsf & 0x0080U) { json += ",\"jbc_usb_ha_robot_status\":"; json += m.jbc_usb_ha_robot_status ? "true" : "false"; }
      const uint8_t hsec = m.jbc_usb_ha_security_flags;
      json += ",\"jbc_usb_ha_security_flags\":"; json += hsec;
      if (hsec & 0x10U) { json += ",\"jbc_usb_ha_beep\":"; json += m.jbc_usb_ha_beep ? "true" : "false"; }
      if (developer_mode_enabled && (hsec & 0x04U)) {
        json += ",\"jbc_usb_ha_pin\":\""; json += json_escape(m.jbc_usb_ha_pin); json += "\"";
      }
    }

    if (jbc_json_is_ph) {
      const uint32_t psf = m.jbc_usb_ph_station_flags;
      json += ",\"jbc_usb_ph_station_flags\":"; json += psf;
      json += ",\"jbc_usb_ph_remote_valid\":"; json += m.jbc_usb_ph_remote_valid ? "true" : "false";
      if (m.jbc_usb_ph_remote_valid) { json += ",\"jbc_usb_ph_remote_mode\":"; json += m.jbc_usb_ph_remote_mode ? "true" : "false"; }
      json += ",\"jbc_usb_ph_conti_valid\":"; json += m.jbc_usb_ph_conti_valid ? "true" : "false";
      if (m.jbc_usb_ph_conti_valid) {
        json += ",\"jbc_usb_ph_conti_speed\":"; json += m.jbc_usb_ph_conti_speed;
        json += ",\"jbc_usb_ph_conti_ports\":"; json += m.jbc_usb_ph_conti_ports;
      }
      if (psf & 0x00000001UL) {
        json += ",\"jbc_usb_ph_max_power\":"; json += m.jbc_usb_ph_max_power;
        json += ",\"jbc_usb_ph_min_power\":"; json += m.jbc_usb_ph_min_power;
      }
      if (psf & 0x00000002UL) {
        json += ",\"jbc_usb_ph_max_temp\":"; json += m.jbc_usb_ph_max_temp;
        json += ",\"jbc_usb_ph_min_temp\":"; json += m.jbc_usb_ph_min_temp;
      }
      if (psf & 0x00000040UL) { json += ",\"jbc_usb_ph_beep\":"; json += m.jbc_usb_ph_beep ? "true" : "false"; }
      if (psf & 0x00000400UL) {
        json += ",\"jbc_usb_ph_robot_config\":[";
        for (uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_ph_robot_config[ri];}
        json += "]";
      }
      if (psf & 0x00004000UL) {
        json += ",\"jbc_usb_ph_profile_points_setting\":"; json += m.jbc_usb_ph_profile_points_setting;
        json += ",\"jbc_usb_ph_profile_consignment\":"; json += m.jbc_usb_ph_profile_consignment;
        json += ",\"jbc_usb_ph_profile_tc_regulation\":"; json += m.jbc_usb_ph_profile_tc_regulation;
      }
      if (psf & 0x00008000UL) {
        json += ",\"jbc_usb_ph_profile_teach_interval\":"; json += m.jbc_usb_ph_profile_teach_interval;
      }
      if (developer_mode_enabled && (psf & 0x00000010UL)) {
        json += ",\"jbc_usb_ph_pin\":\""; json += json_escape(m.jbc_usb_ph_pin); json += "\"";
      }
      json += ",\"jbc_usb_ph_tc\":[";
      for (uint8_t ti=0; ti<4; ++ti) {
        if (ti) json += ',';
        const JbcUsbPhTcState& tc = m.jbc_usb_ph_tc[ti];
        json += "{\"flags\":"; json += tc.flags;
        if (tc.flags & 0x01U) { json += ",\"actual_temp\":"; json += tc.actual_temp; }
        if (tc.flags & 0x02U) { json += ",\"warning\":"; json += tc.warning; }
        if (tc.flags & 0x04U) { json += ",\"mode\":"; json += tc.mode; }
        if (tc.flags & 0x08U) { json += ",\"selected_temp\":"; json += tc.selected_temp; }
        json += "}";
      }
      json += "]";
      if (psf & 0x00002000UL) {
        json += ",\"jbc_usb_ph_profile_count\":"; json += m.jbc_usb_ph_profile_count;
        json += ",\"jbc_usb_ph_profile\":[";
        for (uint8_t pi=0; pi<m.jbc_usb_ph_profile_count && pi<47; ++pi) {
          if (pi) json += ',';
          json += '['; json += m.jbc_usb_ph_profile_time[pi]; json += ','; json += m.jbc_usb_ph_profile_value[pi]; json += ']';
        }
        json += "]";
      }
      if (psf & 0x00008000UL) {
        json += ",\"jbc_usb_ph_teach_count\":"; json += m.jbc_usb_ph_teach_count;
        json += ",\"jbc_usb_ph_teach\":[";
        for (uint8_t pi=0; pi<m.jbc_usb_ph_teach_count && pi<94; ++pi) { if (pi) json += ','; json += m.jbc_usb_ph_teach_value[pi]; }
        json += "]";
      }
    }
    if (jbc_json_is_fe) {
      const uint16_t fsf=m.jbc_usb_fe_station_flags;
      json += ",\"jbc_usb_fe_station_flags\":"; json += fsf;
      const uint16_t fsvc=m.jbc_usb_fe_service_flags;
      json += ",\"jbc_usb_fe_service_flags\":"; json += fsvc;
      if(fsvc&0x0001U){json += ",\"jbc_usb_fe_flow_x_mil\":";json += m.jbc_usb_fe_flow_x_mil;}
      if(fsvc&0x0002U){json += ",\"jbc_usb_fe_speed_rpm\":";json += m.jbc_usb_fe_speed_rpm;}
      if(fsvc&0x0004U){json += ",\"jbc_usb_fe_selected_flow_x_mil\":";json += m.jbc_usb_fe_selected_flow_x_mil;}
      if(fsvc&0x0008U){json += ",\"jbc_usb_fe_filter_status\":";json += m.jbc_usb_fe_filter_status;}
      if(developer_mode_enabled && (fsvc&0x0010U)){json += ",\"jbc_usb_fe_pin\":\"";json += json_escape(m.jbc_usb_fe_pin);json += "\"";}
      if(fsvc&0x0040U){json += ",\"jbc_usb_fe_beep\":";json += m.jbc_usb_fe_beep ? "true" : "false";}
      if (fsf & 0x0010U) {
        json += ",\"jbc_usb_fe_robot_config\":[";
        for(uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_fe_robot_config[ri];}
        json += "]";
      }
    }
    if (jbc_json_is_sf) {
      const uint16_t ssf=m.jbc_usb_sf_station_flags;
      json += ",\"jbc_usb_sf_station_flags\":"; json += ssf;
      json += ",\"jbc_usb_sf_conti_valid\":"; json += m.jbc_usb_sf_conti_valid ? "true" : "false";
      if(m.jbc_usb_sf_conti_valid){json += ",\"jbc_usb_sf_conti_speed\":";json += m.jbc_usb_sf_conti_speed;json += ",\"jbc_usb_sf_conti_ports\":";json += m.jbc_usb_sf_conti_ports;}
      if (ssf & 0x0040U) { json += ",\"jbc_usb_sf_length_unit\":"; json += m.jbc_usb_sf_length_unit; }
      if (ssf & 0x0200U) {
        json += ",\"jbc_usb_sf_robot_config\":[";
        for(uint8_t ri=0;ri<7;++ri){if(ri)json+=',';json+=m.jbc_usb_sf_robot_config[ri];}
        json += "]";
      }
      if (ssf & 0x1000U) {
        json += ",\"jbc_usb_sf_program_list\":[";
        for(uint8_t pi=0;pi<35;++pi){if(pi)json+=',';json+=m.jbc_usb_sf_program_list[pi];}
        json += "]";
      }
      json += ",\"jbc_usb_sf_programs\":[";
      bool sf_first=true;
      for(uint8_t pi=0;pi<35;++pi){
        const JbcUsbSfProgramState& pg=m.jbc_usb_sf_programs[pi];
        if(!(pg.flags&0x01U))continue;
        if(!sf_first)json+=',';sf_first=false;
        json += "{\"n\":";json+=(uint16_t)(pi+1);
        json += ",\"enabled\":";json+=(pg.flags&0x02U)?"true":"false";
        json += ",\"name\":\"";json+=json_escape(pg.name);json+="\"";
        json += ",\"length\":[";for(uint8_t st=0;st<3;++st){if(st)json+=',';json+=pg.length[st];}json+="]";
        json += ",\"speed\":[";for(uint8_t st=0;st<3;++st){if(st)json+=',';json+=pg.speed[st];}json+="]}";
      }
      json += "]";
      if (developer_mode_enabled && (ssf & 0x0001U)) {
        json += ",\"jbc_usb_sf_pin\":\""; json += json_escape(m.jbc_usb_sf_pin); json += "\"";
      }
    }
    json += ",\"jbc_usb_device_id_len\":"; json += m.jbc_usb_device_id_len;
    json += ",\"jbc_usb_device_id\":\""; json += bytes_hex(m.jbc_usb_device_id, m.jbc_usb_device_id_len); json += "\"";
    json += ",\"jbc_usb_port_count\":"; json += m.jbc_usb_port_count;
    json += ",\"jbc_usb_port_count_from_model\":"; json += m.jbc_usb_port_count_from_model ? "true" : "false";
    json += ",\"jbc_usb_cp_vid\":"; json += m.jbc_usb_cp_vid;
    json += ",\"jbc_usb_cp_pid\":"; json += m.jbc_usb_cp_pid;
    json += ",\"jbc_usb_usb_rx_bytes\":"; json += m.jbc_usb_usb_rx_bytes;
    json += ",\"jbc_usb_usb_tx_bytes\":"; json += m.jbc_usb_usb_tx_bytes;
    json += ",\"jbc_usb_rx_frames\":"; json += m.jbc_usb_rx_frames;
    json += ",\"jbc_usb_tx_frames\":"; json += m.jbc_usb_tx_frames;
    json += ",\"jbc_usb_usb_errors\":"; json += m.jbc_usb_usb_errors;
    json += ",\"jbc_usb_bcc_errors\":"; json += m.jbc_usb_bcc_errors;
    json += ",\"jbc_usb_frame_errors\":"; json += m.jbc_usb_frame_errors;
    json += ",\"jbc_usb_decode_errors\":"; json += m.jbc_usb_decode_errors;
    json += ",\"jbc_usb_handshake_errors\":"; json += m.jbc_usb_handshake_errors;
    json += ",\"jbc_usb_decode_last_cmd\":"; json += m.jbc_usb_decode_last_cmd;
    json += ",\"jbc_usb_decode_last_got_len\":"; json += m.jbc_usb_decode_last_got_len;
    json += ",\"jbc_usb_decode_last_expected_min\":"; json += m.jbc_usb_decode_last_expected_min;
    json += ",\"jbc_usb_decode_last_expected_max\":"; json += m.jbc_usb_decode_last_expected_max;
    json += ",\"jbc_usb_decode_top_cmd\":["; for(uint8_t rank=0;rank<3;++rank){if(rank)json+=',';json+=m.jbc_usb_decode_top_cmd[rank];} json += ']';
    json += ",\"jbc_usb_decode_top_count\":["; for(uint8_t rank=0;rank<3;++rank){if(rank)json+=',';json+=m.jbc_usb_decode_top_count[rank];} json += ']';
    json += ",\"jbc_usb_cp_baud\":"; json += m.jbc_usb_cp_baud;
    json += ",\"jbc_usb_cp_line_ctl\":"; json += m.jbc_usb_cp_line_ctl;
    json += ",\"jbc_usb_cp_mdmsts\":"; json += m.jbc_usb_cp_mdmsts;
    json += ",\"jbc_usb_cp_comm_errors\":"; json += m.jbc_usb_cp_comm_errors;
    json += ",\"jbc_usb_cp_hold_reasons\":"; json += m.jbc_usb_cp_hold_reasons;
    json += ",\"jbc_usb_cp_in_queue\":"; json += m.jbc_usb_cp_in_queue;
    json += ",\"jbc_usb_cp_out_queue\":"; json += m.jbc_usb_cp_out_queue;
    json += ",\"jbc_usb_cp_diag_valid\":"; json += m.jbc_usb_cp_diag_valid ? "true" : "false";
    json += ",\"jbc_usb_ports\":[";
    const uint8_t jbc_json_port_count = min(m.jbc_usb_port_count, (uint8_t)4);
    for (uint8_t jp = 0; jp < jbc_json_port_count; ++jp) {
      if (jp) json += ',';
      const JbcUsbPortState& ps = m.jbc_usb_ports[jp];
      json += "{\"valid\":"; json += ps.valid ? "true" : "false";
      json += ",\"tool\":"; json += ps.tool;
      json += ",\"error\":"; json += ps.error;
      json += ",\"flags\":"; json += ps.status_flags;
      json += ",\"temperature\":"; json += ps.temperature;
      json += ",\"power_permille\":"; json += ps.power_permille;
      json += ",\"time_to_sleep_hibern\":"; json += ps.time_to_sleep_hibern;
      json += ",\"time_to_stop\":"; json += ps.time_to_stop;
      json += ",\"future_mode\":"; json += ps.future_mode;

      if (jbc_json_is_cl) {
        const uint16_t cf = ps.cl_flags;
        json += ",\"cl_flags\":"; json += cf;
        if (cf & 0x0001U) { json += ",\"cl_motors_on\":"; json += ps.cl_motors_on ? "true" : "false"; }
        if (cf & 0x0002U) { json += ",\"cl_door_open\":"; json += ps.cl_door_open ? "true" : "false"; }
        if (cf & 0x0004U) {
          json += ",\"cl_counter_plug_min\":"; json += ps.cl_counter_plug_min;
          json += ",\"cl_counter_cleaning_continuous_min\":"; json += ps.cl_counter_cleaning_continuous_min;
          json += ",\"cl_counter_cleaning_detection_min\":"; json += ps.cl_counter_cleaning_detection_min;
          json += ",\"cl_counter_work_cycles\":"; json += ps.cl_counter_work_cycles;
          json += ",\"cl_counter_door_open_cycles\":"; json += ps.cl_counter_door_open_cycles;
        }
        if (cf & 0x0008U) {
          json += ",\"cl_partial_plug_min\":"; json += ps.cl_partial_plug_min;
          json += ",\"cl_partial_cleaning_continuous_min\":"; json += ps.cl_partial_cleaning_continuous_min;
          json += ",\"cl_partial_cleaning_detection_min\":"; json += ps.cl_partial_cleaning_detection_min;
          json += ",\"cl_partial_work_cycles\":"; json += ps.cl_partial_work_cycles;
          json += ",\"cl_partial_door_open_cycles\":"; json += ps.cl_partial_door_open_cycles;
        }
      }

      if (jbc_json_is_sold) {
        const uint16_t dvf = ps.detail_value_flags;
        json += ",\"detail_flags\":"; json += ps.detail_flags;
        json += ",\"detail_value_flags\":"; json += dvf;
        if (ps.delay_config_flags) {
          json += ",\"delay_config_flags\":"; json += ps.delay_config_flags;
          json += ",\"sleep_delay_min\":"; json += ps.sleep_delay_min;
          json += ",\"hiber_delay_min\":"; json += ps.hiber_delay_min;
        }
        if (dvf & 0x0001U) { json += ",\"selected_temp\":"; json += ps.selected_temp; }
        if (dvf & 0x0002U) { json += ",\"sleep_temp\":"; json += ps.sleep_temp; }
        if (dvf & 0x0004U) { json += ",\"adjust_temp\":"; json += ps.adjust_temp; }
        if (dvf & 0x0010U) {
          json += ",\"levels_on\":"; json += ps.levels_on;
          json += ",\"selected_level\":"; json += ps.selected_level;
          json += ",\"level_on\":[";
          for (uint8_t lv=0; lv<3; ++lv) { if(lv) json += ','; json += ps.level_on[lv]; }
          json += "]";
          json += ",\"level_temp\":[";
          for (uint8_t lv=0; lv<3; ++lv) { if(lv) json += ','; json += ps.level_temp[lv]; }
          json += "]";
        }
        if (dvf & 0x0008U) {
          json += ",\"counter_plug_min\":"; json += ps.counter_plug_min;
          json += ",\"counter_work_min\":"; json += ps.counter_work_min;
          json += ",\"counter_sleep_min\":"; json += ps.counter_sleep_min;
          json += ",\"counter_hiber_min\":"; json += ps.counter_hiber_min;
          json += ",\"counter_idle_min\":"; json += ps.counter_idle_min;
          if (dvf & 0x0020U) {
            json += ",\"counter_sleep_cycles\":"; json += ps.counter_sleep_cycles;
            json += ",\"counter_desold_cycles\":"; json += ps.counter_desold_cycles;
          }
        }

        // Cartridge block is only useful when a cartridge record exists.
        if (dvf & 0x03C0U) {
          json += ",\"cartridge_on\":"; json += ps.cartridge_on;
          json += ",\"cartridge_jbc_code\":"; json += ps.cartridge_jbc_code;
          json += ",\"cartridge_adjust_300\":"; json += ps.cartridge_adjust_300;
          json += ",\"cartridge_adjust_400\":"; json += ps.cartridge_adjust_400;
          json += ",\"cartridge_group\":"; json += ps.cartridge_group;
          json += ",\"cartridge_family\":"; json += ps.cartridge_family;
          json += ",\"tip_temp_a\":"; json += ps.tip_temp_a;
          json += ",\"tip_temp_b\":"; json += ps.tip_temp_b;
          json += ",\"cartridge_ma_a\":"; json += ps.cartridge_ma_a;
          json += ",\"cartridge_ma_b\":"; json += ps.cartridge_ma_b;
          json += ",\"cartridge_power_permille_a\":"; json += ps.cartridge_power_permille_a;
          json += ",\"cartridge_power_permille_b\":"; json += ps.cartridge_power_permille_b;
        }

        const uint8_t sdfp = ps.sold_diag_flags;
        json += ",\"sold_diag_flags\":"; json += sdfp;
        if (sdfp & 0x01U) { json += ",\"sold_mos_temp\":"; json += ps.sold_mos_temp; }
        if (sdfp & 0x02U) { json += ",\"sold_tool_type\":"; json += ps.sold_tool_type; }
        if (sdfp & 0x04U) { json += ",\"sold_tool_last_error\":"; json += ps.sold_tool_last_error; }
        if (sdfp & 0x08U) {
          json += ",\"sold_alarm_max_temp\":"; json += ps.sold_alarm_max_temp;
          json += ",\"sold_alarm_max_delay_tenth_sec\":"; json += ps.sold_alarm_max_delay_tenth_sec;
        }
        if (sdfp & 0x10U) {
          json += ",\"sold_alarm_min_temp\":"; json += ps.sold_alarm_min_temp;
          json += ",\"sold_alarm_min_delay_tenth_sec\":"; json += ps.sold_alarm_min_delay_tenth_sec;
        }

        const uint16_t sxf = ps.sold_extra_flags;
        json += ",\"sold_extra_flags\":"; json += sxf;
        if (sxf & 0x0001U) {
          json += ",\"sold_partial_plug_min\":"; json += ps.sold_partial_plug_min;
          json += ",\"sold_partial_work_min\":"; json += ps.sold_partial_work_min;
          json += ",\"sold_partial_sleep_min\":"; json += ps.sold_partial_sleep_min;
          json += ",\"sold_partial_hiber_min\":"; json += ps.sold_partial_hiber_min;
          json += ",\"sold_partial_idle_min\":"; json += ps.sold_partial_idle_min;
          if (sxf & 0x0002U) {
            json += ",\"sold_partial_sleep_cycles\":"; json += ps.sold_partial_sleep_cycles;
            json += ",\"sold_partial_desold_cycles\":"; json += ps.sold_partial_desold_cycles;
          }
        }
        if (sxf & 0x0004U) { json += ",\"sold_selected_profile\":\""; json += json_escape(ps.sold_selected_profile); json += "\""; }
        if (sxf & 0x0008U) { json += ",\"sold_profile_mode\":"; json += ps.sold_profile_mode; }
        if (sxf & 0x0010U) {
          json += ",\"sold_assistant_on\":"; json += ps.sold_assistant_on ? "true" : "false";
          json += ",\"sold_assistant_warning\":"; json += ps.sold_assistant_warning;
          json += ",\"sold_assistant_error\":"; json += ps.sold_assistant_error;
        }

        const uint16_t sprf = ps.sold_readonly_port_flags;
        json += ",\"sold_readonly_port_flags\":"; json += sprf;
        if (sprf & 0x0001U) {
          json += ",\"sold_fixed_temp\":"; json += ps.sold_fixed_temp;
          json += ",\"sold_fixed_temp_on\":"; json += ps.sold_fixed_temp_on ? "true" : "false";
        }
        if (sprf & 0x0002U) { json += ",\"sold_assistant_warning_code\":"; json += ps.sold_assistant_warning_code; }
        if (sprf & 0x0004U) {
          json += ",\"sold_result_similarity\":"; json += ps.sold_result_similarity;
          json += ",\"sold_result_tenths\":"; json += ps.sold_result_tenths;
          json += ",\"sold_result_energy\":"; json += ps.sold_result_energy;
        }
        if (sprf & 0x0008U) { json += ",\"sold_direct_power_permille\":"; json += ps.sold_direct_power_permille; }

        const uint16_t sff = ps.sold_feeder_flags;
        json += ",\"sold_feeder_flags\":"; json += sff;
        if (sff) {
          json += ",\"sold_feeder_working_mode\":"; json += ps.sold_feeder_working_mode;
          json += ",\"sold_feeder_selected_program\":"; json += ps.sold_feeder_selected_program;
          json += ",\"sold_feeder_delivery_length\":"; json += ps.sold_feeder_delivery_length;
          json += ",\"sold_feeder_delivery_speed\":"; json += ps.sold_feeder_delivery_speed;
          json += ",\"sold_feeder_tin_diameter\":"; json += ps.sold_feeder_tin_diameter;
          json += ",\"sold_feeder_remove_length\":"; json += ps.sold_feeder_remove_length;
          json += ",\"sold_feeder_speed_length_readonly\":"; json += ps.sold_feeder_speed_length_readonly ? "true" : "false";
          json += ",\"sold_feeder_selectable_programs\":"; json += ps.sold_feeder_selectable_programs;
          json += ",\"sold_feeder_clogging_detection\":"; json += ps.sold_feeder_clogging_detection ? "true" : "false";
          json += ",\"sold_feeder_motor_on\":"; json += ps.sold_feeder_motor_on ? "true" : "false";
          json += ",\"sold_feeder_motor_direction\":"; json += ps.sold_feeder_motor_direction;
          json += ",\"sold_feeder_program_length\":[";
          for (uint8_t pg=0; pg<5; ++pg) {
            if (pg) json += ',';
            json += '[';
            for (uint8_t st=0; st<3; ++st) { if (st) json += ','; json += ps.sold_feeder_program_length[pg][st]; }
            json += ']';
          }
          json += ']';
          json += ",\"sold_feeder_program_speed\":[";
          for (uint8_t pg=0; pg<5; ++pg) {
            if (pg) json += ',';
            json += '[';
            for (uint8_t st=0; st<3; ++st) { if (st) json += ','; json += ps.sold_feeder_program_speed[pg][st]; }
            json += ']';
          }
          json += ']';
        }

        const uint16_t scf = ps.sold_special_counter_flags;
        json += ",\"sold_special_counter_flags\":"; json += scf;
        if (scf & 0x0001U) {
          json += ",\"sold_tin_deliver_cycles\":"; json += ps.sold_tin_deliver_cycles;
          json += ",\"sold_tin_length\":"; json += ps.sold_tin_length;
        }
        if (scf & 0x0002U) {
          json += ",\"sold_partial_tin_deliver_cycles\":"; json += ps.sold_partial_tin_deliver_cycles;
          json += ",\"sold_partial_tin_length\":"; json += ps.sold_partial_tin_length;
        }
        if (scf & 0x0004U) {
          json += ",\"sold_cde_sold_number\":"; json += ps.sold_cde_sold_number;
          json += ",\"sold_cde_energy_delivered\":"; json += ps.sold_cde_energy_delivered;
          json += ",\"sold_cde_sold_total\":"; json += ps.sold_cde_sold_total;
          json += ",\"sold_cde_sold_per_min\":"; json += ps.sold_cde_sold_per_min;
          json += ",\"sold_cde_sold_ok\":"; json += ps.sold_cde_sold_ok;
        }
        if (scf & 0x0008U) {
          json += ",\"sold_cde_partial_sold_number\":"; json += ps.sold_cde_partial_sold_number;
          json += ",\"sold_cde_partial_energy_delivered\":"; json += ps.sold_cde_partial_energy_delivered;
          json += ",\"sold_cde_partial_sold_total\":"; json += ps.sold_cde_partial_sold_total;
          json += ",\"sold_cde_partial_sold_per_min\":"; json += ps.sold_cde_partial_sold_per_min;
          json += ",\"sold_cde_partial_sold_ok\":"; json += ps.sold_cde_partial_sold_ok;
        }
      }

      if (jbc_json_is_ha) {
        const uint16_t hvf = ps.ha_value_flags;
        const uint16_t hdf = ps.ha_diag_flags;
        json += ",\"ha_value_flags\":"; json += hvf;
        if (hvf & 0x0001U) { json += ",\"protection_temp\":"; json += ps.protection_temp; }
        if (hvf & 0x0002U) { json += ",\"selected_temp\":"; json += ps.selected_temp; }
        if (hvf & 0x0004U) { json += ",\"selected_flow_permille\":"; json += ps.selected_flow_permille; }
        if (hvf & 0x0008U) { json += ",\"selected_ext_temp\":"; json += ps.selected_ext_temp; }
        if (hvf & 0x0010U) { json += ",\"actual_ext_temp\":"; json += ps.actual_ext_temp; }
        if (hvf & 0x0020U) { json += ",\"ha_adjust_temp\":"; json += ps.ha_adjust_temp; }
        if (hvf & 0x0040U) { json += ",\"configured_time_to_stop\":"; json += ps.configured_time_to_stop; }
        if (hvf & 0x0080U) { json += ",\"external_tc_mode\":"; json += ps.external_tc_mode; }
        if (hvf & 0x0100U) { json += ",\"start_mode\":"; json += ps.start_mode; }
        if (hvf & 0x0200U) { json += ",\"profile_mode\":"; json += ps.profile_mode; }
        if (hvf & 0x0400U) {
          json += ",\"levels_on\":"; json += ps.levels_on;
          json += ",\"selected_level\":"; json += ps.selected_level;
          json += ",\"level_on\":[";
          for (uint8_t lv=0; lv<3; ++lv) { if(lv) json += ','; json += ps.level_on[lv]; }
          json += "]";
          json += ",\"level_temp\":[";
          for (uint8_t lv=0; lv<3; ++lv) { if(lv) json += ','; json += ps.level_temp[lv]; }
          json += "]";
          json += ",\"level_flow_permille\":[";
          for (uint8_t lv=0; lv<3; ++lv) { if(lv) json += ','; json += ps.level_flow_permille[lv]; }
          json += "]";
          json += ",\"level_ext_temp\":[";
          for (uint8_t lv=0; lv<3; ++lv) { if(lv) json += ','; json += ps.level_ext_temp[lv]; }
          json += "]";
        }
        if (hvf & 0x0800U) {
          json += ",\"ha_counter_plug_min\":"; json += ps.ha_counter_plug_min;
          json += ",\"ha_counter_work_min\":"; json += ps.ha_counter_work_min;
          json += ",\"ha_counter_work_cycles\":"; json += ps.ha_counter_work_cycles;
          json += ",\"ha_counter_suction_cycles\":"; json += ps.ha_counter_suction_cycles;
        }
        json += ",\"ha_diag_flags\":"; json += hdf;
        if (hdf & 0x0001U) { json += ",\"ha_diag_air_temp\":"; json += ps.ha_diag_air_temp; }
        if (hdf & 0x0002U) { json += ",\"ha_diag_power_permille\":"; json += ps.ha_diag_power_permille; }
        if (hdf & 0x0004U) { json += ",\"ha_diag_flow_permille\":"; json += ps.ha_diag_flow_permille; }
        if (hdf & 0x0008U) { json += ",\"ha_diag_tool\":"; json += ps.ha_diag_tool; }
        if (hdf & 0x0010U) { json += ",\"ha_diag_error\":"; json += ps.ha_diag_error; }
        if (hdf & 0x0020U) { json += ",\"ha_diag_status\":"; json += ps.ha_diag_status; }
        if (hdf & 0x0040U) {
          json += ",\"ha_partial_plug_min\":"; json += ps.ha_partial_plug_min;
          json += ",\"ha_partial_work_min\":"; json += ps.ha_partial_work_min;
          json += ",\"ha_partial_work_cycles\":"; json += ps.ha_partial_work_cycles;
          json += ",\"ha_partial_suction_cycles\":"; json += ps.ha_partial_suction_cycles;
        }
        if (hdf & 0x0080U) { json += ",\"ha_diag_heater_state\":"; json += ps.ha_diag_heater_state; }
        if (hdf & 0x0100U) { json += ",\"ha_diag_suction_state\":"; json += ps.ha_diag_suction_state; }
      }

      if (jbc_json_is_ph) {
        const uint16_t pf = ps.ph_flags;
        json += ",\"ph_status_flags\":"; json += ps.detail_flags;
        json += ",\"ph_flags\":"; json += pf;
        if (pf & 0x0001U) { json += ",\"ph_work_mode\":"; json += ps.ph_work_mode; }
        if (pf & 0x0002U) { json += ",\"ph_heater_status\":"; json += ps.ph_heater_status; }
        if (pf & 0x0004U) { json += ",\"ph_configured_time_to_stop\":"; json += ps.ph_configured_time_to_stop; }
        if (pf & 0x0008U) { json += ",\"ph_selected_power\":"; json += ps.ph_selected_power; }
        if (pf & 0x0010U) { json += ",\"ph_active_zones\":"; json += ps.ph_active_zones; }
        if (pf & 0x0020U) { json += ",\"ph_counter_plug_min\":"; json += ps.ph_counter_plug_min; }
        if (pf & 0x0040U) {
          json += ",\"ph_counter_work_min_power\":"; json += ps.ph_counter_work_min_power;
          json += ",\"ph_counter_work_min_temp\":"; json += ps.ph_counter_work_min_temp;
          json += ",\"ph_counter_work_min_profile\":"; json += ps.ph_counter_work_min_profile;
        }
        if (pf & 0x0080U) {
          json += ",\"ph_counter_work_cycles_power\":"; json += ps.ph_counter_work_cycles_power;
          json += ",\"ph_counter_work_cycles_temp\":"; json += ps.ph_counter_work_cycles_temp;
          json += ",\"ph_counter_work_cycles_profile\":"; json += ps.ph_counter_work_cycles_profile;
        }
        if (pf & 0x0100U) { json += ",\"ph_partial_plug_min\":"; json += ps.ph_partial_plug_min; }
        if (pf & 0x0200U) {
          json += ",\"ph_partial_work_min_power\":"; json += ps.ph_partial_work_min_power;
          json += ",\"ph_partial_work_min_temp\":"; json += ps.ph_partial_work_min_temp;
          json += ",\"ph_partial_work_min_profile\":"; json += ps.ph_partial_work_min_profile;
        }
        if (pf & 0x0400U) {
          json += ",\"ph_partial_work_cycles_power\":"; json += ps.ph_partial_work_cycles_power;
          json += ",\"ph_partial_work_cycles_temp\":"; json += ps.ph_partial_work_cycles_temp;
          json += ",\"ph_partial_work_cycles_profile\":"; json += ps.ph_partial_work_cycles_profile;
        }
      }
      if (jbc_json_is_fe) {
        const uint16_t ff = ps.fe_flags;
        json += ",\"fe_flags\":"; json += ff;
        const uint16_t fsvc=ps.fe_service_flags; json += ",\"fe_service_flags\":"; json += fsvc;
        if(fsvc&0x0001U){json += ",\"fe_stand_intakes\":";json += ps.fe_stand_intakes;}
        if(fsvc&0x0002U){json += ",\"fe_suction_delay_work\":";json += ps.fe_suction_delay_work;}
        if(fsvc&0x0004U){json += ",\"fe_suction_delay_stand\":";json += ps.fe_suction_delay_stand;}
        if(fsvc&0x0008U){json += ",\"fe_pedal_connected\":";json += ps.fe_pedal_connected ? "true" : "false";}
        if (ff & 0x0001U) { json += ",\"fe_intake_work\":"; json += ((ff & 0x0002U) ? "true" : "false"); }
        if (ff & 0x0004U) { json += ",\"fe_intake_stand\":"; json += ((ff & 0x0008U) ? "true" : "false"); }
        if (ff & 0x0010U) { json += ",\"fe_time_to_stop_work\":"; json += ps.fe_time_to_stop_work; }
        if (ff & 0x0020U) { json += ",\"fe_time_to_stop_stand\":"; json += ps.fe_time_to_stop_stand; }
        if (ff & 0x0040U) { json += ",\"fe_pedal_action\":"; json += ps.fe_pedal_action; }
        if (ff & 0x0080U) { json += ",\"fe_pedal_mode\":"; json += ps.fe_pedal_mode; }
        if (ff & 0x0100U) {
          json += ",\"fe_counter_plug_min\":"; json += ps.fe_counter_plug_min;
          json += ",\"fe_counter_idle_min\":"; json += ps.fe_counter_idle_min;
          json += ",\"fe_counter_work_intake_min\":"; json += ps.fe_counter_work_intake_min;
          json += ",\"fe_counter_stand_intake_min\":"; json += ps.fe_counter_stand_intake_min;
          json += ",\"fe_counter_work_cycles\":"; json += ps.fe_counter_work_cycles;
        }
        if (ff & 0x0200U) {
          json += ",\"fe_partial_plug_min\":"; json += ps.fe_partial_plug_min;
          json += ",\"fe_partial_idle_min\":"; json += ps.fe_partial_idle_min;
          json += ",\"fe_partial_work_intake_min\":"; json += ps.fe_partial_work_intake_min;
          json += ",\"fe_partial_stand_intake_min\":"; json += ps.fe_partial_stand_intake_min;
          json += ",\"fe_partial_work_cycles\":"; json += ps.fe_partial_work_cycles;
        }
      }
      if (jbc_json_is_sf) {
        const uint16_t sf=ps.sf_flags;
        json += ",\"sf_flags\":";json+=sf;
        if(sf&0x0001U){json+=",\"sf_speed_tenth_mm_s\":";json+=ps.sf_speed_tenth_mm_s;}
        if(sf&0x0002U){json+=",\"sf_length_tenth_mm\":";json+=ps.sf_length_tenth_mm;}
        if(sf&0x0004U){json+=",\"sf_feeding_state\":";json+=ps.sf_feeding_state;json+=",\"sf_feeding_value_raw\":";json+=ps.sf_feeding_value_raw;json+=",\"sf_feeding_selected_program\":";json+=ps.sf_feeding_selected_program;json+=",\"sf_current_program_step\":";json+=ps.sf_current_program_step;}
        if(sf&0x0008U){json+=",\"sf_tool_enabled\":";json+=(sf&0x0010U)?"true":"false";}
        if(sf&0x0020U){json+=",\"sf_counter_tin_length\":\"";json+=u64_dec(ps.sf_counter_tin_length);json+="\"";json+=",\"sf_counter_plug_min\":";json+=ps.sf_counter_plug_min;json+=",\"sf_counter_work_min\":";json+=ps.sf_counter_work_min;json+=",\"sf_counter_idle_min\":";json+=ps.sf_counter_idle_min;json+=",\"sf_counter_work_cycles\":";json+=ps.sf_counter_work_cycles;}
        if(sf&0x0040U){json+=",\"sf_partial_tin_length\":\"";json+=u64_dec(ps.sf_partial_tin_length);json+="\"";json+=",\"sf_partial_plug_min\":";json+=ps.sf_partial_plug_min;json+=",\"sf_partial_work_min\":";json+=ps.sf_partial_work_min;json+=",\"sf_partial_idle_min\":";json+=ps.sf_partial_idle_min;json+=",\"sf_partial_work_cycles\":";json+=ps.sf_partial_work_cycles;}
      }
      json += "}";
    }
    json += "]";
    json += ",\"jbc_usb_station_error\":"; json += m.jbc_usb_station_error;
    }
    json += ",\"io_input_mask\":"; json += m.io_input_mask;
    json += ",\"io_output_mask\":"; json += m.io_output_mask;
    json += ",\"io_fault_mask\":"; json += m.io_fault_mask;
    json += ",\"io_fault_text\":\""; json += json_escape(output_fault_text_for_module(m.io_fault_mask, m.type).c_str()); json += "\"";
    json += ",\"io_main_alias\":\""; json += json_escape(m.io_main_alias); json += "\"";
    json += ",\"io_in1_alias\":\""; json += json_escape(m.io_in1_alias); json += "\"";
    json += ",\"io_in2_alias\":\""; json += json_escape(m.io_in2_alias); json += "\"";
    json += ",\"io_out1_alias\":\""; json += json_escape(m.io_out1_alias); json += "\"";
    json += ",\"io_out2_alias\":\""; json += json_escape(m.io_out2_alias); json += "\"";
    json += ",\"output_status_valid\":"; json += m.output_status_valid ? "true" : "false";
    json += ",\"module_output_enabled\":"; json += m.output_enabled ? "true" : "false";
    json += ",\"module_output_power\":"; json += m.output_power;
    json += ",\"module_output_rpm\":"; json += m.output_rpm;
    json += ",\"module_output_fault\":"; json += m.output_fault_mask;
    json += ",\"module_output_fault_text\":\""; json += json_escape(output_fault_text_for_module(m.output_fault_mask, m.type).c_str()); json += "\"";
    json += ",\"combined_fault_text\":\""; json += json_escape(output_fault_text_for_module(m.io_fault_mask | m.output_fault_mask, m.type).c_str()); json += "\"";
    json += ",\"route_in1_output\":"; json += m.route_in1_output ? "true" : "false";
    json += ",\"route_in2_output\":"; json += m.route_in2_output ? "true" : "false";
    json += ",\"weller_speed_percent\":"; json += m.weller_speed_percent;
    json += ",\"weller_filter_status\":"; json += m.weller_filter_status;
    json += ",\"weller_filter_runtime_minutes\":"; json += m.weller_filter_runtime_minutes;
    json += ",\"weller_programmed_filter_minutes\":"; json += m.weller_programmed_filter_minutes;
    json += ",\"weller_fan_rpm\":"; json += m.weller_fan_rpm;
    json += ",\"weller_version\":"; json += m.weller_version;
    json += ",\"weller_work_light\":"; json += m.weller_work_light;
    json += ",\"weller_uart_age_sec\":"; json += m.weller_uart_age_sec;
    json += ",\"fanio_filter_saturation_permille\":"; json += m.fanio_filter_saturation_permille;
    json += ",\"fanio_filter_pressure_raw\":"; json += m.fanio_filter_pressure_raw;
    json += ",\"fanio_filter_zero_raw\":"; json += m.fanio_filter_zero_raw;
    json += ",\"fanio_filter_clean_raw\":"; json += m.fanio_filter_clean_raw;
    json += ",\"fanio_filter_warn_raw\":"; json += m.fanio_filter_warn_raw;
    json += ",\"fanio_filter_full_raw\":"; json += m.fanio_filter_full_raw;
    json += ",\"fanio_filter_flags\":"; json += m.fanio_filter_flags;
    json += ",\"fanio_filter_cal_quality\":"; json += m.fanio_filter_cal_quality;
    json += ",\"telemetry_valid\":"; json += m.telemetry_valid ? "true" : "false";
    json += ",\"module_heap_free\":"; json += m.module_heap_free;
    json += ",\"module_heap_min\":"; json += m.module_heap_min;
    json += ",\"module_uptime_s\":"; json += m.module_uptime_s;
    json += ",\"module_cpu_load_pct\":"; json += m.module_cpu_load_pct;
    json += ",\"module_loop_max_ms\":"; json += m.module_loop_max_ms;
    const bool module_ota_target = scheduler.moduleFirmwareUpdateActive() && scheduler.moduleFirmwareUpdateTarget() == m.addr;
    json += ",\"led_status_valid\":"; json += (m.led_status_valid || module_ota_target) ? "true" : "false";
    json += ",\"led_ofe_event\":"; json += m.led_ofe_event;
    json += ",\"led_evt_event\":"; json += module_ota_target ? (uint8_t)OFE_LED_EVENT_FW_UPDATE : m.led_evt_event;
    json += ",\"module_fw_update_active\":"; json += module_ota_target ? "true" : "false";
    json += ",\"display_view_mode\":"; json += m.display_view_mode;
    json += ",\"display_view_arg\":"; json += m.display_view_arg;
    json += ",\"display_brightness_pct\":"; json += m.display_brightness_pct;
    json += ",\"display_language\":"; json += m.display_language;
    json += ",\"display_theme\":"; json += m.display_theme;
    json += ",\"display_screensaver_min\":"; json += m.display_screensaver_min;
    if (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) {
      const bool is_modbus = m.type == MODULE_MODBUS_RTU;
      json += ",\"universal_profile\":\""; json += json_escape(universal_profile_value(m.addr, "prof", is_modbus ? "Generic Modbus RTU" : "Generic RS232").c_str()); json += "\"";
      json += ",\"universal_station\":\""; json += json_escape(universal_profile_value(m.addr, "stat", is_modbus ? "Modbus device" : "Community device").c_str()); json += "\"";
      json += ",\"universal_baud\":\""; json += json_escape(universal_profile_value(m.addr, "baud", "9600").c_str()); json += "\"";
      json += ",\"universal_frame\":\""; json += json_escape(universal_profile_value(m.addr, "frm", "8N1").c_str()); json += "\"";
      json += ",\"universal_protocol\":\""; json += json_escape(universal_profile_value(m.addr, "proto", is_modbus ? "MODBUS_RTU" : "ASCII").c_str()); json += "\"";
      json += ",\"universal_checksum\":\""; json += json_escape(universal_profile_value(m.addr, "csum", is_modbus ? "CRC16_MODBUS_LE" : "NONE").c_str()); json += "\"";
      json += ",\"universal_line_end\":\""; json += json_escape(universal_profile_value(m.addr, "lend", is_modbus ? "NONE" : "CR").c_str()); json += "\"";
      json += ",\"universal_descriptor_valid\":"; json += m.universal_descriptor_valid ? "true" : "false";
      json += ",\"universal_descriptor_crc\":"; json += m.universal_descriptor_crc;
      json += ",\"universal_descriptor_chunks\":"; json += m.universal_descriptor_chunks;
      json += ",\"universal_descriptor_age_ms\":"; json += m.universal_descriptor_valid ? (uint32_t)(millis() - m.universal_descriptor_last_ms) : 0;
      if (include_universal_descriptor) { json += ",\"universal_descriptor\":\""; json += json_escape(m.universal_descriptor); json += "\""; }
      json += ",\"universal_entities_valid\":"; json += m.universal_entities_valid ? "true" : "false";
      json += ",\"universal_entities_age_ms\":"; json += m.universal_entities_valid ? (uint32_t)(millis() - m.universal_entities_last_ms) : 0;
      json += ",\"universal_entities\":[";
      for (uint8_t ue = 0; ue < m.universal_entity_count; ++ue) {
        const UniversalEntityState& e = m.universal_entities[ue];
        if (ue) json += ',';
        json += "{\"id\":"; json += e.id;
        json += ",\"len\":"; json += e.len;
        json += ",\"age_ms\":"; json += e.age_ms;
        json += ",\"hex\":\""; json += bytes_hex(e.data, e.len); json += "\"";
        json += ",\"ascii\":\""; json += json_escape(bytes_ascii(e.data, e.len).c_str()); json += "\"";
        if (e.len == 1) { const bool vb = e.data[0] != 0 && e.data[0] != '0'; json += ",\"value_bool\":"; json += vb ? "true" : "false"; json += ",\"value\":"; json += vb ? 1 : 0; }
        else if (e.len == 2) { json += ",\"value\":"; json += get_u16_le(e.data); }
        else if (e.len == 4) { json += ",\"value\":"; json += get_u32_le(e.data); }
        json += "}";
      }
      json += "]";
      append_universal_entity_defs_json(json, m);
    }
    json += ",\"last_seen_ms\":"; json += m.last_seen_ms;
    json += ",\"timeouts\":"; json += m.timeout_count;
    json += ",\"offline_events\":"; json += m.timeout_count;
    json += ",\"misses\":"; json += m.consecutive_timeouts;
    json += ",\"miss_total\":"; json += m.miss_count;
    json += ",\"last_timeout_ms\":"; json += m.last_timeout_ms;
    json += ",\"last_timeout_cmd\":"; json += m.last_timeout_cmd;
    json += ",\"comm_quality\":"; json += module_comm_quality_code(m);
    json += ",\"comm_quality_text\":\""; json += module_comm_quality_text(m); json += "\"";
    json += "}";
  }
  json += "]";
  if (include_heap_diag) {
    heap_diag_sample("state_json");
    heap_diag_refresh_snapshot();
    const HeapDiagSnapshot& hd = heap_diag_current_snapshot;
    json += ",\"heap_diag\":{";
    json += "\"active\":"; json += hd.active ? "true" : "false";
    json += ",\"session_ms\":"; json += hd.session_ms;
    json += ",\"samples\":"; json += hd.samples;
    json += ",\"free_now\":"; json += hd.free_now;
    json += ",\"internal_now\":"; json += hd.internal_now;
    json += ",\"largest_now\":"; json += hd.largest_now;
    json += ",\"psram_now\":"; json += hd.psram_now;
    json += ",\"low_free\":"; json += hd.low_free;
    json += ",\"low_internal\":"; json += hd.low_internal;
    json += ",\"low_largest\":"; json += hd.low_largest;
    json += ",\"low_psram\":"; json += hd.low_psram;
    json += ",\"low_at_ms\":"; json += hd.low_at_ms;
    json += ",\"low_core\":"; json += (int)hd.low_core;
    json += ",\"low_label\":\""; json += json_escape(hd.low_label); json += "\"";
    json += ",\"low_task\":\""; json += json_escape(hd.low_task); json += "\"";
    json += ",\"low_context\":\""; json += json_escape(hd.low_context); json += "\"";
    json += ",\"block_free\":"; json += hd.block_free;
    json += ",\"block_internal\":"; json += hd.block_internal;
    json += ",\"block_largest\":"; json += hd.block_largest;
    json += ",\"block_psram\":"; json += hd.block_psram;
    json += ",\"block_at_ms\":"; json += hd.block_at_ms;
    json += ",\"block_core\":"; json += (int)hd.block_core;
    json += ",\"block_label\":\""; json += json_escape(hd.block_label); json += "\"";
    json += ",\"block_task\":\""; json += json_escape(hd.block_task); json += "\"";
    json += ",\"block_context\":\""; json += json_escape(hd.block_context); json += "\"}";
  }
  json += "}";
  return json;
}
static String normalized_hostname(String value) {
  value.trim();
  value.toLowerCase();
  String out;
  out.reserve(31);
  bool last_dash = false;
  for (size_t i = 0; i < value.length() && out.length() < 31; ++i) {
    char c = value[i];
    const bool alpha_num = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alpha_num) {
      out += c;
      last_dash = false;
    } else if ((c == '-' || c == '_' || c == ' ') && out.length() && !last_dash) {
      out += '-';
      last_dash = true;
    }
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

static bool parse_ipv4(const String& text, IPAddress& value) {
  String clean = text;
  clean.trim();
  return clean.length() && value.fromString(clean);
}

static void build_master_hostname() {
  const uint64_t efuse = ESP.getEfuseMac();
  const uint32_t id = (uint32_t)efuse;
  snprintf(master_device_id, sizeof(master_device_id), "open-fume-extractor-%08lx", id);
  snprintf(master_hostname, sizeof(master_hostname), "%s", master_device_id);
  snprintf(master_ap_ssid, sizeof(master_ap_ssid), "OpenFumeExtractor-%08lX", (unsigned long)id);
}


static void apply_netif_hostname(const char* ifkey) {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(ifkey);
  if (netif) esp_netif_set_hostname(netif, master_hostname);
}

static void apply_sta_hostname() {
  WiFi.setHostname(master_hostname);
  apply_netif_hostname("WIFI_STA_DEF");
}

static void apply_ap_hostname() {
  WiFi.softAPsetHostname(master_hostname);
  apply_netif_hostname("WIFI_AP_DEF");
}

static void start_mdns_service() {
  MDNS.end();
  if (MDNS.begin(master_hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS http://");
    Serial.print(master_hostname);
    Serial.println(".local/");
  } else {
    Serial.println("mDNS start failed");
  }
}

static void web_handle_root() {
  web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  web.sendHeader("Pragma", "no-cache");
  web.sendHeader("Expires", "0");
  static const char html1[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Open Fume Extractor</title>
<link rel="icon" type="image/png" href="/favicon.png"><link rel="apple-touch-icon" href="/logo.png">
<style>
:root{--bg:#0c0d0f;--card:#181b20;--card2:#20242a;--line:#303640;--text:#f3f5f7;--muted:#9ba5b1;--blue:#4a90d9;--green:#59c77a;--red:#a94646}*{box-sizing:border-box}
body{margin:0;font-family:Arial,Helvetica,sans-serif;background:var(--bg);color:var(--text)}
header{padding:12px 18px;min-height:66px;background:#17191d;border-bottom:1px solid #2a2e35;display:flex;align-items:center;justify-content:space-between;gap:16px;position:sticky;top:0;z-index:5}
.brand{display:flex;align-items:center;gap:10px;font-size:18px;font-weight:700}.brand-dev{min-height:28px;padding:4px 9px;font-size:11px;margin-left:4px}.brand-logo{width:38px;height:38px;border-radius:9px;object-fit:cover;box-shadow:0 0 0 1px #303640}.brand-text{line-height:1.05}.brand span{display:block;color:#929ba6;font-size:12px;font-weight:400;margin-top:4px}
main{width:min(1100px,100%);margin:auto;padding:24px 18px 40px}.page-head{margin-bottom:18px}.eyebrow{color:var(--green);font-size:12px;font-weight:700;text-transform:uppercase}.page-head h1{font-size:25px;margin:5px 0}.page-head p{color:var(--muted);line-height:1.45;margin:0;max-width:720px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin-bottom:14px}
.tile{position:relative;overflow:hidden;border:1px solid var(--line);background:var(--card);border-radius:8px;padding:14px;box-shadow:0 1px 0 rgba(255,255,255,.03) inset}.grid>.tile:not(.row){background:var(--card2);border-color:#343b45}.grid>.tile:not(.row)::before{content:"";position:absolute;left:0;right:0;top:0;height:3px;background:#4a90d9}.grid>.tile:nth-child(4n+2)::before{background:#59c77a}.grid>.tile:nth-child(4n+3)::before{background:#d3a84d}.grid>.tile:nth-child(4n+4)::before{background:#57b8c8}.k{color:var(--muted);font-size:12px}.v{font-size:24px;margin-top:5px}.mini{font-size:16px;line-height:1.35}#station{font-size:18px;line-height:1.25}
.on{color:#79d279}.off{color:#999}.warn{color:#f4c25b}table{width:100%;min-width:1320px;table-layout:auto;border-collapse:collapse;background:#181a1d;border:1px solid #333;border-radius:6px;overflow:hidden}
th,td{text-align:left;padding:9px 11px;border-bottom:1px solid #2d3034;font-size:14px;white-space:nowrap;vertical-align:middle}th{color:#bbb;background:#202329}tr:last-child td{border-bottom:0}td:nth-child(2),td:nth-child(4){white-space:normal;min-width:180px;line-height:1.35}td:nth-child(6){font-family:Consolas,monospace;min-width:150px}.label-edit{min-width:190px}
th:nth-child(1){min-width:64px}th:nth-child(3){min-width:100px}th:nth-child(5){min-width:70px}th:nth-child(7){min-width:82px}th:nth-child(8){min-width:88px}th:nth-child(9){min-width:150px}th:nth-child(10){min-width:90px}th:nth-child(11){min-width:118px}
label{display:block;color:var(--muted);font-size:12px;margin:12px 0 6px}input,select{width:100%;min-height:42px;background:#101216;color:var(--text);border:1px solid #414854;border-radius:7px;padding:10px 11px;font:inherit;outline:none}input:focus,select:focus{border-color:var(--blue);box-shadow:0 0 0 3px rgba(74,144,217,.16)}input.is-dirty,select.is-dirty,textarea.is-dirty{border-color:#d3a84d;box-shadow:0 0 0 2px rgba(211,168,77,.14)}.label-edit{padding:8px;font-size:13px;min-height:36px}
.bar{height:8px;background:#2b3037;border-radius:99px;overflow:hidden}.fill{height:100%;background:linear-gradient(90deg,#58d37b,#93e6a8);width:0}.muted{color:#98a1ad}.row{margin-top:14px}
.wifi-meter{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;margin-top:8px}.wifi-bar{height:10px;background:#2b3037;border-radius:99px;overflow:hidden}.wifi-fill{height:100%;width:0;background:linear-gradient(90deg,#d85151,#f4c25b,#58d37b);border-radius:99px;transition:width .2s}.wifi-db{font-size:13px;color:#aab2bd;min-width:54px;text-align:right}.ssid{font-size:12px;color:#98a1ad;margin-top:6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.module-card{--accent:#4a90d9;min-height:250px;padding:0!important;border-color:#343b45!important;background:#15181d!important;box-shadow:0 10px 26px rgba(0,0,0,.18)!important}.module-card::before{display:none}.module-card>.module-head{height:136px;min-height:136px;max-height:136px;padding:12px 16px;border-bottom:1px solid #2b3139;background:#1c2026;box-shadow:inset 4px 0 0 var(--accent);display:flex;flex-direction:column;align-items:stretch;justify-content:flex-start;gap:9px}.module-card .module-title{width:100%;font-size:15px;font-weight:800;color:#eef3f8;white-space:normal;overflow:visible;text-overflow:clip;line-height:1.18;overflow-wrap:anywhere}.module-card .module-head-main{display:flex;align-items:center;justify-content:space-between;gap:10px;min-width:0;flex:1 1 auto}.module-card .module-type{margin-top:0;color:#aeb8c3;font-size:12px;font-weight:700;line-height:1.2;white-space:normal;overflow:visible}.module-card .module-address{margin-top:5px;color:#718090;font:600 11px/1.15 Consolas,monospace;white-space:nowrap}.module-card .module-identity{display:flex;align-items:center;gap:11px;min-width:0;flex:1 1 auto}.module-card .module-head-copy{min-width:0;flex:1 1 auto}.module-card .module-icon{width:42px;min-width:42px;height:42px;flex:0 0 42px;border-radius:9px;display:inline-flex;align-items:center;justify-content:center;background:#252b33;color:var(--accent);font-size:17px;font-weight:800}.module-card .module-icon .module-head-glyph{width:24px;height:24px;display:block;color:currentColor}.module-card>.metric-line,.module-card>.io-grid,.module-card>.devid,.module-card>input[type=range],.module-card>select,.module-card>button{margin-left:14px;margin-right:14px}.module-card>.metric-line{margin-top:14px}.module-card .metric-line>div{background:#101318;border-color:#282f38;padding:10px}.module-card .metric-line .v{font-size:18px;font-weight:700}.module-card .devid{background:#101318;border-color:#282f38}.module-card.jbc-card{--accent:#39c779}.module-card.fan-card{--accent:#4a90d9}.module-card.weller-card{--accent:#55b9ca}.module-card.display-card{--accent:#b58cff}.module-card.universal-card{--accent:#f1b84b}.module-card.universal-card.modbus-card{--accent:#e28b52}.module-card .module-meta-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;padding:14px}.module-card .module-meta-grid>div{background:#101318;border:1px solid #282f38;border-radius:7px;padding:10px}.module-card .module-meta-grid .v{font-size:16px;font-weight:700;margin-top:4px}.module-card .module-footer{margin:12px 0 0!important;padding:11px 14px;border-top:1px solid #282f38;color:#8e99a5;font-size:12px}.module-card .display-settings{padding:0 14px 14px}.module-card .display-settings label{margin-top:10px}.module-card .display-settings>.metric-line{margin-top:4px}.module-card .display-settings>.metric-line>div{padding:0;background:transparent;border:0}.module-card .display-settings button{width:100%;margin-top:10px}
#details{grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px;align-items:start}
#details .module-card{width:100%;min-width:0;min-height:0;border-radius:10px!important}
.module-card>.metric-line{margin:14px 14px 0;padding:0;gap:0;background:#101318;border:1px solid #2a313a;border-radius:9px;overflow:hidden}
.module-card>.metric-line>div{min-width:0;padding:12px;background:transparent;border:0;border-radius:0}
.module-card>.metric-line>div+div{border-left:1px solid #2a313a}
.module-card .metric-line .k{font-size:11px;color:#84909d;text-transform:uppercase}
.module-card .metric-line .v{font-size:19px;line-height:1.2;margin-top:6px}
.module-card>.devid{margin:14px 14px 0;padding:12px;border:0;border-radius:9px;background:#101318}
.module-card>.devid .mini{font:600 13px/1.45 Consolas,monospace;overflow-wrap:anywhere;word-break:normal;color:#dfe7ef}
.module-card.jbc-card>.muted,.module-card.fan-card>.muted{margin:0!important;padding:10px 14px 0;font-size:12px;line-height:1.4}
.module-card>.io-grid{margin:14px;padding:4px 0;border-top:1px solid #2a313a;border-bottom:1px solid #2a313a}
.fan-card>.io-grid{grid-template-columns:auto auto minmax(100px,1fr) auto;column-gap:10px;row-gap:0}
.fan-card>.io-grid>*{min-height:46px;display:flex;align-items:center}
.fan-card>.io-grid>span:nth-child(4n+3){font-size:12px;color:#87929e;line-height:1.25}
.fan-card>.io-grid>span:nth-child(9),.fan-card>.io-grid>span:nth-child(11){font-weight:700}
.weller-card>.io-grid{grid-template-columns:1fr auto;row-gap:0}
.weller-card>.io-grid>span{font-weight:700}
.module-card .pill{height:25px;padding:0 10px;min-width:48px}
.module-card input[type=range]{appearance:none;-webkit-appearance:none;height:24px;padding:0;background:transparent;border:0;box-shadow:none}
.module-card input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:99px;background:#303844}
.module-card input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;margin-top:-7px;border-radius:50%;background:#fff;border:4px solid var(--accent);box-shadow:0 2px 7px rgba(0,0,0,.45)}
.module-card input[type=range]::-moz-range-track{height:6px;border-radius:99px;background:#303844}
.module-card input[type=range]::-moz-range-progress{height:6px;border-radius:99px;background:var(--accent)}
.module-card input[type=range]::-moz-range-thumb{width:14px;height:14px;border-radius:50%;background:#fff;border:4px solid var(--accent)}
.weller-card>input[type=range]{margin-top:12px}
.weller-card>select,.weller-card>button{width:calc(100% - 28px)}
.module-card .module-meta-grid{margin:14px;padding:0;gap:0;background:#101318;border:1px solid #2a313a;border-radius:9px;overflow:hidden}
.module-card .module-meta-grid>div{padding:12px;background:transparent;border:0;border-radius:0}
.module-card .module-meta-grid>div:nth-child(even){border-left:1px solid #2a313a}
.module-card .module-meta-grid>div:nth-child(n+3){border-top:1px solid #2a313a}
.module-card .display-settings{padding:0 14px 14px}
.module-card .display-settings>.metric-line{gap:10px;margin-top:8px}
.module-card .display-settings>.metric-line>div{padding:0;background:transparent;border:0}
.module-card .display-settings select{background:#101318;border-color:#303844}
.module-card .module-footer{display:flex;justify-content:space-between;gap:8px;margin-top:14px!important;background:#12151a}
@media(min-width:1200px){main{width:min(1240px,100%)}}
#details{grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;align-items:stretch}
#details .module-card{display:flex;flex-direction:column;height:100%;border-radius:10px!important}
.module-card>.module-head{flex:0 0 136px}
.module-card>.metric-line{margin:0 16px;padding:16px 0;gap:20px;background:transparent;border:0;border-bottom:1px solid #2b3139;border-radius:0;overflow:visible}
.module-card>.metric-line>div{padding:0;background:transparent;border:0!important}
.module-card .metric-line .k{font-size:10px;letter-spacing:0;text-transform:uppercase}
.module-card .metric-line .v{font-size:19px;margin-top:5px}
.module-card>.devid{margin:0 16px;padding:15px 0;background:transparent;border:0;border-bottom:1px solid #2b3139;border-radius:0}
.module-card>.devid .mini{font-size:12px;overflow-wrap:anywhere}
.module-card .control-list{margin:0 16px;border-bottom:1px solid #2b3139}
.module-card .control-row{min-height:52px;display:flex;align-items:center;justify-content:space-between;gap:12px;border-bottom:1px solid #252b33}
.module-card .control-row:last-child{border-bottom:0}
.module-card .control-copy{min-width:0;display:flex;flex-direction:column;gap:3px}
.module-card .control-copy strong{font-size:13px}
.module-card .control-copy small{color:#7f8a96;font-size:10px;line-height:1.25}
.module-card .control-actions{display:flex;align-items:center;gap:9px}
.module-card .status-note{min-height:44px;margin:0 16px;display:flex;align-items:center;justify-content:space-between;gap:10px;color:#8e99a5;font-size:11px;border-bottom:1px solid #2b3139}
.module-card .status-note strong{color:#eef3f8;font-size:12px}
.module-card .status-note code{padding:3px 7px;border-radius:5px;background:#242a32;color:#b7c1cc}
.module-card .module-meta-grid{margin:0 16px;padding:8px 0;gap:0;background:transparent;border:0;border-bottom:1px solid #2b3139;border-radius:0}
.module-card .module-meta-grid>div{padding:10px 0;background:transparent;border:0!important}
.module-card .module-meta-grid>div:nth-child(even){padding-left:14px}
.module-card .display-settings{padding:2px 16px 16px}
.module-card .display-settings>.metric-line{margin:0;gap:10px}
.module-card .display-settings>.metric-line>div{padding:0;border:0}
.module-card .module-footer{margin-top:0!important;padding:12px 16px;background:#12151a;border-top:1px solid #2b3139}
.module-card .switch{width:44px;height:24px;min-height:24px;padding:0;border:0;background:#343b45;box-shadow:none}
.module-card .switch::after{width:20px;height:20px;left:2px;top:2px;box-shadow:0 1px 4px rgba(0,0,0,.45)}
.module-card .switch.on{background:#31c85a}
.module-card .switch.on::after{left:22px}
.module-card .pill{height:22px;min-width:42px;padding:0 8px;font-size:10px}
.fan-card>input[type=range],.weller-card>input[type=range]{margin:10px 16px 8px;width:calc(100% - 32px)}
.weller-card>select,.weller-card>button{margin-left:16px;margin-right:16px;width:calc(100% - 32px)}
.weller-card>select{margin-top:12px}
.weller-card>button{margin-top:8px}
@media(max-width:1050px){#details{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media(max-width:620px){#details{grid-template-columns:1fr}}
.io-grid{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;margin-top:10px}.pill{display:inline-flex;align-items:center;justify-content:center;min-width:44px;height:24px;border-radius:99px;background:#303640;color:#aeb7c3;font-size:12px;font-weight:700}.pill.on{background:#1f6f3e;color:#bff5ce}.pill.warn{background:#5a4320;color:#ffd891}.metric-line{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}.metric-line div{background:#111419;border:1px solid #2a3038;border-radius:6px;padding:8px}.metric-line .k{font-size:11px}.metric-line .v{font-size:15px;margin-top:3px}.devid{margin-top:10px;background:#111419;border:1px solid #2a3038;border-radius:6px;padding:8px;word-break:break-all}.module-head{display:flex;align-items:center;justify-content:space-between;gap:8px}.module-title{font-size:12px;color:#aab2bd}
button,.btn{min-height:40px;background:#326da5;color:#fff;border:1px solid #417fb9;border-radius:7px;padding:9px 14px;font-weight:700;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;gap:6px;cursor:pointer;transition:background .15s,transform .08s,opacity .15s}button:hover,.btn:hover{background:#3a7fbe}button:active,.btn:active{transform:translateY(1px)}button.secondary,.btn.secondary{background:#242a31;color:#dce5ef;border-color:#3a424d}button.danger{background:#8f3b3b;border-color:#a94646}button.danger:hover{background:#a94646}.nav{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.nav select{width:auto}.nav .active{background:#326da5;border-color:#417fb9;color:#fff}
.switch{position:relative;width:52px;height:30px;border-radius:999px;padding:0;min-height:30px;background:#3b424c;color:transparent;box-shadow:inset 0 0 0 1px rgba(255,255,255,.08);vertical-align:middle}.switch::after{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;border-radius:50%;box-shadow:0 2px 6px rgba(0,0,0,.35);transition:left .18s}.switch.on{background:#31d158;color:transparent}.switch.on::after{left:25px}.switch.off{background:#3b424c;color:transparent}
.work-line{display:flex;align-items:center;gap:4px}.overview-work-symbol{width:34px;height:34px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 34px;color:var(--tool-color);filter:drop-shadow(var(--tool-glow));margin-right:6px}.overview-work-symbol svg{width:34px;height:34px;display:block;fill:currentColor}
.jbc-usb-card{overflow:hidden}
.jbu-hero{margin:0 16px;padding:12px 0 11px;border-bottom:1px solid #2b3139}
.jbu-hero-top{display:flex;flex-direction:column;gap:7px}
.jbu-station-name{min-width:0;max-width:100%;font-size:13px;font-weight:800;color:#f3f7fb;line-height:1.15;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;letter-spacing:.01em}
.jbu-badges{display:flex;gap:5px;flex-wrap:wrap;justify-content:flex-start}
.jbu-chip{display:inline-flex;align-items:center;min-height:20px;padding:2px 7px;border:1px solid #35404b;border-radius:999px;background:#20262d;color:#abb7c3;font-size:9px;font-weight:750;white-space:nowrap}
.jbu-chip.proto{border-color:#315f75;background:#172c37;color:#8bdcf2}
.jbu-chip.kind{border-color:#4a424f;background:#29222c;color:#d9bbdf}
.jbu-state-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:12px 16px}
.jbu-state-card{min-width:0;min-height:58px;padding:8px 10px;border:1px solid #2b3139;border-radius:8px;background:#111419;display:flex;align-items:center;gap:8px;color:#7e8995;transition:border-color .16s,background .16s,color .16s,box-shadow .16s}
.jbu-state-card.is-on{border-color:#2f7348;background:#14241a;color:#74dc91;box-shadow:inset 0 0 0 1px rgba(81,199,112,.08)}
.jbu-state-card.is-stand{color:#7e8995}
.jbu-state-card.is-stand.is-on{border-color:#754b20;background:#281b10;color:#f4a340;box-shadow:inset 0 0 0 1px rgba(244,163,64,.08)}
.jbu-state-symbol{width:auto;min-width:42px;height:38px;display:flex;align-items:center;justify-content:flex-start;gap:5px}
.jbu-state-count{font-size:16px;font-weight:850;line-height:1;color:currentColor;min-width:1ch;text-align:left}
.jbu-work-symbol,.jbu-stand-symbol{width:38px;height:38px;display:block;color:currentColor;flex:0 0 38px}
.jbu-work-symbol svg,.jbu-stand-symbol svg{width:100%;height:100%;display:block;fill:currentColor}
.jbu-state-copy{min-width:0;display:flex;flex-direction:column;gap:2px}
.jbu-state-copy small{font-size:9px;letter-spacing:.08em;color:#73808d;font-weight:750}
.jbu-state-copy strong{font-size:15px;color:#eef3f8}
.jbu-state-card.is-on .jbu-state-copy strong{color:currentColor}
.jbu-facts{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin:0 16px 12px}
.jbu-fact{min-width:0;padding:7px 8px;border:1px solid #292f37;border-radius:6px;background:#13171c}
.jbu-fact.is-wide{grid-column:1/-1}
.jbu-fact .k{font-size:9px;color:#707c88;text-transform:uppercase;letter-spacing:.04em}
.jbu-fact .v{margin-top:3px;color:#dbe4ed;font-size:12px;font-weight:700;overflow-wrap:anywhere}
[id^="jbu_stationname_"]{display:flex;align-items:center;justify-content:space-between;gap:8px;cursor:pointer}
[id^="jbu_stationname_"]::after{content:"\270E";flex:0 0 auto;color:#58a6ff;font-size:15px;font-weight:400}
[id^="jbu_stationname_"]:hover,[id^="jbu_stationname_"]:focus-visible{color:#fff;text-decoration:underline;text-underline-offset:3px;outline:none}
.jbu-fact .mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:10px;font-weight:600;letter-spacing:.01em}
.module-card.core-style .jbu-hero{padding-top:11px;padding-bottom:10px}
.module-card.core-style .jbu-station-name{font-size:14px}
.module-card.core-style .jbu-facts{margin-top:12px}
.module-card.core-style .jbu-fact{padding:8px 9px}
.module-card.core-style .jbu-fact .v{font-size:13px}
.module-card.core-style .jbu-fact .v.big{font-size:16px}
.module-card.core-style .jbu-fact .v.mono{font-size:10px}
.module-card.core-style .core-control-title{margin:0 16px;padding:10px 0 6px;border-top:1px solid #2b3139;color:#aeb9c4;font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.05em}
.module-card.core-style .control-list{margin-top:0}
.module-card.core-style .status-note{margin-top:0}
.module-card.core-style .jbu-state-card.metric-only{color:#7e8995}
.module-card.core-style .jbu-state-card.metric-only .jbu-state-symbol{min-width:34px;width:34px}
.module-card.core-style .jbu-state-card.metric-only .core-glyph{width:30px;height:30px;display:flex;align-items:center;justify-content:center;border-radius:7px;background:#20262d;color:var(--accent);font-size:16px;font-weight:900}
.module-card.core-style .jbu-state-card.metric-only .jbu-state-copy strong{font-size:14px}
.display-card.core-style .display-settings{margin:0 16px;padding:0 0 16px;border-top:1px solid #2b3139}
.display-card.core-style .display-settings>label:first-child{margin-top:12px}
.display-card.core-style .display-settings .metric-line{margin-left:0;margin-right:0}
.display-card.core-style .display-settings select{background:#111419;border-color:#293039}.module-card.core-style .range-control{margin:10px 16px 0;padding:0 0 13px;border-bottom:1px solid #2b3139}.module-card.core-style .range-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:#8b97a4;font-size:11px;font-weight:700}.module-card.core-style .range-head strong{color:#eef5fb;font-size:13px;font-weight:850}.module-card.core-style .range-control input[type=range]{display:block;width:100%;margin:7px 0 0!important}.display-card.core-style .display-settings .range-control{margin:12px 0 0;padding-bottom:12px}
.jbu-transport{margin:0 16px 10px;padding:6px 8px;border-radius:6px;background:#101318;border:1px solid #252b32;color:#75818d;font-size:9px;line-height:1.35;display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.jbu-transport .usb-glyph{width:14px;height:14px;color:#55c9d8;flex:0 0 auto}
.jbu-transport strong{color:#aeb9c4;font-size:9px}
.jbu-port-section{margin:0 16px 12px;padding-top:10px;border-top:1px solid #2b3139}
.jbu-port-title{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:7px}
.jbu-port-title .k{font-size:11px;font-weight:800;color:#aeb9c4}
.jbu-port-title small{font-size:9px;color:#697582}
.jbu-port-list{display:grid;gap:7px}
.jbu-port-card{padding:8px 9px;border:1px solid #293039;border-radius:7px;background:#111419}
.jbu-port-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:7px}
.jbu-port-head strong{font-size:11px;color:#e5edf5}
.jbu-port-head-right{display:flex;align-items:center;gap:5px;min-width:0}
.jbu-port-state{font-size:9px;min-width:54px;height:20px}
.jbu-port-state.cooling{background:#17475d;color:#9adcf5}
.jbu-port-state.stand{background:#5a4320;color:#ffd891}
.jbu-port-state.idle{background:#303640;color:#aeb7c3}
.jbu-port-metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:5px}
.jbu-port-metric{min-width:0;padding:5px 6px;border-radius:5px;background:#171b21}
.jbu-port-metric .k{display:block;font-size:8px;color:#687582;text-transform:uppercase;letter-spacing:.04em}
.jbu-port-metric strong{display:block;margin-top:2px;font-size:11px;color:#dbe4ed;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.jbu-port-flags{display:flex;gap:4px;flex-wrap:wrap;margin-top:7px}
.jbu-flag{display:inline-flex;align-items:center;min-height:18px;padding:1px 6px;border-radius:999px;border:1px solid #313a44;background:#171c22;color:#8995a1;font-size:8px;font-weight:750;white-space:nowrap}
.jbu-flag.on{border-color:#2b6c45;background:#13251a;color:#7adf98}
.jbu-flag.cool{border-color:#2e6177;background:#132832;color:#90d7f1}
.jbu-flag.stand{border-color:#6b542b;background:#281f12;color:#f4c875}
.jbu-flag.pedal{border-color:#544a67;background:#211d29;color:#cdb8ef}
.jbu-error{display:inline-flex;align-items:center;min-height:18px;max-width:145px;padding:1px 6px;border-radius:999px;font-size:8px;font-weight:800;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;border:1px solid #31523d;background:#152219;color:#77d493}
.jbu-error.bad{border-color:#713b3b;background:#2a1717;color:#ff9c9c}
.jbu-port-future{margin-top:6px;padding:5px 7px;border-radius:5px;border:1px solid #35323b;background:#18161d;color:#aeb6c2;font-size:9px;display:flex;justify-content:space-between;gap:8px;align-items:center}
.jbu-port-future strong{color:#dfc7f0;font-size:9px}
.jbu-port-details{margin-top:6px;border-top:1px solid #2b3139;padding-top:5px}
.jbu-port-details summary{cursor:pointer;color:#83909d;font-size:9px;font-weight:700;user-select:none}
.jbu-config{margin-top:9px;padding-top:7px;border-top:1px solid #2b3139}.jbu-config>summary{display:flex;align-items:center;min-height:34px;padding:4px 2px;color:#c9d2dc;font-size:11px;font-weight:700;cursor:pointer}.jbu-config[open]>summary{color:#79dca4}.jbu-config-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px 12px;padding:9px 0 3px}.jbu-config label{display:grid;gap:5px;font-size:10px;color:#91a0af}.jbu-config input[type=number],.jbu-config select{min-width:0;width:100%;height:34px;padding:5px 9px;border:1px solid #39434e;border-radius:5px;background:#11161c;color:#eef3f7;font-size:12px}.jbu-config input[type=number]:focus,.jbu-config select:focus{border-color:#58c58a;box-shadow:0 0 0 2px rgba(88,197,138,.14);outline:0}.jbu-config-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-top:10px;padding:8px 0;border-top:1px solid #252c34;border-bottom:1px solid #252c34}.jbu-toggle-row{display:flex!important;align-items:center;justify-content:space-between;gap:9px;color:#c9d2dc!important}.jbu-toggle{position:relative;display:inline-flex;flex:0 0 38px;width:38px;height:22px;cursor:pointer}.jbu-toggle input{position:absolute;width:1px;height:1px;opacity:0}.jbu-toggle-track{display:block;width:38px;height:22px;border:1px solid #48525e;border-radius:999px;background:#2c333c;box-shadow:inset 0 1px 2px rgba(0,0,0,.35);transition:background .16s,border-color .16s}.jbu-toggle-track::after{content:"";display:block;width:16px;height:16px;margin:2px;border-radius:50%;background:#d7dce1;box-shadow:0 1px 3px rgba(0,0,0,.5);transition:transform .16s,background .16s}.jbu-toggle input:checked+.jbu-toggle-track{border-color:#58c58a;background:#26784d}.jbu-toggle input:checked+.jbu-toggle-track::after{transform:translateX(16px);background:#fff}.jbu-toggle input:focus-visible+.jbu-toggle-track{outline:2px solid #79dca4;outline-offset:2px}.jbu-level-edit{display:grid;grid-template-columns:42px minmax(0,1fr);gap:8px;align-items:end;padding:8px 0;border-bottom:1px solid #242b33;background:transparent}.jbu-level-edit.is-ha{grid-template-columns:42px repeat(3,minmax(0,1fr))}.jbu-level-toggle{display:flex!important;align-items:center;justify-content:center;height:34px}.jbu-level-toggle .jbu-toggle{transform:scale(.88)}.jbu-config-status{min-height:18px;padding-top:7px;color:#79dca4;font-size:10px;text-align:right}.jbu-config-status:empty{display:none}.jbu-config-status.is-saving{color:#e3b85b}.jbu-config-status.is-error{color:#ff7272}
.jbu-config input[type=checkbox]{appearance:none;-webkit-appearance:none;display:inline-block!important;flex:0 0 38px;width:38px!important;height:22px!important;min-height:22px!important;margin:0;border:1px solid #48525e;border-radius:999px;background:radial-gradient(circle at 11px 50%,#d7dce1 0 7px,transparent 8px),#2c333c;box-shadow:inset 0 1px 2px rgba(0,0,0,.35);cursor:pointer;transition:background .16s,border-color .16s}.jbu-config input[type=checkbox]:checked{border-color:#58c58a;background:radial-gradient(circle at calc(100% - 11px) 50%,#fff 0 7px,transparent 8px),#26784d}.jbu-config input[type=checkbox]:focus-visible{outline:2px solid #79dca4;outline-offset:2px}.jbu-config-head>label{display:flex;align-items:center;justify-content:space-between;gap:9px;min-width:0;flex:1;color:#c9d2dc}.jbu-level-edit>input[type=checkbox]{align-self:end;margin-bottom:6px;transform:scale(.88)}
.jbu-config-head{display:grid;grid-template-columns:minmax(0,1fr) 92px;align-items:center;gap:10px}.jbu-config-head>label{display:grid!important;grid-template-columns:minmax(0,1fr) 44px;align-items:center;gap:10px;min-width:0;color:#c9d2dc}.jbu-config-head>select{width:92px!important;min-width:92px!important;justify-self:end}.jbu-config input[type=checkbox]{flex:0 0 44px;width:44px!important;height:24px!important;min-height:24px!important;border:0;border-radius:999px;background:radial-gradient(circle at 12px 50%,#fff 0 9px,transparent 10px),#343b45;box-shadow:inset 0 0 0 1px rgba(255,255,255,.08);transition:background .18s}.jbu-config input[type=checkbox]:checked{border:0;background:radial-gradient(circle at 32px 50%,#fff 0 9px,transparent 10px),var(--accent,#31c85a)}.jbu-level-edit{grid-template-columns:48px minmax(0,1fr)}.jbu-level-edit.is-ha{grid-template-columns:48px repeat(3,minmax(0,1fr))}.jbu-level-edit>input[type=checkbox]{align-self:end;margin:0 0 5px 2px;transform:none}
.jbu-config-head{column-gap:14px}.jbu-config-head>label{position:relative;left:-5px;width:calc(100% + 5px);gap:7px;text-align:left;line-height:1.25}.jbu-config-head>label>input[type=checkbox]{justify-self:end}
.jbu-config-head{grid-template-columns:1fr;gap:8px}.jbu-config-head>label{position:static;left:auto;width:100%;grid-template-columns:44px minmax(0,1fr);justify-content:start;gap:10px;line-height:1.25}.jbu-config-head>label>input[type=checkbox]{justify-self:start}.jbu-config-head>select{width:100%!important;min-width:0!important;justify-self:stretch}
.jbu-port-detail-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:5px;margin-top:6px}
.jbu-port-detail-item{padding:5px 6px;border-radius:5px;background:#14181e}
.jbu-port-detail-item .k{display:block;font-size:8px;color:#687582;text-transform:uppercase}
.jbu-port-detail-item strong{display:block;margin-top:2px;font-size:10px;color:#ccd6df}
.jbu-station-section{margin:0 16px 12px;padding-top:10px;border-top:1px solid #2b3139}
.jbu-station-card{overflow:hidden;border:1px solid #2d3742;border-left:3px solid #4ca1dc;border-radius:7px;background:#111419}
.jbu-station-card.kind-ha{border-left-color:#58c79a}.jbu-station-card.kind-ph{border-left-color:#da9b4b}.jbu-station-card.kind-fe{border-left-color:#64a9e9}.jbu-station-card.kind-sf{border-left-color:#b883e8}.jbu-station-card.kind-cl{border-left-color:#61c9c5}.jbu-station-card.kind-unknown{border-left-color:#7b8793}
.jbu-station-head{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:9px 10px 8px;border-bottom:1px solid #293039}
.jbu-station-identity{display:flex;align-items:center;gap:8px;min-width:0}
.jbu-station-mark{display:grid;place-items:center;flex:0 0 30px;height:30px;border-radius:6px;border:1px solid #31546c;background:#142431;color:#7dc7f2;font-size:10px;font-weight:900}
.kind-ha .jbu-station-mark{border-color:#2f5c4b;background:#14261f;color:#80dcb4}.kind-ph .jbu-station-mark{border-color:#664b2c;background:#2b2115;color:#ffc47a}.kind-fe .jbu-station-mark{border-color:#315979;background:#152637;color:#8cc8f5}.kind-sf .jbu-station-mark{border-color:#57406e;background:#24192e;color:#d6a9f6}.kind-cl .jbu-station-mark{border-color:#315d5b;background:#142726;color:#8addd9}
.jbu-station-copy{min-width:0}.jbu-station-copy small{display:block;color:#75818d;font-size:8px;font-weight:800;text-transform:uppercase}.jbu-station-copy strong{display:block;overflow:hidden;color:#edf4fa;font-size:12px;font-weight:850;white-space:nowrap;text-overflow:ellipsis}.jbu-station-copy span{display:block;overflow:hidden;margin-top:1px;color:#8b98a5;font-size:9px;white-space:nowrap;text-overflow:ellipsis}
.jbu-station-state{flex:0 0 auto;min-width:68px;height:21px}.jbu-station-state.ok{background:#174b31;color:#9be6ba}.jbu-station-state.fault{background:#64282b;color:#ffc2c2}.jbu-station-state.offline{background:#5a4320;color:#ffd891}
.jbu-station-metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1px;background:#293039}
.jbu-station-metric{min-width:0;padding:7px 9px;background:#12161b}.jbu-station-metric .k{display:block;color:#6f7c88;font-size:8px;text-transform:uppercase}.jbu-station-metric strong{display:block;overflow:hidden;margin-top:2px;color:#d4dee7;font-size:10px;white-space:nowrap;text-overflow:ellipsis}
.jbu-station-more{padding:0 9px 8px}.jbu-station-more .jbu-port-details{margin-top:7px}
.jbu-station-title{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:8px 10px;border-bottom:1px solid #293039;color:#dce6ee;font-size:10px;font-weight:850}
.jbu-station-title span{color:#7e8b97;font-size:8px;font-weight:800;text-transform:uppercase}
.jbu-station-body{padding:2px 9px 8px}.jbu-station-body>.jbu-port-detail-grid{margin-top:7px}
.jbu-table-wrap{overflow:auto;margin-top:7px}
.jbu-table{width:100%;border-collapse:collapse;font-size:9px}
.jbu-table th,.jbu-table td{padding:4px 5px;border-bottom:1px solid #2b3139;text-align:left;vertical-align:top}
.jbu-table th{color:#83909d;font-weight:700}
.jbu-table td{color:#c8d2dc}
.jbu-cartridge-live{grid-column:1/-1;display:flex;align-items:center;justify-content:space-between;gap:10px;padding:5px 7px}
.jbu-cartridge-live .k{display:inline;font-size:8px;color:#687582;text-transform:uppercase}
.jbu-cartridge-live strong{display:inline;margin-top:0;font-size:10px;color:#ccd6df;white-space:nowrap}
.jbu-counter-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:3px 8px;margin-top:7px;font-size:9px;color:#83909d}
.jbu-counter-grid strong{color:#c8d2dc;font-size:9px}
.jbu-ha-levels{grid-template-columns:1fr}
.jbu-ha-levels>span{display:flex;align-items:center;justify-content:space-between;gap:10px}
.jbu-ha-levels .is-selected{color:#9fe870}
.jbu-ha-levels .is-selected strong{color:#b7f48f}
.jbu-station-error{grid-column:1/-1;border-color:#31523d!important;background:#142019!important}
.jbu-station-error.is-bad{border-color:#713b3b!important;background:#291717!important}
.jbu-station-error.is-bad .v{color:#ff9c9c!important}
.jbu-tech{margin:0 16px 10px;border:1px solid #272d35;border-radius:6px;background:#101318}
.jbu-tech summary{padding:6px 8px;color:#76828e;font-size:9px;cursor:pointer;user-select:none}
.jbu-tech-grid{display:grid;grid-template-columns:1fr 1fr;gap:0;border-top:1px solid #252b32}
.jbu-tech-grid>div{padding:5px 7px;border-bottom:1px solid #20262d;min-width:0}
.jbu-tech-grid>div:nth-child(odd){border-right:1px solid #20262d}
.jbu-tech-grid .k{font-size:8px;color:#616d79}
.jbu-tech-grid strong{display:block;margin-top:1px;color:#9ba7b2;font:9px/1.25 ui-monospace,SFMono-Regular,Consolas,monospace;overflow-wrap:anywhere}
@media(max-width:360px){.jbu-port-metrics{grid-template-columns:1fr}.jbu-state-grid{grid-template-columns:1fr}.jbu-facts{grid-template-columns:1fr}.jbu-fact.is-wide{grid-column:auto}}
.role-lines{display:flex;flex-direction:column;gap:3px;min-width:220px}.role-lines span{display:inline-flex;align-items:center;color:#d7e5f5;font-size:13px}.status-hero{display:grid;grid-template-columns:1fr auto;align-items:center;gap:20px;border:1px solid #31516a;background:#16212a;border-radius:8px;padding:18px;margin-bottom:14px}.hero-state{font-size:27px;font-weight:700;margin-top:4px}.hero-meta{color:var(--muted);margin-top:6px}.hero-power{text-align:right;min-width:180px}.hero-power strong{display:block;font-size:30px;color:var(--green)}.alarm-panel{margin-bottom:14px}.alarm-head{display:flex;align-items:center;justify-content:space-between;gap:10px}.alarm-title{font-size:17px;font-weight:700}.alarm-count{display:inline-flex;min-width:28px;height:24px;padding:0 8px;align-items:center;justify-content:center;border-radius:99px;background:#273039;color:#c9d3de;font-weight:700;font-size:12px}.alarm-count.active{background:#6d252b;color:#ffd9dd}.alarm-list{display:grid;gap:8px;margin-top:12px}.alarm-item{border-left:3px solid #ffb020;background:#211d16;padding:10px 12px}.alarm-item.critical{border-left-color:#ff5968;background:#25171a}.alarm-item.ok{border-left-color:#59c77a;background:#14231a}.alarm-item strong{display:block;margin-bottom:3px}.alarm-item span{color:var(--muted);font-size:13px}code{color:#ddd}.actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.inline{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:8px}.table-wrap{overflow-x:auto;overflow-y:hidden;scrollbar-gutter:stable}.footer{display:flex;justify-content:space-between;color:#77818d;font-size:13px;margin-top:18px;padding-top:14px;border-top:1px solid #252a31}
.rule{display:grid;grid-template-columns:46px 1fr 70px 1fr;gap:8px;align-items:end;margin:8px 0}.rule .muted{padding-bottom:9px}.rule select{min-width:0}
@media(max-width:700px){header{align-items:flex-start;flex-direction:column}.status-hero{grid-template-columns:1fr}.hero-power{text-align:left;min-width:0}.nav{width:100%}.nav>*{flex:1}.grid{grid-template-columns:1fr}.rule{grid-template-columns:1fr}.rule .muted{padding-bottom:0}main{padding:18px 12px}.footer{gap:8px;flex-wrap:wrap}}
.module-card .module-statuses{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:6px}.module-card .module-system{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));margin-top:auto;border-top:1px solid #2b3139;background:#13161b}.module-card .module-system span{min-width:0;padding:10px 12px;text-align:left}.module-card .module-system span+span{border-left:1px solid #2b3139}.module-card .module-system small{display:block;color:#7f8b98;font-size:10px;margin-bottom:3px}.module-card .module-system strong{display:block;color:#eef3f8;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.module-card .pill.critical{background:#922d38;color:#ffe8ea}.weller-card [id^="wfstat_"]{display:flex;align-items:center;justify-content:center;width:100%;min-width:0;min-height:38px;box-sizing:border-box;padding:5px 10px;font-size:12px;font-weight:850;line-height:1.18;text-align:center;white-space:normal}.weller-card [id^="wfstat_"].critical{box-shadow:0 0 0 1px #e25461}.module-card .module-system~.module-footer{margin-top:0!important}.module-card .switch.on{background:var(--accent)}
.led-pair{display:flex;align-items:center;justify-content:center;gap:16px;min-height:38px;padding:7px 12px;border-top:1px solid #2b3139;background:#111419}.module-card>*{order:2}.module-card>.module-head{order:0}.module-card>.led-pair{order:1;min-height:32px;padding:5px 16px;border-top:0;border-bottom:1px solid #2b3139;background:#161a20}.led-word{display:inline-block;min-width:38px;text-align:center;font-size:12px;font-weight:900;letter-spacing:.14em;color:#59616b;opacity:.58;text-shadow:none;transition:none}.led-word.is-live{opacity:1;color:#e8edf2;animation:none!important;transition:none}.led-word.fx-breath{animation:ofeLedBreath var(--led-duration,3200ms) linear infinite}.led-word.fx-whitebreath{animation:ofeLedWhiteBreath var(--led-duration,3200ms) linear infinite}.led-word.fx-greenwhite{animation:ofeLedGreenWhite var(--led-duration,3200ms) linear infinite}.led-word.fx-bluewhite{animation:ofeLedBlueWhite var(--led-duration,3200ms) linear infinite}.led-word.fx-blink{animation:ofeLedBlink var(--led-duration,1000ms) linear infinite}.led-word.fx-double{animation:ofeLedDouble var(--led-duration,900ms) linear infinite}.master-led-pair{display:flex;align-items:center;gap:16px;margin-top:5px}.master-led-pair .led-word{font-size:14px;min-width:42px}@keyframes ofeLedBreath{0%,100%{opacity:.094;text-shadow:0 0 1px var(--led-glow,#fff)}50%{opacity:1;text-shadow:0 0 6px var(--led-glow,#fff),0 0 14px var(--led-glow,#fff),0 0 22px var(--led-glow,#fff)}}@keyframes ofeLedWhiteBreath{0%,100%{opacity:.063;text-shadow:0 0 1px #fff}50%{opacity:1;text-shadow:0 0 6px #fff,0 0 14px #fff,0 0 22px #fff}}@keyframes ofeLedGreenWhite{0%,100%{opacity:1;color:#00ff00;text-shadow:0 0 5px #00ff00,0 0 12px #00ff00,0 0 18px #00ff00}50%{opacity:1;color:#ffffff;text-shadow:0 0 5px #ffffff,0 0 13px #ffffff,0 0 21px #ffffff}}@keyframes ofeLedBlueWhite{0%,100%{opacity:1;color:#0024ff;text-shadow:0 0 5px #0024ff,0 0 12px #0024ff,0 0 18px #0024ff}50%{opacity:1;color:#ffffff;text-shadow:0 0 5px #ffffff,0 0 13px #ffffff,0 0 21px #ffffff}}@keyframes ofeLedBlink{0%,49.999%{opacity:.12;color:#59616b;text-shadow:none}50%,100%{opacity:1;color:var(--led-color,#fff);text-shadow:0 0 5px var(--led-glow,#fff),0 0 11px var(--led-glow,#fff),0 0 18px var(--led-glow,#fff)}}@keyframes ofeLedDouble{0%,9.999%,20%,29.999%{opacity:1;color:var(--led-color,#fff);text-shadow:0 0 5px var(--led-glow,#fff),0 0 11px var(--led-glow,#fff),0 0 18px var(--led-glow,#fff)}10%,19.999%,30%,100%{opacity:.12;color:#59616b;text-shadow:none}}
.dev-toggle{min-height:34px;font-size:12px}.dev-only{display:none!important}body.dev-mode .dev-only{display:revert!important}body.dev-mode th.dev-only,body.dev-mode td.dev-only{display:table-cell!important}.module-card .module-system{grid-template-columns:1fr}.module-card .module-system .dev-only{display:none!important}body.dev-mode .module-card .module-system{grid-template-columns:repeat(4,minmax(0,1fr))}body.dev-mode .module-card .module-system .dev-only{display:block!important}
.weller-card .filter-actions{display:flex;align-items:center;gap:8px;margin:10px 16px 14px}.weller-card .filter-actions select{min-width:0;flex:1;width:auto}.weller-card .filter-actions button{flex:0 0 auto;min-height:42px;margin:0;padding:8px 11px;white-space:nowrap}.weller-card [id^="wfstat_"]{width:100%;margin:4px 0 0;text-align:center}.display-card .display-settings{padding-bottom:20px}.uni-overview{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:14px 16px 10px}.uni-chip{min-width:0;background:#101318;border:1px solid #282f38;border-radius:8px;padding:10px}.uni-chip span{display:block;color:#8793a1;font-size:10px;text-transform:uppercase;letter-spacing:.04em}.uni-chip strong{display:block;margin-top:5px;font-size:15px;color:#eef5fb;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.uni-chip small{display:block;margin-top:4px;color:#83909d;font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.universal-profile,.universal-debug{margin:8px 16px 12px;border:1px solid #29313b;border-radius:10px;background:#11161c;overflow:hidden}.universal-profile summary,.universal-debug summary{cursor:pointer;list-style:none;padding:11px 12px;color:#dce6ef;font-weight:800;font-size:12px}.universal-profile summary::-webkit-details-marker,.universal-debug summary::-webkit-details-marker{display:none}.universal-profile[open] summary,.universal-debug[open] summary{border-bottom:1px solid #29313b}.universal-profile-body{padding:0 12px 12px}.universal-profile label{margin-top:10px}.universal-profile input,.universal-profile select,.universal-entities input{background:#101318;border-color:#303844}.universal-profile button{width:100%;margin-top:10px}.universal-entities{padding:0 16px 14px}.universal-entities.is-main{border-top:1px solid #29313b;margin-top:4px}.universal-debug .universal-entities{padding:0 12px 4px;border-top:0}.universal-entity{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px 12px;align-items:center;padding:10px 0;border-bottom:1px solid #242b34}.universal-entity:last-child{border-bottom:0}.universal-entity strong{display:block;font-size:13px;color:#eef5fb;overflow:hidden;text-overflow:ellipsis}.universal-entity small{display:block;color:#84909d;margin-top:3px;font-size:10px}.universal-entity .v{font-size:16px;margin:0;text-align:right}.uni-entity-main{min-width:0}.uni-entity-control{grid-column:1/-1;margin-top:2px}.universal-send{display:grid;grid-template-columns:minmax(0,1fr) 70px;gap:7px;margin-top:7px}.universal-send button{margin:0!important;min-height:38px}.universal-slider{display:grid;grid-template-columns:minmax(0,1fr);gap:8px;margin-top:9px;align-items:center;width:100%;box-sizing:border-box}.universal-slider input[type=range]{display:block!important;width:100%!important;min-width:0;margin:0!important;height:28px}.universal-slider input[type=number]{width:72px!important;margin:0!important;padding:7px 6px;text-align:right;box-sizing:border-box}.universal-select{margin-top:8px;width:100%}.universal-action{margin-top:8px;width:100%;min-height:38px}.uni-dual-action{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}.uni-dual-action button{width:100%;min-height:38px}.universal-card .uni-switch.switch{margin:0!important;position:relative;width:52px!important;height:30px!important;min-height:30px!important;padding:0!important;border-radius:999px!important;background:#3b424c!important;color:transparent!important;box-shadow:inset 0 0 0 1px rgba(255,255,255,.08)!important;flex:0 0 52px}.universal-card .uni-switch.switch.on{background:var(--accent)!important;color:transparent!important}.universal-card .uni-switch.switch.off{background:#3b424c!important;color:transparent!important}.universal-card .uni-switch.switch::after{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;border-radius:50%;box-shadow:0 2px 6px rgba(0,0,0,.35);transition:left .18s}.universal-card .uni-switch.switch.on::after{left:25px}.universal-card .uni-switch.switch.off::after{left:3px}.universal-entity.is-switch{grid-template-columns:minmax(0,1fr) 58px}.universal-entity.is-switch .uni-entity-control{grid-column:2;grid-row:1 / span 2;justify-self:end;align-self:center;margin-top:0}.universal-entity.is-switch .v{display:none}.universal-entity.is-button .v,.universal-entity.is-text .v{font-size:13px;max-width:180px;overflow:hidden;text-overflow:ellipsis}.universal-entity.is-sensor .v,.universal-entity.is-binary_sensor .v{font-weight:700}.universal-card input[type=file]{font-size:11px}.uni-descriptor{border-top:1px solid #29313b}.uni-descriptor summary{font-size:11px;color:#9fb3c7}.uni-descriptor pre{white-space:pre-wrap;font-size:11px;max-height:220px;overflow:auto;padding:10px 12px;margin:0;background:#0c1016}.uni-empty{padding:12px 0;color:#84909d;font-size:12px}.weller-card .filter-actions{grid-template-columns:minmax(0,1fr) 72px;display:grid}.weller-card .filter-actions select{width:100%;font-size:12px;padding-left:8px;padding-right:6px}.weller-card .filter-actions button{width:72px;min-width:72px;padding:7px 5px;font-size:10px}.pro-cal{margin:18px 16px 8px;border:1px solid #2b3642;border-radius:12px;background:linear-gradient(180deg,#141922,#10141a);overflow:hidden}.pro-cal summary.cal-head{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:6px 10px;padding:13px 12px;cursor:pointer;list-style:none}.pro-cal summary.cal-head>div{min-width:0;grid-column:1}.pro-cal summary.cal-head::-webkit-details-marker{display:none}.pro-cal summary.cal-head::after{content:"";display:block;width:8px;height:8px;border-right:2px solid #91a4b8;border-bottom:2px solid #91a4b8;transform:rotate(45deg);transition:transform .15s;justify-self:end}.pro-cal[open] summary.cal-head{border-bottom:1px solid #29313b}.pro-cal[open] summary.cal-head::after{transform:rotate(225deg)}.pro-cal .cal-title{font-size:13px;font-weight:850;color:#edf4fb;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.pro-cal .cal-sub{font-size:10px;line-height:1.35;color:#8794a2;margin-top:3px}.pro-cal .cal-badge{grid-column:1 / -1;justify-self:start;max-width:100%;text-align:center;padding:5px 9px;border-radius:999px;background:#26313d;color:#c7d3df;font-size:10px;font-weight:850;white-space:normal;overflow:visible;text-overflow:clip}.pro-cal .cal-body{padding-bottom:12px}.pro-cal .cal-sensor-row{margin:0 14px;min-height:52px;padding:0;border-bottom:1px solid #252b33}.pro-cal .cal-sensor-row .switch{flex:0 0 44px;margin-left:auto}.pro-cal .cal-sensor-row .control-copy small{max-width:170px}.pro-cal .cal-readings{display:grid;grid-template-columns:1.1fr .9fr .8fr;gap:8px;padding:10px 14px 0}.pro-cal .cal-reading{min-width:0;border:1px solid #28313b;border-radius:9px;background:#0c1016;padding:8px}.pro-cal .cal-reading small{display:block;color:#81909f;font-size:10px;text-transform:uppercase;letter-spacing:.03em}.pro-cal .cal-reading strong{display:block;margin-top:4px;font-size:13px;color:#eef5fb;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.pro-cal .cal-reading.is-wide strong{white-space:normal}.pro-cal .cal-steps{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;padding:10px 14px 13px}.pro-cal .cal-steps button,.pro-cal .cal-actions button{width:100%;min-height:34px;font-size:11px;padding:7px 8px}.pro-cal .cal-advanced{border-top:1px solid #29313b;padding:10px 14px 0}.pro-cal .cal-advanced summary{cursor:pointer;color:#9fc0df;font-size:11px;font-weight:800;list-style:none}.pro-cal .cal-advanced summary::-webkit-details-marker{display:none}.pro-cal .cal-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}.pro-cal .cal-grid label{margin:0;color:#9aa7b5;font-size:10px;text-transform:uppercase;letter-spacing:.03em}.pro-cal .cal-grid input{width:100%;box-sizing:border-box;margin-top:5px;background:#0c1016;border:1px solid #303844;border-radius:8px;padding:8px;color:#fff;font-weight:800}.pro-cal .cal-actions{display:grid;grid-template-columns:1fr;gap:8px;margin-top:10px}
.module-card .module-footer b{font:inherit;color:inherit}.module-card .module-head .module-identity{min-width:0;flex:1 1 auto}.module-card .module-statuses{flex:0 0 auto;min-width:78px}.module-card .module-statuses .pill{min-width:78px;height:22px;padding:0 9px;font-size:11px;white-space:nowrap;word-break:normal;line-height:1}.module-card .module-icon{width:42px;min-width:42px;height:42px;flex:0 0 42px}.module-card .module-statuses{height:74px;min-height:74px;flex-direction:column;flex-wrap:nowrap;align-items:flex-end;justify-content:center;gap:3px}.module-card .module-head .uni-save-state{min-height:0;margin:0;font-size:10px;line-height:1.1}.module-card .module-head .uni-save-state:empty{display:none}.overview-board{display:grid;gap:12px;margin-bottom:14px}.overview-group{border:1px solid #303640;background:#171a1f;border-radius:8px;overflow:hidden}.section-title{padding:11px 14px;border-bottom:1px solid #2d333b;color:#dce5ef;font-size:13px;font-weight:700}.master-section-title{display:grid;grid-template-columns:1fr auto 1fr;align-items:center;gap:14px}.master-section-title>span{grid-column:1;justify-self:start}.master-section-title .master-led-pair{grid-column:2;justify-self:center;margin:0;gap:18px}.master-section-title .master-led-pair .led-word{font-size:13px;min-width:40px}.stat-strip{display:grid;grid-template-columns:repeat(6,minmax(0,1fr))}.extraction-stats{grid-template-columns:repeat(7,minmax(0,1fr))}.connection-stats{grid-template-columns:repeat(auto-fit,minmax(130px,1fr))}.stat-cell{min-width:0;padding:13px 14px;border-right:1px solid #292f37}.stat-cell:last-child{border-right:0}.stat-cell .v{font-size:19px}.stat-cell .mini{font-size:14px}.stat-cell .bar{margin-top:8px}.stat-cell .work-line{min-height:34px}.control-layout{display:grid;grid-template-columns:minmax(240px,300px) minmax(0,1fr);gap:12px;margin:14px 0 12px}.control-panel{border:1px solid #303640;background:#171a1f;border-radius:8px;padding:16px;margin-bottom:12px}.control-layout .control-panel{margin-bottom:0}.panel-heading{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;margin-bottom:14px}.panel-heading h2{font-size:16px;margin:0}.panel-heading p{margin:5px 0 0;color:#87929e;font-size:12px;line-height:1.4}.output-control{display:grid;gap:10px}.output-control button{width:100%}.settings-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px 12px}.settings-grid label{margin-top:0}.form-actions{display:flex;align-items:center;gap:12px;margin-top:14px}.routing-panel{margin-bottom:14px}.routing-master{display:flex;align-items:center;gap:10px;color:#aab4bf;font-size:12px}.rule-head,.routing-rule{display:grid;grid-template-columns:42px minmax(0,1fr) 34px minmax(0,1fr);gap:10px;align-items:center}.rule-head{padding:0 10px 7px;color:#7f8a96;font-size:11px}.routing-rule{padding:10px;border-top:1px solid #292f37}.rule-number{width:29px;height:29px;border-radius:50%;display:flex;align-items:center;justify-content:center;background:#25303a;color:#dce7f1;font-size:12px;font-weight:700}.rule-arrow{text-align:center;color:#4a90d9;font-size:20px}.routing-rule select{min-width:0}
@media(max-width:1050px){.stat-strip,.extraction-stats,.connection-stats{grid-template-columns:repeat(3,minmax(0,1fr))}.stat-cell{border-bottom:1px solid #292f37}}
@media(max-width:760px){.control-layout{grid-template-columns:1fr}.settings-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.rule-head{display:none}.routing-rule{grid-template-columns:32px 1fr}.routing-rule .rule-arrow{grid-column:1;text-align:center;transform:rotate(90deg)}.routing-rule select:last-child{grid-column:2}}
@media(max-width:520px){.stat-strip,.extraction-stats,.connection-stats,.settings-grid{grid-template-columns:1fr}.stat-cell{border-right:0}.panel-heading{flex-direction:column}.routing-master{width:100%;justify-content:space-between}}.route-summary{display:inline-flex;align-items:center;min-height:30px;padding:5px 10px;border:1px solid #33404d;border-radius:999px;background:#202832;color:#b9c9d8;font-size:12px;font-weight:700;white-space:nowrap}.settings-save-state{min-height:22px;margin-top:10px}.settings-save-state .muted{font-size:12px}.control-row.input-row .pill{min-width:54px;text-align:center}.routing-rule select:disabled{opacity:.55}.logic-summary{margin:14px 10px 4px;border-top:1px solid #29313b;padding:14px 10px 2px}.logic-summary-list{display:grid;gap:10px}.logic-summary-card{border:1px solid #303a45;background:#11161c;border-radius:9px;padding:12px}.logic-summary-card.is-disabled{opacity:.82}.logic-summary-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}.logic-summary-head .actions{gap:10px}.logic-summary-head .pill{min-width:0;padding:0 11px}.logic-summary-title{font-weight:850;color:#edf4fb}.logic-summary-sub{margin-top:3px;color:#8794a3;font-size:12px}.logic-summary-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}.logic-summary-chip{border:1px solid #28313b;background:#0c1016;border-radius:8px;padding:8px;min-width:0}.logic-summary-chip.is-wide{grid-column:span 2}.logic-summary-chip small{display:block;color:#8291a1;font-size:10px;text-transform:uppercase}.logic-summary-chip strong{display:block;margin-top:4px;color:#eef5fb;font-size:13px;white-space:normal;overflow-wrap:anywhere}.logic-block-tags{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px}.logic-block-tags span{display:inline-flex;align-items:center;border-radius:99px;background:#21303b;border:1px solid #344959;color:#dce8f3;padding:4px 8px;font-size:11px;font-weight:800;white-space:nowrap}

.universal-card .uni-overview{grid-template-columns:repeat(3,minmax(0,1fr));padding-bottom:8px}.universal-card .uni-chip.is-wide{grid-column:span 1}.uni-section-title{padding:11px 16px 7px;color:#dce6ef;font-size:12px;font-weight:850;letter-spacing:.02em;display:flex;align-items:center;justify-content:space-between;gap:10px}.uni-section-title small{font-weight:500;color:#8794a2}.uni-live-note{padding:0 16px 12px;color:#8794a2;font-size:11px;line-height:1.4}.universal-entities.is-main{padding-top:0}.universal-profile summary,.universal-debug summary{display:flex;align-items:center;justify-content:space-between;gap:10px}.universal-profile summary::after,.universal-debug summary::after{content:"";display:block;width:8px;height:8px;border-right:2px solid #91a4b8;border-bottom:2px solid #91a4b8;transform:rotate(45deg);transition:transform .15s;flex:0 0 8px}.universal-profile[open] summary::after,.universal-debug[open] summary::after{transform:rotate(225deg)}.universal-profile-body{padding:0 12px 14px}.uni-direct-fields{display:none!important}.uni-form-section{margin-top:10px;padding-top:10px;border-top:1px solid #242c35}.uni-form-section:first-child{border-top:0;margin-top:0;padding-top:0}.uni-form-title{color:#eef5fb;font-size:11px;font-weight:850;text-transform:uppercase;letter-spacing:.05em;margin:2px 0 8px}.uni-help{color:#8794a2;font-size:11px;line-height:1.4;margin:7px 0 0}.uni-config-actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:12px}.uni-config-actions button{width:100%;margin:0!important;min-height:38px}.uni-file-label{border:1px dashed #384450;border-radius:9px;padding:10px;background:#0f1319}.uni-file-label input{margin-top:7px}.uni-save-state{min-height:20px;margin-top:10px;font-size:12px}.uni-save-state.is-dirty{color:#f4c25b}.uni-save-state.is-ok{color:#79d279}.universal-debug .uni-debug-stats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;padding:0 12px 10px}.universal-debug .uni-debug-stats>div{background:#0c1016;border:1px solid #28313b;border-radius:8px;padding:8px}.universal-debug .uni-debug-stats span{display:block;color:#8794a2;font-size:10px;text-transform:uppercase}.universal-debug .uni-debug-stats strong{display:block;margin-top:4px;color:#eef5fb;font-size:14px}.uni-primary-action{background:#275d38;border-color:#347449;color:#eefaf1}.universal-card .metric-line{margin:14px 16px 0}.universal-card .metric-line>div{min-width:0}.universal-card .metric-line .v{font-size:17px;line-height:1.18;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;word-break:normal}.universal-card .status-note{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin:12px 16px 0;padding:0;border:0}.universal-card .status-note span{min-width:0;background:#101318;border:1px solid #282f38;border-radius:7px;padding:8px;color:#9aa7b5;font-size:10px}.universal-card .status-note strong{display:block;margin-top:4px;color:#eef5fb;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.universal-card .universal-entities.is-main{margin:12px 16px 0;padding:0;border-top:1px solid #29313b}.universal-card .uni-section-title{padding:12px 0 6px}.universal-card .uni-section-title .k{font-size:12px;font-weight:850;color:#dce6ef}.universal-card .universal-entities-list{padding:0}.universal-card .universal-entity{grid-template-columns:minmax(0,1fr) auto;min-height:50px;padding:11px 0;border-bottom:1px solid #29313b}.universal-card .universal-entity strong{font-size:14px}.universal-card .universal-entity small{font-size:10px;color:#7f8b98}.universal-card .universal-entity .v{font-size:15px;font-weight:800;color:#eef5fb}.universal-card .universal-entity.is-number{grid-template-columns:minmax(0,1fr)}.universal-card .universal-entity.is-number .uni-entity-control{grid-column:1/-1;margin-top:7px}.universal-card .universal-slider{grid-template-columns:minmax(0,1fr);gap:9px;margin-top:0}.universal-card .universal-slider input[type=range]{display:block!important;width:100%!important;min-width:0;height:28px;margin:0!important;accent-color:var(--accent)}.universal-card .universal-slider input[type=number]{width:66px!important;min-width:0!important}.universal-card .universal-entity.is-switch{grid-template-columns:minmax(0,1fr) 48px}.universal-card .universal-entity.is-switch .uni-entity-control{grid-column:2;grid-row:1 / span 2;justify-self:end;align-self:center}.universal-card .uni-switch.switch{width:44px!important;height:24px!important;min-height:24px!important}.universal-card .uni-switch.switch::after{width:20px;height:20px;left:2px;top:2px}.universal-card .uni-switch.switch.on::after{left:22px}.universal-card .uni-switch.switch.off::after{left:2px}.universal-card .universal-profile,.universal-card .universal-debug{margin:12px 16px 0}.universal-card .universal-debug{margin-bottom:18px}.universal-card .module-system{margin-top:auto}.universal-card .metric-line .v{white-space:normal;overflow:visible;text-overflow:clip;overflow-wrap:anywhere}.universal-card .status-note strong{white-space:normal;overflow:visible;text-overflow:clip;overflow-wrap:anywhere}.universal-card .universal-entity{grid-template-columns:minmax(0,1fr);gap:5px}.universal-card .universal-entity .v{text-align:left}.universal-card .universal-entity small{display:none}.universal-card .universal-entity.is-switch{grid-template-columns:minmax(0,1fr) 48px}.universal-card .universal-entity.is-switch .uni-entity-control{grid-column:2;grid-row:1 / span 2}.universal-card .universal-entity.is-number{grid-template-columns:minmax(0,1fr)}.universal-card .universal-entity.is-number small{display:block}.universal-card .universal-entity.is-select .uni-entity-control,.universal-card .universal-entity.is-button .uni-entity-control{grid-column:1/-1}.universal-card .universal-debug .universal-entity small{display:block!important}@media(max-width:760px){.universal-card .uni-overview{grid-template-columns:1fr}.uni-config-actions{grid-template-columns:1fr}.universal-debug .uni-debug-stats{grid-template-columns:1fr}}
.module-card .inline-alias{width:100%;max-width:150px;min-height:0;height:22px;padding:0;border:0;background:transparent;color:#eef5fb;font-weight:850;font-size:14px;line-height:22px;box-shadow:none}.module-card .inline-alias:focus{height:30px;padding:3px 7px;border:1px solid #303844;border-radius:8px;background:#101318;outline:none}.module-card .control-copy strong{display:flex;align-items:center;min-height:24px}.row button+button{margin-left:10px}.routing-rule{grid-template-columns:42px minmax(0,1fr) 34px minmax(0,1fr) auto}.route-add{margin:10px}.signal-path-editor{display:grid;grid-template-columns:1fr;gap:8px}.signal-path-editor label{margin-top:0}.signal-arrow{display:flex;align-items:center;justify-content:center;min-height:20px;color:#4a90d9;font-size:22px;font-weight:700;transform:rotate(90deg)}.output-panel{min-width:0}
/* v1.7.58 Modbus Register Map Builder */
.modbus-builder{margin:12px 16px 0;border:1px solid #31506c;border-radius:10px;background:#0d141c;overflow:hidden}
.modbus-builder>summary{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:12px 13px;cursor:pointer;color:#e8f3ff;font-weight:850;list-style:none}
.modbus-builder>summary::-webkit-details-marker{display:none}
.modbus-builder>summary small{font-size:10px;font-weight:650;color:#80a7c8}
.modbus-builder-body{padding:0 12px 13px}
.mb-intro{margin:0 0 11px;color:#94a5b5;font-size:11px;line-height:1.45}
.mb-device-grid{display:grid;grid-template-columns:1.35fr 1.35fr .8fr .8fr .8fr .9fr;gap:8px}
.mb-device-grid label,.mb-row label{display:flex;flex-direction:column;gap:4px;min-width:0;color:#93a2b1;font-size:10px;font-weight:700}
.mb-device-grid input,.mb-device-grid select,.mb-row input,.mb-row select{width:100%;min-width:0;box-sizing:border-box}
.mb-builder-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:14px 0 7px}
.mb-builder-head strong{font-size:12px;color:#e4edf6}
.mb-builder-head small{color:#80909f;font-size:10px}
.mb-rows{display:flex;flex-direction:column;gap:9px}
.mb-row{border:1px solid #293b4b;border-radius:9px;background:#0a1017;padding:9px}
.mb-row-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px}
.mb-row-title{display:flex;align-items:center;gap:7px;min-width:0}
.mb-row-num{display:inline-flex;align-items:center;justify-content:center;width:22px;height:22px;border-radius:6px;background:#18324a;color:#a8d4ff;font:800 10px/1 Consolas,monospace}
.mb-row-title strong{font-size:11px;color:#dce7f1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.mb-remove{min-height:28px!important;padding:4px 8px!important;font-size:10px!important}
.mb-row-grid{display:grid;grid-template-columns:1.4fr 1.15fr 1fr .9fr 1.1fr .8fr .8fr;gap:7px;align-items:end}
.mb-reg-note{margin-top:7px;padding:6px 7px;border-radius:6px;background:#101a24;color:#8ea0b2;font:10px/1.35 Consolas,monospace}
.mb-reg-note.ok{color:#9fd5b0}.mb-reg-note.err{color:#ff9b9b;background:#251317}
.mb-advanced{margin-top:8px;border-top:1px solid #22303c;padding-top:7px}
.mb-advanced>summary{cursor:pointer;color:#8ea2b5;font-size:10px;font-weight:750}
.mb-advanced-grid{display:grid;grid-template-columns:1.2fr .65fr .95fr .75fr .75fr .75fr .75fr;gap:7px;margin-top:8px}
.mb-select-grid{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:7px}
.mb-builder-actions{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px;margin-top:11px}
.mb-builder-actions button{width:100%;min-height:36px;margin:0!important;padding:7px 8px;font-size:11px}
.mb-builder-msg{min-height:18px;margin-top:8px;color:#93a3b3;font-size:11px}
.mb-builder-msg.ok{color:#7fda96}.mb-builder-msg.warn{color:#e9c873}.mb-builder-msg.err{color:#ff9797}
.mb-empty{padding:14px;border:1px dashed #33414e;border-radius:8px;color:#8595a5;text-align:center;font-size:11px}
.mb-limit-note{margin-top:8px;color:#728393;font-size:10px;line-height:1.4}
@media(max-width:1180px){.mb-device-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.mb-row-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.mb-advanced-grid{grid-template-columns:repeat(4,minmax(0,1fr))}}
@media(max-width:760px){.mb-device-grid,.mb-row-grid,.mb-advanced-grid,.mb-select-grid{grid-template-columns:1fr 1fr}.mb-builder-actions{grid-template-columns:1fr 1fr}}
@media(max-width:520px){.mb-device-grid,.mb-row-grid,.mb-advanced-grid,.mb-select-grid,.mb-builder-actions{grid-template-columns:1fr}}


/* v1.7.59 Large Modbus Builder modal */
.modbus-builder-launch{
  margin:12px 16px 0;
  width:calc(100% - 32px);
  min-height:54px;
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:12px;
  box-sizing:border-box;
  padding:11px 13px;
  border:1px solid #31506c;
  border-radius:10px;
  background:#0d141c;
  color:#e8f3ff;
  text-align:left;
  cursor:pointer
}
.modbus-builder-launch:hover{border-color:#47749a;background:#101b26}
.modbus-builder-launch-copy{display:flex;flex-direction:column;gap:3px;min-width:0}
.modbus-builder-launch-copy strong{font-size:14px;line-height:1.2}
.modbus-builder-launch-copy small{font-size:10px;font-weight:650;color:#80a7c8;line-height:1.3}
.modbus-builder-launch-open{
  flex:0 0 auto;
  display:inline-flex;
  align-items:center;
  justify-content:center;
  min-height:31px;
  padding:5px 9px;
  border:1px solid #38536a;
  border-radius:8px;
  background:#182431;
  color:#b8d8f3;
  font-size:10px;
  font-weight:850;
  white-space:nowrap
}
body.mb-modal-open{overflow:hidden}
.mb-modal-backdrop{
  position:fixed;
  inset:0;
  z-index:10050;
  display:flex;
  align-items:center;
  justify-content:center;
  padding:22px;
  box-sizing:border-box;
  background:rgba(2,6,10,.82);
  backdrop-filter:blur(3px)
}
.mb-modal-window{
  width:min(1600px,96vw);
  height:min(92vh,1040px);
  min-height:520px;
  display:flex;
  flex-direction:column;
  overflow:hidden;
  border:1px solid #385a78;
  border-radius:14px;
  background:#0b1017;
  box-shadow:0 26px 80px rgba(0,0,0,.6)
}
.mb-modal-head{
  flex:0 0 auto;
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:18px;
  padding:13px 16px;
  border-bottom:1px solid #263849;
  background:#101821
}
.mb-modal-title{display:flex;align-items:center;gap:11px;min-width:0}
.mb-modal-icon{
  display:inline-flex;
  align-items:center;
  justify-content:center;
  width:31px;
  height:31px;
  flex:0 0 auto;
  border-radius:8px;
  background:#18324a;
  color:#acd8ff;
  font:900 12px/1 Consolas,monospace
}
.mb-modal-title-copy{display:flex;flex-direction:column;gap:2px;min-width:0}
.mb-modal-title-copy strong{font-size:15px;color:#eef6fd;line-height:1.2}
.mb-modal-title-copy small{font-size:10px;color:#8da1b3;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.mb-modal-close{
  flex:0 0 auto;
  min-width:38px!important;
  min-height:34px!important;
  margin:0!important;
  padding:4px 10px!important;
  font-size:18px!important;
  line-height:1!important
}
.mb-modal-scroll{
  flex:1 1 auto;
  min-height:0;
  overflow:auto;
  padding:15px 17px 20px;
  scrollbar-gutter:stable
}
.mb-modal-scroll .modbus-builder{
  margin:0;
  border:0;
  border-radius:0;
  background:transparent;
  overflow:visible
}
.mb-modal-scroll .modbus-builder-body{padding:0}
.mb-modal-scroll .mb-intro{max-width:1050px;font-size:12px}
.mb-modal-scroll .mb-device-grid{
  grid-template-columns:minmax(180px,1.35fr) minmax(180px,1.35fr) minmax(105px,.8fr) minmax(90px,.7fr) minmax(90px,.7fr) minmax(130px,.9fr);
  gap:10px
}
.mb-modal-scroll .mb-row{padding:12px}
.mb-modal-scroll .mb-row-grid{
  grid-template-columns:minmax(190px,1.4fr) minmax(150px,1.1fr) minmax(150px,1fr) minmax(150px,.95fr) minmax(170px,1.1fr) minmax(120px,.8fr) minmax(130px,.8fr);
  gap:9px
}
.mb-modal-scroll .mb-advanced-grid{
  grid-template-columns:minmax(180px,1.2fr) minmax(90px,.55fr) minmax(160px,.95fr) minmax(120px,.75fr) minmax(100px,.7fr) minmax(100px,.7fr) minmax(100px,.7fr);
  gap:9px
}
.mb-modal-scroll .mb-builder-actions{
  max-width:980px;
  grid-template-columns:repeat(4,minmax(150px,1fr));
  gap:9px
}
.mb-modal-scroll .mb-device-grid label,
.mb-modal-scroll .mb-row label{font-size:11px}
.mb-modal-scroll .mb-device-grid input,
.mb-modal-scroll .mb-device-grid select,
.mb-modal-scroll .mb-row input,
.mb-modal-scroll .mb-row select{min-height:36px}
.mb-modal-scroll .mb-reg-note{font-size:11px}
@media(max-width:1150px){
  .mb-modal-window{width:97vw;height:94vh}
  .mb-modal-scroll .mb-device-grid{grid-template-columns:repeat(3,minmax(0,1fr))}
  .mb-modal-scroll .mb-row-grid{grid-template-columns:repeat(4,minmax(0,1fr))}
  .mb-modal-scroll .mb-advanced-grid{grid-template-columns:repeat(4,minmax(0,1fr))}
}
@media(max-width:760px){
  .mb-modal-backdrop{padding:6px}
  .mb-modal-window{width:100%;height:98vh;border-radius:10px}
  .mb-modal-head{padding:10px 11px}
  .mb-modal-scroll{padding:11px}
  .mb-modal-scroll .mb-device-grid,
  .mb-modal-scroll .mb-row-grid,
  .mb-modal-scroll .mb-advanced-grid,
  .mb-modal-scroll .mb-select-grid{grid-template-columns:1fr 1fr}
  .mb-modal-scroll .mb-builder-actions{grid-template-columns:1fr 1fr}
}
@media(max-width:520px){
  .mb-modal-scroll .mb-device-grid,
  .mb-modal-scroll .mb-row-grid,
  .mb-modal-scroll .mb-advanced-grid,
  .mb-modal-scroll .mb-select-grid,
  .mb-modal-scroll .mb-builder-actions{grid-template-columns:1fr}
}


/* v1.7.60 Universal RS232 Protocol Builder */
.upb-launch{
  margin:12px 16px 0;
  width:calc(100% - 32px);
  min-height:54px;
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:12px;
  box-sizing:border-box;
  padding:11px 13px;
  border:1px solid #31506c;
  border-radius:10px;
  background:#0d141c;
  color:#e8f3ff;
  text-align:left;
  cursor:pointer
}
.upb-launch:hover{border-color:#47749a;background:#101b26}
.upb-launch-copy{display:flex;flex-direction:column;gap:3px;min-width:0}
.upb-launch-copy strong{font-size:14px;line-height:1.2}
.upb-launch-copy small{font-size:10px;font-weight:650;color:#80a7c8;line-height:1.3}
.upb-launch-open{
  flex:0 0 auto;display:inline-flex;align-items:center;justify-content:center;
  min-height:31px;padding:5px 9px;border:1px solid #38536a;border-radius:8px;
  background:#182431;color:#b8d8f3;font-size:10px;font-weight:850;white-space:nowrap
}
.upb-editor{min-width:0}
.upb-intro{
  margin:0 0 13px;padding:11px 13px;border:1px solid #263b4e;border-radius:9px;
  background:#0d1721;color:#9db0c2;font-size:12px;line-height:1.5
}
.upb-intro strong{color:#dcecff}
.upb-device-grid{
  display:grid;
  grid-template-columns:minmax(180px,1.3fr) minmax(180px,1.3fr) minmax(105px,.75fr) minmax(90px,.65fr) minmax(150px,.9fr) minmax(150px,.9fr) minmax(120px,.75fr);
  gap:10px
}
.upb-device-grid label,.upb-row label{
  display:flex;flex-direction:column;gap:4px;min-width:0;
  color:#93a2b1;font-size:11px;font-weight:700
}
.upb-device-grid input,.upb-device-grid select,.upb-row input,.upb-row select{
  width:100%;min-width:0;min-height:36px;box-sizing:border-box
}
.upb-help{
  display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin:12px 0 4px
}
.upb-help-card{
  padding:8px 10px;border:1px solid #263847;border-radius:8px;background:#0a1118;
  color:#8194a7;font-size:10px;line-height:1.4
}
.upb-help-card strong{display:block;margin-bottom:2px;color:#bed7ed;font-size:10px}
.upb-builder-head{
  display:flex;align-items:center;justify-content:space-between;gap:12px;margin:16px 0 8px
}
.upb-builder-head strong{font-size:13px;color:#e4edf6}
.upb-builder-head small{color:#80909f;font-size:10px}
.upb-add-actions{display:flex;gap:6px;flex-wrap:wrap}
.upb-add-actions button{margin:0!important;min-height:31px!important;padding:5px 8px!important;font-size:10px!important}
.upb-rows{display:flex;flex-direction:column;gap:10px}
.upb-row{border:1px solid #293b4b;border-radius:10px;background:#0a1017;padding:12px}
.upb-row-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:9px}
.upb-row-title{display:flex;align-items:center;gap:8px;min-width:0}
.upb-row-num{
  display:inline-flex;align-items:center;justify-content:center;width:24px;height:24px;
  border-radius:7px;background:#18324a;color:#a8d4ff;font:800 10px/1 Consolas,monospace
}
.upb-row-title strong{font-size:12px;color:#dce7f1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.upb-remove{min-height:28px!important;padding:4px 8px!important;font-size:10px!important;margin:0!important}
.upb-row-grid{
  display:grid;
  grid-template-columns:minmax(180px,1.35fr) minmax(145px,.95fr) minmax(125px,.8fr) minmax(155px,1.05fr) minmax(180px,1.25fr) minmax(95px,.65fr) minmax(115px,.72fr);
  gap:9px;align-items:end
}
.upb-rule-note{
  margin-top:8px;padding:7px 9px;border-radius:7px;background:#101a24;color:#9fc4df;
  font:10px/1.4 Consolas,monospace;overflow-wrap:anywhere
}
.upb-advanced{margin-top:9px;border-top:1px solid #22303c;padding-top:8px}
.upb-advanced>summary{cursor:pointer;color:#91a9bc;font-size:10px;font-weight:800}
.upb-advanced-grid{
  display:grid;grid-template-columns:minmax(170px,1.2fr) minmax(90px,.55fr) minmax(175px,1fr) minmax(95px,.65fr) minmax(95px,.65fr) minmax(95px,.65fr) minmax(95px,.65fr);
  gap:9px;margin-top:9px
}
.upb-write-grid{
  display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:9px;margin-top:9px
}
.upb-select-grid{display:grid;grid-template-columns:1fr 1fr;gap:9px;margin-top:9px}
.upb-empty{
  padding:18px;border:1px dashed #33414e;border-radius:9px;color:#8595a5;
  text-align:center;font-size:11px;line-height:1.5
}
.upb-actions{
  display:grid;grid-template-columns:repeat(4,minmax(150px,1fr));gap:9px;
  max-width:1040px;margin-top:13px
}
.upb-actions button{width:100%;min-height:38px;margin:0!important;padding:7px 9px;font-size:11px}
.upb-msg{min-height:20px;margin-top:9px;color:#93a3b3;font-size:11px}
.upb-msg.ok{color:#7fda96}.upb-msg.warn{color:#e9c873}.upb-msg.err{color:#ff9797}
.upb-preview{margin-top:10px;border:1px solid #25394b;border-radius:9px;background:#080e14;overflow:hidden}
.upb-preview>summary{padding:9px 11px;cursor:pointer;color:#91aac0;font-size:10px;font-weight:800}
.upb-preview textarea{
  width:100%;min-height:230px;box-sizing:border-box;margin:0;border:0;border-top:1px solid #25394b;
  border-radius:0;background:#070c11;color:#b8c7d5;font:11px/1.45 Consolas,monospace;resize:vertical
}
.upb-limit-note{margin-top:9px;color:#728393;font-size:10px;line-height:1.4}
.upb-type-extra[hidden]{display:none!important}
@media(max-width:1250px){
  .upb-device-grid{grid-template-columns:repeat(4,minmax(0,1fr))}
  .upb-row-grid{grid-template-columns:repeat(4,minmax(0,1fr))}
  .upb-advanced-grid{grid-template-columns:repeat(4,minmax(0,1fr))}
}
@media(max-width:850px){
  .upb-device-grid,.upb-row-grid,.upb-advanced-grid{grid-template-columns:1fr 1fr}
  .upb-write-grid,.upb-help{grid-template-columns:1fr}
  .upb-actions{grid-template-columns:1fr 1fr}
}
@media(max-width:520px){
  .upb-device-grid,.upb-row-grid,.upb-advanced-grid,.upb-write-grid,.upb-select-grid,.upb-actions{grid-template-columns:1fr}
}


/* v1.7.61 shared main-output assistant */
.builder-output-assistant{
  margin:12px 0 2px;padding:11px 12px;border:1px solid #31506c;border-radius:9px;
  background:#0d1721
}
.builder-output-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:9px}
.builder-output-title{display:flex;flex-direction:column;gap:2px;min-width:0}
.builder-output-title strong{font-size:12px;color:#dcecff}
.builder-output-title small{font-size:10px;color:#8298aa;line-height:1.35}
.builder-output-toggle{display:flex!important;flex-direction:row!important;align-items:center!important;gap:7px!important;color:#bed9ef!important;white-space:nowrap}
.builder-output-toggle input{width:auto!important;min-height:0!important}
.builder-output-grid{display:grid;grid-template-columns:1fr 1fr;gap:9px}
.builder-output-grid label{display:flex;flex-direction:column;gap:4px;color:#93a2b1;font-size:10px;font-weight:700}
.builder-output-note{margin-top:8px;color:#7f91a2;font-size:10px;line-height:1.4}
@media(max-width:650px){.builder-output-grid{grid-template-columns:1fr}.builder-output-head{align-items:flex-start;flex-direction:column}}

</style></head><body><header><div class="brand"><img class="brand-logo" src="/logo.png" alt=""><div class="brand-text">Open Fume Extractor<span>by IceCube20</span></div><button class="secondary dev-toggle brand-dev" id="dev_btn" onclick="toggleDevMode()">Developer mode</button></div><nav class="nav"><select id="lang_sel" data-no-dirty="1" onchange="setLang(this.value)"><option value="de">Deutsch</option><option value="en">English</option></select><a class="btn active" href="/">Status</a><a class="btn secondary" href="/config" data-i18n="wifi">Network Setup</a><a class="btn secondary" href="/update" data-i18n="updates">Updates</a><a class="btn secondary" href="/diagnostics" data-i18n="diagnostics">Bus Diagnose</a><a class="btn secondary" href="/logic">Logik Designer</a><button class="secondary" onclick="load(true)" data-i18n="refresh">Refresh</button><button class="danger" onclick="restartMaster()" data-i18n="restart">Restart</button></nav></header><main>
)HTML";
  static const char html2[] PROGMEM = R"HTML(
<div class="page-head"><div class="eyebrow" data-i18n="system">System</div><h1 data-i18n="status_overview">Statusübersicht</h1><p data-i18n="status_intro">Live-Status, Module und Steuerung des Open Fume Extractor.</p></div>
<section class="status-hero"><div><div class="eyebrow" data-i18n="extractor_state">Absaugung</div><div id="hero_state" class="hero-state">-</div><div id="hero_meta" class="hero-meta">-</div></div><div class="hero-power"><span data-i18n="requested_power">Angeforderte Leistung</span><strong id="hero_power">0%</strong><div class="bar"><div id="hero_fill" class="fill"></div></div></div></section>
<section class="alarm-panel tile"><div class="alarm-head"><div class="alarm-title" data-i18n="alarm_center">Alarmzentrale</div><span id="alarm_count" class="alarm-count">0</span></div><div id="alarm_list" class="alarm-list"><div class="alarm-item ok"><strong data-i18n="no_active_alarms">Keine aktiven Alarme</strong></div></div></section>
<section class="overview-board">
<div class="overview-group"><div class="section-title master-section-title"><span data-i18n="master_system">Master system</span><div class="master-led-pair"><span id="master_led_ofe" class="led-word">OFE</span><span id="master_led_evt" class="led-word">EVT</span></div></div><div class="stat-strip master-stats">
<div class="stat-cell"><div class="k">Master FW</div><div id="master_fw" class="v">-</div></div>
<div class="stat-cell"><div class="k">IP</div><div id="master_ip" class="v mini">-</div></div>
<div class="stat-cell wifi-cell"><div class="k" data-i18n="wifi_signal">WiFi Signal</div><div class="wifi-meter"><div class="wifi-bar"><div id="wifi_fill" class="wifi-fill"></div></div><div id="wifi_db" class="wifi-db">-</div></div><div id="wifi_ssid" class="ssid">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="uptime">Uptime</div><div id="uptime" class="v mini">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="datetime">Date/Time</div><div id="datetime" class="v mini">-</div></div>
<div class="stat-cell dev-only"><div class="k" data-i18n="heap_free">Free Heap</div><div id="heap" class="v mini">-</div></div>
<div class="stat-cell dev-only"><div class="k">Heap Diagnose</div><div id="heap_diag" class="v mini">-</div><div id="heap_diag_low" class="mini">-</div><div id="heap_diag_block" class="mini">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="cpu_load">CPU</div><div id="cpu_load" class="v mini">-</div><div class="bar"><div id="loop_fill" class="fill"></div></div><div id="loop_max_dev" class="mini dev-only">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="modules">Modules</div><div id="mods" class="v">0</div></div>
</div></div>
<div class="overview-group"><div class="section-title" data-i18n="extraction_status">Extraction status</div><div class="stat-strip extraction-stats">
<div class="stat-cell"><div class="k" data-i18n="output">Output</div><div id="out" class="v off">off</div></div>
<div class="stat-cell"><div class="k" data-i18n="requested_power">Requested Power</div><div id="power" class="v">0</div><div class="bar"><div id="pfill" class="fill"></div></div></div>
<div class="stat-cell"><div class="k">Work</div><div class="work-line"><span id="work_iron" class="overview-work-symbol" style="--tool-color:#777;--tool-glow:none" aria-label="work"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4.86 4.03L2.03 6.86L5.21 10.04V12.87L6.63 14.28L12.28 8.63L10.87 7.21H8.04L4.86 4.03M17 6V7.5C18 7.5 18.85 8.33 18.85 9.35C18.85 10.37 18 11.2 17 11.2V12.7C19.24 12.7 21 14.53 21 16.77V21H22.5V16.76C22.5 14.54 21.22 12.62 19.35 11.73C19.97 11.12 20.35 10.28 20.35 9.35C20.35 7.5 18.85 6 17 6M11.93 11.1L9.1 13.93L14.05 18.88L14.76 18.17L16.88 20.29L19 21L18.29 18.88L16.17 16.76L16.88 16.05L11.93 11.1Z" fill="currentColor"/></svg></span><div id="work" class="v">0x0</div></div></div>
<div class="stat-cell"><div class="k" data-i18n="afterrun">Afterrun</div><div id="after" class="v">0s</div></div>
<div class="stat-cell"><div class="k" data-i18n="output_feedback">Output Feedback</div><div id="ofb" class="v mini">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="fan_rpm">Fan RPM</div><div id="rpm" class="v">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="output_fault">Output Fault</div><div id="fault" class="v mini">0x0</div></div>
</div></div>
<div class="overview-group"><div class="section-title" data-i18n="connections_settings">Connections and settings</div><div class="stat-strip connection-stats">
<div id="jbc_link_cell" class="stat-cell"><div class="k">JBC Link</div><div id="jbc_link" class="v mini off">off</div></div>
<div id="station_cell" class="stat-cell"><div class="k" data-i18n="station">Station</div><div id="station" class="v mini">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="roles">Roles</div><div id="roles" class="v mini">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="suction_level">Suction Level</div><div id="suction" class="v">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="custom_power">Custom Power</div><div id="select" class="v">-</div></div>
<div class="stat-cell"><div class="k" data-i18n="delay_work">Delay Work</div><div id="delay" class="v">-</div></div>
<div id="jbc_error_cell" class="stat-cell"><div class="k" data-i18n="error">Error</div><div id="err" class="v mini">OK</div></div>
</div></div>
</section>
<section class="control-layout">
<section class="control-panel output-panel">
<div class="panel-heading"><div><h2 data-i18n="main_signal_path">Main signal path</h2><p data-i18n="main_signal_hint">Choose what starts the extractor and which module provides its main output.</p></div></div>
<div class="signal-path-editor"><div><label data-i18n="main_input">Main input</label><select id="main_input_sel" onchange="saveMainInput()"></select></div><span class="signal-arrow">&rarr;</span><div><label data-i18n="main_output">Main extractor output</label><select id="output_sel" onchange="saveOutput()"></select></div></div>
</section>
<section class="control-panel settings-panel">
<div class="panel-heading"><div><h2 data-i18n="extractor_settings">Extractor Settings</h2><p data-i18n="extractor_settings_hint">Operating mode, power and afterrun.</p></div></div>
<form id="jbcForm">
<div class="settings-grid">
<div><label data-i18n="suction_level">Suction Level</label><select id="jbc_suction" name="suction"><option value="0">High (100%)</option><option value="1">Medium (60%)</option><option value="2">Low (30%)</option><option value="3">Custom</option></select></div>
<div><label data-i18n="custom_power_pct">Custom Power (%)</label><input id="jbc_select" name="select" type="number" min="10" max="100"></div>
<div><label data-i18n="delay_work_s">Delay Work (s)</label><input id="jbc_delay_work" name="delay_work" type="number" min="0" max="3600"></div>
<div><label data-i18n="delay_stand_s">Delay Stand (s)</label><input id="jbc_delay_stand" name="delay_stand" type="number" min="0" max="3600"></div>
<div><label data-i18n="stand_intakes">Stand Intakes</label><select id="jbc_stand" name="stand_intakes"><option value="1">on</option><option value="0">off</option></select></div>
<div><label data-i18n="continuous">Continuous</label><select id="jbc_cont" name="continuous"><option value="0">off</option><option value="1">on</option></select></div>
<div><label data-i18n="afterrun_power_enabled">Afterrun Power</label><select id="afterrun_power_enabled" name="afterrun_power_enabled"><option value="0">off</option><option value="1">on</option></select></div>
<div><label data-i18n="afterrun_power_pct">Afterrun Power (%)</label><input id="afterrun_power" name="afterrun_power" type="number" min="10" max="100"></div>
</div>
<div class="settings-save-state"><span id="jbc_save" class="muted"></span></div>
</form>
</section>
</section>
<section class="control-panel routing-panel">
<div class="panel-heading"><div><h2 data-i18n="input_routing">Input Routing</h2><p data-i18n="routing_hint">Choose an available input and the output it should control.</p></div><span id="route_summary" class="route-summary">-</span></div>
<div class="rule-head"><span data-i18n="rule">Rule</span><span data-i18n="when_input">When input is on</span><span></span><span data-i18n="then_output">Then control output</span></div>
<div id="rule_box"></div><button class="secondary route-add" onclick="addRule()">+ <span data-i18n="add_rule">Add rule</span></button><div id="logic_summary" class="logic-summary"></div>
</section><section class="row"><div id="details" class="grid"></div></section>
<section class="row"><button class="secondary" onclick="scan()" data-i18n="scan_modules">Scan Modules</button><button class="secondary" onclick="addressBus()" data-i18n="address_bus">Address Bus</button></section>
<section class="row table-wrap"><table><thead><tr><th>Addr</th><th data-i18n="role">Role</th><th data-i18n="type">Type</th><th data-i18n="name">Name</th><th>FW</th><th data-i18n="serial_number">UID</th><th data-i18n="state">State</th><th class="dev-only">Heap</th><th data-i18n="cpu_load">CPU</th><th data-i18n="uptime">Uptime</th><th data-i18n="offline_events">Offline Events</th><th class="dev-only" data-i18n="miss_counter">Miss total / live</th><th data-i18n="comm_quality">Bus quality</th><th data-i18n="action">Action</th></tr></thead><tbody id="mt"></tbody></table></section>
<p class="muted"><span data-i18n="state_refresh">State refreshes automatically from</span> <code>/state</code>.</p>
<div class="footer"><span>Open Fume Extractor</span><span>Master FW <span id="footer_fw">-</span></span></div>
</main><script>
const I18N={
en:{diagnostics:'Bus Diagnostics',alarm_center:'Alarm Center',no_active_alarms:'No active alarms',all_systems_ok:'All connected systems are operating normally',active_alarms:'active alarms',module_offline:'Module offline',station_disconnected:'JBC station disconnected',weller_disconnected:'Weller link disconnected',main_input_missing:'No main extractor input selected',main_output_missing:'No main extractor output selected',extractor_state:'Extraction',extraction_active:'Extraction active',ready:'Ready',not_ready:'Not ready',system:'System',status_overview:'Status Overview',status_intro:'Live status, modules and controls for Open Fume Extractor.',wifi:'Network Setup',updates:'Updates',refresh:'Refresh',restart:'Restart',wifi_signal:'WiFi Signal',uptime:'Uptime',heap_free:'Free Heap',cpu_load:'CPU',output:'Output',requested_power:'Requested Power',afterrun:'Afterrun',modules:'Modules',roles:'Routing',suction_level:'Suction Level',custom_power:'Custom Power',delay_work:'Delay Work',error:'Error',output_feedback:'Output Feedback',fan_rpm:'Fan RPM',output_fault:'Output Fault',master_system:'Master system',extraction_status:'Extraction status',connections_settings:'Connections and settings',main_signal_path:'Main signal path',main_signal_hint:'Choose what starts the extractor and which module provides its main output.',main_input:'Main input',main_signal:'Main path',additional_rules:'Additional rules',all_jbc:'All JBC modules',main_output:'Main extractor output',main_output_hint:'Select which module controls the extractor.',save_output:'Save Output',extractor_settings:'Extractor Settings',extractor_settings_hint:'Operating mode, power and afterrun.',custom_power_pct:'Custom Power (%)',delay_work_s:'Delay Work (s)',delay_stand_s:'Delay Stand (s)',stand_intakes:'Stand Intakes',save_settings:'Save Settings',input_routing:'Input Routing',routing_hint:'Choose an available input and the output it should control.',station_controls:'Station signals control extractor',then_output:'Then control output',jbc_controls:'JBC station controls extractor',scan_modules:'Scan Modules',address_bus:'Address Bus',role:'Role',type:'Type',name:'Name',serial_number:'UID',state:'State',offline_events:'Offline Events',state_refresh:'State refreshes automatically from',high:'High',medium:'Medium',low:'Low',custom:'Custom',on:'on',off:'off',auto:'Auto',jbc_inputs:'JBC Inputs',output_label:'Output',no_modules:'No modules',jbc_input:'JBC input',online:'online',offline:'offline',saving:'Saving...',saved:'Saved',active_routes:'Active routes',input_state:'Input state',connection_failed:'Connection failed',restart_confirm:'Restart master',rule:'Rule',when_input:'When input is on',controls:'controls',target_output:'Output',no_input:'No input',jbc_work_signal:'JBC work signal',no_output:'No output',extractor_output:'Extractor output',link:'Link',station:'Station',flags:'Flags',raw_flags:'Raw flags',device_id:'Device ID',fault:'Fault',fan:'Fan',light:'Light',reset_filter:'Reset Filter',filter:'Filter',runtime:'Runtime',filter_runtime:'Filter runtime',speed:'Speed',very_good:'very good',change_soon:'change soon',change_filter:'change filter',connected:'Connected',continuous:'Continuous',event:'Event',changed:'Changed',select_time:'Select time',brightness:'Brightness',language:'Language',theme:'Theme',screensaver:'Idle mode',disabled:'Disabled',minute:'minute',minutes:'minutes',apply:'Apply',hour:'hour',hours:'hours',day:'day',days:'days',datetime:'Date/Time',action:'Action',reboot_module:'Reboot',miss_counter:'Miss total / live',comm_quality:'Bus quality',last_timeout:'Last timeout',never:'never',comm_ok:'RS485 OK',comm_miss:'RS485 miss',comm_instable:'RS485 unstable',comm_offline:'RS485 offline',add_rule:'Add rule',remove:'Remove',filter_calibration:'Filter calibration',filter_calibration_hint:'Open only when a pressure sensor is installed in the air path.',pressure:'Pressure',pressure_sensor:'Pressure sensor',learn_zero:'Learn zero',learn_clean:'Learn clean',set_warn_full:'Set warn / full',advanced_thresholds:'Advanced thresholds',sensor_off:'Sensor off',sensor_ok:'Sensor OK',sensor_missing:'Sensor missing',calibrated:'calibrated',incomplete:'incomplete',not_calibrated:'not calibrated',enable_sensor:'Enable sensor',afterrun_power_enabled:'Afterrun Power',afterrun_power_pct:'Afterrun Power (%)',developer_mode:'Developer mode',main_input_alarm:'Main extractor input',logic_definition:'Logic definition',logic_blocks:'Blocks',logic_inputs:'Inputs',logic_outputs:'Outputs',logic_edit:'Edit',logic_empty:'No boolean definition stored',logic_status:'Status',logic_enabled:'enabled',logic_disabled:'disabled',logic_links:'Links',logic_sim_inputs:'simulation inputs',logic_real_inputs:'real inputs',logic_none:'none',logic_types:'Logic blocks',logic_slot:'Slot',logic_selected:'selected in editor',and_label:'AND',or_label:'OR',not_label:'NOT'},
de:{diagnostics:'Bus Diagnose',alarm_center:'Alarmzentrale',no_active_alarms:'Keine aktiven Alarme',all_systems_ok:'Alle verbundenen Systeme arbeiten normal',active_alarms:'aktive Alarme',module_offline:'Modul offline',station_disconnected:'JBC-Station getrennt',weller_disconnected:'Weller-Verbindung getrennt',main_input_missing:'Kein Haupteingang Absaugung gewählt',main_output_missing:'Kein Hauptausgang Absaugung gewählt',extractor_state:'Absaugung',extraction_active:'Absaugung aktiv',ready:'Bereit',not_ready:'Nicht bereit',system:'System',status_overview:'Statusübersicht',status_intro:'Live-Status, Module und Steuerung des Open Fume Extractor.',wifi:'Netzwerk Setup',updates:'Updates',refresh:'Aktualisieren',restart:'Neustart',wifi_signal:'WLAN Signal',uptime:'Laufzeit',heap_free:'Freier Heap',cpu_load:'CPU',output:'Ausgang',requested_power:'Angeforderte Leistung',afterrun:'Nachlauf',modules:'Module',roles:'Signalwege',suction_level:'Absaugstufe',custom_power:'Benutzerleistung',delay_work:'Nachlauf Work',error:'Fehler',output_feedback:'Ausgang Rückmeldung',fan_rpm:'Lüfter RPM',output_fault:'Ausgang Fehler',master_system:'Master-System',extraction_status:'Absaugstatus',connections_settings:'Verbindungen und Einstellungen',main_signal_path:'Hauptsignalweg',main_signal_hint:'Wählt, was die Absaugung startet und welches Modul den Hauptausgang bereitstellt.',main_input:'Haupteingang',main_signal:'Hauptsignalweg',additional_rules:'Zusatzregeln',all_jbc:'Alle JBC-Module',main_output:'Hauptausgang Absaugung',main_output_hint:'Wählt das Modul für den Hauptausgang der Absaugung.',save_output:'Ausgang speichern',extractor_settings:'Absaugung Einstellungen',extractor_settings_hint:'Betriebsart, Leistung und Nachlauf einstellen.',custom_power_pct:'Benutzerleistung (%)',delay_work_s:'Nachlauf Work (s)',delay_stand_s:'Nachlauf Stand (s)',stand_intakes:'Stand Intakes',save_settings:'Einstellungen speichern',input_routing:'Eingang Routing',routing_hint:'Verfügbaren Eingang wählen und dem gewünschten Ausgang zuordnen.',station_controls:'Stationssignale steuern Absaugung',then_output:'Dann Ausgang steuern',jbc_controls:'JBC Station steuert Absaugung',scan_modules:'Module scannen',address_bus:'Adressvergabe',role:'Rolle',type:'Typ',name:'Name',serial_number:'Seriennummer',state:'Status',offline_events:'Offline Events',state_refresh:'Status aktualisiert automatisch von',high:'Hoch',medium:'Mittel',low:'Niedrig',custom:'Benutzer',on:'an',off:'aus',auto:'Auto',jbc_inputs:'JBC Eingänge',output_label:'Ausgang',no_modules:'Keine Module',jbc_input:'JBC Eingang',online:'online',offline:'offline',saving:'Speichern...',saved:'Gespeichert',active_routes:'Aktive Signalwege',input_state:'Eingangsstatus',connection_failed:'Verbindung fehlgeschlagen',restart_confirm:'Master wirklich neu starten',rule:'Regel',when_input:'Wenn Eingang an ist',controls:'steuert',target_output:'Ausgang',no_input:'Kein Eingang',jbc_work_signal:'JBC Work Signal',no_output:'Kein Ausgang',extractor_output:'Absaugung Hauptausgang',link:'Verbindung',station:'Station',flags:'Flags',raw_flags:'Raw Flags',device_id:'Device ID',fault:'Fehler',fan:'Lüfter',light:'Licht',reset_filter:'Filter reset',filter:'Filter',runtime:'Laufzeit',filter_runtime:'Filterlaufzeit',speed:'Drehzahl',very_good:'sehr gut',change_soon:'bald wechseln',change_filter:'Filter wechseln',connected:'Verbunden',continuous:'Continuous',event:'Event',changed:'Geändert',select_time:'Zeit wählen',brightness:'Helligkeit',language:'Sprache',theme:'Farbschema',screensaver:'Ruhemodus',disabled:'Aus',minute:'Minute',minutes:'Minuten',apply:'Übernehmen',hour:'Stunde',hours:'Stunden',day:'Tag',days:'Tage',datetime:'Datum/Zeit',action:'Aktion',reboot_module:'Neustart',miss_counter:'Miss Historie / live',comm_quality:'Busqualität',last_timeout:'Letzter Timeout',never:'nie',comm_ok:'RS485 OK',comm_miss:'RS485 Miss',comm_instable:'RS485 instabil',comm_offline:'RS485 offline',add_rule:'Regel hinzufügen',remove:'Entfernen',filter_calibration:'Filter kalibrieren',filter_calibration_hint:'Nur öffnen, wenn ein Drucksensor im Luftkanal verbaut ist.',pressure:'Druck',pressure_sensor:'Drucksensor',learn_zero:'Zero lernen',learn_clean:'Clean lernen',set_warn_full:'Warn / Full setzen',advanced_thresholds:'Erweiterte Schwellenwerte',sensor_off:'Sensor aus',sensor_ok:'Sensor OK',sensor_missing:'Sensor fehlt',calibrated:'kalibriert',incomplete:'unvollständig',not_calibrated:'nicht kalibriert',enable_sensor:'Sensor aktivieren',afterrun_power_enabled:'Nachlaufleistung',afterrun_power_pct:'Nachlaufleistung (%)',developer_mode:'Entwicklermodus',main_input_alarm:'Haupteingang Absaugung',logic_definition:'Boolesche Definition',logic_blocks:'Bausteine',logic_inputs:'Eingänge',logic_outputs:'Ausgänge',logic_edit:'Bearbeiten',logic_empty:'Keine boolesche Definition gespeichert',logic_status:'Status',logic_enabled:'aktiv',logic_disabled:'inaktiv',logic_links:'Verbindungen',logic_sim_inputs:'Simulationseingänge',logic_real_inputs:'echte Eingänge',logic_none:'keine',logic_types:'Logikbausteine',logic_slot:'Slot',logic_selected:'im Editor ausgewählt',and_label:'UND',or_label:'ODER',not_label:'NICHT'}
};
let uiLang='de';
let devMode=localStorage.getItem('ofe_dev_mode')==='1';
function t(k){return (I18N[uiLang]&&I18N[uiLang][k])||I18N.en[k]||k}
function applyDevMode(){document.body.classList.toggle('dev-mode',devMode);let b=document.getElementById('dev_btn');if(b)b.textContent=(devMode?'✓ ':'')+t('developer_mode')}
async function toggleDevMode(){let next=!devMode,body=new URLSearchParams();body.set('enabled',next?'1':'0');if(next){let pw=prompt(uiLang==='de'?'Entwicklerpasswort':'Developer password');if(pw===null)return;body.set('password',pw)}try{let r=await fetch('/developer/mode',{method:'POST',body,cache:'no-store'});if(!r.ok)throw await r.text();let j=await r.json().catch(()=>({enabled:next}));devMode=!!j.enabled;localStorage.setItem('ofe_dev_mode',devMode?'1':'0');applyDevMode();load(true)}catch(e){alert((uiLang==='de'?'Entwicklermodus: ':'Developer mode: ')+e)}}
function applyLang(){document.querySelectorAll('[data-i18n]').forEach(e=>e.textContent=t(e.dataset.i18n));applyDevMode();let ls=document.getElementById('lang_sel');if(ls)ls.value=uiLang;let js=document.getElementById('jbc_suction');if(js&&!fieldBusy(js)){let v=js.value;js.innerHTML=`<option value="0">${t('high')} (100%)</option><option value="1">${t('medium')} (60%)</option><option value="2">${t('low')} (30%)</option><option value="3">${t('custom')}</option>`;js.value=v}let st=document.getElementById('jbc_stand');if(st&&!fieldBusy(st)){let v=st.value;st.innerHTML=`<option value="1">${t('on')}</option><option value="0">${t('off')}</option>`;st.value=v}let co=document.getElementById('jbc_cont');if(co&&!fieldBusy(co)){let v=co.value;co.innerHTML=`<option value="0">${t('off')}</option><option value="1">${t('on')}</option>`;co.value=v}let apen=document.getElementById('afterrun_power_enabled');if(apen&&!fieldBusy(apen)){let v=apen.value;apen.innerHTML=`<option value="0">${t('off')}</option><option value="1">${t('on')}</option>`;apen.value=v}}
async function setLang(lang){let sel=document.getElementById('lang_sel');if(sel)clearFieldDirty(sel);uiLang=lang=='en'?'en':'de';applyLang();routingSig='';detailsSig='';await fetch('/language/set?lang='+uiLang,{method:'POST',cache:'no-store'});if(sel)clearFieldDirty(sel);setTimeout(()=>{load();if(sel)clearFieldDirty(sel)},120)}
function hx(n){return '0x'+Number(n||0).toString(16).toUpperCase().padStart(2,'0')}
function mn(m){return m.label||m.display_name||m.name||m.type_name||hx(m.addr)}
function amn(m){return hx(m.addr)+' '+mn(m)}
function set(id,t){let e=document.getElementById(id);if(e){e.textContent=t;if(id.startsWith('jbu_stationname_')){e.tabIndex=0;e.setAttribute('role','button');e.title=uiLang=='de'?'Stationsname bearbeiten':'Edit station name'}}}
function isFormField(e){return e&&e.matches&&e.matches('input,select,textarea')}
function fieldBusy(e){return !!(e&&(document.activeElement===e||e.dataset.dirty==='1'||Date.now()<Number(e.dataset.holdUntil||0)))}
function markFieldDirty(e,ms=120000){if(isFormField(e)&&!e.dataset.noDirty){e.dataset.dirty='1';e.dataset.holdUntil=String(Date.now()+ms);e.classList.add('is-dirty');let a=universalAddrFromField(e);if(a)universalDirtyBadge(a)}}
function clearFieldDirty(e){if(e){let a=universalAddrFromField(e);delete e.dataset.dirty;delete e.dataset.holdUntil;e.classList.remove('is-dirty');if(a)setTimeout(()=>universalDirtyBadge(a),0)}}
function clearDirtyIn(root){let r=typeof root==='string'?document.querySelector(root):root;if(r)r.querySelectorAll('input,select,textarea').forEach(clearFieldDirty)}
function val(id,v,force=false){let e=typeof id==='string'?document.getElementById(id):id;if(!e)return;if(!force&&fieldBusy(e))return;let sv=String(v??'');if(e.value!==sv)e.value=sv;e.dataset.saved=sv;if(force)clearFieldDirty(e)}
document.addEventListener('input',e=>markFieldDirty(e.target),true);
document.addEventListener('change',e=>markFieldDirty(e.target),true);
function kb(v){return Math.round(Number(v||0)/1024)+' KB'}
function up(ms){let s=Math.floor(Number(ms||0)/1000),d=Math.floor(s/86400);s%=86400;let h=Math.floor(s/3600);s%=3600;let m=Math.floor(s/60);let a=[];if(d)a.push(d+'d');if(h||a.length)a.push(h+'h');a.push(m+'m');return a.join(' ')}
function ups(sec){return up(Number(sec||0)*1000)}
function cmdHex(v){return Number(v||0)?('0x'+Number(v||0).toString(16).toUpperCase().padStart(2,'0')):'-'}
function agoMs(ms,now){if(!Number(ms||0))return t('never');let a=Math.max(0,Number(now||0)-Number(ms||0));return a<60000?Math.round(a/1000)+'s':ups(Math.round(a/1000))}
function commClass(m){let q=Number(m.comm_quality||0);return q==0?'on':(q==1?'warn':'off')}
function commTransport(m){return m&&m.transport==='wifi'?(uiLang==='de'?'WLAN':'WiFi'):'RS485'}
function commText(m){let q=Number(m.comm_quality||0),p=commTransport(m);return q==0?p+' OK':(q==1?p+' '+(uiLang==='de'?'Miss':'miss'):(q==2?p+' '+(uiLang==='de'?'instabil':'unstable'):p+' '+t('offline')))}
function commTitle(m,d){return t('last_timeout')+': '+agoMs(m.last_timeout_ms,d&&d.uptime_ms)+' / '+cmdHex(m.last_timeout_cmd)}
function wifiPct(r){r=Number(r||0);if(r>=-50)return 100;if(r<=-90)return 0;return Math.round((r+90)*2.5)}
function wifiSet(d){let ok=!!d.wifi_connected,r=Number(d.wifi_rssi||0),p=ok?wifiPct(r):0;document.getElementById('wifi_fill').style.width=p+'%';set('wifi_db',ok?(r+' dBm'):'-');set('wifi_ssid',ok?(d.wifi_ssid||'WiFi'):'AP / offline')}
function bit(n,b){return (Number(n||0)&(1<<b))!=0}
function pill(id,on){let e=document.getElementById(id);if(e){e.textContent=on?t('on'):t('off');e.className='pill '+(on?'on':'')}}
function pillNA(id){let e=document.getElementById(id);if(e){e.textContent='-';e.className='pill'}}
function pillText(id,text,kind){let e=document.getElementById(id);if(e){e.textContent=text;e.className='pill '+(kind||'')}}
function faultText(v,type){v=Number(v||0);if(!v)return'OK';let de=uiLang=='de',w=Number(type||0)==5,ub=Number(type||0)==7||Number(type||0)==8,a=[];if(v&1)a.push(w?(de?'Weller Gerätebus Fehler':'Weller device bus error'):(ub?(de?'Lokaler Gerätebus inaktiv':'Local device bus inactive'):(de?'Drehzahlrückmeldung fehlt':'No speed feedback')));if(v&2)a.push(de?'Filterwarnung':'Filter warn');if(v&4)a.push(de?'Filter voll':'Filter full');if(v&8)a.push(de?'Filter fehlt':'Filter missing');if(v&0x10)a.push(de?'Sensorfehler':'Sensor fault');if(v&0x100)a.push(de?'Drehzahlrückmeldung fehlt':'No speed feedback');if(v&0x200)a.push(de?'Master Timeout':'Master timeout');if(v&0x400)a.push(de?'Drehzahl zu niedrig':'Low RPM');let rest=v&~0x71F;if(rest)a.push(hx(rest));return [...new Set(a)].join(', ')+' ('+hx(v)+')'}
function jbcErrorText(v){v=Number(v||0);if(!v)return'OK';let de=uiLang=='de',a=[];if(v&1)a.push(de?'Filterlaufzeit abgelaufen':'Filter lifetime expired');if(v&2)a.push(de?'Filterlaufzeit endet bald':'Filter lifetime ending');if(v&4)a.push(de?'Filter verstopft':'Filter clogged');if(v&8)a.push(de?'Filter fast verstopft':'Filter almost clogged');if(v&16)a.push(de?'Kein Filter':'No filter');if(v&32)a.push(de?'Abdeckung offen':'Cover open');if(v&64)a.push(de?'Lüfter defekt':'Blower damaged');if(v&256)a.push(de?'Ventilfehler':'Valve error');if(v&512)a.push(de?'Aux Überstrom':'Aux overcurrent');if(v&1024)a.push(de?'Pedalfehler':'Pedal error');if(v&2048)a.push(de?'FAE Systemfehler':'FAE system error');if(v&4096)a.push(de?'FAE Systemfehler 2':'FAE system error 2');let rest=v&~0x1F7F;if(rest)a.push(hx(rest));return a.join(', ')+' ('+hx(v)+')'}
function escHtml(v){return String(v||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function escAttr(v){return escHtml(v)}
function renderAlarms(d){let alarms=Array.isArray(d.alarm_items)?d.alarm_items:[];if(!alarms.length&&Number(d.alarm_count||0)>0&&Array.isArray(d.alarms)){alarms=d.alarms.map(x=>{let p=String(x||''),i=p.indexOf(': ');return{critical:false,title:i>=0?p.slice(0,i):p,detail:i>=0?p.slice(i+2):''}})}let count=document.getElementById('alarm_count'),list=document.getElementById('alarm_list');count.textContent=alarms.length;count.className='alarm-count '+(alarms.length?'active':'');if(!alarms.length){list.innerHTML=`<div class="alarm-item ok"><strong>${t('no_active_alarms')}</strong><span>${t('all_systems_ok')}</span></div>`;return}list.innerHTML=alarms.map(a=>`<div class="alarm-item ${a.critical?'critical':''}"><strong>${escHtml(a.title)}</strong><span>${escHtml(a.detail)}</span></div>`).join('')}
function maskState(v){return Number(v||0)?t('on'):t('off')}
function jbcFaeStandActive(m){return !!(m&&m.online&&Number(m.station_addr||0)&&(Number(m.jbc_link_flags||0)&1)&&!Number(m.jbc_work_mask||0))}
function suctionName(n){n=Number(n||0);return n==0?t('high'):(n==1?t('medium'):(n==2?t('low'):t('custom')))}
function customPower(v){v=Number(v);if(!Number.isFinite(v)||v<=0)v=100;if(v>=100)v=Math.round(v/10);return Math.max(10,Math.min(100,v))}
function universalDefsForModule(m){
  let cache=universalDescriptorCache[m.addr];
  let text=(m.universal_descriptor&&m.universal_descriptor.length)?m.universal_descriptor:(cache?cache.text:'');
  text=universalNormalizeDescriptorText(text);
  let rawDefs=universalEnsureProfileControls(m,universalParseDescriptor(text||''));
  let byId=new Map();
  rawDefs.forEach(e=>byId.set(Number(e.id),e));
  if(Array.isArray(m.universal_entity_defs)&&m.universal_entity_defs.length){
    m.universal_entity_defs.forEach(e=>{
      e.meta=e.meta||{};
      let id=Number(e.id),r=byId.get(id);
      if(r){
        e.meta={...(r.meta||{}),...(e.meta||{})};
        if((!e.mode||e.mode==='ro')&&r.mode)e.mode=r.mode;
        if(!e.type&&r.type)e.type=r.type;
        if(!e.key&&r.key)e.key=r.key;
      }
      if(!e.label)e.label=(uiLang=='de'&&(e.meta.de||'').length)?e.meta.de:(e.meta.en||e.key||('Entity '+e.id));
      byId.set(id,universalNormalizeDef(e));
    });
  }
  return Array.from(byId.values()).map(universalNormalizeDef).sort((a,b)=>Number(a.meta.profile_index||a.meta.idx||a.id)-Number(b.meta.profile_index||b.meta.idx||b.id));
}
function universalEntityControlLabel(m,e){return amn(m)+' - '+escHtml(e.label||e.key||('Entity '+e.id))}
function universalIsOutputPower(e){let role=String(e.meta&&e.meta.role||'').toLowerCase();return e.type=='number'&&(role=='main_output_power'||role=='output_power')}
function universalIsOutputEnable(e){let role=String(e.meta&&e.meta.role||'').toLowerCase();return e.type=='switch'&&(role=='main_output_enable'||role=='output_enable'||role=='output')}
function universalOutputDefs(m){let defs=universalDefsForModule(m);return defs.filter(e=>universalIsOutputPower(e)||universalIsOutputEnable(e))}
function universalOutputMin(m){let defs=universalDefsForModule(m),p=defs.find(e=>e.type=='number'&&String(e.meta&&e.meta.role||'').toLowerCase()=='main_output_power')||defs.find(e=>universalIsOutputPower(e));if(p){let n=Number(p.meta&&p.meta.min);if(Number.isFinite(n)&&n>0&&n<=100)return n}return 10}
function universalInputDefs(m){return universalDefsForModule(m).filter(e=>Number(e.id)>=20&&(e.type=='binary_sensor'||e.type=='switch')&&String(e.mode||'').indexOf('r')>=0)}
function universalTargetDefs(m){return universalDefsForModule(m).filter(e=>Number(e.id)>=20&&(e.type=='switch'||e.type=='button')&&String(e.mode||'').indexOf('w')>=0)}
function mainOutputMin(d){let m=(d.modules||[]).find(x=>Number(x.addr)===Number(d.active_output_addr)&&x.online);if(!m)return 10;if((m.type==7||m.type==8))return universalOutputMin(m);return m&&(m.caps&512)?30:10}
function mainInputAvailable(d){
  let t=Number(d.main_input_source_type||0),a=Number(d.main_input_source_addr||0),b=Number(d.main_input_source_bit||0),mods=d.modules||[];
  if(!t)return false;
  if(t===1){
    if(!a)return Number(d.jbc_inputs_count||0)>0;
    return mods.some(m=>Number(m.addr)===a&&m.online&&(Number(m.caps||0)&1));
  }
  if(t===2)return mods.some(m=>Number(m.addr)===a&&m.online&&(Number(m.caps||0)&2048));
  if(t===3){
    let m=mods.find(x=>Number(x.addr)===a&&x.online);
    return !!(m&&universalInputDefs(m).some(e=>Number(e.id)===b));
  }
  return false;
}function dhm(min){min=Number(min||0);let d=Math.floor(min/1440),h=Math.floor((min%1440)/60),m=min%60;let a=[];if(d)a.push(d+'d');if(h)a.push(h+'h');if(m||!a.length)a.push(m+'m');return a.join(' ')}
function hexAscii(s){if(!s)return '-';let out='';for(let i=0;i+1<s.length;i+=2){let c=parseInt(s.substr(i,2),16);out+=c>=32&&c<127?String.fromCharCode(c):'.'}return out.trim()||'-'}
function jbcFlags(n){n=Number(n||0);let a=[];if(n&1)a.push(t('connected'));if(n&2)a.push(t('continuous'));if(n&4)a.push(t('error'));if(n&8)a.push(t('event'));if(n&16)a.push(t('changed'));return a.length?a.join(', '):'-'}
function ruleAvailable(d,r){if(!r||!r.enabled)return false;let src=r.source_type==1?(r.source_addr?(d.modules||[]).some(m=>m.online&&m.addr==r.source_addr&&((Number(m.caps||0)&16777217)!=0)):Number(d.jbc_inputs_count||0)>0):(r.source_type==2&&(d.modules||[]).some(m=>m.online&&m.addr==r.source_addr&&(m.caps&2048)!=0));if(r.source_type==3)src=(d.modules||[]).some(m=>m.online&&m.addr==r.source_addr&&(m.type==7||m.type==8)&&universalInputDefs(m).some(e=>Number(e.id)==Number(r.source_bit)));let tgt=r.target_type==1||r.target_type==4||(r.target_type==2&&(d.modules||[]).some(m=>m.online&&m.addr==r.target_addr&&(m.caps&16384)!=0));if(r.target_type==3)tgt=(d.modules||[]).some(m=>m.online&&m.addr==r.target_addr&&(m.type==7||m.type==8)&&universalTargetDefs(m).some(e=>Number(e.id)==Number(r.target_bit)));return src&&tgt}
function availableRouteCount(d){return (d.input_rules||[]).filter(r=>ruleAvailable(d,r)).length}
function ioInputLabel(m,bit){bit=Number(bit||0);let a=m?(bit?m.io_in2_alias:m.io_in1_alias):'';return a&&a.length?a:('IN'+(bit+1))}
function ioMainOutputLabel(m){let a=m&&(m.io_main_alias||'');return a&&a.length?a:'Relais / Fan'}
function ioOutputLabel(m,bit){bit=Number(bit||0);if(m&&Number(m.type)==5)return bit?t('light'):t('fan');let a=m?(bit?m.io_out2_alias:m.io_out1_alias):'';if(a&&a.length)return a;if(bit==0)return 'OUT1';if(bit==1)return 'OUT2';return 'OUT'+(bit+1)}
function ioAliasInput(m,key,fallback){let v=key=='main'?(m.io_main_alias||''):(key=='in1'?(m.io_in1_alias||''):(key=='in2'?(m.io_in2_alias||''):(key=='out1'?(m.io_out1_alias||''):(m.io_out2_alias||''))));return `<input class="inline-alias" data-uid="${m.uid}" data-addr="${m.addr}" data-key="${key}" data-saved="${escAttr(v)}" value="${escAttr(v)}" placeholder="${escAttr(fallback)}" onchange="saveIoAlias(this)">`}
function ioTargetLabel(m,bit){let label=ioOutputLabel(m,bit);return m?hx(m.addr)+' '+label:label}
function inputRoleWord(){return uiLang=='de'?'Eingang':'Input'}
function mainInputText(d){
  let mods=d.modules||[],tpe=Number(d.main_input_source_type||0),addr=Number(d.main_input_source_addr||0),bit=Number(d.main_input_source_bit||0);
  if(!tpe)return t('no_input');
  if(tpe==1){
    if(!addr)return Number(d.jbc_inputs_count||0)?(t('all_jbc')+' ('+Number(d.jbc_inputs_count)+')'):(t('no_input')+' ('+t('all_jbc')+')');
    let j=mods.find(m=>Number(m.addr)==addr&&(Number(m.caps||0)&1));
    return (j&&j.online)?amn(j):(t('no_input')+' ('+hx(addr)+' JBC)');
  }
  if(tpe==2){
    let m=mods.find(x=>Number(x.addr)==addr),label=m?ioInputLabel(m,bit):ioInputLabel(null,bit);
    return (m&&m.online)?(amn(m)+' - '+label):(t('no_input')+' ('+hx(addr)+' '+label+')');
  }
  if(tpe==3){
    let m=mods.find(x=>Number(x.addr)==addr),e=m?universalInputDefs(m).find(x=>Number(x.id)==bit):null,label=e?(e.label||e.key):('Entity '+bit);
    return (m&&m.online)?(amn(m)+' - '+label):(t('no_input')+' ('+hx(addr)+' '+label+')');
  }
  return t('no_input')
}
function mainOutputText(d,forAuto=false){
  let mods=d.modules||[],auto=Number(d.auto_output_addr||0),preferred=Number(d.preferred_output_addr||0);
  if(forAuto||!preferred){
    if(auto){let m=mods.find(x=>Number(x.addr)==auto&&x.online);return t('auto')+' ('+(m?amn(m):hx(auto))+')'}
    return t('auto')+' ('+t('no_output')+')'
  }
  let m=mods.find(x=>Number(x.addr)==preferred);
  let provides=m&&m.online&&(((Number(m.caps||0)&36)!=0)||((Number(m.type)==7||Number(m.type)==8)&&universalOutputDefs(m).length));
  return provides?amn(m):(t('no_output')+' ('+(m?amn(m):hx(preferred))+')')
}
function activeLogicRouteText(){let defs=Array.isArray(logicSummaryCache)?logicSummaryCache.filter(g=>g&&g.enabled):[];let label=uiLang=='de'?'Boolesche Definition':'Boolean definition',count=defs.length;if(!count)return '<br>'+label+': 0';let names=defs.map(g=>String(g.name||'').trim()).filter(Boolean),shown=names.slice(0,2).map(escHtml).join(', '),more=Math.max(0,names.length-2),suffix=String(count);if(shown){suffix+=' ('+shown;if(more)suffix+=' +'+more;suffix+=')'}return '<br>'+label+': '+suffix}function outputRoleText(d){return t('main_signal')+': '+mainInputText(d)+' &rarr; '+mainOutputText(d)+'<br>'+t('additional_rules')+': '+availableRouteCount(d)+activeLogicRouteText()}
function extractorActionLabel(a){a=Number(a||0);let de=uiLang=='de';return ({1:de?'Nächste Absaugstufe':'Cycle suction level',2:de?'Vorherige Absaugstufe':'Previous suction level',3:de?'Absaugstufe Hoch':'Suction level High',4:de?'Absaugstufe Mittel':'Suction level Medium',5:de?'Absaugstufe Niedrig':'Suction level Low',6:de?'Absaugstufe Benutzer':'Suction level Custom',7:de?'Benutzerleistung +1 %':'Custom power +1%',8:de?'Benutzerleistung -1 %':'Custom power -1%',9:de?'Benutzerleistung +10 %':'Custom power +10%',10:de?'Benutzerleistung -10 %':'Custom power -10%'})[a]||(de?'Absaugungsaktion':'Extractor action')}
function ruleLabel(d,r,side){let mods=(d&&d.modules)||window.lastState&&window.lastState.modules||[];if(side=='source'){if(r.source_type==1)return r.source_addr?(hx(r.source_addr)+' JBC'):t('all_jbc');if(r.source_type==2){let m=mods.find(x=>x.addr==r.source_addr);return hx(r.source_addr)+' '+ioInputLabel(m,r.source_bit)}if(r.source_type==3){let m=mods.find(x=>x.addr==r.source_addr),e=m?universalInputDefs(m).find(x=>Number(x.id)==Number(r.source_bit)):null;return hx(r.source_addr)+' '+(e?(e.label||e.key):('Entity '+r.source_bit))}return t('no_input')}if(r.target_type==1)return t('extractor_output');if(r.target_type==2){let m=mods.find(x=>x.addr==r.target_addr);return ioTargetLabel(m,r.target_bit)}if(r.target_type==3){let m=mods.find(x=>x.addr==r.target_addr),e=m?universalTargetDefs(m).find(x=>Number(x.id)==Number(r.target_bit)):null;return hx(r.target_addr)+' '+(e?(e.label||e.key):('Entity '+r.target_bit))}if(r.target_type==4)return extractorActionLabel(r.target_bit);return t('no_output')}
function moduleRoleText(d,m){let a=[];if(Number(d.main_input_source_type)==1&&((!Number(d.main_input_source_addr)&&((Number(m.caps||0)&16777217)||Number(m.type)==1||Number(m.type)==9))||Number(d.main_input_source_addr)==Number(m.addr)))a.push(t('main_input')+(Number(d.main_input_source_addr)?'':': '+t('all_jbc')));if(Number(d.main_input_source_type)==2&&Number(d.main_input_source_addr)==Number(m.addr))a.push(t('main_input')+': '+ioInputLabel(m,d.main_input_source_bit));if(Number(d.main_input_source_type)==3&&Number(d.main_input_source_addr)==Number(m.addr)){let e=universalInputDefs(m).find(x=>Number(x.id)==Number(d.main_input_source_bit));a.push(t('main_input')+': '+(e?(e.label||e.key):('Entity '+d.main_input_source_bit)))}if(Number(d.active_output_addr)==Number(m.addr)||m.role_output)a.push(t('main_output'));(d.input_rules||[]).forEach((r,i)=>{if(!r||!r.enabled)return;let rp=t('rule')+' '+(i+1)+': ';if(r.source_type==1&&((!r.source_addr&&((Number(m.caps||0)&16777217)||Number(m.type)==1||Number(m.type)==9))||Number(r.source_addr)==Number(m.addr)))a.push(rp+inputRoleWord()+' '+ruleLabel(d,r,'source'));if(r.source_type==2&&Number(r.source_addr)==Number(m.addr))a.push(rp+inputRoleWord()+' '+ioInputLabel(m,r.source_bit));if(r.source_type==3&&Number(r.source_addr)==Number(m.addr)){let e=universalInputDefs(m).find(x=>Number(x.id)==Number(r.source_bit));a.push(rp+inputRoleWord()+' '+(e?(e.label||e.key):('Entity '+r.source_bit)))}if(r.target_type==2&&Number(r.target_addr)==Number(m.addr))a.push(rp+t('target_output')+' '+ioOutputLabel(m,r.target_bit));if(r.target_type==3&&Number(r.target_addr)==Number(m.addr)){let e=universalTargetDefs(m).find(x=>Number(x.id)==Number(r.target_bit));a.push(rp+t('target_output')+' '+(e?(e.label||e.key):('Entity '+r.target_bit)))}});if(m.role_jbc||(((Number(m.caps||0)&16777217)!=0)&&m.station_addr)){let st=m.station_addr?(((Number(m.type)==9?(m.jbc_usb_model_raw||m.jbc_usb_model||'JBC USB'):(m.station_type||'JBC')))+' '+hx(m.station_addr)):t('jbc_input');a.push(st+' / work '+maskState(m.jbc_work_mask))}return a.length?'<div class="role-lines">'+a.map(x=>'<span>'+escHtml(x)+'</span>').join('')+'</div>':'-'}function canRender(id){let el=document.getElementById(id),a=document.activeElement;if(!el)return true;if(el.querySelector('[data-dirty="1"]'))return false;if(!el.contains(a))return true;return !(a&&['SELECT','INPUT','TEXTAREA'].includes(a.tagName))}
let wellerPending={},wellerHoldUntil={},wellerFilterPending={},wellerFilterHoldUntil={},displayHoldUntil={},displaySetTimers={},moduleOutputPending={},moduleOutputHoldUntil={};
let detailsSig='';
let routingSig='';
let logicSummaryCache=null,logicSummaryLast=0;
let universalDescriptorCache={};
function wellerUiSpeed(addr,reported){let now=Date.now();if(wellerHoldUntil[addr]&&now<wellerHoldUntil[addr])return wellerPending[addr];delete wellerPending[addr];delete wellerHoldUntil[addr];return reported}
function wellerSlide(addr,value){wellerPending[addr]=Number(value);wellerHoldUntil[addr]=Date.now()+3000;let e=document.getElementById('wspd_'+addr);if(e)e.textContent=value;set('wsl_v_'+addr,value+' %')}
function fanOutputPct(addr,reported){let now=Date.now();if(moduleOutputHoldUntil[addr]&&now<moduleOutputHoldUntil[addr])return moduleOutputPending[addr];delete moduleOutputPending[addr];delete moduleOutputHoldUntil[addr];return reported}
function fanOutputSlide(addr,value){moduleOutputPending[addr]=Number(value);moduleOutputHoldUntil[addr]=Date.now()+2500;set('io_power_v_'+addr,value+' %')}
function wellerUiFilter(addr,reported){let now=Date.now();if(wellerFilterHoldUntil[addr]&&now<wellerFilterHoldUntil[addr])return wellerFilterPending[addr];delete wellerFilterPending[addr];delete wellerFilterHoldUntil[addr];return reported}
function wellerFilterOptions(current){let opts=`<option value="0">${t('select_time')}</option>`;for(let h=1;h<=23;h++){let v=h*60;opts+=`<option value="${v}"${current==v?' selected':''}>${h} ${t(h>1?'hours':'hour')}</option>`}for(let d=1;d<=6;d++){let v=d*1440;opts+=`<option value="${v}"${current==v?' selected':''}>${d} ${t(d>1?'days':'day')}</option>`}opts+=`<option value="9600"${current==9600?' selected':''}>6 ${t('days')} 16 ${t('hours')}</option>`;return opts}
function wellerFilterTouch(addr){let e=document.getElementById('wf_sel_'+addr);let total=Number(e.value||0);if(total<=0)return 0;wellerFilterPending[addr]=total;wellerFilterHoldUntil[addr]=Date.now()+5000;return total}
function btn(id,text,fn){let e=document.getElementById(id);if(e){e.textContent=text;e.onclick=fn}}
function sw(id,on,fn){let e=document.getElementById(id);if(e){e.textContent=on?t('on'):t('off');e.className='switch '+(on?'on':'off');e.title=on?t('on'):t('off');e.setAttribute('aria-label',on?t('on'):t('off'));e.onclick=fn}}
function swNA(id){let e=document.getElementById(id);if(e){e.textContent='-';e.className='switch off';e.title='-';e.setAttribute('aria-label','-');e.onclick=null}}
function iron(id,on){let e=document.getElementById(id);if(e){e.className='overview-work-symbol';e.style.setProperty('--tool-color',on?'#3bd16f':'#777');e.style.setProperty('--tool-glow',on?'0 0 12px rgba(59,209,111,.26)':'none');e.title=on?'Work active':'Work inactive';e.setAttribute('aria-label',e.title)}}
function cls(id,c){let e=document.getElementById(id);if(e)e.className=c}
function moduleSig(d){return (d.modules||[]).map(m=>[m.addr,m.type,m.online?1:0,m.fw,m.caps,m.display_name||'',m.label||'',m.io_main_alias||'',m.io_in1_alias||'',m.io_in2_alias||'',m.io_out1_alias||'',m.io_out2_alias||''].join(':')).join('|')}
function routeSig(d){return moduleSig(d)+'|'+(d.input_rules||[]).map(r=>[r.enabled,r.source_type,r.source_addr,r.source_bit,r.target_type,r.target_addr,r.target_bit].join(',')).join('|')}
function enc(t,a,b){return [t||0,a||0,b||0].join(',')}
function fanIoKind(m){return (m.type==3||(m.caps&256))?'Fan / IO Pro':'Fan / IO'}
let ofeLedClockOffsetMs=0,ofeLedClockValid=false,ofeLedRafStarted=false;
function ofeLedClockSync(masterMs,t0,t1){let a=Number(t0||0),b=Number(t1||performance.now()),rtt=Math.max(0,b-a),sample=Math.max(0,Number(masterMs||0))+(rtt*.5)-b;if(!ofeLedClockValid||Math.abs(sample-ofeLedClockOffsetMs)>250){ofeLedClockOffsetMs=sample;ofeLedClockValid=true}else ofeLedClockOffsetMs+=(sample-ofeLedClockOffsetMs)*.25}
function ofeLedNow(){return ofeLedClockValid?performance.now()+ofeLedClockOffsetMs:0}
function ofeLedEventStyle(ev){ev=Number(ev||0);switch(ev){case 1:return {c:'#00ff00',kind:'greenwhite',period:3200,n:'Bus online'};case 2:return {c:'#00ff00',kind:'solid',period:0,n:'Bus activity'};case 3:return {c:'#ff0000',kind:'breath',period:1600,n:'Bus offline'};case 4:return {c:'#ff8c00',kind:'blink',step:500,n:'Not paired'};case 5:return {c:'#0024ff',kind:'bluewhite',period:3200,n:'Firmware update'};case 6:return {c:'#ffffff',kind:'whitebreath',period:3200,n:'Device online'};case 7:return {c:'#ff0000',kind:'breath',period:1600,n:'Device offline'};case 8:return {c:'#00ff00',kind:'solid',period:0,n:'Work active'};case 9:return {c:'#0046ff',kind:'breath',period:3200,n:'Extractor on'};case 10:return {c:'#aa00ff',kind:'blink',step:650,n:'Afterrun'};case 11:return {c:'#0046ff',kind:'blink',step:500,n:'Continuous'};case 12:return {c:'#ffaa00',kind:'blink',step:500,n:'Warning'};case 13:return {c:'#ff0000',kind:'double',period:900,n:'Critical'};default:return {c:'#59616b',kind:'off',period:0,n:'Off'}}}
function ofeLedTriangle(now,period){if(!period)return 1;let p=((now%period)+period)%period;return p<period/2?(p*2/period):(2-(p*2/period))}
function ofeLedRgb(r,g,b){return 'rgb('+Math.round(r)+','+Math.round(g)+','+Math.round(b)+')'}
function ofeLedDark(e,opacity){e.style.color='#59616b';e.style.opacity=String(opacity);e.style.textShadow='none'}
function ofeLedGlow(e,c,level){let a=Math.max(0,Math.min(1,Number(level)));e.style.color=c;e.style.opacity=String(a);if(a<=.13){e.style.textShadow='0 0 1px '+c;return}let r1=(1+5*a).toFixed(1),r2=(3+11*a).toFixed(1),r3=(5+17*a).toFixed(1);e.style.textShadow='0 0 '+r1+'px '+c+',0 0 '+r2+'px '+c+',0 0 '+r3+'px '+c}
function ofeLedRenderElement(e,now){let live=e.dataset.ledLive==='1',st=ofeLedEventStyle(e.dataset.ledEvent);if(!live||st.kind==='off'){ofeLedDark(e,.58);return}if(st.kind==='solid'){ofeLedGlow(e,st.c,1);return}if(st.kind==='breath'){let w=ofeLedTriangle(now,st.period),level=(24+w*231)/255;ofeLedGlow(e,st.c,level);return}if(st.kind==='whitebreath'){let w=ofeLedTriangle(now,st.period),level=(16+w*239)/255;ofeLedGlow(e,'#ffffff',level);return}if(st.kind==='greenwhite'){let m=ofeLedTriangle(now,st.period),c=ofeLedRgb(255*m,255,255*m);ofeLedGlow(e,c,1);return}if(st.kind==='bluewhite'){let m=ofeLedTriangle(now,st.period),c=ofeLedRgb(255*m,36+(219*m),255);ofeLedGlow(e,c,1);return}if(st.kind==='blink'){let on=(Math.floor(now/st.step)&1)!==0;if(on)ofeLedGlow(e,st.c,1);else ofeLedDark(e,.12);return}if(st.kind==='double'){let p=((now%st.period)+st.period)%st.period,on=p<90||(p>=180&&p<270);if(on)ofeLedGlow(e,st.c,1);else ofeLedDark(e,.12);return}ofeLedGlow(e,st.c,1)}
function ofeLedRenderFrame(){let now=ofeLedNow();document.querySelectorAll('[data-ofe-led="1"]').forEach(e=>ofeLedRenderElement(e,now));requestAnimationFrame(ofeLedRenderFrame)}
function ofeLedEnsureRenderer(){if(ofeLedRafStarted)return;ofeLedRafStarted=true;requestAnimationFrame(ofeLedRenderFrame)}
function setLedWord(id,label,ev,valid,enabled){let e=document.getElementById(id);if(!e)return;let st=ofeLedEventStyle(ev),live=!!valid&&!!enabled&&Number(ev)>0;e.textContent=label;e.dataset.ofeLed='1';e.dataset.ledEvent=String(Number(ev||0));e.dataset.ledLive=live?'1':'0';e.classList.remove('fx-breath','fx-whitebreath','fx-greenwhite','fx-bluewhite','fx-blink','fx-double');e.classList.toggle('is-live',live);e.title=valid?(label+' · '+st.n):(label+' · '+(uiLang=='de'?'keine LED-Telemetrie':'no LED telemetry'));ofeLedRenderElement(e,ofeLedNow())}
let ledStateBusy=false;
async function loadLedState(){if(ledStateBusy)return;ledStateBusy=true;let t0=performance.now();try{let r=await fetch('/led_state',{cache:'no-store'});if(!r.ok)return;let d=await r.json(),t1=performance.now();ofeLedClockSync(d.uptime_ms,t0,t1);ofeLedEnsureRenderer();setLedWord('master_led_ofe','OFE',d.master_ofe,true,!!d.enabled);setLedWord('master_led_evt','EVT',d.master_evt,true,!!d.enabled);(d.modules||[]).forEach(m=>{setLedWord('mled_ofe_'+m.addr,'OFE',m.ofe,!!m.valid&&!!m.online,!!d.enabled);setLedWord('mled_evt_'+m.addr,'EVT',m.evt,!!m.valid&&!!m.online,!!d.enabled)})}catch(e){}finally{ledStateBusy=false}}
function moduleMeta(m){let tv=m.online&&m.telemetry_valid;let u=tv?ups(m.module_uptime_s):'-';let off=Number(m.offline_events||0),cpu=tv?Number(m.module_cpu_load_pct||0)+'%':'-',heap=tv?kb(m.module_heap_free):'-',loop=tv?Number(m.module_loop_max_ms||0)+' ms':'-',miss=Number(m.miss_total||0)+' / '+Number(m.misses||0);return `<div class="led-pair"><span id="mled_ofe_${m.addr}" class="led-word">OFE</span><span id="mled_evt_${m.addr}" class="led-word">EVT</span></div><div class="module-system"><span><small>CPU</small><strong id="mcpu_${m.addr}">${cpu}</strong></span><span class="dev-only"><small>Heap</small><strong id="mheap_${m.addr}">${heap}</strong></span><span class="dev-only"><small>Loop max</small><strong id="mloop_${m.addr}">${loop}</strong></span><span><small id="mtransport_${m.addr}">${commTransport(m)}</small><strong id="mcomm_${m.addr}" class="${commClass(m)}">${commText(m)}</strong></span></div><div class="module-footer"><span>${t('uptime')}: <b id="muptime_${m.addr}">${u}</b></span><span>${t('offline_events')}: <b id="moffline_${m.addr}">${off}</b></span><span class="dev-only">${t('miss_counter')}: <b id="mmiss_${m.addr}">${miss}</b></span></div>`}
function dec(v){let p=String(v||'0,0,0').split(',').map(x=>Number(x||0));return {t:p[0]||0,a:p[1]||0,b:p[2]||0}}
function optionHas(opts,value){return opts.indexOf(`value="${value}"`)>=0}
function sourceOptions(d,current){let opts=`<option value="0,0,0">${t('no_input')}</option>`;if(d.jbc_inputs_count){opts+=`<option value="1,0,0">${t('all_jbc')} (${d.jbc_inputs_count})</option>`;(d.modules||[]).forEach(m=>{if(m.online&&((Number(m.caps||0)&16777217)!=0))opts+=`<option value="1,${m.addr},0">${amn(m)} - JBC</option>`})}(d.modules||[]).forEach(m=>{if(m.online&&(m.caps&2048)!=0){opts+=`<option value="2,${m.addr},0">${amn(m)} - ${ioInputLabel(m,0)}</option>`;opts+=`<option value="2,${m.addr},1">${amn(m)} - ${ioInputLabel(m,1)}</option>`}if(m.online&&(m.type==7||m.type==8))universalInputDefs(m).forEach(e=>{opts+=`<option value="3,${m.addr},${e.id}">${universalEntityControlLabel(m,e)}</option>`})});if(current&&current!='0,0,0'&&!optionHas(opts,current)){let v=dec(current),tmp=Object.assign({},d,{main_input_source_type:v.t,main_input_source_addr:v.a,main_input_source_bit:v.b});opts+=`<option value="${current}">${escHtml(mainInputText(tmp))}</option>`}return opts.replace(`value="${current}"`,`value="${current}" selected`)}
function targetOptions(d,current){let opts=`<option value="0,0,0">${t('no_output')}</option><option value="1,0,0">${t('extractor_output')}</option>`;opts+=`<optgroup label="${uiLang=='de'?'Absaugung steuern':'Control extractor'}">`;for(let a=1;a<=10;a++)opts+=`<option value="4,0,${a}">${extractorActionLabel(a)}</option>`;opts+='</optgroup>';(d.modules||[]).forEach(m=>{if(m.online&&(m.caps&16384)!=0){let n=amn(m);opts+=`<option value="2,${m.addr},0">${n} - ${ioOutputLabel(m,0)}</option>`;opts+=`<option value="2,${m.addr},1">${n} - ${ioOutputLabel(m,1)}</option>`}if(m.online&&(m.type==7||m.type==8))universalTargetDefs(m).forEach(e=>{opts+=`<option value="3,${m.addr},${e.id}">${universalEntityControlLabel(m,e)}</option>`})});if(current&&current!='0,0,0'&&!optionHas(opts,current)){let v=dec(current),label=ruleLabel(d,{target_type:v.t,target_addr:v.a,target_bit:v.b},'target');opts+=`<option value="${current}">${escHtml(t('no_output')+' ('+label+')')}</option>`}return opts.replace(`value="${current}"`,`value="${current}" selected`)}
function visibleRules(d){return (d.input_rules||[]).map((r,i)=>({r,i})).filter(x=>x.r&&x.r.enabled)}
function logicTypeName(type){return ({input:t('input_state'),output:t('output_label'),and:t('and_label'),or:t('or_label'),not:t('not_label'),toggle:'Toggle',sr:'RS',ton:'TON',tof:'TOFF',pulse:'Pulse',clock:(uiLang=='de'?'Takt':'Clock')})[type]||type}
function logicSignalText(n){let s=String(n.signal||'');if(!s||s==='manual'||s==='main_output')return '';if(s.startsWith('extractor:action:')){let map={level_next:1,level_previous:2,level_high:3,level_medium:4,level_low:5,level_custom:6,power_plus_1:7,power_minus_1:8,power_plus_10:9,power_minus_10:10};return extractorActionLabel(map[s.slice(17)]||0)}return n.name||logicTypeName(n.type)}
function logicCountText(count,label){return count+' '+label}
function logicSummaryCard(g){let nodes=Array.isArray(g&&g.nodes)?g.nodes:[],links=Array.isArray(g&&g.links)?g.links:[];let counts={};nodes.forEach(n=>counts[n.type]=(counts[n.type]||0)+1);let inputs=nodes.filter(n=>n.type==='input'),outputs=nodes.filter(n=>n.type==='output'),logicNodes=nodes.filter(n=>n.type!=='input'&&n.type!=='output');let simInputs=inputs.filter(n=>!logicSignalText(n)).length,realInputs=inputs.length-simInputs;let realInNames=[...new Set(inputs.map(logicSignalText).filter(Boolean))].slice(0,4);let outNames=[...new Set(outputs.map(n=>logicSignalText(n)||n.name||logicTypeName(n.type)).filter(Boolean))].slice(0,4);let tags=Object.keys(counts).filter(k=>k!=='input'&&k!=='output').map(k=>`<span>${logicTypeName(k)} x${counts[k]}</span>`).join('')||`<span>${t('logic_none')}</span>`;let status=g.enabled?`<span class="pill on">${t('logic_enabled')}</span>`:`<span class="pill">${t('logic_disabled')}</span>`;let selected=g.selected?`<span class="pill">${t('logic_selected')}</span>`:'';let inText=[logicCountText(realInputs,t('logic_real_inputs')),logicCountText(simInputs,t('logic_sim_inputs'))].join(' / ');let outText=outputs.length?`${outputs.length}${outNames.length?' &middot; '+escHtml(outNames.join(', ')):''}`:t('logic_none');let title=escHtml(g.name||'Absaugung Logik');let slot=Number(g.slot||0)+1;return `<div class="logic-summary-card ${g.enabled?'':'is-disabled'}"><div class="logic-summary-head"><div><div class="k">${t('logic_definition')} &middot; ${t('logic_slot')} ${slot}</div><div class="logic-summary-title">${title}</div><div class="logic-summary-sub">${logicNodes.length} ${t('logic_types')} &middot; ${links.length} ${t('logic_links')}</div></div><div class="actions">${status}${selected}<a class="btn secondary" href="/logic?slot=${encodeURIComponent(g.slot||0)}">${t('logic_edit')}</a></div></div><div class="logic-summary-grid"><div class="logic-summary-chip"><small>${t('logic_blocks')}</small><strong>${nodes.length}</strong></div><div class="logic-summary-chip"><small>${t('logic_links')}</small><strong>${links.length}</strong></div><div class="logic-summary-chip is-wide"><small>${t('logic_inputs')}</small><strong>${escHtml(inText)}${realInNames.length?' &middot; '+escHtml(realInNames.join(', ')):''}</strong></div><div class="logic-summary-chip is-wide"><small>${t('logic_outputs')}</small><strong>${outText}</strong></div></div><div class="logic-block-tags">${tags}</div></div>`}
function renderLogicSummary(defs){let box=document.getElementById('logic_summary');if(!box)return;let list=Array.isArray(defs)?defs:(defs?[defs]:[]);list=list.filter(Boolean);if(!list.length){box.innerHTML=`<div class="logic-summary-card"><div class="muted">${t('logic_empty')}</div><div style="margin-top:8px"><a class="btn secondary" href="/logic">${t('logic_edit')}</a></div></div>`}else{box.innerHTML=`<div class="logic-summary-list">${list.map(logicSummaryCard).join('')}</div>`}if(window.lastState&&document.getElementById('roles'))document.getElementById('roles').innerHTML=outputRoleText(window.lastState)}
async function loadLogicSummary(force=false){let now=Date.now();if(!force&&now-logicSummaryLast<5000){if(logicSummaryCache)renderLogicSummary(logicSummaryCache);return}logicSummaryLast=now;try{let lr=await fetch('/logic/list',{cache:'no-store'});if(!lr.ok)return;let ld=await lr.json();let items=(ld.items||[]).filter(x=>x.used);let defs=await Promise.all(items.map(async it=>{try{let r=await fetch('/logic/json?slot='+encodeURIComponent(it.slot),{cache:'no-store'});if(!r.ok)return null;let g=await r.json();g.slot=Number(it.slot);g.selected=Number(it.slot)===Number(ld.active);if(!g.name)g.name=it.name;return g}catch(e){return null}}));logicSummaryCache=defs.filter(Boolean);renderLogicSummary(logicSummaryCache)}catch(e){}}function renderRules(d){let rules=visibleRules(d),html='';rules.forEach((x,n)=>{let r=x.r,i=x.i,s=enc(r.source_type,r.source_addr,r.source_bit),tt=enc(r.target_type,r.target_addr,r.target_bit);html+=`<div class="routing-rule"><span class="rule-number">${n+1}</span><select id="rule_s_${i}" onchange="ruleChanged(${i})">${sourceOptions(d,s)}</select><span class="rule-arrow">&rarr;</span><select id="rule_t_${i}" onchange="ruleChanged(${i})">${targetOptions(d,tt)}</select><button class="secondary" onclick="deleteRule(${i})">${t('remove')}</button></div>`});document.getElementById('rule_box').innerHTML=html||`<div class="muted">${t('no_modules')}</div>`}
function updateRules(d){let sig=routeSig(d);if(sig!==routingSig&&canRender('rule_box')){routingSig=sig;renderRules(d);return}visibleRules(d).forEach(x=>{let r=x.r,i=x.i,se=document.getElementById('rule_s_'+i),te=document.getElementById('rule_t_'+i);val(se,enc(r.source_type,r.source_addr,r.source_bit));val(te,enc(r.target_type,r.target_addr,r.target_bit))})}

function universalProtocolBuilderOutputOptions(addr,kind,current=''){
  let rows=universalProtocolBuilderRows(addr),opts=[];
  rows.forEach((r,i)=>{
    let type=String(r.type||'sensor').toLowerCase(),access=String(r.access||'ro').toLowerCase();
    let writable=access==='rw'||access==='wo';
    if(kind==='power'&&type==='number'&&writable)opts.push([String(i),r.name||('Entity '+(i+1))]);
    if(kind==='enable'&&type==='switch'&&writable)opts.push([String(i),r.name||('Entity '+(i+1))]);
    if(kind==='rpm'&&(type==='sensor'||type==='number')&&(access==='ro'||access==='rw'))opts.push([String(i),r.name||('Entity '+(i+1))]);
  });
  let html='<option value="">'+(uiLang=='de'?'nicht verwendet':'not used')+'</option>';
  if(kind==='enable')html+='<option value="__power__">'+(uiLang=='de'?'über Leistungswert (0 % = AUS)':'derive from power (0% = OFF)')+'</option>';
  html+=opts.map(([v,l])=>`<option value="${v}"${String(current)===v?' selected':''}>${escHtml(l)}</option>`).join('');
  return html;
}
function universalProtocolBuilderOutputChanged(addr,el){
  if(el)markFieldDirty(el);
  universalProtocolBuilderOutputRefresh(addr);
  universalDirtyBadge(addr);
}
function universalProtocolBuilderOutputRefresh(addr){
  let p=document.getElementById('upb_output_power_'+addr),e=document.getElementById('upb_output_enable_'+addr),r=document.getElementById('upb_output_rpm_'+addr);
  if(!p||!e||!r)return;
  let pv=p.value,ev=e.value,rv=r.value;
  p.innerHTML=universalProtocolBuilderOutputOptions(addr,'power',pv);
  e.innerHTML=universalProtocolBuilderOutputOptions(addr,'enable',ev);
  r.innerHTML=universalProtocolBuilderOutputOptions(addr,'rpm',rv);
  if([...p.options].some(o=>o.value===pv))p.value=pv;
  if([...e.options].some(o=>o.value===ev))e.value=ev;
  if([...r.options].some(o=>o.value===rv))r.value=rv;
  let on=!!document.getElementById('upb_output_use_'+addr)?.checked;
  p.disabled=!on;e.disabled=!on;r.disabled=!on;
}
function universalProtocolBuilderOutputLoad(addr,rows){
  let power=-1,enable=-1,rpm=-1;
  (rows||[]).forEach((r,i)=>{
    let role=String(r.role||'').toLowerCase();
    if(role==='main_output_power')power=i;
    if(role==='main_output_enable')enable=i;
    if(role==='main_output_rpm')rpm=i;
  });
  if(rpm<0){
    (rows||[]).some((r,i)=>{let type=String(r.type||'sensor').toLowerCase(),access=String(r.access||'ro').toLowerCase(),unit=String(r.unit||'').toLowerCase();if((type==='sensor'||type==='number')&&(access==='ro'||access==='rw')&&unit==='rpm'){rpm=i;return true}return false});
  }
  let use=document.getElementById('upb_output_use_'+addr);
  if(use)use.checked=power>=0||enable>=0||rpm>=0;
  universalProtocolBuilderOutputRefresh(addr);
  let p=document.getElementById('upb_output_power_'+addr),e=document.getElementById('upb_output_enable_'+addr),r=document.getElementById('upb_output_rpm_'+addr);
  if(p&&power>=0)p.value=String(power);
  if(e)e.value=enable>=0?String(enable):(power>=0?'__power__':'');
  if(r&&rpm>=0)r.value=String(rpm);
  universalProtocolBuilderOutputRefresh(addr);
}
function universalProtocolBuilderApplyOutputRoles(addr,rows){
  rows=(rows||[]).map(r=>({...r}));
  rows.forEach(r=>{
    let role=String(r.role||'').toLowerCase();
    if(role==='main_output_enable'||role==='main_output_power'||role==='main_output_rpm')r.role='';
  });
  if(!document.getElementById('upb_output_use_'+addr)?.checked)return rows;
  let pv=document.getElementById('upb_output_power_'+addr)?.value||'';
  let ev=document.getElementById('upb_output_enable_'+addr)?.value||'';
  let rv=document.getElementById('upb_output_rpm_'+addr)?.value||'';
  if(pv!==''&&rows[Number(pv)])rows[Number(pv)].role='main_output_power';
  if(ev!==''&&ev!=='__power__'&&rows[Number(ev)])rows[Number(ev)].role='main_output_enable';
  if(rv!==''&&rows[Number(rv)])rows[Number(rv)].role='main_output_rpm';
  return rows;
}

function universalProtocolBuilderHtml(m){
  let a=Number(m.addr);
  return `<button type="button" class="upb-launch" onclick="universalProtocolBuilderOpen(${a})">
    <span class="upb-launch-copy">
      <strong>${uiLang=='de'?'Universal RS232 Protocol Builder':'Universal RS232 Protocol Builder'}</strong>
      <small>${uiLang=='de'?'Telegramme, Antworten und Werte ohne Profiltext anlegen':'Create telegrams, responses and values without editing profile text'}</small>
    </span>
    <span class="upb-launch-open">${uiLang=='de'?'Editor öffnen':'Open editor'} ↗</span>
  </button>`;
}

function upbEscValue(v){return escHtml(String(v??'')).replaceAll('"','&quot;')}
function upbLineValue(v){return fixText(String(v??'')).replace(/[\r\n]+/g,' ').trim()}
function builderTimeBaseOptions(current='none'){
  let opts=[['none',uiLang=='de'?'Keine Zeitbasis':'No time base'],['s',uiLang=='de'?'Sekunden':'Seconds'],['m',uiLang=='de'?'Minuten':'Minutes'],['h',uiLang=='de'?'Stunden':'Hours'],['d',uiLang=='de'?'Tage':'Days']];
  return opts.map(([v,l])=>`<option value="${v}"${String(current||'none')===v?' selected':''}>${l}</option>`).join('');
}
function builderTimeDisplayOptions(current='raw'){
  let opts=[['raw',uiLang=='de'?'Rohwert / Basis':'Raw / base'],['m',uiLang=='de'?'Als Minuten':'As minutes'],['h',uiLang=='de'?'Als Stunden':'As hours'],['d',uiLang=='de'?'Als Tage':'As days'],['dhm',uiLang=='de'?'Tage · Stunden · Minuten':'Days · hours · minutes']];
  return opts.map(([v,l])=>`<option value="${v}"${String(current||'raw')===v?' selected':''}>${l}</option>`).join('');
}
function builderMaskValid(v){
  let s=String(v??'').trim();if(!s)return true;
  if(/^0x[0-9a-f]+$/i.test(s)){let n=parseInt(s.slice(2),16);return Number.isSafeInteger(n)&&n>=0&&n<=0xFFFFFFFF}
  if(/^0b[01]+$/i.test(s)){let n=parseInt(s.slice(2),2);return Number.isSafeInteger(n)&&n>=0&&n<=0xFFFFFFFF}
  if(!/^\d+$/.test(s))return false;let n=Number(s);return Number.isSafeInteger(n)&&n>=0&&n<=0xFFFFFFFF;
}
function builderMapModeOptions(current='none'){
  let opts=[['none',uiLang=='de'?'Kein Text-Mapping':'No text mapping'],['exact',uiLang=='de'?'Exakter Wert → Text':'Exact value → text'],['flags',uiLang=='de'?'Flags kombinieren':'Combine flags']];
  return opts.map(([v,l])=>`<option value="${v}"${String(current||'none')===v?' selected':''}>${l}</option>`).join('');
}
function builderMapNumberValid(v){
  let s=String(v??'').trim();if(!s)return false;
  if(/^[+-]?0x[0-9a-f]+$/i.test(s))return true;
  if(/^[+-]?0b[01]+$/i.test(s))return true;
  return /^[+-]?\d+$/.test(s);
}
function builderValueMapValid(v){
  let s=String(v??'').trim();if(!s)return true;
  let entries=s.split('|').map(x=>x.trim()).filter(Boolean);if(!entries.length)return false;
  return entries.every(e=>{let m=e.match(/^([^=:]+)\s*[=:]\s*(.+)$/);return !!m&&builderMapNumberValid(m[1])&&String(m[2]||'').trim().length>0});
}
function upbSafeKey(v,fallback='value'){
  let s=String(v||fallback).trim().toLowerCase()
    .replace(/[ä]/g,'ae').replace(/[ö]/g,'oe').replace(/[ü]/g,'ue').replace(/ß/g,'ss')
    .replace(/[^a-z0-9_]+/g,'_').replace(/^_+|_+$/g,'');
  return (s||fallback).slice(0,17);
}
function upbDefaultRow(kind='sensor'){
  let base={
    name:uiLang=='de'?'Neuer Wert':'New value',type:'sensor',access:'ro',
    poll:'',match:'',unit:'',poll_ms:'1000',key:'',id:'',role:'',
    match_offset:'0',value_offset:'0',value_type:'u8',value_len:'',
    scale:'1',divisor:'1',offset:'0',bitmask:'',bit_shift:'0',time_base:'none',time_display:'raw',map_mode:'none',value_map:'',map_default:'',min:'0',max:'100',step:'1',value_on:'1',value_off:'0',
    set:'',set_on:'',set_off:'',options:'',values:'',
    repeat_on_ms:'',repeat_off_ms:''
  };
  if(kind==='number'){
    Object.assign(base,{name:uiLang=='de'?'Einstellwert':'Adjustable value',type:'number',access:'rw',set:'{value}'});
  }else if(kind==='switch'){
    Object.assign(base,{name:uiLang=='de'?'Schalter':'Switch',type:'switch',access:'rw',set_on:'ON',set_off:'OFF'});
  }else if(kind==='select'){
    Object.assign(base,{name:uiLang=='de'?'Auswahl':'Select',type:'select',access:'rw',options:'Aus|Auto|Ein',values:'0|1|2',set:'{value}'});
  }else if(kind==='button'){
    Object.assign(base,{name:uiLang=='de'?'Aktion':'Action',type:'button',access:'wo',set:'CMD'});
  }
  return base;
}
function upbTypeOptions(type){
  let opts=[
    ['sensor',uiLang=='de'?'Messwert / Sensor':'Measurement / sensor'],
    ['number',uiLang=='de'?'Einstellbarer Zahlenwert':'Adjustable number'],
    ['binary_sensor',uiLang=='de'?'Binärer Status':'Binary status'],
    ['switch',uiLang=='de'?'Schalter':'Switch'],
    ['select',uiLang=='de'?'Auswahlliste':'Select'],
    ['text',uiLang=='de'?'Text':'Text'],
    ['button',uiLang=='de'?'Aktion / Button':'Action / button']
  ];
  return opts.map(([v,l])=>`<option value="${v}"${v===type?' selected':''}>${l}</option>`).join('');
}
function upbAccessOptions(access){
  let opts=[['ro',uiLang=='de'?'nur lesen':'read only'],['rw',uiLang=='de'?'lesen + schreiben':'read + write'],['wo',uiLang=='de'?'nur schreiben':'write only']];
  return opts.map(([v,l])=>`<option value="${v}"${v===access?' selected':''}>${l}</option>`).join('');
}
function upbBinaryValueTypeOptions(current='u8'){
  let opts=[['u8','u8'],['i8','i8'],['u16le','u16 LE'],['u16be','u16 BE'],['i16le','i16 LE'],['i16be','i16 BE'],['u32le','u32 LE'],['u32be','u32 BE'],['i32le','i32 LE'],['i32be','i32 BE'],['ascii','ASCII fixed'],['hex','HEX fixed']];
  return opts.map(([v,l])=>`<option value="${v}"${String(current||'u8').toLowerCase()===v?' selected':''}>${l}</option>`).join('');
}
function upbIsBinary(addr){return String(document.getElementById('upb_protocol_'+addr)?.value||'').trim().toUpperCase()==='BINARY'}
function universalProtocolBuilderProtocolMode(addr){
  let bin=upbIsBinary(addr),root=document.getElementById('upb_editor_'+addr),line=document.getElementById('upb_line_'+addr);
  if(line){if(bin)line.value='NONE';line.disabled=bin}
  root?.querySelectorAll('.upb-binary-top').forEach(e=>e.hidden=!bin);
  root?.querySelectorAll('.upb-binary-field').forEach(e=>e.hidden=!bin);
  root?.querySelectorAll('.upb-read-field input[data-f="poll"]').forEach(e=>e.placeholder=bin?'AA 10 01':'S');
  root?.querySelectorAll('.upb-read-field input[data-f="match"]').forEach(e=>e.placeholder=bin?'AA 90 ?? ??':'S###');
  root?.querySelectorAll('input[data-f="set"]').forEach(e=>e.placeholder=bin?'AA 20 {value:u16be}':'d{value:03}');
  root?.querySelectorAll('input[data-f="set_on"]').forEach(e=>e.placeholder=bin?'AA 30 01':'N');
  root?.querySelectorAll('input[data-f="set_off"]').forEach(e=>e.placeholder=bin?'AA 30 00':'M');
}
function upbBinaryFrameChanged(addr){
  let mode=String(document.getElementById('upb_bin_frame_'+addr)?.value||'IDLE');
  let fixed=document.getElementById('upb_bin_rxlen_'+addr),off=document.getElementById('upb_bin_lenoff_'+addr),adj=document.getElementById('upb_bin_lenadj_'+addr);
  if(fixed)fixed.disabled=mode!=='FIXED';
  let lm=mode.startsWith('LENGTH_');if(off)off.disabled=!lm;if(adj)adj.disabled=!lm;
}
function upbRoleOptions(role){
  let opts=[
    ['',uiLang=='de'?'Keine besondere Rolle':'No special role'],
    ['main_input',uiLang=='de'?'Haupteingang':'Main input'],
    ['main_output_enable',uiLang=='de'?'Hauptausgang Ein/Aus':'Main output enable'],
    ['main_output_power',uiLang=='de'?'Hauptausgang Leistung':'Main output power'],
    ['main_output_rpm',uiLang=='de'?'Hauptausgang Drehzahl':'Main output RPM'],
    ['input',uiLang=='de'?'Eingang':'Input'],
    ['output',uiLang=='de'?'Ausgang':'Output'],
    ['output_enable',uiLang=='de'?'Ausgang Ein/Aus':'Output enable'],
    ['output_power',uiLang=='de'?'Ausgang Leistung':'Output power']
  ];
  return opts.map(([v,l])=>`<option value="${v}"${v===role?' selected':''}>${l}</option>`).join('');
}
function upbRowHtml(addr,row,index){
  row={...upbDefaultRow(row&&row.type||'sensor'),...(row||{})};
  let i=index+1;
  return `<div class="upb-row" data-index="${index}">
    <div class="upb-row-head">
      <div class="upb-row-title"><span class="upb-row-num">${i}</span><strong>${escHtml(row.name||('Entity '+i))}</strong></div>
      <button type="button" class="secondary upb-remove" onclick="universalProtocolBuilderRemoveRow(${addr},${index})">${uiLang=='de'?'Entfernen':'Remove'}</button>
    </div>
    <div class="upb-row-grid">
      <label>${uiLang=='de'?'Name':'Name'}<input data-f="name" value="${upbEscValue(row.name)}" oninput="universalProtocolBuilderRowChanged(${addr},${index})"></label>
      <label>${uiLang=='de'?'Darstellung':'Value type'}<select data-f="type" onchange="universalProtocolBuilderRowType(${addr},${index},true)">${upbTypeOptions(row.type)}</select></label>
      <label>${uiLang=='de'?'Zugriff':'Access'}<select data-f="access" onchange="universalProtocolBuilderRowType(${addr},${index},false);universalProtocolBuilderOutputRefresh(${addr})">${upbAccessOptions(row.access)}</select></label>
      <label class="upb-read-field">${uiLang=='de'?'Abfrage / TX':'Query / TX'}<input data-f="poll" value="${upbEscValue(row.poll)}" placeholder="S"></label>
      <label class="upb-read-field">${uiLang=='de'?'Antwortmuster / RX':'Response pattern / RX'}<input data-f="match" value="${upbEscValue(row.match)}" placeholder="S###"></label>
      <label>${uiLang=='de'?'Einheit':'Unit'}<input data-f="unit" value="${upbEscValue(row.unit)}" placeholder="%"></label>
      <label class="upb-read-field">Polling ms<input data-f="poll_ms" type="number" min="250" max="60000" step="50" value="${upbEscValue(row.poll_ms||1000)}"></label>
    </div>
    <div class="upb-rule-note" data-note>${uiLang=='de'?'Regel wird aus den Feldern oben erzeugt.':'Rule is generated from the fields above.'}</div>
    <details class="upb-advanced">
      <summary>${uiLang=='de'?'Erweitert / Schreiben':'Advanced / writing'}</summary>
      <div class="upb-advanced-grid">
        <label>Key<input data-f="key" value="${upbEscValue(row.key)}" placeholder="speed"></label>
        <label>Entity ID<input data-f="id" type="number" min="20" max="249" value="${upbEscValue(row.id)}"></label>
        <label>${uiLang=='de'?'Rolle im System':'System role'}<select data-f="role">${upbRoleOptions(row.role)}</select></label>
        <label class="upb-binary-field" hidden>${uiLang=='de'?'Match ab Byte':'Match from byte'}<input data-f="match_offset" type="number" min="0" max="191" step="1" value="${upbEscValue(row.match_offset||0)}"></label>
        <label class="upb-binary-field" hidden>${uiLang=='de'?'Wert ab Byte':'Value from byte'}<input data-f="value_offset" type="number" min="0" max="191" step="1" value="${upbEscValue(row.value_offset||0)}"></label>
        <label class="upb-binary-field" hidden>${uiLang=='de'?'Binärer Datentyp':'Binary value type'}<select data-f="value_type">${upbBinaryValueTypeOptions(row.value_type||'u8')}</select></label>
        <label class="upb-binary-field" hidden>${uiLang=='de'?'Feldlänge (ASCII/HEX)':'Field length (ASCII/HEX)'}<input data-f="value_len" type="number" min="1" max="31" step="1" value="${upbEscValue(row.value_len||'')}"></label>
        <label>${uiLang=='de'?'Faktor':'Scale'}<input data-f="scale" type="number" step="1" value="${upbEscValue(row.scale||1)}" title="${uiLang=='de'?'Rohwert wird mit diesem Faktor multipliziert':'Raw value is multiplied by this factor'}"></label>
        <label>${uiLang=='de'?'Teiler':'Divisor'}<input data-f="divisor" type="number" min="1" step="1" value="${upbEscValue(row.divisor||1)}" title="${uiLang=='de'?'Nach dem Faktor durch diesen Wert teilen':'Divide by this value after scaling'}"></label>
        <label>${uiLang=='de'?'Offset':'Offset'}<input data-f="offset" type="number" step="1" value="${upbEscValue(row.offset||0)}" title="${uiLang=='de'?'Nach Faktor/Teiler addieren':'Added after scale/divisor'}"></label>
        <label>${uiLang=='de'?'Bitmaske':'Bit mask'}<input data-f="bitmask" inputmode="text" value="${upbEscValue(row.bitmask||'')}" placeholder="0x00F0 / 0b1111 / 240" title="${uiLang=='de'?'Maske wird vor Faktor/Teiler angewendet':'Mask is applied before scale/divisor'}"></label>
        <label>${uiLang=='de'?'Bit-Shift rechts':'Right bit shift'}<input data-f="bit_shift" type="number" min="0" max="31" step="1" value="${upbEscValue(row.bit_shift||0)}" title="${uiLang=='de'?'Nach der Maske um 0…31 Bits nach rechts schieben':'Shift right 0…31 bits after masking'}"></label>
        <label>${uiLang=='de'?'Zeitbasis nach Umrechnung':'Time base after transform'}<select data-f="time_base">${builderTimeBaseOptions(row.time_base||'none')}</select></label>
        <label>${uiLang=='de'?'Zeit-Ausgabe':'Time output'}<select data-f="time_display">${builderTimeDisplayOptions(row.time_display||'raw')}</select></label>
        <label>${uiLang=='de'?'Wert → Text':'Value → text'}<select data-f="map_mode">${builderMapModeOptions(row.map_mode||'none')}</select></label>
        <label style="grid-column:span 2">${uiLang=='de'?'Mapping (Wert=Text, mit | trennen)':'Mapping (value=text, separate with |)'}<input data-f="value_map" value="${upbEscValue(row.value_map||row.map||'')}" placeholder="0=Bereit|1=Warnung|2=Filter voll / 0x01=Motor|0x02=Filter"></label>
        <label>${uiLang=='de'?'Fallback-Text':'Fallback text'}<input data-f="map_default" value="${upbEscValue(row.map_default||'')}" placeholder="Unbekannt"></label>
        <label>Min<input data-f="min" type="number" value="${upbEscValue(row.min)}"></label>
        <label>Max<input data-f="max" type="number" value="${upbEscValue(row.max)}"></label>
        <label>Step<input data-f="step" type="number" min="1" value="${upbEscValue(row.step||1)}"></label>
      </div>
      <div class="upb-write-grid upb-type-extra upb-generic-write">
        <label>${uiLang=='de'?'Schreibtelegramm':'Write telegram'}<input data-f="set" value="${upbEscValue(row.set)}" placeholder="d{value:03}" title="${uiLang=='de'?'Platzhalter: {value} oder {value:01} bis {value:09}':'Placeholders: {value} or {value:01} through {value:09}'}"></label>
      </div>
      <div class="upb-write-grid upb-type-extra upb-switch-write">
        <label>${uiLang=='de'?'Telegramm AN':'Telegram ON'}<input data-f="set_on" value="${upbEscValue(row.set_on)}" placeholder="N"></label>
        <label>${uiLang=='de'?'Telegramm AUS':'Telegram OFF'}<input data-f="set_off" value="${upbEscValue(row.set_off)}" placeholder="M"></label>
        <label>${uiLang=='de'?'Raw AN / AUS':'Raw ON / OFF'}<span style="display:grid;grid-template-columns:1fr 1fr;gap:6px"><input data-f="value_on" type="number" value="${upbEscValue(row.value_on||1)}"><input data-f="value_off" type="number" value="${upbEscValue(row.value_off||0)}"></span></label>
      </div>
      <div class="upb-write-grid upb-type-extra upb-switch-repeat">
        <label>${uiLang=='de'?'AN wiederholen (ms, 0 = aus)':'Repeat ON (ms, 0 = off)'}<input data-f="repeat_on_ms" type="number" min="0" max="60000" step="100" value="${upbEscValue(row.repeat_on_ms)}" placeholder="1000"></label>
        <label>${uiLang=='de'?'AUS wiederholen (ms, 0 = aus)':'Repeat OFF (ms, 0 = off)'}<input data-f="repeat_off_ms" type="number" min="0" max="60000" step="100" value="${upbEscValue(row.repeat_off_ms)}" placeholder="0"></label>
        <label>${uiLang=='de'?'Hinweis':'Note'}<span style="min-height:36px;display:flex;align-items:center;color:#8fa6b9;font-size:10px;line-height:1.35">${uiLang=='de'?'Die Wiederholung läuft lokal im Universal-Modul und erzeugt keinen zusätzlichen OFE-RS485-Befehl pro Wiederholung.':'Repeats run locally on the Universal module and do not generate an extra OFE-RS485 command per repeat.'}</span></label>
      </div>
      <div class="upb-select-grid upb-type-extra upb-select-map">
        <label>${uiLang=='de'?'Anzeigenamen (mit | trennen)':'Display labels (separate with |)'}<input data-f="options" value="${upbEscValue(row.options)}" placeholder="Aus|Auto|Ein"></label>
        <label>${uiLang=='de'?'Rohwerte (mit | trennen)':'Raw values (separate with |)'}<input data-f="values" value="${upbEscValue(row.values)}" placeholder="0|1|2"></label>
      </div>
    </details>
  </div>`;
}
function upbRowElement(addr,index){return document.querySelector(`#upb_rows_${addr} .upb-row[data-index="${index}"]`)}
function upbReadRow(addr,index){
  let root=upbRowElement(addr,index);if(!root)return null;
  let o={};root.querySelectorAll('[data-f]').forEach(e=>o[e.dataset.f]=e.value);
  return o;
}
function universalProtocolBuilderRowType(addr,index,userChange=false){
  let root=upbRowElement(addr,index);if(!root)return;
  let type=root.querySelector('[data-f="type"]')?.value||'sensor';
  let generic=['number','select','text','button'].includes(type);
  let sw=['switch','binary_sensor'].includes(type);
  let sel=type==='select';
  root.querySelectorAll('.upb-generic-write').forEach(e=>e.hidden=!generic);
  root.querySelectorAll('.upb-switch-write,.upb-switch-repeat').forEach(e=>e.hidden=!sw);
  root.querySelectorAll('.upb-select-map').forEach(e=>e.hidden=!sel);
  let access=root.querySelector('[data-f="access"]');
  if(userChange&&access){
    if(type==='sensor'||type==='binary_sensor')access.value='ro';
    else if(type==='button')access.value='wo';
    else access.value='rw';
    markFieldDirty(access);
  }
  if(access){
    let ro=access.querySelector('option[value="ro"]'),rw=access.querySelector('option[value="rw"]'),wo=access.querySelector('option[value="wo"]');
    if(type==='switch'){
      if(ro)ro.textContent=uiLang=='de'?'nur Status':'status only';
      if(rw)rw.textContent=uiLang=='de'?'mit Rückmeldung':'with feedback';
      if(wo)wo.textContent=uiLang=='de'?'ohne Rückmeldung':'without feedback';
    }else{
      if(ro)ro.textContent=uiLang=='de'?'nur lesen':'read only';
      if(rw)rw.textContent=uiLang=='de'?'lesen + schreiben':'read + write';
      if(wo)wo.textContent=uiLang=='de'?'nur schreiben':'write only';
    }
  }
  let noFeedback=type==='switch'&&String(access?.value||'').toLowerCase()==='wo';
  root.querySelectorAll('.upb-read-field').forEach(e=>e.style.display=noFeedback?'none':'flex');
  universalProtocolBuilderRowChanged(addr,index);
  universalProtocolBuilderOutputRefresh(addr);
  universalProtocolBuilderProtocolMode(addr);
}
function universalProtocolBuilderRowChanged(addr,index){
  let root=upbRowElement(addr,index),r=upbReadRow(addr,index);if(!root||!r)return;
  let title=root.querySelector('.upb-row-title strong');if(title)title.textContent=r.name||('Entity '+(index+1));
  let note=root.querySelector('[data-note]'),parts=[];
  if(r.poll)parts.push('TX '+r.poll);
  if(r.match)parts.push('RX '+r.match);
  if(r.match&&r.match.includes('#'))parts.push('# = '+(uiLang=='de'?'Ziffer':'digit'));
  if(r.match&&r.match.includes('{value}'))parts.push('{value} = '+(uiLang=='de'?'Zahlenwert':'numeric value'));
  if(r.bitmask)parts.push('& '+r.bitmask);
  if(Number(r.bit_shift||0)>0)parts.push('>> '+r.bit_shift);
  if(Number(r.scale||1)!==1)parts.push('× '+r.scale);
  if(Number(r.divisor||1)!==1)parts.push('÷ '+r.divisor);
  if(Number(r.offset||0)!==0)parts.push((Number(r.offset)>0?'+ ':'')+r.offset);
  if(String(r.map_mode||'none')!=='none')parts.push(uiLang=='de'?'→ Text':'→ text');
  if(r.unit)parts.push(r.unit);
  if(note)note.textContent=parts.length?parts.join('  ·  '):(uiLang=='de'?'Noch keine Telegrammregel eingetragen.':'No telegram rule entered yet.');
  universalProtocolBuilderOutputRefresh(addr);
}
function universalProtocolBuilderRenumber(addr){
  let rows=[...document.querySelectorAll(`#upb_rows_${addr} .upb-row`)];
  rows.forEach((root,i)=>{
    root.dataset.index=String(i);
    let num=root.querySelector('.upb-row-num');if(num)num.textContent=String(i+1);
    let rm=root.querySelector('.upb-remove');if(rm)rm.setAttribute('onclick',`universalProtocolBuilderRemoveRow(${addr},${i})`);
    root.querySelectorAll('[oninput],[onchange]').forEach(el=>{
      let a=el.getAttribute('oninput');if(a&&a.startsWith('universalProtocolBuilderRowChanged'))el.setAttribute('oninput',`universalProtocolBuilderRowChanged(${addr},${i})`);
      let c=el.getAttribute('onchange');if(c&&c.startsWith('universalProtocolBuilderRowType'))el.setAttribute('onchange',`universalProtocolBuilderRowType(${addr},${i},true)`);
    });
    universalProtocolBuilderRowType(addr,i,false);
  });
}
function universalProtocolBuilderAddRow(addr,kind='sensor',row=null){
  let box=document.getElementById('upb_rows_'+addr);if(!box)return;
  let count=box.querySelectorAll('.upb-row').length;
  if(count>=32){universalProtocolBuilderMsg(addr,uiLang=='de'?'Maximal 32 Werte/Befehle pro Universal-Modul.':'Maximum 32 values/commands per Universal module.','warn');return}
  if(count===0)box.innerHTML='';
  box.insertAdjacentHTML('beforeend',upbRowHtml(addr,row||upbDefaultRow(kind),count));
  universalProtocolBuilderRowType(addr,count,false);
  let added=box.lastElementChild;if(added)added.querySelector('input[data-f="name"]')?.focus();
}
function universalProtocolBuilderRemoveRow(addr,index){
  let root=upbRowElement(addr,index);if(root)root.remove();
  let box=document.getElementById('upb_rows_'+addr);
  if(box&&!box.querySelector('.upb-row'))box.innerHTML=`<div class="upb-empty">${uiLang=='de'?'Noch keine Werte/Befehle angelegt. Oben einen Typ hinzufügen.':'No values/commands configured yet. Add a type above.'}</div>`;
  universalProtocolBuilderRenumber(addr);
}
function universalProtocolBuilderRows(addr){
  return [...document.querySelectorAll(`#upb_rows_${addr} .upb-row`)].map((_,i)=>upbReadRow(addr,i)).filter(Boolean);
}
function upbParseProfile(text){
  let top={},rows={},trim=String(text||'').trim();
  universalLines(trim).forEach(line=>{
    line=line.trim();if(!line||line.startsWith('#')||line.startsWith(';'))return;
    let p=line.indexOf('=');if(p<0)return;
    let key=line.slice(0,p).trim(),value=fixText(line.slice(p+1).trim());
    let m=key.match(/^entity\.(\d+)\.([a-z0-9_]+)$/i);
    if(m){let i=Number(m[1]);rows[i]=rows[i]||{};rows[i][m[2].toLowerCase()]=value;}
    else top[key.toLowerCase()]=value;
  });
  return {top,rows:Object.keys(rows).map(Number).sort((a,b)=>a-b).slice(0,32).map(i=>rows[i])};
}
function universalProtocolBuilderSetTop(addr,top,clear=true){
  let setv=(id,v)=>{let e=document.getElementById(id+'_'+addr);if(e&&v!==undefined&&v!==null&&String(v).length){e.value=String(v)}};
  setv('upb_profile',top.profile||top.name||'Generic RS232');
  setv('upb_station',top.station||top.device||'Community device');
  setv('upb_baud',top.baud||top.uart_baud||9600);
  setv('upb_frame',String(top.frame||top.uart_frame||'8N1').toUpperCase());
  setv('upb_protocol',top.protocol||top.mode||'ASCII');
  setv('upb_checksum',String(top.checksum||top.checksum_preset||'NONE').toUpperCase());
  setv('upb_line',String(top.line_end||top.lineending||top.ending||'CR').toUpperCase());
  setv('upb_bin_frame',String(top.binary_frame||top.frame_mode||'IDLE').toUpperCase());
  setv('upb_bin_start',top.binary_start||top.start_hex||'');
  setv('upb_bin_rxlen',top.binary_rx_length||top.rx_length||8);
  setv('upb_bin_lenoff',top.binary_length_offset||top.length_offset||0);
  setv('upb_bin_lenadj',top.binary_length_adjust||top.length_adjust||0);
  universalProtocolBuilderProtocolMode(addr);upbBinaryFrameChanged(addr);
  if(clear){let root=document.getElementById('upb_editor_'+addr);if(root)clearDirtyIn(root)}
}
function universalProtocolBuilderRenderRows(addr,rows){
  let box=document.getElementById('upb_rows_'+addr);if(!box)return;
  if(!rows||!rows.length){box.innerHTML=`<div class="upb-empty">${uiLang=='de'?'Noch keine Werte/Befehle angelegt. Oben einen Typ hinzufügen.':'No values/commands configured yet. Add a type above.'}</div>`;return}
  box.innerHTML=rows.slice(0,32).map((r,i)=>upbRowHtml(addr,r,i)).join('');
  rows.slice(0,32).forEach((_,i)=>universalProtocolBuilderRowType(addr,i,false));
  let root=document.getElementById('upb_editor_'+addr);if(root)clearDirtyIn(root);
}
function universalProtocolBuilderFromProfile(addr,text){
  let p=upbParseProfile(text),m=universalCurrentModule(addr),cur=universalStateConfig(m||{type:7});
  p.rows=p.rows.map(r=>({...r,scale:r.scale||r.multiplier||'1',divisor:r.divisor||r.divider||'1',offset:r.offset||'0',bitmask:r.bitmask||r.mask||'',bit_shift:r.bit_shift||r.shift||'0',time_base:r.time_base||r.time_unit||'none',time_display:r.time_display||r.time_format||'raw',map_mode:r.map_mode||r.mapping_mode||(r.map||r.value_map?'exact':'none'),value_map:r.map||r.value_map||'',map_default:r.map_default||r.default_text||''}));
  universalProtocolBuilderSetTop(addr,{...cur,...p.top},false);
  universalProtocolBuilderRenderRows(addr,p.rows);
  universalProtocolBuilderOutputLoad(addr,p.rows);
  let prev=document.getElementById('upb_preview_'+addr);if(prev)prev.value=String(text||'');
  let root=document.getElementById('upb_editor_'+addr);if(root){root.dataset.originalProfile=String(text||'');clearDirtyIn(root)}
}
function universalProtocolBuilderTop(addr){
  let g=id=>document.getElementById(id+'_'+addr)?.value||'';
  return {
    profile:upbLineValue(g('upb_profile')||'Generic RS232'),
    station:upbLineValue(g('upb_station')||'Community device'),
    baud:String(Math.max(300,Math.min(1000000,Number(g('upb_baud')||9600)))),
    frame:String(g('upb_frame')||'8N1').toUpperCase(),
    protocol:upbLineValue(g('upb_protocol')||'ASCII'),
    checksum:String(g('upb_checksum')||'NONE').toUpperCase(),
    line_end:String(g('upb_line')||'CR').toUpperCase(),
    binary_frame:String(g('upb_bin_frame')||'IDLE').toUpperCase(),
    binary_start:upbLineValue(g('upb_bin_start')||''),
    binary_rx_length:String(g('upb_bin_rxlen')||'0'),
    binary_length_offset:String(g('upb_bin_lenoff')||'0'),
    binary_length_adjust:String(g('upb_bin_lenadj')||'0')
  };
}
function universalProtocolBuilderProfile(addr){
  let top=universalProtocolBuilderTop(addr),rows=universalProtocolBuilderApplyOutputRoles(addr,universalProtocolBuilderRows(addr));
  let lines=[
    '# Open Fume Extractor Universal RS232 profile - Protocol Builder',
    'profile='+top.profile,
    'station='+top.station,
    'baud='+top.baud,
    'frame='+top.frame,
    'protocol='+top.protocol,
    'checksum='+top.checksum,
    'line_end='+(String(top.protocol).toUpperCase()==='BINARY'?'NONE':top.line_end)
  ];
  if(String(top.protocol).toUpperCase()==='BINARY'){
    lines.push('binary_frame='+top.binary_frame);
    if(top.binary_start)lines.push('binary_start='+top.binary_start);
    if(top.binary_frame==='FIXED')lines.push('binary_rx_length='+Math.max(1,Math.min(192,Number(top.binary_rx_length)||1)));
    if(String(top.binary_frame).startsWith('LENGTH_')){
      lines.push('binary_length_offset='+Math.max(0,Math.min(191,Number(top.binary_length_offset)||0)));
      lines.push('binary_length_adjust='+Math.max(-192,Math.min(192,Number(top.binary_length_adjust)||0)));
    }
  }
  lines.push('');
  rows.forEach((r,i)=>{
    let n=i+1,p='entity.'+n+'.',type=String(r.type||'sensor').toLowerCase(),access=String(r.access||'ro').toLowerCase();
    let name=upbLineValue(r.name||('Entity '+n)),key=upbSafeKey(r.key||name,'entity'+n);
    lines.push(p+'type='+type);
    lines.push(p+'name='+name);
    lines.push(p+'key='+key);
    lines.push(p+'access='+access);
    if(r.id)lines.push(p+'id='+Math.max(20,Math.min(249,Number(r.id))));
    if(r.role)lines.push(p+'role='+upbLineValue(r.role));
    if(r.unit)lines.push(p+'unit='+upbLineValue(r.unit));
    if(r.poll)lines.push(p+'poll='+upbLineValue(r.poll));
    if(r.match)lines.push(p+'match='+upbLineValue(r.match));
    if(String(top.protocol).toUpperCase()==='BINARY'){
      let mo=Math.max(0,Math.min(191,Number(r.match_offset)||0)),vo=Math.max(0,Math.min(191,Number(r.value_offset)||0));
      let vt=String(r.value_type||'u8').toLowerCase();
      if(mo)lines.push(p+'match_offset='+mo);
      lines.push(p+'value_offset='+vo);
      lines.push(p+'value_type='+vt);
      if(vt==='ascii'||vt==='hex')lines.push(p+'value_len='+Math.max(1,Math.min(31,Number(r.value_len)||1)));
    }
    if(r.poll_ms)lines.push(p+'poll_ms='+Math.max(250,Math.min(60000,Number(r.poll_ms)||1000)));
    let scale=Number(r.scale||1);if(Number.isFinite(scale)&&scale!==1&&scale!==0)lines.push(p+'scale='+Math.trunc(scale));
    let divisor=Number(r.divisor||1);if(Number.isFinite(divisor)&&divisor>1)lines.push(p+'divisor='+Math.trunc(divisor));
    let offset=Number(r.offset||0);if(Number.isFinite(offset)&&offset!==0)lines.push(p+'offset='+Math.trunc(offset));
    let bitmask=String(r.bitmask||'').trim();if(bitmask&&builderMaskValid(bitmask)&&!/^0(?:x0+|b0+)?$/i.test(bitmask))lines.push(p+'bitmask='+bitmask);
    let bitShift=Number(r.bit_shift||0);if(Number.isInteger(bitShift)&&bitShift>0)lines.push(p+'bit_shift='+bitShift);
    let timeBase=String(r.time_base||'none').toLowerCase(),timeDisplay=String(r.time_display||'raw').toLowerCase();
    if(timeBase!=='none'){lines.push(p+'time_base='+timeBase);if(timeDisplay!=='raw')lines.push(p+'time_display='+timeDisplay)}
    let mapMode=String(r.map_mode||'none').toLowerCase(),valueMap=upbLineValue(r.value_map||'');
    if(valueMap&&mapMode==='none')mapMode='exact';
    if(mapMode!=='none'){lines.push(p+'map_mode='+mapMode);if(valueMap)lines.push(p+'map='+valueMap);if(r.map_default)lines.push(p+'map_default='+upbLineValue(r.map_default))}
    if(type==='number'){
      lines.push(p+'min='+(Number(r.min)||0));
      lines.push(p+'max='+(Number(r.max)||100));
      lines.push(p+'step='+Math.max(1,Number(r.step)||1));
    }
    if(type==='switch'||type==='binary_sensor'){
      lines.push(p+'value_on='+(Number(r.value_on)||0));
      lines.push(p+'value_off='+(Number(r.value_off)||0));
      if(r.set_on)lines.push(p+'set_on='+upbLineValue(r.set_on));
      if(r.set_off)lines.push(p+'set_off='+upbLineValue(r.set_off));
    }else{
      if(r.set)lines.push(p+'set='+upbLineValue(r.set));
    }
    if(type==='select'){
      if(r.options)lines.push(p+'options='+upbLineValue(r.options));
      if(r.values)lines.push(p+'values='+upbLineValue(r.values));
    }
    if(r.repeat_on_ms)lines.push(p+'repeat_on_ms='+Math.max(0,Math.min(60000,Number(r.repeat_on_ms)||0)));
    if(r.repeat_off_ms)lines.push(p+'repeat_off_ms='+Math.max(0,Math.min(60000,Number(r.repeat_off_ms)||0)));
    lines.push('');
  });
  return lines.join('\n').trim()+'\n';
}
function upbWriteTemplateValid(v){
  let s=String(v??'');
  let tokens=s.match(/\{value(?::[^}]*)?\}/g)||[];
  for(let token of tokens){
    if(token==='{value}')continue;
    let m=token.match(/^\{value:0([1-9])\}$/);
    if(!m)return false;
  }
  return !/\{value[^}]*\}/.test(s.replace(/\{value\}/g,'').replace(/\{value:0[1-9]\}/g,''));
}
function upbMatchPatternValid(v){
  let s=String(v??'').trim();if(!s)return true;
  let hashes=(s.match(/#/g)||[]).length;
  if(hashes>9)return false;
  if(s.includes('{value')){
    if(!/^.*\{value\}.*$/.test(s))return false;
    if((s.match(/\{value\}/g)||[]).length!==1)return false;
  }
  return true;
}
function upbBinaryHexValid(v,wildcards=false){
  let s=String(v??'').trim();if(!s)return true;
  s=s.replace(/0x/gi,'').replace(/[\s,:_-]+/g,'');
  if(wildcards)s=s.replace(/\?\?/g,'00');
  return s.length>0&&s.length%2===0&&/^[0-9a-f]+$/i.test(s);
}
function upbBinaryTemplateValid(v,allowValue=true){
  let s=String(v??'').trim();if(!s)return true;
  const types='u8|i8|u16le|u16be|i16le|i16be|u32le|u32be|i32le|i32be';
  let bad=false;
  s=s.replace(/\{value:([^}]+)\}/gi,(m,t)=>{if(!allowValue||!(new RegExp('^('+types+')$','i')).test(t)){bad=true;return 'ZZ'}return '00'});
  if(/\{[^}]*\}/.test(s))bad=true;
  return !bad&&upbBinaryHexValid(s,false);
}
function universalProtocolBuilderValidate(addr,text){
  let rows=universalProtocolBuilderRows(addr),issues=[],bin=upbIsBinary(addr),top=universalProtocolBuilderTop(addr);
  if(bin){
    if(!['IDLE','FIXED','LENGTH_U8','LENGTH_U16_LE','LENGTH_U16_BE'].includes(top.binary_frame))issues.push(uiLang=='de'?'Ungültiges Binär-Framing.':'Invalid binary framing.');
    if(top.binary_start&&!upbBinaryHexValid(top.binary_start,false))issues.push(uiLang=='de'?'Startbytes sind kein gültiges HEX.':'Start bytes are not valid HEX.');
    if(top.binary_frame==='FIXED'&&(!Number.isInteger(Number(top.binary_rx_length))||Number(top.binary_rx_length)<1||Number(top.binary_rx_length)>192))issues.push(uiLang=='de'?'Feste RX-Länge muss 1…192 sein.':'Fixed RX length must be 1…192.');
    if(String(top.binary_frame).startsWith('LENGTH_')&&(!Number.isInteger(Number(top.binary_length_offset))||Number(top.binary_length_offset)<0||Number(top.binary_length_offset)>191))issues.push(uiLang=='de'?'Längenfeld-Offset muss 0…191 sein.':'Length field offset must be 0…191.');
  }
  if(!rows.length)issues.push(uiLang=='de'?'Keine Werte/Befehle angelegt.':'No values/commands configured.');
  if(document.getElementById('upb_output_use_'+addr)?.checked){
    let pv=document.getElementById('upb_output_power_'+addr)?.value||'',ev=document.getElementById('upb_output_enable_'+addr)?.value||'';
    if(pv===''&&ev==='')issues.push(uiLang=='de'?'Hauptausgang ist aktiv, aber weder Leistung noch Ein/Aus ist zugeordnet.':'Main output is enabled but neither power nor enable is assigned.');
    if(ev==='__power__'&&pv==='')issues.push(uiLang=='de'?'Für „0 % = AUS“ muss ein Leistungswert gewählt sein.':'Power-derived OFF requires a power value.');
  }
  rows.forEach((r,i)=>{
    let n=i+1,type=String(r.type||'sensor'),access=String(r.access||'ro');
    if(!String(r.name||'').trim())issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Name fehlt':'name missing'));
    if(access!=='wo'&&!String(r.match||'').trim())issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Antwortmuster fehlt':'response pattern missing'));
    if(bin){
      if(String(r.match||'').trim()&&!upbBinaryHexValid(r.match,true))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Binär-Match muss HEX/?? sein':'binary match must contain HEX/??'));
      if(String(r.poll||'').trim()&&!upbBinaryTemplateValid(r.poll,false))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Binär-Abfrage ungültig':'invalid binary query'));
      if(String(r.set||'').trim()&&!upbBinaryTemplateValid(r.set,true))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Binär-Schreibtelegramm ungültig; z. B. AA 20 {value:u16be}':'invalid binary write telegram; e.g. AA 20 {value:u16be}'));
      if(String(r.set_on||'').trim()&&!upbBinaryTemplateValid(r.set_on,true))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': set_on HEX');
      if(String(r.set_off||'').trim()&&!upbBinaryTemplateValid(r.set_off,true))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': set_off HEX');
      let mo=Number(r.match_offset||0),vo=Number(r.value_offset||0),vt=String(r.value_type||'u8').toLowerCase(),vl=Number(r.value_len||0);
      if(!Number.isInteger(mo)||mo<0||mo>191)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': Match offset 0…191');
      if(!Number.isInteger(vo)||vo<0||vo>191)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': Value offset 0…191');
      if(!['u8','i8','u16le','u16be','i16le','i16be','u32le','u32be','i32le','i32be','ascii','hex'].includes(vt))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': Value type');
      if(vt==='ascii'&&(!Number.isInteger(vl)||vl<1||vl>31))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'ASCII-Feldlänge 1…31':'ASCII field length 1…31'));
      if(vt==='hex'&&(!Number.isInteger(vl)||vl<1||vl>15))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'HEX-Feldlänge 1…15':'HEX field length 1…15'));
    }else{
      if(String(r.match||'').trim()&&!upbMatchPatternValid(r.match))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Antwortmuster: maximal 9 # oder genau ein {value}':'response pattern: max 9 # or exactly one {value}'));
      if(String(r.set||'').trim()&&!upbWriteTemplateValid(r.set))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Schreibplatzhalter nur {value} oder {value:01}…{value:09}':'write placeholder must be {value} or {value:01}…{value:09}'));
    }
    let scale=Number(r.scale||1),divisor=Number(r.divisor||1),offset=Number(r.offset||0),mask=String(r.bitmask||'').trim(),shift=Number(r.bit_shift||0),tb=String(r.time_base||'none'),tf=String(r.time_display||'raw'),mapMode=String(r.map_mode||'none'),valueMap=String(r.value_map||'').trim();
    if(!Number.isInteger(scale)||scale===0)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Faktor muss eine ganze Zahl ungleich 0 sein':'scale must be a non-zero integer'));
    if(!Number.isInteger(divisor)||divisor<1)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Teiler muss >= 1 sein':'divisor must be >= 1'));
    if(!Number.isInteger(offset))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': Offset');
    if(mask&&!builderMaskValid(mask))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Bitmaske ungültig':'invalid bit mask'));
    if(!Number.isInteger(shift)||shift<0||shift>31)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': Bit-Shift 0…31');
    if(valueMap&&!builderValueMapValid(valueMap))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Text-Mapping ungültig (z. B. 0=OK|1=Fehler)':'invalid text mapping (e.g. 0=OK|1=Fault)'));
    if(new TextEncoder().encode(valueMap).length>191)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Text-Mapping maximal 191 Byte':'text mapping max 191 bytes'));
    if(new TextEncoder().encode(String(r.map_default||'')).length>31)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Fallback-Text maximal 31 Byte':'fallback text max 31 bytes'));
    if(!['none','exact','flags'].includes(mapMode))issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': Mapping mode');
    if(mapMode!=='none'&&access!=='ro')issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Text-Mapping ist für Lese-Entities vorgesehen':'text mapping is intended for read-only entities'));
    if(tf==='dhm'&&tb==='none')issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Für d/h/m muss eine Zeitbasis gewählt werden':'d/h/m requires a time base'));
    if(tf==='dhm'&&access!=='ro'&&type==='number')issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'d/h/m ist für schreibbare Zahlen nicht zulässig':'d/h/m is not supported for writable numbers'));
    if((access==='rw'||access==='wo')){
      let writable=(type==='switch'||type==='binary_sensor')?(String(r.set_on||'').trim()||String(r.set_off||'').trim()):String(r.set||'').trim();
      if(!writable)issues.push((uiLang=='de'?'Zeile ':'Row ')+n+': '+(uiLang=='de'?'Schreibtelegramm fehlt':'write telegram missing'));
    }
  });
  if(new TextEncoder().encode(text).length>8192)issues.push(uiLang=='de'?'Profil ist größer als 8192 Byte.':'Profile exceeds 8192 bytes.');
  return issues;
}
function universalProtocolBuilderMsg(addr,msg,kind=''){
  let e=document.getElementById('upb_msg_'+addr);if(e){e.textContent=msg||'';e.className='upb-msg '+kind}
}
function universalProtocolBuilderApplyExpert(addr,text,top,dirty=true){
  let raw=document.getElementById('uni_profile_text_'+addr);
  if(raw){raw.value=text;dirty?markFieldDirty(raw):clearFieldDirty(raw)}
  universalSetForm(addr,top,true,dirty);
}
function universalProtocolBuilderGenerate(addr,applyExpert=true){
  let text=universalProtocolBuilderProfile(addr),issues=universalProtocolBuilderValidate(addr,text);
  let prev=document.getElementById('upb_preview_'+addr);if(prev)prev.value=text;
  if(issues.length){
    universalProtocolBuilderMsg(addr,issues.slice(0,3).join(' · '),'warn');
    return {ok:false,text,issues};
  }
  let top=universalProtocolBuilderTop(addr);
  if(applyExpert)universalProtocolBuilderApplyExpert(addr,text,top,true);
  universalProtocolBuilderMsg(addr,(uiLang=='de'?'Profil erzeugt: ':'Profile generated: ')+new TextEncoder().encode(text).length+' B','ok');
  return {ok:true,text,top,issues:[]};
}
async function universalProtocolBuilderLoadModule(addr,silent=false){
  try{
    if(!silent)universalProtocolBuilderMsg(addr,uiLang=='de'?'Lese Profil aus dem Modul...':'Reading profile from module...');
    let r=await fetch('/universal/profile/read?addr='+addr,{cache:'no-store'});
    let text=fixText(await r.text());if(!r.ok)throw text||r.statusText;
    if(text.trim())universalProtocolBuilderFromProfile(addr,text);
    else{
      let m=universalCurrentModule(addr),cur=universalStateConfig(m||{type:7});
      universalProtocolBuilderSetTop(addr,cur,true);universalProtocolBuilderRenderRows(addr,[]);
    }
    let trunc=r.headers.get('X-OFE-Truncated')==='1';
    universalProtocolBuilderMsg(addr,trunc?(uiLang=='de'?'Profil geladen, aber gekürzt.':'Profile loaded but truncated.'):(uiLang=='de'?'Aktuelles Modulprofil geladen.':'Current module profile loaded.'),trunc?'warn':'ok');
  }catch(e){
    let m=universalCurrentModule(addr),cur=universalStateConfig(m||{type:7});
    universalProtocolBuilderSetTop(addr,cur,true);
    universalProtocolBuilderMsg(addr,(uiLang=='de'?'Modulprofil konnte nicht geladen werden: ':'Could not load module profile: ')+String(e),'warn');
  }
}
function universalProtocolBuilderExport(addr){
  let g=universalProtocolBuilderGenerate(addr,false);if(!g.ok)return;
  let top=universalProtocolBuilderTop(addr),safe=String(top.profile||'universal_rs232').replace(/[^a-z0-9_-]+/gi,'_').replace(/^_+|_+$/g,'')||'universal_rs232';
  let blob=new Blob([g.text],{type:'text/plain;charset=utf-8'}),a=document.createElement('a'),url=URL.createObjectURL(blob);
  a.href=url;a.download=safe+'.ofeprofile';document.body.appendChild(a);a.click();
  setTimeout(()=>{URL.revokeObjectURL(url);a.remove()},250);
  universalProtocolBuilderMsg(addr,uiLang=='de'?'Protocol-Builder-Profil exportiert.':'Protocol Builder profile exported.','ok');
}
async function universalProtocolBuilderSave(addr){
  let g=universalProtocolBuilderGenerate(addr,false);if(!g.ok)return false;
  let body=new URLSearchParams(),top=g.top;
  body.set('addr',addr);body.set('profile',top.profile);body.set('station',top.station);
  body.set('baud',top.baud);body.set('frame',top.frame);body.set('protocol',top.protocol);
  body.set('checksum',top.checksum);body.set('line_end',top.line_end);body.set('profile_text',g.text);
  universalProtocolBuilderMsg(addr,uiLang=='de'?'Speichere Protocol Builder Profil ins Modul...':'Saving Protocol Builder profile to module...');
  let r=await fetch('/universal/profile',{method:'POST',body,cache:'no-store'}),txt=await r.text();
  if(!r.ok){universalProtocolBuilderMsg(addr,txt||r.statusText,'err');alert(txt||r.statusText);return false}
  let root=document.getElementById('upb_editor_'+addr);if(root){root.dataset.originalProfile=g.text;clearDirtyIn(root)}
  universalProtocolBuilderApplyExpert(addr,g.text,top,false);
  delete universalDescriptorCache[addr];
  universalProtocolBuilderMsg(addr,txt||(uiLang=='de'?'Im Universal-Modul gespeichert.':'Saved to Universal module.'),'ok');
  setTimeout(()=>load(true),350);
  return true;
}
function universalProtocolBuilderDirty(){
  let root=document.querySelector('#upb_modal_overlay .upb-editor');
  return !!(root&&[...root.querySelectorAll('input,select,textarea')].some(e=>e.dataset.dirty==='1'));
}
function universalProtocolBuilderClose(force=false){
  let overlay=document.getElementById('upb_modal_overlay');if(!overlay)return true;
  if(!force&&universalProtocolBuilderDirty()){
    let ok=confirm(uiLang=='de'
      ?'Im Universal Protocol Builder gibt es ungespeicherte Änderungen. Editor trotzdem schließen?'
      :'The Universal Protocol Builder contains unsaved changes. Close anyway?');
    if(!ok)return false;
  }
  overlay.remove();document.body.classList.remove('mb-modal-open');return true;
}
async function universalProtocolBuilderOpen(addr){
  addr=Number(addr);
  let m=((window.lastState&&window.lastState.modules)||[]).find(x=>Number(x.addr)===addr&&Number(x.type)===7);
  if(!m)return;
  if(document.getElementById('mb_modal_overlay')&&!modbusBuilderClose(false))return;
  if(document.getElementById('upb_modal_overlay')&&!universalProtocolBuilderClose(false))return;

  let overlay=document.createElement('div');overlay.id='upb_modal_overlay';overlay.className='mb-modal-backdrop';
  overlay.innerHTML=`<section class="mb-modal-window" role="dialog" aria-modal="true" aria-label="Universal RS232 Protocol Builder">
    <div class="mb-modal-head">
      <div class="mb-modal-title">
        <span class="mb-modal-icon">U</span>
        <span class="mb-modal-title-copy">
          <strong>Universal RS232 Protocol Builder</strong>
          <small>${escHtml(amn(m))} · ${uiLang=='de'?'Telegramme statt Descriptor/Profiletext':'telegrams instead of descriptor/profile text'}</small>
        </span>
      </div>
      <button type="button" class="secondary mb-modal-close" onclick="universalProtocolBuilderClose(false)" title="${uiLang=='de'?'Schließen':'Close'}">×</button>
    </div>
    <div class="mb-modal-scroll">
      <div class="upb-editor" id="upb_editor_${addr}">
        <div class="upb-intro"><strong>${uiLang=='de'?'So funktioniert es:':'How it works:'}</strong> ${uiLang=='de'
          ?'Du trägst die Telegramme so ein, wie du sie beim Reverse Engineering gesehen hast. Beispiel: Abfrage S, Antwort S030 → Muster S###. OFE erzeugt daraus intern das bestehende Universal-Profil, die Entities und das Trace-Decoding.'
          :'Enter telegrams as observed while reverse engineering. Example: query S, response S030 -> pattern S###. OFE generates the existing Universal profile, entities and trace decoding internally.'}</div>
        <div class="upb-device-grid">
          <label>${uiLang=='de'?'Profilname':'Profile name'}<input id="upb_profile_${addr}" value=""></label>
          <label>${uiLang=='de'?'Gerät / Station':'Device / station'}<input id="upb_station_${addr}" value=""></label>
          <label>Baud<input id="upb_baud_${addr}" type="number" min="300" max="1000000" value="9600"></label>
          <label>Frame<select id="upb_frame_${addr}"><option>8N1</option><option>8E1</option><option>8O1</option><option>7E1</option></select></label>
          <label>${uiLang=='de'?'Protokollname':'Protocol name'}<input id="upb_protocol_${addr}" value="ASCII" list="upb_proto_list_${addr}" onchange="universalProtocolBuilderProtocolMode(${addr})" oninput="universalProtocolBuilderProtocolMode(${addr})"><datalist id="upb_proto_list_${addr}"><option value="ASCII"><option value="WELLER_ASCII"><option value="BINARY"><option value="CUSTOM"></datalist></label>
          <label>${uiLang=='de'?'Prüfsumme':'Checksum'}<select id="upb_checksum_${addr}"><option>NONE</option><option>WELLER_SUM8</option><option>XOR8_HEX</option><option>SUM8_HEX</option><option>XOR8_RAW</option><option>SUM8_RAW</option><option>CRC16_MODBUS_LE</option><option>CRC16_CCITT_BE</option><option>CRC16_CCITT_LE</option></select></label>
          <label>${uiLang=='de'?'Zeilenende':'Line ending'}<select id="upb_line_${addr}"><option>CR</option><option>LF</option><option>CRLF</option><option>NONE</option></select></label>
          <label class="upb-binary-top" hidden>${uiLang=='de'?'Binär-Framing':'Binary framing'}<select id="upb_bin_frame_${addr}" onchange="upbBinaryFrameChanged(${addr})"><option>IDLE</option><option>FIXED</option><option>LENGTH_U8</option><option>LENGTH_U16_LE</option><option>LENGTH_U16_BE</option></select></label>
          <label class="upb-binary-top" hidden>${uiLang=='de'?'Startbytes / Sync':'Start bytes / sync'}<input id="upb_bin_start_${addr}" value="" placeholder="AA 55"></label>
          <label class="upb-binary-top" hidden>${uiLang=='de'?'Feste RX-Länge':'Fixed RX length'}<input id="upb_bin_rxlen_${addr}" type="number" min="1" max="192" value="8"></label>
          <label class="upb-binary-top" hidden>${uiLang=='de'?'Längenfeld Offset':'Length field offset'}<input id="upb_bin_lenoff_${addr}" type="number" min="0" max="191" value="1"></label>
          <label class="upb-binary-top" hidden>${uiLang=='de'?'Längen-Korrektur':'Length adjust'}<input id="upb_bin_lenadj_${addr}" type="number" min="-192" max="192" value="0" title="${uiLang=='de'?'Gesamtlänge = Längenfeld + Korrektur':'Total length = length field + adjust'}"></label>
        </div>
        <div class="builder-output-assistant">
          <div class="builder-output-head">
            <span class="builder-output-title"><strong>${uiLang=='de'?'Verwendung als Hauptausgang':'Use as main output'}</strong><small>${uiLang=='de'?'OFE ordnet die ausgewählten Werte automatisch den internen Hauptausgang-Rollen zu.':'OFE automatically assigns the selected values to the internal main-output roles.'}</small></span>
            <label class="builder-output-toggle"><input id="upb_output_use_${addr}" type="checkbox" onchange="universalProtocolBuilderOutputChanged(${addr},this)">${uiLang=='de'?'Hauptausgang aktiv':'Main output enabled'}</label>
          </div>
          <div class="builder-output-grid">
            <label>${uiLang=='de'?'Leistung steuern über':'Control power with'}<select id="upb_output_power_${addr}" onchange="universalProtocolBuilderOutputChanged(${addr},this)" disabled><option value="">${uiLang=='de'?'nicht verwendet':'not used'}</option></select></label>
            <label>${uiLang=='de'?'Ein/Aus steuern über':'Control ON/OFF with'}<select id="upb_output_enable_${addr}" onchange="universalProtocolBuilderOutputChanged(${addr},this)" disabled><option value="">${uiLang=='de'?'nicht verwendet':'not used'}</option><option value="__power__">${uiLang=='de'?'über Leistungswert (0 % = AUS)':'derive from power (0% = OFF)'}</option></select></label>
            <label>${uiLang=='de'?'Drehzahl-Rückmeldung über':'RPM feedback from'}<select id="upb_output_rpm_${addr}" onchange="universalProtocolBuilderOutputChanged(${addr},this)" disabled><option value="">${uiLang=='de'?'nicht verwendet':'not used'}</option></select></label>
          </div>
          <div class="builder-output-note">${uiLang=='de'?'Bei „über Leistungswert“ wird 0 % zum Ausschalten gesendet. Für die Drehzahl kann eine lesbare Sensor-/Zahlen-Entity als main_output_rpm gewählt werden.':'With power-derived ON/OFF, 0% is sent to switch off. A readable sensor/number entity can be selected as main_output_rpm for RPM feedback.'}</div>
        </div>
        <div class="upb-help">
          <div class="upb-help-card"><strong>S# … S#########</strong>${uiLang=='de'?'Jedes # erwartet genau eine Ziffer; 1 bis 9 Stellen sind vorgesehen. S030 mit S### liefert 30.':'Each # expects exactly one digit; 1 to 9 digits are intended. S030 with S### yields 30.'}</div>
          <div class="upb-help-card"><strong>V{value}</strong>${uiLang=='de'?'{value} liest bzw. schreibt einen variabel langen Zahlenwert.':'{value} reads or writes a variable-length numeric value.'}</div>
          <div class="upb-help-card"><strong>d{value:01} … d{value:09}</strong>${uiLang=='de'?'Beim Schreiben legt :01 bis :09 die Mindestbreite fest und füllt links mit Nullen auf. Beispiel 7 + :04 → 0007.':'When writing, :01 through :09 set the minimum width and pad with leading zeros. Example 7 + :04 -> 0007.'}</div>
        </div>
        <div class="upb-builder-head">
          <div><strong>${uiLang=='de'?'Werte und Befehle':'Values and commands'}</strong><br><small>${uiLang=='de'?'max. 32 Einträge pro Universal-Modul':'max. 32 entries per Universal module'}</small></div>
          <div class="upb-add-actions">
            <button type="button" class="secondary" onclick="universalProtocolBuilderAddRow(${addr},'sensor')">+ ${uiLang=='de'?'Messwert':'Sensor'}</button>
            <button type="button" class="secondary" onclick="universalProtocolBuilderAddRow(${addr},'number')">+ ${uiLang=='de'?'Regler':'Number'}</button>
            <button type="button" class="secondary" onclick="universalProtocolBuilderAddRow(${addr},'switch')">+ ${uiLang=='de'?'Schalter':'Switch'}</button>
            <button type="button" class="secondary" onclick="universalProtocolBuilderAddRow(${addr},'select')">+ Select</button>
            <button type="button" class="secondary" onclick="universalProtocolBuilderAddRow(${addr},'button')">+ ${uiLang=='de'?'Aktion':'Action'}</button>
          </div>
        </div>
        <div id="upb_rows_${addr}" class="upb-rows"><div class="upb-empty">${uiLang=='de'?'Lade aktuelles Modulprofil...':'Loading current module profile...'}</div></div>
        <div class="upb-actions">
          <button type="button" class="secondary" onclick="universalProtocolBuilderLoadModule(${addr},false)">${uiLang=='de'?'Aus Modul laden':'Load from module'}</button>
          <button type="button" class="secondary" onclick="universalProtocolBuilderGenerate(${addr},true)">${uiLang=='de'?'Profil erzeugen':'Generate profile'}</button>
          <button type="button" class="secondary" onclick="universalProtocolBuilderExport(${addr})">Export</button>
          <button type="button" class="uni-primary-action" onclick="universalProtocolBuilderSave(${addr})">${uiLang=='de'?'Erzeugen & speichern':'Generate & save'}</button>
        </div>
        <div id="upb_msg_${addr}" class="upb-msg"></div>
        <details class="upb-preview"><summary>${uiLang=='de'?'Erzeugten Profiltext anzeigen (Experte)':'Show generated profile text (expert)'}</summary><textarea id="upb_preview_${addr}" data-no-dirty="1" readonly></textarea></details>
        <div class="upb-limit-note">${uiLang=='de'
          ?'Der Builder erzeugt das bereits vom Universal-Modul unterstützte .ofeprofile-Format. Für Spezialfälle bleibt der Experten-Profiltext in der Modulkarte erhalten.'
          :'The builder generates the existing .ofeprofile format already supported by the Universal module. The expert profile editor in the module card remains available for special cases.'}</div>
      </div>
    </div>
  </section>`;
  document.body.appendChild(overlay);document.body.classList.add('mb-modal-open');
  let panel=overlay.querySelector('.mb-modal-window');if(panel)panel.addEventListener('mousedown',e=>e.stopPropagation());
  overlay.addEventListener('mousedown',e=>{
    if(e.target===overlay){let win=overlay.querySelector('.mb-modal-window');if(win)win.animate([{transform:'scale(1)'},{transform:'scale(.997)'},{transform:'scale(1)'}],{duration:140});}
  });
  await universalProtocolBuilderLoadModule(addr,true);
  universalProtocolBuilderOutputRefresh(addr);
}
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'&&document.getElementById('upb_modal_overlay')){e.preventDefault();universalProtocolBuilderClose(false)}
});

function modbusBuilderOutputOptions(addr,kind,current=''){
  let rows=modbusBuilderRows(addr),opts=[];
  rows.forEach((row,i)=>{
    let area=row.querySelector('.mb-area')?.value||'holding',access=row.querySelector('.mb-access')?.value||'ro';
    let selected=row.querySelector('.mb-type')?.value||'auto',type=selected==='auto'?modbusBuilderAutoType(area,access):selected;
    let writable=access==='rw'||access==='wo';
    let name=row.querySelector('.mb-name')?.value.trim()||('Register '+(i+1));
    if(kind==='power'&&type==='number'&&writable)opts.push([String(i),name]);
    if(kind==='enable'&&type==='switch'&&writable)opts.push([String(i),name]);
    if(kind==='rpm'&&(type==='sensor'||type==='number')&&(access==='ro'||access==='rw'))opts.push([String(i),name]);
  });
  let html='<option value="">'+(uiLang=='de'?'nicht verwendet':'not used')+'</option>';
  if(kind==='enable')html+='<option value="__power__">'+(uiLang=='de'?'über Leistungsregister (0 = AUS)':'derive from power register (0 = OFF)')+'</option>';
  html+=opts.map(([v,l])=>`<option value="${v}"${String(current)===v?' selected':''}>${escHtml(l)}</option>`).join('');
  return html;
}
function modbusBuilderOutputChanged(addr,el){
  if(el)markFieldDirty(el);
  modbusBuilderOutputRefresh(addr);
  universalDirtyBadge(addr);
}
function modbusBuilderOutputRefresh(addr){
  let p=document.getElementById('mb_output_power_'+addr),e=document.getElementById('mb_output_enable_'+addr),r=document.getElementById('mb_output_rpm_'+addr);
  if(!p||!e||!r)return;
  let pv=p.value,ev=e.value,rv=r.value;
  p.innerHTML=modbusBuilderOutputOptions(addr,'power',pv);
  e.innerHTML=modbusBuilderOutputOptions(addr,'enable',ev);
  r.innerHTML=modbusBuilderOutputOptions(addr,'rpm',rv);
  if([...p.options].some(o=>o.value===pv))p.value=pv;
  if([...e.options].some(o=>o.value===ev))e.value=ev;
  if([...r.options].some(o=>o.value===rv))r.value=rv;
  let on=!!document.getElementById('mb_output_use_'+addr)?.checked;
  p.disabled=!on;e.disabled=!on;r.disabled=!on;
}
function modbusBuilderOutputLoad(addr){
  let rows=modbusBuilderRows(addr),power=-1,enable=-1,rpm=-1;
  rows.forEach((r,i)=>{
    let role=String(r.querySelector('.mb-role')?.value||'').toLowerCase();
    if(role==='main_output_power')power=i;
    if(role==='main_output_enable')enable=i;
    if(role==='main_output_rpm')rpm=i;
  });
  if(rpm<0){
    rows.some((r,i)=>{let area=r.querySelector('.mb-area')?.value||'holding',access=r.querySelector('.mb-access')?.value||'ro',selected=r.querySelector('.mb-type')?.value||'auto',type=selected==='auto'?modbusBuilderAutoType(area,access):selected,unit=String(r.querySelector('.mb-unit')?.value||'').toLowerCase();if((type==='sensor'||type==='number')&&(access==='ro'||access==='rw')&&unit==='rpm'){rpm=i;return true}return false});
  }
  let use=document.getElementById('mb_output_use_'+addr);
  if(use)use.checked=power>=0||enable>=0||rpm>=0;
  modbusBuilderOutputRefresh(addr);
  let p=document.getElementById('mb_output_power_'+addr),e=document.getElementById('mb_output_enable_'+addr),r=document.getElementById('mb_output_rpm_'+addr);
  if(p&&power>=0)p.value=String(power);
  if(e)e.value=enable>=0?String(enable):(power>=0?'__power__':'');
  if(r&&rpm>=0)r.value=String(rpm);
  modbusBuilderOutputRefresh(addr);
}
function modbusBuilderApplyOutputRoles(addr,defs){
  defs=(defs||[]).map(d=>({...d}));
  defs.forEach(d=>{
    let role=String(d.role||'').toLowerCase();
    if(role==='main_output_enable'||role==='main_output_power'||role==='main_output_rpm')d.role='';
  });
  if(!document.getElementById('mb_output_use_'+addr)?.checked)return defs;
  let pv=document.getElementById('mb_output_power_'+addr)?.value||'';
  let ev=document.getElementById('mb_output_enable_'+addr)?.value||'';
  let rv=document.getElementById('mb_output_rpm_'+addr)?.value||'';
  if(pv!==''&&defs[Number(pv)])defs[Number(pv)].role='main_output_power';
  if(ev!==''&&ev!=='__power__'&&defs[Number(ev)])defs[Number(ev)].role='main_output_enable';
  if(rv!==''&&defs[Number(rv)])defs[Number(rv)].role='main_output_rpm';
  return defs;
}

function modbusBuilderEditorHtml(m){
  let a=Number(m.addr);
  return `<div class="modbus-builder" id="mb_builder_${a}">
    <div class="modbus-builder-body">
      <p class="mb-intro">${uiLang=='de'
        ?'Register wie im Gerätehandbuch eintragen. OFE erzeugt daraus automatisch das interne Modbus-Profil. 4xxxx/3xxxx/1xxxx/0xxxx-Referenzen und 0-basierte Adressen werden verstanden.'
        :'Enter registers as listed in the device manual. OFE automatically generates the internal Modbus profile. 4xxxx/3xxxx/1xxxx/0xxxx references and zero-based addresses are accepted.'}</p>
      <div class="mb-device-grid">
        <label>${uiLang=='de'?'Profilname':'Profile name'}<input id="mb_profile_${a}" value=""></label>
        <label>${uiLang=='de'?'Gerät':'Device'}<input id="mb_station_${a}" value=""></label>
        <label>Baud<input id="mb_baud_${a}" type="number" min="300" max="1000000" value="9600"></label>
        <label>Frame<select id="mb_frame_${a}"><option>8N1</option><option>8E1</option><option>8O1</option><option>7E1</option></select></label>
        <label>Slave ID<input id="mb_slave_${a}" type="number" min="1" max="247" value="1"></label>
        <label>${uiLang=='de'?'Standard-Polling':'Default polling'}<input id="mb_poll_${a}" type="number" min="100" max="60000" step="100" value="500"></label>
      </div>

      <div class="builder-output-assistant">
        <div class="builder-output-head">
          <span class="builder-output-title"><strong>${uiLang=='de'?'Verwendung als Hauptausgang':'Use as main output'}</strong><small>${uiLang=='de'?'Register auswählen; OFE setzt die Hauptausgang-Rollen automatisch.':'Select registers and OFE assigns the main-output roles automatically.'}</small></span>
          <label class="builder-output-toggle"><input id="mb_output_use_${a}" type="checkbox" onchange="modbusBuilderOutputChanged(${a},this)">${uiLang=='de'?'Hauptausgang aktiv':'Main output enabled'}</label>
        </div>
        <div class="builder-output-grid">
          <label>${uiLang=='de'?'Leistung steuern über':'Control power with'}<select id="mb_output_power_${a}" onchange="modbusBuilderOutputChanged(${a},this)" disabled><option value="">${uiLang=='de'?'nicht verwendet':'not used'}</option></select></label>
          <label>${uiLang=='de'?'Ein/Aus steuern über':'Control ON/OFF with'}<select id="mb_output_enable_${a}" onchange="modbusBuilderOutputChanged(${a},this)" disabled><option value="">${uiLang=='de'?'nicht verwendet':'not used'}</option><option value="__power__">${uiLang=='de'?'über Leistungsregister (0 = AUS)':'derive from power register (0 = OFF)'}</option></select></label>
          <label>${uiLang=='de'?'Drehzahl-Rückmeldung über':'RPM feedback from'}<select id="mb_output_rpm_${a}" onchange="modbusBuilderOutputChanged(${a},this)" disabled><option value="">${uiLang=='de'?'nicht verwendet':'not used'}</option></select></label>
        </div>
        <div class="builder-output-note">${uiLang=='de'?'Typisch: Coil/Schalter für Ein/Aus, schreibbares Register für Leistung und optional ein lesbares RPM-Register für die Drehzahl-Rückmeldung.':'Typical: coil/switch for ON/OFF, writable register for power and optionally a readable RPM register for speed feedback.'}</div>
      </div>

      <div class="mb-builder-head">
        <div><strong>${uiLang=='de'?'Register':'Registers'}</strong><br><small>${uiLang=='de'?'max. 16 Einträge pro Modbus-Modul':'max. 16 entries per Modbus module'}</small></div>
        <button type="button" class="secondary" onclick="modbusBuilderAddRow(${a})">+ ${uiLang=='de'?'Register hinzufügen':'Add register'}</button>
      </div>
      <div id="mb_rows_${a}" class="mb-rows"><div class="mb-empty">${uiLang=='de'?'Noch keine Register angelegt.':'No registers configured yet.'}</div></div>

      <div class="mb-builder-actions">
        <button type="button" class="secondary" onclick="modbusBuilderLoadModule(${a})">${uiLang=='de'?'Aus Modul laden':'Load from module'}</button>
        <button type="button" class="secondary" onclick="modbusBuilderGenerate(${a})">${uiLang=='de'?'Profil erzeugen':'Generate profile'}</button>
        <button type="button" class="secondary" onclick="modbusBuilderExport(${a})">Export</button>
        <button type="button" class="uni-primary-action" onclick="modbusBuilderSave(${a})">${uiLang=='de'?'Erzeugen & speichern':'Generate & save'}</button>
      </div>
      <div id="mb_msg_${a}" class="mb-builder-msg"></div>
      <div class="mb-limit-note">${uiLang=='de'
        ?'Builder v1 nutzt die bereits unterstützten Modbus-Typen: Coil/Discrete Input sowie 16-Bit Input/Holding Register. Der Experten-Profiltext in der Modulkarte bleibt für Sonderfälle verfügbar.'
        :'Builder v1 uses the already supported Modbus types: coil/discrete input and 16-bit input/holding registers. The expert profile editor in the module card remains available for special cases.'}</div>
    </div>
  </div>`;
}

function modbusBuilderHtml(m){
  let a=Number(m.addr);
  return `<button type="button" class="modbus-builder-launch" onclick="modbusBuilderOpen(${a})">
    <span class="modbus-builder-launch-copy">
      <strong>Modbus Register Map Builder</strong>
      <small>${uiLang=='de'?'Register komfortabel im großen Editor bearbeiten':'Edit registers comfortably in the large editor'}</small>
    </span>
    <span class="modbus-builder-launch-open">${uiLang=='de'?'Editor öffnen':'Open editor'} ↗</span>
  </button>`;
}

function modbusBuilderModalDirty(){
  let root=document.querySelector('#mb_modal_overlay .modbus-builder');
  return !!(root&&(root.dataset.builderDirty==='1'||[...root.querySelectorAll('input,select,textarea')].some(e=>e.dataset.dirty==='1')));
}

function modbusBuilderClose(force=false){
  let overlay=document.getElementById('mb_modal_overlay');
  if(!overlay)return true;
  if(!force&&modbusBuilderModalDirty()){
    let ok=confirm(uiLang=='de'
      ?'Im Modbus Builder gibt es ungespeicherte Änderungen. Editor trotzdem schließen?'
      :'The Modbus Builder contains unsaved changes. Close the editor anyway?');
    if(!ok)return false;
  }
  overlay.remove();
  document.body.classList.remove('mb-modal-open');
  return true;
}

function modbusBuilderOpen(addr){
  addr=Number(addr);
  let m=((window.lastState&&window.lastState.modules)||[]).find(x=>Number(x.addr)===addr&&Number(x.type)===8);
  if(!m)return;
  if(document.getElementById('upb_modal_overlay')&&!universalProtocolBuilderClose(false))return;

  let existing=document.getElementById('mb_modal_overlay');
  if(existing&&!modbusBuilderClose(false))return;

  let overlay=document.createElement('div');
  overlay.id='mb_modal_overlay';
  overlay.className='mb-modal-backdrop';
  overlay.innerHTML=`<section class="mb-modal-window" role="dialog" aria-modal="true" aria-label="Modbus Register Map Builder">
    <div class="mb-modal-head">
      <div class="mb-modal-title">
        <span class="mb-modal-icon">M</span>
        <span class="mb-modal-title-copy">
          <strong>Modbus Register Map Builder</strong>
          <small>${escHtml(amn(m))} · ${hx(m.addr)} · ${uiLang=='de'?'großer Editor':'large editor'}</small>
        </span>
      </div>
      <button type="button" class="secondary mb-modal-close" onclick="modbusBuilderClose(false)" title="${uiLang=='de'?'Schließen':'Close'}">×</button>
    </div>
    <div class="mb-modal-scroll">${modbusBuilderEditorHtml(m)}</div>
  </section>`;
  document.body.appendChild(overlay);
  document.body.classList.add('mb-modal-open');

  let panel=overlay.querySelector('.mb-modal-window');
  if(panel)panel.addEventListener('mousedown',e=>e.stopPropagation());
  overlay.addEventListener('mousedown',e=>{
    if(e.target===overlay){
      // Deliberately do not close on accidental backdrop clicks.
      let win=overlay.querySelector('.mb-modal-window');
      if(win){win.animate([{transform:'scale(1)'},{transform:'scale(.997)'},{transform:'scale(1)'}],{duration:140});}
    }
  });

  modbusBuilderSync(m);
  modbusBuilderOutputRefresh(addr);
  let first=document.getElementById('mb_profile_'+addr);
  if(first)setTimeout(()=>first.focus({preventScroll:true}),0);
}

document.addEventListener('keydown',e=>{
  if(e.key==='Escape'&&document.getElementById('mb_modal_overlay')){
    e.preventDefault();
    modbusBuilderClose(false);
  }
});

function jbcUsbDecodeDetail(m){let total=Number((m&&m.jbc_usb_decode_errors)||0);if(!total)return '-';let cmd=Number(m.jbc_usb_decode_last_cmd||0)&255,got=Number(m.jbc_usb_decode_last_got_len||0)&255,emin=Number(m.jbc_usb_decode_last_expected_min),emax=Number(m.jbc_usb_decode_last_expected_max);let exp=(emin===255||emax===255)?'shape':(emax===254?('>='+emin):(emin===emax?String(emin):(emin+'..'+emax)));let cs=m.jbc_usb_decode_top_cmd||[],ns=m.jbc_usb_decode_top_count||[],top=[];for(let i=0;i<Math.min(3,cs.length,ns.length);i++)if(Number(ns[i]||0)>0)top.push('0x'+Number(cs[i]||0).toString(16).toUpperCase().padStart(2,'0')+':'+Number(ns[i]||0));return 'last 0x'+cmd.toString(16).toUpperCase().padStart(2,'0')+' exp '+exp+' got '+got+(top.length?' · top '+top.join(' · '):'')}
function jbcUsbProtoName(v){v=Number(v||0);return v==1?'Protocol 01 (P01)':(v==2?'Protocol 02 (P02)':(uiLang=='de'?'Erkennung läuft':'Detecting'))}
function jbcUsbFrameProtoName(v){v=Number(v||0);return v==1?'P01':(v==2?'P02':'-')}
function jbcUsbLinkState(v){v=Number(v||0);let de=['USB getrennt','Erkennung','P01: ACK','P01: Adresse','Firmware','Verbunden'],en=['USB down','Detecting','P01: ACK','P01: address','Firmware','Connected'];let a=uiLang=='de'?de:en;return a[v]||'-'}
const jbcUsbStationIdentity=new Map();
function jbcUsbStationKind(m){
  let x=String((m&&m.jbc_usb_model)||'').replace(/[\/\s-]/g,'').toUpperCase(),kind='UNKNOWN';
  if(['CA','CDCF','CDN','CP','CSCV','CDE','CAE','CPE','CSVE','DD','DDE','DDR','DI','DM','DME','HD','HDE','HDR','LC','NA','NAE','PSE','SM','WS','ALE'].includes(x))kind='SOLD';
  else if(x=='JT'||x=='JTSE')kind='HA';
  else if(x=='SF')kind='SF';
  else if(['F1','F2W','F2','F4W'].includes(x))kind='FE';
  else if(['PH','PHBE','PHNE','PHSE','PHXL'].includes(x))kind='PH';
  else if(x=='CLM'||x=='CLMU')kind='CL';
  if(m&&Number(m.addr)){
    const addr=Number(m.addr),identity=[kind,x,String(m.jbc_usb_model_type||''),Number(m.jbc_usb_model_version||0)].join('|'),previous=jbcUsbStationIdentity.get(addr);
    if(previous&&previous!==identity){
      const ports=document.getElementById('jbu_ports_'+addr),active=document.activeElement;
      if(ports&&active&&ports.contains(active)&&active.blur)active.blur();
    }
    jbcUsbStationIdentity.set(addr,identity);
  }
  return kind;
}
function jbcUsbSupportsCartridgeConfig(m){let model=String((m&&m.jbc_usb_model)||'').toUpperCase(),typ=String((m&&m.jbc_usb_model_type)||'').toUpperCase();return model=='PSE'||(model=='DME'&&typ=='TCH')}
function jbcUsbCartridgeDetailsHtml(m,p,addr,port){let f=Number((p&&p.detail_value_flags)||0)&65535,allowCfg=jbcUsbSupportsCartridgeConfig(m),hasCfg=allowCfg&&!!(f&64),hasI=!!(f&128),hasP=!!(f&256),hasT=!!(f&512);if(!hasCfg&&!hasI&&!hasP&&!hasT)return '';let parts=[];if(hasCfg){parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'Kartuschendaten':'Cartridge data'}</span><strong>${Number(p.cartridge_on||0)?(uiLang=='de'?'aktiv':'enabled'):(uiLang=='de'?'deaktiviert':'disabled')}</strong></div>`);parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'JBC-Kartuschencode':'JBC cartridge code'}</span><strong>${Number(p.cartridge_jbc_code||0)}</strong></div>`);let gf=(Number(p.cartridge_group||0)||Number(p.cartridge_family||0))?`${Number(p.cartridge_group||0)} / ${Number(p.cartridge_family||0)}`:(uiLang=='de'?'nicht gesetzt':'not set');parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'Gruppe / Familie':'Group / family'}</span><strong>${gf}</strong></div>`);parts.push(`<div class="jbu-port-detail-item"><span class="k">Adjust 300 / 400 °C</span><strong>${jbcUsbAdjustTemp(p.cartridge_adjust_300)} / ${jbcUsbAdjustTemp(p.cartridge_adjust_400)}</strong></div>`)}let diag=[];let row=(label,value)=>`<div class="jbu-port-detail-item jbu-cartridge-live"><span class="k">${label}</span><strong>${value}</strong></div>`;let dual=Number(p.tip_temp_b||0)!==0||Number(p.cartridge_ma_b||0)!==0||Number(p.cartridge_power_permille_b||0)!==0;if(hasT){let a=Number(p.tip_temp_a||0),b=Number(p.tip_temp_b||0);if(dual){diag.push(row(uiLang=='de'?'Spitzentemperatur A':'Tip temperature A',jbcUsbTemp(a)));diag.push(row(uiLang=='de'?'Spitzentemperatur B':'Tip temperature B',jbcUsbTemp(b)))}else diag.push(row(uiLang=='de'?'Spitzentemperatur':'Tip temperature',jbcUsbTemp(a)))}if(hasI){let a=Number(p.cartridge_ma_a||0),b=Number(p.cartridge_ma_b||0);if(dual){diag.push(row(uiLang=='de'?'Strom A':'Current A',`${a} mA`));diag.push(row(uiLang=='de'?'Strom B':'Current B',`${b} mA`))}else diag.push(row(uiLang=='de'?'Strom':'Current',`${a} mA`))}if(hasP){let a=Number(p.cartridge_power_permille_a||0),b=Number(p.cartridge_power_permille_b||0);if(dual){diag.push(row(uiLang=='de'?'Leistung A':'Power A',jbcUsbPercent(a)));diag.push(row(uiLang=='de'?'Leistung B':'Power B',jbcUsbPercent(b)))}else diag.push(row(uiLang=='de'?'Leistung':'Power',jbcUsbPercent(a)))}let body=parts.concat(diag).join('');let op=jbcUsbCartDetailIsOpen(addr,port)?' open':'';let title=allowCfg?(uiLang=='de'?'Kartuschendetails':'Cartridge details'):(uiLang=='de'?'Kartuschendiagnose':'Cartridge diagnostics');return `<details class="jbu-port-details jbu-cartridge-details" data-port="${port}"${op} ontoggle="jbcUsbRememberCartDetail(${Number(addr)},${Number(port)},this.open)"><summary>${title}</summary><div class="jbu-port-detail-grid">${body}</div></details>`}
function jbcUsbQstSupported(m){let v=Number((m&&m.jbc_usb_qst_valid_flags)||0)&3;return v!=0}
function jbcUsbKindLabel(k){let de={SOLD:'Lötstation',HA:'Heißluft',SF:'Lötzinnzufuhr',FE:'Absaugung',PH:'Vorheizer',CL:'Reiniger',UNKNOWN:'Unbekannt'},en={SOLD:'Soldering',HA:'Hot air',SF:'Solder feeder',FE:'Fume extractor',PH:'Preheater',CL:'Cleaner',UNKNOWN:'Unknown'};return (uiLang=='de'?de:en)[k]||k}
function jbcUsbTemp(v){v=Number(v);if(!Number.isFinite(v)||v==65535)return '-';return Math.trunc(v/9)+' °C'}
function jbcUsbPercent(v){return (Number(v||0)/10).toFixed(1)+' %'}
function jbcUsbTimeToStop(v){let ds=Math.max(0,Number(v||0));if(!Number.isFinite(ds))return '-';let total=Math.ceil(ds/10),min=Math.floor(total/60),sec=total%60;return min+' min '+String(sec).padStart(2,'0')+' s'}
function jbcUsbMaskCount(mask,ports){let m=Number(mask||0)&255,n=Math.max(0,Math.min(8,Number(ports||0))),c=0;if(n<8)m&=(1<<n)-1;while(m){c+=m&1;m>>>=1}return c}
function jbcUsbGenericToolId(m,p){let raw=Number((p&&p.tool)||0)&255,k=jbcUsbStationKind(m),model=String((m&&m.jbc_usb_model)||'').replace(/[\/\s-]/g,'').toUpperCase();/* JBCStationsData.CStationTools.GetGenericToolFromInternal(): exact DLL behavior. */if(k=='SOLD'){if(model=='HD'||model=='HDE')return 9;if(model=='NA'){if(raw==0)return 0;if(raw==1)return 7;if(raw==3)return 8;return raw}if(model=='ALE')return 10;return raw}if(k=='HA'&&raw>0)return (raw+30)&255;return raw}
function jbcUsbToolName(id){id=Number(id||0)&255;let names={0:'-',1:'T210',2:'T245',3:'PA',4:'HT',5:'DS',6:'DR',7:'NT105',8:'NP105',9:'T470',10:'ALE250',31:'JT',32:'TE',33:'PHS',34:'PHB'};return names[id]||('0x'+id.toString(16).toUpperCase().padStart(2,'0'))}
function jbcUsbToolLabel(m,p){let id=jbcUsbGenericToolId(m,p);return id?jbcUsbToolName(id):(uiLang=='de'?'Kein Werkzeug':'No tool')}
function jbcUsbPortStateText(m,p){if(!p||!p.valid)return uiLang=='de'?'Keine Daten':'No data';let k=jbcUsbStationKind(m),flags=Number(p.flags||0),tool=Number(p.tool||0);if(k=='CL'){let cf=Number(p.cl_flags||0);return (cf&1)&&p.cl_motors_on?'CLEANING':'IDLE'}if(k=='SOLD'){if(!tool)return uiLang=='de'?'KEIN TOOL':'NO TOOL';if(flags&4)return 'HIBERNATION';if(flags&2)return 'SLEEP';if(flags&1)return 'STAND';if(flags&8)return 'EXTRACTOR';return 'WORK'}if(k=='HA'){if(!tool)return uiLang=='de'?'KEIN TOOL':'NO TOOL';if(flags&32)return 'WORK';if(flags&1)return 'STAND';if(flags&64)return 'COOLING';if(flags&128)return 'SUCTION';return 'IDLE'}if(k=='PH')return (flags&32)?'WORK':'IDLE';if(k=='SF'){let sf=Number(p.sf_flags||0)&65535;return (sf&4)&&Number(p.sf_feeding_state||0)?'FEEDING':'IDLE'}return 'IDLE'}
function jbcUsbStateClass(st){if(st=='WORK'||st=='CLEANING'||st=='FEEDING')return 'on';if(st=='STAND'||st=='SLEEP'||st=='HIBERNATION')return 'stand';if(st=='COOLING'||st=='SUCTION'||st=='EXTRACTOR')return 'cooling';return 'idle'}
function jbcUsbDeviceIdText(m){let h=String((m&&m.jbc_usb_device_id)||'').replace(/[^0-9a-f]/ig,'').toUpperCase(),n=Math.min(Number((m&&m.jbc_usb_device_id_len)||0),Math.floor(h.length/2));if(!n)return '-';let b=[];for(let i=0;i<n;i++)b.push(parseInt(h.substr(i*2,2),16));if(n==16){let q=x=>b[x].toString(16).padStart(2,'0').toUpperCase();return q(3)+q(2)+q(1)+q(0)+'-'+q(5)+q(4)+'-'+q(7)+q(6)+'-'+q(8)+q(9)+'-'+q(10)+q(11)+q(12)+q(13)+q(14)+q(15)}let printable=b.every(x=>x>=32&&x<127);if(printable)return String.fromCharCode(...b);return b.map(x=>x.toString(16).padStart(2,'0').toUpperCase()).join(' ')}
function jbcUsbToolErrorCode(m,p){let raw=Number((p&&p.error)||0)&255,k=jbcUsbStationKind(m);if(!raw)return 0;if(k=='HA')return raw+20;if(k=='PH')return raw+40;return raw}
function jbcUsbToolErrorName(m,p){let c=jbcUsbToolErrorCode(m,p),n={0:'NO_ERROR',1:'SHORTCIRCUIT',2:'SHORTCIRCUIT_NR',3:'OPENCIRCUIT',4:'NO_TOOL',5:'WRONGTOOL',6:'DETECTIONTOOL',7:'MAXPOWER',8:'STOPOVERLOAD_MOS',9:'TIN_FEEDER_CLOGGING',21:'AIR_PUMP_ERROR',22:'PROTECION_TC_HIGH',23:'REGULATION_TC_HIGH',24:'EXTERNAL_TC_MISSING',25:'SELECTED_TEMP_NOT_REACHED',26:'HIGH_HEATER_INTENSITY',27:'LOW_HEATER_RESISTANCE',28:'WRONG_HEATER',29:'NOTOOL_HA',30:'DETECTIONTOOL_HA',41:'SELECTED_TEMP_NOT_REACHED_PH',42:'LOW_HEATER_INTENSITY',43:'TC1_NOT_CONNECTED',44:'TC2_NOT_CONNECTED',45:'TC3_NOT_CONNECTED',46:'TC4_NOT_CONNECTED',47:'TC1_LIMIT_REACHED',48:'TC2_LIMIT_REACHED',49:'TC3_LIMIT_REACHED',50:'TC4_LIMIT_REACHED'};return n[c]||('ERROR_'+hx(c))}
function jbcUsbFlagPill(txt,cls=''){return `<span class="jbu-flag ${cls}">${txt}</span>`}
function jbcUsbPortFlagsHtml(m,p){if(!p||!p.valid)return '';let k=jbcUsbStationKind(m),raw=Number(p.detail_flags||0)&255,vf=Number(p.detail_value_flags||0)&65535,primary=jbcUsbPortStateText(m,p),a=[],add=(txt,cls='')=>{if(txt!=primary)a.push(jbcUsbFlagPill(txt,cls))};if(k=='HA'){if(raw&1)add('HEATER','on');if(raw&2)add('HEATER REQ');if(raw&4)add('COOLING','cool');if(raw&8)add('SUCTION','cool');if(raw&16)add('SUCTION REQ');if(raw&32)add('PEDAL','pedal');if(raw&64)add('PEDAL ON','pedal');if(raw&128)add('STAND','stand')}else if(k=='SOLD'&&Number(m.jbc_usb_frame_protocol||0)==2){let f=Number(p.flags||0);if(f&1)add('STAND','stand');if(f&2)add('SLEEP','stand');if(f&4)add('HIBERNATION','stand');if(f&8)add('EXTRACTOR','cool');if(f&16)add('DESOLDER');if(vf&1024){if(raw&32)add('QST LOCK');if(raw&64)add(uiLang=='de'?'AKTIVE REINIGUNG':'ACTIVE CLEANING','cool')}if((vf&2048)&&!(vf&4096))add(uiLang=='de'?'PORT GESPERRT':'PORT DISABLED')}else if(k=='PH'){if(raw&1)add('HEATER','on');if(raw&2)add('ZONE B');if(raw&4)add('ZONE A');if(raw&8)add('INTERNAL FAN','cool');if(raw&16)add('PEDAL','pedal');if(raw&32)add('PEDAL ON','pedal')}else{let f=Number(p.flags||0);if(f&1)add('STAND','stand');if(f&2)add('SLEEP','stand');if(f&4)add('HIBERNATION','stand');if(f&8)add('EXTRACTOR','cool');if(f&16)add('DESOLDER');if(f&32)add('HEATER','on');if(f&64)add('COOLING','cool');if(f&128)add('SUCTION','cool')}return a.length?`<div class="jbu-port-flags">${a.join('')}</div>`:''}
function jbcUsbFutureModeName(v){v=Number(v||0)&255;return v==83?'SLEEP':(v==72?'HIBERNATION':(v==78?'NONE':'-'))}
function jbcUsbCountdown(v){v=Math.max(0,Math.floor(Number(v||0)));let m=Math.floor(v/60),s=v%60;return `${m}:${String(s).padStart(2,'0')}`}
function jbcUsbFutureHtml(m,p){if(jbcUsbStationKind(m)!='SOLD'||!p||!p.valid)return '';let n=jbcUsbFutureModeName(p.future_mode),t=Number(p.time_to_sleep_hibern||0),st=jbcUsbPortStateText(m,p),cf=Number(p.delay_config_flags||0)&255,sd=Number(p.sleep_delay_min||0),hd=Number(p.hiber_delay_min||0);if(t&&n!='NONE'&&n!='-'){let parts=[`${uiLang=='de'?'Nächster Modus':'Next mode'}: <strong>${n}</strong>`,`${uiLang=='de'?'Countdown':'Countdown'} <strong>${jbcUsbCountdown(t)}</strong>`];if(st=='SLEEP'&&(cf&4)){let en=!!(cf&8),txt=en?`${hd} min`:(uiLang=='de'?'aus':'off');parts.push(`${uiLang=='de'?'Hibernation-Verzögerung':'Hibernation delay'} <strong>${txt}</strong>`)}return `<div class="jbu-port-future"><span>${parts.join('</span><span>')}</span></div>`}if(st=='SLEEP'&&(cf&4)){let en=!!(cf&8),txt=en?`${hd} min`:(uiLang=='de'?'aus':'off');return `<div class="jbu-port-future"><span>${uiLang=='de'?'Nächster Modus':'Next mode'}: <strong>${en?'HIBERNATION':'NONE'}</strong></span><span>${uiLang=='de'?'Hibernation-Verzögerung':'Hibernation delay'} <strong>${txt}</strong></span></div>`}let parts=[];if(cf&1)parts.push(`${uiLang=='de'?'Sleep':'Sleep'}: <strong>${(cf&2)?sd+' min':(uiLang=='de'?'aus':'off')}</strong>`);if(cf&4)parts.push(`${uiLang=='de'?'Hibernation':'Hibernation'}: <strong>${(cf&8)?hd+' min':(uiLang=='de'?'aus':'off')}</strong>`);return parts.length?`<div class="jbu-port-future"><span>${parts.join(' · ')}</span></div>`:''}
function jbcUsbStationErrorName(v){v=Number(v);if(v==65535)return '-';let n={0:'NO_ERROR',1:'STOPOVERLOAD_TRAFO',2:'WRONGSENSOR_TRAFO',3:'MEMORY',4:'MAINSFREQUENCY',5:'STATION_MODEL',6:'NOT_MCU_TOOLS'};return n[v]||('ERROR_'+hx(v))}
function jbcUsbAdjustTemp(v){v=Number(v||0);if(!Number.isFinite(v))return '-';let c=Math.trunc(v/9);return (c>0?'+':'')+c+' °C'}
function jbcUsbMinutes(v){v=Math.max(0,Math.floor(Number(v||0)));let d=Math.floor(v/1440),h=Math.floor((v%1440)/60),m=v%60;if(d)return `${d} d ${h} h`;if(h)return `${h} h ${m} min`;return `${m} min`}
function jbcUsbHaExternalMode(v){v=Number(v||0)&255;return v===0?'REGULATION':(v===1?'PROTECTION':('0x'+v.toString(16).toUpperCase().padStart(2,'0')))}
function jbcUsbHaStartMode(v){v=Number(v||0)&255;if(v===0)return uiLang=='de'?'aus':'off';let a=[];if(v&1)a.push(uiLang=='de'?'Werkzeugtaste':'tool button');if(v&2)a.push(uiLang=='de'?'Aus Stand':'stand out');if(v&4)a.push(uiLang=='de'?'Pedal Impuls':'pedal pulse');if(v&8)a.push(uiLang=='de'?'Pedal halten':'pedal hold');let rest=v&~15;if(rest)a.push('0x'+rest.toString(16).toUpperCase().padStart(2,'0'));return a.join(' + ')||('0x'+v.toString(16).toUpperCase().padStart(2,'0'))}
function jbcUsbHaStatusText(v){v=Number(v||0)&255;let a=[];if(v&1)a.push('HEATER');if(v&2)a.push('HEATER REQ');if(v&4)a.push('COOLING');if(v&8)a.push('SUCTION');if(v&16)a.push('SUCTION REQ');if(v&32)a.push('PEDAL');if(v&64)a.push('PEDAL ON');if(v&128)a.push('STAND');return a.length?a.join(' · '):'IDLE'}
function jbcUsbSoldEffectiveSetpoint(p){let f=Number((p&&p.detail_value_flags)||0)&65535;if((f&16)&&Number(p.levels_on||0)){let sel=Number(p.selected_level),lt=p.level_temp||[],lo=p.level_on||[];if(sel>=0&&sel<3&&Number(lo[sel]||0))return jbcUsbTemp(lt[sel]);return '-'}return (f&1)?jbcUsbTemp(p.selected_temp):'-'}
let jbcUsbDetailsOpen={};
function jbcUsbDetailKey(addr,port){return 'ofe_jbu_port_details_'+Number(addr)+'_'+Number(port)}
function jbcUsbDetailIsOpen(addr,port){let k=jbcUsbDetailKey(addr,port);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberDetail(addr,port,open){let k=jbcUsbDetailKey(addr,port);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbCartDetailKey(addr,port){return 'ofe_jbu_cart_details_'+Number(addr)+'_'+Number(port)}
function jbcUsbClDetailKey(addr,port){return 'ofe_jbu_cl_details_'+Number(addr)+'_'+Number(port)}
function jbcUsbClDetailIsOpen(addr,port){let k=jbcUsbClDetailKey(addr,port);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberClDetail(addr,port,open){let k=jbcUsbClDetailKey(addr,port);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbCartDetailIsOpen(addr,port){let k=jbcUsbCartDetailKey(addr,port);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberCartDetail(addr,port,open){let k=jbcUsbCartDetailKey(addr,port);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbSoldDiagDetailKey(addr,port){return 'ofe_jbu_sold_diag_details_'+Number(addr)+'_'+Number(port)}
function jbcUsbSoldDiagDetailIsOpen(addr,port){let k=jbcUsbSoldDiagDetailKey(addr,port);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberSoldDiagDetail(addr,port,open){let k=jbcUsbSoldDiagDetailKey(addr,port);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbHaDiagDetailKey(addr,port){return 'ofe_jbu_ha_diag_details_'+Number(addr)+'_'+Number(port)}
function jbcUsbHaDiagDetailIsOpen(addr,port){let k=jbcUsbHaDiagDetailKey(addr,port);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberHaDiagDetail(addr,port,open){let k=jbcUsbHaDiagDetailKey(addr,port);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbHaStationDiagKey(addr){return 'ofe_jbu_ha_station_diag_'+Number(addr)}
function jbcUsbHaStationDiagIsOpen(addr){let k=jbcUsbHaStationDiagKey(addr);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberHaStationDiag(addr,open){let k=jbcUsbHaStationDiagKey(addr);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbPhStationDiagKey(addr){return 'ofe_jbu_ph_station_diag_'+Number(addr)}
function jbcUsbPhStationDiagIsOpen(addr){let k=jbcUsbPhStationDiagKey(addr);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberPhStationDiag(addr,open){let k=jbcUsbPhStationDiagKey(addr);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbPhOnOff(v){return Number(v||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}
function jbcUsbPhRawMode(v){v=Number(v||0)&255;return String(v)+' (0x'+v.toString(16).toUpperCase().padStart(2,'0')+')'}
function jbcUsbPhWarning(v){v=Number(v||0)&255;return v===0?'OK':((uiLang=='de'?'Warnung ':'Warning ')+v+' (0x'+v.toString(16).toUpperCase().padStart(2,'0')+')')}
function jbcUsbPhStatusText(v){v=Number(v||0)&255;let a=[];if(v&1)a.push('HEATER');if(v&2)a.push('ZONE B');if(v&4)a.push('ZONE A');if(v&8)a.push('FAN');if(v&16)a.push('PEDAL CONNECTED');if(v&32)a.push('PEDAL ON');return a.length?a.join(' · '):'IDLE'}
function jbcUsbSoldDetailsHtml(m,p,addr,port){
  let f=Number((p&&p.detail_value_flags)||0)&65535,xf=Number((p&&p.sold_extra_flags)||0)&65535,rf=Number((p&&p.sold_readonly_port_flags)||0)&65535;if(!f&&!xf&&!rf&&!jbcUsbQstSupported(m))return '';
  let parts=[],raw=Number((p&&p.detail_flags)||0)&255,hasToolStatus=(f&1024)!=0;
  if(jbcUsbQstSupported(m)&&hasToolStatus){let qlock=(raw&32)!=0;parts.push(`<div class="jbu-port-detail-item"><span class="k">QST Lock</span><strong>${qlock?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}</strong></div>`)}
  if(hasToolStatus){let ac=(raw&64)!=0;parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'Aktive Reinigung':'Active cleaning'}</span><strong>${ac?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}</strong></div>`)}
  if(f&2048){let en=(f&4096)!=0;parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'Port freigegeben':'Enabled port'}</span><strong>${en?(uiLang=='de'?'ja':'yes'):(uiLang=='de'?'nein':'no')}</strong></div>`)}
  if(rf&1)parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'Fix-Temperatur':'Fixed temperature'}</span><strong>${p.sold_fixed_temp_on?jbcUsbTemp(p.sold_fixed_temp):(uiLang=='de'?'aus':'off')}</strong></div>`);
  if(f&2)parts.push(`<div class="jbu-port-detail-item"><span class="k">${uiLang=='de'?'Sleep-Temperatur':'Sleep temperature'}</span><strong>${jbcUsbTemp(p.sleep_temp)}</strong></div>`);
  if(f&4)parts.push(`<div class="jbu-port-detail-item"><span class="k">Adjust</span><strong>${jbcUsbAdjustTemp(p.adjust_temp)}</strong></div>`);
  let levels='';if(f&16){let lt=p.level_temp||[],lo=p.level_on||[],sel=Number(p.selected_level),rows='';for(let i=0;i<3;i++){let selected=sel===i;rows+=`<span class="${selected?'is-selected':''}">Level ${i+1} <strong>${Number(lo[i]||0)?jbcUsbTemp(lt[i]):(uiLang=='de'?'aus':'off')}${selected?' · '+(uiLang=='de'?'gewählt':'selected'):''}</strong></span>`}levels=`<div class="jbu-counter-grid jbu-ha-levels"><span>${uiLang=='de'?'Temperaturlevel':'Temperature levels'} <strong>${Number(p.levels_on||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}</strong></span>${rows}</div>`}
  let counters='';if(f&8)counters=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Eingesteckt':'Plugged'} <strong>${jbcUsbMinutes(p.counter_plug_min)}</strong></span><span>Work <strong>${jbcUsbMinutes(p.counter_work_min)}</strong></span><span>Sleep <strong>${jbcUsbMinutes(p.counter_sleep_min)}</strong></span><span>Hibernation <strong>${jbcUsbMinutes(p.counter_hiber_min)}</strong></span><span>Idle <strong>${jbcUsbMinutes(p.counter_idle_min)}</strong></span>${f&32?`<span>Sleep-Zyklen <strong>${Number(p.counter_sleep_cycles||0)}</strong></span><span>Desolder-Zyklen <strong>${Number(p.counter_desold_cycles||0)}</strong></span>`:''}</div>`;
  let partial='';if(xf&1)partial=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Teil Eingesteckt':'Partial plugged'} <strong>${jbcUsbMinutes(p.sold_partial_plug_min)}</strong></span><span>${uiLang=='de'?'Teil Work':'Partial work'} <strong>${jbcUsbMinutes(p.sold_partial_work_min)}</strong></span><span>${uiLang=='de'?'Teil Sleep':'Partial sleep'} <strong>${jbcUsbMinutes(p.sold_partial_sleep_min)}</strong></span><span>${uiLang=='de'?'Teil Hibernation':'Partial hibernation'} <strong>${jbcUsbMinutes(p.sold_partial_hiber_min)}</strong></span><span>${uiLang=='de'?'Teil Idle':'Partial idle'} <strong>${jbcUsbMinutes(p.sold_partial_idle_min)}</strong></span>${xf&2?`<span>${uiLang=='de'?'Teil Sleep-Zyklen':'Partial sleep cycles'} <strong>${Number(p.sold_partial_sleep_cycles||0)}</strong></span><span>${uiLang=='de'?'Teil Desolder-Zyklen':'Partial desolder cycles'} <strong>${Number(p.sold_partial_desold_cycles||0)}</strong></span>`:''}</div>`;
  let scf=Number(p.sold_special_counter_flags||0)&65535,special='';if(scf){let z=[];if(scf&1){z.push(`<span>${uiLang=='de'?'Zinnförder-Zyklen':'Tin delivery cycles'} <strong>${Number(p.sold_tin_deliver_cycles||0)}</strong></span>`);z.push(`<span>${uiLang=='de'?'Zinnförder-Länge':'Tin delivery length'} <strong>${Number(p.sold_tin_length||0)}</strong></span>`)}if(scf&2){z.push(`<span>${uiLang=='de'?'Teil Zinnförder-Zyklen':'Partial tin delivery cycles'} <strong>${Number(p.sold_partial_tin_deliver_cycles||0)}</strong></span>`);z.push(`<span>${uiLang=='de'?'Teil Zinnförder-Länge':'Partial tin delivery length'} <strong>${Number(p.sold_partial_tin_length||0)}</strong></span>`)}if(scf&4){z.push(`<span>CDE SoldNumber <strong>${Number(p.sold_cde_sold_number||0)}</strong></span>`);z.push(`<span>CDE EnergyDelivered <strong>${Number(p.sold_cde_energy_delivered||0)}</strong></span>`);z.push(`<span>CDE SoldTotal <strong>${Number(p.sold_cde_sold_total||0)}</strong></span>`);z.push(`<span>CDE Sold/min <strong>${Number(p.sold_cde_sold_per_min||0)}</strong></span>`);z.push(`<span>CDE SoldOK <strong>${Number(p.sold_cde_sold_ok||0)}</strong></span>`)}if(scf&8){z.push(`<span>Teil CDE SoldNumber <strong>${Number(p.sold_cde_partial_sold_number||0)}</strong></span>`);z.push(`<span>Teil CDE EnergyDelivered <strong>${Number(p.sold_cde_partial_energy_delivered||0)}</strong></span>`);z.push(`<span>Teil CDE SoldTotal <strong>${Number(p.sold_cde_partial_sold_total||0)}</strong></span>`);z.push(`<span>Teil CDE Sold/min <strong>${Number(p.sold_cde_partial_sold_per_min||0)}</strong></span>`);z.push(`<span>Teil CDE SoldOK <strong>${Number(p.sold_cde_partial_sold_ok||0)}</strong></span>`)}special=`<div class="jbu-counter-grid">${z.join('')}</div>`}
  if(!parts.length&&!levels&&!counters&&!partial&&!special)return '';let op=jbcUsbDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details" data-port="${port}"${op} ontoggle="jbcUsbRememberDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'Werkzeugdetails / Betriebszähler':'Tool details / counters'}</summary>${parts.length?`<div class="jbu-port-detail-grid">${parts.join('')}</div>`:''}${levels}${counters}${partial}${special}</details>`
}
function jbcUsbSoldDiagDetailsHtml(m,p,addr,port){
  let f=Number((p&&p.sold_diag_flags)||0)&255,xf=Number((p&&p.sold_extra_flags)||0)&65535,rf=Number((p&&p.sold_readonly_port_flags)||0)&65535,ff=Number((p&&p.sold_feeder_flags)||0)&65535;if(!f&&!(xf&28)&&!(rf&14)&&!ff)return '';
  let parts=[],row=(label,value)=>`<div class="jbu-port-detail-item"><span class="k">${label}</span><strong>${value}</strong></div>`,direct=uiLang=='de'?' (Direkt-Read)':' (direct read)',on=v=>v?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off');
  if(f&1)parts.push(row('MOS '+(uiLang=='de'?'Temperatur':'temperature'),jbcUsbTemp(p.sold_mos_temp)));
  if(f&2)parts.push(row(uiLang=='de'?'JBC Werkzeugtyp':'JBC ToolType',jbcUsbToolName(p.sold_tool_type)));
  if(f&4){let lp={error:Number(p.sold_tool_last_error||0)};parts.push(row(uiLang=='de'?'Letzter Werkzeugfehler':'Tool last error',jbcUsbToolErrorName(m,lp)))}
  if(f&8)parts.push(row(uiLang=='de'?'Alarm max. / Verzögerung':'Alarm max / delay',`${jbcUsbTemp(p.sold_alarm_max_temp)} / ${(Number(p.sold_alarm_max_delay_tenth_sec||0)/10).toFixed(1)} s`));
  if(f&16)parts.push(row(uiLang=='de'?'Alarm min. / Verzögerung':'Alarm min / delay',`${jbcUsbTemp(p.sold_alarm_min_temp)} / ${(Number(p.sold_alarm_min_delay_tenth_sec||0)/10).toFixed(1)} s`));
  if(xf&4)parts.push(row(uiLang=='de'?'Ausgewähltes Profil':'Selected profile',escHtml(String(p.sold_selected_profile||'').trim()||'-')));
  if(xf&8)parts.push(row('Profile Mode',Number(p.sold_profile_mode||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')));
  if(xf&16){parts.push(row('Assistant',p.sold_assistant_on?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')));parts.push(row(uiLang=='de'?'Assistant Warnung / Fehler':'Assistant warning / error',`${Number(p.sold_assistant_warning||0)} / ${Number(p.sold_assistant_error||0)}`))}
  if(rf&2)parts.push(row(uiLang=='de'?'Assistant Warncode':'Assistant warning code',String(Number(p.sold_assistant_warning_code||0))));
  if(rf&4){parts.push(row(uiLang=='de'?'Lötergebnis Ähnlichkeit':'Soldering result similarity',String(Number(p.sold_result_similarity||0))));parts.push(row(uiLang=='de'?'Lötergebnis Zeit':'Soldering result time',(Number(p.sold_result_tenths||0)/10).toFixed(1)+' s'));parts.push(row(uiLang=='de'?'Lötergebnis Energie':'Soldering result energy',String(Number(p.sold_result_energy||0))))}
  if(rf&8)parts.push(row((uiLang=='de'?'JBC Leistung':'JBC power')+direct,jbcUsbPercent(p.sold_direct_power_permille)));
  if(ff&1){let mv=Number(p.sold_feeder_working_mode||0)&255,mc=String.fromCharCode(mv),mn={C:'CONTINUOUS',D:'DISCONTINUOUS',P:'PROGRAM',R:'TIN RELOAD',S:'DISABLED'}[mc]||('0x'+mv.toString(16).toUpperCase().padStart(2,'0'));parts.push(row('Tin Feeder Mode',mn));parts.push(row(uiLang=='de'?'Tin Feeder Programm':'Tin Feeder program',String(Number(p.sold_feeder_selected_program||0))));parts.push(row(uiLang=='de'?'Tin Feeder Länge / Geschwindigkeit':'Tin Feeder length / speed',`${Number(p.sold_feeder_delivery_length||0)} / ${Number(p.sold_feeder_delivery_speed||0)}`));parts.push(row(uiLang=='de'?'Lötdrahtdurchmesser / Rückzug':'Tin diameter / remove length',`${Number(p.sold_feeder_tin_diameter||0)} / ${Number(p.sold_feeder_remove_length||0)}`));parts.push(row(uiLang=='de'?'Länge/Geschwindigkeit schreibgeschützt':'Length/speed read-only',on(!!p.sold_feeder_speed_length_readonly)));parts.push(row(uiLang=='de'?'Wählbare Programme':'Selectable programs','0x'+(Number(p.sold_feeder_selectable_programs||0)&65535).toString(16).toUpperCase().padStart(4,'0')));parts.push(row(uiLang=='de'?'Drahtstau-Erkennung':'Tin clogging detection',on(!!p.sold_feeder_clogging_detection)))}
  if(ff&64)parts.push(row(uiLang=='de'?'Tin Feeder Motor':'Tin Feeder motor',`${on(!!p.sold_feeder_motor_on)} · ${Number(p.sold_feeder_motor_direction||0)?'REMOVE_TIN':'ADD_TIN'}`));
  let lens=Array.isArray(p.sold_feeder_program_length)?p.sold_feeder_program_length:[],speeds=Array.isArray(p.sold_feeder_program_speed)?p.sold_feeder_program_speed:[],sel=Number(p.sold_feeder_selectable_programs||0)&65535;for(let pg=0;pg<5;pg++){if(!(ff&(1<<(pg+1))))continue;let l=Array.isArray(lens[pg])?lens[pg]:[],sp=Array.isArray(speeds[pg])?speeds[pg]:[],steps=[];for(let st=0;st<3;st++)steps.push(`${Number(l[st]||0)} @ ${Number(sp[st]||0)}`);parts.push(row(`Tin Feeder P${pg}${sel&(1<<pg)?' ✓':''}`,steps.join(' · ')))}
  if(!parts.length)return '';let op=jbcUsbSoldDiagDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details jbu-sold-diag" data-port="${port}"${op} ontoggle="jbcUsbRememberSoldDiagDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'SOLD JBC Diagnose':'SOLD JBC diagnostics'}</summary><div class="jbu-port-detail-grid">${parts.join('')}</div></details>`
}
function jbcUsbSoldStationDiagKey(addr){return 'ofe_jbu_sold_station_diag_'+Number(addr)}
function jbcUsbSoldStationDiagIsOpen(addr){let k=jbcUsbSoldStationDiagKey(addr);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberSoldStationDiag(addr,open){let k=jbcUsbSoldStationDiagKey(addr);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbRobotConfigText(r){r=Array.isArray(r)?r:[];if(!r.length)return '-';let digit=v=>{v=Number(v||0)&255;return v>=48&&v<=57?v-48:(v<=9?v:NaN)},speeds=[1200,2400,4800,9600,19200,38400,57600,115200,230400,250000,460800,500000],sv=Number(r[0]||0)&255,db=digit(r[1]),pc=String.fromCharCode(Number(r[2]||0)&255).toUpperCase(),par=pc==='E'?(uiLang=='de'?'Gerade':'Even'):(pc==='O'?(uiLang=='de'?'Ungerade':'Odd'):(uiLang=='de'?'Keine':'None')),stop=((Number(r[3]||0)&255)===2||String.fromCharCode(Number(r[3]||0)&255)==='2')?2:1,rv=Number(r[4]||0)&255,proto=(rv===1||String.fromCharCode(rv)==='1')?'RS485':'RS232',a1=digit(r[5]),a2=digit(r[6]),adr=(Number.isFinite(a1)&&Number.isFinite(a2))?String(a1*10+a2):'-',speed=speeds[sv]||0;return `${speed?speed+' bps':'Speed #'+sv} · Data ${Number.isFinite(db)?db:'-'} · ${par} · Stop ${stop} · ${proto}${proto==='RS485'?' · Addr '+adr:''}`}
function jbcUsbSoldPeripheralText(sp){sp=sp||{};let ty={1:'PD',2:'MS',3:'MN',4:'FS',5:'MV',6:'FAE'}[Number(sp.type||0)]||'-',po=Number(sp.port),fn={1:'Sleep',2:'Extractor',3:'Modul'}[Number(sp.function||0)]||'-',ac={1:(uiLang=='de'?'Gedrückt':'Pressed'),2:(uiLang=='de'?'Gezogen':'Pulled')}[Number(sp.activation||0)]||'-',fl=Number(sp.flags||0),active=(fl&2)?((fl&4)?(uiLang=='de'?'aktiv':'active'):(uiLang=='de'?'inaktiv':'inactive')):'-',pd={1:'CC',2:'OC',3:'OK'}[Number(sp.pd_status||0)]||'',meta='';if(devMode&&(fl&1)&&Number(sp.type||0)!==6){let h=String(sp.hash_mcu_uid||'').trim(),dt=String(sp.datetime||'').trim(),v=Number(sp.version||0);meta=` · v${v}${h?' · UID '+h:''}${dt?' · '+dt:''}`}return `${ty} · ${Number.isFinite(po)&&po>=0&&po<4?'Port '+(po+1):'-'} · ${fn} · ${ac} · ${Number(sp.delay||0)} s · ${active}${pd?' · '+pd:''}${meta}`}
function jbcUsbSoldStationDiagHtml(m){
  if(jbcUsbStationKind(m)!='SOLD')return '';let sf=Number((m&&m.jbc_usb_sold_station_diag_flags)||0)&255,f=Number((m&&m.jbc_usb_sold_extra_station_flags)||0)&65535,rf=Number((m&&m.jbc_usb_sold_readonly_flags)||0)>>>0;if(!sf&&!f&&!rf)return '';
  let row=(l,v,cls='')=>`<div class="jbu-port-detail-item ${cls}"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[],on=v=>v?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off'),tu=v=>{v=Number(v||0)&255;return (v===67||v===0)?'°C':((v===70||v===1)?'°F':('0x'+v.toString(16).toUpperCase().padStart(2,'0')))},rawHex=v=>'0x'+(Number(v||0)&255).toString(16).toUpperCase().padStart(2,'0');
  if(sf&1)a.push(row(uiLang=='de'?'Trafo-Temperatur':'Transformer temperature',jbcUsbTemp(m.jbc_usb_sold_trafo_temp)));
  if(sf&4)a.push(row(uiLang=='de'?'Trafo Übertemperaturgrenze':'Transformer overtemp trigger',jbcUsbTemp(m.jbc_usb_sold_trafo_error_temp)));
  if(sf&8)a.push(row(uiLang=='de'?'MOS Übertemperaturgrenze':'MOS overtemp trigger',jbcUsbTemp(m.jbc_usb_sold_mos_error_temp)));
  if((f&16)&&(f&32))a.push(row(uiLang=='de'?'Temperaturbereich':'Temperature range',`${jbcUsbTemp(m.jbc_usb_sold_min_temp)} – ${jbcUsbTemp(m.jbc_usb_sold_max_temp)}`));else{if(f&16)a.push(row(uiLang=='de'?'Min. Temperatur':'Min temperature',jbcUsbTemp(m.jbc_usb_sold_min_temp)));if(f&32)a.push(row(uiLang=='de'?'Max. Temperatur':'Max temperature',jbcUsbTemp(m.jbc_usb_sold_max_temp)))}
  if(f&1)a.push(row(uiLang=='de'?'PIN aktiviert':'PIN enabled',on(!!(f&2))));
  if(f&4)a.push(row(uiLang=='de'?'PIN gesetzt':'PIN configured',(f&8)?(uiLang=='de'?'ja':'yes'):(uiLang=='de'?'nein':'no')));
  if(devMode&&(f&4)&&Object.prototype.hasOwnProperty.call(m,'jbc_usb_sold_pin'))a.push(row('PIN',escHtml(String(m.jbc_usb_sold_pin||'-')),'dev-only'));
  if(rf&1)a.push(row('Remote Mode',on(!!(rf&2))));
  if(rf&4)a.push(row(uiLang=='de'?'Temperatureinheit':'Temperature unit',tu(m.jbc_usb_sold_temp_unit)));
  if(rf&8)a.push(row('N2 Mode',on(!!(rf&16))));
  if(rf&32)a.push(row('Help Text',on(!!(rf&64))));
  if(rf&128){let pl=Number(m.jbc_usb_sold_power_limit||0),cap26=String(m.jbc_usb_model_type||'').toUpperCase()==='CAP26',pv=cap26?`${pl} (${uiLang=='de'?'JBC Rohwert':'JBC raw'})`:`${pl} W`;a.push(row(uiLang=='de'?'Leistungsgrenze':'Power limit',pv));}
  if(rf&256)a.push(row(uiLang=='de'?'Tastenton':'Key beep',on(!!(rf&512))));
  if(rf&1024){let i=m.jbc_usb_sold_interface||[];a.push(row(uiLang=='de'?'Interface Sprache':'Interface language',rawHex(i[0])));a.push(row(uiLang=='de'?'Interface Temperatureinheit':'Interface temperature unit',tu(i[1])));a.push(row(uiLang=='de'?'Interface Tastenton':'Interface key beep',on(Number(i[2]||0)!=0)));a.push(row(uiLang=='de'?'Interface Arbeitsmodus':'Interface working mode',rawHex(i[3])));a.push(row(uiLang=='de'?'Graph Anzeige / Skalierung / Raster':'Graph screen / scale / grid',`${rawHex(i[4])} · ${rawHex(i[5])} · ${rawHex(i[6])}`));a.push(row(uiLang=='de'?'Graph Temperatur max./min./Bereich':'Graph temperature max/min/range',`${jbcUsbTemp(m.jbc_usb_sold_graph_temp_max)} / ${jbcUsbTemp(m.jbc_usb_sold_graph_temp_min)} / ${jbcUsbTemp(m.jbc_usb_sold_graph_temp_range)}`));a.push(row(uiLang=='de'?'Graph Leistung max./min.':'Graph power max/min',`${jbcUsbPercent(m.jbc_usb_sold_graph_power_max)} / ${jbcUsbPercent(m.jbc_usb_sold_graph_power_min)}`))}
  if(rf&2048)a.push(row('AutoClean',`${on(!!m.jbc_usb_sold_autoclean)} · ${jbcUsbTemp(m.jbc_usb_sold_autoclean_temp)} · ${Number(m.jbc_usb_sold_autoclean_seconds||0)} s`));
  if(rf&4096)a.push(row(uiLang=='de'?'Erdungstyp':'Ground type',String(Number(m.jbc_usb_sold_ground_type||0))));
  if(rf&8192){let d=m.jbc_usb_sold_datetime||[],yr=(Number(d[0]||0)&255)|((Number(d[1]||0)&255)<<8),pad=v=>String(Number(v||0)).padStart(2,'0'),valid=yr>0&&d.length>=7;a.push(row(uiLang=='de'?'Stationsdatum/-zeit':'Station date/time',valid?`${yr}-${pad(d[2])}-${pad(d[3])} ${pad(d[4])}:${pad(d[5])}:${pad(d[6])}`:'-'))}
  if(rf&16384)a.push(row(uiLang=='de'?'Frontanschluss':'Frontal connection',escHtml(String(m.jbc_usb_sold_frontal||'').trim()||'-')));
  if(rf&32768){let e=m.jbc_usb_sold_ethernet||[],ip=o=>`${Number(e[o+3]||0)}.${Number(e[o+2]||0)}.${Number(e[o+1]||0)}.${Number(e[o]||0)}`,port=(Number(e[21]||0)&255)|((Number(e[22]||0)&255)<<8);a.push(row('Ethernet DHCP',on(Number(e[0]||0)!=0)));a.push(row('Ethernet IP',ip(1)));a.push(row('Ethernet Mask',ip(5)));a.push(row('Ethernet Gateway',ip(9)));a.push(row('Ethernet DNS1',ip(13)));a.push(row('Ethernet DNS2',ip(17)));a.push(row('Ethernet Port',String(port)))}
  if(rf&65536){let i=m.jbc_usb_sold_station_interface||[];a.push(row(uiLang=='de'?'Stations-Interface':'Station interface',`${uiLang=='de'?'Sprache':'Language'} ${rawHex(i[0])} · ${tu(i[1])} · ${uiLang=='de'?'Tastenton':'Key beep'} ${on(Number(i[2]||0)!=0)} · ${uiLang=='de'?'Längeneinheit':'Length unit'} ${rawHex(i[3])}`))}
  if(f&128)a.push(row('Robot Status',on(!!(f&256))));
  if(f&64)a.push(row('Robot Config',jbcUsbRobotConfigText(m.jbc_usb_sold_robot_config||[])));
  if(f&512){let pc=Number(m.jbc_usb_sold_peripheral_count||0);a.push(row(uiLang=='de'?'Peripheriegeräte':'Peripherals',String(pc)));let pa=m.jbc_usb_sold_peripherals||[];for(let i=0;i<pa.length;i++){let sp=pa[i]||{},fl=Number(sp.flags||0);if(fl&3)a.push(row(`${uiLang=='de'?'Peripherie':'Peripheral'} ${i+1}`,jbcUsbSoldPeripheralText(sp)))}}
  if(!a.length)return '';let op=jbcUsbSoldStationDiagIsOpen(m.addr)?' open':'';return `<details class="jbu-port-details jbu-sold-station-diag"${op} ontoggle="jbcUsbRememberSoldStationDiag(${Number(m.addr)},this.open)"><summary>${uiLang=='de'?'SOLD JBC Stationsdaten':'SOLD JBC station data'}</summary><div class="jbu-port-detail-grid">${a.join('')}</div></details>`
}
function jbcUsbHaDetailsHtml(p,addr,port){let f=Number((p&&p.ha_value_flags)||0)&65535,df=Number((p&&p.ha_diag_flags)||0)&65535;if(!f&&!df)return '';let parts=[];if(f&16)parts.push(`<div class="jbu-port-detail-item"><span class="k">External TC</span><strong>${jbcUsbTemp(p.actual_ext_temp)}</strong></div>`);if(f&8)parts.push(`<div class="jbu-port-detail-item"><span class="k">External TC Soll</span><strong>${jbcUsbTemp(p.selected_ext_temp)}</strong></div>`);if(f&32)parts.push(`<div class="jbu-port-detail-item"><span class="k">Adjust</span><strong>${jbcUsbAdjustTemp(p.ha_adjust_temp)}</strong></div>`);if(f&64)parts.push(`<div class="jbu-port-detail-item"><span class="k">Time to stop ${uiLang=='de'?'Einstellung':'setting'}</span><strong>${jbcUsbTimeToStop(p.configured_time_to_stop)}</strong></div>`);if(f&128)parts.push(`<div class="jbu-port-detail-item"><span class="k">External TC Mode</span><strong>${jbcUsbHaExternalMode(p.external_tc_mode)}</strong></div>`);if(f&256)parts.push(`<div class="jbu-port-detail-item"><span class="k">Start</span><strong>${jbcUsbHaStartMode(p.start_mode)}</strong></div>`);if(f&512)parts.push(`<div class="jbu-port-detail-item"><span class="k">Profile Mode</span><strong>${Number(p.profile_mode||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}</strong></div>`);let levels='';if(f&1024){let lt=p.level_temp||[],lf=p.level_flow_permille||[],le=p.level_ext_temp||[],lo=p.level_on||[];let rows='';for(let i=0;i<3;i++){let selected=Number(p.selected_level)===i;rows+=`<span class="${selected?'is-selected':''}">Level ${i+1} <strong>${Number(lo[i]||0)?jbcUsbTemp(lt[i]):(uiLang=='de'?'aus':'off')} · ${jbcUsbPercent(lf[i])}${Number(le[i]||0)?' · TC '+jbcUsbTemp(le[i]):''}</strong></span>`}levels=`<div class="jbu-counter-grid jbu-ha-levels"><span>${uiLang=='de'?'Temperatur-/Flow-Level':'Temperature/flow levels'} <strong>${Number(p.levels_on||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}</strong></span>${rows}</div>`}let counters='';if(f&2048){let plug=Number(p.ha_counter_plug_min||0),work=Number(p.ha_counter_work_min||0),idle=Math.max(0,plug-work);counters=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Eingesteckt':'Plugged'} <strong>${jbcUsbMinutes(plug)}</strong></span><span>Work <strong>${jbcUsbMinutes(work)}</strong></span><span>Idle <strong>${jbcUsbMinutes(idle)}</strong></span><span>Work-Zyklen <strong>${Number(p.ha_counter_work_cycles||0)}</strong></span><span>${uiLang=='de'?'Vakuumpumpen-Zyklen':'Suction pump cycles'} <strong>${Number(p.ha_counter_suction_cycles||0)}</strong></span></div>`}let partial='';if(df&64){let pp=Number(p.ha_partial_plug_min||0),pw=Number(p.ha_partial_work_min||0),pi=Math.max(0,pp-pw);partial=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Teil Eingesteckt':'Partial plugged'} <strong>${jbcUsbMinutes(pp)}</strong></span><span>${uiLang=='de'?'Teil Work':'Partial work'} <strong>${jbcUsbMinutes(pw)}</strong></span><span>${uiLang=='de'?'Teil Idle':'Partial idle'} <strong>${jbcUsbMinutes(pi)}</strong></span><span>${uiLang=='de'?'Teil Work-Zyklen':'Partial work cycles'} <strong>${Number(p.ha_partial_work_cycles||0)}</strong></span><span>${uiLang=='de'?'Teil Vakuumpumpen-Zyklen':'Partial suction pump cycles'} <strong>${Number(p.ha_partial_suction_cycles||0)}</strong></span></div>`}if(!parts.length&&!levels&&!counters&&!partial)return '';let op=jbcUsbDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details" data-port="${port}"${op} ontoggle="jbcUsbRememberDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'Heißluftdetails / Betriebszähler':'Hot-air details / counters'}</summary>${parts.length?`<div class="jbu-port-detail-grid">${parts.join('')}</div>`:''}${levels}${counters}${partial}</details>`}
function jbcUsbHaDiagDetailsHtml(m,p,addr,port){let f=Number((p&&p.ha_diag_flags)||0)&65535;if(!(f&447))return '';let row=(l,v)=>`<div class="jbu-port-detail-item"><span class="k">${l}</span><strong>${v}</strong></div>`,direct=uiLang=='de'?' (Direkt-Read)':' (direct read)',a=[];if(f&1)a.push(row((uiLang=='de'?'JBC Lufttemperatur':'JBC air temperature')+direct,jbcUsbTemp(p.ha_diag_air_temp)));if(f&2)a.push(row('JBC '+(uiLang=='de'?'Leistung':'power')+direct,jbcUsbPercent(p.ha_diag_power_permille)));if(f&4)a.push(row('JBC '+(uiLang=='de'?'Luftstrom':'flow')+direct,jbcUsbPercent(p.ha_diag_flow_permille)));if(f&8){let id=Number(p.ha_diag_tool||0);a.push(row((uiLang=='de'?'JBC Werkzeugtyp':'JBC ToolType')+direct,id?jbcUsbToolName(id+30):'-'))}if(f&16){let ep={error:Number(p.ha_diag_error||0)};a.push(row((uiLang=='de'?'Letzter Werkzeugfehler':'Tool last error')+direct,jbcUsbToolErrorName(m,ep)))}if(f&32)a.push(row('JBC ToolStatus'+direct,jbcUsbHaStatusText(p.ha_diag_status)));if(f&128){let hs=Number(p.ha_diag_heater_state||0);a.push(row((uiLang=='de'?'JBC Heizstatus':'JBC heater status')+direct,hs===1?'HEATER':(hs===2?'COOLING':'OFF')))}if(f&256)a.push(row((uiLang=='de'?'JBC Vakuumpumpe':'JBC suction pump')+direct,Number(p.ha_diag_suction_state||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')));let op=jbcUsbHaDiagDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details jbu-ha-diag" data-port="${port}"${op} ontoggle="jbcUsbRememberHaDiagDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'HOT AIR JBC Diagnose':'HOT AIR JBC diagnostics'}</summary><div class="jbu-port-detail-grid">${a.join('')}</div></details>`}
function jbcUsbHaStationDiagHtml(m){let f=Number((m&&m.jbc_usb_ha_station_diag_flags)||0)&65535,sf=Number((m&&m.jbc_usb_ha_security_flags)||0)&255;if(jbcUsbStationKind(m)!='HA'||(!f&&!sf))return '';let row=(l,v,cls='')=>`<div class="jbu-port-detail-item ${cls}"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[],digit=v=>{v=Number(v||0)&255;return v>=48&&v<=57?v-48:(v<=9?v:NaN)},on=v=>v?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off'),tu=Number(m.jbc_usb_ha_temp_unit||0)&255;if(f&1)a.push(row('Remote Mode',on(!!m.jbc_usb_ha_remote_mode)));if(f&2)a.push(row(uiLang=='de'?'Temperatureinheit':'Temperature unit',(tu===67||tu===0)?'°C':((tu===70||tu===1)?'°F':('0x'+tu.toString(16).toUpperCase().padStart(2,'0')))));if(f&4)a.push(row(uiLang=='de'?'Temperaturbereich':'Temperature range',`${jbcUsbTemp(m.jbc_usb_ha_min_temp)} – ${jbcUsbTemp(m.jbc_usb_ha_max_temp)}`));if(f&8)a.push(row(uiLang=='de'?'Luftstrombereich':'Flow range',`${jbcUsbPercent(m.jbc_usb_ha_min_flow)} – ${jbcUsbPercent(m.jbc_usb_ha_max_flow)}`));if(f&16)a.push(row('External-TC '+(uiLang=='de'?'Bereich':'range'),`${jbcUsbTemp(m.jbc_usb_ha_min_ext_temp)} – ${jbcUsbTemp(m.jbc_usb_ha_max_ext_temp)}`));if(f&32)a.push(row(uiLang=='de'?'Ausgewähltes Profil':'Selected profile',String(m.jbc_usb_ha_selected_profile||'').trim()||'-'));if(sf&1)a.push(row(uiLang=='de'?'PIN aktiviert':'PIN enabled',on(!!(sf&2))));if(sf&4)a.push(row(uiLang=='de'?'PIN gesetzt':'PIN configured',(sf&8)?(uiLang=='de'?'ja':'yes'):(uiLang=='de'?'nein':'no')));if(devMode&&(sf&4)&&Object.prototype.hasOwnProperty.call(m,'jbc_usb_ha_pin'))a.push(row('PIN',escHtml(String(m.jbc_usb_ha_pin||'-')),'dev-only'));if(sf&16)a.push(row(uiLang=='de'?'Tastenton':'Beep',on(!!(sf&32))));if(f&128)a.push(row('Robot Status',on(!!m.jbc_usb_ha_robot_status)));if(f&64){let r=m.jbc_usb_ha_robot_config||[],speeds=[1200,2400,4800,9600,19200,38400,57600,115200,230400,250000,460800,500000],sv=Number(r[0]||0)&255,db=digit(r[1]),pc=String.fromCharCode(Number(r[2]||0)&255).toUpperCase(),par=pc==='E'?(uiLang=='de'?'Gerade':'Even'):(pc==='O'?(uiLang=='de'?'Ungerade':'Odd'):(uiLang=='de'?'Keine':'None')),stop=((Number(r[3]||0)&255)===2||String.fromCharCode(Number(r[3]||0)&255)==='2')?2:1,rv=Number(r[4]||0)&255,raw485=(rv===1||String.fromCharCode(rv)==='1'),jtseCap1=String(m.jbc_usb_model||'').toUpperCase()==='JTSE'&&String(m.jbc_usb_model_type||'').toUpperCase()==='CAP'&&Number(m.jbc_usb_model_version||0)===1,proto=jtseCap1?'RS232':(raw485?'RS485':'RS232'),a1=digit(r[5]),a2=digit(r[6]),addr=(Number.isFinite(a1)&&Number.isFinite(a2))?String(a1*10+a2):'-',speed=speeds[sv]||0,fmt=r.length?`${speed?speed+' bps':'Speed #'+sv} · Data ${Number.isFinite(db)?db:'-'} · ${par} · Stop ${stop} · ${proto}${proto==='RS485'?' · Addr '+addr:''}`:'-';a.push(row('Robot Config',fmt))}let op=jbcUsbHaStationDiagIsOpen(m.addr)?' open':'';return `<details class="jbu-port-details jbu-ha-station-diag"${op} ontoggle="jbcUsbRememberHaStationDiag(${Number(m.addr)},this.open)"><summary>${uiLang=='de'?'HOT AIR JBC Stationsdaten':'HOT AIR JBC station data'}</summary><div class="jbu-port-detail-grid">${a.join('')}</div></details>`}
function jbcUsbSfStationDiagKey(addr){return 'ofe_jbu_sf_station_diag_'+Number(addr)}
function jbcUsbSfStationDiagIsOpen(addr){let k=jbcUsbSfStationDiagKey(addr);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberSfStationDiag(addr,open){let k=jbcUsbSfStationDiagKey(addr);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbSfOnOff(v){return v?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}
function jbcUsbSfStationDiagHtml(m){let f=Number((m&&m.jbc_usb_sf_station_flags)||0)&65535;if(jbcUsbStationKind(m)!='SF'||!f)return '';let row=(l,v)=>`<div class="jbu-port-detail-item"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[];if(f&1)a.push(row('PIN',`${(f&2)?(uiLang=='de'?'konfiguriert':'configured'):(uiLang=='de'?'nicht gesetzt':'not set')}${devMode&&m.jbc_usb_sf_pin?' · '+escHtml(String(m.jbc_usb_sf_pin)):''}`));if(f&4)a.push(row('PIN '+(uiLang=='de'?'aktiviert':'enabled'),jbcUsbSfOnOff(f&8)));if(f&16)a.push(row('Beep',jbcUsbSfOnOff(f&32)));if(f&64)a.push(row(uiLang=='de'?'Längeneinheit':'Length unit','Raw '+Number(m.jbc_usb_sf_length_unit||0)));if(f&128)a.push(row(uiLang=='de'?'JBC Verbindungsmodus':'JBC connection mode',(f&256)?'CONTROL':'MONITOR'));if(f&1024)a.push(row('Robot Status',jbcUsbSfOnOff(f&2048)));if(f&512)a.push(row('Robot Config',jbcUsbRobotConfigText(m.jbc_usb_sf_robot_config||[])));if(m.jbc_usb_sf_conti_valid)a.push(row('Conti Mode',`speed ${Number(m.jbc_usb_sf_conti_speed||0)} · ports 0x${(Number(m.jbc_usb_sf_conti_ports||0)&255).toString(16).toUpperCase().padStart(2,'0')}`));let programs=Array.isArray(m.jbc_usb_sf_programs)?m.jbc_usb_sf_programs:[],phtml='';if(programs.length){let rows='';for(let pg of programs){let n=Number(pg.n||0),name=escHtml(String(pg.name||'').trim()||'-'),en=pg.enabled?' ✓':'',l=Array.isArray(pg.length)?pg.length:[],sp=Array.isArray(pg.speed)?pg.speed:[],steps=[];for(let i=0;i<3;i++)steps.push(`${Number(l[i]||0)} @ ${Number(sp[i]||0)}`);rows+=`<tr><td>${n}</td><td>${name}${en}</td><td class="mono">${steps.join(' · ')}</td></tr>`}phtml=`<div class="jbu-table-wrap"><table class="jbu-table"><thead><tr><th>#</th><th>${uiLang=='de'?'Programm':'Program'}</th><th>${uiLang=='de'?'Länge @ Geschwindigkeit (raw)':'Length @ speed (raw)'}</th></tr></thead><tbody>${rows}</tbody></table></div>`}let list='';if((f&4096)&&Array.isArray(m.jbc_usb_sf_program_list)){let vals=m.jbc_usb_sf_program_list.map(v=>Number(v||0)&255);list=`<div class="jbu-port-detail-grid">${row(uiLang=='de'?'Verkettungs-Programmliste (raw)':'Concatenation program list (raw)',vals.length?vals.join(', '):'-')}</div>`}let op=jbcUsbSfStationDiagIsOpen(m.addr)?' open':'';return `<details class="jbu-port-details"${op} ontoggle="jbcUsbRememberSfStationDiag(${Number(m.addr)},this.open)"><summary>${uiLang=='de'?'SOLDER FEEDER JBC Stationsdaten':'SOLDER FEEDER JBC station data'}</summary>${a.length?`<div class="jbu-port-detail-grid">${a.join('')}</div>`:''}${list}${phtml}</details>`}
function jbcUsbSfDetailsHtml(p,addr,port){let f=Number((p&&p.sf_flags)||0)&65535;if(!f)return '';let row=(l,v)=>`<div class="jbu-port-detail-item"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[];if(f&1)a.push(row(uiLang=='de'?'Geschwindigkeit':'Speed',(Number(p.sf_speed_tenth_mm_s||0)/10).toFixed(1)+' mm/s'));if(f&2)a.push(row(uiLang=='de'?'Länge':'Length',(Number(p.sf_length_tenth_mm||0)/10).toFixed(1)+' mm'));if(f&4){a.push(row(uiLang=='de'?'Zuführung':'Feeding',jbcUsbSfOnOff(Number(p.sf_feeding_state||0)!=0)));a.push(row(uiLang=='de'?'Zuführwert (DLL raw)':'Feeding value (DLL raw)',Number(p.sf_feeding_value_raw||0)));a.push(row(uiLang=='de'?'Zuführ-Programm':'Feeding program',Number(p.sf_feeding_selected_program||0)));a.push(row(uiLang=='de'?'Aktueller Programmschritt (DLL)':'Current program step (DLL)',Number(p.sf_current_program_step||0)))}if(f&8)a.push(row(uiLang=='de'?'Werkzeug aktiviert':'Tool enabled',jbcUsbSfOnOff(!!(f&16))));let global='';if(f&32)global=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Zinnlänge':'Tin length'} <strong>${escHtml(String(p.sf_counter_tin_length||'0'))}</strong></span><span>${uiLang=='de'?'Eingesteckt':'Plugged'} <strong>${jbcUsbMinutes(p.sf_counter_plug_min)}</strong></span><span>Work <strong>${jbcUsbMinutes(p.sf_counter_work_min)}</strong></span><span>Idle <strong>${jbcUsbMinutes(p.sf_counter_idle_min)}</strong></span><span>${uiLang=='de'?'Work-Zyklen':'Work cycles'} <strong>${Number(p.sf_counter_work_cycles||0)}</strong></span></div>`;let partial='';if(f&64)partial=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Teil Zinnlänge':'Partial tin length'} <strong>${escHtml(String(p.sf_partial_tin_length||'0'))}</strong></span><span>${uiLang=='de'?'Teil Eingesteckt':'Partial plugged'} <strong>${jbcUsbMinutes(p.sf_partial_plug_min)}</strong></span><span>Partial Work <strong>${jbcUsbMinutes(p.sf_partial_work_min)}</strong></span><span>Partial Idle <strong>${jbcUsbMinutes(p.sf_partial_idle_min)}</strong></span><span>${uiLang=='de'?'Teil Work-Zyklen':'Partial work cycles'} <strong>${Number(p.sf_partial_work_cycles||0)}</strong></span></div>`;let op=jbcUsbDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details" data-port="${port}"${op} ontoggle="jbcUsbRememberDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'Solder-Feeder Details / Zähler':'Solder feeder details / counters'}</summary>${a.length?`<div class="jbu-port-detail-grid">${a.join('')}</div>`:''}${global}${partial}</details>`}
function jbcUsbFeStationDiagKey(addr){return 'ofe_jbu_fe_station_diag_'+Number(addr)}
function jbcUsbFeStationDiagIsOpen(addr){let k=jbcUsbFeStationDiagKey(addr);if(Object.prototype.hasOwnProperty.call(jbcUsbDetailsOpen,k))return !!jbcUsbDetailsOpen[k];try{return localStorage.getItem(k)=='1'}catch(e){return false}}
function jbcUsbRememberFeStationDiag(addr,open){let k=jbcUsbFeStationDiagKey(addr);jbcUsbDetailsOpen[k]=!!open;try{localStorage.setItem(k,open?'1':'0')}catch(e){}}
function jbcUsbFeOnOff(v){return Number(v||0)?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off')}
function jbcUsbFePedalAction(v){v=Number(v||0)&255;return v===0?'HOLD_DOWN':v===1?'PULSE':String(v)}
function jbcUsbFeRaw(v){v=Number(v||0)&255;return String(v)+' (0x'+v.toString(16).toUpperCase().padStart(2,'0')+')'}
function jbcUsbFeTimeRaw(v){v=Math.max(0,Math.floor(Number(v||0)));return Number.isFinite(v)?String(v):'-'}
function jbcUsbFeStationDiagHtml(m){let f=Number((m&&m.jbc_usb_fe_station_flags)||0)&65535,sv=Number((m&&m.jbc_usb_fe_service_flags)||0)&65535;if(jbcUsbStationKind(m)!='FE'||(!f&&!sv))return '';let row=(l,v,cls='')=>`<div class="jbu-port-detail-item ${cls}"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[];if(f&1)a.push(row(uiLang=='de'?'Dauerabsaugung':'Continuous suction',jbcUsbFeOnOff(f&2)));if(f&4)a.push(row(uiLang=='de'?'JBC Verbindungsmodus':'JBC connection mode',(f&8)?'CONTROL':'MONITOR'));if(f&32)a.push(row('Robot Status',jbcUsbFeOnOff(f&64)));if(f&16)a.push(row('Robot Config',jbcUsbRobotConfigText(m.jbc_usb_fe_robot_config||[])));if(sv&1)a.push(row('Flow',Number(m.jbc_usb_fe_flow_x_mil||0)));if(sv&2)a.push(row(uiLang=='de'?'Drehzahl':'Speed',Number(m.jbc_usb_fe_speed_rpm||0)+' rpm'));if(sv&4)a.push(row(uiLang=='de'?'Gewählter Flow':'Selected flow',Number(m.jbc_usb_fe_selected_flow_x_mil||0)));if(sv&8)a.push(row(uiLang=='de'?'Filterstatus':'Filter status',Number(m.jbc_usb_fe_filter_status||0)));if(sv&64)a.push(row('Beep',jbcUsbFeOnOff(m.jbc_usb_fe_beep)));if(devMode&&(sv&16)&&Object.prototype.hasOwnProperty.call(m,'jbc_usb_fe_pin'))a.push(row('PIN',escHtml(String(m.jbc_usb_fe_pin||'-')),'dev-only'));if(!a.length)return '';let op=jbcUsbFeStationDiagIsOpen(m.addr)?' open':'';return `<details class="jbu-port-details"${op} ontoggle="jbcUsbRememberFeStationDiag(${Number(m.addr)},this.open)"><summary>${uiLang=='de'?'FUME EXTRACTOR JBC Stationsdaten':'FUME EXTRACTOR JBC station data'}</summary><div class="jbu-port-detail-grid">${a.join('')}</div></details>`}
function jbcUsbFeDetailsHtml(p,addr,port){let f=Number((p&&p.fe_flags)||0)&65535,sv=Number((p&&p.fe_service_flags)||0)&65535;if(!f&&!sv)return '';let row=(l,v)=>`<div class="jbu-port-detail-item"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[];if(f&1)a.push(row('WORK '+(uiLang=='de'?'Ansaugaktivierung':'intake activation'),jbcUsbFeOnOff(!!(f&2))));if(f&4)a.push(row('STAND '+(uiLang=='de'?'Ansaugaktivierung':'intake activation'),jbcUsbFeOnOff(!!(f&8))));/* DLL exposes TimeToStopSuction as a raw int and does not document/convert a unit. */if(f&16)a.push(row('WORK TimeToStopSuction',jbcUsbFeTimeRaw(p.fe_time_to_stop_work)));if(f&32)a.push(row('STAND TimeToStopSuction',jbcUsbFeTimeRaw(p.fe_time_to_stop_stand)));if(f&64)a.push(row(uiLang=='de'?'Pedal Aktion':'Pedal action',jbcUsbFePedalAction(p.fe_pedal_action)));if(f&128)a.push(row(uiLang=='de'?'Pedal Modus':'Pedal mode',jbcUsbFeRaw(p.fe_pedal_mode)));if(sv&1)a.push(row('StandIntakes',Number(p.fe_stand_intakes||0)));if(sv&2)a.push(row('WORK SuctionDelay',jbcUsbFeTimeRaw(p.fe_suction_delay_work)));if(sv&4)a.push(row('STAND SuctionDelay',jbcUsbFeTimeRaw(p.fe_suction_delay_stand)));if(sv&8)a.push(row(uiLang=='de'?'Pedal verbunden':'Pedal connected',jbcUsbFeOnOff(p.fe_pedal_connected)));let global='';if(f&256)global=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Eingesteckt':'Plugged'} <strong>${jbcUsbMinutes(p.fe_counter_plug_min)}</strong></span><span>Idle <strong>${jbcUsbMinutes(p.fe_counter_idle_min)}</strong></span><span>WORK Intake <strong>${jbcUsbMinutes(p.fe_counter_work_intake_min)}</strong></span><span>STAND Intake <strong>${jbcUsbMinutes(p.fe_counter_stand_intake_min)}</strong></span><span>Work-Zyklen <strong>${Number(p.fe_counter_work_cycles||0)}</strong></span></div>`;let partial='';if(f&512)partial=`<div class="jbu-counter-grid"><span>${uiLang=='de'?'Teil Eingesteckt':'Partial plugged'} <strong>${jbcUsbMinutes(p.fe_partial_plug_min)}</strong></span><span>Partial Idle <strong>${jbcUsbMinutes(p.fe_partial_idle_min)}</strong></span><span>Partial WORK Intake <strong>${jbcUsbMinutes(p.fe_partial_work_intake_min)}</strong></span><span>Partial STAND Intake <strong>${jbcUsbMinutes(p.fe_partial_stand_intake_min)}</strong></span><span>${uiLang=='de'?'Teil Work-Zyklen':'Partial work cycles'} <strong>${Number(p.fe_partial_work_cycles||0)}</strong></span></div>`;let op=jbcUsbDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details" data-port="${port}"${op} ontoggle="jbcUsbRememberDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'Absaugdetails / Betriebszähler':'Extraction details / counters'}</summary>${a.length?`<div class="jbu-port-detail-grid">${a.join('')}</div>`:''}${global}${partial}</details>`}
function jbcUsbPhDetailsHtml(p,addr,port){
  let f=Number((p&&p.ph_flags)||0)&65535;if(!f)return '';
  let row=(l,v)=>`<div class="jbu-port-detail-item"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[];
  if(f&1)a.push(row('Work Mode',jbcUsbPhRawMode(p.ph_work_mode)));
  if(f&2)a.push(row(uiLang=='de'?'Heater Status gelesen':'Heater status readback',jbcUsbPhOnOff(p.ph_heater_status)));
  if(f&4)a.push(row('Time to stop '+(uiLang=='de'?'Einstellung':'setting'),jbcUsbTimeToStop(p.ph_configured_time_to_stop)));
  if(f&8)a.push(row(uiLang=='de'?'Gewählte Leistung':'Selected power',jbcUsbPercent(p.ph_selected_power)));
  if(f&16)a.push(row(uiLang=='de'?'Aktive Zonen':'Active zones',jbcUsbPhRawMode(p.ph_active_zones)));
  let global='';
  if(f&0xE0){let x=[];if(f&0x20)x.push(`<span>${uiLang=='de'?'Eingesteckt':'Plugged'} <strong>${jbcUsbMinutes(p.ph_counter_plug_min)}</strong></span>`);if(f&0x40){x.push(`<span>Work Power <strong>${jbcUsbMinutes(p.ph_counter_work_min_power)}</strong></span>`);x.push(`<span>Work Temp <strong>${jbcUsbMinutes(p.ph_counter_work_min_temp)}</strong></span>`);x.push(`<span>Work Profile <strong>${jbcUsbMinutes(p.ph_counter_work_min_profile)}</strong></span>`);if(f&0x20){let idle=Math.max(0,Number(p.ph_counter_plug_min||0)-Number(p.ph_counter_work_min_power||0)-Number(p.ph_counter_work_min_temp||0)-Number(p.ph_counter_work_min_profile||0));x.push(`<span>Idle <strong>${jbcUsbMinutes(idle)}</strong></span>`)}}if(f&0x80){x.push(`<span>Cycles Power <strong>${Number(p.ph_counter_work_cycles_power||0)}</strong></span>`);x.push(`<span>Cycles Temp <strong>${Number(p.ph_counter_work_cycles_temp||0)}</strong></span>`);x.push(`<span>Cycles Profile <strong>${Number(p.ph_counter_work_cycles_profile||0)}</strong></span>`)}global=`<div class="jbu-counter-grid">${x.join('')}</div>`}
  let partial='';
  if(f&0x700){let x=[];if(f&0x100)x.push(`<span>${uiLang=='de'?'Teil Eingesteckt':'Partial plugged'} <strong>${jbcUsbMinutes(p.ph_partial_plug_min)}</strong></span>`);if(f&0x200){x.push(`<span>Partial Work Power <strong>${jbcUsbMinutes(p.ph_partial_work_min_power)}</strong></span>`);x.push(`<span>Partial Work Temp <strong>${jbcUsbMinutes(p.ph_partial_work_min_temp)}</strong></span>`);x.push(`<span>Partial Work Profile <strong>${jbcUsbMinutes(p.ph_partial_work_min_profile)}</strong></span>`);if(f&0x100){let idle=Math.max(0,Number(p.ph_partial_plug_min||0)-Number(p.ph_partial_work_min_power||0)-Number(p.ph_partial_work_min_temp||0)-Number(p.ph_partial_work_min_profile||0));x.push(`<span>Partial Idle <strong>${jbcUsbMinutes(idle)}</strong></span>`)}}if(f&0x400){x.push(`<span>Partial Cycles Power <strong>${Number(p.ph_partial_work_cycles_power||0)}</strong></span>`);x.push(`<span>Partial Cycles Temp <strong>${Number(p.ph_partial_work_cycles_temp||0)}</strong></span>`);x.push(`<span>Partial Cycles Profile <strong>${Number(p.ph_partial_work_cycles_profile||0)}</strong></span>`)}partial=`<div class="jbu-counter-grid">${x.join('')}</div>`}
  let op=jbcUsbDetailIsOpen(addr,port)?' open':'';
  return `<details class="jbu-port-details jbu-ph-details" data-port="${port}"${op} ontoggle="jbcUsbRememberDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'Preheater Details / Betriebszähler':'Preheater details / counters'}</summary>${a.length?`<div class="jbu-port-detail-grid">${a.join('')}</div>`:''}${global}${partial}</details>`
}
function jbcUsbPhStationDiagHtml(m){
  let f=Number((m&&m.jbc_usb_ph_station_flags)||0)>>>0;if(jbcUsbStationKind(m)!='PH'||!f)return '';
  let row=(l,v,cls='')=>`<div class="jbu-port-detail-item ${cls}"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[],on=v=>v?(uiLang=='de'?'an':'on'):(uiLang=='de'?'aus':'off');
  if(f&1)a.push(row(uiLang=='de'?'Leistungsbereich':'Power range',`${jbcUsbPercent(m.jbc_usb_ph_min_power)} – ${jbcUsbPercent(m.jbc_usb_ph_max_power)}`));
  if(f&2)a.push(row(uiLang=='de'?'Temperaturbereich':'Temperature range',`${jbcUsbTemp(m.jbc_usb_ph_min_temp)} – ${jbcUsbTemp(m.jbc_usb_ph_max_temp)}`));
  if(f&4)a.push(row(uiLang=='de'?'PIN aktiviert':'PIN enabled',on(!!(f&8))));
  if(f&16)a.push(row(uiLang=='de'?'PIN gesetzt':'PIN configured',(f&32)?(uiLang=='de'?'ja':'yes'):(uiLang=='de'?'nein':'no')));
  if(devMode&&(f&16)&&Object.prototype.hasOwnProperty.call(m,'jbc_usb_ph_pin'))a.push(row('PIN',escHtml(String(m.jbc_usb_ph_pin||'-')),'dev-only'));
  if(f&64)a.push(row(uiLang=='de'?'Tastenton':'Beep',on(!!m.jbc_usb_ph_beep)));
  if(m.jbc_usb_ph_remote_valid)a.push(row('Remote Mode',on(!!m.jbc_usb_ph_remote_mode)));
  if(m.jbc_usb_ph_conti_valid)a.push(row('Conti Mode',`speed ${Number(m.jbc_usb_ph_conti_speed||0)} · ports 0x${(Number(m.jbc_usb_ph_conti_ports||0)&255).toString(16).toUpperCase().padStart(2,'0')}`));
  if(f&256)a.push(row(uiLang=='de'?'JBC Verbindungsmodus':'JBC connection mode',(f&512)?'CONTROL':'MONITOR'));
  if(f&2048)a.push(row('Robot Status',on(!!(f&4096))));
  if(f&1024){let r=m.jbc_usb_ph_robot_config||[];a.push(row('Robot Config',r.length?r.map(v=>'0x'+(Number(v||0)&255).toString(16).toUpperCase().padStart(2,'0')).join(' '):'-'))}
  if(f&16384)a.push(row('Profile Settings',`${uiLang=='de'?'Punkte':'Points'} ${Number(m.jbc_usb_ph_profile_points_setting||0)} · Consignment ${Number(m.jbc_usb_ph_profile_consignment||0)} · TC ${Number(m.jbc_usb_ph_profile_tc_regulation||0)}`));
  let tc=m.jbc_usb_ph_tc||[],tcRows='';
  for(let i=0;i<4;i++){let x=tc[i]||{},tf=Number(x.flags||0)&255;if(!tf)continue;let vals=[];if(tf&1)vals.push(`${uiLang=='de'?'Ist':'actual'} ${jbcUsbTemp(x.actual_temp)}`);if(tf&8)vals.push(`${uiLang=='de'?'Soll':'set'} ${jbcUsbTemp(x.selected_temp)}`);if(tf&4)vals.push(`Mode ${jbcUsbHaExternalMode(x.mode)}`);if(tf&2)vals.push(jbcUsbPhWarning(x.warning));tcRows+=`<span>TC${i+1} <strong>${vals.join(' · ')}</strong></span>`}
  let tcHtml=tcRows?`<div class="jbu-counter-grid">${tcRows}</div>`:'';
  let profile='';if(f&8192){let pts=m.jbc_usb_ph_profile||[],cnt=Math.min(Number(m.jbc_usb_ph_profile_count||0),pts.length),rows='';for(let i=0;i<cnt;i++){let q=pts[i]||[];rows+=`<span>#${i+1} <strong>t=${Number(q[0]||0)} · v=${Number(q[1]||0)}</strong></span>`}profile=`<details class="jbu-port-details"><summary>Profile · ${cnt} ${uiLang=='de'?'Punkte':'points'}</summary><div class="jbu-counter-grid">${rows||'<span>-</span>'}</div></details>`}
  let teach='';if(f&32768){let vals=m.jbc_usb_ph_teach||[],cnt=Math.min(Number(m.jbc_usb_ph_teach_count||0),vals.length),rows='';for(let i=0;i<cnt;i++)rows+=`<span>#${i+1} <strong>${Number(vals[i]||0)}</strong></span>`;teach=`<details class="jbu-port-details"><summary>Profile Teach · ${cnt} ${uiLang=='de'?'Werte':'values'} · Interval ${Number(m.jbc_usb_ph_profile_teach_interval||0)}</summary><div class="jbu-counter-grid">${rows||'<span>-</span>'}</div></details>`}
  let op=jbcUsbPhStationDiagIsOpen(m.addr)?' open':'';
  return `<details class="jbu-port-details jbu-ph-station-diag"${op} ontoggle="jbcUsbRememberPhStationDiag(${Number(m.addr)},this.open)"><summary>${uiLang=='de'?'PREHEATER JBC Stationsdaten':'PREHEATER JBC station data'}</summary>${a.length?`<div class="jbu-port-detail-grid">${a.join('')}</div>`:''}${tcHtml}${profile}${teach}</details>`
}
function jbcUsbClDetailsHtml(p,addr,port){let f=Number((p&&p.cl_flags)||0)&65535;if(!(f&12))return '';let row=(l,v)=>`<div class="jbu-port-detail-item"><span class="k">${l}</span><strong>${v}</strong></div>`,a=[];if(f&4){a.push(row(uiLang=='de'?'Eingesteckt':'Plugged',jbcUsbMinutes(p.cl_counter_plug_min)));a.push(row(uiLang=='de'?'Reinigung kontinuierlich':'Continuous cleaning',jbcUsbMinutes(p.cl_counter_cleaning_continuous_min)));a.push(row(uiLang=='de'?'Reinigung Erkennung':'Detection cleaning',jbcUsbMinutes(p.cl_counter_cleaning_detection_min)));a.push(row(uiLang=='de'?'Arbeitszyklen':'Work cycles',Number(p.cl_counter_work_cycles||0)));a.push(row(uiLang=='de'?'Türöffnungen':'Door-open cycles',Number(p.cl_counter_door_open_cycles||0)))}if(f&8){a.push(row(uiLang=='de'?'Teilzähler eingesteckt':'Partial plugged',jbcUsbMinutes(p.cl_partial_plug_min)));a.push(row(uiLang=='de'?'Teilzähler kontinuierlich':'Partial continuous',jbcUsbMinutes(p.cl_partial_cleaning_continuous_min)));a.push(row(uiLang=='de'?'Teilzähler Erkennung':'Partial detection',jbcUsbMinutes(p.cl_partial_cleaning_detection_min)));a.push(row(uiLang=='de'?'Teil-Arbeitszyklen':'Partial work cycles',Number(p.cl_partial_work_cycles||0)));a.push(row(uiLang=='de'?'Teil-Türöffnungen':'Partial door-open cycles',Number(p.cl_partial_door_open_cycles||0)))}let op=jbcUsbClDetailIsOpen(addr,port)?' open':'';return `<details class="jbu-port-details jbu-cl-details" data-port="${port}"${op} ontoggle="jbcUsbRememberClDetail(${Number(addr)},${Number(port)},this.open)"><summary>${uiLang=='de'?'CLM Zähler':'CLM counters'}</summary><div class="jbu-port-detail-grid">${a.join('')}</div></details>`}
function jbcUsbStationMetric(label,value){return `<div class="jbu-station-metric"><span class="k">${label}</span><strong title="${escHtml(String(value))}">${escHtml(String(value))}</strong></div>`}
function jbcUsbStationHighlights(m,kind){let a=[],add=(l,v)=>a.push(jbcUsbStationMetric(l,v)),ports=Array.isArray(m.jbc_usb_ports)?m.jbc_usb_ports:[],p=ports[0]||{};if(kind=='SOLD'){let sf=Number(m.jbc_usb_sold_station_diag_flags||0)&255,xf=Number(m.jbc_usb_sold_extra_station_flags||0)&65535,rf=Number(m.jbc_usb_sold_readonly_flags||0)>>>0;if(sf&1)add(uiLang=='de'?'Trafo-Temperatur':'Transformer temperature',jbcUsbTemp(m.jbc_usb_sold_trafo_temp));if((xf&16)&&(xf&32))add(uiLang=='de'?'Temperaturbereich':'Temperature range',jbcUsbTemp(m.jbc_usb_sold_min_temp)+' – '+jbcUsbTemp(m.jbc_usb_sold_max_temp));if(sf&2)add(uiLang=='de'?'Verbindungsmodus':'Connection mode',m.jbc_usb_sold_control_mode?'CONTROL':'MONITOR');if(rf&1)add('Remote Mode',(rf&2)?t('on'):t('off'))}else if(kind=='HA'){let f=Number(m.jbc_usb_ha_station_diag_flags||0)&65535;if(f&4)add(uiLang=='de'?'Temperaturbereich':'Temperature range',jbcUsbTemp(m.jbc_usb_ha_min_temp)+' – '+jbcUsbTemp(m.jbc_usb_ha_max_temp));if(f&8)add(uiLang=='de'?'Luftstrombereich':'Flow range',jbcUsbPercent(m.jbc_usb_ha_min_flow)+' – '+jbcUsbPercent(m.jbc_usb_ha_max_flow));if(f&32)add(uiLang=='de'?'Profil':'Profile',String(m.jbc_usb_ha_selected_profile||'').trim()||'-');if(f&1)add('Remote Mode',m.jbc_usb_ha_remote_mode?t('on'):t('off'))}else if(kind=='PH'){let f=Number(m.jbc_usb_ph_station_flags||0)>>>0;if(f&1)add(uiLang=='de'?'Leistungsbereich':'Power range',jbcUsbPercent(m.jbc_usb_ph_min_power)+' – '+jbcUsbPercent(m.jbc_usb_ph_max_power));if(f&2)add(uiLang=='de'?'Temperaturbereich':'Temperature range',jbcUsbTemp(m.jbc_usb_ph_min_temp)+' – '+jbcUsbTemp(m.jbc_usb_ph_max_temp));if(m.jbc_usb_ph_remote_valid)add('Remote Mode',m.jbc_usb_ph_remote_mode?t('on'):t('off'));if(m.jbc_usb_ph_conti_valid)add('Conti Mode','speed '+Number(m.jbc_usb_ph_conti_speed||0))}else if(kind=='FE'){let f=Number(m.jbc_usb_fe_service_flags||0)&65535;if(f&1)add('Flow',Number(m.jbc_usb_fe_flow_x_mil||0));if(f&4)add(uiLang=='de'?'Gewählter Flow':'Selected flow',Number(m.jbc_usb_fe_selected_flow_x_mil||0));if(f&2)add(uiLang=='de'?'Drehzahl':'Speed',Number(m.jbc_usb_fe_speed_rpm||0)+' rpm');if(f&8)add(uiLang=='de'?'Filterstatus':'Filter status',Number(m.jbc_usb_fe_filter_status||0))}else if(kind=='SF'){let f=Number(m.jbc_usb_sf_station_flags||0)&65535,programs=Array.isArray(m.jbc_usb_sf_programs)?m.jbc_usb_sf_programs:[];add(uiLang=='de'?'Programme':'Programs',programs.length);if(m.jbc_usb_sf_conti_valid)add('Conti Mode','speed '+Number(m.jbc_usb_sf_conti_speed||0));if(f&1024)add('Robot Status',(f&2048)?t('on'):t('off'));if(f&64)add(uiLang=='de'?'Längeneinheit':'Length unit','Raw '+Number(m.jbc_usb_sf_length_unit||0))}else if(kind=='CL'){let f=Number(p.cl_flags||0)&65535;if(f&16)add(uiLang=='de'?'Verbindungsmodus':'Connection mode',(f&32)?'CONTROL':'MONITOR')}if(!a.length){add('Software',m.jbc_usb_sw_version||'-');add('Hardware',m.jbc_usb_hw_version||'-');add(uiLang=='de'?'Ports':'Ports',Number(m.jbc_usb_port_count||0)||'-');add(uiLang=='de'?'Protokoll':'Protocol',jbcUsbProtoName(m.jbc_usb_command_protocol))}return a.slice(0,4).join('')}
function jbcUsbStationCardHtml(m,kind,details=''){let online=!!m.online,linked=online&&((Number(m.jbc_link_flags||0)&1)!=0),err=Number(m.jbc_usb_station_error),errKnown=Number.isFinite(err)&&err!==65535,stateClass=!linked?'offline':(errKnown&&err?'fault':'ok'),stateText=!linked?(uiLang=='de'?'getrennt':'disconnected'):((errKnown&&err)?(uiLang=='de'?'Fehler':'fault'):(uiLang=='de'?'verbunden':'connected')),model=String(m.jbc_usb_model||'').trim(),raw=String(m.jbc_usb_model_raw||'').trim();if(!model||model=='-')model=raw&&raw!='-'?raw.split('_')[0]:'JBC';let station=String(m.jbc_usb_station_name||'').trim(),subtitle=jbcUsbKindLabel(kind)+(station?' · '+station:''),mark=kind=='SOLD'?'S':(kind=='UNKNOWN'?'?':kind);return `<section class="jbu-station-section"><div class="jbu-station-card kind-${String(kind||'unknown').toLowerCase()}"><div class="jbu-station-head"><div class="jbu-station-identity"><span class="jbu-station-mark">${escHtml(mark)}</span><div class="jbu-station-copy"><small>${uiLang=='de'?'JBC Station':'JBC station'}</small><strong title="${escHtml(model)}">${escHtml(model)}</strong><span title="${escHtml(subtitle)}">${escHtml(subtitle)}</span></div></div><span class="pill jbu-station-state ${stateClass}">${stateText}</span></div><div class="jbu-station-metrics">${jbcUsbStationHighlights(m,kind)}</div>${details?`<div class="jbu-station-more">${details}</div>`:''}</div></section>`}
function jbcUsbWithoutDuplicateConnectionMode(html){return String(html||'').replace(/<div class="(?:jbu-station-metric|jbu-port-detail-item(?: [^"]*)?)"><span class="k">(?:JBC )?(?:Verbindungsmodus|Connection mode)<\/span><strong[^>]*>[^<]*<\/strong><\/div>/gi,'')}
const jbcUsbStationHighlightsRawHtml=jbcUsbStationHighlights;
jbcUsbStationHighlights=function(m,kind){return jbcUsbWithoutDuplicateConnectionMode(jbcUsbStationHighlightsRawHtml(m,kind))};
function jbcUsbStationDetailBody(html){let body=jbcUsbWithoutDuplicateConnectionMode(html).trim();if(!body)return '';body=body.replace(/^<details\b[^>]*>\s*<summary>[\s\S]*?<\/summary>/i,'').replace(/<\/details>\s*$/i,'').replace(/<div class="jbu-port-detail-grid">\s*<\/div>/gi,'').trim();return body.replace(/<[^>]*>/g,'').trim()?body:''}
jbcUsbStationCardHtml=function(m,kind,details=''){let body=jbcUsbStationDetailBody(details);if(!body)return '';return `<section class="jbu-station-section"><div class="jbu-station-card kind-${String(kind||'unknown').toLowerCase()}"><div class="jbu-station-title"><strong>${uiLang=='de'?'Stationsdaten':'Station data'}</strong><span>${escHtml(jbcUsbKindLabel(kind))}</span></div><div class="jbu-station-body">${body}</div></div></section>`};
const jbcUsbSoldStationDiagRawHtml=jbcUsbSoldStationDiagHtml;
const jbcUsbHaStationDiagRawHtml=jbcUsbHaStationDiagHtml;
const jbcUsbPhStationDiagRawHtml=jbcUsbPhStationDiagHtml;
const jbcUsbFeStationDiagRawHtml=jbcUsbFeStationDiagHtml;
const jbcUsbSfStationDiagRawHtml=jbcUsbSfStationDiagHtml;
jbcUsbSoldStationDiagHtml=m=>jbcUsbStationCardHtml(m,'SOLD',jbcUsbSoldStationDiagRawHtml(m));
jbcUsbHaStationDiagHtml=m=>jbcUsbStationCardHtml(m,'HA',jbcUsbHaStationDiagRawHtml(m));
jbcUsbPhStationDiagHtml=m=>jbcUsbStationCardHtml(m,'PH',jbcUsbPhStationDiagRawHtml(m));
jbcUsbFeStationDiagHtml=m=>jbcUsbStationCardHtml(m,'FE',jbcUsbFeStationDiagRawHtml(m));
jbcUsbSfStationDiagHtml=m=>jbcUsbStationCardHtml(m,'SF',jbcUsbSfStationDiagRawHtml(m));
function jbcUsbRawTempC(v,fallback=350){v=Number(v);return Number.isFinite(v)&&v>=0&&v<65535?Math.round(v/9):fallback}
function jbcUsbRawFlowPct(v,fallback=50){v=Number(v);return Number.isFinite(v)&&v>=0?Math.round(v)/10:fallback}
function jbcUsbFeFlowPct(v,fallback=50){v=Number(v);if(!Number.isFinite(v)||v<450||v>580)return fallback;return Math.round((((v-450)*800/130)+200)/100)*10}
function jbcUsbSupportsTempLevels(m,kind){
  if(kind=='SOLD')return true;
  if(kind!='HA')return false;
  let model=String(m.jbc_usb_model||'').trim().toUpperCase();
  let modelType=String(m.jbc_usb_model_type||'').trim().toUpperCase();
  let modelVersion=Number(m.jbc_usb_model_version||0);
  // Mirrors JBC_Connect CFeaturesDataInitializer exactly: protocol-02 hot-air
  // stations support temperature levels by default, except JTSE/CAP v1+.
  return !(model=='JTSE'&&modelType=='CAP'&&modelVersion>=1);
}
function jbcUsbConfigHtml(m,p,addr,port){if(!m.online||!p||!p.valid)return '';let kind=jbcUsbStationKind(m),de=uiLang=='de',open=jbcUsbDetailIsOpen(addr,port)?' open':'',tempLimits=kind=='SOLD'?[m.jbc_usb_sold_min_temp,m.jbc_usb_sold_max_temp]:kind=='HA'?[m.jbc_usb_ha_min_temp,m.jbc_usb_ha_max_temp]:[m.jbc_usb_ph_min_temp,m.jbc_usb_ph_max_temp],tmin=jbcUsbRawTempC(tempLimits[0],50),tmax=jbcUsbRawTempC(tempLimits[1],500);if(tmax<tmin){tmin=50;tmax=500}let body='';if(kind=='SOLD'||kind=='HA'){let selected=jbcUsbRawTempC(p.selected_temp,350),flow=jbcUsbRawFlowPct(p.selected_flow_permille,50);body+=`<div class="jbu-config-grid"><label>${de?'Solltemperatur':'Set temperature'}<input id="jbc_cfg_temp_${addr}_${port}" type="number" min="${tmin}" max="${tmax}" step="1" value="${selected}" onchange="jbcUsbAutoTemperature(${addr},${port},'jbc_cfg_temp_${addr}_${port}','jbc_cfg_status_${addr}_${port}')"></label>${kind=='HA'?`<label>${de?'Soll-Luftstrom':'Set flow'}<input id="jbc_cfg_flow_${addr}_${port}" type="number" min="${jbcUsbRawFlowPct(m.jbc_usb_ha_min_flow,10)}" max="${jbcUsbRawFlowPct(m.jbc_usb_ha_max_flow,100)}" step="0.1" value="${flow}" onchange="jbcUsbAutoFlow(${addr},${port},'jbc_cfg_flow_${addr}_${port}','jbc_cfg_status_${addr}_${port}')"></label>`:''}</div>`;if(jbcUsbSupportsTempLevels(m,kind)){let lt=p.level_temp||[],lf=p.level_flow_permille||[],le=p.level_ext_temp||[],lo=p.level_on||[];body+=`<div class="jbu-config-head"><label><input id="jbc_cfg_levels_on_${addr}_${port}" type="checkbox" ${Number(p.levels_on||0)?'checked':''} onchange="jbcUsbAutoLevels(${addr},${port},'${kind}','jbc_cfg_status_${addr}_${port}')"> ${de?'Temperaturlevel aktiv':'Temperature levels enabled'}</label><select id="jbc_cfg_level_sel_${addr}_${port}" onchange="jbcUsbAutoLevels(${addr},${port},'${kind}','jbc_cfg_status_${addr}_${port}')"><option value="0" ${Number(p.selected_level)===0?'selected':''}>Level 1</option><option value="1" ${Number(p.selected_level)===1?'selected':''}>Level 2</option><option value="2" ${Number(p.selected_level)===2?'selected':''}>Level 3</option></select></div>`;for(let i=0;i<3;i++)body+=`<div class="jbu-level-edit ${kind=='HA'?'is-ha':''}"><input id="jbc_cfg_l_on_${addr}_${port}_${i}" type="checkbox" ${Number(lo[i]||0)?'checked':''} onchange="jbcUsbAutoLevels(${addr},${port},'${kind}','jbc_cfg_status_${addr}_${port}')"><label>Level ${i+1} °C<input id="jbc_cfg_l_t_${addr}_${port}_${i}" type="number" min="${tmin}" max="${tmax}" step="1" value="${jbcUsbRawTempC(lt[i],selected)}" onchange="jbcUsbAutoLevels(${addr},${port},'${kind}','jbc_cfg_status_${addr}_${port}')"></label>${kind=='HA'?`<label>Flow %<input id="jbc_cfg_l_f_${addr}_${port}_${i}" type="number" min="${jbcUsbRawFlowPct(m.jbc_usb_ha_min_flow,10)}" max="${jbcUsbRawFlowPct(m.jbc_usb_ha_max_flow,100)}" step="0.1" value="${jbcUsbRawFlowPct(lf[i],flow)}" onchange="jbcUsbAutoLevels(${addr},${port},'${kind}','jbc_cfg_status_${addr}_${port}')"></label><label>Ext. TC °C<input id="jbc_cfg_l_e_${addr}_${port}_${i}" type="number" min="${tmin}" max="${tmax}" step="1" value="${jbcUsbRawTempC(le[i],selected)}" onchange="jbcUsbAutoLevels(${addr},${port},'${kind}','jbc_cfg_status_${addr}_${port}')"></label>`:''}</div>`;}}else if(kind=='PH'&&port===0){let tc=m.jbc_usb_ph_tc||[];body='<div class="jbu-config-grid">';for(let i=0;i<4;i++){let x=tc[i]||{},flags=Number(x.flags||0);if(flags||i<2)body+=`<div><label>TC${i+1} ${de?'Solltemperatur':'set temperature'}<input id="jbc_cfg_ph_${addr}_${i}" type="number" min="${tmin}" max="${tmax}" step="1" value="${jbcUsbRawTempC(x.selected_temp,150)}" onchange="jbcUsbAutoTemperature(${addr},${i},'jbc_cfg_ph_${addr}_${i}','jbc_cfg_status_${addr}_0')"></label></div>`}body+='</div>'}else if(kind=='FE'&&port===0){body=`<div class="jbu-config-grid"><label>${de?'Soll-Flow':'Set flow'}<input id="jbc_cfg_fe_${addr}" type="number" min="20" max="100" step="10" value="${jbcUsbFeFlowPct(m.jbc_usb_fe_selected_flow_x_mil,50)}" onchange="jbcUsbAutoFeFlow(${addr},'jbc_cfg_status_${addr}_0')"></label></div>`}if(!body)return '';return `<details class="jbu-config"${open} ontoggle="jbcUsbRememberDetail(${Number(addr)},${Number(port)},this.open)"><summary>${de?'Stationsparameter einstellen':'Configure station parameters'}</summary>${body}<div id="jbc_cfg_status_${addr}_${port}" class="jbu-config-status" aria-live="polite"></div></details>`}
const jbcUsbConfigTimers={};
const jbcUsbConfigWriteChains={};
function jbcUsbSetConfigStatus(id,text,state=''){let e=document.getElementById(id);if(!e)return;e.textContent=text;e.className='jbu-config-status'+(state?' '+state:'')}
function jbcUsbQueueConfig(key,params,statusId){clearTimeout(jbcUsbConfigTimers[key]);jbcUsbSetConfigStatus(statusId,uiLang=='de'?'Wird gespeichert ...':'Saving ...','is-saving');jbcUsbConfigTimers[key]=setTimeout(()=>{delete jbcUsbConfigTimers[key];let addr=String(params.get('addr')||'0'),previous=jbcUsbConfigWriteChains[addr]||Promise.resolve(),current=previous.then(()=>jbcUsbPostConfig(params,null,statusId)),tracked=current.finally(()=>{if(jbcUsbConfigWriteChains[addr]===tracked)delete jbcUsbConfigWriteChains[addr]});jbcUsbConfigWriteChains[addr]=tracked},450)}
async function jbcUsbPostConfig(params,button,statusId=''){let old=button?button.textContent:'';if(button){button.disabled=true;button.textContent=uiLang=='de'?'Wird geschrieben ...':'Writing ...'}jbcUsbSetConfigStatus(statusId,uiLang=='de'?'Wird gespeichert ...':'Saving ...','is-saving');try{let r=await fetch('/jbc-usb/config',{method:'POST',body:params,cache:'no-store'}),txt=await r.text();if(!r.ok)throw(txt||r.statusText);if(button)button.blur();jbcUsbSetConfigStatus(statusId,uiLang=='de'?'Gespeichert':'Saved');setTimeout(()=>load(true),600);setTimeout(()=>load(true),1900);return true}catch(e){jbcUsbSetConfigStatus(statusId,uiLang=='de'?'Speichern fehlgeschlagen':'Save failed','is-error');alert((uiLang=='de'?'JBC-Parameter: ':'JBC parameter: ')+e);return false}finally{if(button){button.disabled=false;button.textContent=old}}}
function jbcUsbNum(id){let e=document.getElementById(id),v=e?Number(e.value):NaN;if(!Number.isFinite(v))throw new Error(uiLang=='de'?'Ungültiger Zahlenwert':'Invalid number');return v}
function jbcUsbAutoTemperature(addr,port,inputId,statusId){try{let p=new URLSearchParams({addr,port,action:'temperature',value:String(Math.round(jbcUsbNum(inputId)*9))});jbcUsbQueueConfig(`temp:${addr}:${port}`,p,statusId)}catch(e){alert(e.message||e)}}
function jbcUsbAutoFlow(addr,port,inputId,statusId){try{let p=new URLSearchParams({addr,port,action:'flow',value:String(Math.round(jbcUsbNum(inputId)*10))});jbcUsbQueueConfig(`flow:${addr}:${port}`,p,statusId)}catch(e){alert(e.message||e)}}
function jbcUsbAutoLevels(addr,port,kind,statusId){try{let p=new URLSearchParams({addr,port,action:'levels',enabled:document.getElementById(`jbc_cfg_levels_on_${addr}_${port}`).checked?'1':'0',selected:document.getElementById(`jbc_cfg_level_sel_${addr}_${port}`).value,mask:'0'}),mask=0;for(let i=0;i<3;i++){if(document.getElementById(`jbc_cfg_l_on_${addr}_${port}_${i}`).checked)mask|=1<<i;p.set('t'+i,String(Math.round(jbcUsbNum(`jbc_cfg_l_t_${addr}_${port}_${i}`)*9)));p.set('f'+i,kind=='HA'?String(Math.round(jbcUsbNum(`jbc_cfg_l_f_${addr}_${port}_${i}`)*10)):'0');p.set('e'+i,kind=='HA'?String(Math.round(jbcUsbNum(`jbc_cfg_l_e_${addr}_${port}_${i}`)*9)):'0')}p.set('mask',String(mask));jbcUsbQueueConfig(`levels:${addr}:${port}`,p,statusId)}catch(e){alert(e.message||e)}}
function jbcUsbAutoFeFlow(addr,statusId){try{let p=new URLSearchParams({addr,port:'0',action:'flow',value:String(Math.round(jbcUsbNum(`jbc_cfg_fe_${addr}`)*10))});jbcUsbQueueConfig(`flow:${addr}:0`,p,statusId)}catch(e){alert(e.message||e)}}
async function jbcUsbSaveTemperature(addr,port,inputId,button){try{let p=new URLSearchParams({addr,port,action:'temperature',value:String(Math.round(jbcUsbNum(inputId)*9))});await jbcUsbPostConfig(p,button)}catch(e){alert(e.message||e)}}
async function jbcUsbSaveFlow(addr,port,inputId,button){try{let p=new URLSearchParams({addr,port,action:'flow',value:String(Math.round(jbcUsbNum(inputId)*10))});await jbcUsbPostConfig(p,button)}catch(e){alert(e.message||e)}}
async function jbcUsbSaveLevels(addr,port,kind,button){try{let p=new URLSearchParams({addr,port,action:'levels',enabled:document.getElementById(`jbc_cfg_levels_on_${addr}_${port}`).checked?'1':'0',selected:document.getElementById(`jbc_cfg_level_sel_${addr}_${port}`).value,mask:'0'}),mask=0;for(let i=0;i<3;i++){if(document.getElementById(`jbc_cfg_l_on_${addr}_${port}_${i}`).checked)mask|=1<<i;p.set('t'+i,String(Math.round(jbcUsbNum(`jbc_cfg_l_t_${addr}_${port}_${i}`)*9)));p.set('f'+i,kind=='HA'?String(Math.round(jbcUsbNum(`jbc_cfg_l_f_${addr}_${port}_${i}`)*10)):'0');p.set('e'+i,kind=='HA'?String(Math.round(jbcUsbNum(`jbc_cfg_l_e_${addr}_${port}_${i}`)*9)):'0')}p.set('mask',String(mask));await jbcUsbPostConfig(p,button)}catch(e){alert(e.message||e)}}
async function jbcUsbSaveFeFlow(addr,button){try{let p=new URLSearchParams({addr,port:'0',action:'flow',value:String(Math.round(jbcUsbNum(`jbc_cfg_fe_${addr}`)*10))});await jbcUsbPostConfig(p,button)}catch(e){alert(e.message||e)}}
function jbcUsbPortsHtml(m){let ports=m.jbc_usb_ports||[],cnt=Math.max(0,Math.min(4,Number(m.jbc_usb_port_count||0))),kind=jbcUsbStationKind(m);if(!cnt)return `<div class="muted">${uiLang=='de'?'Portanzahl wird ermittelt ...':'Detecting port count ...'}</div>`;let out='';for(let i=0;i<cnt;i++){let p=ports[i]||{},ok=!!p.valid,st=jbcUsbPortStateText(m,p),sc=jbcUsbStateClass(st),tool=jbcUsbToolLabel(m,p),metrics='';if(ok){if(kind=='SOLD'){let sel=jbcUsbSoldEffectiveSetpoint(p);metrics=`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Werkzeug':'Tool'}</span><strong>${tool}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Temperatur':'Temperature'}</span><strong>${jbcUsbTemp(p.temperature)}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Soll':'Setpoint'}</span><strong>${sel}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Leistung':'Power'}</span><strong>${jbcUsbPercent(p.power_permille)}</strong></div>`}else if(kind=='HA'){let hf=Number(p.ha_value_flags||0),selT=(hf&2)?jbcUsbTemp(p.selected_temp):'-',selF=(hf&4)?jbcUsbPercent(p.selected_flow_permille):'-',prot=(hf&1)?jbcUsbTemp(p.protection_temp):'-';metrics=`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Werkzeug':'Tool'}</span><strong>${tool}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Temperatur':'Temperature'}</span><strong>${jbcUsbTemp(p.temperature)}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Soll':'Setpoint'}</span><strong>${selT}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Leistung':'Power'}</span><strong>${jbcUsbPercent(p.power_permille)}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Luftstrom':'Flow'}</span><strong>${jbcUsbPercent(p.time_to_sleep_hibern)}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Soll-Luftstrom':'Set flow'}</span><strong>${selF}</strong></div><div class="jbu-port-metric"><span class="k">Protection TC</span><strong>${prot}</strong></div><div class="jbu-port-metric"><span class="k">Time to stop</span><strong>${jbcUsbTimeToStop(p.time_to_stop)}</strong></div>`}else if(kind=='PH'){let ps=Number(p.ph_status_flags||0)&255,tc=m.jbc_usb_ph_tc||[],tcMetrics='';for(let ti=0;ti<4;ti++){let x=tc[ti]||{},tf=Number(x.flags||0)&255;if(tf&1)tcMetrics+=`<div class="jbu-port-metric"><span class="k">TC${ti+1}</span><strong>${jbcUsbTemp(x.actual_temp)}</strong></div>`}metrics=`${tcMetrics}<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Heizleistung':'Heater power'}</span><strong>${jbcUsbPercent(p.power_permille)}</strong></div><div class="jbu-port-metric"><span class="k">Time to stop</span><strong>${jbcUsbTimeToStop(p.time_to_stop)}</strong></div><div class="jbu-port-metric"><span class="k">Heater</span><strong>${jbcUsbPhOnOff(ps&1)}</strong></div><div class="jbu-port-metric"><span class="k">Zone A / B</span><strong>${jbcUsbPhOnOff(ps&4)} / ${jbcUsbPhOnOff(ps&2)}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Interner Lüfter':'Internal fan'}</span><strong>${jbcUsbPhOnOff(ps&8)}</strong></div><div class="jbu-port-metric"><span class="k">Pedal</span><strong>${(ps&16)?((ps&32)?jbcUsbPhOnOff(1):jbcUsbPhOnOff(0)):(uiLang=='de'?'nicht verbunden':'not connected')}</strong></div>`}else if(kind=='SF'){let sf=Number(p.sf_flags||0)&65535;metrics=`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Modus':'Mode'}</span><strong>${Number(p.future_mode||0)}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Programm':'Program'}</span><strong>${Number(p.time_to_sleep_hibern||0)}</strong></div>${sf&1?`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Geschwindigkeit':'Speed'}</span><strong>${(Number(p.sf_speed_tenth_mm_s||0)/10).toFixed(1)} mm/s</strong></div>`:''}${sf&2?`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Länge':'Length'}</span><strong>${(Number(p.sf_length_tenth_mm||0)/10).toFixed(1)} mm</strong></div>`:''}${sf&4?`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Zuführung':'Feeding'}</span><strong>${jbcUsbSfOnOff(Number(p.sf_feeding_state||0)!=0)}</strong></div>`:''}`}else if(kind=='FE'){let lev=['HIGH','MEDIUM','LOW','CUSTOM'][Number(p.future_mode||0)]||String(Number(p.future_mode||0));metrics=`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Absaugstufe':'Suction level'}</span><strong>${lev}</strong></div>`}else if(kind=='CL'){let cm={1:'DETECTION',2:'CONTINUOUS',4:'CALIBRATING'}[Number(p.future_mode||0)]||String(Number(p.future_mode||0)),cf=Number(p.cl_flags||0),mot=(cf&1)?(p.cl_motors_on?t('on'):t('off')):'-',door=(cf&2)?(p.cl_door_open?(uiLang=='de'?'OFFEN':'OPEN'):(uiLang=='de'?'ZU':'CLOSED')):'-';metrics=`<div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Reinigungsmodus':'Cleaner mode'}</span><strong>${cm}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Motoren':'Motors'}</span><strong>${mot}</strong></div><div class="jbu-port-metric"><span class="k">${uiLang=='de'?'Tür':'Door'}</span><strong>${door}</strong></div>`}else{metrics=`<div class="jbu-port-metric"><span class="k">Status</span><strong>${st}</strong></div>`}}else{metrics=`<div class="muted">${uiLang=='de'?'Keine Antwort vom Port':'No response from port'}</div>`}let err=ok?jbcUsbToolErrorName(m,p):'',bad=ok&&jbcUsbToolErrorCode(m,p)!=0,errHtml=ok?`<span class="jbu-error ${bad?'bad':''}" title="${err}">${bad?err:'OK'}</span>`:'';out+=`<div class="jbu-port-card"><div class="jbu-port-head"><strong>Port ${i+1}</strong><div class="jbu-port-head-right">${errHtml}<span class="pill jbu-port-state ${sc}">${st}</span></div></div><div class="jbu-port-metrics">${metrics}</div>${jbcUsbPortFlagsHtml(m,p)}${jbcUsbFutureHtml(m,p)}${kind=='SOLD'?(jbcUsbSoldDetailsHtml(m,p,m.addr,i)+jbcUsbSoldDiagDetailsHtml(m,p,m.addr,i)+jbcUsbCartridgeDetailsHtml(m,p,m.addr,i)):(kind=='HA'?(jbcUsbHaDetailsHtml(p,m.addr,i)+jbcUsbHaDiagDetailsHtml(m,p,m.addr,i)):(kind=='PH'?jbcUsbPhDetailsHtml(p,m.addr,i):(kind=='SF'?jbcUsbSfDetailsHtml(p,m.addr,i):(kind=='FE'?jbcUsbFeDetailsHtml(p,m.addr,i):(kind=='CL'?jbcUsbClDetailsHtml(p,m.addr,i):'')))))}${jbcUsbConfigHtml(m,p,m.addr,i)}</div>`}return out}
const jbcUsbPortsRawHtml=jbcUsbPortsHtml;
jbcUsbPortsHtml=function(m){let kind=jbcUsbStationKind(m),host=document.getElementById('jbu_sold_stationdiag_'+m.addr),portHost=document.getElementById('jbu_ports_'+m.addr),section=portHost&&portHost.closest?portHost.closest('.jbu-port-section'):null;if(section)section.hidden=false;if(host&&(kind=='CL'||kind=='UNKNOWN'))host.innerHTML=jbcUsbStationCardHtml(m,kind,'');return jbcUsbPortsRawHtml(m)};
async function jbcUsbRenameStation(addr){let m=((window.lastState&&window.lastState.modules)||[]).find(x=>Number(x.addr)===Number(addr));if(!m||!m.online||!(Number(m.jbc_link_flags||0)&1)){alert(uiLang=='de'?'Die JBC-Station ist nicht verbunden.':'The JBC station is not connected.');return}let current=String(m.jbc_usb_station_name||'');let name=prompt(uiLang=='de'?'Neuer Stationsname (maximal 16 Zeichen)':'New station name (maximum 16 characters)',current);if(name===null)return;if(name.length>16){alert(uiLang=='de'?'Der Stationsname darf höchstens 16 Zeichen lang sein.':'The station name may contain at most 16 characters.');return}let allowed=" 0123456789QWERTYUIOPASDFGHJKLMNBVCXZ'!?$%&@-=,.;()[]",upper=name.toUpperCase();for(let ch of upper){if(!allowed.includes(ch)){alert(uiLang=='de'?'Dieses Zeichen wird von JBC nicht unterstützt: '+ch:'This character is not supported by JBC: '+ch);return}}if(!name.length&&!confirm(uiLang=='de'?'Stationsname wirklich löschen?':'Really clear the station name?'))return;let body=new URLSearchParams();body.set('addr',addr);body.set('name',name);let value=document.getElementById('jbu_stationname_'+addr),previous=value?value.textContent:'';if(value)value.textContent=uiLang=='de'?'Wird geschrieben ...':'Writing ...';try{let r=await fetch('/jbc-usb/station-name',{method:'POST',body,cache:'no-store'}),txt=await r.text();if(!r.ok)throw(txt||r.statusText);if(value)value.textContent=name||'-';setTimeout(()=>load(true),500);setTimeout(()=>load(true),1800)}catch(e){if(value)value.textContent=previous;alert((uiLang=='de'?'Stationsname: ':'Station name: ')+e)}}
document.addEventListener('click',e=>{let el=e.target&&e.target.closest?e.target.closest('[id^="jbu_stationname_"]'):null;if(!el)return;let addr=Number(el.id.slice('jbu_stationname_'.length));if(addr)jbcUsbRenameStation(addr)});
document.addEventListener('keydown',e=>{if(e.key!='Enter'&&e.key!=' ')return;let el=e.target&&e.target.closest?e.target.closest('[id^="jbu_stationname_"]'):null;if(!el)return;e.preventDefault();let addr=Number(el.id.slice('jbu_stationname_'.length));if(addr)jbcUsbRenameStation(addr)});
function jbcUsbGlyph(){return `<svg class="usb-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M15,7V11H16V13H13V5H15L12,1L9,5H11V13H8V10.93C8.7,10.56 9.2,9.85 9.2,9C9.2,7.78 8.21,6.8 7,6.8C5.78,6.8 4.8,7.78 4.8,9C4.8,9.85 5.3,10.56 6,10.93V13A2,2 0 0,0 8,15H11V18.05C10.29,18.41 9.8,19.15 9.8,20A2.2,2.2 0 0,0 12,22.2A2.2,2.2 0 0,0 14.2,20C14.2,19.15 13.71,18.41 13,18.05V15H16A2,2 0 0,0 18,13V11H19V7H15Z" fill="currentColor"/></svg>`}
function moduleSerialGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M7,3H17V5H19V8H16V14H8V8H5V5H7V3M17,9H19V14H17V9M11,15H13V22H11V15M5,9H7V14H5V9Z" fill="currentColor"/></svg>`}
function moduleSwitchGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M1,11H3.17C3.58,9.83 4.69,9 6,9C6.65,9 7.25,9.21 7.74,9.56L14.44,4.87L15.58,6.5L8.89,11.2C8.96,11.45 9,11.72 9,12A3,3 0 0,1 6,15C4.69,15 3.58,14.17 3.17,13H1V11M23,11V13H20.83C20.42,14.17 19.31,15 18,15A3,3 0 0,1 15,12A3,3 0 0,1 18,9C19.31,9 20.42,9.83 20.83,11H23M6,11A1,1 0 0,0 5,12A1,1 0 0,0 6,13A1,1 0 0,0 7,12A1,1 0 0,0 6,11M18,11A1,1 0 0,0 17,12A1,1 0 0,0 18,13A1,1 0 0,0 19,12A1,1 0 0,0 18,11Z" fill="currentColor"/></svg>`}
function moduleFanGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M12,11A1,1 0 0,0 11,12A1,1 0 0,0 12,13A1,1 0 0,0 13,12A1,1 0 0,0 12,11M12.5,2C17,2 17.11,5.57 14.75,6.75C13.76,7.24 13.32,8.29 13.13,9.22C13.61,9.42 14.03,9.73 14.35,10.13C18.05,8.13 22.03,8.92 22.03,12.5C22.03,17 18.46,17.1 17.28,14.73C16.78,13.74 15.72,13.3 14.79,13.11C14.59,13.59 14.28,14 13.88,14.34C15.87,18.03 15.08,22 11.5,22C7,22 6.91,18.42 9.27,17.24C10.25,16.75 10.69,15.71 10.89,14.79C10.4,14.59 9.97,14.27 9.65,13.87C5.96,15.85 2,15.07 2,11.5C2,7 5.56,6.89 6.74,9.26C7.24,10.25 8.29,10.68 9.22,10.87C9.41,10.39 9.73,9.97 10.14,9.65C8.15,5.96 8.94,2 12.5,2Z" fill="currentColor"/></svg>`}
function moduleMonitorGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M21,16H3V4H21M21,2H3C1.89,2 1,2.89 1,4V16A2,2 0 0,0 3,18H10V20H8V22H16V20H14V18H21A2,2 0 0,0 23,16V4C23,2.89 22.1,2 21,2Z" fill="currentColor"/></svg>`}
function jbcUsbPortGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 2C6.9 2 6 2.9 6 4V12H5V16L9 20V22H15V20L19 16V12H18V4C18 2.9 17.11 2 16 2M8 4H16V12H8M9 7V9H11V7M13 7V9H15V7Z" fill="currentColor"/></svg>`}
function jbcUsbWorkGlyph(){return `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4.86 4.03L2.03 6.86L5.21 10.04V12.87L6.63 14.28L12.28 8.63L10.87 7.21H8.04L4.86 4.03M17 6V7.5C18 7.5 18.85 8.33 18.85 9.35C18.85 10.37 18 11.2 17 11.2V12.7C19.24 12.7 21 14.53 21 16.77V21H22.5V16.76C22.5 14.54 21.22 12.62 19.35 11.73C19.97 11.12 20.35 10.28 20.35 9.35C20.35 7.5 18.85 6 17 6M11.93 11.1L9.1 13.93L14.05 18.88L14.76 18.17L16.88 20.29L19 21L18.29 18.88L16.17 16.76L16.88 16.05L11.93 11.1Z" fill="currentColor"/></svg>`}
function jbcUsbStandGlyph(){return `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12,3C7,3 3,7 3,12C3,17 7,21 12,21C17,21 21,17 21,12C21,7 17,3 12,3M12,19C8.1,19 5,15.9 5,12C5,8.1 8.1,5 12,5C15.9,5 19,8.1 19,12C19,15.9 15.9,19 12,19M20.5,20.5C22.7,18.3 24,15.3 24,12C24,8.7 22.7,5.7 20.5,3.5L19.4,4.6C21.3,6.5 22.5,9.1 22.5,12C22.5,14.9 21.3,17.5 19.4,19.4L20.5,20.5M4.6,19.4C2.7,17.5 1.5,14.9 1.5,12C1.5,9.1 2.7,6.5 4.6,4.6L3.5,3.5C1.3,5.7 0,8.7 0,12C0,15.3 1.3,18.3 3.5,20.5L4.6,19.4M9.5,7V17H11.5V13H13.5A2,2 0 0,0 15.5,11V9A2,2 0 0,0 13.5,7H9.5M11.5,9H13.5V11H11.5V9Z" fill="currentColor"/></svg>`}
function jbcFaeDbgOn(v){return Number(v||0)?t('on'):t('off')}
function jbcFaeDbgFilter(v){v=Math.max(0,Math.min(1000,Number(v||0)));return v+' / 1000 ('+(v/10).toFixed(1)+' %)'}
function jbcFaeDbgPair(tx,rx,unit=''){let a=String(tx),b=(rx===null||typeof rx==='undefined')?'-':String(rx),u=unit?(' '+unit):'';return 'TX '+a+u+' · RX '+b+u}
function jbcFaeDebugUpdate(d,m){if(!devMode)return;let j=d.jbc||{},valid=!!m.jbc_dbg_settings_valid,rx=k=>valid?m[k]:null,txOut=Object.prototype.hasOwnProperty.call(d,'jbc_fae_output_tx')?!!d.jbc_fae_output_tx:null,rxOut=m.jbc_dbg_extractor_output_valid?!!m.jbc_dbg_extractor_output_rx:null;set('jbc_dbg_output_'+m.addr,'TX '+(txOut===null?'-':jbcFaeDbgOn(txOut))+' · RX '+(rxOut===null?'-':jbcFaeDbgOn(rxOut)));set('jbc_dbg_workmask_'+m.addr,'0x'+Number(m.jbc_work_mask||0).toString(16).toUpperCase().padStart(2,'0'));set('jbc_dbg_standmask_'+m.addr,'0x'+Number(m.jbc_stand_mask||0).toString(16).toUpperCase().padStart(2,'0'));set('jbc_dbg_suction_'+m.addr,jbcFaeDbgPair(Number(j.suction_level||0),rx('jbc_dbg_suction_level_rx')));set('jbc_dbg_flowset_'+m.addr,jbcFaeDbgPair(Number(j.select_flow||0),rx('jbc_dbg_select_flow_rx')));set('jbc_dbg_flow_'+m.addr,String(Number(m.jbc_dbg_actual_flow_rx||0)));set('jbc_dbg_speed_'+m.addr,String(Number(m.jbc_dbg_speed_rpm_rx||0))+' rpm');set('jbc_dbg_workdelay_'+m.addr,jbcFaeDbgPair(Number(j.delay_work_sec||0),rx('jbc_dbg_delay_work_sec_rx'),'s'));set('jbc_dbg_standdelay_'+m.addr,jbcFaeDbgPair(Number(j.delay_stand_sec||0),rx('jbc_dbg_delay_stand_sec_rx'),'s'));set('jbc_dbg_standintakes_'+m.addr,'TX '+jbcFaeDbgOn(j.stand_intakes)+' · RX '+(valid?jbcFaeDbgOn(m.jbc_dbg_stand_intakes_rx):'-'));set('jbc_dbg_cont_'+m.addr,'TX '+jbcFaeDbgOn(j.continuous)+' · RX '+(valid?jbcFaeDbgOn(m.jbc_dbg_continuous_rx):'-'));set('jbc_dbg_filterlife_'+m.addr,'TX '+jbcFaeDbgFilter(j.filter_life)+' · RX '+jbcFaeDbgFilter(m.jbc_filter_life_rx));set('jbc_dbg_filtersat_'+m.addr,'TX '+jbcFaeDbgFilter(j.filter_sat)+' · RX '+jbcFaeDbgFilter(m.jbc_filter_sat_rx));set('jbc_dbg_error_'+m.addr,'TX 0x'+Number(j.stat_error||0).toString(16).toUpperCase().padStart(4,'0')+' · RX 0x'+Number(m.jbc_stat_error_rx||0).toString(16).toUpperCase().padStart(4,'0'))}
function renderDetails(d){let det='';(d.modules||[]).forEach(m=>{if(m.type==1){det+=`<div class="tile module-card jbc-card core-style"><div class="module-head"><div class="module-title">${mn(m)}</div><div class="module-head-main"><div class="module-identity"><span class="module-icon">${moduleSerialGlyph()}</span><div class="module-head-copy"><div class="module-type">JBC Bus</div><div class="module-address">${hx(m.addr)}</div></div></div><div class="module-statuses"><span id="jbc_mod_${m.addr}" class="pill">-</span><span id="jbc_conn_${m.addr}" class="pill">-</span></div></div></div><div class="jbu-hero"><div class="jbu-hero-top"><div id="jbc_station_head_${m.addr}" class="jbu-station-name">-</div><div class="jbu-badges"><span class="jbu-chip proto">JBC</span><span class="jbu-chip kind">${uiLang=='de'?'FAE Gerätebus':'FAE device bus'}</span></div></div></div><div class="jbu-state-grid"><div id="jbc_workbox_${m.addr}" class="jbu-state-card"><div class="jbu-state-symbol"><span class="jbu-work-symbol" aria-label="work">${jbcUsbWorkGlyph()}</span></div><div class="jbu-state-copy"><small>WORK</small><strong id="jbc_work_${m.addr}">-</strong></div></div><div id="jbc_standbox_${m.addr}" class="jbu-state-card is-stand"><div class="jbu-state-symbol"><span class="jbu-stand-symbol">${jbcUsbStandGlyph()}</span></div><div class="jbu-state-copy"><small>STAND</small><strong id="jbc_stand_${m.addr}">-</strong></div></div></div><div class="jbu-facts"><div class="jbu-fact"><div class="k">${t('station')}</div><div id="jbc_station_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'JBC FAE Adresse':'JBC FAE address'}</div><div id="jbc_addr_${m.addr}" class="v mono">-</div></div><div class="jbu-fact is-wide"><div class="k">${t('device_id')} (<span id="jbc_devid_len_${m.addr}">0</span>)</div><div id="jbc_devid_ascii_${m.addr}" class="v mono">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Status':'Status'}</div><div id="jbc_flags_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">Flags raw</div><div id="jbc_flags_raw_${m.addr}" class="v mono">-</div></div></div><details class="jbu-tech dev-only"><summary>${uiLang=='de'?'FAE Emulation Debug · OFE ↔ FAE ↔ JBC':'FAE emulation debug · OFE ↔ FAE ↔ JBC'}</summary><div class="jbu-tech-grid"><div><div class="k">0x38 IntakeActivation</div><strong id="jbc_dbg_output_${m.addr}">-</strong></div><div><div class="k">0x39 WORK Request Mask</div><strong id="jbc_dbg_workmask_${m.addr}">-</strong></div><div><div class="k">STAND Request Mask</div><strong id="jbc_dbg_standmask_${m.addr}">-</strong></div><div><div class="k">0x30 SuctionLevel</div><strong id="jbc_dbg_suction_${m.addr}">-</strong></div><div><div class="k">0x34 SelectFlow</div><strong id="jbc_dbg_flowset_${m.addr}">-</strong></div><div><div class="k">0x32 Flow</div><strong id="jbc_dbg_flow_${m.addr}">-</strong></div><div><div class="k">0x33 Speed</div><strong id="jbc_dbg_speed_${m.addr}">-</strong></div><div><div class="k">0x3A WORK Delay</div><strong id="jbc_dbg_workdelay_${m.addr}">-</strong></div><div><div class="k">0x3A STAND Delay</div><strong id="jbc_dbg_standdelay_${m.addr}">-</strong></div><div><div class="k">0x36 StandIntakes</div><strong id="jbc_dbg_standintakes_${m.addr}">-</strong></div><div><div class="k">0x57 ContinuousSuction</div><strong id="jbc_dbg_cont_${m.addr}">-</strong></div><div><div class="k">0x41 FilterStatus</div><strong id="jbc_dbg_filterlife_${m.addr}">-</strong></div><div><div class="k">0x45 FilterSaturation</div><strong id="jbc_dbg_filtersat_${m.addr}">-</strong></div><div><div class="k">0x59 StatError</div><strong id="jbc_dbg_error_${m.addr}">-</strong></div></div><div class="mini">TX = OFE Master → FAE · RX = im FAE gespeicherter / an JBC gelieferter Wert</div></details>${moduleMeta(m)}</div>`}if(m.type==9){det+=`<div class="tile module-card jbc-card jbc-usb-card"><div class="module-head"><div class="module-title">${mn(m)}</div><div class="module-head-main"><div class="module-identity"><span class="module-icon">${jbcUsbPortGlyph()}</span><div class="module-head-copy"><div class="module-type">JBC USB Host</div><div class="module-address">${hx(m.addr)}</div></div></div><div class="module-statuses"><span id="jbu_mod_${m.addr}" class="pill">-</span><span id="jbu_conn_${m.addr}" class="pill">-</span></div></div></div><div class="jbu-hero"><div class="jbu-hero-top"><div id="jbu_model_${m.addr}" class="jbu-station-name">-</div><div class="jbu-badges"><span id="jbu_proto_${m.addr}" class="jbu-chip proto">-</span><span id="jbu_kind_${m.addr}" class="jbu-chip kind">-</span><span id="jbu_portsbadge_${m.addr}" class="jbu-chip">-</span></div></div></div><div class="jbu-state-grid"><div id="jbu_workbox_${m.addr}" class="jbu-state-card"><div class="jbu-state-symbol"><span class="jbu-work-symbol" aria-label="work">${jbcUsbWorkGlyph()}</span><span id="jbu_workcount_${m.addr}" class="jbu-state-count" hidden></span></div><div class="jbu-state-copy"><small>WORK</small><strong id="jbu_work_${m.addr}">-</strong></div></div><div id="jbu_standbox_${m.addr}" class="jbu-state-card is-stand"><div class="jbu-state-symbol"><span class="jbu-stand-symbol">${jbcUsbStandGlyph()}</span><span id="jbu_standcount_${m.addr}" class="jbu-state-count" hidden></span></div><div class="jbu-state-copy"><small>STAND</small><strong id="jbu_stand_${m.addr}">-</strong></div></div></div><div class="jbu-facts"><div class="jbu-fact is-wide"><div class="k">JBC Device ID</div><div id="jbu_devid_${m.addr}" class="v mono">-</div></div><div class="jbu-fact is-wide"><div class="k">${uiLang=='de'?'Stationsname':'Station name'}</div><div id="jbu_stationname_${m.addr}" class="v">-</div></div><div id="jbu_qstactivate_box_${m.addr}" class="jbu-fact" hidden><div class="k">${uiLang=='de'?'QST aktiviert':'QST enabled'}</div><div id="jbu_qstactivate_${m.addr}" class="v">-</div></div><div id="jbu_qststatus_box_${m.addr}" class="jbu-fact" hidden><div class="k">QST Status</div><div id="jbu_qststatus_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">Software</div><div id="jbu_sw_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">Hardware</div><div id="jbu_hw_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Modelltyp':'Model type'}</div><div id="jbu_modeltype_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Modellversion':'Model version'}</div><div id="jbu_modelver_${m.addr}" class="v">-</div></div><div id="jbu_connectmode_box_${m.addr}" class="jbu-fact" hidden><div class="k">${uiLang=='de'?'JBC Verbindungsmodus':'JBC connection mode'}</div><div id="jbu_connectmode_${m.addr}" class="v">-</div></div><div id="jbu_stationerrbox_${m.addr}" class="jbu-fact jbu-station-error"><div class="k">${uiLang=='de'?'Stationsfehler':'Station error'}</div><div id="jbu_stationerr_${m.addr}" class="v">-</div></div></div><div class="jbu-transport">${jbcUsbGlyph()}<span>CP210x</span><strong id="jbu_cp_${m.addr}">-</strong><span>·</span><span>${uiLang=='de'?'Verbindung':'Link'}</span><strong id="jbu_link_${m.addr}">-</strong><span>· Frame</span><strong id="jbu_frame_${m.addr}">-</strong></div><div id="jbu_sold_stationdiag_${m.addr}"></div><div id="jbu_ha_stationdiag_${m.addr}"></div><div id="jbu_ph_stationdiag_${m.addr}"></div><div id="jbu_fe_stationdiag_${m.addr}"></div><div id="jbu_sf_stationdiag_${m.addr}"></div><div class="jbu-port-section"><div class="jbu-port-title"><div class="k">JBC Ports</div><small id="jbu_portcount_${m.addr}">-</small></div><div id="jbu_ports_${m.addr}" class="jbu-port-list"></div></div><details class="jbu-tech dev-only"><summary>${uiLang=='de'?'USB / Protokoll Diagnose':'USB / protocol diagnostics'}</summary><div class="jbu-tech-grid"><div><div class="k">USB RX/TX</div><strong id="jbu_usbio_${m.addr}">-</strong></div><div><div class="k">JBC RX/TX</div><strong id="jbu_jbcio_${m.addr}">-</strong></div><div><div class="k">USB Err</div><strong id="jbu_usberr_${m.addr}">-</strong></div><div><div class="k">BCC / Frame / Decode / HS</div><strong id="jbu_jbcerr_${m.addr}">-</strong></div><div><div class="k">Decode detail</div><strong id="jbu_decodedetail_${m.addr}">-</strong></div><div><div class="k">CP Baud / Line</div><strong id="jbu_cpline_${m.addr}">-</strong></div><div><div class="k">Modem</div><strong id="jbu_cpmdm_${m.addr}">-</strong></div><div><div class="k">TXQ / RXQ</div><strong id="jbu_cpq_${m.addr}">-</strong></div><div><div class="k">Hold / Comm Err</div><strong id="jbu_cphold_${m.addr}">-</strong></div><div><div class="k">Device ID raw</div><strong id="jbu_devidraw_${m.addr}">-</strong></div><div><div class="k">OFE Modul UID</div><strong>${m.uid||'-'}</strong></div></div></details>${moduleMeta(m)}</div>`}
if(m.type==2||m.type==3||(m.type!=5&&m.type!=6&&m.type!=7&&m.type!=8&&(m.caps&18432)!=0)){det+=`<div class="tile module-card fan-card core-style"><div class="module-head"><div class="module-title">${mn(m)}</div><div class="module-head-main"><div class="module-identity"><span class="module-icon">${moduleSwitchGlyph()}</span><div class="module-head-copy"><div class="module-type">${fanIoKind(m)}</div><div class="module-address">${hx(m.addr)}</div></div></div><div class="module-statuses"><span id="io_state_${m.addr}" class="pill">-</span><span id="io_role_${m.addr}" class="pill" style="display:none">-</span></div></div></div><div class="jbu-hero"><div class="jbu-hero-top"><div class="jbu-station-name">${fanIoKind(m)}</div><div class="jbu-badges"><span class="jbu-chip proto">${uiLang=='de'?'Absaugung':'Extraction'}</span><span class="jbu-chip kind">${uiLang=='de'?'I/O Steuerung':'I/O control'}</span></div></div></div><div class="jbu-facts"><div class="jbu-fact"><div class="k">${t('output_label')}</div><div id="io_main_${m.addr}" class="v big">-</div></div><div class="jbu-fact"><div class="k">RPM</div><div id="io_rpm_${m.addr}" class="v big">-</div></div><div class="jbu-fact is-wide"><div class="k">${t('fault')}</div><div id="io_fault_${m.addr}" class="v">-</div></div></div><div class="range-control"><div class="range-head"><span>${uiLang=='de'?'Leistung':'Power'}</span><strong id="io_power_v_${m.addr}">- %</strong></div><input id="io_power_${m.addr}" type="range" min="10" max="100" value="10" oninput="fanOutputSlide(${m.addr},this.value)" onchange="fanOutputSet(${m.addr},null,this.value)"></div><div class="core-control-title">${uiLang=='de'?'Ein- / Ausgänge':'Inputs / outputs'}</div><div class="control-list"><div class="control-row"><div class="control-copy"><strong>${ioAliasInput(m,'main','Relais / Fan')}</strong><small>${t('extractor_output')}</small></div><button id="io_fan_${m.addr}" class="switch off">off</button></div><div class="control-row input-row"><div class="control-copy"><strong>${ioAliasInput(m,'in1','IN1')}</strong><small>${t('input_state')}</small></div><span id="io_in1_${m.addr}" class="pill">-</span></div><div class="control-row input-row"><div class="control-copy"><strong>${ioAliasInput(m,'in2','IN2')}</strong><small>${t('input_state')}</small></div><span id="io_in2_${m.addr}" class="pill">-</span></div><div class="control-row"><div class="control-copy"><strong>${ioAliasInput(m,'out1','OUT1')}</strong><small>${t('output_label')}</small></div><button id="io_out1_${m.addr}" class="switch off">off</button></div><div class="control-row"><div class="control-copy"><strong>${ioAliasInput(m,'out2','OUT2')}</strong><small>${t('output_label')}</small></div><button id="io_out2_${m.addr}" class="switch off">off</button></div></div>${(m.caps&256)?`<div class="jbu-facts"><div class="jbu-fact"><div class="k">${t('filter')}</div><div id="io_filter_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${t('pressure')}</div><div id="io_pressure_${m.addr}" class="v">-</div></div></div><details class="pro-cal"><summary class="cal-head"><div><div class="cal-title">${t('filter_calibration')}</div><div class="cal-sub">${t('filter_calibration_hint')}</div></div><span id="io_cal_badge_${m.addr}" class="cal-badge">-</span></summary><div class="cal-body"><div class="control-row cal-sensor-row"><div class="control-copy"><strong>${t('pressure_sensor')}</strong><small>${t('enable_sensor')}</small></div><button id="io_sensor_${m.addr}" class="switch off">off</button></div><div class="cal-readings"><div class="cal-reading is-wide"><small>Status</small><strong id="io_cal_${m.addr}">-</strong></div><div class="cal-reading"><small>Sensor</small><strong id="io_cal_flags_${m.addr}">-</strong></div><div class="cal-reading"><small>Raw</small><strong id="io_raw_${m.addr}">-</strong></div></div><div class="cal-readings"><div class="cal-reading"><small>Zero</small><strong id="io_zero_${m.addr}">-</strong></div><div class="cal-reading"><small>Clean</small><strong id="io_clean_${m.addr}">-</strong></div><div class="cal-reading"><small>Full</small><strong id="io_full_v_${m.addr}">-</strong></div></div><div class="cal-steps"><button class="secondary" onclick="fanioCal(${m.addr},1)">${t('learn_zero')}</button><button class="secondary" onclick="fanioCal(${m.addr},2)">${t('learn_clean')}</button><button class="secondary" onclick="fanioCal(${m.addr},4)">Reset</button></div><details class="cal-advanced"><summary>${t('advanced_thresholds')}</summary><div class="cal-grid"><label>Warn raw<input id="io_warn_${m.addr}" type="number" min="0" max="65534" value="350"></label><label>Full raw<input id="io_full_${m.addr}" type="number" min="1" max="65535" value="500"></label></div><div class="cal-actions"><button class="secondary" onclick="fanioCal(${m.addr},3)">${t('set_warn_full')}</button></div></details></div></details>`:``}${moduleMeta(m)}</div>`}if(m.type==5){det+=`<div class="tile module-card weller-card core-style"><div class="module-head"><div class="module-title">${mn(m)}</div><div class="module-head-main"><div class="module-identity"><span class="module-icon">${moduleFanGlyph()}</span><div class="module-head-copy"><div class="module-type">Weller Zero Smog</div><div class="module-address">${hx(m.addr)}</div></div></div><div class="module-statuses"><span id="wmod_${m.addr}" class="pill">-</span><span id="wrole_${m.addr}" class="pill" style="display:none">-</span><span id="wlink_${m.addr}" class="pill">-</span></div></div></div><div class="jbu-hero"><div class="jbu-hero-top"><div class="jbu-station-name">Weller Zero Smog</div><div class="jbu-badges"><span class="jbu-chip proto">${uiLang=='de'?'Absaugung':'Extraction'}</span><span class="jbu-chip kind">${uiLang=='de'?'Lüfter + Licht':'Fan + light'}</span></div></div></div><div class="jbu-facts"><div class="jbu-fact"><div class="k">${t('speed')}</div><div class="v big"><span id="wspd_${m.addr}">-</span>%</div></div><div class="jbu-fact"><div class="k">RPM</div><div id="wrpm_${m.addr}" class="v big">-</div></div></div><div class="range-control"><div class="range-head"><span>${t('speed')}</span><strong id="wsl_v_${m.addr}">- %</strong></div><input id="wsl_${m.addr}" type="range" min="30" max="100" oninput="wellerSlide(${m.addr},this.value)" onchange="wellerSpeed(${m.addr},this.value)"></div><div class="core-control-title">${uiLang=='de'?'Ausgänge':'Outputs'}</div><div class="control-list"><div class="control-row"><div class="control-copy"><strong>${t('fan')}</strong><small>${t('extractor_output')}</small></div><button id="wfan_${m.addr}" class="switch off">off</button></div><div class="control-row"><div class="control-copy"><strong>${t('light')}</strong><small>${t('light')}</small></div><button id="wlight_${m.addr}" class="switch off">off</button></div></div><div class="jbu-facts"><div class="jbu-fact"><div class="k">${t('filter')}</div><div id="wfstat_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">SW</div><div id="wver_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${t('runtime')}</div><div id="wfrun_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${t('filter_runtime')}</div><div id="wfprog_${m.addr}" class="v">-</div></div></div><div class="filter-actions"><select id="wf_sel_${m.addr}" onchange="wellerFilter(${m.addr})"></select><button class="secondary" onclick="wellerReset(${m.addr})">${t('reset_filter')}</button></div>${moduleMeta(m)}</div>`}if((m.type==7||m.type==8)){det+=`<div class="tile module-card universal-card ${m.type==8?'modbus-card':'rs232-card'} core-style"><div class="module-head"><div class="module-title">${mn(m)}</div><div class="module-head-main"><div class="module-identity"><span class="module-icon">${moduleSerialGlyph()}</span><div class="module-head-copy"><div class="module-type">${m.type==8?'Modbus RTU':'Universal RS232'}</div><div class="module-address">${hx(m.addr)}</div></div></div><div class="module-statuses"><span id="uni_state_${m.addr}" class="pill">-</span><span id="uni_local_${m.addr}" class="pill">-</span><span id="uni_role_${m.addr}" class="pill" style="display:none">-</span><span id="uni_dirty_${m.addr}" class="uni-save-state"></span></div></div></div><div class="jbu-hero"><div class="jbu-hero-top"><div id="uni_station_head_${m.addr}" class="jbu-station-name">-</div><div class="jbu-badges">${m.type==8?`<span class="jbu-chip proto">Modbus RTU</span><span class="jbu-chip kind">${uiLang=='de'?'Register Map':'Register map'}</span>`:`<span class="jbu-chip proto">RS232</span><span class="jbu-chip kind">${uiLang=='de'?'Gerätebus':'Device bus'}</span>`}</div></div></div><div class="jbu-facts"><div class="jbu-fact is-wide"><div class="k">${uiLang=='de'?'Profil':'Profile'}</div><div id="uni_profile_${m.addr}" class="v">-</div></div><div class="jbu-fact is-wide"><div class="k">${uiLang=='de'?'Gerät':'Device'}</div><div id="uni_station_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">UART</div><div id="uni_uart_${m.addr}" class="v mono">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Protokoll':'Protocol'}</div><div id="uni_proto_${m.addr}" class="v mono">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Prüfsumme':'Checksum'}</div><div id="uni_csum_${m.addr}" class="v mono">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Zeilenende':'Line ending'}</div><div id="uni_line_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">Entities</div><div id="uni_ent_state_${m.addr}" class="v">-</div></div><div class="jbu-fact"><div class="k">${uiLang=='de'?'Alter':'Age'}</div><div id="uni_age_${m.addr}" class="v">-</div></div><div class="jbu-fact is-wide"><div class="k">${t('fault')}</div><div id="uni_fault_${m.addr}" class="v">-</div></div></div><div class="universal-entities is-main"><div class="uni-section-title"><div class="k">${uiLang=='de'?'Bedienung':'Controls'}</div><small>${m.type==8?(uiLang=='de'?'Register Map':'Register map'):'Descriptor'}</small></div><div id="uni_entities_${m.addr}" class="universal-entities-list"><div class="muted">${uiLang=='de'?'Kein Descriptor vom Modul empfangen.':'No descriptor received from module.'}</div></div></div>${m.type==8?modbusBuilderHtml(m):universalProtocolBuilderHtml(m)}<details class="universal-profile"><summary>${uiLang=='de'?'Expertenmodus - Profiltext':'Expert mode - profile text'}</summary><div class="universal-profile-body"><p class="muted">${m.type==8?(uiLang=='de'?'Für Sonderfälle kann das vom Builder erzeugte Profil hier direkt bearbeitet oder eine bestehende .ofeprofile-Datei geladen werden.':'For special cases, edit the profile generated by the builder here or load an existing .ofeprofile file.'):(uiLang=='de'?'Für Sonderfälle kann das vom Protocol Builder erzeugte Profil hier direkt bearbeitet oder eine bestehende .ofeprofile-Datei geladen werden.':'For special cases, edit the profile generated by the Protocol Builder here or load an existing .ofeprofile file.')}</p><div class="uni-direct-fields"><label>${uiLang=='de'?'Profilname':'Profile name'}<input id="uni_prof_in_${m.addr}" value=""></label><label>${uiLang=='de'?'Station / Gerät':'Station / device'}<input id="uni_stat_in_${m.addr}" value=""></label><div class="metric-line"><div><label>${uiLang=='de'?'Baudrate':'Baud rate'}<input id="uni_baud_in_${m.addr}" type="number" min="300" max="2000000" value="9600"></label></div><div><label>Frame<select id="uni_frame_in_${m.addr}"><option>8N1</option><option>8E1</option><option>8O1</option><option>7E1</option><option>7O1</option></select></label></div></div><div class="metric-line"><div><label>Protocol<input id="uni_proto_in_${m.addr}" value="ASCII"></label></div><div><label>${uiLang=='de'?'Prüfsumme':'Checksum'}<input id="uni_csum_in_${m.addr}" value="NONE"></label></div></div><label>${uiLang=='de'?'Zeilenende':'Line ending'}<select id="uni_line_in_${m.addr}"><option>CR</option><option>LF</option><option>CRLF</option><option>NONE</option></select></label></div><div class="uni-form-section"><div class="uni-form-title">${uiLang=='de'?'Profil':'Profile'}</div><label class="uni-file-label">${uiLang=='de'?'Profil-Datei':'Profile file'}<input id="uni_file_${m.addr}" type="file" accept=".ofeprofile,.txt,.json" onchange="universalLoadProfile(${m.addr},this.files&&this.files[0])"></label><label>${uiLang=='de'?'Profiltext / Entity-Regeln':'Profile text / entity rules'}<textarea id="uni_profile_text_${m.addr}" rows="7" placeholder="profile=${m.universal_profile||(m.type==8?'Generic Modbus RTU':'Generic RS232')}"></textarea></label></div><div class="uni-dual-action"><button class="secondary" onclick="universalReload(${m.addr})">${uiLang=='de'?'Status neu laden':'Reload status'}</button><button class="secondary" onclick="universalLoadFromModule(${m.addr})">${uiLang=='de'?'Aus Modul laden':'Load from module'}</button></div><div class="uni-dual-action"><button class="secondary" onclick="universalExport(${m.addr})">Export</button><button onclick="universalSave(${m.addr})">${uiLang=='de'?'Ins Modul speichern':'Save to module'}</button></div><button class="secondary danger" onclick="universalClearProfile(${m.addr})">${uiLang=='de'?'Profil-Entities löschen':'Clear profile entities'}</button><div id="uni_msg_${m.addr}" class="uni-save-state"></div></div></details><details class="universal-debug"><summary>Debug / Descriptor</summary><div class="universal-entities" id="uni_debug_entities_${m.addr}"></div><details class="uni-descriptor"><summary>Descriptor <span id="uni_desc_state_${m.addr}">-</span></summary><pre id="uni_descriptor_${m.addr}"></pre></details></details>${moduleMeta(m)}</div>`}
if(m.type==6){det+=`<div class="tile module-card display-card core-style"><div class="module-head"><div class="module-title">${mn(m)}</div><div class="module-head-main"><div class="module-identity"><span class="module-icon">${moduleMonitorGlyph()}</span><div class="module-head-copy"><div class="module-type">${m.type_name||'Display'}</div><div class="module-address">${hx(m.addr)}</div></div></div><div class="module-statuses"><span id="display_conn_${m.addr}" class="pill">-</span></div></div></div><div class="jbu-hero"><div class="jbu-hero-top"><div class="jbu-station-name">${m.type_name||'Display'}</div><div class="jbu-badges"><span class="jbu-chip proto">${uiLang=='de'?'Bedienfeld':'Control panel'}</span><span class="jbu-chip kind">${(String(m.type_name||'').match(/\d+x\d+/)||['Display'])[0]}</span></div></div></div><div class="display-settings"><div class="range-control"><div class="range-head"><span>${t('brightness')}</span><strong id="dbri_v_${m.addr}">85 %</strong></div><input id="dbri_${m.addr}" type="range" min="10" max="100" value="85" oninput="displayBrightnessInput(${m.addr},this.value)" onchange="displaySet(${m.addr})"></div><div class="metric-line"><div><label>${t('language')}</label><select id="dlang_${m.addr}" onchange="displaySet(${m.addr})"><option value="0">English</option><option value="1">Deutsch</option></select></div><div><label>${t('theme')}</label><select id="dtheme_${m.addr}" onchange="displaySet(${m.addr})"><option value="0">Dark</option><option value="1">Light</option></select></div></div><label>${t('screensaver')}</label><select id="dsaver_${m.addr}" onchange="displaySet(${m.addr})"><option value="0">${t('disabled')}</option><option value="1">1 ${t('minute')}</option><option value="2">2 ${t('minutes')}</option><option value="5">5 ${t('minutes')}</option><option value="10">10 ${t('minutes')}</option></select></div>${moduleMeta(m)}</div>`}});document.getElementById('details').innerHTML=det}
function updateDetails(d){
set('route_summary',availableRouteCount(d)+' '+t('additional_rules'));
(d.modules||[]).forEach(m=>{
let mtv=m.online&&m.telemetry_valid;set('mcpu_'+m.addr,mtv?Number(m.module_cpu_load_pct||0)+'%':'-');set('mheap_'+m.addr,mtv?kb(m.module_heap_free):'-');set('mloop_'+m.addr,mtv?Number(m.module_loop_max_ms||0)+' ms':'-');set('mtransport_'+m.addr,commTransport(m));set('muptime_'+m.addr,mtv?ups(m.module_uptime_s):'-');set('moffline_'+m.addr,Number(m.offline_events||0));set('mmiss_'+m.addr,Number(m.miss_total||0)+' / '+Number(m.misses||0));let ce=document.getElementById('mcomm_'+m.addr);if(ce){ce.textContent=commText(m);ce.className=commClass(m);ce.title=commTitle(m,d)}
if(m.type==1){pillText('jbc_mod_'+m.addr,'RS485 '+(m.online?t('online'):t('offline')),m.online?'on':'warn');let wb=document.getElementById('jbc_workbox_'+m.addr),sb=document.getElementById('jbc_standbox_'+m.addr);if(!m.online){pillText('jbc_conn_'+m.addr,'JBC '+t('off'),'warn');set('jbc_station_head_'+m.addr,'-');set('jbc_station_'+m.addr,'-');set('jbc_addr_'+m.addr,'-');set('jbc_work_'+m.addr,'-');set('jbc_stand_'+m.addr,'-');if(wb)wb.classList.remove('is-on');if(sb)sb.classList.remove('is-on');set('jbc_flags_'+m.addr,'-');set('jbc_flags_raw_'+m.addr,'-');set('jbc_devid_len_'+m.addr,0);set('jbc_devid_ascii_'+m.addr,'-')}else{let on=m.station_addr&&(m.jbc_link_flags&1),workOn=Number(m.jbc_work_mask||0)!=0,standOn=jbcFaeStandActive(m);pillText('jbc_conn_'+m.addr,'JBC '+(on?t('on'):t('off')),on?'on':'warn');let jsn=m.station_addr?(m.station_type||'JBC'):'-';set('jbc_station_head_'+m.addr,jsn);set('jbc_station_'+m.addr,jsn);set('jbc_addr_'+m.addr,hx(m.jbc_addr));set('jbc_work_'+m.addr,maskState(m.jbc_work_mask));set('jbc_stand_'+m.addr,standOn?t('on'):t('off'));if(wb)wb.classList.toggle('is-on',workOn);if(sb)sb.classList.toggle('is-on',standOn);set('jbc_flags_'+m.addr,jbcFlags(m.jbc_link_flags));set('jbc_flags_raw_'+m.addr,hx(m.jbc_link_flags));set('jbc_devid_len_'+m.addr,m.jbc_device_id_len||0);set('jbc_devid_ascii_'+m.addr,hexAscii(m.jbc_device_id))}if(m.online)jbcFaeDebugUpdate(d,m)}
if(m.type==9){let online=!!m.online,linked=online&&((Number(m.jbc_link_flags||0)&1)!=0);pillText('jbu_mod_'+m.addr,'RS485 '+(online?t('online'):t('offline')),online?'on':'warn');pillText('jbu_conn_'+m.addr,'JBC '+(linked?t('on'):t('off')),linked?'on':'warn');let proto=Number(m.jbc_usb_command_protocol||0);set('jbu_proto_'+m.addr,jbcUsbProtoName(proto));set('jbu_frame_'+m.addr,jbcUsbFrameProtoName(m.jbc_usb_frame_protocol));let kind=jbcUsbStationKind(m);set('jbu_kind_'+m.addr,jbcUsbKindLabel(kind));let shortModel=String(m.jbc_usb_model||'').trim(),raw=String(m.jbc_usb_model_raw||'');if(!shortModel||shortModel=='-')shortModel=(raw&&raw!='-'?raw.split('_')[0]:'-');set('jbu_model_'+m.addr,shortModel);set('jbu_modeltype_'+m.addr,m.jbc_usb_model_type||'-');set('jbu_modelver_'+m.addr,Number(m.jbc_usb_model_version||0)||'-');set('jbu_stationname_'+m.addr,String(m.jbc_usb_station_name||'').trim()||'-');let sdf=Number(m.jbc_usb_sold_station_diag_flags||0)&255,hap=((m.jbc_usb_ports||[])[0]||{}),haf=Number(hap.ha_value_flags||0)&65535,haCmValid=kind=='HA'&&!!(haf&4096),clf=Number(hap.cl_flags||0)&65535,clCmValid=kind=='CL'&&!!(clf&16),phf=Number(m.jbc_usb_ph_station_flags||0)>>>0,phCmValid=kind=='PH'&&!!(phf&256),fef=Number(m.jbc_usb_fe_station_flags||0)&65535,feCmValid=kind=='FE'&&!!(fef&4),sff=Number(m.jbc_usb_sf_station_flags||0)&65535,sfCmValid=kind=='SF'&&!!(sff&128),cmb=document.getElementById('jbu_connectmode_box_'+m.addr);if(cmb)cmb.hidden=!((kind=='SOLD'&&(sdf&2))||haCmValid||clCmValid||phCmValid||feCmValid||sfCmValid);if(kind=='SOLD'&&(sdf&2))set('jbu_connectmode_'+m.addr,m.jbc_usb_sold_control_mode?'CONTROL':'MONITOR');else if(haCmValid)set('jbu_connectmode_'+m.addr,(haf&8192)?'CONTROL':'MONITOR');else if(clCmValid)set('jbu_connectmode_'+m.addr,(clf&32)?'CONTROL':'MONITOR');else if(phCmValid)set('jbu_connectmode_'+m.addr,(phf&512)?'CONTROL':'MONITOR');else if(feCmValid)set('jbu_connectmode_'+m.addr,(fef&8)?'CONTROL':'MONITOR');else if(sfCmValid)set('jbu_connectmode_'+m.addr,(sff&256)?'CONTROL':'MONITOR');let qv=kind=='SOLD'?(Number(m.jbc_usb_qst_valid_flags||0)&3):0,qs=Number(m.jbc_usb_qst_state_flags||0)&3,qab=document.getElementById('jbu_qstactivate_box_'+m.addr),qsb=document.getElementById('jbu_qststatus_box_'+m.addr);if(qab)qab.hidden=!(qv&1);if(qsb)qsb.hidden=!(qv&2);if(qv&1)set('jbu_qstactivate_'+m.addr,(qs&1)?t('on'):t('off'));if(qv&2)set('jbu_qststatus_'+m.addr,(qs&2)?t('on'):t('off'));let pc=Number(m.jbc_usb_port_count||0),pcText=pc?(String(pc)+' '+(pc==1?(uiLang=='de'?'Port':'port'):(uiLang=='de'?'Ports':'ports'))+(m.jbc_usb_port_count_from_model?'':' *')):(uiLang=='de'?'wird ermittelt ...':'detecting ...');set('jbu_portcount_'+m.addr,pcText);set('jbu_portsbadge_'+m.addr,pc?String(pc)+' '+(pc==1?'Port':'Ports'):'-');let vid=Number(m.jbc_usb_cp_vid||0),pid=Number(m.jbc_usb_cp_pid||0);set('jbu_cp_'+m.addr,vid?(hx(vid)+':'+hx(pid).replace('0x','')):'-');set('jbu_sw_'+m.addr,m.jbc_usb_sw_version||'-');set('jbu_hw_'+m.addr,m.jbc_usb_hw_version||'-');let se=Number(m.jbc_usb_station_error),seKnown=Number.isFinite(se)&&se!=65535,setxt=jbcUsbStationErrorName(se);set('jbu_stationerr_'+m.addr,seKnown?(se?setxt:'OK'):'-');let seb=document.getElementById('jbu_stationerrbox_'+m.addr);if(seb)seb.classList.toggle('is-bad',seKnown&&se!=0);let workOn=Number(m.jbc_work_mask||0)!=0,standOn=Number(m.jbc_stand_mask||0)!=0;set('jbu_work_'+m.addr,workOn?t('on'):t('off'));set('jbu_stand_'+m.addr,standOn?t('on'):t('off'));let showPortCounts=pc>1,wc=document.getElementById('jbu_workcount_'+m.addr),scnt=document.getElementById('jbu_standcount_'+m.addr);if(wc){wc.hidden=!showPortCounts;wc.textContent=showPortCounts?String(jbcUsbMaskCount(m.jbc_work_mask,pc)):''}if(scnt){scnt.hidden=!showPortCounts;scnt.textContent=showPortCounts?String(jbcUsbMaskCount(m.jbc_stand_mask,pc)):''}let wb=document.getElementById('jbu_workbox_'+m.addr),sb=document.getElementById('jbu_standbox_'+m.addr);if(wb)wb.classList.toggle('is-on',workOn);if(sb)sb.classList.toggle('is-on',standOn);set('jbu_link_'+m.addr,jbcUsbLinkState(m.jbc_usb_link_state));let did=jbcUsbDeviceIdText(m);set('jbu_devid_'+m.addr,did);set('jbu_devidraw_'+m.addr,m.jbc_usb_device_id||'-');set('jbu_usbio_'+m.addr,Number(m.jbc_usb_usb_rx_bytes||0)+' / '+Number(m.jbc_usb_usb_tx_bytes||0));set('jbu_jbcio_'+m.addr,Number(m.jbc_usb_rx_frames||0)+' / '+Number(m.jbc_usb_tx_frames||0));set('jbu_usberr_'+m.addr,Number(m.jbc_usb_usb_errors||0));set('jbu_jbcerr_'+m.addr,Number(m.jbc_usb_bcc_errors||0)+' / '+Number(m.jbc_usb_frame_errors||0)+' / '+Number(m.jbc_usb_decode_errors||0)+' / '+Number(m.jbc_usb_handshake_errors||0));set('jbu_decodedetail_'+m.addr,jbcUsbDecodeDetail(m));let dl=Number(m.jbc_usb_cp_line_ctl||0).toString(16).toUpperCase().padStart(4,'0');set('jbu_cpline_'+m.addr,Number(m.jbc_usb_cp_baud||0)+' / 0x'+dl);set('jbu_cpmdm_'+m.addr,'0x'+Number(m.jbc_usb_cp_mdmsts||0).toString(16).toUpperCase().padStart(2,'0'));set('jbu_cpq_'+m.addr,Number(m.jbc_usb_cp_out_queue||0)+' / '+Number(m.jbc_usb_cp_in_queue||0));set('jbu_cphold_'+m.addr,'0x'+Number(m.jbc_usb_cp_hold_reasons||0).toString(16).toUpperCase()+' / 0x'+Number(m.jbc_usb_cp_comm_errors||0).toString(16).toUpperCase());let ssd=document.getElementById('jbu_sold_stationdiag_'+m.addr);if(ssd)ssd.innerHTML=kind=='SOLD'?jbcUsbSoldStationDiagHtml(m):'';let hsd=document.getElementById('jbu_ha_stationdiag_'+m.addr);if(hsd)hsd.innerHTML=kind=='HA'?jbcUsbHaStationDiagHtml(m):'';let psd=document.getElementById('jbu_ph_stationdiag_'+m.addr);if(psd)psd.innerHTML=kind=='PH'?jbcUsbPhStationDiagHtml(m):'';let fsd=document.getElementById('jbu_fe_stationdiag_'+m.addr);if(fsd)fsd.innerHTML=kind=='FE'?jbcUsbFeStationDiagHtml(m):'';let sfsd=document.getElementById('jbu_sf_stationdiag_'+m.addr);if(sfsd)sfsd.innerHTML=kind=='SF'?jbcUsbSfStationDiagHtml(m):'';let pe=document.getElementById('jbu_ports_'+m.addr);if(pe){let active=document.activeElement,editing=!!(active&&pe.contains(active)&&active.closest&&active.closest('.jbu-config'));if(!editing)pe.innerHTML=jbcUsbPortsHtml(m)}}
if(m.type==2||m.type==3||(m.type!=5&&m.type!=6&&m.type!=7&&m.type!=8&&(m.caps&18432)!=0)){let ior=document.getElementById('io_role_'+m.addr),ioMainOut=Number(d.active_output_addr||0)==Number(m.addr);if(ior){ior.style.display=ioMainOut?'inline-flex':'none';if(ioMainOut)pillText('io_role_'+m.addr,uiLang=='de'?'Hauptausgang':'Main output','on')}if(!m.online){pillText('io_state_'+m.addr,'RS485 '+t('offline'),'warn');pillNA('io_in1_'+m.addr);pillNA('io_in2_'+m.addr);set('io_main_'+m.addr,'-');set('io_rpm_'+m.addr,'-');set('io_fault_'+m.addr,'-');set('io_power_v_'+m.addr,'- %');if(m.caps&256){set('io_filter_'+m.addr,'-');set('io_pressure_'+m.addr,'-');set('io_cal_'+m.addr,'-');set('io_cal_flags_'+m.addr,'-');set('io_raw_'+m.addr,'-');set('io_zero_'+m.addr,'-');set('io_clean_'+m.addr,'-');set('io_full_v_'+m.addr,'-');set('io_cal_badge_'+m.addr,'-');swNA('io_sensor_'+m.addr)}swNA('io_fan_'+m.addr);swNA('io_out1_'+m.addr);swNA('io_out2_'+m.addr);let p=document.getElementById('io_power_'+m.addr);if(p)p.disabled=true}else{pillText('io_state_'+m.addr,'RS485 '+t('online'),'on');pill('io_in1_'+m.addr,bit(m.io_input_mask,0));pill('io_in2_'+m.addr,bit(m.io_input_mask,1));let rawPower=m.output_status_valid?Math.max(10,Math.round(Number(m.module_output_power||0)/10)):10,fp=fanOutputPct(m.addr,rawPower),fe=!!m.module_output_enabled;set('io_main_'+m.addr,m.output_status_valid?((fe?t('on'):t('off'))+' / '+fp+'%'):'-');set('io_rpm_'+m.addr,m.output_status_valid?(m.module_output_rpm||0):'-');let ps=document.getElementById('io_power_'+m.addr);if(ps){ps.disabled=false;val(ps,fp)}set('io_power_v_'+m.addr,fp+' %');sw('io_fan_'+m.addr,fe,()=>fanOutputSet(m.addr,!fe,fp));set('io_fault_'+m.addr,m.combined_fault_text||faultText(m.io_fault_mask||m.module_output_fault,m.type));if(m.caps&256){let ff=Number(m.fanio_filter_flags||0),fq=Number(m.fanio_filter_cal_quality||0),sen=(ff&1)!=0,sok=(ff&2)!=0,ready=(ff&16)!=0;let zero=Number(m.fanio_filter_zero_raw||0),clean=Number(m.fanio_filter_clean_raw||0),warn=Number(m.fanio_filter_warn_raw||350),full=Number(m.fanio_filter_full_raw||500),raw=Number(m.fanio_filter_pressure_raw||0);set('io_filter_'+m.addr,sen?(Math.round(Number(m.fanio_filter_saturation_permille||0)/10)+'%'):'-');set('io_pressure_'+m.addr,sen?raw:'-');set('io_raw_'+m.addr,sen?raw:'-');set('io_zero_'+m.addr,sen?zero:'-');set('io_clean_'+m.addr,sen?clean:'-');set('io_full_v_'+m.addr,sen?full:'-');let cal=sen?(ready?t('calibrated'):(fq?t('incomplete'):t('not_calibrated'))):t('sensor_off');set('io_cal_'+m.addr,cal);set('io_cal_badge_'+m.addr,cal);set('io_cal_flags_'+m.addr,sen?(sok?t('sensor_ok'):t('sensor_missing')):t('off'));sw('io_sensor_'+m.addr,sen,()=>fanioCal(m.addr,5,sen?0:1));let iw=document.getElementById('io_warn_'+m.addr),ifull=document.getElementById('io_full_'+m.addr);val(iw,warn);val(ifull,full)}let o1=bit(m.io_output_mask,0),o2=bit(m.io_output_mask,1);sw('io_out1_'+m.addr,o1,()=>ioSet(m.addr,0,o1?0:1));sw('io_out2_'+m.addr,o2,()=>ioSet(m.addr,1,o2?0:1))}}
if((m.type==7||m.type==8)){pillText('uni_state_'+m.addr,'RS485 '+(m.online?t('online'):t('offline')),m.online?'on':'warn');let localEnt=universalEntityState(m,1),localKnown=!!(localEnt&&typeof localEnt.value_bool!=='undefined'),localOk=!!(localKnown&&localEnt.value_bool);pillText('uni_local_'+m.addr,(m.type==8?'Modbus':'Gerätebus')+' '+(m.online?(localKnown?(localOk?t('online'):t('offline')):'-'):t('offline')),m.online&&localOk?'on':'warn');set('uni_fault_'+m.addr,m.online?(m.combined_fault_text||faultText((Number(m.io_fault_mask||0)|Number(m.module_output_fault||0)),m.type)):'-');let ur=document.getElementById('uni_role_'+m.addr),isMainOut=Number(d.active_output_addr||0)==Number(m.addr);if(ur){ur.style.display=isMainOut?'inline-flex':'none';if(isMainOut)pillText('uni_role_'+m.addr,uiLang=='de'?'Hauptausgang':'Main output','on')}let uc=universalStateConfig(m),up=uc.profile,us=uc.station,ub=uc.baud,uf=uc.frame,upr=uc.protocol,ucs=uc.checksum,ul=uc.line_end;set('uni_profile_'+m.addr,up);set('uni_station_head_'+m.addr,us);set('uni_station_'+m.addr,us);set('uni_uart_'+m.addr,ub+' '+uf);set('uni_proto_'+m.addr,upr);set('uni_csum_'+m.addr,ucs);set('uni_line_'+m.addr,ul);universalSetForm(m.addr,uc,false,false);set('uni_desc_state_'+m.addr,m.universal_descriptor_valid?'OK':'-');set('uni_ent_state_'+m.addr,m.universal_entities_valid?(m.universal_entities||[]).length:'-');set('uni_age_'+m.addr,(m.universal_entities_valid||m.universal_descriptor_valid)?Math.round(Math.max(Number(m.universal_entities_age_ms||0),Number(m.universal_descriptor_age_ms||0))/1000)+'s':'-');renderUniversalEntities(m);if(m.type==8)modbusBuilderSync(m)}
if(m.type==6){pillText('display_conn_'+m.addr,(m.transport==='wifi'?(uiLang==='de'?'WLAN ':'WiFi '):'RS485 ')+(m.online?t('online'):t('offline')),m.online?'on':'warn');let bri=document.getElementById('dbri_'+m.addr),lang=document.getElementById('dlang_'+m.addr),theme=document.getElementById('dtheme_'+m.addr),saver=document.getElementById('dsaver_'+m.addr),ready=m.online&&m.telemetry_valid,hold=Date.now()<(displayHoldUntil[m.addr]||0);if(bri)bri.disabled=!ready;if(lang)lang.disabled=!ready;if(theme)theme.disabled=!ready;if(saver)saver.disabled=!ready;if(ready&&!hold){let bv=Math.max(10,Math.min(100,Number(m.display_brightness_pct||85)));val(bri,bv);set('dbri_v_'+m.addr,bv+' %');val(lang,String(Number(m.display_language||0)));val(theme,String(Number(m.display_theme||0)));val(saver,String(Number(m.display_screensaver_min||0)))}}
if(m.type==5){let sl=document.getElementById('wsl_'+m.addr);pillText('wmod_'+m.addr,'RS485 '+(m.online?t('online'):t('offline')),m.online?'on':'warn');let wr=document.getElementById('wrole_'+m.addr),wMainOut=Number(d.active_output_addr||0)==Number(m.addr);if(wr){wr.style.display=wMainOut?'inline-flex':'none';if(wMainOut)pillText('wrole_'+m.addr,uiLang=='de'?'Hauptausgang':'Main output','on')}if(!m.online){pillText('wlink_'+m.addr,'Weller '+t('off'),'warn');set('wver_'+m.addr,'-');set('wspd_'+m.addr,'-');set('wsl_v_'+m.addr,'- %');set('wrpm_'+m.addr,'-');if(sl)sl.disabled=true;swNA('wfan_'+m.addr);swNA('wlight_'+m.addr);set('wfstat_'+m.addr,'-');set('wfrun_'+m.addr,'-');set('wfprog_'+m.addr,'-');let sel=document.getElementById('wf_sel_'+m.addr);if(sel){sel.innerHTML='<option>-</option>';sel.disabled=true}set('wlight_state_'+m.addr,'-')}else{if(sl)sl.disabled=false;let fs=m.weller_filter_status==1?t('very_good'):(m.weller_filter_status==10?t('change_soon'):(m.weller_filter_status==100?t('change_filter'):'-'));let fsk=m.weller_filter_status==1?'on':(m.weller_filter_status==10?'warn':(m.weller_filter_status==100?'critical':''));let ver=m.weller_version?('V'+Math.floor(m.weller_version/100)+'.'+String(m.weller_version%100).padStart(2,'0')):'-';let wlink=m.weller_uart_age_sec!=65535&&m.weller_uart_age_sec<=10;pillText('wlink_'+m.addr,'Weller '+(wlink?t('on'):t('off')),wlink?'on':'warn');let wf=bit(m.io_output_mask,0);let wl=bit(m.io_output_mask,1)||m.weller_work_light;let reported=Math.max(30,Math.min(100,m.weller_speed_percent||30));let sp=wellerUiSpeed(m.addr,reported);let pm=wellerUiFilter(m.addr,Number(m.weller_programmed_filter_minutes||0));set('wspd_'+m.addr,sp);set('wsl_v_'+m.addr,sp+' %');set('wrpm_'+m.addr,m.weller_fan_rpm||0);val(sl,sp);sw('wfan_'+m.addr,wf,()=>wellerFanToggle(m.addr,wf?0:1));sw('wlight_'+m.addr,wl,()=>ioSet(m.addr,1,wl?0:1));pillText('wfstat_'+m.addr,fs,fsk);set('wfrun_'+m.addr,dhm(m.weller_filter_runtime_minutes));set('wfprog_'+m.addr,dhm(pm));let sel=document.getElementById('wf_sel_'+m.addr);if(sel){sel.disabled=false;if(!fieldBusy(sel)){sel.innerHTML=wellerFilterOptions(pm);val(sel,String(pm),true)}}set('wlight_state_'+m.addr,wl?t('on'):t('off'));set('wver_'+m.addr,ver)}}
})}
let universalEntitySig={};
let universalWoSwitchTarget={};
function fixText(s){return String(s||'').replaceAll('\u00c3\u00bc','\u00fc').replaceAll('\u00c3\u0153','\u00dc').replaceAll('\u00c3\u00a4','\u00e4').replaceAll('\u00c3\u201e','\u00c4').replaceAll('\u00c3\u00b6','\u00f6').replaceAll('\u00c3\u2013','\u00d6').replaceAll('\u00c3\u0178','\u00df').replaceAll('\u00c2\u00b7','\u00b7')}
function universalBuiltinDebugDefs(){let lines=`11 text line_tx wo group=debug ui=debug en=Send line de=Zeile senden
13 text last_rx ro group=debug ui=debug en=Last RX de=Letzter RX
14 text last_tx ro group=debug ui=debug en=Last TX de=Letzter TX
15 text last_rx_status ro group=debug ui=debug en=RX status de=RX Status
2 sensor rx_count ro unit=frames group=debug ui=debug en=RX frames de=RX Frames
3 sensor tx_count ro unit=frames group=debug ui=debug en=TX frames de=TX Frames
7 sensor rx_ok ro unit=frames group=debug ui=debug en=RX OK de=RX OK
8 sensor rx_checksum_error ro unit=frames group=debug ui=debug en=RX checksum errors de=RX Prüfsummenfehler
9 sensor rx_pattern_error ro unit=frames group=debug ui=debug en=RX pattern errors de=RX Patternfehler
4 sensor baud ro unit=baud group=debug ui=debug en=UART baud de=UART Baud
10 text raw_tx wo group=debug ui=debug en=Send raw bytes de=Rohdaten senden
12 switch trace_enable rw group=debug ui=debug en=Local trace de=Lokaler Trace`;return universalParseDescriptorRaw(lines,false)}
function universalLines(text){return String(text||'').replaceAll(String.fromCharCode(13),'').split(String.fromCharCode(10))}
function universalNormalizeDescriptorText(text){let out=[];universalLines(text).forEach(line=>{let raw=String(line||''),trimmed=raw.trim();let m=trimmed.match(/^line_end\s*=\s*(\d+\s+\S+\s+\S+\s+\S+.*)$/);if(m){out.push('line_end=NONE');out.push(m[1]);return}if(/^line_end\s*=\s*$/.test(trimmed)){out.push('line_end=NONE');return}out.push(raw)});return out.join(String.fromCharCode(10))}
function universalParseDescriptorRaw(text,assignProfile){text=universalNormalizeDescriptorText(text);let out=[],profileOrd=0,metaRe=new RegExp('(\\w+)=([\\s\\S]*?)(?=\\s+\\w+=|$)','g');universalLines(text).forEach(line=>{line=line.trim();if(!line||line[0]=='#'||line.toLowerCase()=='entities:')return;let parts=line.split(' ').filter(Boolean);if(parts.length<4)return;let idText=parts[0].replace(/^p/i,''),id=Number(idText);if(!Number.isFinite(id))return;let e={id,type:parts[1].toLowerCase(),key:parts[2],mode:parts[3].toLowerCase(),meta:{}};let rest=line.slice(line.indexOf(parts[3])+parts[3].length).trim(),x;while((x=metaRe.exec(rest))){e.meta[x[1].toLowerCase()]=fixText(x[2].trim())}let isProfile=(e.meta.source||'').toLowerCase()=='profile'||e.id>=20||((e.meta.ui||e.meta.group||'')=='control'&&e.id>=20);if(assignProfile&&isProfile&&!e.meta.profile_index)e.meta.profile_index=e.meta.idx||String(++profileOrd);if(e.meta.idx&&!e.meta.profile_index)e.meta.profile_index=e.meta.idx;e.label=(uiLang=='de'&&(e.meta.de||'').length)?e.meta.de:(e.meta.en||e.key);out.push(e)});return out}
function universalParseDescriptor(text){let out=universalParseDescriptorRaw(text,true),ids=new Set(out.map(e=>e.id));universalBuiltinDebugDefs().forEach(e=>{if(!ids.has(e.id))out.push(e)});return out}
function universalNormalizeDef(e){
  e=e||{}; e.meta=e.meta||{};
  e.type=String(e.type||'').toLowerCase();
  let role=String(e.meta.role||'').toLowerCase().replace(/[ -]/g,'_');
  if(role){e.meta.role=role}
  let access=String(e.meta.access||e.meta.mode||'').toLowerCase();
  let mode=String(e.mode||'').toLowerCase();
  if(access.indexOf('w')>=0 && mode.indexOf('w')<0) mode=access;
  if((role=='main_output_power'||role=='output_power')){ if(!e.type||e.type=='sensor')e.type='number'; if(mode.indexOf('w')<0)mode='rw'; }
  if((role=='main_output_enable'||role=='output_enable'||role=='output')){ if(!e.type||e.type=='binary_sensor')e.type='switch'; if(mode.indexOf('w')<0)mode='rw'; }
  if(!mode && access)mode=access;
  e.mode=mode||'ro';
  if(access)e.meta.access=access;
  return e;
}
function universalEnsureProfileControls(m,defs){return defs.filter(d=>Number(d.id)<20||String(d.meta.source||'').toLowerCase()=='profile'||Number(d.id)>=20).map(universalNormalizeDef)}
function universalDefsSignature(defs){return defs.map(e=>[e.id,e.type,e.mode,e.meta&&e.meta.role,e.meta&&e.meta.access,e.meta&&e.meta.min,e.meta&&e.meta.max,e.meta&&e.meta.step].join(':')).join('|')}
function universalConfigValue(k,v){v=fixText(v).trim();if(k=='baud'){let m=v.match(/\d+/);return m?m[0]:''}if(k=='frame'){let m=v.toUpperCase().match(/[78][NEO][12]/);return m?m[0]:''}if(k=='protocol'||k=='checksum'||k=='line_end')v=(v.split(/\s+/)[0]||'').toUpperCase();if(k=='line_end'&&!['CR','LF','CRLF','NONE'].includes(v))return '';return v}
function universalDescriptorConfig(m){let cfg={};universalLines(universalNormalizeDescriptorText(m.universal_descriptor||'')).forEach(line=>{let p=line.indexOf('=');if(p>0){let k=line.slice(0,p).trim().toLowerCase(),v=universalConfigValue(k,line.slice(p+1));if(v&&['profile','station','baud','frame','protocol','checksum','line_end'].includes(k))cfg[k]=v}});return cfg}
function universalEntityState(m,id){return (m.universal_entities||[]).find(e=>Number(e.id)===Number(id))||null}
function universalList(v){return String(v||'').split('|').map(x=>fixText(x.trim())).filter(x=>x.length)}
function universalAccessMode(d){d=universalNormalizeDef(d);let access=String(d.meta&&d.meta.access||'').trim().toLowerCase(),mode=String(d.mode||'').trim().toLowerCase();return ['ro','rw','wo'].includes(access)?access:(['ro','rw','wo'].includes(mode)?mode:'')}
function universalReadable(d){let m=universalAccessMode(d);return m==='ro'||m==='rw'}
function universalWritable(d){let m=universalAccessMode(d);if(m)return m==='wo'||m==='rw';d=universalNormalizeDef(d);let role=String(d.meta&&d.meta.role||'').toLowerCase();if(d.type=='number'&&(role=='main_output_power'||role=='output_power'))return true;if(d.type=='switch'&&(role=='main_output_enable'||role=='output_enable'||role=='output'))return true;return false}
function universalNumberMeta(d){let min=Number(d.meta.min??0),max=Number(d.meta.max??100),step=Number(d.meta.step??1);if(!Number.isFinite(min))min=0;if(!Number.isFinite(max))max=100;if(!Number.isFinite(step)||step<=0)step=1;return {min,max,step}}
function universalSelectPairs(d){let opts=universalList(d.meta.options),vals=universalList(d.meta.values);if(!opts.length&&vals.length)opts=vals.slice();if(!vals.length)vals=opts.slice();return opts.map((label,i)=>({label,value:vals[i]!==undefined?vals[i]:label}))}
function universalAsciiNumber(st){let hex=String(st&&st.hex||'');if(!hex||hex.length%2)return '';let chars='';for(let i=0;i<hex.length;i+=2){let b=parseInt(hex.slice(i,i+2),16);if(!Number.isFinite(b)||b<32||b>=127)return '';chars+=String.fromCharCode(b)}chars=fixText(chars).trim();return /^[-+]?\d+(?:[.,]\d+)?$/.test(chars)?chars.replace(',','.'):''}
function universalRawValue(def,st){if(!st)return '';if(def.type=='switch'||def.type=='binary_sensor')return st.value_bool?'1':'0';if(def.type=='text'||def.type=='select'||def.type=='button')return (st.ascii&&st.ascii!='-')?fixText(st.ascii):'';if(def.type=='number'){let a=universalAsciiNumber(st);if(a!=='')return a}if(st.value!==undefined)return String(st.value);if(st.ascii&&st.ascii!='-')return fixText(st.ascii);return ''}
function universalSelectLabel(def,value){let pairs=universalSelectPairs(def),sv=String(value??'');let p=pairs.find(x=>String(x.value)===sv)||pairs.find(x=>String(x.label)===sv);return p?p.label:sv}
function universalEntityRef(d){let pi=d.meta.profile_index||d.meta.idx||d.meta.profile_entity;if(pi)return 'entity.'+pi+' / #'+d.id;return '#'+d.id}
function universalEntityValue(def,st){if(!st)return '-';let unit=def.meta.unit&&def.meta.unit!='-'?' '+def.meta.unit:'';if(def.type=='binary_sensor'||def.type=='switch')return st.value_bool?t('on'):t('off');if(def.type=='select')return universalSelectLabel(def,universalRawValue(def,st))||'-';if(def.type=='text'||def.type=='button')return st.ascii&&st.ascii!='-'?fixText(st.ascii):'-';if(def.type=='number'){let raw=universalRawValue(def,st);return raw!==''?raw+unit:(st.hex||'-')}if(st.value!==undefined)return st.value+unit;if(st.ascii&&st.ascii!='-')return fixText(st.ascii);return st.hex||'-'}
function universalEntityControls(m,d){
  let writable=universalWritable(d),addr=m.addr,id=d.id;if(!writable)return '';let type=d.type;
  if(type=='number'){let nm=universalNumberMeta(d);return `<div class="universal-slider"><input id="uni_ent_in_${addr}_${id}" type="range" min="${nm.min}" max="${nm.max}" step="${nm.step}" value="${nm.min}" oninput="universalNumberPreview(${addr},${id},this.value)" onchange="universalEntitySendValue(${addr},${id},this.value)"></div>`}
  if(type=='switch'){
    if(universalAccessMode(d)==='wo'){let k=addr+':'+id,on=!!universalWoSwitchTarget[k];return `<button id="uni_ent_sw_${addr}_${id}" class="switch uni-switch ${on?'on':'off'}" aria-label="${uiLang=='de'?'Soll-Schalter':'Target switch'}" data-wo-state="${on?'1':'0'}" onclick="universalWoSwitchToggle(${addr},${id},this)">${on?(uiLang=='de'?'Ein':'On'):(uiLang=='de'?'Aus':'Off')}</button>`;}
    return `<button id="uni_ent_sw_${addr}_${id}" class="switch uni-switch off" aria-label="toggle" onclick="universalEntitySendValue(${addr},${id},this.dataset.next||'1')">-</button>`;
  }
  if(type=='select'){let opts=universalSelectPairs(d).map(p=>`<option value="${escHtml(p.value)}">${escHtml(p.label)}</option>`).join('');return `<select id="uni_ent_sel_${addr}_${id}" class="universal-select" onchange="universalEntitySendValue(${addr},${id},this.value)">${opts}</select>`}
  if(type=='button')return `<button class="secondary universal-action" onclick="universalEntitySendValue(${addr},${id},'1')">${escHtml(d.label)}</button>`;
  let ph=id==11?(uiLang=='de'?'Text senden':'Send text'):'Text oder hex: 4E0D';return `<div class="universal-send"><input id="uni_ent_in_${addr}_${id}" placeholder="${escHtml(ph)}"><button class="secondary" onclick="universalEntitySend(${addr},${id})">${uiLang=='de'?'Senden':'Send'}</button></div>`;
}
function universalRenderList(m,defs,targetId,title,emptyText){let html=title?'<div class="k">'+title+'</div>':'',showMeta=String(targetId||'').indexOf('debug')>=0;if(!defs.length)html+='<div class="uni-empty">'+emptyText+'</div>';defs.forEach(d=>{let cls='universal-entity is-'+String(d.type||'entity').replace(/[^a-z0-9_-]/g,'_'),ctrl=universalEntityControls(m,d),ref=universalEntityRef(d),noFeedback=universalAccessMode(d)==='wo',meta=showMeta?'<small>'+escHtml(ref)+' / '+escHtml(d.type)+' / '+escHtml(d.mode)+(d.meta.role?' / role='+escHtml(d.meta.role):'')+(d.meta.access?' / access='+escHtml(d.meta.access):'')+(d.meta.group?' / '+escHtml(d.meta.group):'')+'</small>':(noFeedback?'<small>'+(uiLang=='de'?'Nur schreiben · keine Rückmeldung':'Write only · no feedback')+'</small>':'');html+='<div class="'+cls+'"><div class="uni-entity-main"><strong>'+escHtml(d.label)+'</strong>'+meta+'</div><div id="uni_ent_val_'+m.addr+'_'+d.id+'" class="v">'+(noFeedback?'—':'-')+'</div>'+(ctrl?'<div class="uni-entity-control">'+ctrl+'</div>':'')+'</div>'});let box=document.getElementById(targetId);if(box)box.innerHTML=html}
function universalNumberPreview(addr,id,value){set('uni_ent_val_'+addr+'_'+id,value);let r=document.getElementById('uni_ent_in_'+addr+'_'+id);if(r&&document.activeElement!==r)r.value=value}
function universalUpdateEntity(m,d,st){let readable=universalReadable(d);if(!readable){set('uni_ent_val_'+m.addr+'_'+d.id,'—');if(d.type=='switch'&&universalAccessMode(d)==='wo'){let k=m.addr+':'+d.id;if(st&&typeof st.value_bool!=='undefined')universalWoSwitchVisual(m.addr,d.id,!!st.value_bool);else if(Object.prototype.hasOwnProperty.call(universalWoSwitchTarget,k))universalWoSwitchVisual(m.addr,d.id,!!universalWoSwitchTarget[k]);}return}let raw=universalRawValue(d,st),txt=universalEntityValue(d,st);set('uni_ent_val_'+m.addr+'_'+d.id,txt);if(d.type=='number'){let nm=universalNumberMeta(d),shown=raw!==''?raw:String(nm.min);let r=document.getElementById('uni_ent_in_'+m.addr+'_'+d.id);if(!fieldBusy(r))val(r,shown);if(raw==='')set('uni_ent_val_'+m.addr+'_'+d.id,'-')}else if(d.type=='switch'){let b=document.getElementById('uni_ent_sw_'+m.addr+'_'+d.id),on=!!(st&&st.value_bool);if(b){b.textContent=on?t('on'):t('off');b.className='switch uni-switch '+(on?'on':'off');b.dataset.next=on?'0':'1'}}else if(d.type=='select'){let sel=document.getElementById('uni_ent_sel_'+m.addr+'_'+d.id);if(sel&&raw!==''&&!fieldBusy(sel))val(sel,raw)}}
function renderUniversalEntities(m){let box=document.getElementById('uni_entities_'+m.addr);if(!box)return;let cache=universalDescriptorCache[m.addr];if((!m.universal_descriptor||!m.universal_descriptor.length)&&cache&&Number(cache.crc)===Number(m.universal_descriptor_crc))m.universal_descriptor=cache.text;else if(m.universal_descriptor&&m.universal_descriptor.length)universalDescriptorCache[m.addr]={crc:Number(m.universal_descriptor_crc),text:m.universal_descriptor};let debug=document.getElementById('uni_debug_entities_'+m.addr),desc=document.getElementById('uni_descriptor_'+m.addr),defs=universalDefsForModule(m);let sig=[m.addr,uiLang,m.universal_descriptor_crc,m.universal_descriptor_valid,defs.length,universalDefsSignature(defs)].join('|');if(box.dataset.sig!==sig){box.dataset.sig=sig;if(desc)desc.textContent=fixText(universalNormalizeDescriptorText(m.universal_descriptor||''));if(!m.universal_descriptor_valid&&!defs.length){box.innerHTML='<div class="muted">Kein Descriptor vom Modul empfangen.</div>';if(debug)debug.innerHTML='';return}if(!defs.length){box.innerHTML='<div class="muted">Descriptor wird geladen...</div>';if(debug)debug.innerHTML='';return}let mainDefs=defs.filter(d=>((d.meta.source||'').toLowerCase()=='profile'||Number(d.id)>=20||(d.meta.ui||d.meta.group||'control')=='control')).sort((a,b)=>(a.type=='number'?-1:b.type=='number'?1:0)||Number(a.meta.profile_index||a.id)-Number(b.meta.profile_index||b.id)),debugDefs=defs.filter(d=>(d.meta.ui||d.meta.group||'')=='debug');universalRenderList(m,mainDefs,'uni_entities_'+m.addr,'Community',uiLang=='de'?'Keine Bedien-Entities im Descriptor.':'No control entities in descriptor.');universalRenderList(m,debugDefs,'uni_debug_entities_'+m.addr,'Debug',uiLang=='de'?'Keine Debug-Entities.':'No debug entities.')}defs.forEach(d=>universalUpdateEntity(m,d,universalEntityState(m,d.id)))}
function universalWoSwitchVisual(addr,id,on){let k=addr+':'+id;universalWoSwitchTarget[k]=!!on;let b=document.getElementById('uni_ent_sw_'+addr+'_'+id);if(!b)return;b.className='switch uni-switch '+(on?'on':'off');b.dataset.woState=on?'1':'0';b.textContent=on?(uiLang=='de'?'Ein':'On'):(uiLang=='de'?'Aus':'Off')}
async function universalWoSwitchToggle(addr,id,el){let k=addr+':'+id,prev=Object.prototype.hasOwnProperty.call(universalWoSwitchTarget,k)?!!universalWoSwitchTarget[k]:!!(el&&el.dataset.woState==='1'),next=!prev;universalWoSwitchVisual(addr,id,next);let body=new URLSearchParams();body.set('addr',addr);body.set('id',id);body.set('value',next?'1':'0');try{let r=await fetch('/universal/entity',{method:'POST',body,cache:'no-store'}),txt=await r.text();if(!r.ok){universalWoSwitchVisual(addr,id,prev);alert(txt||'Entity set failed');return}universalSetMsg(addr,'Entity #'+id+(uiLang=='de'?' gesendet':' sent'),'is-ok')}catch(e){universalWoSwitchVisual(addr,id,prev);alert('Entity set failed')}}
async function universalEntitySendValue(addr,id,value){let body=new URLSearchParams();body.set('addr',addr);body.set('id',id);body.set('value',value??'');let r=await fetch('/universal/entity',{method:'POST',body,cache:'no-store'});let txt=await r.text();if(!r.ok){alert(txt||'Entity set failed');return}universalSetMsg(addr,'Entity #'+id+(uiLang=='de'?' gesendet':' sent'),'is-ok');['uni_ent_in_','uni_ent_sel_'].forEach(p=>clearFieldDirty(document.getElementById(p+addr+'_'+id)));setTimeout(load,250)}
async function universalEntitySend(addr,id){let inp=document.getElementById('uni_ent_in_'+addr+'_'+id),value=inp?inp.value:'';let body=new URLSearchParams();body.set('addr',addr);body.set('id',id);if(value.trim().toLowerCase().startsWith('hex:'))body.set('hex',value.trim().slice(4));else body.set('value',value);let r=await fetch('/universal/entity',{method:'POST',body,cache:'no-store'});let txt=await r.text();if(!r.ok){alert(txt||'Entity set failed');return}universalSetMsg(addr,'Entity #'+id+' gesendet','is-ok');if(inp){inp.value='';clearFieldDirty(inp)}setTimeout(load,250)}
function universalSetMsg(addr,msg,kind=''){let e=document.getElementById('uni_msg_'+addr);if(e){e.textContent=msg||'';e.className='uni-save-state '+(kind||'')}}

let modbusBuilderSeq={};

function modbusBuilderMsg(addr,msg,kind=''){
  let e=document.getElementById('mb_msg_'+addr);
  if(!e)return;
  e.textContent=msg||'';
  e.className='mb-builder-msg '+(kind||'');
}

function modbusBuilderRows(addr){
  let box=document.getElementById('mb_rows_'+addr);
  return box?[...box.querySelectorAll('.mb-row')]:[];
}

function modbusBuilderKey(name,fallback='entity'){
  let s=String(name||'').trim().toLowerCase()
    .normalize('NFD').replace(/[\u0300-\u036f]/g,'')
    .replace(/ä/g,'ae').replace(/ö/g,'oe').replace(/ü/g,'ue').replace(/ß/g,'ss')
    .replace(/[^a-z0-9]+/g,'_').replace(/^_+|_+$/g,'');
  return s||fallback;
}

function modbusBuilderNextId(addr){
  let used=new Set(modbusBuilderRows(addr).map(r=>Number(r.querySelector('.mb-id')?.value||0)).filter(n=>n>=20&&n<=249));
  for(let id=20;id<=249;id++)if(!used.has(id))return id;
  return 20;
}

function modbusBuilderRegisterValue(raw,area){
  let s=String(raw??'').trim().replace(/\s+/g,'');
  if(!s)return {ok:false,value:0,note:'-'};
  let n;
  if(/^0x[0-9a-f]+$/i.test(s))n=parseInt(s,16);
  else if(/^\d+$/.test(s))n=parseInt(s,10);
  else return {ok:false,value:0,note:uiLang=='de'?'Ungültige Adresse':'Invalid address'};
  let original=n,ref='';
  if(area==='holding'&&n>=40001&&n<=49999){n-=40001;ref='4xxxx';}
  else if(area==='input'&&n>=30001&&n<=39999){n-=30001;ref='3xxxx';}
  else if(area==='discrete'&&n>=10001&&n<=19999){n-=10001;ref='1xxxx';}
  else if(area==='coil'&&/^0\d{4,}$/.test(s)&&n>=1&&n<=9999){n-=1;ref='0xxxx';}
  if(!Number.isFinite(n)||n<0||n>65535)return {ok:false,value:0,note:uiLang=='de'?'Adresse außerhalb 0…65535':'Address outside 0…65535'};
  return {ok:true,value:n,original:original,reference:ref,note:'0x'+n.toString(16).toUpperCase().padStart(4,'0')};
}

function modbusBuilderFunctions(area,access){
  if(area==='discrete')return {read:'read_discrete',write:'',fc:'FC02'};
  if(area==='input')return {read:'read_input',write:'',fc:'FC04'};
  if(area==='coil'){
    if(access==='rw')return {read:'read_coil',write:'write_coil',fc:'FC01 / FC05'};
    if(access==='wo')return {read:'',write:'write_coil',fc:'FC05'};
    return {read:'read_coil',write:'',fc:'FC01'};
  }
  if(access==='rw')return {read:'read_holding',write:'write_holding',fc:'FC03 / FC06'};
  if(access==='wo')return {read:'',write:'write_holding',fc:'FC06'};
  return {read:'read_holding',write:'',fc:'FC03'};
}

function modbusBuilderAutoType(area,access){
  if(area==='discrete')return 'binary_sensor';
  if(area==='coil')return access==='ro'?'binary_sensor':'switch';
  if(area==='input')return 'sensor';
  return access==='ro'?'sensor':'number';
}

function modbusBuilderAreaFromFuncs(func,readFunc){
  let s=(String(func||'')+' '+String(readFunc||'')).toLowerCase();
  if(s.includes('discrete'))return 'discrete';
  if(s.includes('coil'))return 'coil';
  if(s.includes('input'))return 'input';
  return 'holding';
}

function modbusBuilderAccessFromFuncs(func,readFunc,explicit){
  let a=String(explicit||'').toLowerCase();
  if(['ro','rw','wo'].includes(a))return a;
  let f=String(func||'').toLowerCase(),r=String(readFunc||'').toLowerCase();
  let wr=f.startsWith('write_'),rd=f.startsWith('read_')||r.startsWith('read_');
  return rd&&wr?'rw':(wr?'wo':'ro');
}

function modbusBuilderRowChanged(row,addr){
  if(!row)return;
  let area=row.querySelector('.mb-area')?.value||'holding';
  let access=row.querySelector('.mb-access')?.value||'ro';
  let accessEl=row.querySelector('.mb-access');
  if(area==='input'||area==='discrete'){
    access='ro';
    if(accessEl){accessEl.value='ro';accessEl.disabled=true;}
  }else if(accessEl)accessEl.disabled=false;

  let selected=row.querySelector('.mb-type')?.value||'auto';
  let kind=selected==='auto'?modbusBuilderAutoType(area,access):selected;
  row.dataset.kind=kind;

  row.querySelectorAll('.mb-if-number').forEach(e=>e.style.display=kind==='number'?'flex':'none');
  row.querySelectorAll('.mb-if-switch').forEach(e=>e.style.display=(kind==='switch'||kind==='binary_sensor'||kind==='button')?'flex':'none');
  row.querySelectorAll('.mb-if-select').forEach(e=>e.style.display=kind==='select'?'grid':'none');

  let reg=modbusBuilderRegisterValue(row.querySelector('.mb-reg')?.value||'',area);
  let fn=modbusBuilderFunctions(area,access),note=row.querySelector('.mb-reg-note');
  if(note){
    note.textContent=reg.ok
      ?((reg.reference?reg.reference+' → ':'')+reg.note+' · '+fn.fc+' · '+(uiLang=='de'?'0-basiert':'zero based'))
      :reg.note;
    note.className='mb-reg-note '+(reg.ok?'ok':'err');
  }
  let title=row.querySelector('.mb-row-title strong'),name=row.querySelector('.mb-name')?.value.trim();
  if(title)title.textContent=name||(uiLang=='de'?'Neues Register':'New register');
  universalDirtyBadge(addr);
  modbusBuilderOutputRefresh(addr);
}

function modbusBuilderRenumber(addr){
  let rows=modbusBuilderRows(addr),box=document.getElementById('mb_rows_'+addr);
  rows.forEach((r,i)=>{
    let n=r.querySelector('.mb-row-num');if(n)n.textContent=String(i+1);
  });
  if(box&&!rows.length)box.innerHTML='<div class="mb-empty">'+(uiLang=='de'?'Noch keine Register angelegt.':'No registers configured yet.')+'</div>';
}

function modbusBuilderRemove(btn,addr){
  let row=btn&&btn.closest('.mb-row');
  if(row)row.remove();
  modbusBuilderRenumber(addr);
  let root=document.getElementById('mb_builder_'+addr);
  if(root){root.dataset.rowsInit='1';root.dataset.builderDirty='1';}
  universalDirtyBadge(addr);
  modbusBuilderOutputRefresh(addr);
}

function modbusBuilderAddRow(addr,data={}){
  let box=document.getElementById('mb_rows_'+addr);
  if(!box)return null;
  let rows=modbusBuilderRows(addr);
  if(rows.length>=32){
    modbusBuilderMsg(addr,uiLang=='de'?'Dieses Modul unterstützt maximal 32 Profil-Entities.':'This module supports up to 32 profile entities.','warn');
    return null;
  }
  let empty=box.querySelector('.mb-empty');if(empty)empty.remove();
  let seq=(modbusBuilderSeq[addr]||0)+1;modbusBuilderSeq[addr]=seq;
  let id=Number(data.id||0);if(id<20||id>249)id=modbusBuilderNextId(addr);
  let area=data.area||'holding',access=data.access||'ro',type=data.type||'auto';
  let row=document.createElement('div');
  row.className='mb-row';
  row.dataset.mbRow=String(seq);
  row.innerHTML=`
    <div class="mb-row-head">
      <div class="mb-row-title"><span class="mb-row-num">1</span><strong>${escHtml(data.name||(uiLang=='de'?'Neues Register':'New register'))}</strong></div>
      <button type="button" class="secondary mb-remove" onclick="modbusBuilderRemove(this,${addr})">${uiLang=='de'?'Entfernen':'Remove'}</button>
    </div>
    <div class="mb-row-grid">
      <label>${uiLang=='de'?'Name':'Name'}<input class="mb-name" value="${escHtml(data.name||'')}" placeholder="${uiLang=='de'?'z. B. Filterdruck':'e.g. Filter pressure'}"></label>
      <label>${uiLang=='de'?'Bereich':'Area'}<select class="mb-area">
        <option value="holding"${area==='holding'?' selected':''}>Holding 4xxxx</option>
        <option value="input"${area==='input'?' selected':''}>Input 3xxxx</option>
        <option value="coil"${area==='coil'?' selected':''}>Coil 0xxxx</option>
        <option value="discrete"${area==='discrete'?' selected':''}>Discrete 1xxxx</option>
      </select></label>
      <label>${uiLang=='de'?'Adresse':'Address'}<input class="mb-reg" inputmode="text" value="${escHtml(data.reg??'')}" placeholder="0x0012 / 40019"></label>
      <label>${uiLang=='de'?'Zugriff':'Access'}<select class="mb-access">
        <option value="ro"${access==='ro'?' selected':''}>${uiLang=='de'?'Lesen':'Read'}</option>
        <option value="rw"${access==='rw'?' selected':''}>${uiLang=='de'?'Lesen + Schreiben':'Read + write'}</option>
        <option value="wo"${access==='wo'?' selected':''}>${uiLang=='de'?'Nur Schreiben':'Write only'}</option>
      </select></label>
      <label>${uiLang=='de'?'Darstellung':'Display'}<select class="mb-type">
        <option value="auto"${type==='auto'?' selected':''}>Auto</option>
        <option value="sensor"${type==='sensor'?' selected':''}>${uiLang=='de'?'Messwert':'Sensor'}</option>
        <option value="number"${type==='number'?' selected':''}>${uiLang=='de'?'Zahl / Regler':'Number / slider'}</option>
        <option value="binary_sensor"${type==='binary_sensor'?' selected':''}>${uiLang=='de'?'Status':'Binary status'}</option>
        <option value="switch"${type==='switch'?' selected':''}>${uiLang=='de'?'Schalter':'Switch'}</option>
        <option value="select"${type==='select'?' selected':''}>Select</option>
        <option value="text"${type==='text'?' selected':''}>${uiLang=='de'?'Text / Status':'Text / status'}</option>
        <option value="button"${type==='button'?' selected':''}>Button</option>
      </select></label>
      <label>${uiLang=='de'?'Einheit':'Unit'}<input class="mb-unit" value="${escHtml(data.unit||'')}" placeholder="%, rpm, Pa"></label>
      <label>${uiLang=='de'?'Polling ms':'Polling ms'}<input class="mb-poll" type="number" min="100" max="60000" step="100" value="${escHtml(data.poll_ms||'')}"></label>
    </div>
    <div class="mb-reg-note">-</div>
    <details class="mb-advanced">
      <summary>${uiLang=='de'?'Erweitert':'Advanced'}</summary>
      <div class="mb-advanced-grid">
        <label>${uiLang=='de'?'Interner Schlüssel':'Internal key'}<input class="mb-key" value="${escHtml(data.key||'')}" placeholder="filter_pressure"></label>
        <label>Entity ID<input class="mb-id" type="number" min="20" max="249" value="${id}"></label>
        <label>${uiLang=='de'?'Rolle':'Role'}<select class="mb-role">
          <option value="">-</option>
          <option value="main_input"${data.role==='main_input'?' selected':''}>Main input</option>
          <option value="main_output_enable"${data.role==='main_output_enable'?' selected':''}>Main output enable</option>
          <option value="main_output_power"${data.role==='main_output_power'?' selected':''}>Main output power</option>
          <option value="main_output_rpm"${data.role==='main_output_rpm'?' selected':''}>Main output RPM</option>
          <option value="input"${data.role==='input'?' selected':''}>Input</option>
          <option value="output_enable"${data.role==='output_enable'?' selected':''}>Output enable</option>
          <option value="output_power"${data.role==='output_power'?' selected':''}>Output power</option>
        </select></label>
        <label>${uiLang=='de'?'Slave Override':'Slave override'}<input class="mb-slave-override" type="number" min="1" max="247" value="${escHtml(data.slave||'')}" placeholder="-"></label>
        <label>${uiLang=='de'?'Faktor':'Scale'}<input class="mb-scale" type="number" step="1" value="${escHtml(data.scale??1)}"></label>
        <label>${uiLang=='de'?'Teiler':'Divisor'}<input class="mb-divisor" type="number" min="1" step="1" value="${escHtml(data.divisor??1)}"></label>
        <label>${uiLang=='de'?'Offset':'Offset'}<input class="mb-offset" type="number" step="1" value="${escHtml(data.offset??0)}"></label>
        <label>${uiLang=='de'?'Bitmaske':'Bit mask'}<input class="mb-bitmask" inputmode="text" value="${escHtml(data.bitmask||'')}" placeholder="0x00F0 / 0b1111 / 240"></label>
        <label>${uiLang=='de'?'Bit-Shift rechts':'Right bit shift'}<input class="mb-bit-shift" type="number" min="0" max="31" step="1" value="${escHtml(data.bit_shift??0)}"></label>
        <label>${uiLang=='de'?'Zeitbasis nach Umrechnung':'Time base after transform'}<select class="mb-time-base">${builderTimeBaseOptions(data.time_base||'none')}</select></label>
        <label>${uiLang=='de'?'Zeit-Ausgabe':'Time output'}<select class="mb-time-display">${builderTimeDisplayOptions(data.time_display||'raw')}</select></label>
        <label>${uiLang=='de'?'Wert → Text':'Value → text'}<select class="mb-map-mode">${builderMapModeOptions(data.map_mode||'none')}</select></label>
        <label style="grid-column:span 2">${uiLang=='de'?'Mapping (Wert=Text, mit | trennen)':'Mapping (value=text, separate with |)'}<input class="mb-value-map" value="${escHtml(data.value_map||data.map||'')}" placeholder="0=Bereit|1=Warnung|0x04=Filter"></label>
        <label>${uiLang=='de'?'Fallback-Text':'Fallback text'}<input class="mb-map-default" value="${escHtml(data.map_default||'')}" placeholder="Unbekannt"></label>
        <label class="mb-if-number">Min<input class="mb-min" type="number" value="${escHtml(data.min??0)}"></label>
        <label class="mb-if-number">Max<input class="mb-max" type="number" value="${escHtml(data.max??100)}"></label>
        <label class="mb-if-number">Step<input class="mb-step" type="number" min="1" value="${escHtml(data.step??1)}"></label>
        <label class="mb-if-switch">${uiLang=='de'?'Wert AN':'Value ON'}<input class="mb-on" type="number" value="${escHtml(data.value_on??1)}"></label>
        <label class="mb-if-switch">${uiLang=='de'?'Wert AUS':'Value OFF'}<input class="mb-off" type="number" value="${escHtml(data.value_off??0)}"></label>
      </div>
      <div class="mb-select-grid mb-if-select">
        <label>${uiLang=='de'?'Anzeigenamen, mit | getrennt':'Labels, separated by |'}<input class="mb-options" value="${escHtml(data.options||'')}" placeholder="Aus|Auto|Manuell"></label>
        <label>${uiLang=='de'?'Registerwerte, mit | getrennt':'Register values, separated by |'}<input class="mb-values" value="${escHtml(data.values||'')}" placeholder="0|1|2"></label>
      </div>
    </details>`;
  box.appendChild(row);
  let root=document.getElementById('mb_builder_'+addr);if(root&&!data._silent)root.dataset.builderDirty='1';
  row.addEventListener('input',()=>universalDirtyBadge(addr),true);
  row.addEventListener('change',()=>universalDirtyBadge(addr),true);

  ['.mb-area','.mb-access','.mb-type','.mb-reg'].forEach(sel=>{
    let el=row.querySelector(sel);
    if(el)el.addEventListener(sel==='.mb-reg'?'input':'change',()=>modbusBuilderRowChanged(row,addr));
  });
  let name=row.querySelector('.mb-name');if(name)name.addEventListener('input',()=>modbusBuilderRowChanged(row,addr));
  modbusBuilderRowChanged(row,addr);
  modbusBuilderRenumber(addr);
  return row;
}

function modbusBuilderDescriptorHeader(text,key){
  let re=new RegExp('^'+key.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')+'\\s*=\\s*(.+)$','im');
  let m=String(text||'').match(re);
  return m?m[1].trim():'';
}

function modbusBuilderSync(m){
  if(!m||Number(m.type)!==8)return;
  let addr=Number(m.addr),root=document.getElementById('mb_builder_'+addr);
  if(!root)return;
  let poll=modbusBuilderDescriptorHeader(m.universal_descriptor||'','poll_ms')||'500';
  if(!root.dataset.headerInit){
    let c=universalStateConfig(m),slave=modbusBuilderDescriptorHeader(m.universal_descriptor||'','slave')||'1';
    val('mb_profile_'+addr,c.profile||'Generic Modbus RTU',true);
    val('mb_station_'+addr,c.station||'Modbus device',true);
    val('mb_baud_'+addr,c.baud||'9600',true);
    val('mb_frame_'+addr,c.frame||'8N1',true);
    val('mb_slave_'+addr,slave,true);
    val('mb_poll_'+addr,poll,true);
    root.dataset.headerInit='1';
  }
  if(!root.dataset.rowsInit){
    let defs=universalDefsForModule(m).filter(d=>Number(d.id)>=20||String(d.meta.source||'').toLowerCase()==='profile');
    if(defs.length){
      let box=document.getElementById('mb_rows_'+addr);if(box)box.innerHTML='';
      defs.sort((a,b)=>Number(a.meta.profile_index||a.meta.idx||a.id)-Number(b.meta.profile_index||b.meta.idx||b.id)).slice(0,32).forEach(d=>{
        let func=d.meta.func||'',readFunc=d.meta.read_func||'',area=modbusBuilderAreaFromFuncs(func,readFunc);
        modbusBuilderAddRow(addr,{
          _silent:true,id:d.id,name:d.label,key:d.key,area:area,access:modbusBuilderAccessFromFuncs(func,readFunc,d.mode||d.meta.access),
          type:d.type||'auto',reg:d.meta.reg||'',slave:d.meta.slave||'',unit:d.meta.unit||'',role:d.meta.role||'',
          scale:d.meta.scale||d.meta.sc||1,divisor:d.meta.divisor||d.meta.div||1,offset:d.meta.offset||d.meta.off||0,bitmask:d.meta.bitmask||d.meta.mask||'',bit_shift:d.meta.bit_shift||d.meta.shift||0,time_base:d.meta.time_base||d.meta.tb||'none',time_display:d.meta.time_display||d.meta.tf||'raw',map_mode:d.meta.map_mode||'none',value_map:'',map_default:'',
          min:d.meta.min??0,max:d.meta.max??100,step:d.meta.step??1,value_on:d.meta.value_on??1,value_off:d.meta.value_off??0,
          options:d.meta.options||'',values:d.meta.values||'',poll_ms:d.meta.poll_ms||d.meta.poll||poll
        });
      });
      root.dataset.rowsInit='1';delete root.dataset.builderDirty;
      modbusBuilderOutputLoad(addr);
      clearDirtyIn(root);
      universalDirtyBadge(addr);
      modbusBuilderMsg(addr,uiLang=='de'?'Register und Polling-Zeiten aus dem Modul-Descriptor \u00fcbernommen.':'Registers and polling times loaded from the module descriptor.','ok');
    }
  }
}

function modbusBuilderMap(text){
  let map={};
  universalLines(String(text||'')).forEach(line=>{
    line=line.trim();
    if(!line||line.startsWith('#')||line.startsWith(';'))return;
    let p=line.indexOf('=');
    if(p<0)return;
    let k=line.slice(0,p).trim().toLowerCase(),v=fixText(line.slice(p+1).trim());
    map[k]=v;
  });
  return map;
}

function modbusBuilderFromProfile(addr,text){
  let map=modbusBuilderMap(text),root=document.getElementById('mb_builder_'+addr),box=document.getElementById('mb_rows_'+addr);
  if(!root||!box)return false;
  val('mb_profile_'+addr,map.profile||map.name||'Generic Modbus RTU',true);
  val('mb_station_'+addr,map.station||map.device||'Modbus device',true);
  val('mb_baud_'+addr,map.baud||map.uart_baud||'9600',true);
  val('mb_frame_'+addr,String(map.frame||map.uart_frame||'8N1').toUpperCase(),true);
  val('mb_slave_'+addr,map.slave||map.slave_id||'1',true);
  val('mb_poll_'+addr,map.poll_ms||'500',true);

  box.innerHTML='';
  modbusBuilderSeq[addr]=0;
  let added=0;
  for(let i=1;i<=32;i++){
    let p='entity.'+i+'.',name=map[p+'name']||'',type=map[p+'type']||'',func=map[p+'func']||'',readFunc=map[p+'read_func']||'',reg=map[p+'reg']||map[p+'register']||'';
    if(!name&&!type&&!func&&!readFunc&&!reg)continue;
    let area=modbusBuilderAreaFromFuncs(func,readFunc),access=modbusBuilderAccessFromFuncs(func,readFunc,map[p+'access']||map[p+'mode']);
    modbusBuilderAddRow(addr,{
      _silent:true,id:map[p+'id']||'',name:name||map[p+'key']||('Register '+i),key:map[p+'key']||'',area:area,access:access,type:type||'auto',
      reg:reg,slave:map[p+'slave']||'',unit:map[p+'unit']||'',role:map[p+'role']||'',poll_ms:map[p+'poll_ms']||'',
      scale:map[p+'scale']||map[p+'multiplier']||1,divisor:map[p+'divisor']||map[p+'divider']||1,offset:map[p+'offset']||0,bitmask:map[p+'bitmask']||map[p+'mask']||'',bit_shift:map[p+'bit_shift']||map[p+'shift']||0,time_base:map[p+'time_base']||map[p+'time_unit']||'none',time_display:map[p+'time_display']||map[p+'time_format']||'raw',map_mode:map[p+'map_mode']||map[p+'mapping_mode']||(map[p+'map']||map[p+'value_map']?'exact':'none'),value_map:map[p+'map']||map[p+'value_map']||'',map_default:map[p+'map_default']||map[p+'default_text']||'',
      min:map[p+'min']??0,max:map[p+'max']??100,step:map[p+'step']??1,value_on:map[p+'value_on']??1,value_off:map[p+'value_off']??0,
      options:map[p+'options']||'',values:map[p+'values']||''
    });
    added++;
  }
  root.dataset.headerInit='1';
  root.dataset.rowsInit='1';delete root.dataset.builderDirty;
  modbusBuilderRenumber(addr);
  modbusBuilderOutputLoad(addr);
  clearDirtyIn(root);
  universalDirtyBadge(addr);
  modbusBuilderMsg(addr,(uiLang=='de'?'Profil geladen: ':'Profile loaded: ')+added+' '+(uiLang=='de'?'Register.':'registers.'),'ok');
  return true;
}

async function modbusBuilderLoadModule(addr){
  try{
    modbusBuilderMsg(addr,uiLang=='de'?'Lese vollständige Register Map aus dem Modul…':'Reading full register map from module…');
    let r=await fetch('/universal/profile/read?addr='+addr,{cache:'no-store'});
    let text=fixText(await r.text());
    if(!r.ok)throw new Error(text||('HTTP '+r.status));
    let raw=document.getElementById('uni_profile_text_'+addr);
    if(raw){raw.value=text;clearFieldDirty(raw);}
    modbusBuilderFromProfile(addr,text);
    let map=modbusBuilderMap(text);
    universalSetForm(addr,{profile:map.profile,station:map.station,baud:map.baud,frame:map.frame,protocol:'MODBUS_RTU',checksum:'CRC16_MODBUS_LE',line_end:'NONE'},true,false);
    let trunc=r.headers.get('X-OFE-Truncated')==='1';
    if(trunc)modbusBuilderMsg(addr,uiLang=='de'?'Profil wurde vom Master gekürzt; bitte Expertenprofil prüfen.':'Profile was truncated by the Master; check expert profile.','warn');
  }catch(e){
    modbusBuilderMsg(addr,String(e&&e.message?e.message:e),'err');
  }
}

function modbusBuilderLineValue(v){
  return fixText(String(v??'')).replace(/[\r\n]+/g,' ').trim();
}

function modbusBuilderReadRow(row,index,addr,usedIds,usedKeys){
  let area=row.querySelector('.mb-area')?.value||'holding',access=row.querySelector('.mb-access')?.value||'ro';
  if(area==='input'||area==='discrete')access='ro';
  let reg=modbusBuilderRegisterValue(row.querySelector('.mb-reg')?.value||'',area);
  if(!reg.ok)throw new Error((uiLang=='de'?'Register ':'Register ')+index+': '+reg.note);
  let name=modbusBuilderLineValue(row.querySelector('.mb-name')?.value||'');
  if(!name)throw new Error((uiLang=='de'?'Register ':'Register ')+index+': '+(uiLang=='de'?'Name fehlt':'name is required'));
  let id=Number(row.querySelector('.mb-id')?.value||0);
  if(id<20||id>249)throw new Error(name+': Entity ID 20…249');
  if(usedIds.has(id))throw new Error(name+': '+(uiLang=='de'?'Entity ID doppelt':'duplicate Entity ID')+' '+id);
  usedIds.add(id);

  let key=modbusBuilderKey(row.querySelector('.mb-key')?.value||name,'entity'+index);
  if(usedKeys.has(key))key=key+'_'+index;
  usedKeys.add(key);

  let selected=row.querySelector('.mb-type')?.value||'auto';
  let type=selected==='auto'?modbusBuilderAutoType(area,access):selected;
  let fn=modbusBuilderFunctions(area,access);
  if(type==='button'&&access==='ro')throw new Error(name+': '+(uiLang=='de'?'Button muss schreibbar sein':'button must be writable'));

  let slave=String(row.querySelector('.mb-slave-override')?.value||'').trim();
  if(slave){
    let sn=Number(slave);if(!Number.isInteger(sn)||sn<1||sn>247)throw new Error(name+': Slave 1…247');
  }
  let poll=String(row.querySelector('.mb-poll')?.value||'').trim();
  if(poll){
    let pn=Number(poll);if(!Number.isFinite(pn)||pn<100||pn>60000)throw new Error(name+': Polling 100…60000 ms');
    poll=String(Math.round(pn));
  }

  let role=row.querySelector('.mb-role')?.value||'',unit=modbusBuilderLineValue(row.querySelector('.mb-unit')?.value||'');
  let data={index,name,key,id,area,access,type,reg:reg.value,fn,slave,poll,role,unit};
  data.scale=Number(row.querySelector('.mb-scale')?.value||1);
  data.divisor=Number(row.querySelector('.mb-divisor')?.value||1);
  data.offset=Number(row.querySelector('.mb-offset')?.value||0);
  data.bitmask=String(row.querySelector('.mb-bitmask')?.value||'').trim();
  data.bit_shift=Number(row.querySelector('.mb-bit-shift')?.value||0);
  data.time_base=String(row.querySelector('.mb-time-base')?.value||'none').toLowerCase();
  data.time_display=String(row.querySelector('.mb-time-display')?.value||'raw').toLowerCase();
  data.map_mode=String(row.querySelector('.mb-map-mode')?.value||'none').toLowerCase();
  data.value_map=modbusBuilderLineValue(row.querySelector('.mb-value-map')?.value||'');
  data.map_default=modbusBuilderLineValue(row.querySelector('.mb-map-default')?.value||'');
  if(data.value_map&&data.map_mode==='none')data.map_mode='exact';
  if(!Number.isInteger(data.scale)||data.scale===0)throw new Error(name+': '+(uiLang=='de'?'Faktor muss ganzzahlig und ungleich 0 sein':'scale must be a non-zero integer'));
  if(!Number.isInteger(data.divisor)||data.divisor<1)throw new Error(name+': '+(uiLang=='de'?'Teiler muss >= 1 sein':'divisor must be >= 1'));
  if(!Number.isInteger(data.offset))throw new Error(name+': Offset');
  if(data.bitmask&&!builderMaskValid(data.bitmask))throw new Error(name+': '+(uiLang=='de'?'Bitmaske ungültig':'invalid bit mask'));
  if(!Number.isInteger(data.bit_shift)||data.bit_shift<0||data.bit_shift>31)throw new Error(name+': Bit-Shift 0…31');
  if((data.bitmask||data.bit_shift)&&access!=='ro')throw new Error(name+': '+(uiLang=='de'?'Bitmaske/Shift sind bei Modbus nur für Lese-Entities erlaubt (kein Read-Modify-Write)':'Modbus bit mask/shift transforms are read-only (no read-modify-write)'));
  if(data.value_map&&!builderValueMapValid(data.value_map))throw new Error(name+': '+(uiLang=='de'?'Text-Mapping ungültig':'invalid text mapping'));
  if(new TextEncoder().encode(data.value_map).length>191)throw new Error(name+': '+(uiLang=='de'?'Text-Mapping maximal 191 Byte':'text mapping max 191 bytes'));
  if(new TextEncoder().encode(data.map_default).length>31)throw new Error(name+': '+(uiLang=='de'?'Fallback-Text maximal 31 Byte':'fallback text max 31 bytes'));
  if(!['none','exact','flags'].includes(data.map_mode))throw new Error(name+': Mapping mode');
  if(data.map_mode!=='none'&&access!=='ro')throw new Error(name+': '+(uiLang=='de'?'Text-Mapping ist für Lese-Entities vorgesehen':'text mapping is intended for read-only entities'));
  if(data.time_display==='dhm'&&data.time_base==='none')throw new Error(name+': '+(uiLang=='de'?'Für d/h/m muss eine Zeitbasis gewählt werden':'d/h/m requires a time base'));
  if(data.time_display==='dhm'&&access!=='ro'&&type==='number')throw new Error(name+': '+(uiLang=='de'?'d/h/m ist für schreibbare Zahlen nicht zulässig':'d/h/m is not supported for writable numbers'));
  data.min=Number(row.querySelector('.mb-min')?.value||0);
  data.max=Number(row.querySelector('.mb-max')?.value||100);
  data.step=Number(row.querySelector('.mb-step')?.value||1);
  data.on=Number(row.querySelector('.mb-on')?.value||1);
  data.off=Number(row.querySelector('.mb-off')?.value||0);
  data.options=modbusBuilderLineValue(row.querySelector('.mb-options')?.value||'');
  data.values=modbusBuilderLineValue(row.querySelector('.mb-values')?.value||'');
  if(type==='number'){
    if(!Number.isFinite(data.min)||!Number.isFinite(data.max)||data.min>data.max)throw new Error(name+': Min/Max');
    if(!Number.isFinite(data.step)||data.step<=0)throw new Error(name+': Step > 0');
  }
  if(type==='select'){
    let o=data.options.split('|').filter(Boolean),v=data.values.split('|').filter(Boolean);
    if(!o.length||!v.length||o.length!==v.length)throw new Error(name+': '+(uiLang=='de'?'Select benötigt gleich viele Anzeigenamen und Werte':'select needs equal label/value counts'));
  }
  return data;
}

function modbusBuilderGenerate(addr,quiet=false){
  try{
    let rows=modbusBuilderRows(addr);
    if(!rows.length)throw new Error(uiLang=='de'?'Bitte mindestens ein Register hinzufügen.':'Add at least one register.');
    if(rows.length>32)throw new Error(uiLang=='de'?'Maximal 32 Register.':'Maximum 32 registers.');

    let profile=modbusBuilderLineValue(document.getElementById('mb_profile_'+addr)?.value||'')||'Generic Modbus RTU';
    let station=modbusBuilderLineValue(document.getElementById('mb_station_'+addr)?.value||'')||'Modbus device';
    let baud=Number(document.getElementById('mb_baud_'+addr)?.value||9600);
    let frame=String(document.getElementById('mb_frame_'+addr)?.value||'8N1').toUpperCase();
    let slave=Number(document.getElementById('mb_slave_'+addr)?.value||1);
    let defaultPoll=Number(document.getElementById('mb_poll_'+addr)?.value||500);
    if(!Number.isFinite(baud)||baud<300||baud>1000000)throw new Error('Baud 300…1000000');
    if(!['8N1','8E1','8O1','7E1'].includes(frame))throw new Error('Frame 8N1 / 8E1 / 8O1 / 7E1');
    if(!Number.isInteger(slave)||slave<1||slave>247)throw new Error('Slave ID 1…247');
    if(!Number.isFinite(defaultPoll)||defaultPoll<100||defaultPoll>60000)throw new Error('Polling 100…60000 ms');
    if(document.getElementById('mb_output_use_'+addr)?.checked){
      let pv=document.getElementById('mb_output_power_'+addr)?.value||'',ev=document.getElementById('mb_output_enable_'+addr)?.value||'';
      if(pv===''&&ev==='')throw new Error(uiLang=='de'?'Hauptausgang aktiv, aber keine Steuerung zugeordnet.':'Main output enabled but no control is assigned.');
      if(ev==='__power__'&&pv==='')throw new Error(uiLang=='de'?'Für „0 = AUS“ muss ein Leistungsregister gewählt sein.':'Power-derived OFF requires a power register.');
    }

    let usedIds=new Set(),usedKeys=new Set(),defs=rows.map((r,i)=>modbusBuilderReadRow(r,i+1,addr,usedIds,usedKeys));
    defs=modbusBuilderApplyOutputRoles(addr,defs);
    let lines=[
      '# Open Fume Extractor Modbus RTU profile v1',
      '# Generated by Modbus Register Map Builder',
      'profile='+profile,
      'station='+station,
      'baud='+Math.round(baud),
      'frame='+frame,
      'protocol=MODBUS_RTU',
      'checksum=CRC16_MODBUS_LE',
      'slave='+slave,
      'poll_ms='+Math.round(defaultPoll),
      ''
    ];

    defs.forEach((d,i)=>{
      let p='entity.'+(i+1)+'.';
      lines.push(p+'id='+d.id);
      lines.push(p+'name='+d.name);
      lines.push(p+'key='+d.key);
      lines.push(p+'type='+d.type);
      lines.push(p+'access='+d.access);
      if(d.fn.write)lines.push(p+'func='+d.fn.write);
      else if(d.fn.read)lines.push(p+'func='+d.fn.read);
      if(d.fn.write&&d.fn.read)lines.push(p+'read_func='+d.fn.read);
      lines.push(p+'reg=0x'+d.reg.toString(16).toUpperCase().padStart(4,'0'));
      if(d.slave)lines.push(p+'slave='+d.slave);
      if(d.role)lines.push(p+'role='+d.role);
      if(d.unit)lines.push(p+'unit='+d.unit);
      if(d.poll)lines.push(p+'poll_ms='+d.poll);
      if(d.scale!==1)lines.push(p+'scale='+Math.trunc(d.scale));
      if(d.divisor!==1)lines.push(p+'divisor='+Math.trunc(d.divisor));
      if(d.offset!==0)lines.push(p+'offset='+Math.trunc(d.offset));
      if(d.bitmask&&!/^0(?:x0+|b0+)?$/i.test(d.bitmask))lines.push(p+'bitmask='+d.bitmask);
      if(d.bit_shift>0)lines.push(p+'bit_shift='+Math.trunc(d.bit_shift));
      if(d.time_base!=='none'){lines.push(p+'time_base='+d.time_base);if(d.time_display!=='raw')lines.push(p+'time_display='+d.time_display)}
      if(d.map_mode!=='none'){lines.push(p+'map_mode='+d.map_mode);if(d.value_map)lines.push(p+'map='+d.value_map);if(d.map_default)lines.push(p+'map_default='+d.map_default)}
      if(d.type==='number'){
        lines.push(p+'min='+Math.trunc(d.min));
        lines.push(p+'max='+Math.trunc(d.max));
        lines.push(p+'step='+Math.trunc(d.step));
      }
      if(d.type==='switch'||d.type==='binary_sensor'||d.type==='button'){
        lines.push(p+'value_on='+Math.trunc(d.on));
        lines.push(p+'value_off='+Math.trunc(d.off));
      }
      if(d.type==='select'){
        lines.push(p+'options='+d.options);
        lines.push(p+'values='+d.values);
      }
      lines.push('');
    });

    let text=lines.join('\n').trim()+'\n';
    if(new TextEncoder().encode(text).length>8192)throw new Error((uiLang=='de'?'Erzeugtes Profil ist zu groß: ':'Generated profile is too large: ')+new TextEncoder().encode(text).length+' / 8192 B');

    let raw=document.getElementById('uni_profile_text_'+addr);
    if(raw){raw.value=text;markFieldDirty(raw);}
    universalSetForm(addr,{profile,station,baud:String(Math.round(baud)),frame,protocol:'MODBUS_RTU',checksum:'CRC16_MODBUS_LE',line_end:'NONE'},true,true);
    if(!quiet)modbusBuilderMsg(addr,(uiLang=='de'?'Profil erzeugt: ':'Profile generated: ')+defs.length+' '+(uiLang=='de'?'Register · noch nicht gespeichert.':'registers · not saved yet.'),'ok');
    return text;
  }catch(e){
    modbusBuilderMsg(addr,String(e&&e.message?e.message:e),'err');
    return '';
  }
}

async function modbusBuilderSave(addr){
  let text=modbusBuilderGenerate(addr,true);
  if(!text)return;
  modbusBuilderMsg(addr,uiLang=='de'?'Speichere Register Map ins Modul…':'Saving register map to module…');
  let ok=await universalSave(addr);
  if(ok){
    let root=document.getElementById('mb_builder_'+addr);if(root){clearDirtyIn(root);delete root.dataset.builderDirty;}
    universalDirtyBadge(addr);
    modbusBuilderMsg(addr,uiLang=='de'?'Register Map gespeichert. Descriptor, Bedienung und Trace werden automatisch daraus aktualisiert.':'Register map saved. Descriptor, controls and trace will update automatically.','ok');
  }
}

function modbusBuilderExport(addr){
  let text=modbusBuilderGenerate(addr,true);
  if(!text)return;
  let name=modbusBuilderKey(document.getElementById('mb_profile_'+addr)?.value||'modbus_rtu','modbus_rtu');
  let blob=new Blob([text],{type:'text/plain;charset=utf-8'}),a=document.createElement('a'),url=URL.createObjectURL(blob);
  a.href=url;a.download=name+'.ofeprofile';document.body.appendChild(a);a.click();
  setTimeout(()=>{URL.revokeObjectURL(url);a.remove()},250);
  modbusBuilderMsg(addr,uiLang=='de'?'Register Map exportiert.':'Register map exported.','ok');
}

function universalAddrFromField(e){let id=e&&e.id||'',m=id.match(/^uni_(prof|stat|baud|frame|proto|csum|line)_in_(\d+)$/);if(m)return m[2];m=id.match(/^uni_profile_text_(\d+)$/);if(m)return m[1];m=id.match(/^mb_(profile|station|baud|frame|slave|poll)_(\d+)$/);return m?m[2]:null}
function universalConfigFields(addr){return ['uni_prof_in_','uni_stat_in_','uni_baud_in_','uni_frame_in_','uni_proto_in_','uni_csum_in_','uni_line_in_','uni_profile_text_'].map(p=>document.getElementById(p+addr)).filter(Boolean)}
function universalHasDirty(addr){let roots=[document.getElementById('mb_builder_'+addr),document.getElementById('upb_editor_'+addr)].filter(Boolean),builderDirty=roots.some(root=>root.dataset.builderDirty==='1'||[...root.querySelectorAll('input,select,textarea')].some(e=>e.dataset.dirty==='1'));return universalConfigFields(addr).some(e=>e.dataset.dirty==='1')||builderDirty}
function universalDirtyBadge(addr){let e=document.getElementById('uni_dirty_'+addr);if(!e)return;let dirty=universalHasDirty(addr);e.textContent=dirty?(uiLang=='de'?'ungespeichert':'unsaved'):'';e.className='uni-save-state '+(dirty?'is-dirty':'')}
function universalStateConfig(m){let dc=universalDescriptorConfig(m),isModbus=Number(m.type)===8;return {profile:dc.profile||m.universal_profile||(isModbus?'Generic Modbus RTU':'Generic RS232'),station:dc.station||m.universal_station||(isModbus?'Modbus device':'Community device'),baud:dc.baud||m.universal_baud||'9600',frame:dc.frame||m.universal_frame||'8N1',protocol:dc.protocol||m.universal_protocol||(isModbus?'MODBUS_RTU':'ASCII'),checksum:dc.checksum||m.universal_checksum||(isModbus?'CRC16_MODBUS_LE':'NONE'),line_end:isModbus?'NONE':(dc.line_end||m.universal_line_end||'CR')}}
function universalCurrentModule(addr){let d=window.lastState||{};return (d.modules||[]).find(m=>Number(m.addr)===Number(addr))||null}
function universalProfileMap(text){let map={};let trimmed=String(text||'').trim();if(!trimmed)return map;if(trimmed.startsWith('{')){try{return JSON.parse(trimmed)||{}}catch(e){return map}}universalLines(trimmed).forEach(line=>{line=line.trim();if(!line||line.startsWith('#')||line.startsWith(';'))return;let p=line.indexOf('=');if(p<0)p=line.indexOf(':');if(p<0)return;let k=line.slice(0,p).trim().toLowerCase().replace(/[- ]/g,'_');map[k]=fixText(line.slice(p+1).trim())});return map}
function universalReadForm(addr){let g=id=>document.getElementById(id+'_'+addr)?.value||'',m=universalCurrentModule(addr),isModbus=m&&Number(m.type)===8,cur=universalStateConfig(m||{type:isModbus?8:7}),text=g('uni_profile_text'),map=universalProfileMap(text);return {profile:map.profile||map.name||g('uni_prof_in')||cur.profile,station:map.station||map.device||map.vendor||g('uni_stat_in')||cur.station,baud:universalConfigValue('baud',map.baud||map.uart_baud||g('uni_baud_in')||cur.baud)||cur.baud,frame:universalConfigValue('frame',map.frame||map.uart_frame||g('uni_frame_in')||cur.frame)||cur.frame,protocol:universalConfigValue('protocol',map.protocol||map.mode||g('uni_proto_in')||cur.protocol)||(isModbus?'MODBUS_RTU':'ASCII'),checksum:universalConfigValue('checksum',map.checksum||map.checksum_preset||g('uni_csum_in')||cur.checksum)||(isModbus?'CRC16_MODBUS_LE':'NONE'),line_end:isModbus?'NONE':(universalConfigValue('line_end',map.line_end||map.lineending||map.ending||g('uni_line_in')||cur.line_end)||'CR'),profile_text:text}}
function universalSetForm(addr,c,force=false,makeDirty=false){let map=[['uni_prof_in_',c.profile],['uni_stat_in_',c.station],['uni_baud_in_',c.baud],['uni_frame_in_',String(c.frame||'').toUpperCase()],['uni_proto_in_',c.protocol],['uni_csum_in_',String(c.checksum||'NONE').toUpperCase()],['uni_line_in_',String(c.line_end||'CR').toUpperCase()]];map.forEach(x=>{let e=document.getElementById(x[0]+addr);if(!e||x[1]===undefined||x[1]===null||String(x[1]).length===0)return;val(e,String(x[1]),force);if(makeDirty)markFieldDirty(e)});universalDirtyBadge(addr)}
function universalApplyMap(addr,map){universalSetForm(addr,{profile:map.profile||map.name,station:map.station||map.device||map.vendor,baud:map.baud||map.uart_baud,frame:map.frame||map.uart_frame,protocol:map.protocol||map.mode,checksum:map.checksum||map.checksum_preset,line_end:map.line_end||map.lineending||map.ending},true,true)}
async function universalLoadProfile(addr,file){if(!file)return;try{let text=fixText(await file.text()),map={};let raw=document.getElementById('uni_profile_text_'+addr);if(raw){raw.value=text;markFieldDirty(raw)}let trimmed=text.trim();if(trimmed.startsWith('{')){map=JSON.parse(trimmed)}else{universalLines(trimmed).forEach(line=>{line=line.trim();if(!line||line.startsWith('#')||line.startsWith(';'))return;let p=line.indexOf('=');if(p<0)p=line.indexOf(':');if(p<0)return;let k=line.slice(0,p).trim().toLowerCase().replace(/[- ]/g,'_');let v=fixText(line.slice(p+1).trim());map[k]=v})}universalApplyMap(addr,map);universalSetMsg(addr,(uiLang=='de'?'Profil-Datei ins Formular geladen, noch nicht im Modul gespeichert: ':'Profile loaded into form, not saved to module yet: ')+file.name,'is-dirty')}catch(e){universalSetMsg(addr,(uiLang=='de'?'Profil konnte nicht gelesen werden: ':'Profile could not be read: ')+e)}}
function universalReload(addr){let m=universalCurrentModule(addr);if(!m){universalSetMsg(addr,uiLang=='de'?'Modul nicht im aktuellen Status gefunden':'Module not found in current state');return}universalSetForm(addr,universalStateConfig(m),true,false);let f=document.getElementById('uni_file_'+addr);if(f)f.value='';universalSetMsg(addr,uiLang=='de'?'Aktuell gespeicherte Modulwerte neu geladen.':'Current saved module values reloaded.','is-ok')}
async function universalLoadFromModule(addr){try{universalSetMsg(addr,uiLang=='de'?'Lese Profil direkt aus dem Modul...':'Reading profile directly from module...');let r=await fetch('/universal/profile/read?addr='+addr,{cache:'no-store'});let text=fixText(await r.text());if(!r.ok)throw text||r.statusText;let raw=document.getElementById('uni_profile_text_'+addr);if(raw){raw.value=text;clearFieldDirty(raw)}let map={};universalLines(text.trim()).forEach(line=>{line=line.trim();if(!line||line.startsWith('#')||line.startsWith(';'))return;let p=line.indexOf('=');if(p<0)p=line.indexOf(':');if(p<0)return;let k=line.slice(0,p).trim().toLowerCase().replace(/[- ]/g,'_');map[k]=fixText(line.slice(p+1).trim())});universalSetForm(addr,{profile:map.profile,station:map.station,baud:map.baud,frame:map.frame,protocol:map.protocol,checksum:map.checksum,line_end:map.line_end},true,false);universalDirtyBadge(addr);let trunc=r.headers.get('X-OFE-Truncated')==='1';universalSetMsg(addr,trunc?(uiLang=='de'?'Profil aus Modul geladen, aber im Master gekürzt.':'Profile loaded from module, but truncated in master.'):(uiLang=='de'?'Profil aus Modul geladen.':'Profile loaded from module.'),trunc?'is-dirty':'is-ok')}catch(e){universalSetMsg(addr,String(e));alert(e)}}
function universalDiscard(addr){universalReload(addr);universalSetMsg(addr,uiLang=='de'?'Ungespeicherte Änderungen verworfen.':'Unsaved changes discarded.','is-ok')}
function universalClearProfile(addr){let m=universalCurrentModule(addr);if(!m)return;let isModbus=m.type==8;let msg=uiLang=='de'?(isModbus?'Profil-Entities wirklich aus dem Modbus-Modul löschen? Debug/System-Entities bleiben erhalten.':'Profil-Entities wirklich aus dem Universalmodul löschen? Debug/System-Entities bleiben erhalten.'):(isModbus?'Really clear profile entities from the Modbus module? Debug/system entities stay.':'Really clear profile entities from the Universal module? Debug/system entities stay.');if(!confirm(msg))return;let body=new URLSearchParams();body.set('addr',addr);let c=universalStateConfig(m);body.set('profile',c.profile);body.set('station',c.station);body.set('baud',c.baud);body.set('frame',c.frame);body.set('protocol',c.protocol);body.set('checksum',c.checksum);body.set('line_end',c.line_end);body.set('profile_text','');universalSetMsg(addr,uiLang=='de'?'Lösche Profil-Entities im Modul...':'Clearing profile entities in module...');fetch('/universal/profile',{method:'POST',body,cache:'no-store'}).then(async r=>{let txt=await r.text();if(!r.ok)throw txt;let raw=document.getElementById('uni_profile_text_'+addr);if(raw){raw.value='';clearFieldDirty(raw)}universalSetMsg(addr,txt|| (uiLang=='de'?'Profil-Entities gelöscht.':'Profile entities cleared.'),'is-ok');setTimeout(load,300)}).catch(e=>{universalSetMsg(addr,String(e));alert(e)})}
function universalExport(addr){let c=universalReadForm(addr),m=universalCurrentModule(addr),isModbus=m&&Number(m.type)===8,safe=String(c.profile||(isModbus?'modbus_rtu':'universal_rs232')).replace(/[^a-z0-9_-]+/gi,'_').replace(/^_+|_+$/g,'')||(isModbus?'modbus_rtu':'universal_rs232');let text=(c.profile_text||'').trim();if(!text&&isModbus)text=`# Open Fume Extractor Modbus RTU profile v1
profile=${c.profile||'Generic Modbus RTU'}
station=${c.station||'Modbus device'}
baud=${c.baud||9600}
frame=${c.frame||'8N1'}
protocol=MODBUS_RTU
checksum=CRC16_MODBUS_LE
slave=1
poll_ms=500

# entity.1.type=binary_sensor
# entity.1.name=Start Input
# entity.1.func=read_discrete
# entity.1.reg=0x0000
# entity.1.role=main_input
`;if(!text)text=
`# Open Fume Extractor Universal RS232 profile v1
profile=${c.profile}
station=${c.station}
baud=${c.baud}
frame=${c.frame}
protocol=${c.protocol}
checksum=${c.checksum}
line_end=${c.line_end}

# entity.1.type=number
# entity.1.name=Leistung
# entity.1.unit=%
# entity.1.min=30
# entity.1.max=100
# entity.1.poll=D
# entity.1.match=D###
# entity.1.set=d{value:03}
`;let blob=new Blob([text+'\n'],{type:'text/plain;charset=utf-8'}),a=document.createElement('a'),url=URL.createObjectURL(blob);a.href=url;a.download=safe+'.ofeprofile';document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(url);a.remove()},250);universalSetMsg(addr,uiLang=='de'?'Profil aus Formular exportiert.':'Profile exported from form.','is-ok')}
async function universalSave(addr){let c=universalReadForm(addr),body=new URLSearchParams();body.set('addr',addr);body.set('profile',fixText(c.profile));body.set('station',fixText(c.station));body.set('baud',c.baud);body.set('frame',c.frame);body.set('protocol',c.protocol);body.set('checksum',c.checksum);body.set('line_end',c.line_end);let ptEl=document.getElementById('uni_profile_text_'+addr);if((c.profile_text||'').trim() || (ptEl&&ptEl.dataset.dirty==='1'))body.set('profile_text',fixText(c.profile_text||''));universalSetMsg(addr,uiLang=='de'?'Speichere dauerhaft ins Modul...':'Saving permanently to module...');let r=await fetch('/universal/profile',{method:'POST',body,cache:'no-store'});let txt=await r.text();if(!r.ok){universalSetMsg(addr,txt);alert(txt);return false}['uni_prof_in_','uni_stat_in_','uni_baud_in_','uni_frame_in_','uni_proto_in_','uni_csum_in_','uni_line_in_','uni_profile_text_','uni_file_'].forEach(pfx=>clearFieldDirty(document.getElementById(pfx+addr)));universalDirtyBadge(addr);universalSetMsg(addr,txt|| (uiLang=='de'?'Im Modul gespeichert.':'Saved to module.'),'is-ok');delete universalDescriptorCache[addr];setTimeout(()=>load(true),300);return true}
function displayBrightnessInput(addr,value){set('dbri_v_'+addr,value+' %');clearTimeout(displaySetTimers[addr]);displaySetTimers[addr]=setTimeout(()=>displaySet(addr),350)}
async function displaySet(addr){clearTimeout(displaySetTimers[addr]);delete displaySetTimers[addr];let b=document.getElementById('dbri_'+addr).value,l=document.getElementById('dlang_'+addr).value,tv=document.getElementById('dtheme_'+addr).value,sv=document.getElementById('dsaver_'+addr).value;displayHoldUntil[addr]=Date.now()+4000;let r=await fetch('/display/set?addr='+addr+'&brightness='+b+'&language='+l+'&theme='+tv+'&screensaver='+sv,{method:'POST',cache:'no-store'});if(!r.ok)alert(await r.text());else ['dbri_','dlang_','dtheme_','dsaver_'].forEach(pfx=>clearFieldDirty(document.getElementById(pfx+addr)))}
async function scan(){await fetch('/scan',{method:'POST',cache:'no-store'});setTimeout(load,300)}
async function addressBus(){await fetch('/scan?address=1',{method:'POST',cache:'no-store'});setTimeout(load,900)}
async function saveLabel(el){if(!el||el.dataset.saving==='1')return;let v=el.value.trim();if(v===el.dataset.saved)return;el.dataset.saving='1';try{let body=new URLSearchParams();body.set('uid',el.dataset.uid||'');body.set('addr',el.dataset.addr||'0');body.set('label',v);let r=await fetch('/module/label',{method:'POST',body});if(!r.ok)throw await r.text();el.dataset.saved=v;clearFieldDirty(el);setTimeout(load,300)}catch(e){alert('Label failed: '+e)}finally{el.dataset.saving='0'}}
async function saveIoAlias(el){if(!el||el.dataset.saving==='1')return;let v=el.value.trim();if(v===el.dataset.saved)return;el.dataset.saving='1';try{let body=new URLSearchParams();body.set('uid',el.dataset.uid||'');body.set('addr',el.dataset.addr||'0');body.set('key',el.dataset.key||'');body.set('alias',v);let r=await fetch('/module/io_alias',{method:'POST',body});if(!r.ok)throw await r.text();el.dataset.saved=v;clearFieldDirty(el);detailsSig='';routingSig='';setTimeout(load,250)}catch(e){alert('Alias failed: '+e)}finally{el.dataset.saving='0'}}
async function saveMainInput(){let e=document.getElementById('main_input_sel'),v=dec(e.value);let r=await fetch(`/routing/main?st=${v.t}&sa=${v.a}&sb=${v.b}`,{method:'POST',cache:'no-store'});if(r.ok)clearFieldDirty(e);setTimeout(load,250)}async function saveOutput(){let e=document.getElementById('output_sel');let r=await fetch('/output/select?addr='+e.value,{method:'POST',cache:'no-store'});if(r.ok)clearFieldDirty(e);setTimeout(load,300)}
async function rebootModule(button,addr){let row=button&&button.closest?button.closest('tr'):null,input=row?row.querySelector('.label-edit'):null,name=input?input.value.trim():'';if(!name&&row&&row.cells&&row.cells[2])name=row.cells[2].textContent.trim();let target=hx(addr)+(name?' '+name:'');if(!confirm(t('reboot_module')+' '+target))return;let r=await fetch('/module/reboot?addr='+addr,{method:'POST',cache:'no-store'});if(!r.ok)alert(await r.text());setTimeout(load,1200)}
async function restartMaster(){if(!confirm(t('restart_confirm')))return;await fetch('/restart',{method:'POST',cache:'no-store'});setTimeout(()=>{location.reload()},3500)}
async function routeSet(kind,addr,bit,value){let url='/routing/set?value='+(value?1:0);if(kind=='jbc')url+='&jbc=1';else url+='&addr='+addr+'&bit='+bit;await fetch(url,{method:'POST',cache:'no-store'});setTimeout(load,150)}
async function ruleChanged(i){let se=document.getElementById('rule_s_'+i),te=document.getElementById('rule_t_'+i),s=dec(se.value),t=dec(te.value);let r=await fetch(`/routing/rule?idx=${i}&st=${s.t}&sa=${s.a}&sb=${s.b}&tt=${t.t}&ta=${t.a}&tb=${t.b}`,{method:'POST',cache:'no-store'});if(r.ok){clearFieldDirty(se);clearFieldDirty(te)}setTimeout(load,150)}
async function addRule(){let d=window.lastState||{},rules=d.input_rules||[],i=rules.findIndex(r=>!r.enabled);if(i<0){alert('Max rules reached');return}let src=dec((document.getElementById('main_input_sel')||{}).value||'0,0,0'),out=Number((document.getElementById('output_sel')||{}).value||0);let tt=out?2:1,ta=out,tb=0;await fetch(`/routing/rule?idx=${i}&st=${src.t}&sa=${src.a}&sb=${src.b}&tt=${tt}&ta=${ta}&tb=${tb}`,{method:'POST',cache:'no-store'});routingSig='';setTimeout(load,150)}
async function deleteRule(i){await fetch(`/routing/rule?idx=${i}&st=0&sa=0&sb=0&tt=0&ta=0&tb=0`,{method:'POST',cache:'no-store'});routingSig='';setTimeout(load,150)}async function ioSet(addr,bitNo,value){if(document.activeElement)document.activeElement.blur();let ws=document.getElementById('wsl_'+addr);if(ws)wellerHoldUntil[addr]=Date.now()+2500;await fetch('/io/set?addr='+addr+'&bit='+bitNo+'&value='+(value?1:0),{method:'POST',cache:'no-store'});setTimeout(load,250)}async function wellerFanToggle(addr,value){if(document.activeElement)document.activeElement.blur();let sl=document.getElementById('wsl_'+addr);let speed=Math.max(30,Math.min(100,Number(sl.value||wellerPending[addr]||30)));wellerPending[addr]=speed;wellerHoldUntil[addr]=Date.now()+3000;if(value)await fetch('/weller/set?addr='+addr+'&speed='+speed,{method:'POST',cache:'no-store'});await fetch('/io/set?addr='+addr+'&bit=0&value='+(value?1:0),{method:'POST',cache:'no-store'});setTimeout(load,250)}
async function fanOutputSet(addr,enable,power){if(document.activeElement)document.activeElement.blur();let sl=document.getElementById('io_power_'+addr);let pct=power==null?Number(sl.value||10):Number(power||10);if(enable!==false&&pct<10)pct=10;moduleOutputPending[addr]=pct;moduleOutputHoldUntil[addr]=Date.now()+3000;let url='/output/module?addr='+addr+'&power='+pct;if(enable!==null&&enable!==undefined)url+='&enable='+(enable?1:0);let r=await fetch(url,{method:'POST',cache:'no-store'});if(!r.ok)alert(await r.text());else clearFieldDirty(sl);setTimeout(load,250)}
async function fanioCal(addr,action,value){
  if(document.activeElement)document.activeElement.blur();
  let body=new URLSearchParams();
  body.set('addr',addr);
  body.set('action',action);
  if(action===3){
    let warn=Number(document.getElementById('io_warn_'+addr).value||0);
    let full=Number(document.getElementById('io_full_'+addr).value||0);
    if(full<=warn){alert('Full raw must be greater than Warn raw');return;}
    body.set('warn',warn);
    body.set('full',full);
  } else if(action===5){
    body.set('enabled',value?1:0);
  }
  let r=await fetch('/fanio/calibrate',{method:'POST',body,cache:'no-store'});
  if(!r.ok)alert(await r.text());
  else ['io_warn_','io_full_','io_sensor_'].forEach(pfx=>clearFieldDirty(document.getElementById(pfx+addr)));
  setTimeout(load,350);
}async function wellerSpeed(addr,value){wellerSlide(addr,value);let r=await fetch('/weller/set?addr='+addr+'&speed='+value,{method:'POST',cache:'no-store'});if(r.ok)clearFieldDirty(document.getElementById('wsl_'+addr));wellerHoldUntil[addr]=Date.now()+3000;setTimeout(load,400)}
async function wellerReset(addr){if(document.activeElement)document.activeElement.blur();await fetch('/weller/set?addr='+addr+'&reset_filter=1',{method:'POST',cache:'no-store'});setTimeout(load,250)}
async function wellerFilter(addr){let total=wellerFilterTouch(addr),sel=document.getElementById('wf_sel_'+addr);if(total<=0)return;if(document.activeElement)document.activeElement.blur();wellerFilterHoldUntil[addr]=Date.now()+8000;let r=await fetch('/weller/set?addr='+addr+'&filter_minutes='+total,{method:'POST',cache:'no-store'});if(r.ok)clearFieldDirty(sel);setTimeout(load,350)}
let stateLoadBusy=false;async function load(force=false){if(stateLoadBusy)return;stateLoadBusy=true;try{if(force){routingSig='';detailsSig=''}let r=await fetch('/state',{cache:'no-store'});let d=await r.json();if(typeof d.developer_mode==='boolean'&&devMode!==d.developer_mode){devMode=!!d.developer_mode;localStorage.setItem('ofe_dev_mode',devMode?'1':'0');applyDevMode()}let needDesc=force;(d.modules||[]).forEach(m=>{if((m.type==7||m.type==8)&&m.universal_descriptor_valid){let c=universalDescriptorCache[m.addr];if(!c||Number(c.crc)!==Number(m.universal_descriptor_crc))needDesc=true}});if(needDesc){try{let r2=await fetch('/state?desc=1',{cache:'no-store'});let dd=await r2.json();(dd.modules||[]).forEach(dm=>{if((dm.type==7||dm.type==8)&&dm.universal_descriptor){universalDescriptorCache[dm.addr]={crc:Number(dm.universal_descriptor_crc),text:dm.universal_descriptor};let m=(d.modules||[]).find(x=>Number(x.addr)===Number(dm.addr));if(m){m.universal_descriptor=dm.universal_descriptor;m.universal_descriptor_crc=dm.universal_descriptor_crc;m.universal_descriptor_valid=dm.universal_descriptor_valid;}}})}catch(e){console.log('descriptor refresh failed',e)}}window.lastState=d;
if(d.ui_lang&&d.ui_lang!==uiLang){uiLang=d.ui_lang=='en'?'en':'de';applyLang();routingSig='';detailsSig=''}
set('master_fw',d.master_fw||'-');set('footer_fw',d.master_fw||'-');set('master_ip',d.master_ip||'-');
wifiSet(d);
set('uptime',up(d.uptime_ms));set('datetime',d.datetime||'-');
set('heap',kb(d.heap_free)+' / min '+kb(d.heap_min));
set('cpu_load','CPU '+Number(d.cpu_load_pct||0)+'%');set('loop_max_dev','max '+Number(d.loop_max_ms||0)+' ms');if(devMode&&d.heap_diag){let h=d.heap_diag;let lm=String(h.low_label||'-')+' ['+String(h.low_task||'-')+'/C'+Number(h.low_core===undefined?-1:h.low_core)+'] '+String(h.low_context||'');let bm=String(h.block_label||'-')+' ['+String(h.block_task||'-')+'/C'+Number(h.block_core===undefined?-1:h.block_core)+'] '+String(h.block_context||'');set('heap_diag','Block '+kb(h.largest_now)+' · intern '+kb(h.internal_now));set('heap_diag_low','Heap-Low '+kb(h.low_free)+' / Block '+kb(h.low_largest)+' @ '+lm);set('heap_diag_block','Block-Low '+kb(h.block_largest)+' / Heap '+kb(h.block_free)+' @ '+bm);}else{set('heap_diag','-');set('heap_diag_low','-');set('heap_diag_block','-');}
document.getElementById('loop_fill').style.width=Math.max(0,Math.min(100,Number(d.cpu_load_pct||0)))+'%';
set('out',d.output_enabled?t('on'):t('off'));document.getElementById('out').className='v '+(d.output_enabled?'on':'off');
let reqPct=Math.max(0,Math.min(100,Math.round(Number(d.output_power||0)/10)));set('power',reqPct+'%');document.getElementById('pfill').style.width=reqPct+'%';
let heroPct=reqPct,offlineModule=(d.modules||[]).some(m=>!m.online),noMainInput=!mainInputAvailable(d),noMainOutput=!Number(d.active_output_addr||0);set('hero_state',d.output_enabled?t('extraction_active'):((offlineModule||noMainInput||noMainOutput)?t('not_ready'):t('ready')));set('hero_power',heroPct+'%');set('hero_meta',(d.afterrun_ms?`${t('afterrun')} ${Math.ceil(d.afterrun_ms/1000)}s | `:'')+Number(d.modules_count||0)+' '+t('modules'));document.getElementById('hero_fill').style.width=heroPct+'%';renderAlarms(d);
let workOn=Number(d.work_mask||0)!=0;set('work',workOn?t('on'):t('off'));iron('work_iron',workOn);set('after',Math.ceil((d.afterrun_ms||0)/1000)+'s');set('mods',d.modules_count);
let jbcMods=(d.modules||[]).filter(m=>m.online&&((Number(m.caps||0)&16777217)!=0));
let hasJbc=jbcMods.length>0;document.getElementById('jbc_link_cell').style.display=hasJbc?'':'none';document.getElementById('station_cell').style.display=hasJbc?'':'none';let jbcOn=jbcMods.filter(m=>{let caps=Number(m.caps||0),linked=(Number(m.jbc_link_flags||0)&1)!=0;if(!linked)return false;if((caps&16777216)!=0)return true;return (caps&1)!=0&&Number(m.station_addr||0)!=0}).length;
set('jbc_link',jbcMods.length?(jbcOn+'/'+jbcMods.length+' '+t('on')):t('off'));document.getElementById('jbc_link').className='v '+(jbcOn===jbcMods.length&&jbcOn?'on':(jbcOn?'warn':'off'));document.getElementById('jbc_error_cell').style.display=hasJbc?'':'none';
let stations=jbcMods.map(m=>{let linked=(Number(m.jbc_link_flags||0)&1)!=0;if(!linked)return '';if(Number(m.type)==9){let sm=String(m.jbc_usb_model||'').trim(),raw=String(m.jbc_usb_model_raw||'').trim();if((!sm||sm=='-')&&raw&&raw!='-')sm=raw.split('_')[0];return sm&&sm!='-'?sm:'JBC USB'}if(Number(m.station_addr||0)){let st=String(m.station_type||'JBC').trim();return st&&st!='-'?st:'JBC'}return ''}).filter(Boolean).join(', ');
set('station',stations||'-');
document.getElementById('roles').innerHTML=outputRoleText(d);
set('suction',suctionName(d.jbc.suction_level));set('select',customPower(d.jbc.select_flow)+'%');set('delay',d.jbc.delay_work_sec+'s');set('err',hasJbc?(d.jbc.stat_error_text||jbcErrorText(d.jbc.stat_error)):'-');
val('jbc_suction',d.jbc.suction_level);val('jbc_select',customPower(d.jbc.select_flow));val('jbc_delay_work',d.jbc.delay_work_sec);val('jbc_delay_stand',d.jbc.delay_stand_sec);val('jbc_stand',d.jbc.stand_intakes?1:0);val('jbc_cont',d.jbc.continuous?1:0);val('afterrun_power_enabled',d.afterrun_power_enabled?1:0);val('afterrun_power',Math.round(Number(d.afterrun_power||300)/10));let jp=document.getElementById('jbc_select'),jm=mainOutputMin(d);if(jp){jp.min=jm;if(!fieldBusy(jp)&&Number(jp.value||0)<jm)val(jp,jm)}let ap=document.getElementById('afterrun_power');if(ap){ap.min=jm;if(!fieldBusy(ap)&&Number(ap.value||0)<jm)val(ap,jm)}
set('ofb',d.output_module.valid?((d.output_module.enabled?t('on'):t('off'))+' | '+Math.max(0,Math.min(100,Math.round(Number(d.output_module.power||0)/10)))+'%'):'-');
set('rpm',d.output_module.valid?d.output_module.rpm:'-');set('fault',d.output_module.valid?(d.output_module.fault_text||faultText(d.output_module.fault_mask,d.active_output_type)):'-');
let mi=document.getElementById('main_input_sel');if(!fieldBusy(mi)){mi.innerHTML=sourceOptions(d,enc(d.main_input_source_type,d.main_input_source_addr,d.main_input_source_bit));val(mi,enc(d.main_input_source_type,d.main_input_source_addr,d.main_input_source_bit),true)}let os=document.getElementById('output_sel');if(!fieldBusy(os)){let current=String(d.preferred_output_addr||0),hasPreferred=!Number(d.preferred_output_addr||0);let opts=`<option value="0">${escHtml(mainOutputText(d,true))}</option>`;(d.modules||[]).forEach(m=>{if(m.online&&((m.caps&36)!=0||((m.type==7||m.type==8)&&universalOutputDefs(m).length))){opts+=`<option value="${m.addr}">${escHtml(hx(m.addr)+' '+mn(m))}</option>`;if(Number(m.addr)==Number(d.preferred_output_addr||0))hasPreferred=true}});if(Number(d.preferred_output_addr||0)&&!hasPreferred)opts+=`<option value="${d.preferred_output_addr}">${escHtml(mainOutputText(d,false))}</option>`;os.innerHTML=opts;val(os,current,true);}
let rows='';(d.modules||[]).forEach(m=>{let role=moduleRoleText(d,m);let tv=m.online&&m.telemetry_valid;let mh=tv?kb(m.module_heap_free):'-';let ml=tv?('CPU '+Number(m.module_cpu_load_pct||0)+'%'):'-';let mu=tv?ups(m.module_uptime_s):'-';let off=Number(m.offline_events||0);let label=(m.label||m.type_name||m.name||'').replace(/"/g,'&quot;');let cq=commText(m),cc=commClass(m),ct=commTitle(m,d);rows+=`<tr><td>${hx(m.addr)}</td><td>${role}</td><td>${m.type_name}</td><td><input class="label-edit" value="${label}" data-saved="${label}" data-uid="${m.uid}" data-addr="${m.addr}" onkeydown="if(event.key==='Enter'){event.preventDefault();saveLabel(this)}" onblur="saveLabel(this)"></td><td>${m.fw}</td><td>${m.uid}</td><td class="${m.online?'on':'warn'}">${m.online?t('online'):t('offline')}</td><td class="dev-only">${mh}</td><td>${ml}</td><td>${mu}</td><td>${off}</td><td class="dev-only" title="${ct}">${Number(m.miss_total||0)} / ${m.misses||0}</td><td class="${cc}" title="${ct}">${cq}</td><td><button class="secondary" onclick="rebootModule(this,${m.addr})" ${m.online?'':'disabled'}>${t('reboot_module')}</button></td></tr>`});
if(!document.activeElement||!document.activeElement.classList.contains('label-edit'))document.getElementById('mt').innerHTML=rows||`<tr><td colspan="14" class="muted">${t('no_modules')}</td></tr>`;
let sig=moduleSig(d);if(sig!==detailsSig){detailsSig=sig;renderDetails(d)}updateDetails(d);updateRules(d);loadLogicSummary(force);
}catch(e){console.log(e)}finally{stateLoadBusy=false}}setInterval(load,1000);setInterval(loadLedState,200);load();loadLedState();
let settingsSaveTimer=0,settingsSaveSeq=0;async function saveExtractorSettings(){clearTimeout(settingsSaveTimer);let seq=++settingsSaveSeq,s=document.getElementById('jbc_save'),form=document.getElementById('jbcForm');s.textContent=t('saving');try{let r=await fetch('/jbc/settings',{method:'POST',body:new FormData(form)}),msg=await r.text();if(seq!==settingsSaveSeq)return;s.textContent=r.ok?t('saved'):('Error: '+msg);if(r.ok){clearDirtyIn(form);setTimeout(()=>{if(seq===settingsSaveSeq)s.textContent=''},1800)}setTimeout(load,250)}catch(x){if(seq===settingsSaveSeq)s.textContent=t('connection_failed')}}function scheduleExtractorSettings(delay=180){clearTimeout(settingsSaveTimer);settingsSaveTimer=setTimeout(saveExtractorSettings,delay)}document.getElementById('jbcForm').addEventListener('submit',e=>{e.preventDefault();saveExtractorSettings()});document.getElementById('jbcForm').addEventListener('change',()=>scheduleExtractorSettings(120));document.getElementById('jbcForm').addEventListener('input',e=>{if(e.target&&e.target.matches('input[type=number]'))scheduleExtractorSettings(650)});
applyLang();
</script></body></html>
)HTML";
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "text/html; charset=utf-8", "");
  web.sendContent_P(html1);
  web.sendContent(web_csrf_script());
  web.sendContent_P(html2);
  web.sendContent("");
}

static void web_handle_logo() {
  web.sendHeader("Cache-Control", "public, max-age=86400");
  web.send_P(200, PSTR("image/png"), reinterpret_cast<const char*>(OFE_LOGO_PNG), OFE_LOGO_PNG_SIZE);
}
