#pragma once

// Serial CLI implementation. Included from the master sketch so it can keep
// using the sketch-local static objects while the main file stays readable.
static char serial_cli_line[96] = {0};
static uint8_t serial_cli_len = 0;
static uint32_t serial_cli_last_char_ms = 0;
static bool serial_cli_monitor_enabled = false;

static void serial_cli_prompt() {
  Serial.print(F("ofe> "));
}

static String yes_no(bool v) {
  return v ? F("yes") : F("no");
}

static void serial_cli_print_help() {
  Serial.println();
  Serial.println(F("Open Fume Extractor CLI"));
  Serial.println(F("======================"));
  Serial.println(F("help                 Show this help"));
  Serial.println(F("status               Show compact master status"));
  Serial.println(F("modules              List discovered modules"));
  Serial.println(F("module <addr>        Show one module, e.g. module 0x50"));
  Serial.println(F("heap                 Show heap diagnostic snapshot"));
  Serial.println(F("log [normal|debug]   Show/set serial log level"));
  Serial.println(F("routes               Show main input/output routing"));
  Serial.println(F("bus                  Show RS485 summary"));
  Serial.println(F("network              Show WiFi/Web/mDNS settings"));
  Serial.println(F("mqtt                 Show MQTT settings/status"));
  Serial.println(F("scan                 Scan known module addresses"));
  Serial.println(F("monitor on|off       Toggle 10s live status output"));
  Serial.println(F("webauth reset        Reset web login to compiled defaults"));
  Serial.println(F("network reset        Clear WiFi/Web/MQTT settings and reboot"));
  Serial.println(F("restart              Reboot master"));
  Serial.println();
}

static void serial_cli_print_status() {
  const JbcModuleState& js = extractor.jbcState();
  const uint16_t power_pct = (extractor.outputPower() + 5U) / 10U;
  Serial.println();
  Serial.println(F("[Master]"));
  Serial.print(F("FW        : ")); Serial.print(MASTER_FW_VERSION); Serial.println(F(" by IceCube20"));
  Serial.print(F("IP        : ")); Serial.println(current_master_ip_string());
  Serial.print(F("WiFi      : ")); Serial.print(WiFi.status() == WL_CONNECTED ? F("connected") : (captive_active ? F("setup AP") : F("offline")));
  if (WiFi.status() == WL_CONNECTED) { Serial.print(F(" RSSI ")); Serial.print(WiFi.RSSI()); Serial.print(F(" dBm")); }
  Serial.println();
  Serial.print(F("Modules   : ")); Serial.print(registry.count()); Serial.println();
  Serial.print(F("Output    : ")); Serial.print(extractor.outputEnabled() ? F("on") : F("off")); Serial.print(F("  power ")); Serial.print(power_pct); Serial.println(F("%"));
  Serial.print(F("Work mask : 0x")); Serial.println(extractor.workMask(), HEX);
  Serial.print(F("Afterrun  : ")); Serial.print((extractor.afterrunLeftMs() + 999UL) / 1000UL); Serial.println(F(" s"));
  const ModuleRecord* active_jbc = registry.find(scheduler.jbcAddr());
  if (active_jbc && (active_jbc->caps & CAP_JBC_USB)) {
    const JbcUsbCoreState jbc = jbc_usb_core_state(*active_jbc);
    Serial.print(F("JBC       : station=")); Serial.print(jbc.linked ? F("yes") : F("no"));
    Serial.print(F(" ports="));
    if (jbc.linked || jbc.port_count) Serial.println(jbc.port_count);
    else Serial.println(F("-"));
  } else {
    Serial.print(F("JBC       : addr=0x")); if (js.jbc_addr < 0x10) Serial.print('0'); Serial.print(js.jbc_addr, HEX); Serial.print(F(" station=0x")); if (js.station_addr < 0x10) Serial.print('0'); Serial.print(js.station_addr, HEX); Serial.print(F(" err=0x")); Serial.println(js.stat_error, HEX);
  }
  Serial.print(F("Heap      : ")); Serial.print(ESP.getFreeHeap() / 1024); Serial.print(F(" KB free, min ")); Serial.print(ESP.getMinFreeHeap() / 1024); Serial.println(F(" KB"));
  Serial.print(F("CPU/Loop  : ")); Serial.print(cpu_load_pct); Serial.print(F("% / ")); Serial.print(loop_max_ms); Serial.println(F(" ms"));
  Serial.println();
}

static void serial_cli_print_modules() {
  Serial.println();
  Serial.println(F("Addr  Online  Type              Name                    FW            Heap   Miss/Now  Quality"));
  Serial.println(F("----  ------  ----------------  ----------------------  ------------  -----  --------  -------"));
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    Serial.print(F("0x")); if (m.addr < 0x10) Serial.print('0'); Serial.print(m.addr, HEX); Serial.print(F("  "));
    Serial.print(m.online ? F("yes   ") : F("no    ")); Serial.print(F("  "));
    String type = module_type_name_for(m); while (type.length() < 16) type += ' '; Serial.print(type); Serial.print(F("  "));
    String name = module_display_name(m); if (name.length() > 22) name = name.substring(0, 22); while (name.length() < 22) name += ' '; Serial.print(name); Serial.print(F("  "));
    String fw = String(m.fw_major) + F(".") + String(m.fw_minor) + F(".") + String(m.fw_patch) + String(m.fw_suffix); while (fw.length() < 12) fw += ' '; Serial.print(fw); Serial.print(F("  "));
    if (m.telemetry_valid) { Serial.print(m.module_heap_free / 1024); Serial.print(F("KB")); } else Serial.print(F("-"));
    Serial.print(F("  "));
    Serial.print(m.miss_count); Serial.print('/'); Serial.print(m.consecutive_timeouts); Serial.print(F("      "));
    Serial.println(module_comm_quality_text(m));
  }
  Serial.println();
}

static void serial_cli_print_module(uint8_t addr) {
  const ModuleRecord* m = registry.find(addr);
  if (!m) {
    Serial.print(F("No module at 0x")); if (addr < 0x10) Serial.print('0'); Serial.println(addr, HEX);
    return;
  }
  Serial.println();
  Serial.print(F("Module 0x")); if (m->addr < 0x10) Serial.print('0'); Serial.println(m->addr, HEX);
  Serial.print(F("Type      : ")); Serial.println(module_type_name_for(*m));
  Serial.print(F("Name      : ")); Serial.println(module_display_name(*m));
  Serial.print(F("Online    : ")); Serial.println(m->online ? F("yes") : F("no"));
  Serial.print(F("FW        : ")); Serial.print(m->fw_major); Serial.print('.'); Serial.print(m->fw_minor); Serial.print('.'); Serial.print(m->fw_patch); Serial.println(m->fw_suffix);
  Serial.print(F("UID       : ")); Serial.println(uid_hex(m->uid));
  Serial.print(F("Caps      : 0x")); Serial.println(m->caps, HEX);
  Serial.print(F("Roles     : jbc=")); Serial.print(yes_no(m->role_jbc)); Serial.print(F(" output=")); Serial.println(yes_no(m->role_output));
  Serial.print(F("Heap      : ")); if (m->telemetry_valid) { Serial.print(m->module_heap_free / 1024); Serial.println(F(" KB")); } else Serial.println(F("-"));
  Serial.print(F("CPU/Loop  : ")); if (m->telemetry_valid) { Serial.print(m->module_cpu_load_pct); Serial.print(F("% / ")); Serial.print(m->module_loop_max_ms); Serial.println(F(" ms")); } else Serial.println(F("-"));
  Serial.print(F("Uptime    : ")); if (m->telemetry_valid) Serial.println(duration_text_seconds(m->module_uptime_s)); else Serial.println(F("-"));
  Serial.print(F("Miss      : ")); Serial.print(m->miss_count); Serial.print('/'); Serial.println(m->consecutive_timeouts);
  Serial.print(F("Timeouts  : ")); Serial.println(m->timeout_count);
  Serial.print(F("Quality   : ")); Serial.println(module_comm_quality_text(*m));
  if (m->output_status_valid || m->role_output) {
    Serial.print(F("Output    : ")); Serial.print(m->output_enabled ? F("on") : F("off")); Serial.print(F(" power=")); Serial.print((m->output_power + 5U) / 10U); Serial.print(F("% rpm=")); Serial.print(m->output_rpm); Serial.print(F(" fault=0x")); Serial.println(m->output_fault_mask, HEX);
  }
  if (m->universal_descriptor_valid) {
    Serial.print(F("Descriptor: crc=0x")); Serial.print(m->universal_descriptor_crc, HEX); Serial.print(F(" entities=")); Serial.println(m->universal_entity_count);
  }
  if (m->caps & CAP_JBC_USB) {
    const JbcUsbCoreState jbc = jbc_usb_core_state(*m);
    Serial.print(F("Station   : ")); Serial.println(jbc.linked ? F("yes") : F("no"));
    Serial.print(F("Ports     : "));
    if (jbc.linked || jbc.port_count) Serial.println(jbc.port_count);
    else Serial.println(F("-"));
  }
  Serial.println();
}

static void serial_cli_print_heap() {
  heap_diag_sample("cli_heap");
  heap_diag_refresh_snapshot();
  const HeapDiagSnapshot& h = heap_diag_current_snapshot;
  Serial.println();
  Serial.println(F("[Heap diagnostics]"));
  Serial.print(F("Developer : ")); Serial.println(h.active ? F("on") : F("off"));
  Serial.print(F("Now       : free=")); Serial.print(h.free_now / 1024UL); Serial.print(F(" KB internal=")); Serial.print(h.internal_now / 1024UL); Serial.print(F(" KB largest=")); Serial.print(h.largest_now / 1024UL); Serial.print(F(" KB psram=")); Serial.print(h.psram_now / 1024UL); Serial.println(F(" KB"));
  if (h.active) {
    Serial.print(F("Free low  : ")); Serial.print(h.low_free / 1024UL); Serial.print(F(" KB @ ")); Serial.print(h.low_label); Serial.print(F(" [")); Serial.print(h.low_task); Serial.print(F("] ")); Serial.println(h.low_context);
    Serial.print(F("Block low : ")); Serial.print(h.block_largest / 1024UL); Serial.print(F(" KB @ ")); Serial.print(h.block_label); Serial.print(F(" [")); Serial.print(h.block_task); Serial.print(F("] ")); Serial.println(h.block_context);
  }
  Serial.println();
}

static void serial_cli_print_log_mode() {
  Serial.print(F("Log mode  : "));
  Serial.println(scheduler.serialDebugLog() ? F("debug") : F("normal"));
}

static void serial_cli_set_log_mode(bool debug) {
  scheduler.setSerialDebugLog(debug);
  heap_diag_set_serial_debug(debug);
  Serial.print(F("Log mode set to "));
  Serial.println(debug ? F("debug") : F("normal"));
}

static void serial_cli_print_routes() {
  Serial.println();
  Serial.println(F("[Routing]"));
  Serial.print(F("Main input  : type=")); Serial.print(scheduler.mainInputSourceType()); Serial.print(F(" addr=0x")); if (scheduler.mainInputSourceAddr() < 0x10) Serial.print('0'); Serial.print(scheduler.mainInputSourceAddr(), HEX); Serial.print(F(" bit=")); Serial.println(scheduler.mainInputSourceBit());
  Serial.print(F("Main output : active=0x")); if (scheduler.outputAddr() < 0x10) Serial.print('0'); Serial.print(scheduler.outputAddr(), HEX); Serial.print(F(" preferred=0x")); if (scheduler.preferredOutputAddr() < 0x10) Serial.print('0'); Serial.println(scheduler.preferredOutputAddr(), HEX);
  const JbcModuleState& js = scheduler.controlSettings();
  Serial.print(F("Suction     : level=")); Serial.print(js.suction_level); Serial.print(F(" select=")); Serial.print(js.select_flow / 10); Serial.print(F("% delay_work=")); Serial.print(js.delay_work_sec); Serial.print(F("s delay_stand=")); Serial.print(js.delay_stand_sec); Serial.print(F("s continuous=")); Serial.println(js.continuous ? F("yes") : F("no"));
  Serial.println();
}

static void serial_cli_print_bus() {
  Serial.println();
  Serial.println(F("[RS485]"));
  Serial.print(F("Baud      : ")); Serial.println(RS485_BAUD);
  Serial.print(F("Modules   : ")); Serial.println(registry.count());
  uint32_t miss = 0, timeouts = 0, crc = 0;
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    miss += m.miss_count;
    timeouts += m.timeout_count;
    crc += m.crc_error_count;
  }
  Serial.print(F("Miss total: ")); Serial.println(miss);
  Serial.print(F("Timeouts  : ")); Serial.println(timeouts);
  Serial.print(F("CRC errors: ")); Serial.println(crc);
  Serial.println();
}

static void serial_cli_print_mqtt() {
  Serial.println();
  Serial.println(F("[MQTT]"));
  Serial.print(F("Enabled   : ")); Serial.println(mqtt_enabled ? F("yes") : F("no"));
  Serial.print(F("State     : ")); Serial.println(mqtt_last_state);
  Serial.print(F("TLS       : ")); Serial.println(mqtt_tls_enabled ? (mqtt_ca_cert.length() ? F("verified") : F("insecure")) : F("off"));
  Serial.print(F("Host      : ")); Serial.print(strlen(mqtt_host) ? mqtt_host : "-"); Serial.print(':'); Serial.println(mqtt_port);
  Serial.print(F("Base topic: ")); Serial.println(strlen(mqtt_base_topic) ? mqtt_base_topic : "-");
  Serial.print(F("Discovery : ")); Serial.println(mqtt_ha_discovery ? F("on") : F("off"));
  Serial.println();
}

static void serial_cli_print_network() {
  Serial.println();
  Serial.println(F("[Network]"));
  Serial.print(F("Hostname  : ")); Serial.println(master_hostname);
  Serial.print(F("Mode      : ")); Serial.println(wifi_static_enabled ? F("static") : F("DHCP"));
  Serial.print(F("SSID      : ")); Serial.println(strlen(wifi_ssid) ? wifi_ssid : "-");
  Serial.print(F("Web user  : ")); Serial.println(strlen(web_auth_user) ? web_auth_user : "-");
  Serial.print(F("WiFi      : ")); Serial.println(WiFi.status() == WL_CONNECTED ? F("connected") : (captive_active ? F("setup AP") : F("offline")));
  Serial.print(F("IP        : ")); Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP());
  Serial.print(F("mDNS      : http://")); Serial.print(master_hostname); Serial.println(F(".local/"));
  if (wifi_static_enabled) {
    Serial.print(F("Gateway   : ")); Serial.println(wifi_static_gateway);
    Serial.print(F("Subnet    : ")); Serial.println(wifi_static_subnet);
    Serial.print(F("DNS1      : ")); Serial.println(wifi_static_dns1);
    Serial.print(F("DNS2      : ")); Serial.println(wifi_static_dns2);
  }
  Serial.println();
}

static bool parse_cli_addr(const String& text, uint8_t& addr) {
  String s = text;
  s.trim();
  if (!s.length()) return false;
  char* endp = nullptr;
  unsigned long v = 0;
  if (s.startsWith("0x") || s.startsWith("0X")) v = strtoul(s.c_str() + 2, &endp, 16);
  else v = strtoul(s.c_str(), &endp, 0);
  if (!endp || *endp != 0 || v > 255UL) return false;
  addr = (uint8_t)v;
  return true;
}

static void serial_cli_execute(String command) {
  command.trim();
  String lower = command;
  lower.toLowerCase();
  if (!lower.length()) return;

  if (lower == "help" || lower == "?") {
    serial_cli_print_help();
  } else if (lower == "status") {
    serial_cli_print_status();
  } else if (lower == "modules" || lower == "mods") {
    serial_cli_print_modules();
  } else if (lower.startsWith("module ") || lower.startsWith("mod ")) {
    int sp = lower.indexOf(' ');
    uint8_t addr = 0;
    if (sp >= 0 && parse_cli_addr(command.substring(sp + 1), addr)) serial_cli_print_module(addr);
    else Serial.println(F("Usage: module 0x50"));
  } else if (lower == "heap" || lower == "heap status") {
    serial_cli_print_heap();
  } else if (lower == "log") {
    serial_cli_print_log_mode();
  } else if (lower == "log normal") {
    serial_cli_set_log_mode(false);
  } else if (lower == "log debug") {
    serial_cli_set_log_mode(true);
  } else if (lower == "routes" || lower == "routing") {
    serial_cli_print_routes();
  } else if (lower == "bus" || lower == "rs485") {
    serial_cli_print_bus();
  } else if (lower == "mqtt") {
    serial_cli_print_mqtt();
  } else if (lower == "wifi status" || lower == "network status" || lower == "network" || lower == "wifi") {
    serial_cli_print_network();
  } else if (lower == "scan") {
    Serial.println(F("Module scan scheduled."));
    scheduler.requestScanKnownModules(false);
  } else if (lower == "monitor on") {
    serial_cli_monitor_enabled = true;
    Serial.println(F("Monitor enabled. Status prints every 10 seconds."));
  } else if (lower == "monitor off") {
    serial_cli_monitor_enabled = false;
    Serial.println(F("Monitor disabled."));
  } else if (lower == "wifi reset" || lower == "network reset") {
    Serial.println(F("Clearing WLAN credentials, hostname, static IP, web auth and MQTT settings..."));
    netcfg_reset();
    Serial.println(F("Network settings cleared. Restarting in setup AP mode."));
    Serial.flush();
    // Network reset must not publish through a potentially stalled MQTT socket.
    delay(100);
    ESP.restart();
  } else if (lower == "webauth reset" || lower == "web auth reset" || lower == "auth reset" || lower == "webauth clear" || lower == "web auth clear" || lower == "auth clear") {
    Serial.println(F("Clearing custom web login. Falling back to compiled defaults."));
    webauth_reset();
    Serial.print(F("Web login reset to user '"));
    Serial.print(web_auth_user);
    Serial.println(F("'. Restarting..."));
    Serial.flush();
    prepare_controlled_restart();
    delay(250);
    ESP.restart();
  } else if (lower == "restart" || lower == "reboot") {
    Serial.println(F("Restarting..."));
    Serial.flush();
    prepare_controlled_restart();
    delay(100);
    ESP.restart();
  } else {
    Serial.print(F("Unknown command: ")); Serial.println(command);
    Serial.println(F("Type 'help' for available commands."));
  }
}

static void serial_cli_tick() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (serial_cli_len) {
        Serial.println();
        serial_cli_line[serial_cli_len] = 0;
        serial_cli_execute(String(serial_cli_line));
        serial_cli_len = 0;
        serial_cli_prompt();
      }
    } else if ((c == '\b' || c == 0x7F) && serial_cli_len) {
      serial_cli_len--;
      Serial.print(F("\b \b"));
    } else if (c >= 32 && c < 127 && serial_cli_len < sizeof(serial_cli_line) - 1) {
      serial_cli_line[serial_cli_len++] = c;
      serial_cli_last_char_ms = millis();
      Serial.print(c);
    }
  }
  // Also support Arduino Serial Monitor with "No line ending" without
  // splitting normally typed commands into individual characters.
  if (serial_cli_len && (uint32_t)(millis() - serial_cli_last_char_ms) >= 1200UL) {
    Serial.println();
    serial_cli_line[serial_cli_len] = 0;
    serial_cli_execute(String(serial_cli_line));
    serial_cli_len = 0;
    serial_cli_prompt();
  }
}

static void serial_cli_task(void* parameter) {
  (void)parameter;
  for (;;) {
    serial_cli_tick();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void web_service_task(void* parameter) {
  (void)parameter;
  UBaseType_t applied_priority = uxTaskPriorityGet(nullptr);
  for (;;) {
    // HTTP normally runs above the Arduino loop so a slow RS485 scan cannot
    // freeze the UI. During RS485 module OTA that ordering is harmful: an
    // 8 KiB multipart POST can keep the higher-priority HTTP task runnable for
    // hundreds of milliseconds while module_update_pump() lives in loopTask.
    // Match HTTP to the actual loop-task priority only for the OTA session.
    // Equal-priority time slicing prevents HTTP from monopolizing core 1 while
    // still giving the producer enough CPU to keep the 24 KiB queue filled.
    UBaseType_t wanted_priority = 2;
    if (module_update_addr && master_loop_task_handle) {
      const UBaseType_t loop_priority = uxTaskPriorityGet(master_loop_task_handle);
      wanted_priority = loop_priority;
    }
    if (wanted_priority != applied_priority) {
      vTaskPrioritySet(nullptr, wanted_priority);
      applied_priority = wanted_priority;
    }

    wifi_service_tick();
    if (captive_active) dns.processNextRequest();
    web.handleClient();
    // Keep the HTTP server independent from MQTT. PubSubClient::connect(),
    // TLS handshakes, and retained Home-Assistant discovery publishes are
    // synchronous and can otherwise make the web UI feel frozen.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
