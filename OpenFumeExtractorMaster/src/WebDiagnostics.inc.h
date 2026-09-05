#pragma once

// Bus diagnostics page and JSON/control endpoints. Included from the master
// sketch so it can keep using the existing static WebServer and scheduler.
static const char* trace_dir_name(uint8_t dir) {
  switch (dir) {
    case MasterScheduler::TRACE_TX: return "TX";
    case MasterScheduler::TRACE_RX: return "RX";
    case MasterScheduler::TRACE_TIMEOUT: return "TIMEOUT";
    case MasterScheduler::TRACE_LOCAL_TX: return "UART_TX";
    case MasterScheduler::TRACE_LOCAL_RX: return "UART_RX";
    default: return "INFO";
  }
}

static String trace_cmd_name(uint8_t cmd) {
  if (cmd == CMD_ERROR) return F("ERROR");
  const bool response = (cmd & 0x80) != 0;
  const uint8_t base = cmd & 0x7F;
  const __FlashStringHelper* name = nullptr;
  switch (base) {
    case CMD_PING: name = F("PING"); break;
    case CMD_INFO: name = F("INFO"); break;
    case CMD_GET_CAPS: name = F("GET_CAPS"); break;
    case CMD_GET_STATUS: name = F("GET_STATUS"); break;
    case CMD_GET_STATE: name = F("GET_STATE"); break;
    case CMD_SET_STATE: name = F("SET_STATE"); break;
    case CMD_GET_TELEMETRY: name = F("GET_TELEMETRY"); break;
    case CMD_FAST_POLL: name = F("FAST_POLL"); break;
    case CMD_GET_EVENTS: name = F("GET_EVENTS"); break;
    case CMD_ACK_EVENTS: name = F("ACK_EVENTS"); break;
    case CMD_LED_SYNC: name = F("LED_SYNC"); break;
    case CMD_SET_ADDRESS: name = F("SET_ADDRESS"); break;
    case CMD_SAVE_CONFIG: name = F("SAVE_CONFIG"); break;
    case CMD_FACTORY_RESET: name = F("FACTORY_RESET"); break;
    case CMD_DISCOVER_MODULES: name = F("DISCOVER"); break;
    case CMD_SET_ADDRESS_UID: name = F("SET_ADDRESS_UID"); break;
    case CMD_SET_LABEL: name = F("SET_LABEL"); break;
    case CMD_SET_ENABLE: name = F("SET_ENABLE"); break;
    case CMD_SET_POWER: name = F("SET_POWER"); break;
    case CMD_SET_TARGET_RPM: name = F("SET_TARGET_RPM"); break;
    case CMD_SET_OUTPUT: name = F("SET_OUTPUT"); break;
    case CMD_GET_IO: name = F("GET_IO"); break;
    case CMD_SET_IO: name = F("SET_IO"); break;
    case CMD_FILTER_CALIBRATION: name = F("FILTER_CAL"); break;
    case CMD_FW_BEGIN: name = F("FW_BEGIN"); break;
    case CMD_FW_CHUNK: name = F("FW_CHUNK"); break;
    case CMD_FW_END: name = F("FW_END"); break;
    case CMD_FW_ABORT: name = F("FW_ABORT"); break;
    case CMD_FW_STATUS: name = F("FW_STATUS"); break;
    case CMD_FW_REBOOT: name = F("FW_REBOOT"); break;
    case CMD_DISPLAY_STATUS: name = F("DISPLAY_STATUS"); break;
    case CMD_DISPLAY_EVENT: name = F("DISPLAY_EVENT"); break;
    case CMD_DISPLAY_UPDATE: name = F("DISPLAY_UPDATE"); break;
    case CMD_DISPLAY_DETAIL_PAGE: name = F("DISPLAY_DETAIL_PAGE"); break;
    case CMD_DISPLAY_ALARMS: name = F("DISPLAY_ALARMS"); break;
    case CMD_DISPLAY_MODULE_LIST: name = F("DISPLAY_MODULE_LIST"); break;
    case CMD_DISPLAY_MODULE_DETAIL: name = F("DISPLAY_MODULE_DETAIL"); break;
    case CMD_DISPLAY_CONFIG: name = F("DISPLAY_CONFIG"); break;
    case CMD_TRACE_CONTROL: name = F("TRACE_CONTROL"); break;
    case CMD_TRACE_READ: name = F("TRACE_READ"); break;
    case CMD_DESCRIPTOR_GET: name = F("DESCRIPTOR_GET"); break;
    case CMD_ENTITY_GET: name = F("ENTITY_GET"); break;
    case CMD_ENTITY_SET: name = F("ENTITY_SET"); break;
    case CMD_ENTITY_EVENT: name = F("ENTITY_EVENT"); break;
    case CMD_FAULT_MAP_GET: name = F("FAULT_MAP_GET"); break;
    case CMD_PROFILE_BEGIN: name = F("PROFILE_BEGIN"); break;
    case CMD_PROFILE_CHUNK: name = F("PROFILE_CHUNK"); break;
    case CMD_PROFILE_END: name = F("PROFILE_END"); break;
    case CMD_PROFILE_GET: name = F("PROFILE_GET"); break;
  }
  if (!name) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", cmd);
    return String(buf);
  }
  String out(name);
  if (response) out += F("|RESP");
  return out;
}

static const char* diagnostics_module_type_name(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "JBC FAE Bus";
    case MODULE_JBC_USB: return "JBC USB";
    case MODULE_FAN_IO: return "Fan/IO";
    case MODULE_FAN_IO_PRO: return "Fan/IO Pro";
    case MODULE_WELLER_ZERO_SMOG: return "Weller Zero Smog";
    case MODULE_DISPLAY: return "Display";
    case MODULE_UNIVERSAL_RS232: return "Universal RS232";
    case MODULE_MODBUS_RTU: return "Modbus RTU";
    default: return "Module";
  }
}

static const char* diagnostics_device_bus_name(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "JBC P02";
    case MODULE_JBC_USB: return "JBC USB / CP210x / P01+P02";
    case MODULE_FAN_IO:
    case MODULE_FAN_IO_PRO: return "Lokale I/O";
    case MODULE_WELLER_ZERO_SMOG: return "Weller UART";
    case MODULE_DISPLAY: return "Display Engine";
    case MODULE_UNIVERSAL_RS232: return "Universal RS232";
    case MODULE_MODBUS_RTU: return "Modbus RTU";
    default: return "-";
  }
}



static String diagnostics_descriptor_meta_value(const char* line, const char* key) {
  if (!line || !key || !*key) return String();
  String needle = String(key) + "=";
  const char* p = line;
  while ((p = strstr(p, needle.c_str())) != nullptr) {
    if (p == line || p[-1] == ' ' || p[-1] == '\t') {
      p += needle.length();
      const char* end = p;
      while (*end) {
        if (*end == ' ' || *end == '\t') {
          const char* q = end + 1;
          while (*q == ' ' || *q == '\t') ++q;
          const char* r = q;
          while ((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z') ||
                 (*r >= '0' && *r <= '9') || *r == '_') ++r;
          if (r > q && *r == '=') break;
        }
        ++end;
      }
      char tmp[256];
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

static String diagnostics_descriptor_meta_value2(const char* line, const char* long_key, const char* short_key) {
  String v = diagnostics_descriptor_meta_value(line, long_key);
  if (!v.length() && short_key && *short_key) v = diagnostics_descriptor_meta_value(line, short_key);
  return v;
}

static bool diagnostics_descriptor_first_tokens(const char* line, uint8_t& id, String& type, String& key, String& mode) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') ++line;
  if (*line < '0' || *line > '9') return false;
  char* endp = nullptr;
  unsigned long raw_id = strtoul(line, &endp, 10);
  if (endp == line || raw_id > 255) return false;
  id = (uint8_t)raw_id;
  line = endp;

  auto next_token = [&](String& out) -> bool {
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

  return next_token(type) && next_token(key) && next_token(mode);
}

static void diagnostics_append_trace_defs_json(String& json, const ModuleRecord& m) {
  json += F(",\"local_trace_defs\":[");
  if (m.universal_descriptor_valid && m.universal_descriptor[0]) {
    const char* p = m.universal_descriptor;
    bool first = true;
    while (p && *p) {
      const char* line = p;
      const char* next = strchr(p, '\n');
      char buf[768];
      size_t n = next ? (size_t)(next - line) : strlen(line);
      if (n >= sizeof(buf)) n = sizeof(buf) - 1;
      memcpy(buf, line, n);
      buf[n] = 0;

      uint8_t id = 0;
      String type, key, mode;
      if (diagnostics_descriptor_first_tokens(buf, id, type, key, mode) && id >= 20) {
        String source = diagnostics_descriptor_meta_value(buf, "source");
        if (!source.length() || source.equalsIgnoreCase("profile")) {
          String label = diagnostics_descriptor_meta_value(buf, strcmp(web_lang, "de") == 0 ? "de" : "en");
          if (!label.length()) label = diagnostics_descriptor_meta_value(buf, "de");
          if (!label.length()) label = diagnostics_descriptor_meta_value(buf, "en");
          if (!label.length()) label = key;

          if (!first) json += ',';
          first = false;
          json += F("{\"id\":"); json += id;
          json += F(",\"type\":\""); json += json_escape(type.c_str()); json += '"';
          json += F(",\"key\":\""); json += json_escape(key.c_str()); json += '"';
          json += F(",\"mode\":\""); json += json_escape(mode.c_str()); json += '"';
          json += F(",\"label\":\""); json += json_escape(label.c_str()); json += '"';
          const char* diag_keys[] = {"unit","options","values","value_on","value_off","scale","div","off","mask","shift","tb","tf","map_mode","map","map_default","role","reg","func","read_func","slave","min","max","step","value_offset","value_type","value_len","match_offset"};
          for (const char* dk : diag_keys) {
            String v = diagnostics_descriptor_meta_value(buf, dk);
            json += F(",\""); json += dk; json += F("\":\""); json += json_escape(v.c_str()); json += '"';
          }
          // Universal trace templates have long and compact descriptor aliases.
          json += F(",\"poll_hex\":\""); { String v = diagnostics_descriptor_meta_value2(buf, "trace_poll_hex", "tp"); json += json_escape(v.c_str()); } json += '"';
          json += F(",\"match_hex\":\""); { String v = diagnostics_descriptor_meta_value2(buf, "trace_match_hex", "tm"); json += json_escape(v.c_str()); } json += '"';
          json += F(",\"set_hex\":\""); { String v = diagnostics_descriptor_meta_value2(buf, "trace_set_hex", "ts"); json += json_escape(v.c_str()); } json += '"';
          json += F(",\"on_hex\":\""); { String v = diagnostics_descriptor_meta_value2(buf, "trace_on_hex", "tn"); json += json_escape(v.c_str()); } json += '"';
          json += F(",\"off_hex\":\""); {
            String v = diagnostics_descriptor_meta_value2(buf, "trace_off_hex", "to");
            // Legacy compact descriptors used tf for trace-off. New descriptors
            // reserve tf for time_format, so use the old alias only when no
            // time-base metadata exists on this entity.
            if (!v.length() && !diagnostics_descriptor_meta_value(buf, "tb").length()) v = diagnostics_descriptor_meta_value(buf, "tf");
            json += json_escape(v.c_str());
          } json += '"';
          json += '}';
        }
      }
      p = next ? next + 1 : nullptr;
    }
  }
  json += ']';
}

static void diagnostics_descriptor_header_value(const ModuleRecord& m, const char* key, char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  if (!key || !*key || !m.universal_descriptor_valid || !m.universal_descriptor[0]) return;
  const size_t key_len = strlen(key);
  const char* p = m.universal_descriptor;
  while (*p) {
    const char* eol = strchr(p, '\n');
    const size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
    if (line_len > key_len + 1 && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
      size_t n = line_len - key_len - 1;
      if (n >= out_len) n = out_len - 1;
      memcpy(out, p + key_len + 1, n);
      out[n] = 0;
      return;
    }
    if (!eol) break;
    p = eol + 1;
  }
}

static String diagnostics_device_detail(const ModuleRecord& m) {
  char buf[96];
  switch (m.type) {
    case MODULE_JBC_USB: {
      const char* proto = m.jbc_usb_command_protocol == 1 ? "P01" : (m.jbc_usb_command_protocol == 2 ? "P02" : "-");
      const char* model = m.jbc_usb_model_raw[0] && strcmp(m.jbc_usb_model_raw, "-") != 0 ? m.jbc_usb_model_raw : "JBC";
      snprintf(buf, sizeof(buf), "%s · %s · %u Port%s · Work %s · Stand %s",
        model, proto, m.jbc_usb_port_count, m.jbc_usb_port_count == 1 ? "" : "s",
        m.jbc_work_mask ? "an" : "aus", m.jbc_stand_mask ? "an" : "aus");
      return String(buf);
    }
    case MODULE_JBC_BUS:
      if (m.station_addr) {
        snprintf(buf, sizeof(buf), "Station 0x%02X · Work %s · Stand %s",
          m.station_addr, m.jbc_work_mask ? "an" : "aus", m.jbc_stand_mask ? "an" : "aus");
      } else {
        snprintf(buf, sizeof(buf), "Keine Station");
      }
      return String(buf);
    case MODULE_FAN_IO:
    case MODULE_FAN_IO_PRO:
      snprintf(buf, sizeof(buf), "IN 0x%04X · OUT 0x%04X · Fault 0x%04X",
        m.io_input_mask, m.io_output_mask, m.io_fault_mask);
      return String(buf);
    case MODULE_WELLER_ZERO_SMOG:
      if (m.weller_uart_age_sec != 0xFFFF) {
        snprintf(buf, sizeof(buf), "UART Alter %us · Drehzahl %u%% · RPM %u",
          m.weller_uart_age_sec, m.weller_speed_percent, m.weller_fan_rpm);
      } else {
        snprintf(buf, sizeof(buf), "UART Status unbekannt");
      }
      return String(buf);
    case MODULE_DISPLAY:
      snprintf(buf, sizeof(buf), "CPU %u%% · Loop max %ums",
        m.module_cpu_load_pct, m.module_loop_max_ms);
      return String(buf);
    case MODULE_UNIVERSAL_RS232:
      snprintf(buf, sizeof(buf), "Descriptor %s · %u Entities",
        m.universal_descriptor_valid ? "OK" : "-", m.universal_entity_count);
      return String(buf);
    case MODULE_MODBUS_RTU:
      snprintf(buf, sizeof(buf), "Descriptor %s · %u Entities",
        m.universal_descriptor_valid ? "OK" : "-", m.universal_entity_count);
      return String(buf);
    default:
      return String("-");
  }
}

static void web_handle_diagnostics_state() {
  scheduler.traceTouch();
  String view = web.arg("view");
  view.toLowerCase();
  const bool local_view = view == "local";
  MasterScheduler::TraceStats st = scheduler.traceStats();
  String json;
  json.reserve(52000);
  json += F("{\"active\":"); json += st.active ? F("true") : F("false");
  json += F(",\"target_addr\":"); json += st.target_addr;
  json += F(",\"view\":\""); json += local_view ? F("local") : F("rs485"); json += '"';
  json += F(",\"started_ms\":"); json += st.started_ms;
  json += F(",\"requests\":"); json += st.requests;
  json += F(",\"responses\":"); json += st.responses;
  json += F(",\"timeouts\":"); json += st.timeouts;
  json += F(",\"dropped\":"); json += st.dropped_events;
  json += F(",\"bad_seq\":"); json += st.bad_seq;
  json += F(",\"bad_cmd\":"); json += st.bad_cmd;
  json += F(",\"request_bad_seq_total\":"); json += scheduler.requestBadSeqCount();
  json += F(",\"request_bad_cmd_total\":"); json += scheduler.requestBadCmdCount();
  json += F(",\"request_total\":"); json += scheduler.requestCount();
  {
    char bus_bytes_buf[24];
    snprintf(bus_bytes_buf, sizeof(bus_bytes_buf), "%llu",
      (unsigned long long)scheduler.requestTxPayloadBytes());
    json += F(",\"request_tx_payload_bytes\":"); json += bus_bytes_buf;
    snprintf(bus_bytes_buf, sizeof(bus_bytes_buf), "%llu",
      (unsigned long long)scheduler.responseRxPayloadBytes());
    json += F(",\"response_rx_payload_bytes\":"); json += bus_bytes_buf;
  }
  json += F(",\"io_compact_polls\":"); json += scheduler.compactIoPollCount();
  json += F(",\"io_full_polls\":"); json += scheduler.fullIoPollCount();
  json += F(",\"sample_ms\":"); json += millis();
  json += F(",\"ofe_baud\":250000");
  {
    char nbuf[24];
    snprintf(nbuf, sizeof(nbuf), "%llu", (unsigned long long)scheduler.ofeTxWireBytes());
    json += F(",\"ofe_tx_wire_bytes\":"); json += nbuf;
    snprintf(nbuf, sizeof(nbuf), "%llu", (unsigned long long)scheduler.ofeRxWireBytes());
    json += F(",\"ofe_rx_wire_bytes\":"); json += nbuf;
  }
  json += F(",\"ofe_tx_frames\":"); json += scheduler.ofeTxFrameCount();
  json += F(",\"ofe_rx_frames\":"); json += scheduler.ofeRxFrameCount();
  {
    const jbc_rs485::BusStats& bs = scheduler.ofeParserStats();
    json += F(",\"ofe_crc_errors\":"); json += bs.crc_errors;
    json += F(",\"ofe_bad_length\":"); json += bs.bad_length;
    json += F(",\"ofe_bad_version\":"); json += bs.bad_version;
    json += F(",\"ofe_escape_errors\":"); json += bs.escape_errors;
    json += F(",\"ofe_overflow_errors\":"); json += bs.overflow_errors;
    json += F(",\"ofe_short_frames\":"); json += bs.short_frames;
  }
  json += F(",\"cmdq_processed\":"); json += master_command_queue_processed;
  json += F(",\"cmdq_rejects\":"); json += master_command_queue_rejects;
  json += F(",\"cmdq_max_depth\":"); json += master_command_queue_max_depth;
  json += F(",\"prefs_begin_count\":"); json += master_prefs.beginCount();
  json += F(",\"prefs_contention\":"); json += master_prefs.contentionCount();
  json += F(",\"prefs_max_wait_us\":"); json += master_prefs.maxWaitUs();
  json += F(",\"extmem_malloc_enabled\":"); json += master_extmem_malloc_enabled ? F("true") : F("false");
  json += F(",\"extmem_malloc_threshold\":"); json += master_extmem_malloc_threshold;
  json += F(",\"psram_total\":"); json += master_psram_total_at_boot;
  json += F(",\"psram_free\":"); json += ESP.getFreePsram();
  json += F(",\"psram_largest_block\":");
  json += heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  json += F(",\"mqtt_manifest_loaded\":"); json += mqtt_manifest_previous_valid ? F("true") : F("false");
  json += F(",\"mqtt_manifest_seed_total\":"); json += mqtt_manifest_seed_total;
  json += F(",\"mqtt_manifest_save_total\":"); json += mqtt_manifest_save_total;
  json += F(",\"mqtt_cleanup_active\":"); json += mqtt_manifest_cleanup_active ? F("true") : F("false");
  json += F(",\"mqtt_cleanup_cycles\":"); json += mqtt_manifest_cleanup_cycles;
  json += F(",\"mqtt_cleanup_delete_total\":"); json += mqtt_manifest_delete_total;
  json += F(",\"mqtt_cleanup_delete_failed\":"); json += mqtt_manifest_delete_failed;
  json += F(",\"mqtt_cleanup_last_stale\":"); json += mqtt_manifest_last_stale_count;
  json += F(",\"mqtt_cleanup_last_deleted\":"); json += mqtt_manifest_last_delete_count;
  json += F(",\"mqtt_manifest_untracked\":"); json += mqtt_manifest_untracked_total;
  json += F(",\"avg_latency_ms\":"); json += st.avg_latency_ms;
  json += F(",\"max_latency_ms\":"); json += st.max_latency_ms;
  json += F(",\"stored\":"); json += st.stored_events;
  json += F(",\"trace_oldest_seq\":"); json += scheduler.traceOldestSeq();
  json += F(",\"trace_newest_seq\":"); json += scheduler.traceNewestSeq();
  json += F(",\"web_event_source_limit\":1100");
  json += F(",\"web_row_limit\":500");
  json += F(",\"psram\":"); json += scheduler.traceUsesPsram() ? F("true") : F("false");
  json += F(",\"modules\":[");
  for (uint8_t i = 0; i < registry.count(); ++i) {
    if (i) json += ',';
    const ModuleRecord& m = registry.at(i);
    MasterScheduler::BusModuleDiag bd;
    scheduler.busModuleDiag(m.addr, bd);
    const String device_detail = diagnostics_device_detail(m);
    json += F("{\"addr\":"); json += m.addr;
    json += F(",\"type\":"); json += m.type;
    json += F(",\"type_name\":\""); json += module_type_name_for(m); json += '"';
    json += F(",\"name\":\""); { const String dn = module_display_name(m); json += json_escape(dn.c_str()); } json += '"';
    json += F(",\"online\":"); json += m.online ? F("true") : F("false");
    json += F(",\"transport\":\"");
    json += (m.type == MODULE_DISPLAY && master_display_wifi.active(m.addr)) ? F("wifi") : F("rs485");
    json += '"';
    json += F(",\"caps\":"); json += m.caps;
    json += F(",\"device_bus\":\""); json += diagnostics_device_bus_name(m.type); json += '"';
    json += F(",\"device_detail\":\""); json += json_escape(device_detail.c_str()); json += '"';
    if (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) {
      char v_profile[48], v_station[48], v_protocol[32], v_checksum[32], v_slave[12];
      diagnostics_descriptor_header_value(m, "profile", v_profile, sizeof(v_profile));
      diagnostics_descriptor_header_value(m, "station", v_station, sizeof(v_station));
      diagnostics_descriptor_header_value(m, "protocol", v_protocol, sizeof(v_protocol));
      diagnostics_descriptor_header_value(m, "checksum", v_checksum, sizeof(v_checksum));
      diagnostics_descriptor_header_value(m, "slave", v_slave, sizeof(v_slave));
      json += F(",\"local_profile\":\""); json += json_escape(v_profile); json += '"';
      json += F(",\"local_station\":\""); json += json_escape(v_station); json += '"';
      json += F(",\"local_protocol\":\""); json += json_escape(v_protocol); json += '"';
      json += F(",\"local_checksum\":\""); json += json_escape(v_checksum); json += '"';
      json += F(",\"local_slave\":\""); json += json_escape(v_slave); json += '"';
      diagnostics_append_trace_defs_json(json, m);
    }
    json += F(",\"local_trace\":"); json += (m.caps & CAP_LOCAL_TRACE) ? F("true") : F("false");
    if (m.type == MODULE_JBC_USB) {
      json += F(",\"jbc_local\":{");
      json += F("\"link_state\":"); json += m.jbc_usb_link_state;
      json += F(",\"model\":\""); json += json_escape(m.jbc_usb_model_raw); json += '"';
      json += F(",\"model_short\":\""); json += json_escape(m.jbc_usb_model); json += '"';
      const uint8_t jbc_diag_family = jbc_usb_core_family_for_model(m.jbc_usb_model);
      json += F(",\"family\":\""); json += jbc_usb_core_family_name(jbc_diag_family); json += '"';
      json += F(",\"family_code\":"); json += jbc_diag_family;
      json += F(",\"frame_protocol\":"); json += m.jbc_usb_frame_protocol;
      json += F(",\"command_protocol\":"); json += m.jbc_usb_command_protocol;
      json += F(",\"state_age_ms\":"); json += m.jbc_usb_state_last_ms ? (uint32_t)(millis() - m.jbc_usb_state_last_ms) : 0xFFFFFFFFUL;
      json += F(",\"usb_rx_bytes\":"); json += m.jbc_usb_usb_rx_bytes;
      json += F(",\"usb_tx_bytes\":"); json += m.jbc_usb_usb_tx_bytes;
      json += F(",\"jbc_rx_frames\":"); json += m.jbc_usb_rx_frames;
      json += F(",\"jbc_tx_frames\":"); json += m.jbc_usb_tx_frames;
      json += F(",\"usb_errors\":"); json += m.jbc_usb_usb_errors;
      json += F(",\"bcc_errors\":"); json += m.jbc_usb_bcc_errors;
      json += F(",\"frame_errors\":"); json += m.jbc_usb_frame_errors;
      json += F(",\"decode_errors\":"); json += m.jbc_usb_decode_errors;
      json += F(",\"handshake_errors\":"); json += m.jbc_usb_handshake_errors;
      json += F(",\"decode_last_cmd\":"); json += m.jbc_usb_decode_last_cmd;
      json += F(",\"decode_last_got_len\":"); json += m.jbc_usb_decode_last_got_len;
      json += F(",\"decode_last_expected_min\":"); json += m.jbc_usb_decode_last_expected_min;
      json += F(",\"decode_last_expected_max\":"); json += m.jbc_usb_decode_last_expected_max;
      json += F(",\"decode_top\":[");
      for (uint8_t rank=0; rank<3; ++rank) { if(rank) json += ','; json += '['; json += m.jbc_usb_decode_top_cmd[rank]; json += ','; json += m.jbc_usb_decode_top_count[rank]; json += ']'; }
      json += ']';
      json += F(",\"cp_valid\":"); json += m.jbc_usb_cp_diag_valid ? F("true") : F("false");
      json += F(",\"cp_baud\":"); json += m.jbc_usb_cp_baud;
      json += F(",\"cp_line_ctl\":"); json += m.jbc_usb_cp_line_ctl;
      json += F(",\"cp_mdmsts\":"); json += m.jbc_usb_cp_mdmsts;
      json += F(",\"cp_comm_errors\":"); json += m.jbc_usb_cp_comm_errors;
      json += F(",\"cp_hold_reasons\":"); json += m.jbc_usb_cp_hold_reasons;
      json += F(",\"cp_in_queue\":"); json += m.jbc_usb_cp_in_queue;
      json += F(",\"cp_out_queue\":"); json += m.jbc_usb_cp_out_queue;
      json += '}';
    }
    json += F(",\"requests\":"); json += bd.requests;
    json += F(",\"responses\":"); json += bd.responses;
    json += F(",\"timeouts\":"); json += bd.timeouts;
    json += F(",\"bad_seq\":"); json += bd.bad_seq;
    json += F(",\"bad_cmd\":"); json += bd.bad_cmd;
    json += F(",\"tx_frames\":"); json += bd.tx_frames;
    json += F(",\"rx_frames\":"); json += bd.rx_frames;
    {
      char nbuf[24];
      snprintf(nbuf, sizeof(nbuf), "%llu", (unsigned long long)bd.tx_wire_bytes);
      json += F(",\"tx_wire_bytes\":"); json += nbuf;
      snprintf(nbuf, sizeof(nbuf), "%llu", (unsigned long long)bd.rx_wire_bytes);
      json += F(",\"rx_wire_bytes\":"); json += nbuf;
    }
    json += F(",\"latency_avg_ms\":");
    json += bd.responses ? (uint32_t)(bd.latency_sum_ms / bd.responses) : 0;
    json += F(",\"latency_last_ms\":"); json += bd.latency_last_ms;
    json += F(",\"latency_max_ms\":"); json += bd.latency_max_ms;
    json += F(",\"last_activity_ms\":"); json += bd.last_activity_ms;
    json += '}';
  }
  json += F("]}");
  web.send(200, "application/json", json);
}

// Trace payload is intentionally split from /diagnostics/state. The ESP32
// WebServer handler runs synchronously on loopTask; serializing hundreds of
// PSRAM-backed TraceEvent records into one giant JSON string every second used
// to stall scheduler/RS485 service. This endpoint scans a strictly bounded
// number of events and lets the browser advance with a sequence cursor.
static void web_handle_diagnostics_events() {
  scheduler.traceTouch();
  String view = web.arg("view");
  view.toLowerCase();
  const bool local_view = view == "local";

  uint32_t after_seq = 0;
  const String after_arg = web.arg("after_seq");
  if (after_arg.length()) after_seq = (uint32_t)strtoul(after_arg.c_str(), nullptr, 10);

  uint16_t scan_limit = 48;
  const String limit_arg = web.arg("limit");
  if (limit_arg.length()) {
    const long requested = strtol(limit_arg.c_str(), nullptr, 10);
    if (requested >= 16 && requested <= 48) scan_limit = (uint16_t)requested;
  }

  const MasterScheduler::TraceStats st = scheduler.traceStats();
  const uint16_t count = scheduler.traceEventCount();
  const uint32_t oldest_seq = scheduler.traceOldestSeq();
  const uint32_t newest_seq = scheduler.traceNewestSeq();
  uint32_t cursor_seq = after_seq;
  bool reset = false;

  uint16_t start_index = count;
  if (count && newest_seq) {
    uint32_t wanted_seq = after_seq ? (after_seq + 1UL) : oldest_seq;
    if (wanted_seq < oldest_seq) {
      wanted_seq = oldest_seq;
      reset = true;
    }
    if (wanted_seq <= newest_seq) {
      const uint32_t offset = wanted_seq - oldest_seq;
      if (offset < count) start_index = (uint16_t)offset;
    }
  }

  String json;
  json.reserve(30000);
  json += F("{\"view\":\""); json += local_view ? F("local") : F("rs485"); json += '"';
  json += F(",\"epoch\":"); json += st.started_ms;
  json += F(",\"oldest_seq\":"); json += oldest_seq;
  json += F(",\"newest_seq\":"); json += newest_seq;
  json += F(",\"reset\":"); json += reset ? F("true") : F("false");
  json += F(",\"events\":[");

  bool first_event = true;
  uint16_t scanned = 0;
  for (uint16_t index = start_index; index < count && scanned < scan_limit; ++index, ++scanned) {
    MasterScheduler::TraceEvent ev;
    if (!scheduler.traceEventAt(index, ev)) continue;
    cursor_seq = ev.seq; // advance even across events filtered for the other view
    const bool is_local = ev.direction == MasterScheduler::TRACE_LOCAL_TX || ev.direction == MasterScheduler::TRACE_LOCAL_RX;
    if (local_view != is_local) continue;
    if (!first_event) json += ',';
    first_event = false;
    json += F("{\"ms\":"); json += ev.ms;
    json += F(",\"seq\":"); json += ev.seq;
    json += F(",\"frame_seq\":"); json += ev.frame_seq;
    json += F(",\"addr\":"); json += ev.addr;
    json += F(",\"dir\":\""); json += trace_dir_name(ev.direction); json += '"';
    json += F(",\"cmd\":\""); json += trace_cmd_name(ev.cmd); json += '"';
    json += F(",\"status\":"); json += ev.status;
    json += F(",\"len\":"); json += ev.len;
    json += F(",\"latency\":"); json += ev.latency_ms;
    json += F(",\"data\":\""); json += bytes_hex(ev.data, ev.data_len); json += '"';
    json += F(",\"text\":\""); json += json_escape(ev.text); json += F("\"}");
  }
  json += F("]");
  json += F(",\"cursor_seq\":"); json += cursor_seq;
  json += F(",\"more\":"); json += (cursor_seq < newest_seq) ? F("true") : F("false");
  json += F("}");
  web.send(200, "application/json", json);
}
static void web_handle_diagnostics_control() {
  String action = web.arg("action");
  action.toLowerCase();
  if (action == "start") {
    String addr_arg = web.arg("addr");
    uint8_t addr = 0;
    if (addr_arg.length()) addr = (uint8_t)strtoul(addr_arg.c_str(), nullptr, 0);
    const String view = web.arg("view");
    if (!master_cmd_trace_start(addr, view == "local")) {
      web.send(503, "text/plain; charset=utf-8", "trace start failed");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "trace started");
    return;
  }
  if (action == "stop") {
    if (!master_cmd_trace_stop()) {
      web.send(503, "text/plain; charset=utf-8", "trace stop failed");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "trace stopped");
    return;
  }
  if (action == "clear") {
    if (!master_cmd_trace_clear()) {
      web.send(503, "text/plain; charset=utf-8", "trace clear failed");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "trace cleared");
    return;
  }
  web.send(400, "text/plain; charset=utf-8", "bad action");
}

static void web_handle_diagnostics() {
  String html;
  html.reserve(26000);
  web_shell_begin(html, web_text("Bus Diagnose", "Bus Diagnostics"), web_text("Analyse", "Analysis"), "diagnostics");

  html += F("<p class='muted'>");
  html += web_text(
    "OFE-Bus und lokale Gerätebusse getrennt analysieren. Transport, Modul und angeschlossenes Gerät bleiben optisch klar voneinander getrennt.",
    "Analyze the OFE bus and local device buses separately. Transport, module and attached device remain visually separated.");
  html += F("</p>");



  html += F("<section class='panel'><div class='diag-panel-title diag-ofe-panel-title'><div><h2>OFE Bus</h2><p class='muted'>");
  html += web_text("Master ↔ Module · 250 kBaud · Live-Raten aus aufeinanderfolgenden Diagnose-Samples.", "Master ↔ modules · 250 kbaud · live rates from consecutive diagnostics samples.");
  html += F("</p></div><div class='diag-master-led-pair' title='Master Status LEDs'><span id='diag_master_led_ofe' class='diag-led-word'>OFE</span><span id='diag_master_led_evt' class='diag-led-word'>EVT</span></div><span class='diag-badge'>OFE</span></div><div class='stat-grid diag-stat-grid'>");
  html += F("<div class='stat'><div class='k'>"); html += web_text("Zustand", "State"); html += F("</div><div id='diag_health' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>"); html += web_text("Buslast", "Bus load"); html += F("</div><div id='diag_busload' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>TX / RX</div><div id='diag_rate' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>Frames/s</div><div id='diag_fps' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>"); html += web_text("Trace-Latenz", "Trace latency"); html += F("</div><div id='diag_latency' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>"); html += web_text("Transportfehler seit Boot", "Transport errors since boot"); html += F("</div><div id='diag_transport_errors' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>"); html += web_text("Antwortfehler seit Boot", "Response errors since boot"); html += F("</div><div id='diag_bad' class='v'>-</div></div>");
  html += F("<div class='stat'><div class='k'>"); html += web_text("Trace-Puffer", "Trace buffer"); html += F("</div><div id='diag_buffer' class='v'>-</div></div>");
  html += F("</div></section>");

  html += F("<section class='panel'><div class='diag-panel-title'><div><h2>");
  html += web_text("Bus-Topologie", "Bus topology");
  html += F("</h2><p class='muted'>");
  html += web_text("OFE-Transport oben, modulspezifischer Gerätebus darunter. Raten sind Live-Werte, Zähler bleiben seit Boot erhalten.", "OFE transport on top, module-specific device bus below. Rates are live values; counters remain since boot.");
  html += F("</p></div></div><div id='diag_module_cards' class='diag-module-grid'></div></section>");

  html += F("<section class='panel diag-session'><div class='diag-session-head'><div><div class='diag-title-row'><h2>");
  html += web_text("Diagnose-Modus", "Diagnostics Mode");
  html += F("</h2><span id='diag_session_state' class='diag-session-state stopped'><span class='diag-state-dot'></span>");
  html += web_text("gestoppt", "stopped");
  html += F("</span></div><p class='muted' id='diag_hint'>-</p></div></div>");

  html += F("<div class='diag-mode-cards'>");
  html += F("<button type='button' class='diag-mode-card' id='diag_mode_rs485' onclick='diagSetView(&quot;rs485&quot;)'><span class='diag-mode-icon'>OFE</span><span class='diag-mode-copy'><strong>OFE Bus</strong><small>");
  html += web_text("Master ↔ Module", "Master ↔ modules");
  html += F("</small></span><span class='diag-mode-check'>✓</span></button>");
  html += F("<button type='button' class='diag-mode-card' id='diag_mode_local' onclick='diagSetView(&quot;local&quot;)'><span class='diag-mode-icon local'>DEV</span><span class='diag-mode-copy'><strong>");
  html += web_text("Gerätebus", "Device bus");
  html += F("</strong><small>");
  html += web_text("Modul ↔ Gerät", "Module ↔ device");
  html += F("</small></span><span class='diag-mode-check'>✓</span></button></div>");

  html += F("<div class='diag-control-row'><div class='diag-target-wrap'><label>");
  html += web_text("Zielmodul", "Target module");
  html += F("</label><select id='diag_addr'><option value='0'>");
  html += web_text("Alle Module", "All modules");
  html += F("</option>");
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    html += F("<option value='0x");
    if (m.addr < 0x10) html += '0';
    html += String(m.addr, HEX);
    html += F("'>0x");
    if (m.addr < 0x10) html += '0';
    html += String(m.addr, HEX);
    html += ' ';
    html += html_escape(module_display_name(m));
    html += F("</option>");
  }
  html += F("</select></div><input type='hidden' id='diag_view' value='rs485'>");
  html += F("<div class='diag-session-actions'><button id='diag_start_btn' class='diag-start-btn' onclick='diagStart()'><span class='diag-btn-dot'></span>");
  html += web_text("Trace starten", "Start trace");
  html += F("</button><button id='diag_stop_btn' class='secondary' onclick='diagStop()'>");
  html += web_text("Stoppen", "Stop");
  html += F("</button><button class='secondary' onclick='diagClear()'>");
  html += web_text("Leeren", "Clear");
  html += F("</button></div>");
  html += F("<div class='diag-export-actions'><button class='secondary' onclick='diagDownload(&quot;csv&quot;)'>CSV</button><button class='secondary' onclick='diagDownload(&quot;json&quot;)'>JSON</button></div></div>");

  html += F("<div class='diag-filterbar'><div class='diag-search-wrap'><span class='diag-search-icon'>⌕</span><input id='diag_filter' type='search' placeholder='");
  html += web_text("Trace durchsuchen: 0x40, DISPLAY_STATUS, Weller...", "Search trace: 0x40, DISPLAY_STATUS, Weller...");
  html += F("' oninput='diagApplyFilter()'><button type='button' class='diag-search-clear' onclick='diagClearSearch()' title='");
  html += web_text("Suche löschen", "Clear search");
  html += F("'>×</button></div><div class='diag-filter-chips' role='group' aria-label='Trace Filter'>");
  html += F("<button type='button' id='diag_q_all' class='diag-filter-chip active' onclick='diagSetQuickFilter(&quot;all&quot;)'>");
  html += web_text("Alle", "All");
  html += F("</button><button type='button' id='diag_q_tx' class='diag-filter-chip' onclick='diagSetQuickFilter(&quot;tx&quot;)'>TX</button>");
  html += F("<button type='button' id='diag_q_rx' class='diag-filter-chip' onclick='diagSetQuickFilter(&quot;rx&quot;)'>RX</button>");
  html += F("<button type='button' id='diag_q_errors' class='diag-filter-chip' onclick='diagSetQuickFilter(&quot;errors&quot;)'>");
  html += web_text("Fehler", "Errors");
  html += F("</button><button type='button' id='diag_q_timeout' class='diag-filter-chip' onclick='diagSetQuickFilter(&quot;timeout&quot;)'>Timeout</button></div>");
  html += F("<div id='diag_visible_count' class='diag-visible-count'>0 Events</div></div><div id='diag_control_error' class='diag-control-error'></div></section>");


  html += F("<section id='diag_rs485_panel' class='panel diag-log-panel'><div class='diag-trace-head'><div><div class='diag-title-row'><h2>OFE Bus Trace</h2><span class='diag-badge'>OFE</span></div><p class='muted'>");
  html += web_text("Requests und Responses als zusammenhängende Transaktionen oder vollständig als Roh-Trace.", "Requests and responses as combined transactions or as the complete raw trace.");
  html += F("</p></div><div class='diag-trace-switch'><button id='diag_pairs_btn' class='active' onclick='diagSetTraceLayout(&quot;pairs&quot;)'>");
  html += web_text("Transaktionen", "Transactions");
  html += F("</button><button id='diag_raw_btn' onclick='diagSetTraceLayout(&quot;raw&quot;)'>Raw</button></div></div>");
  html += F("<div class='diag-trace-legend'><span><span class='diag-legend-dot tx'></span>TX Request</span><span><span class='diag-legend-dot rx'></span>RX Response</span><span><span class='diag-legend-dot timeout'></span>Timeout</span><span class='diag-trace-note'>");
  html += web_text("Payload anklicken zum Aufklappen", "Click payload to expand");
  html += F("</span></div>");
  html += F("<div id='diag_pairs_wrap' class='table-wrap diag-scroll'><table class='diag-table diag-pair-table'><thead><tr><th>#</th><th>");
  html += web_text("Zeit", "Time");
  html += F("</th><th>Addr</th><th>");
  html += web_text("Befehl", "Command");
  html += F("</th><th>Status</th><th>TX / RX</th><th>");
  html += web_text("Latenz", "Latency");
  html += F("</th><th>"); html += web_text("Klartext", "Decoded"); html += F("</th><th>Payload</th></tr></thead><tbody id='diag_pair_rows'></tbody></table></div>");
  html += F("<div id='diag_raw_wrap' class='table-wrap diag-scroll' style='display:none'><table class='diag-table'><thead><tr><th>#</th><th>ms</th><th>SEQ</th><th>Addr</th><th>Dir</th><th>Cmd</th><th>Status</th><th>Len</th><th>Lat</th><th>");
  html += web_text("Klartext", "Decoded");
  html += F("</th><th>Payload</th></tr></thead><tbody id='diag_rows'></tbody></table></div></section>");

  html += F("<section id='diag_local_panel' class='panel diag-log-panel'><div class='diag-trace-head'><div><div class='diag-title-row'><h2>");
  html += web_text("Gerätebus Trace", "Device bus trace");
  html += F("</h2><span class='diag-badge local'>LOCAL</span></div><p class='muted'>");
  html += web_text("JBC-, Weller-, Universal- und Modbus-Frames werden protokoll- bzw. descriptorbasiert zu Transaktionen zusammengeführt. Rohframes bleiben jederzeit verfügbar.", "JBC, Weller, Universal and Modbus frames are combined into protocol- or descriptor-aware transactions. Raw frames remain available at all times.");
  html += F("</p></div><div class='diag-trace-switch'><button id='diag_local_txn_btn' class='active' onclick='diagSetLocalLayout(&quot;transactions&quot;)'>");
  html += web_text("Transaktionen", "Transactions");
  html += F("</button><button id='diag_local_raw_btn' onclick='diagSetLocalLayout(&quot;raw&quot;)'>");
  html += web_text("Rohframes", "Raw frames");
  html += F("</button></div></div>");

  html += F("<div id='diag_local_summary' class='diag-local-summary muted'>-</div>");
  html += F("<div class='diag-local-toolbar'><div id='diag_local_stats' class='diag-local-stats'></div><button type='button' id='diag_local_changes_btn' class='diag-filter-chip' onclick='diagToggleLocalChanges()'>");
  html += web_text("Wiederholungen ausblenden", "Hide repeats");
  html += F("</button></div>");

  html += F("<div id='diag_local_txn_wrap' class='table-wrap diag-scroll'><table class='diag-table diag-local-table diag-local-txn-table'><thead><tr><th>#</th><th>");
  html += web_text("Zeit", "Time");
  html += F("</th><th>");
  html += web_text("Protokoll", "Protocol");
  html += F("</th><th>");
  html += web_text("Befehl", "Command");
  html += F("</th><th>");
  html += web_text("Ablauf", "Flow");
  html += F("</th><th>");
  html += web_text("Ergebnis", "Result");
  html += F("</th><th>Δt</th><th>");
  html += web_text("Details", "Details");
  html += F("</th></tr></thead><tbody id='uart_txn_rows'></tbody></table></div>");

  html += F("<div id='diag_local_raw_wrap' class='table-wrap diag-scroll' style='display:none'><table class='diag-table diag-local-table'><thead><tr><th>#</th><th>");
  html += web_text("Event-Zeit", "Event time");
  html += F("</th><th>Dir</th><th>");
  html += web_text("Protokoll", "Protocol");
  html += F("</th><th>");
  html += web_text("Frame / Befehl", "Frame / Command");
  html += F("</th><th>");
  html += web_text("Details", "Details");
  html += F("</th><th>");
  html += web_text("Abruf-Age", "Fetch age");
  html += F("</th><th>");
  html += web_text("Rohdaten", "Raw data");
  html += F("</th></tr></thead><tbody id='uart_rows'></tbody></table></div></section>");

  html += F(R"CSS(<style>
.diag-session{border-color:#28415f}
.diag-mode-head,.diag-panel-title{display:flex;align-items:center;justify-content:space-between;gap:16px}
.diag-ofe-panel-title{position:relative}.diag-master-led-pair{position:absolute;left:50%;top:2px;transform:translateX(-50%);display:flex;align-items:center;justify-content:center;gap:18px;min-height:30px;padding:3px 12px;border:1px solid #2b3139;border-radius:8px;background:#161a20}
.diag-mode-switch,.diag-trace-switch{display:flex;gap:6px;background:#0b1119;border:1px solid var(--line);border-radius:12px;padding:4px}
.diag-mode-switch button,.diag-trace-switch button{border:0;background:transparent;color:var(--muted);padding:9px 13px;border-radius:9px}
.diag-mode-switch button.active,.diag-trace-switch button.active{background:#27659f;color:white}
.diag-control-grid{align-items:end}.diag-actions{display:flex;flex-wrap:wrap}
.diag-badge{font-size:11px;font-weight:900;letter-spacing:.08em;padding:6px 10px;border-radius:999px;background:#12304d;color:#8fd0ff}
.diag-badge.local{background:#17341f;color:#8be8a0}
.diag-stat-grid{grid-template-columns:repeat(auto-fit,minmax(150px,1fr))}
.diag-scroll{max-height:660px;overflow:auto;border:1px solid var(--line);border-radius:12px}
.diag-table{width:100%;border-collapse:collapse;font-size:13px;min-width:1080px}
.diag-pair-table{min-width:1160px}
.diag-scroll thead th{position:sticky;top:0;background:var(--panel);z-index:1}
th,td{border-bottom:1px solid var(--line);padding:8px 10px;text-align:left;vertical-align:top}
th{color:var(--muted);font-size:11px;text-transform:uppercase}
.diag-dir,.diag-status,.diag-mini-badge{display:inline-flex;align-items:center;border-radius:999px;padding:2px 8px;font-size:11px;font-weight:800;background:#1b2430}
.tx{color:#7db7ff}.rx{color:#66e38a}.uart_tx{color:#8fd7ff}.uart_rx{color:#b6f080}.timeout{color:#ff7782}.info{color:#f2c96b}
.diag-status.ok{color:#72e68d;background:#13281a}.diag-status.warn{color:#f2c96b;background:#2b2613}.diag-status.err{color:#ff8b93;background:#30171a}
.data{font-family:Consolas,monospace;color:#aeb8c5;word-break:break-all}.decoded{min-width:300px;line-height:1.35}
.diag-log-panel{margin-top:14px}.diag-filter-hit{background:rgba(255,209,102,.06)}#diag_filter{min-width:220px}
.diag-module-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}
.diag-module-card{border:1px solid var(--line);border-radius:14px;background:#0c131c;overflow:hidden}
.diag-module-head{display:flex;justify-content:space-between;gap:10px;padding:12px 14px;border-bottom:1px solid var(--line)}
.diag-module-name{font-weight:800}.diag-module-type{font-size:12px;color:var(--muted);margin-top:2px}
.diag-led-pair{display:flex;align-items:center;justify-content:center;gap:18px;min-height:32px;padding:5px 16px;border-bottom:1px solid #2b3139;background:#161a20}
.diag-led-word{display:inline-block;min-width:40px;text-align:center;font-size:12px;font-weight:900;letter-spacing:.14em;color:#59616b;opacity:.58;text-shadow:none;transition:none}
.diag-led-word.is-live{opacity:1;color:#e8edf2;animation:none!important;transition:none}
.diag-led-word.fx-breath{animation:diagLedBreath var(--led-duration,3200ms) linear infinite}.diag-led-word.fx-whitebreath{animation:diagLedWhiteBreath var(--led-duration,3200ms) linear infinite}.diag-led-word.fx-greenwhite{animation:diagLedGreenWhite var(--led-duration,3200ms) linear infinite}.diag-led-word.fx-bluewhite{animation:diagLedBlueWhite var(--led-duration,3200ms) linear infinite}.diag-led-word.fx-blink{animation:diagLedBlink var(--led-duration,1000ms) linear infinite}.diag-led-word.fx-double{animation:diagLedDouble var(--led-duration,900ms) linear infinite}
@keyframes diagLedBreath{0%,100%{opacity:.094;text-shadow:0 0 1px var(--led-glow,#fff)}50%{opacity:1;text-shadow:0 0 6px var(--led-glow,#fff),0 0 14px var(--led-glow,#fff),0 0 22px var(--led-glow,#fff)}}
@keyframes diagLedWhiteBreath{0%,100%{opacity:.063;text-shadow:0 0 1px #fff}50%{opacity:1;text-shadow:0 0 6px #fff,0 0 14px #fff,0 0 22px #fff}}
@keyframes diagLedGreenWhite{0%,100%{opacity:1;color:#00ff00;text-shadow:0 0 5px #00ff00,0 0 12px #00ff00,0 0 18px #00ff00}50%{opacity:1;color:#ffffff;text-shadow:0 0 5px #ffffff,0 0 13px #ffffff,0 0 21px #ffffff}}
@keyframes diagLedBlueWhite{0%,100%{opacity:1;color:#0024ff;text-shadow:0 0 5px #0024ff,0 0 12px #0024ff,0 0 18px #0024ff}50%{opacity:1;color:#ffffff;text-shadow:0 0 5px #ffffff,0 0 13px #ffffff,0 0 21px #ffffff}}
@keyframes diagLedBlink{0%,49.999%{opacity:.12;color:#59616b;text-shadow:none}50%,100%{opacity:1;color:var(--led-color,#fff);text-shadow:0 0 5px var(--led-glow,#fff),0 0 11px var(--led-glow,#fff),0 0 18px var(--led-glow,#fff)}}
@keyframes diagLedDouble{0%,9.999%,20%,29.999%{opacity:1;color:var(--led-color,#fff);text-shadow:0 0 5px var(--led-glow,#fff),0 0 11px var(--led-glow,#fff),0 0 18px var(--led-glow,#fff)}10%,19.999%,30%,100%{opacity:.12;color:#59616b;text-shadow:none}}
.diag-layer{padding:11px 14px}.diag-layer+.diag-layer{border-top:1px solid var(--line)}
.diag-layer-title{display:flex;justify-content:space-between;align-items:center;font-size:12px;font-weight:800;margin-bottom:7px}
.diag-layer-grid{display:grid;grid-template-columns:1fr 1fr;gap:4px 12px;font-size:12px}
.diag-layer-grid .k{color:var(--muted)}.diag-layer-grid .v{text-align:right}
.diag-arrow{text-align:center;color:var(--muted);font-size:18px;line-height:15px;margin:-3px 0}
.diag-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:#69717d}
.diag-dot.ok{background:#5cdb78}.diag-dot.err{background:#ff6570}
.diag-local-summary{margin:0 0 12px;padding:10px 12px;border:1px solid var(--line);border-radius:10px;background:#0c131c}
.diag-payload details{max-width:360px}.diag-payload summary{cursor:pointer;color:#8fb9df}.diag-payload-line{margin-top:5px}
.diag-rate-muted{color:var(--muted);font-size:11px}
@media(max-width:760px){.diag-mode-head,.diag-panel-title{align-items:flex-start;flex-direction:column}.diag-ofe-panel-title{padding-top:2px}.diag-master-led-pair{position:static;transform:none;align-self:center}.diag-module-grid{grid-template-columns:1fr}}

/* v1.7.46 Status-style diagnostic module cards + trace switch fix */
.diag-trace-switch{min-width:270px;width:auto;max-width:100%;flex:0 0 auto}
.diag-trace-switch button{flex:1 1 0;min-width:122px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;padding:9px 14px}
.diag-module-grid{grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px;align-items:stretch}
.diag-module-card{--diag-accent:#4a90d9;display:flex;flex-direction:column;min-height:0;border:1px solid #343b45!important;border-radius:10px;background:#15181d!important;box-shadow:0 10px 26px rgba(0,0,0,.18)!important;overflow:hidden}
.diag-module-card.jbc{--diag-accent:#39c779}.diag-module-card.fan{--diag-accent:#4a90d9}.diag-module-card.weller{--diag-accent:#55b9ca}.diag-module-card.display{--diag-accent:#b58cff}.diag-module-card.universal{--diag-accent:#f1b84b}.diag-module-card.modbus{--diag-accent:#e28b52}
.diag-module-head{display:flex;align-items:center;justify-content:space-between;gap:10px;min-height:64px;padding:14px 16px;border-bottom:1px solid #2b3139;background:#1c2026;box-shadow:inset 4px 0 0 var(--diag-accent)}
.diag-module-identity{display:flex;align-items:center;gap:10px;min-width:0;flex:1 1 auto}
.diag-module-icon{width:34px;min-width:34px;height:34px;flex:0 0 34px;border-radius:8px;display:inline-flex;align-items:center;justify-content:center;background:#252b33;color:var(--diag-accent);font-size:17px;font-weight:850}
.diag-module-icon .module-head-glyph{width:22px;height:22px;display:block;color:currentColor}
.diag-module-copy{min-width:0}.diag-module-name{font-size:15px;font-weight:700;color:#eef3f8;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.diag-module-type{margin-top:3px;color:#7f8b98;font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.diag-mini-badge.diag-online-pill{flex:0 0 78px;width:78px;min-width:78px;height:25px;padding:0 10px;justify-content:center;box-sizing:border-box;background:#303640;color:#aeb7c3;font-size:11px;font-weight:750}
.diag-online-pill.is-online{background:#1f6f3e;color:#bff5ce}.diag-online-pill.is-offline{background:#4a252a;color:#ffc2c7}
.diag-module-dynamic{display:flex;flex-direction:column;flex:1 1 auto;min-height:0}.diag-module-body{padding:14px;display:grid;gap:10px}
.diag-status-metric{display:grid;grid-template-columns:1fr 1fr;gap:0;background:#101318;border:1px solid #2a313a;border-radius:9px;overflow:hidden}
.diag-status-metric>div{min-width:0;padding:11px 12px}.diag-status-metric>div+div{border-left:1px solid #2a313a}
.diag-status-metric .k{font-size:10px;color:#84909d;text-transform:uppercase;letter-spacing:.035em}.diag-status-metric .v{font-size:16px;font-weight:700;line-height:1.2;margin-top:5px;text-align:left;white-space:normal}
.diag-bus-share-value{display:flex;align-items:center;gap:8px}.diag-bus-share-value strong{min-width:43px;font-size:16px;font-variant-numeric:tabular-nums}.diag-bus-share-value .diag-share-track{flex:1 1 auto;width:auto;min-width:50px;height:7px;background:#2b3037;border-radius:99px;overflow:hidden}.diag-bus-share-value .diag-share-fill{display:block!important;height:100%;background:var(--diag-accent);border-radius:99px;transition:width .28s ease}
.diag-errors-line{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:10px 12px;background:#101318;border:1px solid #2a313a;border-radius:9px}.diag-errors-line>.k{font-size:10px;color:#84909d;text-transform:uppercase}.diag-errors-line .diag-metric-split{display:flex;gap:5px}
.diag-device-section{margin-top:auto;border-top:1px solid #2b3139;background:#13161b;padding:11px 14px 12px}
.diag-device-head{display:flex;align-items:center;justify-content:space-between;gap:10px}.diag-device-title{font-size:12px;font-weight:800;color:#dce5ef}.diag-device-detail{min-height:0;margin-top:7px;color:#8e99a5;font-size:12px;line-height:1.4}
.diag-module-card .diag-arrow,.diag-module-card .diag-layer{display:none}
@media(max-width:760px){.diag-trace-head{align-items:stretch}.diag-trace-switch{width:100%;min-width:0}.diag-trace-switch button{min-width:0}.diag-module-grid{grid-template-columns:1fr}}

/* v1.7.50 Raw-frame payload box wrapping */
.jbc-byte-map{
  grid-template-columns:repeat(5,max-content) minmax(0,1fr);
  width:100%;
  min-width:0;
}
.jbc-byte-field{min-width:0;max-width:100%;box-sizing:border-box}
.jbc-byte-field.payload{
  min-width:0;
  width:100%;
  overflow:hidden;
}
.jbc-byte-field.payload .k{
  display:block;
  max-width:100%;
  overflow:hidden;
  text-overflow:ellipsis;
  white-space:nowrap;
}
.jbc-byte-field.payload .v{
  display:block;
  width:100%;
  max-width:100%;
  min-width:0;
  white-space:normal;
  overflow-wrap:anywhere;
  word-break:break-word;
}
.diag-raw-details,
.diag-raw-details[open],
.diag-payload details,
.diag-payload details[open]{
  max-width:100%;
  box-sizing:border-box;
  overflow:hidden;
}
.diag-payload-line{
  max-width:100%;
  min-width:0;
  overflow-wrap:anywhere;
  word-break:break-word;
}
.diag-payload-line .data{
  white-space:normal;
  overflow-wrap:anywhere;
  word-break:break-word;
}
@media(max-width:900px){
  .jbc-byte-map{
    grid-template-columns:repeat(3,minmax(0,1fr));
  }
  .jbc-byte-field.payload{
    grid-column:1/-1;
  }
}
@media(max-width:620px){
  .jbc-byte-map{
    grid-template-columns:repeat(2,minmax(0,1fr));
  }
}

/* v1.7.49 Trace chip baseline alignment */
.diag-local-table td{vertical-align:middle}
.diag-local-command{justify-content:center}
.diag-local-command .diag-proto-command,
.diag-proto-frame .diag-proto-command,
.jbc-raw-command .diag-proto-command{
  height:26px;
  min-height:26px;
  box-sizing:border-box;
  display:inline-flex;
  align-items:center;
  line-height:1;
}
.jbc-meta-badge,
.diag-check-badge,
.jbc-format-badge,
.diag-mini-badge{
  height:24px;
  min-height:24px;
  box-sizing:border-box;
  display:inline-flex;
  align-items:center;
  line-height:1;
}
.jbc-value,
.diag-proto-field,
.diag-result-value{
  min-height:26px;
  box-sizing:border-box;
  display:inline-flex;
  align-items:center;
  line-height:1.15;
}
.diag-flow-node{
  height:26px;
  min-height:26px;
  box-sizing:border-box;
  line-height:1;
}
.jbc-raw-command,
.jbc-inline-meta,
.jbc-raw-result,
.jbc-result-wrap,
.diag-proto-frame,
.diag-proto-detail,
.diag-result-main,
.diag-flow{
  align-items:center;
}
.jbc-command-stack{justify-content:center}
.jbc-command-stack strong,
.jbc-command-stack small,
.diag-local-command strong,
.diag-local-command small{
  line-height:1.2;
}
.diag-local-command small{
  min-height:12px;
  display:flex;
  align-items:center;
}
.diag-local-txn-table tbody td:nth-child(4),
.diag-local-txn-table tbody td:nth-child(5),
.diag-local-txn-table tbody td:nth-child(6){
  vertical-align:middle;
}

/* v1.7.48 JBC visual alignment with Weller */
.diag-proto-command.jbc{background:#18243a;color:#aecbff}
.jbc-inline-meta{display:flex;align-items:center;gap:5px;flex-wrap:wrap;margin-top:4px}
.jbc-meta-badge{display:inline-flex;align-items:center;min-height:20px;padding:2px 6px;border-radius:7px;background:#111923;border:1px solid var(--line);font:10px Consolas,monospace;color:#aab6c2;white-space:nowrap}
.jbc-result-wrap{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.jbc-result-wrap .jbc-decode{display:flex;align-items:center;gap:5px;flex-wrap:wrap}
.jbc-result-wrap .jbc-value{min-height:24px;padding:3px 7px}
.jbc-result-role{display:inline-flex;align-items:center;height:21px;padding:0 6px;border-radius:999px;background:#1a222c;color:#9caab8;font-size:9px;font-weight:900;text-transform:uppercase}
.jbc-result-role.request{background:#17263b;color:#8ab9f2}
.jbc-result-role.response{background:#14291a;color:#7adf91}
.jbc-raw-command{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.jbc-raw-command .diag-proto-command{max-width:310px;overflow:hidden;text-overflow:ellipsis}
.jbc-raw-result{display:flex;align-items:center;gap:6px;flex-wrap:wrap}

/* v1.7.47 JBC P02 command-aware raw-frame view */
.jbc-frame-line{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.jbc-frame-dir{display:inline-flex;align-items:center;justify-content:center;min-width:34px;height:24px;border-radius:7px;font-size:10px;font-weight:900}
.jbc-frame-dir.rx{background:#142a1b;color:#73e18b}.jbc-frame-dir.tx{background:#17263b;color:#87baff}
.jbc-route{display:inline-flex;align-items:center;gap:5px;font-family:Consolas,monospace;font-size:11px}
.jbc-route-node{display:inline-flex;align-items:center;justify-content:center;min-width:35px;height:24px;padding:0 6px;border-radius:7px;border:1px solid var(--line);background:#0b131c}
.jbc-route-arrow{color:#687c90}
.jbc-command-stack{display:flex;flex-direction:column;gap:3px;min-width:205px}
.jbc-command-stack strong{font-size:12px;line-height:1.2}.jbc-command-stack small{font-size:10px;color:var(--muted);font-family:Consolas,monospace}
.jbc-decode{display:flex;gap:5px;align-items:center;flex-wrap:wrap}
.jbc-value{display:inline-flex;align-items:center;min-height:25px;padding:3px 8px;border-radius:8px;background:#131f2b;border:1px solid #28394a;font-size:11px;font-weight:800}
.jbc-value.primary{color:#9fcfff}.jbc-value.bool-on{color:#75df8c;background:#14271a;border-color:#26432e}.jbc-value.bool-off{color:#aeb8c3}
.jbc-value.text{font-family:Consolas,monospace;font-weight:650;max-width:440px;white-space:normal;word-break:break-word}
.jbc-value.warn{color:#efcc79;background:#292414;border-color:#51461f}
.jbc-byte-map{display:grid;grid-template-columns:repeat(5,max-content) minmax(110px,1fr);gap:5px;align-items:stretch;margin-top:7px}
.jbc-byte-field{display:flex;flex-direction:column;gap:2px;padding:5px 7px;border-radius:7px;border:1px solid var(--line);background:#0a1118;min-width:48px}
.jbc-byte-field.payload{min-width:120px}.jbc-byte-field .k{font-size:8px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}
.jbc-byte-field .v{font:11px Consolas,monospace;color:#c5ced8;white-space:nowrap}
.jbc-byte-field.payload .v{white-space:normal;word-break:break-all}
.jbc-raw-note{margin-top:6px;color:var(--muted);font-size:10px;line-height:1.35}
.jbc-format-badge{display:inline-flex;align-items:center;height:22px;padding:0 7px;border-radius:999px;background:#1a222c;color:#aab6c2;font-size:9px;font-weight:850;letter-spacing:.025em;text-transform:uppercase}
.jbc-len-ok{color:#75df8c}.jbc-len-bad{color:#ff9098}
@media(max-width:760px){
  .jbc-byte-map{grid-template-columns:repeat(2,minmax(0,1fr))}
  .jbc-byte-field.payload{grid-column:1/-1}
}

/* v1.7.45 Local protocol transaction view */
.diag-local-toolbar{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:8px 0 10px;min-height:34px}
.diag-local-stats{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.diag-local-stat{display:inline-flex;align-items:center;gap:5px;min-height:28px;padding:3px 8px;border:1px solid var(--line);border-radius:8px;background:#0c131b;font-size:11px;color:var(--muted)}
.diag-local-stat strong{color:inherit;font-variant-numeric:tabular-nums}
.diag-local-stat.ok{color:#74df8a;border-color:#24472e}.diag-local-stat.warn{color:#efc96d;border-color:#56491f}
.diag-local-stat.err{color:#ff8e96;border-color:#5c2930}
.diag-local-txn-table{min-width:1120px}
.diag-local-txn-table td{vertical-align:middle}
.diag-local-command{display:flex;flex-direction:column;gap:3px;min-width:170px}
.diag-local-command strong{font-size:12px}
.diag-local-command small{font-family:Consolas,monospace;color:var(--muted);font-size:10px}
.diag-flow{display:flex;align-items:center;gap:5px;white-space:nowrap}
.diag-flow-node{display:inline-flex;align-items:center;justify-content:center;min-width:42px;height:25px;padding:0 7px;border-radius:7px;background:#111c27;border:1px solid var(--line);font-family:Consolas,monospace;font-size:11px}
.diag-flow-arrow{color:#64798e;font-size:12px}
.diag-result-main{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.diag-result-value{font-weight:850;font-variant-numeric:tabular-nums}
.diag-result-sub{color:var(--muted);font-size:10px}
.diag-delta{font-variant-numeric:tabular-nums;white-space:nowrap}
.diag-delta.fast{color:#79df91}.diag-delta.slow{color:#efc96d}
.diag-txn-details details{min-width:120px}
.diag-txn-details summary{cursor:pointer;color:#8fb9df;white-space:nowrap}
.diag-local-repeat{opacity:.58}
.diag-local-unpaired{background:rgba(238,196,91,.035)}
.diag-local-check-bad{background:rgba(255,92,105,.06)}
@media(max-width:760px){
  .diag-local-toolbar{align-items:flex-start;flex-direction:column}
  .diag-local-stats{width:100%}
}

/* v1.7.44 Trace reliability + protocol decode */
.diag-local-table{min-width:1040px}
.diag-proto-frame{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.diag-proto-command{display:inline-flex;align-items:center;min-height:25px;padding:3px 8px;border-radius:8px;background:#142231;font-size:11px;font-weight:850;color:#9bc8f1;white-space:nowrap}
.diag-proto-command.jbc{background:#18243a;color:#a9c8ff}
.diag-proto-command.weller{background:#2b2415;color:#f1ce78}
.diag-proto-field{display:inline-flex;gap:4px;align-items:center;padding:2px 6px;border:1px solid var(--line);border-radius:7px;background:#0c131b;font-size:11px;white-space:nowrap}
.diag-proto-field .k{color:var(--muted);font-size:10px;text-transform:uppercase}
.diag-check-badge{display:inline-flex;align-items:center;padding:2px 7px;border-radius:999px;font-size:10px;font-weight:900}
.diag-check-badge.ok{color:#72df8a;background:#13291a}.diag-check-badge.err{color:#ff8d96;background:#30171a}.diag-check-badge.neutral{color:#aab4bf;background:#1a222c}
.diag-proto-detail{display:flex;gap:6px;flex-wrap:wrap;align-items:center;line-height:1.45}
.diag-proto-ascii{font-family:Consolas,monospace;color:#c3ccd6}
.diag-payload details[data-payload-key],.diag-raw-details{min-width:112px}
.diag-raw-details summary{cursor:pointer;color:#8fb9df;white-space:nowrap}
.diag-control-error{margin-top:8px;padding:8px 10px;border:1px solid #63323a;border-radius:9px;background:#271317;color:#ff9ca4;font-size:12px;display:none}
.diag-control-error.show{display:block}

/* v1.7.43 Bus Diagnose UX */
.diag-session{padding:16px 18px}
.diag-session-head,.diag-trace-head{display:flex;align-items:flex-start;justify-content:space-between;gap:14px}
.diag-title-row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.diag-title-row h2{margin:0}
.diag-session-state{display:inline-flex;align-items:center;gap:7px;height:28px;padding:0 10px;border-radius:999px;font-size:11px;font-weight:850;text-transform:uppercase;letter-spacing:.04em;background:#1b212a;color:#9aa7b5}
.diag-session-state.running{background:#13291a;color:#70df8a}.diag-session-state.stopped{background:#20252c;color:#9aa7b5}
.diag-state-dot{width:8px;height:8px;border-radius:50%;background:currentColor;box-shadow:0 0 0 3px rgba(255,255,255,.035)}
.diag-session-state.running .diag-state-dot{box-shadow:0 0 0 3px rgba(112,223,138,.10)}
.diag-mode-cards{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin:14px 0}
.diag-mode-card{display:grid;grid-template-columns:44px minmax(0,1fr) 22px;align-items:center;gap:11px;width:100%;min-height:68px;text-align:left;padding:10px 12px;border:1px solid var(--line);border-radius:13px;background:#0c131b;color:inherit;transition:border-color .15s ease,background .15s ease,transform .08s ease}
.diag-mode-card:hover{border-color:#3b5875;background:#0f1822}.diag-mode-card:active{transform:translateY(1px)}
.diag-mode-card.active{border-color:#3976aa;background:linear-gradient(135deg,rgba(37,101,159,.18),rgba(37,101,159,.055))}
.diag-mode-icon{display:inline-flex;align-items:center;justify-content:center;width:44px;height:38px;border-radius:10px;background:#14324c;color:#91ccff;font-size:11px;font-weight:900;letter-spacing:.04em}
.diag-mode-icon.local{background:#17351f;color:#91e8a2}
.diag-mode-copy{display:flex;flex-direction:column;gap:2px;min-width:0}.diag-mode-copy strong{font-size:14px}.diag-mode-copy small{color:var(--muted);font-size:11px}
.diag-mode-check{opacity:0;color:#74d989;font-weight:900;text-align:center}.diag-mode-card.active .diag-mode-check{opacity:1}
.diag-control-row{display:grid;grid-template-columns:minmax(210px,1fr) auto auto;gap:10px;align-items:end;padding-top:12px;border-top:1px solid var(--line)}
.diag-target-wrap label{display:block;margin-bottom:5px}.diag-target-wrap select{width:100%}
.diag-session-actions,.diag-export-actions{display:flex;gap:7px;align-items:center;flex-wrap:wrap}
.diag-start-btn{display:inline-flex;align-items:center;gap:7px}.diag-btn-dot{width:7px;height:7px;border-radius:50%;background:currentColor}
.diag-session-actions button:disabled{opacity:.45;cursor:not-allowed}
.diag-filterbar{display:grid;grid-template-columns:minmax(260px,1fr) auto auto;gap:10px;align-items:center;margin-top:12px;padding:10px;border:1px solid var(--line);border-radius:12px;background:#0a1118}
.diag-search-wrap{position:relative;display:flex;align-items:center}.diag-search-wrap input{width:100%;padding-left:34px;padding-right:34px}
.diag-search-icon{position:absolute;left:11px;color:var(--muted);font-size:18px;pointer-events:none}
.diag-search-clear{position:absolute;right:5px;width:28px;height:28px;padding:0;border:0;background:transparent;color:var(--muted);font-size:20px;line-height:28px;border-radius:7px}.diag-search-clear:hover{background:#18222d;color:inherit}
.diag-filter-chips{display:flex;gap:5px;flex-wrap:wrap}
.diag-filter-chip{min-height:32px;padding:5px 10px;border:1px solid var(--line);border-radius:9px;background:#101821;color:var(--muted);font-size:11px;font-weight:800}
.diag-filter-chip:hover{border-color:#40566b;color:inherit}.diag-filter-chip.active{background:#1c5688;border-color:#3478ae;color:white}
.diag-visible-count{min-width:78px;text-align:right;font-size:11px;color:var(--muted);font-variant-numeric:tabular-nums;white-space:nowrap}
.diag-trace-head{align-items:center;margin-bottom:9px}
.diag-trace-legend{display:flex;align-items:center;gap:14px;flex-wrap:wrap;min-height:34px;margin:4px 0 10px;padding:7px 9px;border:1px solid var(--line);border-radius:10px;background:#0a1118;font-size:11px;color:var(--muted)}
.diag-legend-dot{display:inline-block;width:7px;height:7px;border-radius:50%;margin-right:5px;background:#73808f}.diag-legend-dot.tx{background:#76adf2}.diag-legend-dot.rx{background:#67d982}.diag-legend-dot.timeout{background:#f06d78}
.diag-trace-note{margin-left:auto}
.diag-trace-switch{min-width:190px}.diag-trace-switch button{flex:1}
.diag-scroll{scrollbar-color:#334456 #0b1118;scrollbar-width:thin}
.diag-table th{white-space:nowrap}
.diag-table td{transition:background .1s ease}.diag-table tbody tr:nth-child(even){background:rgba(255,255,255,.012)}
.diag-table tbody tr.row-error{background:rgba(255,99,110,.045)}.diag-table tbody tr.row-timeout{background:rgba(255,99,110,.075)}
.diag-payload details{border:1px solid transparent;border-radius:7px;padding:2px 5px}.diag-payload details[open]{border-color:var(--line);background:#0a1118;padding:6px 8px}
.diag-payload summary{list-style:none}.diag-payload summary::-webkit-details-marker{display:none}.diag-payload summary:before{content:"＋";display:inline-block;width:15px;color:#70879c}.diag-payload details[open] summary:before{content:"−"}
.diag-local-summary{display:flex;align-items:center;min-height:44px}
.diag-local-summary.is-jbcusb{display:block;padding:12px 14px;border-color:#315a50;background:#0b1515}
.diag-jbcusb-head{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-bottom:9px}
.diag-jbcusb-title{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.diag-jbcusb-title strong{color:#e7f4ef}
.diag-jbcusb-grid{display:grid;grid-template-columns:repeat(4,minmax(110px,1fr));gap:7px}
.diag-jbcusb-stat{padding:8px 9px;border:1px solid #253a39;border-radius:8px;background:#0c1115;min-width:0}
.diag-jbcusb-stat .k{display:block;color:#7f9393;font-size:9px;text-transform:uppercase;letter-spacing:.045em}
.diag-jbcusb-stat .v{display:block;margin-top:3px;color:#e0e9e8;font-size:12px;font-weight:750;line-height:1.25;overflow-wrap:anywhere}
.diag-jbcusb-stat.warn{border-color:#715b2f}.diag-jbcusb-stat.err{border-color:#71373d;background:#171012}
.diag-jbcusb-note{margin-top:8px;color:#7f9393;font-size:10px}
.diag-module-card.jbcusb{--diag-accent:#39c779}
.diag-module-card .diag-share-track{position:relative}
.diag-module-card .diag-share-fill{display:block!important}
@media(max-width:900px){
  .diag-control-row{grid-template-columns:1fr}.diag-session-actions,.diag-export-actions{justify-content:flex-start}
  .diag-filterbar{grid-template-columns:1fr}.diag-visible-count{text-align:left}
}
@media(max-width:620px){
  .diag-jbcusb-grid{grid-template-columns:1fr 1fr}
  .diag-mode-cards{grid-template-columns:1fr}
  .diag-filter-chips{overflow-x:auto;flex-wrap:nowrap;padding-bottom:2px}
  .diag-filter-chip{white-space:nowrap}
}

/* v1.7.42 Bus Diagnose visual polish */
.diag-module-grid{grid-template-columns:repeat(auto-fit,minmax(290px,1fr));align-items:stretch}
.diag-module-card{display:flex;flex-direction:column;min-height:278px;box-shadow:0 8px 22px rgba(0,0,0,.10)}
.diag-module-head{min-height:66px;align-items:center;padding:13px 14px}
.diag-module-head>div:first-child{min-width:0}
.diag-module-name{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.diag-module-type{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.diag-mini-badge.diag-online-pill{
  flex:0 0 86px;
  width:86px;
  min-width:86px;
  height:28px;
  padding:0 10px;
  justify-content:center;
  box-sizing:border-box;
  text-align:center;
  white-space:nowrap;
}
.diag-online-pill .diag-dot{flex:0 0 8px;margin-right:7px}
.diag-layer{padding:12px 14px}
.diag-layer.transport{background:linear-gradient(180deg,rgba(35,83,125,.10),rgba(35,83,125,.025))}
.diag-layer.device{margin-top:auto;background:linear-gradient(180deg,rgba(35,100,58,.07),rgba(35,100,58,.015))}
.diag-layer-title{min-height:24px;margin-bottom:9px}
.diag-share-wrap{display:flex;align-items:center;gap:7px;min-width:90px;justify-content:flex-end}
.diag-share-track{width:48px;height:5px;border-radius:99px;background:#202a35;overflow:hidden}
.diag-share-fill{display:block;height:100%;min-width:1px;border-radius:inherit;background:currentColor;opacity:.82;transition:width .28s ease}
.diag-share-text{font-variant-numeric:tabular-nums;min-width:39px;text-align:right}
.diag-layer-grid{grid-template-columns:minmax(86px,1fr) minmax(118px,1.25fr);row-gap:7px;align-items:center}
.diag-layer-grid .k{font-size:11px;text-transform:uppercase;letter-spacing:.035em}
.diag-layer-grid .v{font-variant-numeric:tabular-nums;font-weight:650;white-space:nowrap}
.diag-metric-split{display:inline-flex;gap:5px;align-items:center;justify-content:flex-end}
.diag-metric-chip{display:inline-flex;min-width:28px;justify-content:center;border-radius:7px;padding:2px 5px;background:#151e29;font-size:11px}
.diag-metric-chip.err{color:#ff9198;background:#2b171b}
.diag-metric-chip.warn{color:#f3cc71;background:#292414}
.diag-metric-chip.ok{color:#73df8b;background:#132519}
.diag-device-detail{min-height:34px;line-height:1.4;display:flex;align-items:center}
.diag-arrow{height:16px;display:flex;align-items:center;justify-content:center;margin:0;color:#61758b}
.diag-arrow:before{content:"";width:1px;height:11px;background:#344354;margin-right:-1px}
.diag-stat-grid .stat{min-height:76px;display:flex;flex-direction:column;justify-content:center}
.diag-stat-grid .v{font-variant-numeric:tabular-nums}
.diag-panel-title h2{margin-bottom:2px}
.diag-table tbody tr:hover{background:rgba(90,155,215,.055)}
.diag-pair-table tbody tr:hover{background:rgba(90,155,215,.07)}
@media(max-width:760px){
  .diag-module-card{min-height:0}
  .diag-mini-badge.diag-online-pill{flex-basis:82px;width:82px;min-width:82px}
}

/* v1.7.51 FINAL trace-chip typography normalization.
   Deliberately last in the stylesheet so legacy rules cannot override it. */
.diag-local-table .diag-proto-command,
.diag-local-table .jbc-meta-badge,
.diag-local-table .jbc-value:not(.text),
.diag-local-table .diag-proto-field,
.diag-local-table .diag-check-badge,
.diag-local-table .jbc-format-badge,
.diag-local-table .diag-mini-badge,
.diag-local-table .diag-flow-node{
  box-sizing:border-box !important;
  display:inline-flex !important;
  align-items:center !important;
  vertical-align:middle !important;
  margin-top:0 !important;
  margin-bottom:0 !important;
  padding-top:0 !important;
  padding-bottom:0 !important;
  line-height:1 !important;
}

.diag-local-table .diag-proto-command,
.diag-local-table .jbc-value:not(.text),
.diag-local-table .diag-proto-field,
.diag-local-table .diag-flow-node{
  height:28px !important;
  min-height:28px !important;
}

.diag-local-table .jbc-meta-badge,
.diag-local-table .diag-check-badge,
.diag-local-table .jbc-format-badge,
.diag-local-table .diag-mini-badge{
  height:24px !important;
  min-height:24px !important;
}

.diag-local-table .diag-proto-command{
  padding-left:8px !important;
  padding-right:8px !important;
  font-size:11px !important;
}

.diag-local-table .jbc-meta-badge{
  padding-left:6px !important;
  padding-right:6px !important;
  font-family:Consolas,monospace !important;
  font-size:10px !important;
  font-weight:400 !important;
}

.diag-local-table .jbc-value{
  gap:5px !important;
  padding-left:8px !important;
  padding-right:8px !important;
}

.diag-local-table .jbc-chip-label,
.diag-local-table .jbc-chip-value{
  display:inline-flex !important;
  align-items:center !important;
  min-height:0 !important;
  margin:0 !important;
  padding:0 !important;
  line-height:1 !important;
  vertical-align:middle !important;
}

.diag-local-table .jbc-chip-label{
  font-size:10px !important;
  font-weight:700 !important;
  color:var(--muted) !important;
}

.diag-local-table .jbc-chip-value{
  font-size:11px !important;
  font-weight:800 !important;
  color:inherit;
}

.diag-local-table .jbc-value.text{
  min-height:28px !important;
  height:auto !important;
  padding-top:6px !important;
  padding-bottom:6px !important;
  align-items:center !important;
}

.diag-local-table .jbc-value.text .jbc-chip-label,
.diag-local-table .jbc-value.text .jbc-chip-value{
  line-height:1.25 !important;
}

.diag-local-table .diag-check-badge,
.diag-local-table .jbc-format-badge,
.diag-local-table .diag-mini-badge{
  justify-content:center !important;
}

.diag-local-table .diag-proto-field{
  gap:4px !important;
  padding-left:7px !important;
  padding-right:7px !important;
}

.diag-local-table .diag-proto-field .k{
  display:inline-flex !important;
  align-items:center !important;
  height:100% !important;
  margin:0 !important;
  padding:0 !important;
  line-height:1 !important;
}

.diag-local-table .jbc-raw-command,
.diag-local-table .jbc-inline-meta,
.diag-local-table .jbc-raw-result,
.diag-local-table .jbc-result-wrap,
.diag-local-table .jbc-decode,
.diag-local-table .diag-proto-frame,
.diag-local-table .diag-proto-detail,
.diag-local-table .diag-result-main,
.diag-local-table .diag-flow{
  align-items:center !important;
  line-height:1 !important;
}


/* v1.7.52 Raw payload details spacing */
.diag-local-table .diag-raw-details[open]{
  padding:9px 10px 10px !important;
}

.diag-local-table .diag-raw-details[open] > summary{
  margin:-2px 0 8px !important;
}

.diag-local-table .jbc-byte-map{
  margin:0 0 8px !important;
  padding:3px !important;
  gap:7px !important;
  box-sizing:border-box !important;
}

.diag-local-table .jbc-byte-field{
  padding:6px 8px !important;
}

.diag-local-table .jbc-byte-field.payload{
  margin-right:2px !important;
}

.diag-local-table .jbc-raw-note{
  margin:8px 2px 7px !important;
}

.diag-local-table .diag-payload-line{
  margin:6px 2px 0 !important;
}


/* v1.7.55 Universal RS232 + Modbus analyzer styling */
.diag-proto-command.universal{background:#2b2616;color:#f2cf7c}
.diag-proto-command.modbus{background:#1a2b25;color:#82ddb3}

.proto-meta-row{display:flex;align-items:center;gap:5px;flex-wrap:wrap;margin-top:4px}
.proto-meta-badge{
  display:inline-flex;align-items:center;justify-content:center;
  height:24px;min-height:24px;box-sizing:border-box;
  padding:0 7px;border:1px solid var(--line);border-radius:7px;
  background:#111923;color:#aab6c2;font:10px/1 Consolas,monospace;
  white-space:nowrap
}
.proto-meta-badge.ok{color:#79df91;border-color:#274b31;background:#14291a}
.proto-meta-badge.warn{color:#efcc79;border-color:#584b21;background:#292414}
.proto-meta-badge.err{color:#ff929a;border-color:#603038;background:#30171b}

.proto-result{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.proto-result-value{
  display:inline-flex;align-items:center;min-height:28px;box-sizing:border-box;
  padding:0 8px;border:1px solid #2a3b4b;border-radius:8px;
  background:#131f2b;color:#c7d3df;font-size:11px;font-weight:800;
  line-height:1.15
}
.proto-result-value.text{
  max-width:430px;height:auto;min-height:28px;padding-top:5px;padding-bottom:5px;
  white-space:normal;overflow-wrap:anywhere;word-break:break-word;
  font-family:Consolas,monospace
}
.proto-result-value.primary{color:#9fcfff}
.proto-result-sub{font-size:10px;color:var(--muted);line-height:1.25}

.proto-raw-grid{
  display:grid;grid-template-columns:repeat(4,max-content) minmax(0,1fr);
  gap:7px;margin:0 0 8px;padding:3px;box-sizing:border-box;width:100%;min-width:0
}
.proto-raw-field{
  display:flex;flex-direction:column;gap:2px;min-width:0;max-width:100%;
  padding:6px 8px;border:1px solid var(--line);border-radius:7px;background:#0a1118;
  box-sizing:border-box
}
.proto-raw-field.data{min-width:0}
.proto-raw-field .k{font-size:8px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}
.proto-raw-field .v{
  font:11px/1.25 Consolas,monospace;color:#c5ced8;
  white-space:normal;overflow-wrap:anywhere;word-break:break-word
}
.proto-raw-note{margin:8px 2px 7px;color:var(--muted);font-size:10px;line-height:1.35}

@media(max-width:900px){
  .proto-raw-grid{grid-template-columns:repeat(2,minmax(0,1fr))}
  .proto-raw-field.data{grid-column:1/-1}
}


/* v1.8.81 Bus Diagnose: module-card header parity with Status v1.8.70 */
.diag-module-head{height:136px;min-height:136px;max-height:136px;flex:0 0 136px;padding:12px 16px;border-bottom:1px solid #2b3139;background:#1c2026;box-shadow:inset 4px 0 0 var(--diag-accent);display:flex;flex-direction:column;align-items:stretch;justify-content:flex-start;gap:9px}
.diag-module-title{width:100%;font-size:15px;font-weight:800;color:#eef3f8;white-space:normal;overflow:visible;text-overflow:clip;line-height:1.18;overflow-wrap:anywhere}
.diag-module-head-main{display:flex;align-items:center;justify-content:space-between;gap:10px;min-width:0;flex:1 1 auto}
.diag-module-identity{display:flex;align-items:center;gap:11px;min-width:0;flex:1 1 auto}
.diag-module-icon{width:42px;min-width:42px;height:42px;flex:0 0 42px;border-radius:9px;display:inline-flex;align-items:center;justify-content:center;background:#252b33;color:var(--diag-accent);font-size:17px;font-weight:800}
.diag-module-icon .module-head-glyph{width:24px;height:24px;display:block;color:currentColor}
.diag-module-copy{min-width:0;flex:1 1 auto}
.diag-module-type{margin-top:0;color:#aeb8c3;font-size:12px;font-weight:700;line-height:1.2;white-space:normal;overflow:visible;text-overflow:clip}
.diag-module-address{margin-top:5px;color:#718090;font:600 11px/1.15 Consolas,monospace;white-space:nowrap}
.diag-module-statuses{display:flex;height:74px;min-height:74px;flex-direction:column;flex-wrap:nowrap;align-items:flex-end;justify-content:center;gap:3px;flex:0 0 auto;min-width:78px}
.diag-module-head .diag-online-pill{flex:0 0 auto;width:78px;min-width:78px;height:22px;padding:0 9px;font-size:11px;white-space:nowrap;line-height:1}
@media(max-width:520px){.diag-module-head{height:auto;min-height:136px;max-height:none;flex-basis:auto}.diag-module-head-main{align-items:center}.diag-module-statuses{flex:0 0 auto}}

</style>)CSS");

  html += web_is_german() ? F("<script>const UI_DE=true;") : F("<script>const UI_DE=false;");
  html += R"JS(
function u(de,en){return UI_DE?de:en;}
let lastDiag=null;
let prevDiag=null;
let diagFilteredEvents=[];
let diagTraceLayout='pairs';
let diagQuickFilter='all';
const DIAG_TRACE_ROW_LIMIT=500;
let diagLocalLayout='transactions';
let diagLocalChangesOnly=false;
let diagControlPending=false;
let diagStateEpoch=0;
const diagOpenPayloads=new Set();
const DIAG_EVENT_SOURCE_LIMIT=1100;
const DIAG_EVENT_BATCH=48;
let diagEventCache=[];
let diagTraceCursor=0;
let diagTraceEpoch=-1;
let diagTraceView='';
let diagEventsBusy=false;

async function post(a){
  if(diagControlPending)return;
  let fd=new URLSearchParams();
  let view=document.getElementById('diag_view').value;
  let addr=document.getElementById('diag_addr').value;
  if(a==='start'&&view==='local'&&(addr==='0'||addr==='')){
    alert(u('Bitte ein einzelnes Modul für den Gerätebus auswählen.','Please select one target module for device-bus tracing.'));
    return;
  }

  diagControlPending=true;
  const epoch=++diagStateEpoch;
  let sb=document.getElementById('diag_start_btn'),tb=document.getElementById('diag_stop_btn');
  if(sb)sb.disabled=true;if(tb)tb.disabled=true;
  let err=document.getElementById('diag_control_error');
  if(err){err.classList.remove('show');err.textContent='';}

  fd.set('action',a);
  if(a==='start'){fd.set('addr',addr);fd.set('view',view);}
  try{
    let r=await fetch('/diagnostics/control',{method:'POST',body:fd,cache:'no-store'});
    let msg=await r.text();
    if(!r.ok)throw new Error(msg||('HTTP '+r.status));
    await loadDiag(true,epoch);
  }catch(ex){
    if(err){
      err.textContent=u('Trace-Befehl fehlgeschlagen: ','Trace command failed: ')+(ex&&ex.message?ex.message:String(ex));
      err.classList.add('show');
    }
  }finally{
    diagControlPending=false;
    if(sb)sb.disabled=!!(lastDiag&&lastDiag.active);
    if(tb)tb.disabled=!(lastDiag&&lastDiag.active);
  }
}
function diagStart(){post('start')}
function diagStop(){post('stop')}
function diagClear(){post('clear')}
function diagClearSearch(){
  let el=document.getElementById('diag_filter');
  if(el){el.value='';el.focus();}
  diagApplyFilter();
}
function diagSetQuickFilter(v){
  diagQuickFilter=v;
  ['all','tx','rx','errors','timeout'].forEach(function(k){
    let b=document.getElementById('diag_q_'+k);
    if(b)b.classList.toggle('active',k===v);
  });
  diagApplyFilter();
}
function diagQuickMatch(e){
  let dir=String(e.dir||'').toUpperCase();
  let txt=String(e.text||'').toLowerCase();
  let status=Number(e.status);
  if(diagQuickFilter==='tx')return dir==='TX'||dir==='UART_TX';
  if(diagQuickFilter==='rx')return dir==='RX'||dir==='UART_RX';
  if(diagQuickFilter==='timeout')return dir==='TIMEOUT'||txt.indexOf('timeout')>=0;
  if(diagQuickFilter==='errors'){
    return dir==='TIMEOUT'||(status!==0&&status!==255)||
      txt.indexOf('bad seq')>=0||txt.indexOf('bad cmd')>=0||
      txt.indexOf('crc')>=0||txt.indexOf('error')>=0||txt.indexOf('fehler')>=0;
  }
  return true;
}
function diagSetView(v){document.getElementById('diag_view').value=v;diagView();}
function diagSetTraceLayout(v){
  diagTraceLayout=v;
  document.getElementById('diag_pairs_wrap').style.display=v==='pairs'?'block':'none';
  document.getElementById('diag_raw_wrap').style.display=v==='raw'?'block':'none';
  document.getElementById('diag_pairs_btn').classList.toggle('active',v==='pairs');
  document.getElementById('diag_raw_btn').classList.toggle('active',v==='raw');
  diagApplyFilter();
}
function diagSetLocalLayout(v){
  diagLocalLayout=v;
  document.getElementById('diag_local_txn_wrap').style.display=v==='transactions'?'block':'none';
  document.getElementById('diag_local_raw_wrap').style.display=v==='raw'?'block':'none';
  document.getElementById('diag_local_txn_btn').classList.toggle('active',v==='transactions');
  document.getElementById('diag_local_raw_btn').classList.toggle('active',v==='raw');
  diagApplyFilter();
}
function diagToggleLocalChanges(){
  diagLocalChangesOnly=!diagLocalChangesOnly;
  let b=document.getElementById('diag_local_changes_btn');
  if(b)b.classList.toggle('active',diagLocalChangesOnly);
  diagApplyFilter();
}
function diagView(){
  let v=document.getElementById('diag_view').value;
  document.getElementById('diag_rs485_panel').style.display=v==='rs485'?'block':'none';
  document.getElementById('diag_local_panel').style.display=v==='local'?'block':'none';
  document.getElementById('diag_mode_rs485').classList.toggle('active',v==='rs485');
  document.getElementById('diag_mode_local').classList.toggle('active',v==='local');
  document.getElementById('diag_hint').textContent=v==='rs485'
    ?u('OFE-Transport zwischen Master und Modulen. Alle Module oder gezielt eine Adresse mitschneiden.','OFE transport between master and modules. Capture all modules or one specific address.')
    :u('Lokaler Bus hinter einem Modul. Hier immer ein einzelnes Zielmodul auswählen.','Local bus behind a module. Always select one target module here.');
  let sel=document.getElementById('diag_addr');
  if(sel&&v==='local'&&sel.value==='0'&&sel.options.length>1)sel.selectedIndex=1;
  loadDiag();
}
function hx(n){return '0x'+Number(n).toString(16).toUpperCase().padStart(2,'0')}
function addrText(n){return Number(n)===0?'ALL':hx(n)}
function esc(s){return String(s==null?'':s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function csvCell(v){v=String(v==null?'':v);return /[";\n\r]/.test(v)?'"'+v.replace(/"/g,'""')+'"':v;}
function fmtBytes(v){v=Number(v)||0;if(v>=1024*1024)return (v/1024/1024).toFixed(1)+' MB/s';if(v>=1024)return (v/1024).toFixed(1)+' KB/s';return v.toFixed(v<10?1:0)+' B/s';}
function fmtFps(v){v=Number(v)||0;return v.toFixed(v<10?1:0);}
function fmtTime(ms){ms=Number(ms)||0;return (ms/1000).toFixed(3)+' s';}
function diagSetRowCount(shown,total,labelDe,labelEn){
  let c=document.getElementById('diag_visible_count');
  if(!c)return;
  let label=u(labelDe,labelEn);
  c.textContent=shown+' / '+total+' '+label;
}
function statusName(s){
  s=Number(s);
  return ({0:'OK',1:'UNKNOWN_CMD',2:'BAD_LEN',3:'BAD_VALUE',4:'BUSY',5:'CRC_ERROR',6:'NOT_SUPPORTED'})[s]||(s===255?'-':hx(s));
}
function statusClass(s,dir){
  if(dir==='TIMEOUT')return 'err';
  if(Number(s)===0)return 'ok';
  if(Number(s)===255)return 'warn';
  return 'err';
}
function diagProto(e){
  let m=diagModuleByAddr(e&&e.addr),a=Number(e&&e.addr),mark=Number(e&&e.status);
  if(m&&Number(m.type)===9){
    if(mark===1)return 'JBC P01';
    if(mark===2)return 'JBC P02';
    if(mark===0xEE)return 'JBC BCC';
    if(mark===0xEF)return 'JBC FRAME';
    if(mark===0xF0)return 'JBC RAW';
    return 'JBC USB';
  }
  return (a>=0x50&&a<=0x5F)?'Universal RS232':
    ((a>=0x60&&a<=0x6F)?'Modbus RTU':
    ((a>=0x30&&a<=0x3F)?'Weller UART':
    (a>=0x20&&a<=0x2F?'Lokale I/O':'JBC P02')));
}
function diagFilename(ext){
  let v=document.getElementById('diag_view').value;
  let a=document.getElementById('diag_addr').value||'0';
  let stamp=new Date().toISOString().replace(/[:.]/g,'-');
  return 'open-fume-'+v+'-'+a.replace('0x','0X')+'-'+stamp+'.'+ext;
}
function diagSemanticDecoded(e){
  if(!jbcUsbTraceEvent(e))return e&&e.text?String(e.text):'';
  let f=jbcFrame(e);if(!f)return e&&e.text?String(e.text):'';
  let role=jbcTraceRole(e),di=jbcUsbPayloadInfo(f,role),parts=[];
  parts.push(jbcUsbCommandName(f)||('CMD 0x'+hex2(f.ctrl)));
  if(di&&di.format)parts.push(di.format);
  if(di&&di.html){
    let box=document.createElement('div');box.innerHTML=di.html;
    let fields=box.querySelectorAll('.jbc-value'),vals=[];
    for(let i=0;i<fields.length;i++){
      let lab=fields[i].querySelector('.jbc-chip-label'),val=fields[i].querySelector('.jbc-chip-value');
      let a=lab?lab.textContent.trim():'',b=val?val.textContent.trim():'';
      if(a&&b)vals.push(a+'='+b);else if(b)vals.push(b);else if(a)vals.push(a);
    }
    if(vals.length)parts.push(vals.join(' | '));
    else if(di.summary)parts.push(di.summary);
  }else if(di&&di.summary)parts.push(di.summary);
  return parts.filter(function(x,i,a){return x&&a.indexOf(x)===i;}).join(' · ');
}
function diagExportEvent(e){
  let x={};Object.keys(e||{}).forEach(function(k){x[k]=e[k];});
  if(jbcUsbTraceEvent(e)){
    let f=jbcFrame(e);x.decoded_semantic=diagSemanticDecoded(e);
    if(f)x.jbc={protocol:f.protocol,role:jbcTraceRole(e),src:f.src,dst:f.dst,fid:f.fid,command:f.ctrl,command_name:jbcUsbCommandName(f)||'',declared_len:f.declared,payload:f.payload.map(hex2).join(' '),truncated:!!f.truncated};
  }
  return x;
}
function diagDownload(fmt){
  if(!lastDiag)return;
  let ev=diagFilteredEvents.slice();
  let view=lastDiag.view||document.getElementById('diag_view').value;
  let data,mime,name;
  if(fmt==='json'){
    data=JSON.stringify({view:view,target:lastDiag.target_addr,stats:lastDiag,events:ev.map(diagExportEvent)},null,2);
    mime='application/json';name=diagFilename('json');
  }else{
    let head=view==='local'
      ?['#','ms','addr','dir','protocol','len','age_or_latency_ms','decoded','raw']
      :['#','ms','frame_seq','addr','dir','cmd','status','len','latency_ms','decoded','payload'];
    let rows=[head.join(';')];let n=0;
    ev.forEach(function(e){++n;
      if(view==='local'){
        rows.push([n,e.ms,addrText(e.addr),String(e.dir).replace('UART_',''),diagProto(e),e.len,e.latency||'',diagSemanticDecoded(e),e.data||''].map(csvCell).join(';'));
      }else{
        rows.push([n,e.ms,e.frame_seq===255?'':e.frame_seq,addrText(e.addr),e.dir,e.cmd,e.status===255?'':e.status,e.len,e.latency||'',e.text||'',e.data||''].map(csvCell).join(';'));
      }
    });
    data=rows.join('\r\n');mime='text/csv';name=diagFilename('csv');
  }
  let blob=new Blob([data],{type:mime});let url=URL.createObjectURL(blob);let a=document.createElement('a');
  a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();
  setTimeout(function(){URL.revokeObjectURL(url);},1000);
}
function diagHay(e){
  return [e.ms,e.frame_seq,addrText(e.addr),e.dir,e.cmd,e.status===255?'':statusName(e.status),e.len,e.latency||'',e.text||'',e.data||'',diagProto(e)].join(' ').toLowerCase();
}

function diagPairEvents(events){
  let chrono=events.slice().reverse();
  let pending={};let out=[];
  chrono.forEach(function(e){
    let isLocal=String(e.dir).indexOf('UART_')===0;
    if(isLocal)return;
    let key=String(e.addr)+':'+String(e.frame_seq);
    if(e.dir==='TX'&&e.frame_seq!==255&&Number(e.addr)!==0){
      pending[key]={kind:'pair',tx:e,rx:null,order:e.seq};
      return;
    }
    if((e.dir==='RX'||e.dir==='TIMEOUT')&&e.frame_seq!==255&&pending[key]){
      let p=pending[key];p.rx=e;p.order=Math.max(p.order,e.seq);out.push(p);delete pending[key];return;
    }
    out.push({kind:'single',event:e,order:e.seq});
  });
  Object.keys(pending).forEach(k=>out.push(pending[k]));
  out.sort((a,b)=>b.order-a.order);
  return out;
}
function diagPayloadToggle(el){
  let key=el&&el.dataset?el.dataset.payloadKey:'';
  if(!key)return;
  if(el.open)diagOpenPayloads.add(key);else diagOpenPayloads.delete(key);
}
function diagPayloadKey(tx,rx){
  let a=tx?tx.seq:0,b=rx?rx.seq:0;
  return 'p-'+a+'-'+b;
}
function payloadDetails(tx,rx){
  let txd=tx&&tx.data?tx.data:'';
  let rxd=rx&&rx.data?rx.data:'';
  if(!txd&&!rxd)return '-';
  let sum=[];
  if(tx)sum.push('TX '+tx.len+' B');
  if(rx&&rx.dir==='RX')sum.push('RX '+rx.len+' B');
  let key=diagPayloadKey(tx,rx);
  let open=diagOpenPayloads.has(key)?' open':'';
  let h='<details data-payload-key="'+key+'" ontoggle="diagPayloadToggle(this)"'+open+'><summary>'+sum.join(' · ')+'</summary>';
  if(tx)h+='<div class="diag-payload-line"><b>TX:</b> <span class="data">'+esc(txd||'-')+'</span></div>';
  if(rx&&rx.dir==='RX')h+='<div class="diag-payload-line"><b>RX:</b> <span class="data">'+esc(rxd||'-')+'</span></div>';
  h+='</details>';return h;
}
function diagRenderPairs(events){
  let rows='';let no=0;
  let pairs=diagPairEvents(events);
  let shownPairs=pairs.slice(0,DIAG_TRACE_ROW_LIMIT);
  if(document.getElementById('diag_view').value==='rs485'&&diagTraceLayout==='pairs')
    diagSetRowCount(shownPairs.length,pairs.length,'Transaktionen','transactions');
  shownPairs.forEach(function(p){
    ++no;
    if(p.kind==='single'){
      let e=p.event;
      let cls=e.dir.toLowerCase();
      let rowClass=e.dir==='TIMEOUT'?'row-timeout':((Number(e.status)!==0&&Number(e.status)!==255)?'row-error':'');
      rows+='<tr class="'+rowClass+'"><td>'+no+'</td><td>'+fmtTime(e.ms)+'</td><td>'+addrText(e.addr)+'</td><td><span class="diag-dir '+cls+'">'+esc(e.dir)+'</span> '+esc(e.cmd||'-')+'</td><td><span class="diag-status '+statusClass(e.status,e.dir)+'">'+statusName(e.status)+'</span></td><td>'+e.len+' B</td><td>'+(e.latency?e.latency+' ms':'-')+'</td><td class="decoded">'+esc(e.text||'')+'</td><td class="diag-payload">'+payloadDetails(e,null)+'</td></tr>';
      return;
    }
    let tx=p.tx,rx=p.rx;
    let result=rx?(rx.dir==='TIMEOUT'?'TIMEOUT':statusName(rx.status)):u('offen','pending');
    let rclass=rx?(rx.dir==='TIMEOUT'?'err':statusClass(rx.status,rx.dir)):'warn';
    let cmd=String(tx.cmd||'-').replace('|RESP','');
    let decoded=tx.text||'';
    if(rx&&rx.text&&rx.text!==tx.text)decoded+=(decoded?' → ':'')+rx.text;
    let rowClass=rx&&rx.dir==='TIMEOUT'?'row-timeout':(rclass==='err'?'row-error':'');
    rows+='<tr class="'+rowClass+'"><td>'+no+'</td><td>'+fmtTime(tx.ms)+'</td><td>'+addrText(tx.addr)+'</td><td><span class="diag-dir '+(rx&&rx.dir==='TIMEOUT'?'timeout':'rx')+'">↔</span> '+esc(cmd)+'</td><td><span class="diag-status '+rclass+'">'+esc(result)+'</span></td><td><span class="tx">TX '+tx.len+' B</span> / <span class="rx">RX '+(rx&&rx.dir==='RX'?rx.len:'-')+'</span></td><td>'+(rx&&rx.latency?rx.latency+' ms':'-')+'</td><td class="decoded">'+esc(decoded)+'</td><td class="diag-payload">'+payloadDetails(tx,rx)+'</td></tr>';
  });
  document.getElementById('diag_pair_rows').innerHTML=rows||'<tr><td colspan="9" class="muted">'+u('Keine OFE-Bus Events','No OFE bus events')+'</td></tr>';
}
function diagRenderRaw(events){
  let rows='';let no=0;
  let raw=events.filter(e=>String(e.dir).indexOf('UART_')!==0);
  let shownRaw=raw.slice(0,DIAG_TRACE_ROW_LIMIT);
  if(document.getElementById('diag_view').value==='rs485'&&diagTraceLayout==='raw')
    diagSetRowCount(shownRaw.length,raw.length,'Rohframes','raw frames');
  shownRaw.forEach(function(e){
    ++no;let cls=e.dir.toLowerCase();
    rows+='<tr><td>'+no+'</td><td>'+e.ms+'</td><td>'+(e.frame_seq===255?'-':e.frame_seq)+'</td><td>'+addrText(e.addr)+'</td><td><span class="diag-dir '+cls+'">'+esc(e.dir)+'</span></td><td>'+esc(e.cmd)+'</td><td><span class="diag-status '+statusClass(e.status,e.dir)+'">'+statusName(e.status)+'</span></td><td>'+e.len+'</td><td>'+(e.latency?e.latency+' ms':'-')+'</td><td class="decoded">'+esc(e.text||'')+'</td><td class="data">'+esc(e.data||'')+'</td></tr>';
  });
  document.getElementById('diag_rows').innerHTML=rows||'<tr><td colspan="11" class="muted">'+u('Keine OFE-Bus Events','No OFE bus events')+'</td></tr>';
}
function hexBytes(s){
  s=String(s||'').replace(/[^0-9a-f]/gi,'');
  let out=[];for(let i=0;i+1<s.length;i+=2)out.push(parseInt(s.slice(i,i+2),16));
  return out;
}
function asciiBytes(b){
  return b.map(x=>(x>=32&&x<=126)?String.fromCharCode(x):'.').join('');
}
function hex2(v){return Number(v).toString(16).toUpperCase().padStart(2,'0')}
function u16le(b,o=0){return (Number(b[o]||0)|((Number(b[o+1]||0))<<8))>>>0}
function cleanAscii(b){
  let s='';
  for(let x of b){
    if(x===0)continue;
    if(x>=32&&x<=126)s+=String.fromCharCode(x);
    else return null;
  }
  return s;
}
function jbcBool(v){return Number(v)?u('AN','ON'):u('AUS','OFF')}
function jbcIntake(v){return Number(v)===0?'Work':(Number(v)===1?'Stand':('Intake '+Number(v)))}
function jbcPedalMode(v){
  return ({0:u('Aus','Off'),1:u('Moment','Momentary'),2:u('Toggle','Toggle')})[Number(v)]||('Mode '+Number(v));
}
function jbcSuctionLevel(v){
  return ({
    0:u('High · 100 %','High · 100%'),
    1:u('Medium · 60 %','Medium · 60%'),
    2:u('Low · 30 %','Low · 30%'),
    3:u('Custom','Custom')
  })[Number(v)]||('Level '+Number(v));
}
function jbcSelectFlowText(v){
  v=Number(v)||0;
  let pct=v>=100?Math.round(v/10):v;
  return v+' raw · '+pct+' %';
}
function jbcField(label,value,cls=''){
  return '<span class="jbc-value '+cls+'"><span class="jbc-chip-label">'+esc(label)+'</span><span class="jbc-chip-value">'+esc(value)+'</span></span>';
}
function jbcTextField(label,value){
  return '<span class="jbc-value text"><span class="jbc-chip-label">'+esc(label)+'</span><span class="jbc-chip-value">'+esc(value)+'</span></span>';
}
function jbcUsbToolNameDiag(id){
  id=Number(id||0)&255;
  const names={0:'-',1:'T210',2:'T245',3:'PA',4:'HT',5:'DS',6:'DR',7:'NT105',8:'NP105',9:'T470',10:'ALE250',31:'JT',32:'TE',33:'PHS',34:'PHB'};
  return names[id]||('0x'+hex2(id));
}
function jbcUsbToolNameFrame(f,id){
  let raw=Number(id||0)&255,fam=jbcUsbFamilyForEvent(f&&f.e),m=diagModuleByAddr(f&&f.e&&f.e.addr),model=jbcUsbModelForEvent(f&&f.e),generic=raw;
  if(fam==='SOLD'){
    if(model==='HD'||model==='HDE')generic=9;
    else if(model==='NA'){if(raw===0)generic=0;else if(raw===1)generic=7;else if(raw===3)generic=8;}
    else if(model==='ALE')generic=10;
  }else if(fam==='HA'&&raw>0)generic=(raw+30)&255;
  return jbcUsbToolNameDiag(generic);
}
function jbcUsbContiSpeed(v){
  return ({0:'OFF',1:'10 ms · 100 Hz',2:'20 ms · 50 Hz',3:'50 ms · 20 Hz',4:'100 ms · 10 Hz',5:'200 ms · 5 Hz',6:'500 ms · 2 Hz',7:'1000 ms · 1 Hz'})[Number(v)]||('Speed '+Number(v));
}
function i16le(b,o=0){let v=u16le(b,o);return v&0x8000?v-0x10000:v}
function u32le(b,o=0){return ((Number(b[o]||0))|((Number(b[o+1]||0))<<8)|((Number(b[o+2]||0))<<16)|((Number(b[o+3]||0))<<24))>>>0}
function i32le(b,o=0){let v=u32le(b,o);return v>0x7FFFFFFF?v-0x100000000:v}
function u64leText(b,o=0){
  if(typeof BigInt==='function'){
    let v=0n;for(let i=7;i>=0;--i)v=(v<<8n)|BigInt(Number(b[o+i]||0));return v.toString();
  }
  return String(u32le(b,o+4)*4294967296+u32le(b,o));
}
function jbcUsbPortText(v){v=Number(v);return Number.isFinite(v)?String(v+1):'-'}
function jbcUsbToolErrorNameDiag(v){
  v=Number(v)||0;const n={0:'OK',1:'SHORTCIRCUIT',2:'SHORTCIRCUIT_NR',3:'OPENCIRCUIT',4:'NO_TOOL',5:'WRONGTOOL',6:'DETECTIONTOOL',7:'MAXPOWER',8:'STOPOVERLOAD_MOS',9:'TIN_FEEDER_CLOGGING',21:'AIR_PUMP_ERROR',22:'PROTECION_TC_HIGH',23:'REGULATION_TC_HIGH',24:'EXTERNAL_TC_MISSING',25:'SELECTED_TEMP_NOT_REACHED',26:'HIGH_HEATER_INTENSITY',27:'LOW_HEATER_RESISTANCE',28:'WRONG_HEATER',29:'NOTOOL_HA',30:'DETECTIONTOOL_HA',41:'SELECTED_TEMP_NOT_REACHED_PH',42:'LOW_HEATER_INTENSITY',43:'TC1_NOT_CONNECTED',44:'TC2_NOT_CONNECTED',45:'TC3_NOT_CONNECTED',46:'TC4_NOT_CONNECTED',47:'TC1_LIMIT_REACHED',48:'TC2_LIMIT_REACHED',49:'TC3_LIMIT_REACHED',50:'TC4_LIMIT_REACHED'};
  return n[v]||('ERROR_0x'+hex2(v));
}
function jbcUsbStationErrorNameDiag(v){v=Number(v);const n={0:'OK',1:'STOPOVERLOAD_TRAFO',2:'WRONGSENSOR_TRAFO',3:'MEMORY',4:'MAINSFREQUENCY',5:'STATION_MODEL',6:'NOT_MCU_TOOLS'};return n[v]||('ERROR_0x'+Number(v).toString(16).toUpperCase())}
function jbcUsbModeText(p){
  let x=cleanAscii(p);if(x!==null&&x.length){let t=x.trim(),m=(t.includes(':')?t.split(':').pop():t).trim().toUpperCase();return m.startsWith('C')?'CONTROL':(m.startsWith('M')?'MONITOR':t)}
  return p.length?(p.map(hex2).join(' ')):'-';
}
function jbcUsbRobotConfigDiag(p){
  if(p.length!==7)return p.map(hex2).join(' ');
  let digit=v=>{v=Number(v||0)&255;return v>=48&&v<=57?v-48:(v<=9?v:NaN)},speeds=[1200,2400,4800,9600,19200,38400,57600,115200,230400,250000,460800,500000];
  let sv=p[0],db=digit(p[1]),pc=String.fromCharCode(p[2]).toUpperCase(),par=pc==='E'?u('Gerade','Even'):(pc==='O'?u('Ungerade','Odd'):u('Keine','None')),stop=(p[3]===2||p[3]===50)?2:1,rs485=(p[4]===1||p[4]===49),a1=digit(p[5]),a2=digit(p[6]),addr=Number.isFinite(a1)&&Number.isFinite(a2)?a1*10+a2:'-';
  return (speeds[sv]?(speeds[sv]+' bps'):('Speed #'+sv))+' · Data '+(Number.isFinite(db)?db:'-')+' · '+par+' · Stop '+stop+' · '+(rs485?'RS485 · Addr '+addr:'RS232');
}
function jbcUsbReqSelector(p,f=null){
  let h='';if(p.length)h+=jbcField('Port',jbcUsbPortText(p[0]));if(p.length>1)h+=jbcField(u('Werkzeug','Tool'),jbcUsbToolNameFrame(f,p[1]));return h;
}
function jbcUsbCounterField(label,p,o=0,cycles=false){let v=u32le(p,o);return jbcField(label,String(v)+(cycles?'':' min'),'primary')}
function jbcUsbTempDiag(v,signed=false){
  v=Number(v);if(!Number.isFinite(v))return '-';if(!signed&&v===65535)return '-';
  let c=v/9,txt=Math.abs(c-Math.round(c))<0.005?String(Math.round(c)):c.toFixed(1);
  return txt+' °C · raw '+v;
}
function jbcUsbPercentDiag(v){v=Number(v)||0;return (v/10).toFixed(1)+' % · raw '+v;}
function jbcUsbBoolRaw(v){return jbcBool(Number(v)!==0)+' · raw 0x'+hex2(v);}
function jbcUsbTempUnitDiag(v){v=Number(v)||0;return (v===67||v===0)?'°C':((v===70||v===1)?'°F':('0x'+hex2(v)));}
function jbcUsbSoldStatusDiag(raw,proto){
  raw=Number(raw)||0;let a=[];
  if(proto==='P01'){
    if(raw&1)a.push('SLEEP');if(raw&2)a.push('HIBERNATION');if(raw&4)a.push('EXTRACTOR');if(raw&8)a.push('DESOLDER');
  }else{
    if(raw&1)a.push('STAND');if(raw&2)a.push('SLEEP');if(raw&4)a.push('HIBERNATION');if(raw&8)a.push('EXTRACTOR');if(raw&16)a.push('DESOLDER');if(raw&64)a.push('SOLDERING');if(raw&128)a.push('CALIBRATING');
  }
  return (a.length?a.join(' · '):'WORK')+' · raw 0x'+hex2(raw);
}
function jbcUsbHaStatusDiag(raw){
  raw=Number(raw)||0;let a=[];if(raw&1)a.push('HEATER');if(raw&2)a.push('HEATER_REQUESTED');if(raw&4)a.push('COOLING');if(raw&8)a.push('SUCTION');if(raw&16)a.push('SUCTION_REQUESTED');if(raw&32)a.push('PEDAL_CONNECTED');if(raw&64)a.push('PEDAL_PRESSED');if(raw&128)a.push('STAND');return (a.length?a.join(' · '):'-')+' · raw 0x'+hex2(raw);
}
function jbcUsbPhStatusDiag(raw){
  raw=Number(raw)||0;let a=[];if(raw&1)a.push('HEATER');if(raw&2)a.push('ZONE_B');if(raw&4)a.push('ZONE_A');if(raw&8)a.push('FAN');if(raw&16)a.push('PEDAL_CONNECTED');if(raw&32)a.push('PEDAL_PRESSED');return (a.length?a.join(' · '):'-')+' · raw 0x'+hex2(raw);
}
function jbcUsbToolStatusDiag(f,raw){
  let fam=jbcUsbFamilyForEvent(f&&f.e);if(fam==='SOLD')return jbcUsbSoldStatusDiag(raw,f&&f.protocol);if(fam==='HA')return jbcUsbHaStatusDiag(raw);return '0x'+hex2(raw);
}
function jbcUsbAsciiTrim(p){let x=cleanAscii(p);return x===null?null:x.replace(/[\u0000\s]+$/g,'').trim();}
function jbcUsbPeripheralTypeDiag(a,b){let s=String.fromCharCode(Number(a||0),Number(b||0));return ({PD:'PD',MS:'MS',MN:'MN',FS:'FS',MV:'MV'})[s]||s||'-';}
function jbcUsbPeripheralStatusDiag(v){v=Number(v)||0;let c=String.fromCharCode(v);return c==='C'?'CONNECTED':(c==='O'?'OPEN':(c==='K'?'OK':('0x'+hex2(v))));}
function jbcUsbMatchingRequest(f){
  if(!f||!f.e)return null;let ev=(lastDiag&&lastDiag.events)||[],best=null,bestMs=-1,ms0=Number(f.e.ms),addr=Number(f.e.addr),proto=f.protocol;
  for(let e of ev){
    if(Number(e.addr)!==addr||String(e.dir)!=='UART_TX')continue;
    let x=jbcFrame(e);if(!x||x.protocol!==proto||Number(x.ctrl)!==Number(f.ctrl))continue;
    let ms=Number(e.ms);if(ms>ms0||ms<ms0-2000)continue;
    if(proto==='P02'&&Number(x.fid)!==Number(f.fid))continue;
    if(ms>=bestMs){best=x;bestMs=ms;}
  }
  return best;
}
function jbcUsbRequestPayload(f){let x=jbcUsbMatchingRequest(f);return x?(x.payload||[]):[];}
function jbcUsbContextPort(f,fallback=null){let q=jbcUsbRequestPayload(f);if(q.length)return Number(q[0]);return fallback===null?null:Number(fallback);}
function jbcUsbPeripheralConfigDiag(p){
  if(!p||p.length!==31)return null;let id=Number(p[30]),hexok=true;for(let i=0;i<24;i++)if(!((p[i]>=48&&p[i]<=57)||(p[i]>=65&&p[i]<=70)||(p[i]>=97&&p[i]<=102))){hexok=false;break;}if(hexok)for(let i=24;i<30;i++)if(p[i]!==32){hexok=false;break;}
  if(hexok){let dev=String.fromCharCode.apply(null,p.slice(0,24));return {h:jbcField('ID',String(id+1),'primary')+jbcField(u('Typ','Type'),'FAE')+jbcTextField('Device ID',dev),summary:'FAE '+dev};}
  let asc=(a,b)=>String.fromCharCode.apply(null,p.slice(a,b)),num2=(a)=>{let t=asc(a,a+2);return /^\d\d$/.test(t)?String(Number(t)):t;},type=asc(20,22),port=asc(22,24),fn=asc(24,26),act=asc(26,28),delay=num2(28);
  let h=jbcField('ID',String(id+1),'primary')+jbcField(u('Version','Version'),num2(0))+jbcTextField('UID hash',asc(2,6))+jbcTextField(u('Datum/Zeit','Date/time'),asc(6,20))+jbcField(u('Typ','Type'),type)+jbcField('Port',port==='00'||port==='01'||port==='02'||port==='03'?String(Number(port)+1):port)+jbcField(u('Funktion','Function'),fn)+jbcField(u('Aktivierung','Activation'),act)+jbcField(u('Verzögerung','Delay'),delay+' s');
  return {h:h,summary:type+' · P'+(port==='00'||port==='01'||port==='02'||port==='03'?String(Number(port)+1):port)};
}
function jbcUsbContiMaskBefore(f){
  let ev=(lastDiag&&lastDiag.events)||[],best=-1,mask=0,target=Number(f&&f.e&&f.e.ms);for(let e of ev){if(Number(e.addr)!==Number(f&&f.e&&f.e.addr)||String(e.dir)!=='UART_RX'||![1,2].includes(Number(e.status)))continue;let x=jbcFrame(e);if(!x||Number(x.ctrl)!==0x80||x.payload.length<2)continue;let ms=Number(e.ms);if(ms<=target&&ms>=best){best=ms;mask=Number(x.payload[1])&15;}}return mask;
}
function jbcUsbContiPorts(f,count){let mask=jbcUsbContiMaskBefore(f),out=[];if(mask){for(let p=0;p<4;p++)if(mask&(1<<p))out.push(p);}if(out.length!==count){out=[];for(let p=0;p<count;p++)out.push(p);}return out;}
function jbcUsbContiInfoDecode(f,response,fam){
  if(!response)return null;let p=f.payload||[],h='',summary='';if(!p.length)return null;
  if(fam==='SOLD'){
    let model=jbcUsbModelForEvent(f&&f.e);if(model==='ALE')return null;
    let block=f.protocol==='P01'?9:10;if(p.length<1||((p.length-1)%block)!==0)return null;let count=(p.length-1)/block,ports=jbcUsbContiPorts(f,count);h=jbcField(u('Sequenz','Sequence'),String(p[0]),'primary');let sums=[];
    for(let i=0;i<count;i++){let b=1+i*block,pa=u16le(p,b),pb=u16le(p,b+2),wa=u16le(p,b+4),wb=u16le(p,b+6),st=p[b+8],port=ports[i];h+=jbcField('P'+(port+1)+' Temp A',jbcUsbTempDiag(pa))+jbcField('P'+(port+1)+' Temp B',jbcUsbTempDiag(pb))+jbcField('P'+(port+1)+' Power A',jbcUsbPercentDiag(wa))+jbcField('P'+(port+1)+' Power B',jbcUsbPercentDiag(wb))+jbcField('P'+(port+1)+' Status',jbcUsbSoldStatusDiag(st,f.protocol));sums.push('P'+(port+1)+' '+jbcUsbTempDiag(pa).split(' · ')[0]+' '+jbcUsbSoldStatusDiag(st,f.protocol).split(' · raw')[0]);}
    return {h:h,format:'SOLD ContiInfo '+f.protocol,summary:sums.join(' | ')};
  }
  if(fam==='HA'){
    let block=14;if(p.length<1||((p.length-1)%block)!==0)return null;let count=(p.length-1)/block,ports=jbcUsbContiPorts(f,count);h=jbcField(u('Sequenz','Sequence'),String(p[0]),'primary');let sums=[];
    for(let i=0;i<count;i++){let b=1+i*block,port=ports[i],temp=u16le(p,b),flow=u16le(p,b+2),power=u16le(p,b+4),ext1=u16le(p,b+6),ext2=u16le(p,b+8),tts=u16le(p,b+10),st=p[b+12];h+=jbcField('P'+(port+1)+' Temp',jbcUsbTempDiag(temp))+jbcField('P'+(port+1)+' Flow',jbcUsbPercentDiag(flow))+jbcField('P'+(port+1)+' Power',jbcUsbPercentDiag(power))+jbcField('P'+(port+1)+' Ext TC1',ext1===65535?'-':jbcUsbTempDiag(ext1))+jbcField('P'+(port+1)+' Ext TC2',ext2===65535?'-':jbcUsbTempDiag(ext2))+jbcField('P'+(port+1)+' TimeToStop',tts+' s')+jbcField('P'+(port+1)+' Status',jbcUsbHaStatusDiag(st));sums.push('P'+(port+1)+' '+jbcUsbTempDiag(temp).split(' · ')[0]+' '+jbcUsbPercentDiag(flow).split(' · ')[0]);}
    return {h:h,format:'HA ContiInfo P02',summary:sums.join(' | ')};
  }
  return null;
}
function jbcUsbInfoPortDecode(f,response,fam){
  let p=f.payload||[],h='',summary='',format=(fam==='CL'?'CleanerMode':(fam==='FE'?'SuctionLevel':(fam==='SF'?'DispenserMode':(fam+' InfoPort'))));
  if(!response){
    if(fam==='FE'||fam==='SF'||fam==='CL')return {h:'<span class="diag-result-sub">'+u('stationsweite Abfrage','station-wide read')+'</span>',format:format,summary:'station-wide'};
    if(fam==='PH')return {h:p.length?jbcField('Channel',String(Number(p[0])+1)):'<span class="diag-result-sub">'+u('keine Auswahl','no selector')+'</span>',format:format+' selector',summary:p.length?('Channel '+(Number(p[0])+1)):''};
    h=jbcUsbReqSelector(p,f);return {h:h||'<span class="diag-result-sub">'+u('keine Auswahl','no selector')+'</span>',format:format+' selector',summary:p.length?('Port '+jbcUsbPortText(p[0])):''};
  }
  if(fam==='SOLD'&&(p.length===12||p.length===14||p.length===15)){
    let tool=p[0],err=p[1],temp=u16le(p,2),power=u16le(p,6),st=p[10],proto=f.protocol||'P02',port=(p.length===15?Number(p[14]):null);
    h=jbcField(u('Werkzeug','Tool'),jbcUsbToolNameFrame(f,tool),'primary')+jbcField(u('Fehler','Error'),jbcUsbToolErrorNameDiag(err),err?'warn':'ok')+jbcField(u('Temperatur','Temperature'),jbcUsbTempDiag(temp),'primary')+jbcField(u('Leistung','Power'),jbcUsbPercentDiag(power))+jbcField('Status',jbcUsbSoldStatusDiag(st,proto));
    if(proto==='P02'&&(st&0x20))h+=jbcField('QST EnabledPort',u('aus','off')+' · bit5=1');else if(proto==='P02')h+=jbcField('QST EnabledPort',u('an','on')+' · bit5=0');
    if(p.length===15)h+=jbcField('Port',jbcUsbPortText(port));else {let q=jbcUsbContextPort(f);if(q!==null)h+=jbcField('Port',jbcUsbPortText(q));}
    if(p.length>=12)h+=jbcField(u('Reserve','Reserved'),'0x'+hex2(p[11]));
    summary=jbcUsbToolNameFrame(f,tool)+' · '+jbcUsbTempDiag(temp).split(' · ')[0]+' · '+jbcUsbSoldStatusDiag(st,proto).split(' · raw')[0];
  }else if(fam==='HA'&&(p.length===14||p.length===16)){
    let tool=p[0],err=p[1],temp=u16le(p,2),protect=u16le(p,4),power=u16le(p,6),flow=u16le(p,8),tts=u16le(p,10),st=p[12];
    h=jbcField(u('Werkzeug','Tool'),jbcUsbToolNameFrame(f,tool),'primary')+jbcField(u('Fehler','Error'),jbcUsbToolErrorNameDiag(err),err?'warn':'ok')+jbcField(u('Temperatur','Temperature'),jbcUsbTempDiag(temp),'primary')+jbcField(u('Schutztemperatur','Protection temp'),jbcUsbTempDiag(protect))+jbcField(u('Leistung','Power'),jbcUsbPercentDiag(power))+jbcField(u('Luftstrom','Flow'),jbcUsbPercentDiag(flow))+jbcField('TimeToStop',tts+' s')+jbcField('Status',jbcUsbHaStatusDiag(st));
    if(p.length===16)h+=jbcField('Port',jbcUsbPortText(p[15]));else {let q=jbcUsbContextPort(f);if(q!==null)h+=jbcField('Port',jbcUsbPortText(q));}
    summary=jbcUsbToolNameFrame(f,tool)+' · '+jbcUsbTempDiag(temp).split(' · ')[0]+' · '+jbcUsbPercentDiag(flow).split(' · ')[0];
  }else if(fam==='PH'&&p.length===17){
    let err=p[0],tc=[u16le(p,1),u16le(p,3),u16le(p,5),u16le(p,7)],power=u16le(p,9),tts=u32le(p,11),st=p[15];
    h=jbcField(u('Fehler','Error'),jbcUsbToolErrorNameDiag(err),err?'warn':'ok');tc.forEach((v,i)=>h+=jbcField('TC'+(i+1),v===65535?'-':jbcUsbTempDiag(v),i===0?'primary':''));h+=jbcField(u('Leistung','Power'),jbcUsbPercentDiag(power))+jbcField('TimeToStop',tts+' s')+jbcField('Status',jbcUsbPhStatusDiag(st));let q=jbcUsbContextPort(f);if(q!==null)h+=jbcField('Channel',String(q+1));
    summary='TC1 '+(tc[0]===65535?'-':jbcUsbTempDiag(tc[0]).split(' · ')[0])+' · '+jbcUsbPercentDiag(power).split(' · ')[0];
  }else if(fam==='CL'&&p.length===1){h=jbcField(u('Reinigermodus','Cleaner mode'),String(p[0])+' · raw 0x'+hex2(p[0]),'primary');summary='Cleaner mode '+p[0];}
  else if(fam==='FE'&&p.length===1){h=jbcField(u('Absaugstufe','Suction level'),jbcSuctionLevel(p[0]),'primary')+jbcField('Raw','0x'+hex2(p[0]));summary=jbcSuctionLevel(p[0]);}
  else if(fam==='SF'&&p.length===2){h=jbcField('DispenserMode',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+jbcField(u('Programm','Program'),String(p[1]));summary='Mode '+p[0]+' · Program '+p[1];}
  return h?{h:h,format:format,summary:summary}:null;
}
function jbcUsbPayloadInfo(f,role){
  let p=(f&&f.payload)||[],c=Number(f&&f.ctrl||0),response=role==='response',fam=jbcUsbFamilyForEvent(f&&f.e),h='',format=(fam||'JBC')+' '+(jbcUsbCommandName(f)||('0x'+hex2(c))),summary='';
  const raw=()=>p.map(hex2).join(' '), bool=(l,v)=>jbcField(l,jbcBool(v),Number(v)?'bool-on':'bool-off'), portTail=()=>p.length?jbcField('Port',jbcUsbPortText(p[p.length-1])):'', req=()=>jbcUsbRequestPayload(f), reqPort=()=>{let q=req();return q.length?Number(q[0]):null;};
  const addContext=(label='Port')=>{let v=reqPort();return v===null?'':jbcField(label,jbcUsbPortText(v));};
  const stationError=()=>{let v=p.length>=2?u16le(p):Number(p[0]||0);h=jbcField(u('Stationsfehler','Station error'),jbcUsbStationErrorNameDiag(v),v?'warn':'ok');summary=jbcUsbStationErrorNameDiag(v);};
  const robotStatus=()=>{let on=p.length&&(p[0]===67||p[0]===99);h=jbcField('Robot Status',on?'ON':'OFF',on?'primary':'');summary=on?'ON':'OFF';};

  if(c===0x30){let x=jbcUsbInfoPortDecode(f,response,fam);if(x){h=x.h;format=x.format;summary=x.summary;}}
  if(!h&&(c===0x80||c===0x81)){
    format=c===0x80?'ReadContiMode':'WriteContiMode';
    if(p.length){h=jbcField('Conti',jbcUsbContiSpeed(p[0]),p[0]?'primary':'')+(p.length>1?jbcField('Port mask','0x'+hex2(p[1])):'')+(p.length>2?jbcField(u('Erweitert','Extended'),'0x'+hex2(p[2])):'');summary=jbcUsbContiSpeed(p[0]);}
  }
  if(!h&&c===0x82){let x=jbcUsbContiInfoDecode(f,response,fam);if(x){h=x.h;format=x.format;summary=x.summary;}else if(response){format=(fam||'JBC')+' ContiInfo';h=jbcField('Payload',p.length+' B','primary')+jbcField('HEX',raw());summary='Conti '+p.length+' B';}}
  if(!h&&c===0x21){let x=cleanAscii(p);if(x!==null&&p.length){format='Firmware ASCII';h=jbcTextField('Firmware',x);summary=x;}}
  if(!h&&(c===0x1C||c===0x1D||c===0x1E||c===0x1F||c===0xB9)){let x=cleanAscii(p);if(x!==null&&p.length){format='Device ID ASCII';h=jbcTextField('Device ID',x);summary=x;}}
  if(!h&&((fam==='SOLD'&&c===0xB1)||(fam==='HA'&&c===0xB1)||(fam==='PH'&&c===0xB1)||(fam==='CL'&&c===0x54)||((fam==='FE'||fam==='SF')&&c===0x5B))){let x=cleanAscii(p);if(x!==null&&p.length){format='Device name ASCII';h=jbcTextField(u('Gerätename','Device name'),x);summary=x;}}

  if(!h&&fam==='SOLD'){
    let p01=f.protocol==='P01';
    if(response){
      if(p01&&c===0x31&&p.length===2){let v=u16le(p);h=jbcField('FixedTemp',v===65535?u('aus','off'):jbcUsbTempDiag(v),'primary');summary=v===65535?'OFF':jbcUsbTempDiag(v).split(' · ')[0];}
      else if(p01&&c===0x33&&p.length===1){h=jbcField(u('Ausgewähltes Level','Selected level'),p[0]===255?'NONE':String(Number(p[0])+1),'primary');summary=p[0]===255?'NONE':'L'+(Number(p[0])+1);}
      else if(p01&&(c===0x35||c===0x37||c===0x39)&&p.length===2){let v=u16le(p),n=c===0x35?1:(c===0x37?2:3);h=jbcField('Level '+n,jbcUsbTempDiag(v),'primary');summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if(!p01&&c===0x33&&p.length===13){h=bool('Levels',p[0])+jbcField(u('Ausgewählt','Selected'),p[1]===255?'NONE':String(Number(p[1])+1),'primary');for(let i=0;i<3;i++){let o=2+i*3;h+=jbcField('L'+(i+1),(p[o]?'ON · ':'OFF · ')+jbcUsbTempDiag(u16le(p,o+1)));}summary='Levels '+jbcBool(p[0]);}
      else if(!p01&&c===0x35&&p.length>=1){h=bool('ProfileMode',p[0])+(p.length>1?portTail():'');summary=jbcBool(p[0]);}
      else if((c===0x40||c===0x44)){
        if(p01&&p.length===2){let v=u16le(p),on=v!==65535;h=bool(c===0x40?'Sleep':'Hibernation',on)+jbcField(u('Verzögerung','Delay'),on?(v+' min'):'-','primary');summary=on?v+' min':'OFF';}
        else if(!p01&&p.length===4){let mins=p[0]===255?0:Number(p[0]),on=p[0]!==255&&p[1]===1,port=reqPort();h=bool(c===0x40?'Sleep':'Hibernation',on)+jbcField(u('Verzögerung','Delay'),mins+' min','primary')+(port===null?'':jbcField('Port',jbcUsbPortText(port)))+jbcField('Raw state','0x'+hex2(p[1]));summary=on?mins+' min':'OFF';}
      }
      else if(c===0x42&&((p01&&p.length===2)||(!p01&&p.length>=2))){let v=u16le(p);h=jbcField('SleepTemp',jbcUsbTempDiag(v),'primary')+(!p01?addContext():'');summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if(c===0x46&&p.length>=2){let v=i16le(p);h=jbcField('AdjustTemp',jbcUsbTempDiag(v,true),'primary')+(!p01?addContext():'');summary=jbcUsbTempDiag(v,true).split(' · ')[0];}
      else if(c===0x48&&!p01&&p.length===11){h=bool('Cartridge',p[0])+jbcField('JBC code',String(i16le(p,1)),'primary')+jbcField('Adj 300',jbcUsbTempDiag(i16le(p,3),true))+jbcField('Adj 400',jbcUsbTempDiag(i16le(p,5),true))+jbcField('Group',String(p[7]))+jbcField('Family',String(p[8]))+jbcField('Port',jbcUsbPortText(p[9]))+jbcField(u('Werkzeug','Tool'),jbcUsbToolNameDiag(p[10]));summary='Cartridge '+i16le(p,1);}
      else if(c===0x50&&p.length>=2){let v=u16le(p);h=jbcField('SelectTemp',jbcUsbTempDiag(v),'primary')+(!p01&&p.length>=3?jbcField('Port',jbcUsbPortText(p[p.length-1])):'');summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if(c===0x52&&((p01&&p.length===4)||(!p01&&p.length===5))){let a=i16le(p),b=i16le(p,2);h=jbcField('Tip A',jbcUsbTempDiag(a,true),'primary')+jbcField('Tip B',jbcUsbTempDiag(b,true))+(!p01?jbcField('Port',jbcUsbPortText(p[4])):'');summary=jbcUsbTempDiag(a,true).split(' · ')[0]+' / '+jbcUsbTempDiag(b,true).split(' · ')[0];}
      else if(!p01&&c===0x53&&p.length===5){h=jbcField('Current A',i16le(p)+' mA','primary')+jbcField('Current B',i16le(p,2)+' mA')+jbcField('Port',jbcUsbPortText(p[4]));summary=i16le(p)+' / '+i16le(p,2)+' mA';}
      else if(c===0x54&&((p01&&p.length===4)||(!p01&&p.length===5))){let a=i16le(p),b=i16le(p,2);h=jbcField('Power A',jbcUsbPercentDiag(a),'primary')+jbcField('Power B',jbcUsbPercentDiag(b))+(!p01?jbcField('Port',jbcUsbPortText(p[4])):'');summary=jbcUsbPercentDiag(a).split(' · ')[0]+' / '+jbcUsbPercentDiag(b).split(' · ')[0];}
      else if((c===0x55||c===0x56||c===0x57)&&p.length>=(p01?1:2)){let v=p[0];if(c===0x55){h=jbcField(u('Werkzeug','Tool'),jbcUsbToolNameFrame(f,v),'primary');summary=jbcUsbToolNameFrame(f,v);}else if(c===0x56){h=jbcField(u('Werkzeugfehler','Tool error'),jbcUsbToolErrorNameDiag(v),v?'warn':'ok');summary=jbcUsbToolErrorNameDiag(v);}else{h=jbcField('ToolStatus',jbcUsbToolStatusDiag(f,v),'primary');summary=jbcUsbToolStatusDiag(f,v).split(' · raw')[0];}if(!p01)h+=jbcField('Port',jbcUsbPortText(p[p.length-1]));}
      else if(p01&&c===0x58&&p.length===2){let v=u16le(p);h=jbcField('MosTemp',jbcUsbTempDiag(v),'primary')+addContext();summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if(!p01&&c===0x59&&p.length===3){let v=u16le(p);h=jbcField('MosTemp',jbcUsbTempDiag(v),'primary')+jbcField('Port',jbcUsbPortText(p[2]));summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if((p01&&c===0x59&&p.length===3)||(!p01&&c===0x5A&&p.length===4)){let t=u16le(p),fm=p[2],mode=fm?String.fromCharCode(fm):'-',port=!p01?Number(p[3]):reqPort();h=jbcField(u('Countdown','Countdown'),(t===65535?0:t)+' s','primary')+jbcField(u('Nächster Modus','Next mode'),mode)+(port===null?'':jbcField('Port',jbcUsbPortText(port)));summary=(t===65535?0:t)+' s → '+mode;}
      else if(c===0x60&&p.length===1){h=bool('RemoteMode',p[0]);summary=jbcBool(p[0]);}
      else if(p01&&c===0x62&&p.length){h=jbcField('RemoteStatus',jbcUsbBoolRaw(p[0]),'primary');summary=jbcBool(p[0]);}
      else if(!p01&&c===0x70&&p.length===12){h=jbcField('WorkingMode',String(p[0]),'primary')+jbcField(u('Programm','Program'),String(p[1]))+jbcField(u('Lieferlänge raw','Delivery length raw'),String(u16le(p,2)))+jbcField(u('Liefergeschwindigkeit raw','Delivery speed raw'),String(u16le(p,4)))+jbcField('Tin diameter',String(p[6]))+jbcField('Remove length',String(p[7]))+bool('Speed/Length readonly',p[8])+jbcField('Selectable programs','0x'+u16le(p,9).toString(16).toUpperCase())+bool('Clogging detection',p[11]);summary='ALE mode '+p[0]+' · program '+p[1];}
      else if(!p01&&c===0x72&&p.length===14){h=jbcField(u('Programm','Program'),String(p[13]),'primary')+jbcField('Port',jbcUsbPortText(p[12]));for(let i=0;i<3;i++)h+=jbcField(u('Schritt','Step')+' '+(i+1),u16le(p,i*2)+' raw @ '+u16le(p,6+i*2)+' raw');summary='ALE program '+p[13];}
      else if(!p01&&(c===0x83||c===0x85)&&p.length===5){let t=i16le(p),d=i16le(p,2);h=jbcField(u('Temperatur','Temperature'),jbcUsbTempDiag(t,true),'primary')+jbcField(u('Verzögerung','Delay'),(d/10).toFixed(1)+' s · raw '+d)+jbcField('Port',jbcUsbPortText(p[4]));summary=jbcUsbTempDiag(t,true).split(' · ')[0]+' / '+(d/10).toFixed(1)+' s';}
      else if(((!p01&&c===0x88)||(p01&&c===0xD4))&&p.length===2){let port=reqPort();if(!p01){let on=p[0]===0;h=bool('EnabledPort',on)+jbcField('Raw','0x'+hex2(p[0]))+(port===null?'':jbcField('Port',jbcUsbPortText(port)));summary=on?'Enabled':'Disabled';}else{h=jbcField('Legacy QST Lock',u('Status wird aus QST-Stationzustand abgeleitet','status is derived from station QST state'),'primary')+jbcField('Raw','0x'+hex2(p[0])+' '+hex2(p[1]))+(port===null?'':jbcField('Port',jbcUsbPortText(port)));summary='Legacy QST lock';}}
      else if(!p01&&c===0x8A&&p.length===7){h=bool('Assistant',p[0]===1||p[0]===49)+jbcField('Warning',String(i16le(p,1)))+jbcField('Error',String(i16le(p,3)))+jbcField('Port',jbcUsbPortText(p[5]))+jbcField(u('Reserve','Reserved'),'0x'+hex2(p[6]));summary='Assistant '+jbcBool(p[0]===1||p[0]===49);}
      else if(!p01&&c===0x8C&&p.length===7){h=jbcField('Similarity',String(i16le(p)),'primary')+jbcField('Time',(i16le(p,2)/10).toFixed(1)+' s · raw '+i16le(p,2))+jbcField('Energy',String(i16le(p,4)))+jbcField('Port',jbcUsbPortText(p[6]));summary='Similarity '+i16le(p);}
      else if(!p01&&c===0x8D&&p.length===2){h=jbcField('Warning','0x'+hex2(p[0]),p[0]?'warn':'ok')+jbcField('Port',jbcUsbPortText(p[1]));summary='0x'+hex2(p[0]);}
      else if(!p01&&c===0x9A&&p.length>=1){let x=cleanAscii(p.slice(0,-1));h=jbcTextField(u('Profil','Profile'),x===null?raw():x)+portTail();summary=x||'Profile';}
      else if(((!p01&&(c===0x9C||c===0x9E))||(p01&&(c===0xD0||c===0xD2)))&&p.length===1){h=bool(jbcUsbCommandName(f),p[0]);summary=jbcBool(p[0]);}
      else if(p01&&c===0xA0&&p.length===1){h=jbcField(u('Temperatureinheit','Temperature unit'),jbcUsbTempUnitDiag(p[0]),'primary')+jbcField('Raw','0x'+hex2(p[0]));summary=jbcUsbTempUnitDiag(p[0]);}
      else if(!p01&&c===0xA0&&p.length===30){h=jbcField('Interface bytes',p.slice(0,7).map(hex2).join(' '),'primary')+jbcField('Graph Temp Max',jbcUsbTempDiag(u16le(p,7)))+jbcField('Graph Temp Min',jbcUsbTempDiag(u16le(p,9)))+jbcField('Graph Temp Range',jbcUsbTempDiag(u16le(p,11)))+jbcField('Graph Power Max',String(u16le(p,13))+' raw')+jbcField('Graph Power Min',String(u16le(p,15))+' raw')+jbcField(u('Reserve','Reserved'),p.slice(17).map(hex2).join(' '));summary='Interface · '+jbcUsbTempDiag(u16le(p,9)).split(' · ')[0]+'–'+jbcUsbTempDiag(u16le(p,7)).split(' · ')[0];}
      else if((c===0xA2||c===0xA4||c===0xAF||c===0xB7||c===0xB8)&&p.length===2){let v=u16le(p);h=jbcField(jbcUsbCommandName(f),jbcUsbTempDiag(v),'primary');summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if(p01&&(c===0xA6||c===0xA8||c===0xB3||c===0xBD)&&p.length===1){h=bool(jbcUsbCommandName(f),p[0]);summary=jbcBool(p[0]);}
      else if(!p01&&c===0xA6&&p.length===5){h=bool('AutoClean',p[0])+jbcField(u('Temperatur','Temperature'),jbcUsbTempDiag(u16le(p,1)),'primary')+jbcField(u('Dauer','Duration'),u16le(p,3)+' s');summary=jbcBool(p[0])+' · '+u16le(p,3)+' s';}
      else if(!p01&&c===0xA8&&p.length===1){h=bool('PINEnabled',p[0]);summary=jbcBool(p[0]);}
      else if(c===0xAA&&p.length===2){let v=u16le(p);h=jbcField('PowerLimit',String(v)+' raw','primary');summary=String(v)+' raw';}
      else if(c===0xAC&&p.length===4){let x=cleanAscii(p);h=jbcTextField('PIN',x===null?raw():x);summary=x||'PIN';}
      else if(c===0xAE&&p.length){stationError();}
      else if(!p01&&c===0xBA&&p.length===1){h=jbcField('GroundType',String(p[0])+' · raw 0x'+hex2(p[0]),'primary');summary=String(p[0]);}
      else if(!p01&&c===0xBB&&p.length===7){let yr=u16le(p),mo=p[2],da=p[3],hh=p[4],mm=p[5],ss=p[6];h=jbcField(u('Datum/Zeit','Date/time'),String(da).padStart(2,'0')+'.'+String(mo).padStart(2,'0')+'.'+String(yr)+' '+String(hh).padStart(2,'0')+':'+String(mm).padStart(2,'0')+':'+String(ss).padStart(2,'0'),'primary');summary='DateTime';}
      else if(!p01&&c===0xBE&&p.length===4){h=jbcField('StationInterface',p.map(hex2).join(' '),'primary')+jbcField('Byte0',String(p[0]))+jbcField('Byte1',String(p[1]))+jbcField('Byte2',String(p[2]))+jbcField('Byte3',String(p[3]));summary=p.map(hex2).join(' ');}
      else if(c===0xE0&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
      else if(!p01&&c===0xE3&&p.length){let x=jbcUsbAsciiTrim(p);h=jbcTextField('FrontalConnection',x===null?raw():x);summary=x||raw();}
      else if(!p01&&c===0xE7&&p.length===23){h=jbcField('Ethernet config',p.length+' B','primary')+jbcField('HEX',raw());summary='Ethernet 23 B';}
      else if(c===0xF0&&p.length===7){let x=jbcUsbRobotConfigDiag(p);h=jbcTextField('Robot Config',x);summary=x;}
      else if(c===0xF2&&p.length===1){robotStatus();}
      else if(!p01&&c===0xF9&&p.length===1){h=jbcField(u('Peripheriegeräte','Peripherals'),String(p[0]),'primary');summary=String(p[0]);}
      else if(!p01&&c===0xFA&&p.length===31){let x=jbcUsbPeripheralConfigDiag(p);if(x){h=x.h;summary=x.summary;}}
      else if(!p01&&c===0xFC&&p.length===3){h=bool(u('Aktiv','Active'),p[0])+jbcField('Status',jbcUsbPeripheralStatusDiag(p[1]),'primary')+jbcField('ID',String(Number(p[2])+1));summary='Peripheral '+(Number(p[2])+1)+' · '+jbcUsbPeripheralStatusDiag(p[1]);}
      else {
        let counterP02=[0xC0,0xC2,0xC4,0xC6,0xC8,0xCA,0xCC,0xD0,0xD2,0xD4,0xD6,0xD8,0xDA,0xDC],counterP01=[0xC0,0xC2,0xC4,0xC6,0xC8,0xCA,0xCC,0xF0,0xF2,0xF4,0xF6,0xF8,0xFA,0xFC],isCounter=(p01?counterP01:counterP02).includes(c);
        if(isCounter&&p.length>=4){let cycles=p01?[0xCA,0xCC,0xFA,0xFC].includes(c):[0xCA,0xCC,0xDA,0xDC].includes(c);if(!p01&&p.length===5){h=jbcUsbCounterField(jbcUsbCommandName(f),p,0,cycles)+jbcField('Port',jbcUsbPortText(p[4]));summary=String(u32le(p))+(cycles?' cycles':' min');}else if(p.length%4===0){let n=p.length/4;for(let i=0;i<n;i++)h+=jbcField('P'+(i+1),String(u32le(p,i*4))+(cycles?' cycles':' min'),i===0?'primary':'');summary=n+' port counters';}else{h=jbcField(jbcUsbCommandName(f),u('Gebündelter Sonderzähler','bundled special counter')+' · '+p.length+' B','primary')+jbcField('HEX',raw());summary='bundled '+p.length+' B';}}
      }
    }else if(p.length){
      if((c===0x40||c===0x42||c===0x44||c===0x46||c===0x50||c===0x55||c===0x56||c===0x57||c===0x58||c===0x59||c===0x5A||c===0x83||c===0x85||c===0x88)&&p.length>=1)h=jbcUsbReqSelector(p,f);
      else if(c===0x70&&p.length>=1)h=jbcField('Port',jbcUsbPortText(p[0]));
      else if(c===0x72&&p.length>=2)h=jbcField('Port',jbcUsbPortText(p[0]))+jbcField(u('Programm','Program'),String(p[1]));
      else if(c===0xFA||c===0xFC)h=jbcField('ID',String(Number(p[0])+1),'primary');
      else h=jbcUsbReqSelector(p,f);
    }
  }

  if(!h&&fam==='HA'){
    if(response){
      if(c===0x33&&p.length>=2){h=bool('ProfileMode',p[0])+jbcField('Port',jbcUsbPortText(p[p.length-1]));summary=jbcBool(p[0]);}
      else if((c===0x35||c===0x37)&&p.length>=2){h=bool(jbcUsbCommandName(f),p[0])+jbcField('Port',jbcUsbPortText(p[p.length-1]));summary=jbcBool(p[0]);}
      else if(c===0x39&&p.length>=2){h=jbcField('ExternalTCMode',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+jbcField('Port',jbcUsbPortText(p[p.length-1]));summary=String(p[0]);}
      else if(c===0x40&&p.length===25){h=bool('Levels',p[0])+jbcField(u('Ausgewählt','Selected'),p[1]===255?'NONE':String(Number(p[1])+1),'primary');for(let i=0;i<3;i++){let o=2+i*7,ext=u16le(p,o+5);h+=jbcField('L'+(i+1),(p[o]?'ON':'OFF')+' · '+jbcUsbTempDiag(u16le(p,o+1))+' · '+jbcUsbPercentDiag(u16le(p,o+3))+' · Ext '+(ext===65535?'-':jbcUsbTempDiag(ext)));}summary='Levels '+jbcBool(p[0]);}
      else if(c===0x42&&p.length>=2){let v=i16le(p);h=jbcField('AdjustTemp',jbcUsbTempDiag(v,true),'primary')+addContext();summary=jbcUsbTempDiag(v,true).split(' · ')[0];}
      else if(c===0x44&&p.length>=2){let v=u16le(p);h=jbcField('TimeToStop',v+' s','primary')+addContext();summary=v+' s';}
      else if(c===0x46&&p.length>=1){h=jbcField('StartMode',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+addContext();summary=String(p[0]);}
      else if((c===0x50||c===0x52||c===0x5B||c===0x5F)&&p.length>=2){let v=u16le(p),txt=(c===0x5F&&v===65535)?'-':jbcUsbTempDiag(v);h=jbcField(jbcUsbCommandName(f),txt,'primary')+(p.length>2?jbcField('Port',jbcUsbPortText(p[p.length-1])):addContext());summary=txt.split(' · ')[0];}
      else if((c===0x54||c===0x59||c===0x5D)&&p.length>=2){let v=u16le(p);if(c===0x5D&&v===65535)v=0;h=jbcField(jbcUsbCommandName(f),jbcUsbPercentDiag(v),'primary')+(p.length>2?jbcField('Port',jbcUsbPortText(p[p.length-1])):addContext());summary=jbcUsbPercentDiag(v).split(' · ')[0];}
      else if(c===0x55&&p.length>=2){h=jbcField(u('Werkzeug','Tool'),jbcUsbToolNameFrame(f,p[0]),'primary')+jbcField('Port',jbcUsbPortText(p[p.length-1]));summary=jbcUsbToolNameFrame(f,p[0]);}
      else if(c===0x56&&p.length>=2){h=jbcField(u('Werkzeugfehler','Tool error'),jbcUsbToolErrorNameDiag(p[0]),p[0]?'warn':'ok')+jbcField('Port',jbcUsbPortText(p[p.length-1]));summary=jbcUsbToolErrorNameDiag(p[0]);}
      else if(c===0x57&&p.length>=2){h=jbcField('ToolStatus',jbcUsbHaStatusDiag(p[0]),'primary')+jbcField('Port',jbcUsbPortText(p[p.length-1]));summary=jbcUsbHaStatusDiag(p[0]).split(' · raw')[0];}
      else if(c===0x60&&p.length===1){h=bool('RemoteMode',p[0]);summary=jbcBool(p[0]);}
      else if(c===0x9A&&p.length>=1){let x=cleanAscii(p.slice(0,-1));h=jbcTextField(u('Profil','Profile'),x===null?raw():x)+portTail();summary=x||'Profile';}
      else if(c===0xA0&&p.length===1){h=jbcField(u('Temperatureinheit','Temperature unit'),jbcUsbTempUnitDiag(p[0]),'primary')+jbcField('Raw','0x'+hex2(p[0]));summary=jbcUsbTempUnitDiag(p[0]);}
      else if((c===0xA2||c===0xA6)&&p.length===4){let mx=u16le(p),mn=u16le(p,2);h=jbcField('Max',jbcUsbTempDiag(mx),'primary')+jbcField('Min',jbcUsbTempDiag(mn));summary=jbcUsbTempDiag(mn).split(' · ')[0]+' – '+jbcUsbTempDiag(mx).split(' · ')[0];}
      else if(c===0xA4&&p.length===4){let mx=u16le(p),mn=u16le(p,2);h=jbcField('Max',jbcUsbPercentDiag(mx),'primary')+jbcField('Min',jbcUsbPercentDiag(mn));summary=jbcUsbPercentDiag(mn).split(' · ')[0]+' – '+jbcUsbPercentDiag(mx).split(' · ')[0];}
      else if((c===0xA8||c===0xB3)&&p.length===1){h=bool(jbcUsbCommandName(f),p[0]);summary=jbcBool(p[0]);}
      else if(c===0xAC&&p.length===4){let x=cleanAscii(p);h=jbcTextField('PIN',x===null?raw():x);summary=x||'PIN';}
      else if(c===0xAE&&p.length){stationError();}
      else if([0xC0,0xC2,0xC4,0xC6,0xD0,0xD2,0xD4,0xD6].includes(c)&&p.length===5){let cycles=[0xC4,0xC6,0xD4,0xD6].includes(c);h=jbcUsbCounterField(jbcUsbCommandName(f),p,0,cycles)+jbcField('Port',jbcUsbPortText(p[4]));summary=String(u32le(p))+(cycles?' cycles':' min');}
      else if(c===0xE0&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
      else if(c===0xF0&&p.length===7){let x=jbcUsbRobotConfigDiag(p);h=jbcTextField('Robot Config',x);summary=x;}
      else if(c===0xF2&&p.length===1){robotStatus();}
    }else if(p.length)h=jbcUsbReqSelector(p,f);
  }

  if(!h&&fam==='CL'){
    if(response){
      if((c===0x32||c===0x38)&&p.length===1){let label=c===0x32?u('Motoren','Motors'):u('Tür offen','Door open');h=bool(label,p[0]);summary=jbcBool(p[0]);}
      else if((c===0xC0||c===0xC2)&&p.length===20){let labels=[u('Plug','Plug'),u('Reinigung Continuous','Cleaning continuous'),u('Reinigung Detection','Cleaning detection'),u('Work cycles','Work cycles'),u('Door cycles','Door cycles')];for(let i=0;i<5;i++){let rv=i32le(p,i*4),dv=Math.trunc(rv/60);h+=jbcField(labels[i],dv+' · DLL /60 · raw '+rv,i===0?'primary':'');}summary='5 counters · DLL /60';}
      else if(c===0xE0&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
    }
  }

  if(!h&&fam==='PH'){
    let ctx=reqPort(),ctxField=ctx===null?'':jbcField('Channel',String(ctx+1));
    if(response){
      if(c===0x33&&p.length===2){h=jbcField('WorkMode',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+ctxField;summary=String(p[0]);}
      else if(c===0x35&&p.length===2){h=bool('HeaterStatus',p[0])+ctxField;summary=jbcBool(p[0]);}
      else if(c===0x39&&p.length===2){h=jbcField('ExternalTCMode',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+ctxField;summary=String(p[0]);}
      else if(c===0x44&&p.length===5){h=jbcField('TimeToStop',u32le(p)+' s','primary')+ctxField+jbcField(u('Reserve','Reserved'),'0x'+hex2(p[4]));summary=u32le(p)+' s';}
      else if(c===0x50&&p.length===3){let ch=p[2]<4?Number(p[2]):ctx,v=u16le(p);h=jbcField('SelectTemp',jbcUsbTempDiag(v),'primary')+(ch===null?'':jbcField('Channel',String(ch+1)));summary=jbcUsbTempDiag(v).split(' · ')[0];}
      else if(c===0x52&&p.length===3){let v=u16le(p);h=jbcField('SelectPower',jbcUsbPercentDiag(v),'primary')+ctxField;summary=jbcUsbPercentDiag(v).split(' · ')[0];}
      else if(c===0x58&&p.length===1){h=jbcField('TCWarning','0x'+hex2(p[0]),p[0]?'warn':'primary')+ctxField;summary='0x'+hex2(p[0]);}
      else if(c===0x5B&&p.length===2){h=jbcField('ActiveZones','0x'+hex2(p[0])+' · raw '+p[0],'primary')+ctxField+jbcField(u('Reserve','Reserved'),'0x'+hex2(p[1]));summary='0x'+hex2(p[0]);}
      else if(c===0x5F&&p.length===2){let v=u16le(p),txt=v===65535?'-':jbcUsbTempDiag(v);h=jbcField('ExternalAirTemp',txt,'primary')+ctxField;summary=txt.split(' · ')[0];}
      else if(c===0x60&&p.length===1){h=bool('RemoteMode',p[0]);summary=jbcBool(p[0]);}
      else if(c===0x90&&p.length>=1){let n=p[0];h=jbcField(u('Profilpunkte','Profile points'),String(n),'primary');for(let i=0;i<n&&1+i*4+3<p.length;i++)h+=jbcField('#'+(i+1),'time '+i16le(p,1+i*4)+' raw · value '+i16le(p,3+i*4)+' raw');summary=n+' points';}
      else if(c===0x92&&p.length===3){h=jbcField('Points',String(p[0]),'primary')+jbcField('Consignment',String(p[1]))+jbcField('TC regulation',String(p[2]));summary='Profile settings';}
      else if(c===0x94&&p.length>=4){let n=u16le(p,2);h=jbcField('Interval',i16le(p)+' raw','primary')+jbcField(u('Werte','Values'),String(n));for(let i=0;i<n&&4+i*2+1<p.length;i++)h+=jbcField('#'+(i+1),i16le(p,4+i*2)+' raw');summary=n+' teach values';}
      else if(c===0xA2&&p.length===4){let mx=i16le(p),mn=i16le(p,2);h=jbcField('Max',jbcUsbPercentDiag(mx),'primary')+jbcField('Min',jbcUsbPercentDiag(mn));summary=jbcUsbPercentDiag(mn).split(' · ')[0]+' – '+jbcUsbPercentDiag(mx).split(' · ')[0];}
      else if(c===0xA6&&p.length===4){let mx=u16le(p),mn=u16le(p,2);h=jbcField('Max',jbcUsbTempDiag(mx),'primary')+jbcField('Min',jbcUsbTempDiag(mn));summary=jbcUsbTempDiag(mn).split(' · ')[0]+' – '+jbcUsbTempDiag(mx).split(' · ')[0];}
      else if((c===0xA8||c===0xB3)&&p.length===1){h=bool(jbcUsbCommandName(f),p[0]);summary=jbcBool(p[0]);}
      else if(c===0xAC&&p.length===4){let x=cleanAscii(p);h=jbcTextField('PIN',x===null?raw():x);summary=x||'PIN';}
      else if(c===0xAE&&p.length){stationError();}
      else if((c===0xC0||c===0xD0)&&p.length===4){h=jbcUsbCounterField(jbcUsbCommandName(f),p)+ctxField;summary=String(u32le(p))+' min';}
      else if((c===0xC2||c===0xD2)&&p.length===12){h=jbcField('Power',u32le(p)+' min','primary')+jbcField('Temp',u32le(p,4)+' min')+jbcField('Profile',u32le(p,8)+' min')+ctxField;summary='Power/Temp/Profile minutes';}
      else if((c===0xC7||c===0xD7)&&p.length===12){h=jbcField('Power',u32le(p)+' cycles','primary')+jbcField('Temp',u32le(p,4)+' cycles')+jbcField('Profile',u32le(p,8)+' cycles')+ctxField;summary='Power/Temp/Profile cycles';}
      else if(c===0xE0&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
      else if(c===0xF0&&p.length===7){let x=jbcUsbRobotConfigDiag(p);h=jbcTextField('Robot Config',x);summary=x;}
      else if(c===0xF2&&p.length===1){robotStatus();}
    }else if(p.length){h=jbcField('Channel',String(Number(p[0])+1),'primary');}
  }

  if(!h&&fam==='FE'){
    if(response){
      if((c===0x32||c===0x33||c===0x34||c===0x41)&&p.length===2){let v=u16le(p),label=c===0x32?'Flow':(c===0x33?u('Drehzahl','Speed'):(c===0x34?u('Gewählter Flow','Selected flow'):u('Filterstatus','Filter status'))),txt=c===0x33?(v+' rpm'):((c===0x32||c===0x34)?(v+' x_mil'):(v+' raw'));h=jbcField(label,txt,'primary');summary=txt;}
      else if(c===0x36&&p.length===2){let q=reqPort();h=jbcField('StandIntakes',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+(q===null?'':jbcField('Port',jbcUsbPortText(q)))+jbcField(u('Antwort Tail','Response tail'),'0x'+hex2(p[1]));summary='StandIntakes '+p[0];}
      else if(c===0x38&&p.length===3){h=bool(u('Aktiv','Active'),p[0])+jbcField('Port',jbcUsbPortText(p[1]))+jbcField('Intake',jbcIntake(p[2]));summary=jbcBool(p[0])+' '+jbcIntake(p[2]);}
      else if(c===0x3A&&p.length===4){let q=req(),port=q.length?Number(q[0]):null,intake=q.length>1?Number(q[1]):null;h=jbcField('SuctionDelay',String(u16le(p))+' raw','primary')+(port===null?'':jbcField('Port',jbcUsbPortText(port)))+(intake===null?'':jbcField('Intake',jbcIntake(intake)))+jbcField(u('Antwort Tail','Response tail'),p.slice(2).map(hex2).join(' '));summary=u16le(p)+' raw';}
      else if(c===0x3C&&p.length===4){h=jbcField('TimeToStop',String(u16le(p))+' raw','primary')+jbcField('Port',jbcUsbPortText(p[2]))+jbcField('Intake',jbcIntake(p[3]));summary=u16le(p)+' raw';}
      else if((c===0x3D||c===0x3F)&&p.length===2){let txt=c===0x3D?(p[0]===0?'HOLD_DOWN':(p[0]===1?'PULSE':String(p[0]))):jbcPedalMode(p[0]);h=jbcField(jbcUsbCommandName(f),txt+' · raw 0x'+hex2(p[0]),'primary')+jbcField('Port',jbcUsbPortText(p[1]));summary=txt;}
      else if(c===0x44&&p.length===2){let q=reqPort();h=bool(u('Pedal verbunden','Pedal connected'),p[0])+(q===null?'':jbcField('Port',jbcUsbPortText(q)))+jbcField(u('Antwort Tail','Response tail'),'0x'+hex2(p[1]));summary=jbcBool(p[0]);}
      else if(c===0x51&&p.length===4){let x=cleanAscii(p);h=x!==null?jbcTextField('PIN',x):jbcField('PIN HEX',raw(),'warn');summary=x||'PIN';}
      else if(c===0x55&&p.length===1){h=bool('Beep',p[0]);summary=jbcBool(p[0]);}
      else if(c===0x57&&p.length===1){h=bool(u('Dauerabsaugung','Continuous suction'),p[0]);summary=jbcBool(p[0]);}
      else if(c===0x59&&p.length===2){stationError();}
      else if((c===0xC0||c===0xC2)&&p.length>=20&&p.length%20===0){let count=p.length/20;h=jbcField(u('Ports','Ports'),String(count),'primary');for(let port=0;port<count;port++){let plug=u32le(p,port*4+count*4*0),idle=u32le(p,port*4+count*4*1),work=u32le(p,port*4+count*4*2),stand=u32le(p,port*4+count*4*3),cy=u32le(p,port*4+count*4*4);h+=jbcField('P'+(port+1)+' Plug',plug+' min')+jbcField('P'+(port+1)+' Idle',idle+' min')+jbcField('P'+(port+1)+' Work intake',work+' min')+jbcField('P'+(port+1)+' Stand intake',stand+' min')+jbcField('P'+(port+1)+' Work cycles',cy+' cycles');}summary=count+' port counters';}
      else if(c===0xE0&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
      else if(c===0xF0&&p.length===7){let x=jbcUsbRobotConfigDiag(p);h=jbcTextField('Robot Config',x);summary=x;}
      else if(c===0xF2&&p.length===1){robotStatus();}
    }else if(p.length){if(c===0x38||c===0x3A||c===0x3C){h=jbcField('Port',jbcUsbPortText(p[0]),'primary')+(p.length>1?jbcField('Intake',jbcIntake(p[1])):'');}else h=jbcUsbReqSelector(p,f);}
  }

  if(!h&&fam==='SF'){
    if(response){
      if(c===0x32&&p.length===21){let name=cleanAscii(p.slice(1,9));h=jbcField(u('Programm','Program'),String(p[0]),'primary')+jbcTextField(u('Name','Name'),name===null?'-':name.trim());for(let i=0;i<3;i++)h+=jbcField(u('Schritt','Step')+' '+(i+1),(u16le(p,9+i*4)/10).toFixed(1)+' mm · raw '+u16le(p,9+i*4)+' @ '+(u16le(p,11+i*4)/10).toFixed(1)+' mm/s · raw '+u16le(p,11+i*4));summary='Program '+p[0]+' '+(name||'');}
      else if(c===0x34&&p.length){h=jbcField(u('Programmliste','Program list'),p.join(', '),'primary')+jbcField(u('Einträge','Entries'),String(p.length));summary=p.length+' entries';}
      else if((c===0x36||c===0x38)&&p.length===2){let rv=u16le(p),v=rv/10,unit=c===0x36?' mm/s':' mm';h=jbcField(jbcUsbCommandName(f),v.toFixed(1)+unit+' · raw '+rv,'primary');summary=v.toFixed(1)+unit;}
      else if(c===0x3C&&p.length===5){h=jbcField('Feeding state',String(p[0])+' · raw 0x'+hex2(p[0]),'primary')+jbcField('Value',String(u16le(p,1))+' raw')+jbcField(u('Programm','Program'),String(p[3]))+jbcField('CurrentProgramStep',String(p[0]))+jbcField(u('Reserve','Reserved'),'0x'+hex2(p[4]));summary='State '+p[0]+' · Program '+p[3];}
      else if(c===0x51&&p.length===4){let x=cleanAscii(p);h=jbcTextField('PIN',x===null?raw():x);summary=x||'PIN';}
      else if((c===0x55||c===0x5D||c===0x5F)&&p.length===1){h=bool(jbcUsbCommandName(f),p[0]);summary=jbcBool(p[0]);}
      else if(c===0x57&&p.length===1){h=jbcField(u('Längeneinheit','Length unit'),String(p[0])+' · raw 0x'+hex2(p[0]),'primary');summary=String(p[0]);}
      else if(c===0x59&&p.length){stationError();}
      else if((c===0xC0||c===0xC2)&&p.length===20){let plugRaw=i32le(p,8),workRaw=i32le(p,12),plug=plugRaw*60,work=workRaw*60,idle=plug-work;h=jbcField(u('Zinnlänge','Tin length'),u64leText(p)+' raw','primary')+jbcField('Plug',plug+' min · raw '+plugRaw+' ×60')+jbcField('Work',work+' min · raw '+workRaw+' ×60')+jbcField('Idle',idle+' min · derived')+jbcField('Cycles',u32le(p,16)+' cycles');summary='Tin '+u64leText(p)+' · '+work+' min work';}
      else if(c===0xE0&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
      else if(c===0xF0&&p.length===7){let x=jbcUsbRobotConfigDiag(p);h=jbcTextField('Robot Config',x);summary=x;}
      else if(c===0xF2&&p.length===1){robotStatus();}
    }else if(p.length){if(c===0x32)h=jbcField(u('Programm','Program'),String(p[0]),'primary');else h=jbcUsbReqSelector(p,f);}
  }

  if(!h&&c===0xE0&&response&&p.length){let x=jbcUsbModeText(p);h=jbcField(u('Verbindungsmodus','Connection mode'),x,'primary');summary=x;}
  if(!h&&c===0xF0&&response&&p.length===7){let x=jbcUsbRobotConfigDiag(p);h=jbcTextField('Robot Config',x);summary=x;}
  if(!h&&p.length){let x=cleanAscii(p);h=jbcField('HEX',raw());if(x!==null&&x.length)h+=jbcTextField('ASCII',x);summary=x||raw();}
  if(!h&&!p.length){h='<span class="diag-result-sub">'+u('keine Nutzdaten','no payload')+'</span>';summary='';}
  return {html:'<div class="jbc-decode">'+h+'</div>',format:format,summary:summary};
}
function jbcPayloadInfo(f,role){
  if(f&&jbcUsbTraceEvent(f.e))return jbcUsbPayloadInfo(f,role);
  let p=(f&&f.payload)||[],c=Number(f&&f.ctrl||0),response=role==='response';
  let h='',format='raw',summary='';
  const boolField=(label,v)=>jbcField(label,jbcBool(v),Number(v)?'bool-on':'bool-off');

  if(c===0x00){
    format='handshake';
    if(p.length===1&&p[0]===0x06){h=jbcField('HS','ACK 0x06','primary');summary='ACK';}
  }else if(c===0x06){
    format='ack';
    h=jbcField('ACK',p.length&&p[0]===0x06?'0x06':(p.map(hex2).join(' ')||'—'),'primary');
    summary='ACK';
  }else if(c===0x15){
    format='nack';h=jbcField('NACK',p.map(hex2).join(' ')||'—','warn');summary='NACK';
  }else if(c===0x16){
    format='sync';
    h=jbcField('SYNC',p.length&&p[0]===0x06?'ACK 0x06':(p.map(hex2).join(' ')||'—'),'primary');
    summary='SYNC';
  }else if(c===0x21){
    format='ASCII firmware';
    let s=cleanAscii(p);
    h=s!==null?jbcTextField('Firmware',s):jbcField('Firmware HEX',p.map(hex2).join(' '),'warn');
    summary=s||'Firmware';
  }else if(c===0x1C||c===0x1D||c===0x1E||c===0x1F){
    format='ASCII device-id';
    if(p.length){
      let s=cleanAscii(p);
      h=s!==null?jbcTextField('Device ID',s):jbcField('Device ID HEX',p.map(hex2).join(' '),'warn');
      summary=s||'Device ID';
    }
  }else if(c===0x30||c===0x31){
    format='uint8 level';
    if(p.length){h=jbcField(u('Absaugstufe','Suction level'),jbcSuctionLevel(p[0]),'primary');summary=jbcSuctionLevel(p[0]);}
  }else if(c===0x32){
    format='uint16 LE';
    if(response&&p.length>=2){let v=u16le(p);h=jbcField(u('Luftstrom','Flow'),String(v),'primary');summary=String(v);}
  }else if(c===0x33){
    format='uint16 LE rpm';
    if(response&&p.length>=2){let v=u16le(p);h=jbcField('RPM',v+' rpm','primary');summary=v+' rpm';}
  }else if(c===0x34||c===0x35){
    format='uint16 LE flow';
    if(p.length>=2){let v=u16le(p);h=jbcField(u('Flow-Auswahl','Flow select'),jbcSelectFlowText(v),'primary');summary=jbcSelectFlowText(v);}
  }else if(c===0x36||c===0x37){
    format='bool';
    if(p.length){h=boolField('Stand Intakes',p[0]);summary=jbcBool(p[0]);}
  }else if(c===0x38){
    format=response?'bool + port + intake':'port + intake';
    if(response&&p.length>=3){
      h=boolField(u('Aktiv','Active'),p[0])+jbcField('Port',String(p[1]))+jbcField('Intake',jbcIntake(p[2]));
      summary=jbcBool(p[0])+' · '+jbcIntake(p[2]);
    }else if(!response&&p.length>=2){
      h=jbcField('Port',String(p[0]))+jbcField('Intake',jbcIntake(p[1]));
      summary='Port '+p[0]+' · '+jbcIntake(p[1]);
    }
  }else if(c===0x39){
    format='bool + port + intake';
    if(p.length>=2){
      h=boolField(u('Aktiv','Active'),p[0])+jbcField('Port',String(p.length>=2?p[1]:0));
      if(p.length>=3)h+=jbcField('Intake',jbcIntake(p[2]));
      summary=jbcBool(p[0]);
    }
  }else if(c===0x3A||c===0x3C){
    format=response?'uint16 LE sec + port + intake':'port + intake';
    if(response&&p.length>=4){
      let sec=u16le(p);h=jbcField(u('Zeit','Time'),sec+' s','primary')+jbcField('Port',String(p[2]))+jbcField('Intake',jbcIntake(p[3]));
      summary=sec+' s · '+jbcIntake(p[3]);
    }else if(!response&&p.length>=2){
      h=jbcField('Port',String(p[0]))+jbcField('Intake',jbcIntake(p[1]));
      summary='Port '+p[0]+' · '+jbcIntake(p[1]);
    }
  }else if(c===0x3B){
    format='uint16 LE sec + port + intake';
    if(p.length>=4){
      let sec=u16le(p);h=jbcField(u('Zeit','Time'),sec+' s','primary')+jbcField('Port',String(p[2]))+jbcField('Intake',jbcIntake(p[3]));
      summary=sec+' s · '+jbcIntake(p[3]);
    }
  }else if(c===0x3D||c===0x3F||c===0x44){
    format=response?'uint8 + port':'port selector';
    if(response&&p.length>=2){
      let label=c===0x3D?u('Pedal aktiv','Pedal active'):(c===0x3F?u('Pedalmodus','Pedal mode'):u('Pedal verbunden','Pedal connected'));
      let value=c===0x3F?jbcPedalMode(p[0]):jbcBool(p[0]);
      h=(c===0x3F?jbcField(label,value,'primary'):boolField(label,p[0]))+jbcField('Port',String(p[1]));
      summary=value;
    }else if(!response&&p.length>=1){h=jbcField('Port',String(p[0]));summary='Port '+p[0];}
  }else if(c===0x3E||c===0x40){
    format='uint8 + port';
    if(p.length>=2){
      let label=c===0x3E?u('Pedal aktiv','Pedal active'):u('Pedalmodus','Pedal mode');
      let value=c===0x40?jbcPedalMode(p[0]):jbcBool(p[0]);
      h=(c===0x40?jbcField(label,value,'primary'):boolField(label,p[0]))+jbcField('Port',String(p[1]));
      summary=value;
    }
  }else if(c===0x41||c===0x45){
    format='uint16 LE raw';
    if(response&&p.length>=2){
      let v=u16le(p),label=c===0x41?u('Filter Life','Filter life'):u('Filtersättigung','Filter saturation');
      h=jbcField(label,String(v)+' raw','primary');summary=String(v)+' raw';
    }
  }else if(c===0x42){
    format='uint8 reset';
    if(response&&p.length){h=jbcField(u('Reset','Reset'),String(p[0]),'primary');summary='Reset '+p[0];}
  }else if(c===0x51||c===0x52){
    format='ASCII 4';
    if(p.length){
      let s=cleanAscii(p);
      h=s!==null?jbcTextField('PIN',s):jbcField('PIN HEX',p.map(hex2).join(' '),'warn');summary=s||'PIN';
    }
  }else if(c===0x53||c===0x54||c===0x55||c===0x56||c===0x57||c===0x58||c===0x5D||c===0x5E||c===0x60||c===0xE0||c===0xE1){
    format='bool';
    if(p.length){
      let labels={
        0x53:u('Station gesperrt','Station locked'),0x54:u('Station gesperrt','Station locked'),
        0x55:'Beep',0x56:'Beep',0x57:u('Dauerabsaugung','Continuous suction'),0x58:u('Dauerabsaugung','Continuous suction'),
        0x5D:'PIN enabled',0x5E:'PIN enabled',0x60:'Work Intakes',0xE0:'USB connected',0xE1:'USB connected'
      };
      h=boolField(labels[c]||'Flag',p[0]);summary=jbcBool(p[0]);
    }
  }else if(c===0x59){
    format='uint16 LE bitmask';
    if(response&&p.length>=2){
      let v=u16le(p);h=jbcField(u('Statusfehler','Status error'),'0x'+v.toString(16).toUpperCase().padStart(4,'0')+' · '+v,'primary');summary='0x'+v.toString(16).toUpperCase().padStart(4,'0');
    }
  }else if(c===0x5B||c===0x5C){
    format='ASCII text';
    if(p.length){
      let s=cleanAscii(p);h=s!==null?jbcTextField(u('Gerätename','Device name'),s):jbcField('Name HEX',p.map(hex2).join(' '),'warn');summary=s||'Name';
    }
  }

  if(!h&&p.length){
    h=jbcField('HEX',p.map(hex2).join(' '));
    summary=p.map(hex2).join(' ');
  }
  if(!h&&!p.length){
    h='<span class="diag-result-sub">'+u('keine Nutzdaten','no payload')+'</span>';
    summary='';
  }
  return {html:'<div class="jbc-decode">'+h+'</div>',format:format,summary:summary};
}
function localEventMs(e){
  let v=(Number(e.ms)||0)-(Number(e.latency)||0);
  return v<0?0:v;
}
function localDeltaMs(a,b){
  return Math.max(0,Math.round(Math.abs(localEventMs(b)-localEventMs(a))));
}
function jbcCtrlLabel(c){
  const m={
    0x00:'M_HS',0x06:'M_ACK',0x15:'M_NACK',0x16:'CTRL_SYN_P02',
    0x1C:'M_R_DEVICEIDORIGINAL',0x1D:'M_R_DISCOVER',0x1E:'M_R_DEVICEID',0x1F:'M_W_DEVICEID',
    0x21:'M_FIRMWARE',0x30:'M_R_SUCTIONLEVEL',0x31:'M_W_SUCTIONLEVEL',0x32:'M_R_FLOW',
    0x33:'M_R_SPEED',0x34:'M_R_SELECTFLOW',0x35:'M_W_SELECTFLOW',0x36:'M_R_STANDINTAKES',
    0x37:'M_W_STANDINTAKES',0x38:'M_R_INTAKEACTIVATION',0x39:'M_W_INTAKEACTIVATION',
    0x3A:'M_R_SUCTIONDELAY',0x3B:'M_W_SUCTIONDELAY',0x3C:'M_R_DELAYTIME',
    0x3D:'M_R_ACTIVATIONPEDAL',0x3E:'M_W_ACTIVATIONPEDAL',0x3F:'M_R_PEDALMODE',
    0x40:'M_W_PEDALMODE',0x41:'M_R_FILTERSTATUS',0x42:'M_R_RESETFILTER',0x44:'M_R_CONNECTEDPEDAL',
    0x45:'M_R_FILTERSAT',0x51:'M_R_PIN',0x52:'M_W_PIN',0x53:'M_R_STATIONLOCKED',
    0x54:'M_W_STATIONLOCKED',0x55:'M_R_BEEP',0x56:'M_W_BEEP',0x57:'M_R_CONTINUOUSSUCTION',
    0x58:'M_W_CONTINUOUSSUCTION',0x59:'M_R_STATERROR',0x5B:'M_R_DEVICENAME',
    0x5C:'M_W_DEVICENAME',0x5D:'M_R_PINENABLED',0x5E:'M_W_PINENABLED',0x60:'M_W_WORKINTAKES',
    0xE0:'M_R_USB_CONNECTSTATUS',0xE1:'M_W_USB_CONNECTSTATUS'
  };
  return m[c]||('CTRL 0x'+hex2(c));
}
function jbcFriendly(c){
  const m={
    0x1C:u('Original Device ID','Original device ID'),0x1D:u('Gerätesuche','Device discovery'),0x1E:'Device ID',
    0x21:'Firmware',0x30:u('Absaugstufe','Suction level'),0x32:u('Luftstrom','Flow'),0x33:u('Drehzahl','Speed'),
    0x34:u('Flow-Auswahl','Flow select'),0x36:'Stand Intakes',0x38:u('Intake-Aktivierung','Intake activation'),
    0x3A:u('Absaugverzögerung','Suction delay'),0x41:u('Filterstatus','Filter status'),0x45:u('Filtersättigung','Filter saturation'),
    0x59:u('Statusfehler','Status error'),0x5B:u('Gerätename','Device name'),0x57:u('Dauerabsaugung','Continuous suction')
  };
  return m[c]||jbcCtrlLabel(c);
}
function wellerLabel(c){
  const m={
    65:u('Lichtstatus','Light state'),68:'RPM',70:u('Filterzeit Soll','Programmed filter time'),
    71:u('Filterlaufzeit','Filter runtime'),76:u('Filterstatus','Filter status'),77:u('Lüfter aus','Fan off'),
    78:u('Lüfter an','Fan on'),83:u('Drehzahl','Speed'),86:u('Software-Version','Software version'),
    97:u('Licht-Bestätigung','Light acknowledgement'),100:u('Drehzahl setzen','Set speed'),
    102:u('Filterzeit setzen','Set filter time'),103:u('Filter zurücksetzen','Reset filter')
  };
  return m[c]||('Command '+String.fromCharCode(c||0));
}
function checksum8(b,n){let s=0;for(let i=0;i<n&&i<b.length;i++)s=(s+b[i])&255;return s;}
function jbcRawDetails(e,f){
  let key='j-'+e.seq;
  let open=diagOpenPayloads.has(key)?' open':'';
  let declared=Number(f.declared||0),actual=(f.payload||[]).length,lenOk=declared===actual;
  let role=jbcTraceRole(e);
  let info=jbcPayloadInfo(f,role);
  let payload=(f.payload||[]).map(hex2).join(' ')||'—';
  let h='<details class="diag-raw-details" data-payload-key="'+key+'" ontoggle="diagPayloadToggle(this)"'+open+'>'+
    '<summary>'+esc(f.protocol)+' '+u('Felder','fields')+' · '+f.b.length+' B</summary>'+
    '<div class="jbc-byte-map">'+
      '<span class="jbc-byte-field"><span class="k">SRC</span><span class="v">'+hex2(f.src)+'</span></span>'+
      '<span class="jbc-byte-field"><span class="k">DST</span><span class="v">'+hex2(f.dst)+'</span></span>'+
      (f.protocol==='P02'?'<span class="jbc-byte-field"><span class="k">FID</span><span class="v">'+hex2(f.fid)+'</span></span>':'')+
      '<span class="jbc-byte-field"><span class="k">CTRL</span><span class="v">'+hex2(f.ctrl)+'</span></span>'+
      '<span class="jbc-byte-field"><span class="k">LEN</span><span class="v '+(lenOk?'jbc-len-ok':'jbc-len-bad')+'">'+declared+'</span></span>'+
      '<span class="jbc-byte-field payload"><span class="k">PAYLOAD</span><span class="v">'+esc(payload)+'</span></span>'+
    '</div>'+
    '<div class="jbc-raw-note"><span class="jbc-format-badge">'+esc(info.format)+'</span> '+
      u('Lokaler Trace enthält den bereits entpackten JBC-Innenframe; USB-Wire-Framing, Byte-Stuffing und BCC sind im normalen Frameevent nicht enthalten.',
        'Local trace contains the already decoded JBC inner frame; USB wire framing, byte stuffing and BCC are not included in a normal frame event.')+
    '</div>'+
    '<div class="diag-payload-line"><b>HEX:</b> <span class="data">'+esc((f.b||[]).map(hex2).join(' '))+'</span></div>'+
  '</details>';
  return h;
}
function localRawDetails(e,label){
  let key='l-'+e.seq;
  let open=diagOpenPayloads.has(key)?' open':'';
  let ascii=asciiBytes(hexBytes(e.data||''));
  return '<details class="diag-raw-details" data-payload-key="'+key+'" ontoggle="diagPayloadToggle(this)"'+open+'><summary>'+esc(label||((e.len||0)+' B'))+'</summary><div class="diag-payload-line"><b>HEX:</b> <span class="data">'+esc(e.data||'-')+'</span></div><div class="diag-payload-line"><b>ASCII:</b> <span class="diag-proto-ascii">'+esc(ascii||'-')+'</span></div></details>';
}
function txnRawDetails(txn){
  let events=txn.events||[];
  let key='t-'+events.map(e=>e.seq).join('-');
  let open=diagOpenPayloads.has(key)?' open':'';
  let h='<details class="diag-raw-details" data-payload-key="'+key+'" ontoggle="diagPayloadToggle(this)"'+open+'><summary>'+u('Details','Details')+'</summary>';
  events.forEach(function(e){
    if(txn.proto==='JBC'){
      let f=jbcFrame(e);
      if(f){
        let role=jbcTraceRole(e);
        let info=jbcPayloadInfo(f,role);
        h+='<div class="diag-payload-line"><b>'+esc(String(e.dir).replace('UART_',''))+' · '+esc(jbcCtrlLabelFrame(f))+'</b> <span class="jbc-format-badge">'+esc(info.format)+'</span></div>';
        h+=info.html;
        h+='<div class="diag-payload-line"><span class="data">'+esc(f.b.map(hex2).join(' '))+'</span></div>';
        return;
      }
    }
    if(txn.proto==='Universal'){
      let uf=universalFrame(e);
      h+='<div class="diag-payload-line"><b>'+esc(String(e.dir).replace('UART_',''))+' · '+esc(universalLabel(uf))+'</b> '+universalStatusBadge(uf)+'</div>';
      if(uf.decoded)h+='<div class="proto-result"><span class="proto-result-value primary">'+esc(uf.decoded.text)+'</span></div>';
      else {
        let ct=universalBytesText(uf.content);
        if(ct!==null&&ct.length)h+='<div class="diag-payload-line"><span class="proto-result-value text">'+esc(ct)+'</span></div>';
      }
      h+='<div class="diag-payload-line"><span class="data">'+esc(bytesCompactHex(uf.b,96))+'</span></div>';
      return;
    }
    if(txn.proto==='Modbus'){
      let f=modbusFrame(e),role=f.tx?'request':'response',di=modbusDecode(f,role);
      h+='<div class="diag-payload-line"><b>'+esc(String(e.dir).replace('UART_',''))+' · Slave '+f.slave+' · '+esc(modbusFuncLabel(f.func))+'</b> '+modbusCrcBadge(f)+'</div>';
      h+=di.html;
      h+='<div class="diag-payload-line"><span class="data">'+esc(bytesCompactHex(f.b,96))+'</span></div>';
      return;
    }
    h+='<div class="diag-payload-line"><b>'+esc(String(e.dir).replace('UART_',''))+':</b> <span class="data">'+esc(e.data||'-')+'</span>';
    let a=asciiBytes(hexBytes(e.data||''));if(a)h+=' <span class="diag-result-sub">· '+esc(a)+'</span>';
    h+='</div>';
  });
  h+='</details>';return h;
}
function jbcUsbTraceEvent(e){let m=diagModuleByAddr(e&&e.addr);return !!(m&&Number(m.type)===9)}
function jbcTraceProtocol(e){
  if(jbcUsbTraceEvent(e)){
    let v=Number(e&&e.status);return v===1?'P01':(v===2?'P02':'RAW');
  }
  return 'P02';
}
function jbcTraceRole(e){
  let tx=String(e&&e.dir)==='UART_TX';
  // JBC FAE is a station-side bridge (incoming RX=request, outgoing TX=response).
  // JBC USB is the PC/host side (outgoing TX=request, incoming RX=response).
  return jbcUsbTraceEvent(e)?(tx?'request':'response'):(tx?'response':'request');
}
function jbcFrame(e){
  let b=hexBytes(e.data||'');
  if(jbcUsbTraceEvent(e)&&![1,2].includes(Number(e.status)))return null;
  if(b.length<5)return null;
  let declared=Number(b[4]||0),payload=b.slice(5);
  return {
    e:e,b:b,src:b[0],dst:b[1],fid:b[2],ctrl:b[3],declared:declared,payload:payload,
    protocol:jbcTraceProtocol(e),truncated:payload.length<declared
  };
}
function jbcUsbModelForEvent(e){
  let m=diagModuleByAddr(e&&e.addr),j=m&&m.jbc_local?m.jbc_local:{};
  let shortModel=String(j.model_short||'').trim();
  if(shortModel&&shortModel!=='-')return shortModel.toUpperCase();
  let raw=String(j.model||'').trim();
  if(!raw||raw==='-')return '';
  // Raw USB model strings may carry firmware/hardware suffixes (for example
  // DDE_* / CLM_*). Family/command decoding must use the canonical station token.
  return raw.split('_')[0].replace(/[\/\s-]/g,'').toUpperCase();
}
function jbcUsbFamilyForEvent(e){
  let m=diagModuleByAddr(e&&e.addr),j=m&&m.jbc_local?m.jbc_local:{},family=String(j.family||'').trim().toUpperCase();
  if(['SOLD','HA','CL','PH','FE','SF'].includes(family))return family;
  let model=jbcUsbModelForEvent(e);
  if(['CA','CDCF','CDN','CP','CSCV','CDE','CFE','CAE','CPE','CSVE','DD','DDE','DDR','DI','DM','DME','HD','HDE','HDR','LC','NA','NAE','PSE','SM','WS','ALE'].includes(model))return 'SOLD';
  if(/^(JT|JTSE|TE|HA)/.test(model))return 'HA';
  if(/^(CL|CLM|CLMU)/.test(model))return 'CL';
  if(/^PH/.test(model))return 'PH';
  if(/^(F1|F2|F2W|F4W|FE)/.test(model))return 'FE';
  if(/^SF/.test(model))return 'SF';
  return '';
}
function jbcUsbCommandName(f){
  if(!f||!jbcUsbTraceEvent(f.e))return null;
  let c=Number(f.ctrl),fam=jbcUsbFamilyForEvent(f.e);
  const common={0x00:'M_HS',0x15:'M_NACK',0x21:'Firmware',0x30:'InfoPort',0x80:'ReadContiMode',0x81:'WriteContiMode',0x82:'ContiInfo',0xE0:'ReadConnectStatus',0xE1:'WriteConnectStatus'};
  const soldP02={0x33:'Levels',0x35:'ProfileMode',0x40:'SleepDelay',0x42:'SleepTemp',0x44:'HiberDelay',0x46:'AdjustTemp',0x48:'Cartridge',0x50:'SelectTemp',0x52:'TipTemp',0x53:'Current',0x54:'Power',0x55:'ToolType',0x56:'ToolLastError',0x57:'ToolStatus',0x59:'MosTemp',0x5A:'DelayTime',0x60:'RemoteMode',0x70:'ALEFeederInfo',0x72:'ALEFeederProgram',0x83:'AlarmMax',0x85:'AlarmMin',0x87:'AlarmTriggerNClear',0x88:'LockPort',0x8A:'AssistantConfig',0x8C:'SolderingResult',0x8D:'AssistantWarning',0x9A:'SelectedProfile',0x9C:'QSTActivate',0x9E:'QSTStatus',0xA0:'InterfaceConfig',0xA2:'MaxTemp',0xA4:'MinTemp',0xA6:'AutoClean',0xA8:'PINEnabled',0xAA:'PowerLimit',0xAC:'PIN',0xAE:'StationError',0xAF:'TrafoTemp',0xB1:'DeviceName',0xB7:'TrafoErrorTemp',0xB8:'MosErrorTemp',0xBA:'GroundType',0xBB:'DateTime',0xBE:'StationInterface',0xC0:'CounterPlug',0xC2:'CounterWork',0xC4:'CounterSleep',0xC6:'CounterHiber',0xC8:'CounterIdle',0xCA:'CounterSleepCycles',0xCC:'CounterDesoldCycles',0xD0:'PartialPlug',0xD2:'PartialWork',0xD4:'PartialSleep',0xD6:'PartialHiber',0xD8:'PartialIdle',0xDA:'PartialSleepCycles',0xDC:'PartialDesoldCycles',0xE3:'FrontalConnection',0xE7:'Ethernet',0xF0:'RobotConfig',0xF2:'RobotStatus',0xF9:'PeripheralCount',0xFA:'PeripheralConfig',0xFC:'PeripheralStatus'};
  const soldP01={0x31:'FixedTemp',0x33:'Levels',0x35:'Level1',0x37:'Level2',0x39:'Level3',0x40:'SleepDelay',0x42:'SleepTemp',0x44:'HiberDelay',0x46:'AdjustTemp',0x50:'SelectTemp',0x52:'TipTemp',0x54:'Power',0x55:'ToolType',0x56:'ToolLastError',0x57:'ToolStatus',0x58:'MosTemp',0x59:'DelayTime',0x60:'RemoteMode',0x62:'RemoteStatus',0xA0:'TempUnit',0xA2:'MaxTemp',0xA4:'MinTemp',0xA6:'N2Mode',0xA8:'HelpText',0xAA:'PowerLimit',0xAC:'PIN',0xAE:'StationError',0xAF:'TrafoTemp',0xB1:'DeviceName',0xB3:'Beep',0xB7:'TrafoErrorTemp',0xB8:'MosErrorTemp',0xBD:'PINEnabled',0xC0:'CounterPlug',0xC2:'CounterWork',0xC4:'CounterSleep',0xC6:'CounterHiber',0xC8:'CounterIdle',0xCA:'CounterSleepCycles',0xCC:'CounterDesoldCycles',0xD0:'QSTActivate',0xD2:'QSTStatus',0xD4:'LockPort',0xF0:'PartialPlug',0xF2:'PartialWork',0xF4:'PartialSleep',0xF6:'PartialHiber',0xF8:'PartialIdle',0xFA:'PartialSleepCycles',0xFC:'PartialDesoldCycles'};
  const ha={0x33:'ProfileMode',0x35:'HeaterStatus',0x37:'SuctionStatus',0x39:'ExternalTCMode',0x40:'Levels',0x42:'AdjustTemp',0x44:'TimeToStop',0x46:'StartMode',0x50:'SelectTemp',0x52:'AirTemp',0x54:'Power',0x55:'ConnectTool',0x56:'ToolError',0x57:'ToolStatus',0x59:'SelectFlow',0x5B:'SelectExtTemp',0x5D:'AirFlow',0x5F:'ActualExtTemp',0x60:'RemoteMode',0x9A:'SelectedProfile',0xA0:'TempUnit',0xA2:'MaxMinTemp',0xA4:'MaxMinFlow',0xA6:'MaxMinExtTemp',0xA8:'PINEnabled',0xAC:'PIN',0xAE:'StationError',0xB1:'DeviceName',0xB3:'Beep',0xC0:'CounterPlug',0xC2:'CounterWork',0xC4:'WorkCycles',0xC6:'SuctionCycles',0xD0:'PartialPlug',0xD2:'PartialWork',0xD4:'PartialWorkCycles',0xD6:'PartialSuctionCycles',0xF0:'RobotConfig',0xF2:'RobotStatus'};
  const cl={0x30:'CleanerMode',0x32:'MotorsState',0x38:'DoorsState',0x54:'DeviceName',0xC0:'Counters',0xC2:'PartialCounters',0xE0:'ConnectStatus'};
  const ph={0x33:'WorkMode',0x35:'HeaterStatus',0x39:'ExternalTCMode',0x44:'TimeToStop',0x50:'SelectTemp',0x52:'SelectPower',0x58:'TCWarning',0x5B:'ActiveZones',0x5F:'ExternalAirTemp',0x90:'Profile',0x92:'ProfileSettings',0x94:'ProfileTeach',0xA2:'MaxMinPower',0xA6:'MaxMinTemp',0xA8:'PINEnabled',0xAC:'PIN',0xAE:'StationError',0xB1:'DeviceName',0xB3:'Beep',0xC0:'CounterPlug',0xC2:'CounterWork',0xC7:'WorkCycles',0xD0:'PartialPlug',0xD2:'PartialWork',0xD7:'PartialWorkCycles',0xE0:'ConnectStatus',0xF0:'RobotConfig',0xF2:'RobotStatus'};
  const fe={0x30:'SuctionLevel',0x32:'Flow',0x33:'Speed',0x34:'SelectFlow',0x36:'StandIntakes',0x38:'IntakeActivation',0x3A:'SuctionDelay',0x3C:'TimeToStopSuction',0x3D:'ActivationPedal',0x3F:'PedalMode',0x41:'FilterStatus',0x44:'ConnectedPedal',0x51:'PIN',0x55:'Beep',0x57:'ContinuousSuction',0x59:'StationError',0x5B:'DeviceName',0xC0:'Counters',0xC2:'PartialCounters',0xE0:'ConnectStatus',0xF0:'RobotConfig',0xF2:'RobotStatus'};
  const sf={0x30:'DispenserMode',0x32:'Program',0x34:'ProgramList',0x36:'Speed',0x38:'Length',0x3C:'Feeding',0x51:'PIN',0x55:'Beep',0x57:'LengthUnit',0x59:'StationError',0x5B:'DeviceName',0x5D:'ToolEnabled',0x5F:'PINEnabled',0xC0:'Counters',0xC2:'PartialCounters',0xE0:'ConnectStatus',0xF0:'RobotConfig',0xF2:'RobotStatus'};
  let sold=f&&f.protocol==='P01'?soldP01:soldP02;
  let map=fam==='SOLD'?sold:(fam==='HA'?ha:(fam==='CL'?cl:(fam==='PH'?ph:(fam==='FE'?fe:(fam==='SF'?sf:null)))));
  return (map&&map[c])||common[c]||null;
}
function jbcCtrlLabelFrame(f){return jbcUsbCommandName(f)||jbcCtrlLabel(f?f.ctrl:0)}
function jbcFriendlyFrame(f){return jbcUsbCommandName(f)||jbcFriendly(f?f.ctrl:0)}
function wellerFrame(e){
  let b=hexBytes(e.data||'');
  let c=b.length?b[0]:Number(e.cmd||0);
  let value=null,digits='';
  if(b.length>=4&&b[1]>=48&&b[1]<=57&&b[2]>=48&&b[2]<=57&&b[3]>=48&&b[3]<=57){
    digits=String.fromCharCode(b[1],b[2],b[3]);
    value=(b[1]-48)*100+(b[2]-48)*10+(b[3]-48);
  }
  let check=null,calc=null,frameCs=null;
  if(b.length===5){frameCs=b[4];calc=checksum8(b,4);check=frameCs===calc;}
  else if(b.length===7&&b[0]===97){frameCs=b[6];calc=checksum8(b,6);check=frameCs===calc;}
  if(Number(e.status)===0xEE)check=false;
  return {e:e,b:b,cmd:c,value:value,digits:digits,check:check,calc:calc,frameCs:frameCs};
}

function bytesTextPreview(b){
  if(!b||!b.length)return null;
  let s='',printable=0,control=0;
  for(let x of b){
    if(x===13){s+='\\r';control++;continue;}
    if(x===10){s+='\\n';control++;continue;}
    if(x===9){s+='\\t';control++;continue;}
    if(x>=32&&x<=126){s+=String.fromCharCode(x);printable++;continue;}
    return null;
  }
  return printable?s:null;
}
function bytesCompactHex(b,max=32){
  let a=(b||[]).slice(0,max).map(hex2).join(' ');
  if((b||[]).length>max)a+=' …';
  return a||'—';
}
function diagModuleByAddr(addr){
  let mods=(lastDiag&&lastDiag.modules)||[];
  return mods.find(m=>Number(m.addr)===Number(addr))||null;
}
function universalModuleFor(e){return diagModuleByAddr(e&&e.addr)}
function universalDefsFor(e){
  let m=universalModuleFor(e);
  return Array.isArray(m&&m.local_trace_defs)?m.local_trace_defs:[];
}
function universalHexText(s){
  let b=hexBytes(s||'');
  let out='';
  for(let x of b)out+=String.fromCharCode(x);
  return out;
}
function universalIsBinary(e){let m=universalModuleFor(e);return String(m&&m.local_protocol||'').trim().toUpperCase()==='BINARY'}
function universalBinaryTokenize(text,template=false){
  text=String(text||'').trim();let out=[],i=0;
  while(i<text.length){
    while(i<text.length&&/[\s,:_-]/.test(text[i]))i++;
    if(i>=text.length)break;
    if(template&&text[i]==='{'){
      let end=text.indexOf('}',i);if(end<0)return null;
      let token=text.slice(i+1,end),m=token.match(/^value:(u8|i8|u16le|u16be|i16le|i16be|u32le|u32be|i32le|i32be)$/i);if(!m)return null;
      let sz=/32/i.test(m[1])?4:(/16/i.test(m[1])?2:1);for(let k=0;k<sz;k++)out.push({wild:true,cap:m[1].toLowerCase()});i=end+1;continue;
    }
    if(!template&&text.slice(i,i+2)==='??'){out.push({wild:true});i+=2;continue}
    if(text.slice(i,i+2).toLowerCase()==='0x')i+=2;
    let pair=text.slice(i,i+2);if(!/^[0-9a-f]{2}$/i.test(pair))return null;out.push({wild:false,v:parseInt(pair,16)});i+=2;
  }
  return out;
}
function universalBinaryReadValue(def,content){
  let off=diagParseInt(def&&def.value_offset,0),type=String(def&&def.value_type||'u8').toLowerCase(),len=diagParseInt(def&&def.value_len,0);
  let sz=(type.includes('32')?4:(type.includes('16')?2:(type==='u8'||type==='i8'?1:len)));if(!sz||off<0||off+sz>content.length)return {raw:'',num:null,text:''};
  let a=content.slice(off,off+sz);
  if(type==='ascii'){let t=a.map(x=>x>=32&&x<=126?String.fromCharCode(x):'.').join('');return {raw:t,num:/^-?\d+$/.test(t)?Number(t):null,text:t}}
  if(type==='hex'){let t=a.map(hex2).join('');let num=a.slice(0,4).reduce((v,x)=>((v*256)+x)>>>0,0);return {raw:t,num:num,text:t}}
  let le=type.endsWith('le'),u=0;if(le){for(let i=a.length-1;i>=0;i--)u=(u*256+a[i])>>>0}else{for(let x of a)u=(u*256+x)>>>0}
  let num=u;if(type==='i8'&&u&0x80)num=u-0x100;else if(type.startsWith('i16')&&u&0x8000)num=u-0x10000;else if(type.startsWith('i32')&&u>0x7fffffff)num=u-0x100000000;
  return {raw:String(num),num:num,text:String(num)};
}
function universalBinaryPatternMatch(def,pattern,content){
  let tok=universalBinaryTokenize(pattern,false);if(!tok)return null;let off=diagParseInt(def&&def.match_offset,0);if(off<0||off+tok.length>content.length)return null;
  for(let i=0;i<tok.length;i++)if(!tok[i].wild&&content[off+i]!==tok[i].v)return null;
  let v=universalBinaryReadValue(def,content);return {raw:v.raw,num:v.num,text:v.text};
}
function universalBinaryTemplateMatch(template,content){
  let tok=universalBinaryTokenize(template,true);if(!tok||tok.length!==content.length)return null;let cap=[],capType='';
  for(let i=0;i<tok.length;i++){if(tok[i].wild){cap.push(content[i]);if(tok[i].cap)capType=tok[i].cap}else if(content[i]!==tok[i].v)return null}
  let num=null;if(cap.length){let le=capType.endsWith('le'),u=0;if(le){for(let i=cap.length-1;i>=0;i--)u=(u*256+cap[i])>>>0}else{for(let x of cap)u=(u*256+x)>>>0}num=u;if(capType==='i8'&&u&0x80)num=u-0x100;else if(capType.startsWith('i16')&&u&0x8000)num=u-0x10000;else if(capType.startsWith('i32')&&u>0x7fffffff)num=u-0x100000000}
  return {raw:cap.length?cap.map(hex2).join(' '):bytesCompactHex(content),num:num,text:bytesCompactHex(content)};
}
function universalBytesText(b){
  let s='';
  for(let x of (b||[])){
    if(x<32||x>126)return null;
    s+=String.fromCharCode(x);
  }
  return s;
}
function universalContentBytes(e,b){
  let a=(b||[]).slice(),binary=universalIsBinary(e);
  if(!binary)while(a.length&&(a[a.length-1]===13||a[a.length-1]===10))a.pop();
  let m=universalModuleFor(e);
  let cs=String(m&&m.local_checksum||'NONE').trim().toUpperCase();
  if((cs==='WELLER'||cs==='WELLER_SUM8'||cs==='WELLER_ASCII'||cs==='XOR8_RAW'||cs==='SUM8_RAW')&&a.length>1){
    a=a.slice(0,-1);
  }else if((cs==='XOR8_HEX'||cs==='SUM8_HEX'||cs==='CRC16_MODBUS_LE'||cs==='MODBUS_CRC16'||cs==='CRC16_CCITT_BE'||cs==='CRC16_CCITT_LE')&&a.length>2){
    a=a.slice(0,-2);
  }
  return a;
}
function universalPatternMatch(pattern,content){
  pattern=String(pattern||'');
  let text=universalBytesText(content);
  if(text===null)return null;
  let tm=pattern.match(/\{value(?::0[1-9])?\}/),marker=tm?tm[0]:'',p=tm?tm.index:-1;
  if(p>=0){
    let prefix=pattern.slice(0,p),suffix=pattern.slice(p+marker.length);
    if(text.length<prefix.length+suffix.length)return null;
    if(prefix&&text.slice(0,prefix.length)!==prefix)return null;
    if(suffix&&text.slice(text.length-suffix.length)!==suffix)return null;
    let raw=text.slice(prefix.length,text.length-suffix.length);
    return {raw:raw,num:/^-?\d+$/.test(raw)?Number(raw):null,text:text};
  }
  if(pattern.length!==text.length)return null;
  let digits='',saw=false;
  for(let i=0;i<pattern.length;i++){
    if(pattern[i]==='#'){
      if(text[i]<'0'||text[i]>'9')return null;
      digits+=text[i];saw=true;
    }else if(pattern[i]!==text[i])return null;
  }
  return {raw:saw?digits:text,num:saw?Number(digits):0,text:text};
}
function universalTemplateMatch(template,content){
  template=String(template||'');
  let text=universalBytesText(content);
  if(text===null||!template)return null;
  let token=/\{value(?::\d+)?\}/;
  let m=template.match(token);
  if(!m)return template===text?{raw:text,num:null,text:text}:null;
  let p=m.index,prefix=template.slice(0,p),suffix=template.slice(p+m[0].length);
  if(text.length<prefix.length+suffix.length)return null;
  if(prefix&&text.slice(0,prefix.length)!==prefix)return null;
  if(suffix&&text.slice(text.length-suffix.length)!==suffix)return null;
  let raw=text.slice(prefix.length,text.length-suffix.length);
  return {raw:raw,num:/^-?\d+$/.test(raw)?Number(raw):null,text:text};
}
function diagParseInt(v,fallback=0){
  let s=String(v??'').trim();if(!s)return fallback;
  let sign=1;if(s[0]==='-'){sign=-1;s=s.slice(1)}else if(s[0]==='+')s=s.slice(1);
  let n;if(/^0x[0-9a-f]+$/i.test(s))n=parseInt(s.slice(2),16);else if(/^0b[01]+$/i.test(s))n=parseInt(s.slice(2),2);else if(/^\d+$/.test(s))n=Number(s);else return fallback;
  return Number.isFinite(n)?sign*n:fallback;
}
function diagTransformBase(def,raw){
  let v=Number(raw||0),mask=String(def&&def.mask||'').trim(),shift=Math.max(0,Math.min(31,diagParseInt(def&&def.shift,0)));
  if(mask||shift){let bits=(v>>>0);if(mask)bits=(bits&(diagParseInt(mask,0)>>>0))>>>0;if(shift)bits=bits>>>shift;v=bits;}
  let scale=diagParseInt(def&&def.scale,1);if(!scale)scale=1;
  let div=diagParseInt(def&&def.div,1);if(div<1)div=1;
  let off=diagParseInt(def&&def.off,0);
  return Math.trunc((v*scale)/div)+off;
}
function diagBaseMinutes(def,v){
  switch(String(def&&def.tb||'none').toLowerCase()){
    case 's':return Math.trunc(v/60);case 'm':return v;case 'h':return v*60;case 'd':return v*1440;default:return v;
  }
}
function diagTransformNumeric(def,raw){
  let v=diagTransformBase(def,raw),tb=String(def&&def.tb||'none').toLowerCase(),tf=String(def&&def.tf||'raw').toLowerCase();
  if(tb!=='none'){
    let min=diagBaseMinutes(def,v);
    if(tf==='m')v=min;else if(tf==='h')v=Math.trunc(min/60);else if(tf==='d')v=Math.trunc(min/1440);
  }
  return v;
}
function diagFormatDhm(def,raw){
  let min=diagBaseMinutes(def,diagTransformBase(def,raw)),neg=min<0;if(neg)min=-min;
  let d=Math.trunc(min/1440),h=Math.trunc((min%1440)/60),m=Math.trunc(min%60);
  return (neg?'-':'')+d+'d '+h+'h '+m+'m';
}
function diagMapEntries(text){
  return String(text||'').split('|').map(x=>x.trim()).filter(Boolean).map(x=>{let m=x.match(/^([^=:]+)\s*[=:]\s*(.+)$/);return m?{key:diagParseInt(m[1],NaN),label:String(m[2]||'').trim()}:null}).filter(x=>x&&Number.isFinite(x.key)&&x.label);
}
function diagMappedText(def,value){
  let mode=String(def&&def.map_mode||'').toLowerCase(),entries=diagMapEntries(def&&def.map);
  if(mode==='exact'){
    let hit=entries.find(e=>e.key===value);if(hit)return hit.label;
  }else if(mode==='flags'){
    let bits=value>>>0,labels=[];entries.forEach(e=>{let key=e.key>>>0;if((key===0&&bits===0)||(key!==0&&((bits&key)>>>0)===key))labels.push(e.label)});if(labels.length)return labels.join(' | ');
  }
  return String(def&&def.map_default||'');
}
function diagSelectText(def,value){
  let opts=String(def&&def.options||'').split('|'),vals=String(def&&def.values||'').split('|');
  for(let i=0;i<vals.length;i++)if(diagParseInt(vals[i],NaN)===value&&opts[i]!==undefined)return opts[i];
  return '';
}
function diagDecodeDefValue(def,raw,rawText=''){
  if(!def)return null;
  let type=String(def.type||'').toLowerCase(),unit=String(def.unit||''),mask=String(def.mask||'').trim();
  if(type==='switch'||type==='binary_sensor'){
    let on;if(mask)on=(((Number(raw)>>>0)&(diagParseInt(mask,0)>>>0))>>>0)!==0;else{let von=diagParseInt(def.value_on,1),voff=diagParseInt(def.value_off,0);on=Number(raw)===von?true:(Number(raw)===voff?false:Number(raw)!==0)}
    return {text:on?u('AN','ON'):u('AUS','OFF'),raw:String(raw),num:Number(raw),value:on?1:0,label:String(def.label||def.key||'Entity')};
  }
  let transformed=diagTransformNumeric(def,Number(raw)),mapValue=transformed;
  if(String(def.map_mode||'').toLowerCase()==='flags'&&(mask||diagParseInt(def.shift,0))){let bits=Number(raw)>>>0;if(mask)bits=(bits&(diagParseInt(mask,0)>>>0))>>>0;let sh=Math.max(0,Math.min(31,diagParseInt(def.shift,0)));if(sh)bits=bits>>>sh;mapValue=bits;}
  let text='';
  if(String(def.tf||'').toLowerCase()==='dhm'&&String(def.tb||'none').toLowerCase()!=='none')text=diagFormatDhm(def,Number(raw));
  if(!text)text=diagMappedText(def,mapValue);
  if(!text&&type==='select')text=diagSelectText(def,transformed);
  if(!text&&type==='text'&&rawText&&diagParseInt(def.scale,1)===1&&diagParseInt(def.div,1)===1&&diagParseInt(def.off,0)===0&&!mask&&!diagParseInt(def.shift,0)&&String(def.tb||'none')==='none')text=rawText;
  if(!text)text=String(transformed)+(unit?' '+unit:'');
  return {text:text,raw:String(raw),num:Number(raw),value:transformed,label:String(def.label||def.key||'Entity')};
}
function universalEntityDecoded(def,match,action){
  if(!def)return null;
  let raw=match?String(match.raw==null?'':match.raw):'',num=match&&match.num!==null&&match.num!==undefined?Number(match.num):null;
  if(action==='on')return {text:u('AN','ON'),raw:raw,num:num,label:String(def.label||def.key||'Entity')};
  if(action==='off')return {text:u('AUS','OFF'),raw:raw,num:num,label:String(def.label||def.key||'Entity')};
  if(action==='set'&&num===null)return {text:raw||String(def.label||def.key||'-'),raw:raw,num:num,label:String(def.label||def.key||'Entity')};
  if(num===null)return {text:raw||String(def.label||def.key||'-'),raw:raw,num:num,label:String(def.label||def.key||'Entity')};
  return diagDecodeDefValue(def,num,raw);
}
function universalResolve(e,b,tx){
  let defs=universalDefsFor(e),content=universalContentBytes(e,b),hits=[];
  for(let def of defs){
    if(tx){
      let poll=universalHexText(def.poll_hex),set=universalHexText(def.set_hex);
      let on=universalHexText(def.on_hex),off=universalHexText(def.off_hex);
      let m=null,action='';
      let bm=universalIsBinary(e);
      if(poll&&(m=(bm?universalBinaryTemplateMatch(poll,content):universalTemplateMatch(poll,content)))){action='poll';}
      else if(on&&(m=(bm?universalBinaryTemplateMatch(on,content):universalTemplateMatch(on,content)))){action='on';}
      else if(off&&(m=(bm?universalBinaryTemplateMatch(off,content):universalTemplateMatch(off,content)))){action='off';}
      else if(set&&(m=(bm?universalBinaryTemplateMatch(set,content):universalTemplateMatch(set,content)))){action='set';}
      if(m)hits.push({def:def,match:m,action:action,decoded:universalEntityDecoded(def,m,action)});
    }else{
      let pattern=universalHexText(def.match_hex);if(!pattern)continue;
      let m=universalIsBinary(e)?universalBinaryPatternMatch(def,pattern,content):universalPatternMatch(pattern,content);if(m)hits.push({def:def,match:m,action:'response',decoded:universalEntityDecoded(def,m,'response')});
    }
  }
  let first=hits[0]||null;
  return {def:first&&first.def||null,defs:hits.map(h=>h.def),match:first&&first.match||null,action:first&&first.action||(tx?'tx':'rx'),decoded:first&&first.decoded||null,decoded_all:hits,content:content};
}
function universalFrame(e){
  let b=hexBytes(e.data||''),dir=String(e.dir||''),status=Number(e.status);
  let tx=dir==='UART_TX';
  let kind=tx?(Number(e.cmd)===0x4C?'line':'raw'):'rx';
  let resolved=universalResolve(e,b,tx);
  let text=bytesTextPreview(b);
  return {
    e:e,b:b,tx:tx,kind:kind,text:text,
    status:status,
    ok:status===0||status===255,
    badChecksum:status===0xEE,
    badPattern:status===0xEF,
    def:resolved.def,defs:resolved.defs,match:resolved.match,action:resolved.action,
    decoded:resolved.decoded,decoded_all:resolved.decoded_all,content:resolved.content
  };
}
function universalStatusBadge(f){
  if(!f)return '';
  if(f.badChecksum)return '<span class="diag-check-badge err">CS BAD</span>';
  if(f.badPattern)return '<span class="diag-check-badge err">'+u('Pattern BAD','Pattern BAD')+'</span>';
  if(!f.tx)return '<span class="diag-check-badge ok">OK</span>';
  return '';
}
function universalLabel(f){
  if(!f)return 'RS232';
  if(Array.isArray(f.defs)&&f.defs.length>1)return f.defs.length+' '+u('Entities','entities');
  if(f.def)return String(f.def.label||f.def.key||'Entity');
  if(f.tx&&f.kind==='line')return u('Zeile senden','Send line');
  if(f.tx)return u('Raw senden','Send raw');
  if(f.text){
    let p=f.text.replace(/\\r|\\n|\\t/g,' ').trim();
    if(p.length>24)p=p.slice(0,24)+'…';
    return p||'RX';
  }
  return 'RX Frame';
}
function buildUniversalTransactions(events){
  let arr=events.filter(e=>String(e.dir).indexOf('UART_')===0).map(universalFrame);
  arr.sort((a,b)=>localEventMs(a.e)-localEventMs(b.e)||(Number(a.e.seq)-Number(b.e.seq)));
  let pending=new Map(),pendingUnknown=null,out=[];
  let haveDefs=arr.some(f=>universalDefsFor(f.e).length>0);
  arr.forEach(function(f){
    let key=(Array.isArray(f.defs)&&f.defs.length)?('E'+f.defs.map(d=>String(d.id)).join(',')):null;
    if(f.tx){
      if(key){
        if(pending.has(key)){
          let old=pending.get(key);
          out.push({proto:'Universal',kind:'single',request:old,response:null,events:[old.e],unpaired:true});
        }
        pending.set(key,f);
      }else if(!haveDefs){
        if(pendingUnknown)out.push({proto:'Universal',kind:'single',request:pendingUnknown,response:null,events:[pendingUnknown.e],unpaired:true});
        pendingUnknown=f;
      }else{
        out.push({proto:'Universal',kind:'single',request:f,response:null,events:[f.e],unpaired:true});
      }
      return;
    }
    if(key){
      let req=pending.get(key);
      if(req&&localDeltaMs(req.e,f.e)<=2000){
        pending.delete(key);
        out.push({proto:'Universal',kind:'pair',request:req,response:f,events:[req.e,f.e],delta:localDeltaMs(req.e,f.e)});
      }else{
        out.push({proto:'Universal',kind:'single',request:null,response:f,events:[f.e],unpaired:true});
      }
    }else if(!haveDefs&&pendingUnknown&&localDeltaMs(pendingUnknown.e,f.e)<=2000){
      let req=pendingUnknown;pendingUnknown=null;
      out.push({proto:'Universal',kind:'pair',request:req,response:f,events:[req.e,f.e],delta:localDeltaMs(req.e,f.e)});
    }else{
      out.push({proto:'Universal',kind:'single',request:null,response:f,events:[f.e],unpaired:true});
    }
  });
  pending.forEach(f=>out.push({proto:'Universal',kind:'single',request:f,response:null,events:[f.e],unpaired:true}));
  if(pendingUnknown)out.push({proto:'Universal',kind:'single',request:pendingUnknown,response:null,events:[pendingUnknown.e],unpaired:true});
  out.sort((a,b)=>Math.max(...b.events.map(localEventMs))-Math.max(...a.events.map(localEventMs)));
  return out;
}
function universalResultHtml(txn){
  let f=txn.response||txn.request;if(!f)return '-';
  let h='<div class="proto-result">',all=Array.isArray(f.decoded_all)?f.decoded_all.filter(x=>x&&x.decoded):[];
  if(all.length>1){
    all.forEach(x=>{h+='<span class="proto-result-value '+(all.length===1?'primary':'')+'"><b>'+esc(String(x.def.label||x.def.key||'Entity'))+':</b> '+esc(x.decoded.text)+'</span>'});
  }else if(f.decoded){
    h+='<span class="proto-result-value primary">'+esc(f.decoded.text)+'</span>';
  }else{
    let contentText=universalBytesText(f.content);
    if(contentText!==null&&contentText.length)h+='<span class="proto-result-value text">'+esc(contentText)+'</span>';
    else h+='<span class="proto-result-value">'+esc(bytesCompactHex(f.content))+'</span>';
  }
  h+=universalStatusBadge(f);h+='</div>';return h;
}

function modbusCrc16(b,n=null){
  let len=n==null?b.length:n,crc=0xFFFF;
  for(let i=0;i<len;i++){
    crc^=Number(b[i])&255;
    for(let j=0;j<8;j++)crc=(crc&1)?((crc>>1)^0xA001):(crc>>1);
  }
  return crc&0xFFFF;
}
function be16(b,o){return (((Number(b[o]||0)&255)<<8)|(Number(b[o+1]||0)&255))>>>0}
function modbusFuncLabel(fn){
  const m={
    1:'Read Coils',2:'Read Discrete Inputs',3:'Read Holding Registers',4:'Read Input Registers',
    5:'Write Single Coil',6:'Write Single Register',15:'Write Multiple Coils',16:'Write Multiple Registers'
  };
  let base=Number(fn)&0x7F;
  return m[base]||('Function 0x'+hex2(fn));
}
function modbusExceptionLabel(code){
  const m={1:'Illegal Function',2:'Illegal Data Address',3:'Illegal Data Value',4:'Server Device Failure',
           5:'Acknowledge',6:'Server Device Busy',8:'Memory Parity Error',10:'Gateway Path Unavailable',
           11:'Gateway Target Failed'};
  return m[Number(code)]||('Exception 0x'+hex2(code));
}
function modbusFrame(e){
  let b=hexBytes(e.data||''),status=Number(e.status),tx=String(e.dir)==='UART_TX';
  let slave=b.length?b[0]:0,func=b.length>1?b[1]:0;
  let crcPresent=b.length>=4;
  let crcFrame=crcPresent?((b[b.length-2]&255)|((b[b.length-1]&255)<<8)):null;
  let crcCalc=crcPresent?modbusCrc16(b,b.length-2):null;
  let crcOk=crcPresent?(crcFrame===crcCalc):null;
  if(status===0xEE)crcOk=false;
  return {e:e,b:b,tx:tx,slave:slave,func:func,baseFunc:func&0x7F,exception:(func&0x80)!==0,
          status:status,crcPresent:crcPresent,crcFrame:crcFrame,crcCalc:crcCalc,crcOk:crcOk};
}
function modbusKey(f){return String(f.slave)+':'+String(f.baseFunc)}
function buildModbusTransactions(events){
  let arr=events.filter(e=>String(e.dir).indexOf('UART_')===0).map(modbusFrame);
  arr.sort((a,b)=>localEventMs(a.e)-localEventMs(b.e)||(Number(a.e.seq)-Number(b.e.seq)));
  let pending=new Map(),out=[];
  arr.forEach(function(f){
    if(f.tx){
      let key=modbusKey(f);
      if(pending.has(key)){
        let old=pending.get(key);
        out.push({proto:'Modbus',kind:'single',request:old,response:null,events:[old.e],unpaired:true});
      }
      pending.set(key,f);
      return;
    }
    let key=modbusKey(f),req=pending.get(key);
    if(req&&localDeltaMs(req.e,f.e)<=2000){
      pending.delete(key);
      out.push({proto:'Modbus',kind:'pair',request:req,response:f,events:[req.e,f.e],delta:localDeltaMs(req.e,f.e)});
    }else{
      out.push({proto:'Modbus',kind:'single',request:null,response:f,events:[f.e],unpaired:true});
    }
  });
  pending.forEach(f=>out.push({proto:'Modbus',kind:'single',request:f,response:null,events:[f.e],unpaired:true}));
  out.sort((a,b)=>Math.max(...b.events.map(localEventMs))-Math.max(...a.events.map(localEventMs)));
  return out;
}
function modbusCrcBadge(f){
  if(!f||f.crcOk===null)return '<span class="diag-check-badge neutral">CRC —</span>';
  return f.crcOk
    ?'<span class="diag-check-badge ok">CRC OK</span>'
    :'<span class="diag-check-badge err">CRC BAD</span>';
}
function modbusDecode(f,role){
  if(!f||f.b.length<2)return {html:'<span class="proto-result-value">—</span>',summary:'—'};
  let b=f.b,fn=f.baseFunc,dataEnd=f.crcPresent?b.length-2:b.length,h='',summary='';
  if(f.exception&&b.length>=3){
    let ex=modbusExceptionLabel(b[2]);
    h='<span class="proto-result-value">'+esc(ex)+'</span>';summary=ex;
  }else if(role==='request'&&(fn===1||fn===2||fn===3||fn===4)&&dataEnd>=6){
    let start=be16(b,2),qty=be16(b,4);
    h='<span class="proto-result-value primary">'+u('Start','Start')+' 0x'+start.toString(16).toUpperCase().padStart(4,'0')+'</span>'+
      '<span class="proto-result-value">Qty '+qty+'</span>';
    summary='0x'+start.toString(16).toUpperCase().padStart(4,'0')+' × '+qty;
  }else if(role==='response'&&(fn===3||fn===4)&&dataEnd>=3){
    let count=b[2],vals=[];
    for(let o=3;o+1<Math.min(dataEnd,3+count);o+=2)vals.push(be16(b,o));
    h='<span class="proto-result-value primary">'+vals.length+' '+u('Register','registers')+'</span>';
    if(vals.length)h+='<span class="proto-result-value">'+esc(vals.slice(0,8).join(', ')+(vals.length>8?' …':''))+'</span>';
    summary=vals.join(',');
  }else if(role==='response'&&(fn===1||fn===2)&&dataEnd>=3){
    let count=b[2],bytes=b.slice(3,Math.min(dataEnd,3+count));
    h='<span class="proto-result-value primary">'+count+' B</span><span class="proto-result-value">'+esc(bytesCompactHex(bytes))+'</span>';
    summary=bytesCompactHex(bytes);
  }else if((fn===5||fn===6)&&dataEnd>=6){
    let reg=be16(b,2),val=be16(b,4);
    let value=(fn===5)?(val===0xFF00?u('AN','ON'):(val===0?u('AUS','OFF'):'0x'+val.toString(16).toUpperCase().padStart(4,'0'))):String(val);
    h='<span class="proto-result-value primary">0x'+reg.toString(16).toUpperCase().padStart(4,'0')+'</span>'+
      '<span class="proto-result-value">'+esc(value)+'</span>';
    summary='0x'+reg.toString(16).toUpperCase().padStart(4,'0')+'='+value;
  }else if(role==='request'&&(fn===15||fn===16)&&dataEnd>=7){
    let start=be16(b,2),qty=be16(b,4),bc=b[6];
    h='<span class="proto-result-value primary">'+u('Start','Start')+' 0x'+start.toString(16).toUpperCase().padStart(4,'0')+'</span>'+
      '<span class="proto-result-value">Qty '+qty+'</span><span class="proto-result-value">'+bc+' B</span>';
    summary='0x'+start.toString(16).toUpperCase().padStart(4,'0')+' × '+qty;
  }else if(role==='response'&&(fn===15||fn===16)&&dataEnd>=6){
    let start=be16(b,2),qty=be16(b,4);
    h='<span class="proto-result-value primary">'+u('Start','Start')+' 0x'+start.toString(16).toUpperCase().padStart(4,'0')+'</span>'+
      '<span class="proto-result-value">Qty '+qty+'</span>';
    summary='0x'+start.toString(16).toUpperCase().padStart(4,'0')+' × '+qty;
  }else{
    let data=b.slice(2,dataEnd);
    h='<span class="proto-result-value">'+esc(bytesCompactHex(data))+'</span>';summary=bytesCompactHex(data);
  }
  return {html:'<div class="proto-result">'+h+modbusCrcBadge(f)+'</div>',summary:summary};
}
function modbusFuncFromDef(def,read=true){
  let f=String(read?(def&&def.read_func||def&&def.func||''):(def&&def.func||'')).toLowerCase();
  if(f.includes('discrete'))return read?2:0;if(f.includes('coil'))return read?1:5;if(f.includes('input'))return read?4:0;if(f.includes('holding'))return read?3:6;return 0;
}
function modbusDefsForRequest(req){
  if(!req||!req.tx||req.b.length<6)return [];
  let mod=diagModuleByAddr(req.e.addr),defs=universalDefsFor(req.e),fn=req.baseFunc,reg=be16(req.b,2),slave=req.slave,defSlave=Number(mod&&mod.local_slave||0),reading=(fn>=1&&fn<=4);
  return defs.filter(def=>{
    let dr=diagParseInt(def.reg,-1);if(dr!==reg)return false;
    let ds=diagParseInt(def.slave,defSlave||slave);if(ds!==slave)return false;
    let mode=String(def.mode||'').toLowerCase();if(reading&&mode==='wo')return false;if(!reading&&mode==='ro')return false;
    return modbusFuncFromDef(def,reading)===fn;
  });
}
function modbusDescriptorDecoded(txn){
  if(!txn||!txn.request)return [];
  let req=txn.request,resp=txn.response,defs=modbusDefsForRequest(req);if(!defs.length)return [];
  let fn=req.baseFunc,raw=null;
  if((fn===3||fn===4)&&resp&&!resp.exception&&resp.b.length>=5&&Number(resp.b[2])>=2)raw=be16(resp.b,3);
  else if((fn===1||fn===2)&&resp&&!resp.exception&&resp.b.length>=4)raw=(resp.b[3]&1)?1:0;
  else if(fn===6&&req.b.length>=6)raw=be16(req.b,4);
  else if(fn===5&&req.b.length>=6)raw=be16(req.b,4)===0xFF00?1:0;
  if(raw===null)return [];
  return defs.map(def=>({def:def,decoded:diagDecodeDefValue(def,raw,''),raw:raw})).filter(x=>x.decoded);
}
function modbusResultHtml(txn){
  let f=txn.response||txn.request;if(!f)return '-';
  let dd=modbusDescriptorDecoded(txn);
  if(dd.length){let h='<div class="proto-result">';dd.forEach(x=>{h+='<span class="proto-result-value"><b>'+esc(String(x.def.label||x.def.key||'Entity'))+':</b> '+esc(x.decoded.text)+'</span>'});h+=modbusCrcBadge(f)+'</div>';return h;}
  return modbusDecode(f,txn.response?'response':'request').html;
}
function protocolRawDetails(e,proto){
  let b=hexBytes(e.data||''),key='x-'+e.seq;
  let open=diagOpenPayloads.has(key)?' open':'';
  if(proto==='Modbus'){
    let f=modbusFrame(e),dataEnd=f.crcPresent?b.length-2:b.length;
    let data=b.slice(2,dataEnd);
    let crc=f.crcPresent?(hex2(b[b.length-2])+' '+hex2(b[b.length-1])):'—';
    let rawDefs=f.tx?modbusDefsForRequest(f):[],rawDecoded=[];
    if(rawDefs.length&&(f.baseFunc===5||f.baseFunc===6)&&b.length>=6){let rv=f.baseFunc===5?(be16(b,4)===0xFF00?1:0):be16(b,4);rawDecoded=rawDefs.map(d=>({def:d,val:diagDecodeDefValue(d,rv,'')})).filter(x=>x.val);}
    return '<details class="diag-raw-details" data-payload-key="'+key+'" ontoggle="diagPayloadToggle(this)"'+open+'>'+
      '<summary>'+u('Frame-Felder','Frame fields')+' · '+b.length+' B</summary>'+
      '<div class="proto-raw-grid">'+
        '<span class="proto-raw-field"><span class="k">Slave</span><span class="v">'+f.slave+'</span></span>'+
        '<span class="proto-raw-field"><span class="k">Function</span><span class="v">0x'+hex2(f.func)+'</span></span>'+
        '<span class="proto-raw-field"><span class="k">CRC</span><span class="v">'+crc+'</span></span>'+
        '<span class="proto-raw-field"><span class="k">Status</span><span class="v">'+(f.crcOk===true?'OK':(f.crcOk===false?'BAD':'—'))+'</span></span>'+
        '<span class="proto-raw-field data"><span class="k">Data</span><span class="v">'+esc(bytesCompactHex(data,64))+'</span></span>'+
        (rawDecoded.length?'<span class="proto-raw-field data"><span class="k">'+u('Descriptor','Descriptor')+'</span><span class="v">'+rawDecoded.map(x=>esc(String(x.def.label||x.def.key||'Entity')+': '+x.val.text)).join('<br>')+'</span></span>':'')+
      '</div>'+
      '<div class="diag-payload-line"><b>HEX:</b> <span class="data">'+esc(bytesCompactHex(b,96))+'</span></div>'+
      '</details>';
  }
  let f=universalFrame(e),contentText=universalBytesText(f.content);
  let title=(Array.isArray(f.defs)&&f.defs.length>1)?(f.defs.length+' '+u('Entities','entities')):(f.def?String(f.def.label||f.def.key||'Entity'):u('Frame','Frame'));
  let pattern=f.def?universalHexText(f.def.match_hex):'';
  let poll=f.def?universalHexText(f.def.poll_hex):'';
  let rule=f.tx?(poll||universalHexText(f.def&&f.def.set_hex)||universalHexText(f.def&&f.def.on_hex)||universalHexText(f.def&&f.def.off_hex)):pattern;
  return '<details class="diag-raw-details" data-payload-key="'+key+'" ontoggle="diagPayloadToggle(this)"'+open+'>'+
    '<summary>'+esc(title)+' · '+b.length+' B</summary>'+
    '<div class="proto-raw-grid">'+
      '<span class="proto-raw-field"><span class="k">Dir</span><span class="v">'+(f.tx?'TX':'RX')+'</span></span>'+
      '<span class="proto-raw-field"><span class="k">Entity</span><span class="v">'+esc(f.def?String(f.def.key||f.def.id):'—')+'</span></span>'+
      '<span class="proto-raw-field"><span class="k">'+u('Regel','Rule')+'</span><span class="v">'+esc(rule||'—')+'</span></span>'+
      '<span class="proto-raw-field"><span class="k">Status</span><span class="v">'+(f.badChecksum?'BAD-CS':(f.badPattern?'BAD-PATTERN':'OK'))+'</span></span>'+
      '<span class="proto-raw-field data"><span class="k">'+u('Dekodiert','Decoded')+'</span><span class="v">'+((Array.isArray(f.decoded_all)&&f.decoded_all.filter(x=>x.decoded).length>1)?f.decoded_all.filter(x=>x.decoded).map(x=>esc(String(x.def.label||x.def.key||'Entity')+': '+x.decoded.text)).join('<br>'):esc(f.decoded?f.decoded.text:(contentText!==null?contentText:bytesCompactHex(f.content,64))))+'</span></span>'+
    '</div>'+
    '<div class="diag-payload-line"><b>HEX:</b> <span class="data">'+esc(bytesCompactHex(b,96))+'</span></div>'+
    '</details>';
}

function buildJbcTransactions(events){
  let arr=events.filter(e=>String(e.dir).indexOf('UART_')===0).map(jbcFrame).filter(Boolean);
  arr.sort((a,b)=>localEventMs(a.e)-localEventMs(b.e)||(Number(a.e.seq)-Number(b.e.seq)));
  let pending=new Map(),out=[];
  arr.forEach(function(f){
    let role=jbcTraceRole(f.e),key=(f.protocol==='P01'?('P01:'+hex2(f.ctrl)):('P02:'+hex2(f.fid)));
    if(role==='request'){
      if(pending.has(key)){
        let old=pending.get(key);out.push({proto:'JBC',kind:'single',request:old,response:null,events:[old.e],unpaired:true});
      }
      pending.set(key,f);return;
    }
    let req=pending.get(key);
    if(req&&localDeltaMs(req.e,f.e)<=2000){
      pending.delete(key);out.push({proto:'JBC',kind:'pair',request:req,response:f,events:[req.e,f.e],delta:localDeltaMs(req.e,f.e)});
    }else out.push({proto:'JBC',kind:'single',request:null,response:f,events:[f.e],unpaired:true});
  });
  pending.forEach(f=>out.push({proto:'JBC',kind:'single',request:f,response:null,events:[f.e],unpaired:true}));
  out.sort((a,b)=>Math.max(...b.events.map(localEventMs))-Math.max(...a.events.map(localEventMs)));
  return out;
}

function buildWellerTransactions(events){
  let arr=events.filter(e=>String(e.dir).indexOf('UART_')===0).map(wellerFrame);
  arr.sort((a,b)=>localEventMs(a.e)-localEventMs(b.e)||(Number(a.e.seq)-Number(b.e.seq)));
  let pending=new Map(),out=[];
  arr.forEach(function(f){
    let dir=String(f.e.dir),key=f.cmd;
    if(dir==='UART_TX'){
      // Uppercase one-byte frames are queries. Lowercase/config commands may
      // still get an acknowledgement with the same command and are paired too.
      if(pending.has(key)){
        let old=pending.get(key);
        out.push({proto:'Weller',kind:'single',request:old,response:null,events:[old.e],unpaired:true});
      }
      pending.set(key,f);
      return;
    }
    if(dir==='UART_RX'){
      let req=pending.get(key);
      if(req&&localDeltaMs(req.e,f.e)<=650){
        pending.delete(key);
        out.push({proto:'Weller',kind:'pair',request:req,response:f,events:[req.e,f.e],delta:localDeltaMs(req.e,f.e)});
      }else{
        out.push({proto:'Weller',kind:'single',request:null,response:f,events:[f.e],unpaired:true});
      }
    }
  });
  pending.forEach(f=>out.push({proto:'Weller',kind:'single',request:f,response:null,events:[f.e],unpaired:true}));
  out.sort((a,b)=>Math.max(...b.events.map(localEventMs))-Math.max(...a.events.map(localEventMs)));
  return out;
}
function buildLocalTransactions(events){
  let addr=0;
  for(let e of events){if(String(e.dir).indexOf('UART_')===0){addr=Number(e.addr);break;}}
  if(addr>=0x10&&addr<=0x1F)return buildJbcTransactions(events);
  if(addr>=0x30&&addr<=0x3F)return buildWellerTransactions(events);
  if(addr>=0x50&&addr<=0x5F)return buildUniversalTransactions(events);
  if(addr>=0x60&&addr<=0x6F)return buildModbusTransactions(events);
  return events.filter(e=>String(e.dir).indexOf('UART_')===0).map(e=>({proto:diagProto(e),kind:'single',request:null,response:null,events:[e],raw:e}));
}
function jbcIsWriteCommand(c){
  return [
    0x1F,0x31,0x35,0x37,0x39,0x3B,0x3E,0x40,
    0x52,0x54,0x56,0x58,0x5C,0x5E,0x60,0xE1
  ].includes(Number(c));
}
function jbcResultHtml(txn){
  if(!txn)return '-';
  let request=txn.request||null,response=txn.response||null;
  let f=request||response;
  if(!f)return '-';

  let write=jbcIsWriteCommand(f.ctrl);
  let chosen=null,role='';
  if(write&&request&&request.payload&&request.payload.length){
    chosen=request;role='request';
  }else if(response){
    chosen=response;role='response';
  }else{
    chosen=request;role='request';
  }

  let info=jbcPayloadInfo(chosen,role);
  let h='<div class="jbc-result-wrap">'+info.html;
  if(write&&response){
    h+='<span class="diag-check-badge ok">'+u('ACK','ACK')+'</span>';
  }else if(!response&&request){
    h+='<span class="diag-check-badge neutral">'+u('offen','pending')+'</span>';
  }
  h+='</div>';
  return h;
}
function wellerValueHtml(f){
  if(!f)return '<span class="diag-result-sub">-</span>';
  let label=wellerLabel(f.cmd),value='';
  if(f.value!==null){
    if(f.cmd===83)value=f.value+' %';
    else if(f.cmd===68)value=(f.value*10)+' RPM';
    else if(f.cmd===86)value='V'+(f.value/100).toFixed(2);
    else if(f.cmd===65)value=f.value>=100?u('an','on'):u('aus','off');
    else if(f.cmd===70||f.cmd===71)value=(f.value*10)+' min';
    else value=String(f.value);
  }else value=asciiBytes(f.b);
  let h='<div class="diag-result-main"><span class="diag-result-value">'+esc(value||'-')+'</span>';
  if(f.check===true)h+='<span class="diag-check-badge ok">CS OK</span>';
  else if(f.check===false)h+='<span class="diag-check-badge err">CS BAD</span>';
  h+='</div>';
  return h;
}
function txnSignature(txn){
  if(txn.proto==='JBC'){
    let f=txn.response||txn.request;
    let payload=(txn.response?txn.response.payload:(f?f.payload:[]))||[];
    return 'J:'+String(f?f.ctrl:'')+':'+payload.map(hex2).join('');
  }
  if(txn.proto==='Weller'){
    let f=txn.response||txn.request;
    return 'W:'+String(f?f.cmd:'')+':'+String(f&&f.value!==null?f.value:asciiBytes(f?f.b:[]));
  }
  if(txn.proto==='Universal'){
    let f=txn.response||txn.request;
    return 'U:'+String(f&&f.def?f.def.id:'?')+':'+String(f&&f.decoded?f.decoded.text:bytesCompactHex(f?f.content:[]));
  }
  if(txn.proto==='Modbus'){
    let f=txn.response||txn.request;
    return 'M:'+String(f?f.slave:'')+':'+String(f?f.baseFunc:'')+':'+String(f?bytesCompactHex(f.b):'');
  }
  return JSON.stringify(txn.events.map(e=>[e.dir,e.cmd,e.data]));
}
function suppressLocalRepeats(txns){
  if(!diagLocalChangesOnly)return txns;
  let seen=new Map(),out=[];
  txns.forEach(function(t){
    let sig=txnSignature(t);
    let f=t.response||t.request;
    let command=t.proto==='JBC'?f?.ctrl:(t.proto==='Modbus'?f?.baseFunc:(t.proto==='Universal'?'rs232':f?.cmd));
    let key=t.proto+':'+command;
    if(seen.get(key)===sig)return;
    seen.set(key,sig);out.push(t);
  });
  return out;
}
function renderLocalStats(txns,totalAvailable){
  let paired=txns.filter(t=>t.kind==='pair').length;
  let unpaired=txns.length-paired;
  let bad=0,deltas=[];
  txns.forEach(t=>{
    if(t.delta!=null)deltas.push(Number(t.delta));
    if(t.proto==='Weller'&&t.response&&t.response.check===false)bad++;
    if(t.proto==='Universal'){
      let f=t.response||t.request;if(f&&(f.badChecksum||f.badPattern))bad++;
    }
    if(t.proto==='Modbus'){
      let f=t.response||t.request;if(f&&f.crcOk===false)bad++;
    }
  });
  let avg=deltas.length?Math.round(deltas.reduce((a,b)=>a+b,0)/deltas.length):0;
  let total=Number(totalAvailable==null?txns.length:totalAvailable);
  let countText=txns.length<total?(txns.length+' / '+total):String(total);
  let h='<span class="diag-local-stat"><strong>'+countText+'</strong> '+u('Transaktionen','transactions')+'</span>'+
    '<span class="diag-local-stat ok"><strong>'+paired+'</strong> '+u('gepaart','paired')+'</span>';
  if(unpaired)h+='<span class="diag-local-stat warn"><strong>'+unpaired+'</strong> '+u('einzeln','unpaired')+'</span>';
  if(deltas.length)h+='<span class="diag-local-stat"><strong>'+avg+' ms</strong> Ø Δt</span>';
  if(bad)h+='<span class="diag-local-stat err"><strong>'+bad+'</strong> '+u('Framefehler','frame errors')+'</span>';
  document.getElementById('diag_local_stats').innerHTML=h;
}
function diagRenderLocalTransactions(events){
  let allTxns=suppressLocalRepeats(buildLocalTransactions(events));
  let txns=allTxns.slice(0,DIAG_TRACE_ROW_LIMIT);
  renderLocalStats(txns,allTxns.length);
  if(document.getElementById('diag_view').value==='local'&&diagLocalLayout==='transactions')
    diagSetRowCount(txns.length,allTxns.length,'Transaktionen','transactions');
  let rows='';let no=0;
  txns.forEach(function(t){
    ++no;
    let evt=t.events[0];
    let tm=Math.min(...t.events.map(localEventMs));
    let proto=t.proto;
    let command='-',code='',flow='-',result='-',delta=t.delta!=null?t.delta:null;
    let rowClass=t.unpaired?'diag-local-unpaired':'';
    if(proto==='JBC'){
      let f=t.request||t.response;
      command=jbcFriendlyFrame(f);code=jbcCtrlLabelFrame(f)+' · '+f.protocol+(f.protocol==='P02'?(' · FID '+hex2(f.fid)):'');
      if(t.request&&t.response){
        flow='<div class="diag-flow"><span class="diag-flow-node">0x'+hex2(t.request.src)+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">0x'+hex2(t.request.dst)+'</span><span class="diag-flow-arrow">↩</span></div>';
      }else{
        let q=t.request||t.response;
        flow='<div class="diag-flow"><span class="diag-flow-node">0x'+hex2(q.src)+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">0x'+hex2(q.dst)+'</span></div>';
      }
      result=jbcResultHtml(t);
    }else if(proto==='Weller'){
      let f=t.request||t.response;
      command=wellerLabel(f.cmd);code=(f.cmd>=32&&f.cmd<=126?String.fromCharCode(f.cmd):'0x'+hex2(f.cmd));
      flow=t.request&&t.response
        ?'<div class="diag-flow"><span class="diag-flow-node">'+u('Modul','Module')+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">Weller</span><span class="diag-flow-arrow">↩</span></div>'
        :'<div class="diag-flow"><span class="diag-flow-node">'+(t.request?u('Modul','Module'):'Weller')+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">'+(t.request?'Weller':u('Modul','Module'))+'</span></div>';
      result=wellerValueHtml(t.response||t.request);
      if(t.response&&t.response.check===false)rowClass+=' diag-local-check-bad';
    }else if(proto==='Universal'){
      let f=t.request||t.response;
      command=universalLabel(f);
      let mod=universalModuleFor(f&&f.e);
      code=(f&&f.def?String(f.def.key||('Entity '+f.def.id)):(f&&f.tx?(f.kind==='line'?'LINE':'RAW'):'RX'));
      if(mod&&mod.local_profile)code+=' · '+String(mod.local_profile);
      flow=t.request&&t.response
        ?'<div class="diag-flow"><span class="diag-flow-node">'+u('Modul','Module')+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">RS232</span><span class="diag-flow-arrow">↩</span></div>'
        :'<div class="diag-flow"><span class="diag-flow-node">'+(t.request?u('Modul','Module'):'RS232')+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">'+(t.request?'RS232':u('Modul','Module'))+'</span></div>';
      result=universalResultHtml(t);
      let uf=t.response||t.request;
      if(uf&&(uf.badChecksum||uf.badPattern))rowClass+=' diag-local-check-bad';
    }else if(proto==='Modbus'){
      let f=t.request||t.response;
      command=modbusFuncLabel(f.func);code='FC '+hex2(f.func)+' · Slave '+f.slave;
      flow=t.request&&t.response
        ?'<div class="diag-flow"><span class="diag-flow-node">'+u('Modul','Module')+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">S'+f.slave+'</span><span class="diag-flow-arrow">↩</span></div>'
        :'<div class="diag-flow"><span class="diag-flow-node">'+(t.request?u('Modul','Module'):('S'+f.slave))+'</span><span class="diag-flow-arrow">→</span><span class="diag-flow-node">'+(t.request?('S'+f.slave):u('Modul','Module'))+'</span></div>';
      result=modbusResultHtml(t);
      let mf=t.response||t.request;if(mf&&mf.crcOk===false)rowClass+=' diag-local-check-bad';
    }else{
      command=proto;code='';flow='-';result=esc(evt.text||'-');
    }
    let dclass=delta!=null?(delta<=25?'fast':(delta>=250?'slow':'')):'';
    rows+='<tr class="'+rowClass.trim()+'"><td>'+no+'</td><td>'+fmtTime(tm)+'</td>'+
      '<td><span class="diag-mini-badge">'+esc(proto)+'</span></td>'+
      '<td>'+(
        proto==='JBC'
          ?'<div class="diag-local-command"><span class="diag-proto-command jbc">'+esc(command)+'</span><small>'+esc(code)+'</small></div>'
        :proto==='Universal'
          ?'<div class="diag-local-command"><span class="diag-proto-command universal">'+esc(command)+'</span><small>'+esc(code)+'</small></div>'
        :proto==='Modbus'
          ?'<div class="diag-local-command"><span class="diag-proto-command modbus">'+esc(command)+'</span><small>'+esc(code)+'</small></div>'
          :'<div class="diag-local-command"><strong>'+esc(command)+'</strong><small>'+esc(code)+'</small></div>')+'</td>'+
      '<td>'+flow+'</td><td>'+result+'</td>'+
      '<td class="diag-delta '+dclass+'">'+(delta!=null?delta+' ms':'—')+'</td>'+
      '<td class="diag-txn-details">'+txnRawDetails(t)+'</td></tr>';
  });
  document.getElementById('uart_txn_rows').innerHTML=rows||'<tr><td colspan="8" class="muted">'+u('Keine Gerätebus-Transaktionen','No device-bus transactions')+'</td></tr>';
}
function diagLocalDecoded(e){
  let addr=Number(e.addr),b=hexBytes(e.data||''),tx=String(e.dir)==='UART_TX';
  if(addr>=0x10&&addr<=0x1F){
    let f=jbcFrame(e);
    if(!f){
      let proto=diagProto(e),mark=Number(e.status),label=mark===0xEE?'BCC ERROR':(mark===0xEF?'FRAME ERROR':(mark===0xF0?'RAW HANDSHAKE':'RAW'));
      return {proto:proto,frame:'<div class="diag-proto-frame"><span class="diag-proto-command jbc">'+esc(label)+'</span></div>',detail:'<span class="diag-check-badge '+((mark===0xEE||mark===0xEF)?'err':'neutral')+'">'+esc(proto)+'</span>',raw:localRawDetails(e,(e.len||0)+' B')};
    }
    let role=jbcTraceRole(e);
    let info=jbcPayloadInfo(f,role);
    let exact=jbcCtrlLabelFrame(f),friendly=jbcFriendlyFrame(f);

    let frame='<div class="jbc-raw-command">'+
      '<span class="diag-proto-command jbc">'+esc(friendly)+'</span>'+
      '<span class="jbc-meta-badge">'+esc(f.protocol)+'</span>'+
      (f.protocol==='P02'?'<span class="jbc-meta-badge">FID '+hex2(f.fid)+'</span>':'')+
      '</div>'+
      '<div class="jbc-inline-meta">'+
      '<span class="jbc-meta-badge">0x'+hex2(f.src)+' → 0x'+hex2(f.dst)+'</span>'+
      '<span class="jbc-meta-badge">'+esc(exact)+'</span>'+
      '</div>';

    let detail='<div class="jbc-raw-result">'+info.html+'</div>';
    return {proto:'JBC '+f.protocol,frame:frame,detail:detail,raw:jbcRawDetails(e,f)};
  }
  if(addr>=0x30&&addr<=0x3F){
    let f=wellerFrame(e),char=f.cmd>=32&&f.cmd<=126?String.fromCharCode(f.cmd):('0x'+hex2(f.cmd));
    let frame='<div class="diag-proto-frame"><span class="diag-proto-command weller">'+esc(char+' · '+wellerLabel(f.cmd))+'</span>';
    if(f.check===true)frame+='<span class="diag-check-badge ok">CS OK</span>';
    else if(f.check===false)frame+='<span class="diag-check-badge err">CS BAD</span>';
    frame+='</div>';
    let detail='<div class="diag-proto-detail">'+wellerValueHtml(f)+'</div>';
    return {proto:'Weller UART',frame:frame,detail:detail,raw:localRawDetails(e,(e.len||0)+' B')};
  }
  if(addr>=0x50&&addr<=0x5F){
    let f=universalFrame(e),label=universalLabel(f);
    let mod=universalModuleFor(e);
    let frame='<div class="diag-proto-frame"><span class="diag-proto-command universal">'+esc(label)+'</span>'+
      '<span class="proto-meta-badge">'+(f.def?esc(String(f.def.key||('E'+f.def.id))):(f.tx?(f.kind==='line'?'LINE':'RAW'):'RX'))+'</span>'+
      (mod&&mod.local_profile?'<span class="proto-meta-badge">'+esc(String(mod.local_profile))+'</span>':'')+
      universalStatusBadge(f)+'</div>';
    let detail=universalResultHtml({request:f.tx?f:null,response:f.tx?null:f});
    return {proto:'Universal RS232',frame:frame,detail:detail,raw:protocolRawDetails(e,'Universal')};
  }
  if(addr>=0x60&&addr<=0x6F){
    let f=modbusFrame(e),role=f.tx?'request':'response',di=modbusDecode(f,role);
    let frame='<div class="diag-proto-frame"><span class="diag-proto-command modbus">'+esc(modbusFuncLabel(f.func))+'</span>'+
      '<span class="proto-meta-badge">Slave '+f.slave+'</span>'+
      '<span class="proto-meta-badge">FC '+hex2(f.func)+'</span>'+
      modbusCrcBadge(f)+'</div>';
    return {proto:'Modbus RTU',frame:frame,detail:di.html,raw:protocolRawDetails(e,'Modbus')};
  }
  return {
    proto:diagProto(e),
    frame:'<div class="diag-proto-frame"><span class="diag-proto-command">'+esc(diagProto(e))+'</span></div>',
    detail:'<div class="diag-proto-detail">'+esc(e.text||'-')+'</div>',
    raw:localRawDetails(e,(e.len||0)+' B')
  };
}
function diagRenderLocalRaw(events){
  let rows='';let no=0;
  let raw=events.filter(e=>String(e.dir).indexOf('UART_')===0);
  let shownRaw=raw.slice(0,DIAG_TRACE_ROW_LIMIT);
  if(document.getElementById('diag_view').value==='local'&&diagLocalLayout==='raw')
    diagSetRowCount(shownRaw.length,raw.length,'Rohframes','raw frames');
  shownRaw.forEach(function(e){
    ++no;let cls=e.dir.toLowerCase();let d=diagLocalDecoded(e);
    let rowClass=(Number(e.status)===0xEE||Number(e.status)===0xEF)?'row-error':'';
    rows+='<tr class="'+rowClass+'"><td>'+no+'</td><td>'+fmtTime(localEventMs(e))+'</td>'+
      '<td><span class="diag-dir '+cls+'">'+e.dir.replace('UART_','')+'</span></td>'+
      '<td><span class="diag-mini-badge">'+esc(d.proto)+'</span></td>'+
      '<td>'+d.frame+'</td><td>'+d.detail+'</td>'+
      '<td>'+(e.latency?e.latency+' ms':'-')+'</td><td class="diag-payload">'+d.raw+'</td></tr>';
  });
  document.getElementById('uart_rows').innerHTML=rows||'<tr><td colspan="8" class="muted">'+u('Kein Gerätebus-Mitschnitt','No device-bus trace')+'</td></tr>';
}
function diagRenderLocal(events){
  diagRenderLocalTransactions(events);
  diagRenderLocalRaw(events);
}

function diagApplyFilter(){
  let q=(document.getElementById('diag_filter')?.value||'').trim().toLowerCase();
  let ev=(lastDiag&&lastDiag.events)||[];
  diagFilteredEvents=ev.filter(function(e){
    return diagQuickMatch(e)&&(!q||diagHay(e).indexOf(q)>=0);
  });
  if(diagTraceLayout==='pairs')diagRenderPairs(diagFilteredEvents);else diagRenderRaw(diagFilteredEvents);
  diagRenderLocal(diagFilteredEvents);

}

function rateDelta(cur,prev,key,sec){
  if(!prev||sec<=0)return 0;
  let a=Number(cur[key]||0),b=Number(prev[key]||0);
  return a>=b?(a-b)/sec:0;
}
function modulePrevByAddr(d,addr){
  if(!d||!d.modules)return null;
  return d.modules.find(m=>Number(m.addr)===Number(addr))||null;
}
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
let diagLedSnapshot=null,diagLedBusy=false;
function diagSetLedWord(id,label,ev,valid,enabled){let e=document.getElementById(id);if(!e)return;let st=ofeLedEventStyle(ev),live=!!valid&&!!enabled&&Number(ev)>0;e.textContent=label;e.dataset.ofeLed='1';e.dataset.ledEvent=String(Number(ev||0));e.dataset.ledLive=live?'1':'0';e.classList.remove('fx-breath','fx-whitebreath','fx-greenwhite','fx-bluewhite','fx-blink','fx-double');e.classList.toggle('is-live',live);e.title=valid?(label+' · '+st.n):(label+' · '+u('keine LED-Telemetrie','no LED telemetry'));ofeLedRenderElement(e,ofeLedNow())}
function diagApplyLedSnapshot(){let d=diagLedSnapshot;if(!d)return;diagSetLedWord('diag_master_led_ofe','OFE',d.master_ofe,true,!!d.enabled);diagSetLedWord('diag_master_led_evt','EVT',d.master_evt,true,!!d.enabled);(d.modules||[]).forEach(m=>{diagSetLedWord('diag_led_ofe_'+m.addr,'OFE',m.ofe,!!m.valid&&!!m.online,!!d.enabled);diagSetLedWord('diag_led_evt_'+m.addr,'EVT',m.evt,!!m.valid&&!!m.online,!!d.enabled)})}
async function loadDiagLedState(){if(diagLedBusy)return;diagLedBusy=true;let t0=performance.now();try{let r=await fetch('/led_state',{cache:'no-store'});if(!r.ok)return;let d=await r.json(),t1=performance.now();diagLedSnapshot=d;ofeLedClockSync(d.uptime_ms,t0,t1);ofeLedEnsureRenderer();diagApplyLedSnapshot()}catch(e){}finally{diagLedBusy=false}}
function diagSerialGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M7,3H17V5H19V8H16V14H8V8H5V5H7V3M17,9H19V14H17V9M11,15H13V22H11V15M5,9H7V14H5V9Z" fill="currentColor"/></svg>`}
function diagSwitchGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M1,11H3.17C3.58,9.83 4.69,9 6,9C6.65,9 7.25,9.21 7.74,9.56L14.44,4.87L15.58,6.5L8.89,11.2C8.96,11.45 9,11.72 9,12A3,3 0 0,1 6,15C4.69,15 3.58,14.17 3.17,13H1V11M23,11V13H20.83C20.42,14.17 19.31,15 18,15A3,3 0 0,1 15,12A3,3 0 0,1 18,9C19.31,9 20.42,9.83 20.83,11H23M6,11A1,1 0 0,0 5,12A1,1 0 0,0 6,13A1,1 0 0,0 7,12A1,1 0 0,0 6,11M18,11A1,1 0 0,0 17,12A1,1 0 0,0 18,13A1,1 0 0,0 19,12A1,1 0 0,0 18,11Z" fill="currentColor"/></svg>`}
function diagFanGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M12,11A1,1 0 0,0 11,12A1,1 0 0,0 12,13A1,1 0 0,0 13,12A1,1 0 0,0 12,11M12.5,2C17,2 17.11,5.57 14.75,6.75C13.76,7.24 13.32,8.29 13.13,9.22C13.61,9.42 14.03,9.73 14.35,10.13C18.05,8.13 22.03,8.92 22.03,12.5C22.03,17 18.46,17.1 17.28,14.73C16.78,13.74 15.72,13.3 14.79,13.11C14.59,13.59 14.28,14 13.88,14.34C15.87,18.03 15.08,22 11.5,22C7,22 6.91,18.42 9.27,17.24C10.25,16.75 10.69,15.71 10.89,14.79C10.4,14.59 9.97,14.27 9.65,13.87C5.96,15.85 2,15.07 2,11.5C2,7 5.56,6.89 6.74,9.26C7.24,10.25 8.29,10.68 9.22,10.87C9.41,10.39 9.73,9.97 10.14,9.65C8.15,5.96 8.94,2 12.5,2Z" fill="currentColor"/></svg>`}
function diagMonitorGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M21,16H3V4H21M21,2H3C1.89,2 1,2.89 1,4V16A2,2 0 0,0 3,18H10V20H8V22H16V20H14V18H21A2,2 0 0,0 23,16V4C23,2.89 22.1,2 21,2Z" fill="currentColor"/></svg>`}
function diagUsbGlyph(){return `<svg class="module-head-glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 2C6.9 2 6 2.9 6 4V12H5V16L9 20V22H15V20L19 16V12H18V4C18 2.9 17.11 2 16 2M8 4H16V12H8M9 7V9H11V7M13 7V9H15V7Z" fill="currentColor"/></svg>`}
function diagModuleVisual(m){
  switch(Number(m.type)){
    case 1:return {cls:'jbc',icon:diagSerialGlyph(),sub:'JBC Bus'};
    case 2:return {cls:'fan',icon:diagSwitchGlyph(),sub:'Fan/IO'};
    case 3:return {cls:'fan',icon:diagSwitchGlyph(),sub:'Fan/IO Pro'};
    case 5:return {cls:'weller',icon:diagFanGlyph(),sub:'Weller'};
    case 6:return {cls:'display',icon:diagMonitorGlyph(),sub:'Display'};
    case 7:return {cls:'universal',icon:diagSerialGlyph(),sub:'Universal RS232'};
    case 8:return {cls:'modbus',icon:diagSerialGlyph(),sub:'Modbus RTU'};
    case 9:return {cls:'jbcusb',icon:diagUsbGlyph(),sub:'JBC USB'};
    default:return {cls:'fan',icon:'?',sub:m.type_name||'Module'};
  }
}
let diagModuleCardSignature=null;
function diagModuleSignature(modules){return (modules||[]).map(m=>[Number(m.addr||0),Number(m.type||0),String(m.name||''),String(m.type_name||''),String(m.device_bus||''),String(m.transport||'rs485')].join('|')).join(';')}
function diagRenderModules(d,sec){
  let modules=d.modules||[],totalRate=0;
  modules.forEach(function(m){let p=modulePrevByAddr(prevDiag,m.addr);if(p&&sec>0)totalRate+=Math.max(0,(Number(m.tx_wire_bytes)-Number(p.tx_wire_bytes))/sec)+Math.max(0,(Number(m.rx_wire_bytes)-Number(p.rx_wire_bytes))/sec)});
  let signature=diagModuleSignature(modules),cards=document.getElementById('diag_module_cards');
  if(signature!==diagModuleCardSignature){
    let shell='';
    modules.forEach(function(m){
      let v=diagModuleVisual(m),online=m.online?'is-online':'is-offline';
      shell+='<div class="diag-module-card '+v.cls+'" id="diag_card_'+m.addr+'">';
      shell+='<div class="diag-module-head"><div class="diag-module-title">'+esc(m.name)+'</div><div class="diag-module-head-main"><div class="diag-module-identity"><span class="diag-module-icon">'+v.icon+'</span><div class="diag-module-copy"><div class="diag-module-type">'+esc(m.type_name||v.sub)+'</div><div class="diag-module-address">'+addrText(m.addr)+'</div></div></div>';
      shell+='<div class="diag-module-statuses"><span class="diag-mini-badge">'+(m.transport==='wifi'?'WLAN':'RS485')+'</span><span id="diag_online_'+m.addr+'" class="diag-mini-badge diag-online-pill '+online+'"><span class="diag-dot '+(m.online?'ok':'err')+'"></span>'+(m.online?'online':'offline')+'</span></div></div></div>';
      shell+='<div class="diag-led-pair"><span id="diag_led_ofe_'+m.addr+'" class="diag-led-word">OFE</span><span id="diag_led_evt_'+m.addr+'" class="diag-led-word">EVT</span></div>';
      shell+='<div id="diag_dynamic_'+m.addr+'" class="diag-module-dynamic"></div></div>';
    });
    cards.innerHTML=shell||'<div class="muted">'+u('Keine Module','No modules')+'</div>';
    diagModuleCardSignature=signature;
    diagApplyLedSnapshot();
  }
  modules.forEach(function(m){
    let p=modulePrevByAddr(prevDiag,m.addr);
    let tx=p&&sec>0?(Number(m.tx_wire_bytes)-Number(p.tx_wire_bytes))/sec:0;
    let rx=p&&sec>0?(Number(m.rx_wire_bytes)-Number(p.rx_wire_bytes))/sec:0;
    let req=p&&sec>0?(Number(m.requests)-Number(p.requests))/sec:0;
    if(tx<0)tx=0;if(rx<0)rx=0;if(req<0)req=0;
    let share=totalRate>0?((tx+rx)*100/totalRate):0;
    let timeouts=Number(m.timeouts||0),badSeq=Number(m.bad_seq||0),badCmd=Number(m.bad_cmd||0),online=m.online?'is-online':'is-offline';
    let pill=document.getElementById('diag_online_'+m.addr);
    if(pill){pill.className='diag-mini-badge diag-online-pill '+online;pill.innerHTML='<span class="diag-dot '+(m.online?'ok':'err')+'"></span>'+(m.online?'online':'offline')}
    let body='';
    body+='<div class="diag-module-body">';
    body+='<div class="diag-status-metric"><div><div class="k">'+u('Busanteil','Bus share')+'</div><div class="v diag-bus-share-value"><strong>'+share.toFixed(1)+'%</strong><span class="diag-share-track"><span class="diag-share-fill" style="width:'+Math.min(100,Math.max(0,share)).toFixed(1)+'%"></span></span></div></div>';
    body+='<div><div class="k">Req/s</div><div class="v">'+req.toFixed(req<10?1:0)+'</div></div></div>';
    body+='<div class="diag-status-metric"><div><div class="k">TX / RX</div><div class="v">'+fmtBytes(tx)+'<br><span class="diag-rate-muted">'+fmtBytes(rx)+'</span></div></div>';
    body+='<div><div class="k">'+u('Latenz','Latency')+'</div><div class="v">'+m.latency_avg_ms+' ms<br><span class="diag-rate-muted">max '+m.latency_max_ms+' ms</span></div></div></div>';
    body+='<div class="diag-errors-line"><div class="k">'+u('Fehler seit Boot','Errors since boot')+'</div><div class="diag-metric-split">';
    body+='<span class="diag-metric-chip '+(timeouts?'err':'ok')+'" title="Timeout">T '+timeouts+'</span>';
    body+='<span class="diag-metric-chip '+(badSeq?'warn':'ok')+'" title="Sequence">S '+badSeq+'</span>';
    body+='<span class="diag-metric-chip '+(badCmd?'warn':'ok')+'" title="Command">C '+badCmd+'</span>';
    body+='</div></div></div>';
    body+='<div class="diag-device-section"><div class="diag-device-head"><div class="diag-device-title">'+esc(m.device_bus)+'</div><span class="diag-mini-badge diag-online-pill '+online+'"><span class="diag-dot '+(m.online?'ok':'err')+'"></span>'+(m.online?u('bereit','ready'):u('offline','offline'))+'</span></div>';
    body+='<div class="diag-device-detail">'+esc(m.device_detail||'-')+'</div></div>';
    let dynamic=document.getElementById('diag_dynamic_'+m.addr);if(dynamic)dynamic.innerHTML=body;
  });
  diagApplyLedSnapshot();
}
function jbcUsbLinkStateName(v){
  return ({0:'USB DOWN',1:'DETECT',2:'P01 WAIT ACK',3:'P01 WAIT ADDR',4:'WAIT FW',5:'ACTIVE'})[Number(v)]||('STATE '+Number(v));
}
function jbcUsbProtocolName(v){return Number(v)===1?'P01':(Number(v)===2?'P02':'—')}
function diagJbcDelta(now,prev,key,sec){
  if(!prev||!(sec>0))return null;
  let v=(Number(now&&now[key]||0)-Number(prev&&prev[key]||0))/sec;
  return v<0?0:v;
}
function diagSyncTraceCursor(d,v){
  let newest=Number(d&&d.trace_newest_seq||0),oldest=Number(d&&d.trace_oldest_seq||0),epoch=Number(d&&d.started_ms||0);
  let stale=v!==diagTraceView||epoch!==diagTraceEpoch||diagTraceCursor>newest||(oldest>0&&diagTraceCursor>0&&(diagTraceCursor+1)<oldest);
  if(!stale)return;
  diagEventCache=[];
  diagTraceView=v;
  diagTraceEpoch=epoch;
  if(newest>0){let first=Math.max(oldest||1,newest-DIAG_EVENT_SOURCE_LIMIT+1);diagTraceCursor=Math.max(0,first-1)}else diagTraceCursor=0;
}
async function loadDiagEvents(){
  if(diagEventsBusy||diagControlPending)return;
  let v=document.getElementById('diag_view').value;
  if(v!==diagTraceView)return;
  const epoch=diagTraceEpoch;
  const after=diagTraceCursor;
  diagEventsBusy=true;
  try{
    let r=await fetch('/diagnostics/events?view='+encodeURIComponent(v)+'&after_seq='+encodeURIComponent(after)+'&limit='+DIAG_EVENT_BATCH,{cache:'no-store'});
    if(!r.ok)return;
    let d=await r.json();
    if(v!==diagTraceView||epoch!==diagTraceEpoch||Number(d.epoch||0)!==diagTraceEpoch)return;
    if(d.reset)diagEventCache=[];
    let incoming=Array.isArray(d.events)?d.events:[];
    if(incoming.length){
      let seen=new Set(diagEventCache.map(e=>Number(e.seq||0)));
      incoming.forEach(e=>{let seq=Number(e.seq||0);if(!seen.has(seq)){diagEventCache.push(e);seen.add(seq)}});
      if(diagEventCache.length>DIAG_EVENT_SOURCE_LIMIT)diagEventCache.splice(0,diagEventCache.length-DIAG_EVENT_SOURCE_LIMIT);
    }
    diagTraceCursor=Math.max(diagTraceCursor,Number(d.cursor_seq||diagTraceCursor));
    if(lastDiag&&String(lastDiag.view||'')===v){lastDiag.events=diagEventCache.slice();diagApplyFilter();}
  }catch(e){}finally{diagEventsBusy=false;}
}

function diagJbcDecodeDetail(j){
  let total=Number(j.decode_errors||0);if(!total)return '—';
  let cmd=Number(j.decode_last_cmd||0)&255,got=Number(j.decode_last_got_len||0)&255,emin=Number(j.decode_last_expected_min);let emax=Number(j.decode_last_expected_max);
  let exp=(emin===255||emax===255)?'shape':(emax===254?('>='+emin):(emin===emax?String(emin):(emin+'..'+emax)));
  let top=(j.decode_top||[]).filter(x=>Array.isArray(x)&&Number(x[1]||0)>0).map(x=>'0x'+Number(x[0]||0).toString(16).toUpperCase().padStart(2,'0')+':'+Number(x[1]||0)).join(' · ');
  return 'last 0x'+cmd.toString(16).toUpperCase().padStart(2,'0')+' exp '+exp+' got '+got+(top?' · top '+top:'');
}
function diagRenderLocalSummary(d,sec){
  let addr=parseInt(document.getElementById('diag_addr').value||'0',0);
  let m=(d.modules||[]).find(x=>Number(x.addr)===Number(addr));
  let el=document.getElementById('diag_local_summary');
  if(!m){el.className='diag-local-summary muted';el.textContent=u('Gerätebus: Zielmodul auswählen.','Device bus: select a target module.');return;}
  if(Number(m.type)!==9||!m.jbc_local){
    el.className='diag-local-summary muted';
    el.innerHTML='<b>'+addrText(m.addr)+' '+esc(m.name)+'</b> · '+esc(m.device_bus)+' · '+esc(m.device_detail||'-')+(m.local_trace?'':' · '+u('kein lokaler Trace gemeldet','no local trace capability reported'));
    return;
  }
  let j=m.jbc_local||{},pm=modulePrevByAddr(prevDiag,m.addr),pj=pm&&pm.jbc_local?pm.jbc_local:null;
  let rxf=diagJbcDelta(j,pj,'jbc_rx_frames',sec),txf=diagJbcDelta(j,pj,'jbc_tx_frames',sec);
  let rxb=diagJbcDelta(j,pj,'usb_rx_bytes',sec),txb=diagJbcDelta(j,pj,'usb_tx_bytes',sec);
  let errs=Number(j.usb_errors||0)+Number(j.bcc_errors||0)+Number(j.frame_errors||0)+Number(j.decode_errors||0)+Number(j.handshake_errors||0),cpErr=Number(j.cp_comm_errors||0),hold=Number(j.cp_hold_reasons||0);
  let proto=jbcUsbProtocolName(j.frame_protocol),link=jbcUsbLinkStateName(j.link_state),active=Number(j.link_state)===5;
  let age=Number(j.state_age_ms);let ageTxt=age>=0xFFFFFFFE?'—':(age<1000?age+' ms':(age/1000).toFixed(1)+' s');
  let qIn=Number(j.cp_in_queue||0),qOut=Number(j.cp_out_queue||0);
  let stat=(k,v,cls='')=>'<div class="diag-jbcusb-stat '+cls+'"><span class="k">'+esc(k)+'</span><span class="v">'+esc(v)+'</span></div>';
  el.className='diag-local-summary is-jbcusb';
  el.innerHTML='<div class="diag-jbcusb-head"><div class="diag-jbcusb-title"><strong>'+addrText(m.addr)+' '+esc(m.name)+'</strong><span class="diag-mini-badge">'+esc(String(j.model||'JBC'))+'</span><span class="diag-mini-badge">'+esc(proto)+'</span></div><span class="diag-status '+(active?'ok':'warn')+'">'+esc(link)+'</span></div>'+
    '<div class="diag-jbcusb-grid">'+
      stat('JBC RX / TX',rxf===null?'—':(rxf.toFixed(rxf<10?1:0)+' / '+txf.toFixed(txf<10?1:0)+' fps'),(!active||rxf===0)?'warn':'')+
      stat('USB RX / TX',rxb===null?'—':(fmtBytes(rxb)+' / '+fmtBytes(txb)))+
      stat('BCC / Frame / Decode / HS / USB',Number(j.bcc_errors||0)+' / '+Number(j.frame_errors||0)+' / '+Number(j.decode_errors||0)+' / '+Number(j.handshake_errors||0)+' / '+Number(j.usb_errors||0),errs?'warn':'')+
      stat('Decode detail',diagJbcDecodeDetail(j),Number(j.decode_errors||0)?'warn':'')+
      stat('CP210x Queue IN / OUT',qIn+' / '+qOut,(qIn||qOut)?'warn':'')+
      stat('CP210x Baud',j.cp_valid?String(Number(j.cp_baud||0)):'—',j.cp_valid?'':'warn')+
      stat('COMM errors','0x'+Number(cpErr).toString(16).toUpperCase(),cpErr?'err':'')+
      stat('Hold reasons','0x'+Number(hold).toString(16).toUpperCase(),hold?'warn':'')+
      stat(u('Letzter JBC-State','Last JBC state'),ageTxt,(age>3000||!active)?'warn':'')+
    '</div><div class="diag-jbcusb-note">'+u('Für den DDE-Freeze besonders beobachten: letzte gepaarte Transaktion, RX-fps, BCC/Frame-Fehler, CP210x-Queues, COMM/Hold und ob der Link ACTIVE bleibt.','For the DDE freeze watch the last paired transaction, RX fps, BCC/frame errors, CP210x queues, COMM/hold and whether the link remains ACTIVE.')+'</div>';
}

async function loadDiag(force=false,epochOverride=null){
  if(diagControlPending&&!force)return;
  const epoch=epochOverride===null?diagStateEpoch:epochOverride;
  let v=document.getElementById('diag_view').value;
  let r=await fetch('/diagnostics/state?view='+encodeURIComponent(v),{cache:'no-store'});
  let d=await r.json();
  if(epoch!==diagStateEpoch)return;
  diagSyncTraceCursor(d,v);
  d.events=diagEventCache.slice();
  let state=document.getElementById('diag_session_state');
  if(state){
    state.classList.toggle('running',!!d.active);
    state.classList.toggle('stopped',!d.active);
    state.innerHTML='<span class="diag-state-dot"></span>'+(d.active?u('läuft','running'):u('gestoppt','stopped'));
  }
  let sb=document.getElementById('diag_start_btn'),tb=document.getElementById('diag_stop_btn');
  if(sb)sb.disabled=!!d.active;
  if(tb)tb.disabled=!d.active;
  let sec=0;
  if(prevDiag&&Number(d.sample_ms)>=Number(prevDiag.sample_ms))sec=(Number(d.sample_ms)-Number(prevDiag.sample_ms))/1000;

  let txRate=rateDelta(d,prevDiag,'ofe_tx_wire_bytes',sec);
  let rxRate=rateDelta(d,prevDiag,'ofe_rx_wire_bytes',sec);
  let txFps=rateDelta(d,prevDiag,'ofe_tx_frames',sec);
  let rxFps=rateDelta(d,prevDiag,'ofe_rx_frames',sec);
  let busload=sec>0?((txRate+rxRate)*10*100/Number(d.ofe_baud||250000)):0;
  let transportErr=Number(d.ofe_crc_errors||0)+Number(d.ofe_bad_length||0)+Number(d.ofe_bad_version||0)+Number(d.ofe_escape_errors||0)+Number(d.ofe_overflow_errors||0)+Number(d.ofe_short_frames||0);
  let responseErr=Number(d.request_bad_seq_total||0)+Number(d.request_bad_cmd_total||0);
  let prevTransportErr=prevDiag?(Number(prevDiag.ofe_crc_errors||0)+Number(prevDiag.ofe_bad_length||0)+Number(prevDiag.ofe_bad_version||0)+Number(prevDiag.ofe_escape_errors||0)+Number(prevDiag.ofe_overflow_errors||0)+Number(prevDiag.ofe_short_frames||0)):transportErr;
  let prevResponseErr=prevDiag?(Number(prevDiag.request_bad_seq_total||0)+Number(prevDiag.request_bad_cmd_total||0)):responseErr;
  let liveErrors=(transportErr-prevTransportErr)+(responseErr-prevResponseErr);
  let healthy=liveErrors<=0;

  document.getElementById('diag_health').innerHTML='<span class="diag-status '+(healthy?'ok':'warn')+'">'+(healthy?'OK':u('Live-Fehler','live errors'))+'</span>';
  document.getElementById('diag_busload').textContent=sec>0?'≈ '+busload.toFixed(busload<10?2:1)+' %':'-';
  document.getElementById('diag_rate').textContent=sec>0?fmtBytes(txRate)+' / '+fmtBytes(rxRate):'-';
  document.getElementById('diag_fps').textContent=sec>0?fmtFps(txFps)+' / '+fmtFps(rxFps):'-';
  document.getElementById('diag_latency').textContent=d.avg_latency_ms+' ms / max '+d.max_latency_ms+' ms';
  document.getElementById('diag_transport_errors').textContent='CRC '+d.ofe_crc_errors+' · LEN '+d.ofe_bad_length+' · ESC '+d.ofe_escape_errors;
  document.getElementById('diag_bad').textContent='SEQ '+d.request_bad_seq_total+' · CMD '+d.request_bad_cmd_total;
  document.getElementById('diag_buffer').textContent=d.stored+' / dropped '+d.dropped+(d.psram?' / PSRAM':'');

  diagRenderModules(d,sec);
  if(d.active){
    let sel=document.getElementById('diag_addr');
    if(sel){
      let desired='0x'+Number(d.target_addr||0).toString(16).toUpperCase().padStart(2,'0');
      if(Number(d.target_addr)===0)desired='0';
      for(let i=0;i<sel.options.length;i++){if(sel.options[i].value.toUpperCase()===desired.toUpperCase()){sel.selectedIndex=i;break;}}
    }
  }
  diagRenderLocalSummary(d,sec);
  lastDiag=d;
  diagApplyFilter();
  prevDiag=d;
}
setInterval(loadDiag,1000);
setInterval(loadDiagEvents,250);
setInterval(loadDiagLedState,200);
diagView();
diagSetTraceLayout('pairs');
diagSetLocalLayout('transactions');
loadDiag();
loadDiagEvents();
loadDiagLedState();
</script>)JS";

  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
}
