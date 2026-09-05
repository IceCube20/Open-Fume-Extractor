#pragma once

// Master startup sequence. Keep ordering conservative: hardware, persisted state,
// web/MQTT tasks, initial scan, then route labels.
static void master_setup() {
  // setup() executes on Arduino's loop task. Keep its handle so auxiliary
  // tasks can yield priority to the real-time RS485 owner during module OTA.
  master_loop_task_handle = xTaskGetCurrentTaskHandle();
  ofe_keep_master_fw_signature();
  ofe_status_leds.begin();
  rs485_link.setActivityCallback([]() { ofe_status_leds.pulseBusActivity(); });
  ofe_status_leds.setBusOnline(false);
  Serial.begin(115200);
  delay(500);

  // Route large ordinary malloc/realloc allocations to PSRAM while keeping
  // smaller real-time/control allocations in internal DRAM.
  master_psram_total_at_boot = ESP.getPsramSize();
  if (psramFound() && master_psram_total_at_boot > 0) {
    heap_caps_malloc_extmem_enable(MASTER_EXTMEM_MALLOC_THRESHOLD);
    master_extmem_malloc_enabled = true;
    master_psram_free_after_policy = ESP.getFreePsram();

    Serial.print(F("[MEM] PSRAM malloc enabled >= "));
    Serial.print((unsigned long)MASTER_EXTMEM_MALLOC_THRESHOLD);
    Serial.print(F(" B | PSRAM "));
    Serial.print((unsigned long)(master_psram_total_at_boot / 1024UL));
    Serial.print(F(" KB total / "));
    Serial.print((unsigned long)(master_psram_free_after_policy / 1024UL));
    Serial.println(F(" KB free"));
  } else {
    master_extmem_malloc_enabled = false;
    master_psram_free_after_policy = 0;
    Serial.println(F("[MEM] PSRAM unavailable; using internal malloc policy"));
  }

  // ModuleRecord contains the Universal/Modbus descriptor cache. Allocate the
  // complete registry after PSRAM is ready instead of permanently consuming
  // tens of kilobytes of scarce internal DRAM.
  if (!registry.begin()) {
    Serial.println(F("[MEM] FATAL: module registry allocation failed"));
  } else {
    Serial.print(F("[MEM] Module registry "));
    Serial.print((unsigned long)(registry.storageBytes() / 1024UL));
    Serial.print(F(" KB in "));
    Serial.println(registry.usesPsram() ? F("PSRAM") : F("internal DRAM (fallback)"));
  }

#if WEB_ENABLE
  ui_config_load();
  if (developer_mode_enabled) heap_diag_enable();
#endif
  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  logic_fs_ready = LittleFS.begin(true);
  if (logic_fs_ready) {
    LittleFS.mkdir("/logic");
    logic_migrate_legacy_slots_to_fs();
  }
  logic_cache_reload_all();

  Serial.println();
  Serial.print(MASTER_FW_NAME);
  Serial.print(" ");
  Serial.print(MASTER_FW_VERSION);
  Serial.println(" by IceCube20");
  Serial.print("RS485 @ ");
  Serial.println(RS485_BAUD);
#if WEB_ENABLE
  Serial.println(F("[CLI] ready. Type 'help' for commands. Auto monitor is off."));
  serial_cli_prompt();
#endif

  scheduler.begin();
  master_command_queue_begin();
  module_update_io_init();
  scheduler.setServiceCallback([]() {
    ofe_status_leds.setFirmwareUpdate(scheduler.firmwareUpdateActive());
    ofe_status_leds.tick();
  });
  scheduler.setMasterInfo(MASTER_FW_MAJOR, MASTER_FW_MINOR, MASTER_FW_PATCH, MASTER_FW_NAME, MASTER_FW_SUFFIX);
#if WEB_ENABLE
  load_control_settings();
  const uint8_t preferred_output_addr = MasterSettingsStore::loadPreferredOutput(master_prefs);
  scheduler.setPreferredOutputAddr(preferred_output_addr);
  load_module_snapshot();
#endif
#if WEB_ENABLE
  // Bring the web interface up before the initial RS485 scan. The scan can take
  // a while when modules are missing, rebooting, or when Universal/Modbus
  // descriptor reads need retries. With the web task running first, the update
  // page and status endpoint become reachable immediately after WiFi is ready.
  web_begin();
  // Keep HTTP off core 0. Core 0 is the WiFi/lwIP core on ESP32-S3;
  // long module OTA requests there can starve IDLE0 and trip the task WDT.
  // Give the web task a slightly higher priority than the loop task so a
  // startup scan or a slow RS485 poll cannot make the UI feel dead.
  if (xTaskCreatePinnedToCore(web_service_task, "web-http", 8192, nullptr, 2, &web_service_task_handle, 1) != pdPASS) {
    // Keep the server usable even if a fragmented heap cannot provide the
    // auxiliary task stack. master_loop_tick() runs the same service inline
    // as a controlled fallback.
    web_service_task_handle = nullptr;
    Serial.println(F("[WEB] HTTP task allocation failed; using loop fallback"));
  }
  // MQTT is intentionally separate from HTTP. TLS handshakes, broker retries,
  // and Home Assistant discovery are synchronous in PubSubClient and must not
  // block web.handleClient(). Delay first MQTT connect a little so the page is
  // reachable directly after reboot.
  mqtt_next_connect_ms = millis() + 8000UL;
  xTaskCreatePinnedToCore(mqtt_service_task, "mqtt", 8192, nullptr, 1, nullptr, 1);
#endif
  scheduler.requestScanKnownModules(false, false);
#if WEB_ENABLE
  apply_module_labels();
  load_routing_config();
#endif
}
