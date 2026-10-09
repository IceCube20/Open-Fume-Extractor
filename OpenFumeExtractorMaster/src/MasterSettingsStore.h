#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace MasterSettingsStore {

constexpr const char* NS_NET = "master-net";
constexpr const char* NS_CFG = "master-cfg";

constexpr const char* KEY_WIFI_SSID = "ssid";
constexpr const char* KEY_WIFI_PASS = "pass";
constexpr const char* KEY_HOSTNAME = "host";
constexpr const char* KEY_WEB_USER = "web_user";
constexpr const char* KEY_WEB_PASS = "web_pass";
constexpr const char* KEY_AP_PASS = "ap_pass";
constexpr const char* KEY_STATIC_IP = "static";
constexpr const char* KEY_IP = "ip";
constexpr const char* KEY_GATEWAY = "gateway";
constexpr const char* KEY_SUBNET = "subnet";
constexpr const char* KEY_DNS1 = "dns1";
constexpr const char* KEY_DNS2 = "dns2";
constexpr const char* KEY_MQTT_ENABLED = "mqtt_en";
constexpr const char* KEY_MQTT_TLS = "mqtt_tls";
constexpr const char* KEY_MQTT_HA = "mqtt_ha";
constexpr const char* KEY_MQTT_HOST = "mqtt_host";
constexpr const char* KEY_MQTT_PORT = "mqtt_port";
constexpr const char* KEY_MQTT_USER = "mqtt_user";
constexpr const char* KEY_MQTT_PASS = "mqtt_pass";
constexpr const char* KEY_MQTT_TOPIC = "mqtt_topic";
constexpr const char* KEY_MQTT_DISC = "mqtt_disc";
constexpr const char* KEY_MQTT_CA = "mqtt_ca";
constexpr const char* KEY_LED_ENABLED = "led_en";
constexpr const char* KEY_LED_BRIGHTNESS = "led_pct";
constexpr const char* KEY_POWER_SAVE_ENABLED = "eco_en";
constexpr const char* KEY_POWER_SAVE_IDLE_MIN = "eco_idle";

constexpr const char* KEY_LANG = "lang";
constexpr const char* KEY_DEV_MODE = "dev_mode";
constexpr const char* KEY_OUT_ADDR = "out_addr";
constexpr const char* KEY_MAIN_IN_TYPE = "main_in_t";
constexpr const char* KEY_MAIN_IN_ADDR = "main_in_a";
constexpr const char* KEY_MAIN_IN_BIT = "main_in_b";
constexpr const char* KEY_MAIN_IN_BLOB = "main_in_v1";
constexpr const char* KEY_LEGACY_JBC_INPUT = "r_jbc";
constexpr const char* KEY_CFG_SUCTION = "cfg_suction";
constexpr const char* KEY_CFG_FLOW = "cfg_flow";
constexpr const char* KEY_CFG_DELAY_WORK = "cfg_dwork";
constexpr const char* KEY_CFG_DELAY_STAND = "cfg_dstand";
constexpr const char* KEY_CFG_STAND = "cfg_stand";
constexpr const char* KEY_CFG_AFTER_POWER_ENABLED = "cfg_arpen";
constexpr const char* KEY_CFG_AFTER_POWER = "cfg_arpwr";
constexpr const char* KEY_CFG_BLOB = "cfg_blob1";
constexpr const char* KEY_LOGIC_ACTIVE = "logic_active";
constexpr const char* KEY_LOGIC_LEGACY_JSON = "logic_json";
constexpr const char* KEY_MODULE_SNAPSHOT_COUNT = "mod_cnt";

struct ControlSettings {
  uint8_t suction = 3;
  uint16_t select_flow = 1000;
  uint16_t delay_work_sec = 10;
  uint16_t delay_stand_sec = 0;
  bool stand_intakes = true;
  bool continuous = false; // Runtime only; never serialized or restored.
  bool afterrun_power_enabled = false;
  uint16_t afterrun_power = 300;
};

struct MainInputSettings {
  uint8_t type = 0;
  uint8_t addr = 0;
  uint8_t bit = 0;
};

struct InputActionRuleSettings {
  bool enabled = false;
  uint8_t source_type = 0;
  uint8_t source_addr = 0;
  uint8_t source_bit = 0;
  uint8_t target_type = 0;
  uint8_t target_addr = 0;
  uint8_t target_bit = 0;
};

inline String routeKey(uint8_t addr, uint8_t bit) {
  char key[12];
  snprintf(key, sizeof(key), "r%u_%02X", (unsigned)(bit + 1), addr);
  return String(key);
}

inline String ruleKey(uint8_t index, const char* suffix) {
  char key[14];
  snprintf(key, sizeof(key), "ar%u_%s", index, suffix ? suffix : "");
  return String(key);
}

inline String moduleLabelKey(uint64_t uid) {
  const uint32_t hi = (uint32_t)(uid >> 32);
  const uint32_t lo = (uint32_t)uid;
  char key[16];
  snprintf(key, sizeof(key), "l%01lX%04lX%08lX", (hi >> 28) & 0x0FUL, hi & 0xFFFFUL, lo);
  return String(key);
}

inline String moduleSnapshotKey(uint8_t index, char suffix) {
  char key[8];
  snprintf(key, sizeof(key), "m%c%02u", suffix, index);
  return String(key);
}

inline String logicSlotKey(uint8_t slot) {
  return String("logic") + String(slot);
}

inline String logicSlotCountKey(uint8_t slot) {
  return String("logic") + String(slot) + String("_cnt");
}

inline String logicSlotPartKey(uint8_t slot, uint8_t part) {
  char key[13];
  snprintf(key, sizeof(key), "logic%u_%02u", (unsigned)slot, (unsigned)part);
  return String(key);
}

ControlSettings loadControl(Preferences& prefs);
bool saveControl(Preferences& prefs, const ControlSettings& s);
MainInputSettings loadMainInput(Preferences& prefs, uint8_t legacy_jbc_type, uint8_t none_type);
bool saveMainInput(Preferences& prefs, uint8_t type, uint8_t addr, uint8_t bit);
uint8_t loadPreferredOutput(Preferences& prefs);
bool savePreferredOutput(Preferences& prefs, uint8_t addr);
InputActionRuleSettings loadRuleOpen(Preferences& prefs, uint8_t index, uint8_t none_source, uint8_t none_target);
bool saveRuleOpen(Preferences& prefs, uint8_t index, const InputActionRuleSettings& rule);

// Open-handle writers skip unchanged values and verify successful persistence.
// Callers own the handle and their existing task locks; these functions do not
// wait for MQTT, the command queue or the OFE bus.
bool putStringVerified(Preferences& prefs, const char* key, const String& value);
bool putBoolVerified(Preferences& prefs, const char* key, bool value);
bool putUCharVerified(Preferences& prefs, const char* key, uint8_t value);
bool putUShortVerified(Preferences& prefs, const char* key, uint16_t value);
bool saveStatusLed(Preferences& prefs, bool enabled, uint8_t brightness);
bool savePowerSave(Preferences& prefs, bool enabled, uint16_t idle_minutes);
bool normalizeMainInput(MainInputSettings& s);

} // namespace MasterSettingsStore
