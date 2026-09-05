#pragma once

// Network, WiFi, MQTT and web-auth persistence helpers.
static void generate_setup_password(char* out, size_t out_len) {
  if (!out || out_len < 21) return;
  snprintf(out, out_len, "OFE-%08lX%08lX", (unsigned long)esp_random(), (unsigned long)esp_random());
}

static uint8_t status_led_raw_brightness() {
  if (!status_led_enabled) return 0;
  const uint8_t pct = constrain(status_led_brightness_pct, (uint8_t)10, (uint8_t)100);
  return (uint8_t)((uint16_t)pct * 255U / 100U);
}

static void apply_status_led_config() {
  status_led_brightness_pct = constrain(status_led_brightness_pct, (uint8_t)10, (uint8_t)100);
  ofe_status_leds.setBrightness(status_led_raw_brightness());
  master_cmd_set_led_config(status_led_enabled, status_led_brightness_pct);
}

static bool save_status_led_config(bool enabled, uint8_t brightness_pct) {
  Preferences led_prefs;
  if (!led_prefs.begin(MasterSettingsStore::NS_NET, false)) return false;
  status_led_enabled = enabled;
  status_led_brightness_pct = constrain(brightness_pct, (uint8_t)10, (uint8_t)100);
  bool ok = true;
  ok = led_prefs.putBool(MasterSettingsStore::KEY_LED_ENABLED, status_led_enabled) && ok;
  ok = led_prefs.putUChar(MasterSettingsStore::KEY_LED_BRIGHTNESS, status_led_brightness_pct) && ok;
  led_prefs.end();
  if (ok) apply_status_led_config();
  return ok;
}

static void netcfg_load() {
  MqttConfigGuard guard;
  if (!guard.locked() || !net_prefs.begin(MasterSettingsStore::NS_NET, false)) {
    snprintf(master_bootstrap_password, sizeof(master_bootstrap_password), "%s", MASTER_DEFAULT_PASSWORD);
    snprintf(master_ap_password, sizeof(master_ap_password), "%s", MASTER_AP_PASSWORD);
    snprintf(web_auth_password, sizeof(web_auth_password), "%s", master_bootstrap_password);
    web_password_change_required = strcmp(web_auth_password, MASTER_DEFAULT_PASSWORD) == 0;
    Serial.println(F("[NVS] network settings unavailable; using bootstrap defaults"));
    return;
  }
  String ssid = net_prefs.getString(MasterSettingsStore::KEY_WIFI_SSID, String(WIFI_SSID));
  String pass = net_prefs.getString(MasterSettingsStore::KEY_WIFI_PASS, String(WIFI_PASSWORD));
  String host = normalized_hostname(net_prefs.getString(MasterSettingsStore::KEY_HOSTNAME, String(master_hostname)));
  String web_user = net_prefs.getString(MasterSettingsStore::KEY_WEB_USER, String(WEB_AUTH_USER));
  String web_pass = net_prefs.getString(MasterSettingsStore::KEY_WEB_PASS, "");
  String ap_pass = net_prefs.getString(MasterSettingsStore::KEY_AP_PASS, "");
  web_user.trim();
  if (!web_user.length()) web_user = WEB_AUTH_USER;
  bool generated_web_password = !web_pass.length();
  if (generated_web_password) {
    snprintf(master_bootstrap_password, sizeof(master_bootstrap_password), "%s", MASTER_DEFAULT_PASSWORD);
    web_pass = master_bootstrap_password;
    net_prefs.putString(MasterSettingsStore::KEY_WEB_PASS, web_pass);
  }
  if (ap_pass.length() < 8 || ap_pass.length() > 23) {
    ap_pass = MASTER_AP_PASSWORD;
    net_prefs.putString(MasterSettingsStore::KEY_AP_PASS, ap_pass);
  }
  ap_pass.toCharArray(master_ap_password, sizeof(master_ap_password));
  if (host == "open-fume-extractor") host = master_device_id;
  if (host.length()) host.toCharArray(master_hostname, sizeof(master_hostname));
  ssid.toCharArray(wifi_ssid, sizeof(wifi_ssid));
  pass.toCharArray(wifi_password, sizeof(wifi_password));
  web_user.toCharArray(web_auth_user, sizeof(web_auth_user));
  web_pass.toCharArray(web_auth_password, sizeof(web_auth_password));
  web_password_change_required = web_pass == MASTER_DEFAULT_PASSWORD;

  wifi_static_enabled = net_prefs.getBool(MasterSettingsStore::KEY_STATIC_IP, false);
  bool valid = parse_ipv4(net_prefs.getString(MasterSettingsStore::KEY_IP, ""), wifi_static_ip);
  valid = parse_ipv4(net_prefs.getString(MasterSettingsStore::KEY_GATEWAY, ""), wifi_static_gateway) && valid;
  valid = parse_ipv4(net_prefs.getString(MasterSettingsStore::KEY_SUBNET, "255.255.255.0"), wifi_static_subnet) && valid;
  if (!parse_ipv4(net_prefs.getString(MasterSettingsStore::KEY_DNS1, ""), wifi_static_dns1)) wifi_static_dns1 = wifi_static_gateway;
  if (!parse_ipv4(net_prefs.getString(MasterSettingsStore::KEY_DNS2, ""), wifi_static_dns2)) wifi_static_dns2 = IPAddress(0, 0, 0, 0);
  if (!valid) wifi_static_enabled = false;

  mqtt_enabled = net_prefs.getBool(MasterSettingsStore::KEY_MQTT_ENABLED, false);
  mqtt_tls_enabled = net_prefs.getBool(MasterSettingsStore::KEY_MQTT_TLS, false);
  mqtt_ha_discovery = net_prefs.getBool(MasterSettingsStore::KEY_MQTT_HA, true);
  net_prefs.getString(MasterSettingsStore::KEY_MQTT_HOST, "").toCharArray(mqtt_host, sizeof(mqtt_host));
  mqtt_port = net_prefs.getUShort(MasterSettingsStore::KEY_MQTT_PORT, mqtt_tls_enabled ? 8883 : 1883);
  net_prefs.getString(MasterSettingsStore::KEY_MQTT_USER, "").toCharArray(mqtt_user, sizeof(mqtt_user));
  net_prefs.getString(MasterSettingsStore::KEY_MQTT_PASS, "").toCharArray(mqtt_password, sizeof(mqtt_password));
  net_prefs.getString(MasterSettingsStore::KEY_MQTT_TOPIC, "open-fume-extractor").toCharArray(mqtt_base_topic, sizeof(mqtt_base_topic));
  net_prefs.getString(MasterSettingsStore::KEY_MQTT_DISC, "homeassistant").toCharArray(mqtt_discovery_prefix, sizeof(mqtt_discovery_prefix));
  status_led_enabled = net_prefs.getBool(MasterSettingsStore::KEY_LED_ENABLED, true);
  status_led_brightness_pct = (uint8_t)net_prefs.getUChar(MasterSettingsStore::KEY_LED_BRIGHTNESS, 20);
  status_led_brightness_pct = constrain(status_led_brightness_pct, (uint8_t)10, (uint8_t)100);
  mqtt_ca_cert = net_prefs.getString(MasterSettingsStore::KEY_MQTT_CA, "");
  mqtt_ca_cert.replace("\r\n", "\n");
  mqtt_ca_cert.trim();
  mqtt_tls_verify_enabled = false;
  net_prefs.end();
  if (generated_web_password) {
    Serial.print(F("[AUTH] initial web user "));
    Serial.print(web_auth_user);
    Serial.print(F(" password "));
    Serial.println(master_bootstrap_password);
  }
  apply_status_led_config();
}

static bool netcfg_save(const String& ssid, const String& pass, const String& hostname,
                        const String& web_user_arg, const String& web_pass_arg,
                        bool use_static, const IPAddress& ip, const IPAddress& gateway,
                        const IPAddress& subnet, const IPAddress& dns1, const IPAddress& dns2,
                         bool mqtt_en, bool mqtt_tls, const String& mqtt_host_arg, uint16_t mqtt_port_arg,
                         const String& mqtt_user_arg, const String& mqtt_pass_arg, const String& mqtt_topic_arg,
                         bool mqtt_ha, const String& mqtt_disc_arg, const String& mqtt_ca_arg,
                         bool led_en, uint8_t led_pct) {
  // Keep configuration writes independent from MQTT socket activity. The web
  // setup page must be able to save even while a broker connection is failing.
  Preferences save_prefs;
  if (!save_prefs.begin(MasterSettingsStore::NS_NET, false)) return false;
  bool ok = true;
#define OFE_NET_PUT(expr) do { if (!(expr)) ok = false; } while (0)
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_WIFI_SSID, ssid));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_WIFI_PASS, pass));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_HOSTNAME, hostname));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_WEB_USER, web_user_arg));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_WEB_PASS, web_pass_arg));
  OFE_NET_PUT(save_prefs.putBool(MasterSettingsStore::KEY_STATIC_IP, use_static));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_IP, ip.toString()));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_GATEWAY, gateway.toString()));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_SUBNET, subnet.toString()));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_DNS1, dns1.toString()));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_DNS2, dns2.toString()));
  OFE_NET_PUT(save_prefs.putBool(MasterSettingsStore::KEY_MQTT_ENABLED, mqtt_en));
  OFE_NET_PUT(save_prefs.putBool(MasterSettingsStore::KEY_MQTT_TLS, mqtt_tls));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_MQTT_HOST, mqtt_host_arg));
  OFE_NET_PUT(save_prefs.putUShort(MasterSettingsStore::KEY_MQTT_PORT, mqtt_port_arg));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_MQTT_USER, mqtt_user_arg));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_MQTT_PASS, mqtt_pass_arg));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_MQTT_TOPIC, mqtt_topic_arg));
  OFE_NET_PUT(save_prefs.putBool(MasterSettingsStore::KEY_MQTT_HA, mqtt_ha));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_MQTT_DISC, mqtt_disc_arg));
  OFE_NET_PUT(save_prefs.putString(MasterSettingsStore::KEY_MQTT_CA, mqtt_ca_arg));
  OFE_NET_PUT(save_prefs.putBool(MasterSettingsStore::KEY_LED_ENABLED, led_en));
  OFE_NET_PUT(save_prefs.putUChar(MasterSettingsStore::KEY_LED_BRIGHTNESS, constrain(led_pct, (uint8_t)10, (uint8_t)100)));
#undef OFE_NET_PUT
  save_prefs.end();
  if (!ok) return false;
  ssid.toCharArray(wifi_ssid, sizeof(wifi_ssid));
  pass.toCharArray(wifi_password, sizeof(wifi_password));
  hostname.toCharArray(master_hostname, sizeof(master_hostname));
  web_user_arg.toCharArray(web_auth_user, sizeof(web_auth_user));
  web_pass_arg.toCharArray(web_auth_password, sizeof(web_auth_password));
  web_password_change_required = web_pass_arg == MASTER_DEFAULT_PASSWORD;
  wifi_static_enabled = use_static;
  wifi_static_ip = ip;
  wifi_static_gateway = gateway;
  wifi_static_subnet = subnet;
  wifi_static_dns1 = dns1;
  wifi_static_dns2 = dns2;
  mqtt_enabled = mqtt_en;
  mqtt_tls_enabled = mqtt_tls;
  mqtt_ha_discovery = mqtt_ha;
  mqtt_port = mqtt_port_arg;
  mqtt_host_arg.toCharArray(mqtt_host, sizeof(mqtt_host));
  mqtt_user_arg.toCharArray(mqtt_user, sizeof(mqtt_user));
  mqtt_pass_arg.toCharArray(mqtt_password, sizeof(mqtt_password));
  mqtt_topic_arg.toCharArray(mqtt_base_topic, sizeof(mqtt_base_topic));
  mqtt_disc_arg.toCharArray(mqtt_discovery_prefix, sizeof(mqtt_discovery_prefix));
  mqtt_ca_cert = mqtt_ca_arg;
  mqtt_ca_cert.replace("\r\n", "\n");
  mqtt_ca_cert.trim();
  mqtt_tls_verify_enabled = false;
  status_led_enabled = led_en;
  status_led_brightness_pct = constrain(led_pct, (uint8_t)10, (uint8_t)100);
  mqtt_reconfigure_requested = true;
  apply_status_led_config();
  return true;
}

static bool netcfg_save_wifi(const String& ssid, const String& pass, const String& hostname,
                             bool use_static, const IPAddress& ip, const IPAddress& gateway,
                             const IPAddress& subnet, const IPAddress& dns1, const IPAddress& dns2) {
  // Initial setup only needs the WiFi keys. Keeping this write small avoids
  // touching MQTT, certificates and LED settings before the first reboot.
  Preferences wifi_prefs;
  if (!wifi_prefs.begin(MasterSettingsStore::NS_NET, false)) return false;
  bool ok = true;
#define OFE_WIFI_PUT(expr) do { if (!(expr)) ok = false; } while (0)
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_WIFI_SSID, ssid));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_WIFI_PASS, pass));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_HOSTNAME, hostname));
  OFE_WIFI_PUT(wifi_prefs.putBool(MasterSettingsStore::KEY_STATIC_IP, use_static));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_IP, ip.toString()));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_GATEWAY, gateway.toString()));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_SUBNET, subnet.toString()));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_DNS1, dns1.toString()));
  OFE_WIFI_PUT(wifi_prefs.putString(MasterSettingsStore::KEY_DNS2, dns2.toString()));
#undef OFE_WIFI_PUT
  wifi_prefs.end();
  if (!ok) return false;

  ssid.toCharArray(wifi_ssid, sizeof(wifi_ssid));
  pass.toCharArray(wifi_password, sizeof(wifi_password));
  hostname.toCharArray(master_hostname, sizeof(master_hostname));
  wifi_static_enabled = use_static;
  wifi_static_ip = ip;
  wifi_static_gateway = gateway;
  wifi_static_subnet = subnet;
  wifi_static_dns1 = dns1;
  wifi_static_dns2 = dns2;
  return true;
}

static void netcfg_reset() {
  // A network reset must still work when MQTT is blocked in a socket call.
  // Use a local NVS handle and do not wait for the MQTT configuration mutex.
  Preferences reset_prefs;
  bool persisted = false;
  if (reset_prefs.begin(MasterSettingsStore::NS_NET, false)) {
    reset_prefs.clear();
    reset_prefs.putString(MasterSettingsStore::KEY_WIFI_SSID, "");
    reset_prefs.putString(MasterSettingsStore::KEY_WIFI_PASS, "");
    reset_prefs.end();
    persisted = true;
  }
  if (!persisted) {
    Serial.println(F("[NVS] network reset could not be persisted"));
  }
  wifi_ssid[0] = 0;
  wifi_password[0] = 0;
  snprintf(web_auth_user, sizeof(web_auth_user), "%s", WEB_AUTH_USER);
  master_bootstrap_password[0] = 0;
  snprintf(master_ap_password, sizeof(master_ap_password), "%s", MASTER_AP_PASSWORD);
  snprintf(master_bootstrap_password, sizeof(master_bootstrap_password), "%s", MASTER_DEFAULT_PASSWORD);
  snprintf(web_auth_password, sizeof(web_auth_password), "%s", MASTER_DEFAULT_PASSWORD);
  web_password_change_required = true;
  wifi_static_enabled = false;
  mqtt_enabled = false;
  mqtt_tls_enabled = false;
  mqtt_ha_discovery = true;
  mqtt_host[0] = 0;
  mqtt_user[0] = 0;
  mqtt_password[0] = 0;
  snprintf(mqtt_base_topic, sizeof(mqtt_base_topic), "open-fume-extractor");
  snprintf(mqtt_discovery_prefix, sizeof(mqtt_discovery_prefix), "homeassistant");
  mqtt_ca_cert = "";
  mqtt_tls_verify_enabled = false;
  mqtt_port = 1883;
  status_led_enabled = true;
  status_led_brightness_pct = 20;
  // Do not submit an RS485 command while the reset is waiting to reboot.
  ofe_status_leds.setBrightness(status_led_raw_brightness());
  build_master_hostname();
}
static void webauth_reset() {
  // Apply the fallback immediately so a busy MQTT task can never lock the CLI
  // out of the device or leave the running web server with an empty password.
  snprintf(master_bootstrap_password, sizeof(master_bootstrap_password), "%s", MASTER_DEFAULT_PASSWORD);
  snprintf(web_auth_user, sizeof(web_auth_user), "%s", WEB_AUTH_USER);
  snprintf(web_auth_password, sizeof(web_auth_password), "%s", master_bootstrap_password);
  web_password_change_required = true;

  // Use a local NVS handle. The MQTT mutex also protects the live MQTT client
  // and may be held while a broker operation is in progress.
  Preferences auth_prefs;
  bool persisted = false;
  if (auth_prefs.begin(MasterSettingsStore::NS_NET, false)) {
    const bool user_written = auth_prefs.putString(MasterSettingsStore::KEY_WEB_USER, web_auth_user) > 0;
    const bool password_written = auth_prefs.putString(MasterSettingsStore::KEY_WEB_PASS, master_bootstrap_password) > 0;
    auth_prefs.end();
    persisted = user_written && password_written;
  }
  if (!persisted) Serial.println(F("[NVS] web-auth reset could not be persisted; RAM fallback is active"));
  Serial.print(F("[AUTH] reset web user "));
  Serial.print(web_auth_user);
  Serial.print(F(" password "));
  Serial.println(master_bootstrap_password);
}
