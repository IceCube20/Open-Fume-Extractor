#pragma once

// WiFi startup, captive portal mode and HTTP route registration.
// Included from the master sketch so route lambdas can call sketch-local handlers.
#include "WebHomeAssistant.inc.h"

static void master_time_service_tick(uint32_t now) {
  const bool blocked=master_update_active || scheduler.firmwareUpdateActive() || master_display_ble.active() || hosted_c6_busy();
  if (!master_time_sync.poll(now,master_network_ready(),uint32_t(master_network_ip()),
                            master_ethernet_ready(),time(nullptr)>1700000000,blocked)) return;
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com", "time.google.com");
  Serial.printf("[Time] NTP start via %s; DNS %s / %s\n",master_network_transport(),
                master_network_dns(0).toString().c_str(),master_network_dns(1).toString().c_str());
}

static void wifi_start_setup_ap() {
  // A fallback portal must not permanently disable reconnection to saved WiFi.
  WiFi.mode(wifi_ssid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.setSleep(false);
  apply_ap_hostname();
  if (strlen(master_ap_password) < 8) generate_setup_password(master_ap_password, sizeof(master_ap_password));
  WiFi.softAP(master_ap_ssid, master_ap_password);
  dns.start(53, "*", WiFi.softAPIP());
  captive_active = true;
  wifi_sta_pending = false;
  wifi_sta_was_connected = false;
  Serial.print("Web hostname ");
  Serial.println(master_hostname);
  Serial.print("Web AP ");
  Serial.print(master_ap_ssid);
  Serial.print(" password ");
  Serial.print(master_ap_password);
  Serial.print(" IP ");
  Serial.println(WiFi.softAPIP());
  start_mdns_service();
}

static void wifi_start_sta_connect() {
  // Keep the recovery portal usable throughout background association attempts.
  WiFi.mode(captive_active ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  apply_sta_hostname();
  if (wifi_static_enabled) {
    WiFi.config(wifi_static_ip, wifi_static_gateway, wifi_static_subnet, wifi_static_dns1, wifi_static_dns2);
  }
  apply_sta_hostname();
  WiFi.begin(wifi_ssid, wifi_password);
  wifi_sta_pending = true;
  wifi_sta_start_ms = millis();
  wifi_next_retry_ms = wifi_sta_start_ms + 30000UL;
  Serial.print("Web hostname ");
  Serial.println(master_hostname);
  Serial.println("Web STA connecting...");
}

static void wifi_pause_connection_for_scan() {
  if (!wifi_sta_pending || WiFi.status()==WL_CONNECTED) return;
  WiFi.setAutoReconnect(false);
  const bool stopped=WiFi.disconnect(false,false);
  wifi_sta_pending=false;
  wifi_next_retry_ms=millis()+30000UL;
  Serial.printf("[WiFi scan] association paused; disconnect=%u\n",unsigned(stopped));
}

static void wifi_on_sta_connected() {
  if (captive_active) {
    dns.stop();
    WiFi.softAPdisconnect(true);
  }
  wifi_sta_pending = false;
  wifi_sta_was_connected = true;
  captive_active = false;
  Serial.print("Web STA IP ");
  Serial.println(WiFi.localIP());
  start_mdns_service();
  mqtt_next_connect_ms = millis() + 1000UL;
}

static void wifi_service_tick() {
  const uint32_t now = millis();
  master_ethernet_activity().tick(now);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  hosted_c6().tick(now,master_ethernet_ready());
  if (hosted_c6_radio_owned()) return;
  const bool scanning=WiFi.scanComplete()==WIFI_SCAN_RUNNING;
  hosted_c6().pollVersion(now,master_display_ble.active() || master_update_active || scheduler.firmwareUpdateActive() || scanning || wifi_sta_pending);
  const bool ethernet=master_ethernet_ready();
  if (ethernet && captive_active) { dns.stop(); captive_active=false; }
  const bool busy=hosted_c6_busy() || master_display_ble.active() || master_update_active || scheduler.firmwareUpdateActive();
  // The policy owns radio transitions. Avoid a hosted getMode RPC every HTTP tick.
  const auto action=master_lan_wifi_policy.tick(now,ethernet,busy,!master_lan_wifi_policy.suspended(),scanning);
  if (action==MasterLanWifiPolicy::SuspendWifi) {
    // Stop only WiFi; do not power down/reset the shared C6 or its BLE owner.
    WiFi.setAutoReconnect(false);
    if (WiFi.mode(WIFI_OFF)) {
      master_lan_wifi_policy.confirmSuspended();
      dns.stop(); captive_active=false; wifi_sta_pending=false; wifi_sta_was_connected=false;
      Serial.println("[Network] Ethernet ready; WiFi suspended");
    }
  } else if (action==MasterLanWifiPolicy::ResumeWifi) {
    wifi_sta_pending=false; wifi_sta_was_connected=false; wifi_next_retry_ms=now;
    if (!wifi_ssid[0]) wifi_start_setup_ap();
    Serial.println("[Network] Ethernet unavailable; restoring WiFi fallback");
  }
  if (master_network_changed()) {
    mqtt_network_reconnect_requested.store(true);
    if (master_network_ready()) {
    start_mdns_service();
    mqtt_next_connect_ms = now + 1000UL;
    Serial.printf("[Network] %s IP %s\n", master_network_transport(), master_network_ip().toString().c_str());
    }
  }
#endif
  master_time_service_tick(now);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (ethernet) return;
#endif
  if (!wifi_ssid[0]) return;
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifi_sta_was_connected || wifi_sta_pending || captive_active) wifi_on_sta_connected();
    return;
  }
  if (wifi_sta_was_connected) {
    wifi_sta_was_connected = false;
    wifi_next_retry_ms = now;
  }
  if (wifi_sta_pending) {
    if ((uint32_t)(now - wifi_sta_start_ms) >= 12000UL) {
      Serial.println("Web STA connect timeout, starting setup AP");
      wifi_start_setup_ap();
    }
    return;
  }
  if ((uint32_t)(now - wifi_next_retry_ms) < 0x80000000UL) {
    wifi_start_sta_connect();
  }
}

static void web_begin() {
  build_master_hostname();
  netcfg_load();
  if (!master_network_begin(master_hostname)) Serial.println("[Network] Hosted SDIO pin setup failed");
  web.on("/display-link", HTTP_GET, web_handle_display_link);
  web.on("/display-link/ble", HTTP_GET, web_handle_ble_pairing);
  web.on("/display-link/ble", HTTP_POST, web_handle_ble_pairing);
  web.on("/display-link", HTTP_POST, web_handle_display_link);
  // The ESP32 default modem-sleep can add very noticeable latency to small
  // HTTP requests. The master is mains-powered, so prefer deterministic web UI
  // response over WiFi power saving.
  WiFi.setSleep(false);
  if (strlen(wifi_ssid) > 0) wifi_start_sta_connect();
  else wifi_start_setup_ap();
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  web_usb_routes();
  hosted_c6().verifyBoot();
  web.on("/update/c6/status",HTTP_GET,[](){if(web_require_auth())web_handle_c6_status();});
  web.on("/update/c6/begin",HTTP_POST,[](){if(web_require_auth())web_handle_c6_begin();});
  web.on("/update/c6/chunk",HTTP_POST,[](){if(web_require_auth())web_handle_c6_chunk_done();},[](){if(web_require_auth())web_handle_c6_chunk_upload();});
  web.on("/update/c6/seal",HTTP_POST,[](){if(web_require_auth())web_handle_c6_seal();});
  web.on("/update/c6/cancel",HTTP_POST,[](){if(web_require_auth())web_handle_c6_cancel();});
  web.on("/update/c6/install",HTTP_POST,[](){if(web_require_auth())web_handle_c6_install();});
#endif
  master_display_wifi.begin(rs485_link, registry, [](ofe_wifi::Config& config) {
    if (!master_network_ready() || !wifi_ssid[0]) return;
    strlcpy(config.ssid, wifi_ssid, sizeof(config.ssid));
    strlcpy(config.password, wifi_password, sizeof(config.password));
    master_network_ip().toString().toCharArray(config.host, sizeof(config.host));
  });
  master_display_ble.configure(0,0,[](uint64_t uid, ofe_wifi::Config& config) {
    return master_display_wifi.pairingConfiguration(uid,config);
  });
  web.collectHeaders(WEB_COLLECT_HEADERS, sizeof(WEB_COLLECT_HEADERS) / sizeof(WEB_COLLECT_HEADERS[0]));
  web.on("/favicon.png", HTTP_GET, web_handle_logo);
  web.on("/favicon.ico", HTTP_GET, web_handle_logo);
  web.on("/logo.png", HTTP_GET, web_handle_logo);
  web.on("/", HTTP_GET, [](){ if (captive_active) web_redirect_config(); else if (web_require_auth(true)) web_handle_root(); });
  web.on("/state", HTTP_GET, [](){ if (web_require_auth()) web_handle_state(); });
  web.on("/api/ha/session", HTTP_GET, [](){ if (web_require_auth()) web_handle_ha_session(); });
  web.on("/api/ha/control", HTTP_POST, [](){ if (web_require_auth()) web_handle_ha_control(); });
  web.on("/descriptors", HTTP_GET, [](){ if (web_require_auth()) web_handle_descriptors(); });
  web.on("/led_state", HTTP_GET, [](){ if (web_require_auth()) web_handle_led_state(); });
  web.on("/developer/mode", HTTP_POST, [](){ if (web_require_auth()) web_handle_developer_mode(); });
  web.on("/scan", HTTP_POST, [](){ if (web_require_auth()) web_handle_scan(); });
  web.on("/diagnostics", HTTP_GET, [](){ if (web_require_auth()) web_handle_diagnostics(); });
  web.on("/diagnostics/state", HTTP_GET, [](){ if (web_require_auth()) web_handle_diagnostics_state(); });
  web.on("/diagnostics/events", HTTP_GET, [](){ if (web_require_auth()) web_handle_diagnostics_events(); });
  web.on("/diagnostics/control", HTTP_POST, [](){ if (web_require_auth()) web_handle_diagnostics_control(); });
  web.on("/module/label", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_label(); });
  web.on("/module/connection_warnings", HTTP_POST, [](){ if (web_require_auth()) web_handle_connection_warnings(); });
  web.on("/module/io_alias", HTTP_POST, [](){ if (web_require_auth()) web_handle_io_alias(); });
  web.on("/jbc-usb/station-name", HTTP_POST, [](){ if (web_require_auth()) web_handle_jbc_usb_station_name(); });
  web.on("/jbc-usb/config", HTTP_POST, [](){ if (web_require_auth()) web_handle_jbc_usb_config(); });
  web.on("/module/reboot", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_reboot(); });
  web.on("/jbc/settings", HTTP_POST, [](){ if (web_require_auth()) web_handle_jbc_settings(); });
  web.on("/io/set", HTTP_POST, [](){ if (web_require_auth()) web_handle_io_set(); });
  web.on("/output/module", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_output_set(); });
  web.on("/fanio/calibrate", HTTP_POST, [](){ if (web_require_auth()) web_handle_fanio_calibration(); });
  web.on("/weller/set", HTTP_POST, [](){ if (web_require_auth()) web_handle_weller_set(); });
  web.on("/display/set", HTTP_POST, [](){ if (web_require_auth()) web_handle_display_set(); });
  web.on("/universal/profile", HTTP_POST, [](){ if (web_require_auth()) web_handle_universal_profile(); });
  web.on("/universal/profile/read", HTTP_GET, [](){ if (web_require_auth()) web_handle_universal_profile_read(); });
  web.on("/universal/entity", HTTP_POST, [](){ if (web_require_auth()) web_handle_universal_entity(); });
  web.on("/module/io_config", HTTP_POST, [](){ if (web_require_auth()) web_handle_io_config(); });
  web.on("/output/select", HTTP_POST, [](){ if (web_require_auth()) web_handle_output_select(); });
  web.on("/routing/main", HTTP_POST, [](){ if (web_require_auth()) web_handle_main_input_select(); });
  web.on("/routing/set", HTTP_POST, [](){ if (web_require_auth()) web_handle_routing_set(); });
  web.on("/routing/rule", HTTP_POST, [](){ if (web_require_auth()) web_handle_routing_rule(); });
  web.on("/language/set", HTTP_POST, [](){ if (web_require_auth()) web_handle_language_set(); });
  web.on("/restart", HTTP_POST, [](){ if (web_require_auth()) web_handle_restart(); });
  web.on("/update", HTTP_GET, [](){ if (web_require_auth()) web_handle_update(); });
  web.on("/update/master/begin", HTTP_POST, [](){ if (web_require_auth()) web_handle_master_chunk_begin(); });
  web.on("/update/master/chunk", HTTP_POST, [](){ if (web_require_auth()) web_handle_master_chunk_done(); }, [](){ if (web_require_auth()) web_handle_master_chunk_upload(); });
  web.on("/update/master/end", HTTP_POST, [](){ if (web_require_auth()) web_handle_master_chunk_end(); });
  web.on("/update/master/abort", HTTP_POST, [](){ if (web_require_auth()) web_handle_master_chunk_abort(); });
  // Legacy one-shot multipart OTA is intentionally disabled. It cannot bind
  // the target type to the actual streamed BIN as strictly as the chunked path.
  web.on("/update/master", HTTP_POST, [](){
    if (web_require_auth()) web.send(410, "text/plain; charset=utf-8",
      "Legacy OTA endpoint disabled; use the firmware update page/chunked API");
  });
  web.on("/update/module/begin", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_chunk_begin(); });
  web.on("/update/module/chunk", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_chunk_done(); }, [](){ if (web_require_auth()) web_handle_module_chunk_upload(); });
  web.on("/update/module/end", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_chunk_end(); });
  web.on("/update/module/abort", HTTP_POST, [](){ if (web_require_auth()) web_handle_module_chunk_abort(); });
  web.on("/update/module/stats", HTTP_GET, [](){ if (web_require_auth()) web_handle_module_update_stats(); });
  web.on("/logic", HTTP_GET, [](){ if (web_require_auth()) web_handle_logic(); });
  web.on("/logic/json", HTTP_GET, [](){ if (web_require_auth()) web_handle_logic_json(); });
  web.on("/logic/runtime", HTTP_GET, [](){ if (web_require_auth()) web_handle_logic_runtime(); });
  web.on("/logic/json/save", HTTP_POST, [](){ if (web_require_auth()) web_handle_logic_json(); });
  web.on("/logic/list", HTTP_GET, [](){ if (web_require_auth()) web_handle_logic_list(); });
  web.on("/logic/select", HTTP_POST, [](){ if (web_require_auth()) web_handle_logic_select(); });
  web.on("/logic/new", HTTP_POST, [](){ if (web_require_auth()) web_handle_logic_new(); });
  web.on("/logic/delete", HTTP_POST, [](){ if (web_require_auth()) web_handle_logic_delete(); });
  web.on("/update/module", HTTP_POST, [](){
    if (web_require_auth()) web.send(410, "text/plain; charset=utf-8",
      "Legacy module OTA endpoint disabled; use the firmware update page/chunked API");
  });
  web.on("/config", HTTP_GET, [](){ if (web_require_config_auth()) web_handle_config(); });
  web.on("/config/wifi-scan", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_wifi_scan(); });
  web.on("/config/wifi-scan", HTTP_GET, [](){ if (web_require_config_auth()) web_handle_wifi_scan(); });
  web.on("/config/export", HTTP_GET, [](){ if (web_require_auth()) web_handle_config_export(); });
  web.on("/config/import", HTTP_POST, [](){ if (web_require_auth()) web_handle_config_import(); });
  web.on("/config/leds", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_config_leds(); });
  web.on("/config/power-save", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_config_power_save(); });
  web.on("/config/mqtt", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_config_mqtt(); });
  web.on("/config/web-login", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_config_web_login(); });
  web.on("/config/save", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_config_save(); });
  web.on("/config/password", HTTP_POST, [](){ if (web_require_config_auth()) web_handle_config_password(); });
  web.on("/generate_204", HTTP_GET, web_redirect_config);
  web.on("/gen_204", HTTP_GET, web_redirect_config);
  web.on("/hotspot-detect.html", HTTP_GET, web_redirect_config);
  web.on("/connecttest.txt", HTTP_GET, web_redirect_config);
  web.onNotFound([](){ if (captive_active) web_redirect_config(); else if (web_require_auth()) web_handle_not_found(); });
  web.begin();
}
