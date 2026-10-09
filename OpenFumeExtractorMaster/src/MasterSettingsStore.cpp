#include "MasterSettingsStore.h"
#include <cstring>

namespace MasterSettingsStore {

bool putStringVerified(Preferences& prefs, const char* key, const String& value) {
  const String missing = value.length() ? String("") : String("\x01");
  if (prefs.isKey(key) && prefs.getString(key, missing) == value) return true;
  const size_t written = prefs.putString(key, value);
  // Arduino Preferences returns the string length, including zero for empty strings.
  if (value.length() && written != value.length()) return false;
  return prefs.isKey(key) && prefs.getString(key, missing) == value;
}

bool putBoolVerified(Preferences& prefs, const char* key, bool value) {
  if (prefs.isKey(key) && prefs.getBool(key, !value) == value) return true;
  return prefs.putBool(key, value) == 1 && prefs.isKey(key) && prefs.getBool(key, !value) == value;
}

bool putUCharVerified(Preferences& prefs, const char* key, uint8_t value) {
  const uint8_t missing = (uint8_t)~value;
  if (prefs.isKey(key) && prefs.getUChar(key, missing) == value) return true;
  return prefs.putUChar(key, value) == 1 && prefs.isKey(key) && prefs.getUChar(key, missing) == value;
}

bool putUShortVerified(Preferences& prefs, const char* key, uint16_t value) {
  const uint16_t missing = (uint16_t)~value;
  if (prefs.isKey(key) && prefs.getUShort(key, missing) == value) return true;
  return prefs.putUShort(key, value) == 2 && prefs.isKey(key) && prefs.getUShort(key, missing) == value;
}

static bool blobMatches(Preferences& prefs, const char* key, const uint8_t* blob, size_t size) {
  uint8_t stored[14];
  return size <= sizeof(stored) && prefs.getBytesLength(key) == size &&
    prefs.getBytes(key, stored, size) == size && memcmp(stored, blob, size) == 0;
}

static bool putBlobVerified(Preferences& prefs, const char* key, const uint8_t* blob, size_t size) {
  if (blobMatches(prefs, key, blob, size)) return true;
  return prefs.putBytes(key, blob, size) == size && blobMatches(prefs, key, blob, size);
}

bool normalizeMainInput(MainInputSettings& s) {
  if (s.type > 3 || s.bit == 0xFF || (s.type == 2 && s.bit > 15)) return false;
  if (s.type == 0) { s.addr = 0; s.bit = 0; }
  else if (s.type == 1) {
    if (s.addr && (s.addr < 0x10 || s.addr > 0x6F)) return false;
    s.bit = 0;
  } else if (s.addr < 0x10 || s.addr > 0x6F) return false;
  return true;
}

bool saveStatusLed(Preferences& prefs, bool enabled, uint8_t brightness) {
  brightness = brightness < 10 ? 10 : (brightness > 100 ? 100 : brightness);
  if (!prefs.begin(NS_NET, false)) return false;
  const bool existed = prefs.isKey(KEY_LED_ENABLED);
  const bool previous = prefs.getBool(KEY_LED_ENABLED, true);
  const bool brightness_existed = prefs.isKey(KEY_LED_BRIGHTNESS);
  const uint8_t previous_brightness = prefs.getUChar(KEY_LED_BRIGHTNESS, 20);
  const bool ok = putBoolVerified(prefs, KEY_LED_ENABLED, enabled) &&
    putUCharVerified(prefs, KEY_LED_BRIGHTNESS, brightness);
  if (!ok) {
    if (existed) putBoolVerified(prefs, KEY_LED_ENABLED, previous);
    else if (prefs.isKey(KEY_LED_ENABLED)) prefs.remove(KEY_LED_ENABLED);
    if (brightness_existed) putUCharVerified(prefs, KEY_LED_BRIGHTNESS, previous_brightness);
    else if (prefs.isKey(KEY_LED_BRIGHTNESS)) prefs.remove(KEY_LED_BRIGHTNESS);
  }
  prefs.end();
  return ok;
}

bool savePowerSave(Preferences& prefs, bool enabled, uint16_t idle_minutes) {
  idle_minutes = idle_minutes < 1 ? 1 : (idle_minutes > 1440 ? 1440 : idle_minutes);
  if (!prefs.begin(NS_NET, false)) return false;
  const bool existed = prefs.isKey(KEY_POWER_SAVE_ENABLED);
  const bool previous = prefs.getBool(KEY_POWER_SAVE_ENABLED, false);
  const bool idle_existed = prefs.isKey(KEY_POWER_SAVE_IDLE_MIN);
  const uint16_t previous_idle = prefs.getUShort(KEY_POWER_SAVE_IDLE_MIN, 30);
  const bool ok = putBoolVerified(prefs, KEY_POWER_SAVE_ENABLED, enabled) &&
    putUShortVerified(prefs, KEY_POWER_SAVE_IDLE_MIN, idle_minutes);
  if (!ok) {
    if (existed) putBoolVerified(prefs, KEY_POWER_SAVE_ENABLED, previous);
    else if (prefs.isKey(KEY_POWER_SAVE_ENABLED)) prefs.remove(KEY_POWER_SAVE_ENABLED);
    if (idle_existed) putUShortVerified(prefs, KEY_POWER_SAVE_IDLE_MIN, previous_idle);
    else if (prefs.isKey(KEY_POWER_SAVE_IDLE_MIN)) prefs.remove(KEY_POWER_SAVE_IDLE_MIN);
  }
  prefs.end();
  return ok;
}

ControlSettings loadControl(Preferences& prefs) {
  ControlSettings out;
  if (!prefs.begin(NS_CFG, false)) return out;
  const size_t blob_size = prefs.getBytesLength(KEY_CFG_BLOB);
  if (blob_size) {
    uint8_t blob[14] = {0};
    const bool read = blob_size == sizeof(blob) && prefs.getBytes(KEY_CFG_BLOB, blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
    uint8_t check = 0;
    for (size_t i = 0; i < sizeof(blob) - 1; ++i) check ^= blob[i];
    if (!read || blob[0] != 0xC6 || blob[1] != 1 || blob[2] > 3 ||
        blob[9] > 1 || blob[10] > 1 || check != blob[13]) return out;
    out.suction = blob[2];
    out.select_flow = (uint16_t)blob[3] | ((uint16_t)blob[4] << 8);
    out.delay_work_sec = (uint16_t)blob[5] | ((uint16_t)blob[6] << 8);
    out.delay_stand_sec = (uint16_t)blob[7] | ((uint16_t)blob[8] << 8);
    out.stand_intakes = blob[9] != 0;
    out.afterrun_power_enabled = blob[10] != 0;
    out.afterrun_power = (uint16_t)blob[11] | ((uint16_t)blob[12] << 8);
    return out;
  }
  out.suction = prefs.getUChar(KEY_CFG_SUCTION, out.suction);
  out.select_flow = prefs.getUShort(KEY_CFG_FLOW, out.select_flow);
  out.delay_work_sec = prefs.getUShort(KEY_CFG_DELAY_WORK, out.delay_work_sec);
  out.delay_stand_sec = prefs.getUShort(KEY_CFG_DELAY_STAND, out.delay_stand_sec);
  out.stand_intakes = prefs.getUChar(KEY_CFG_STAND, out.stand_intakes ? 1 : 0) != 0;
  out.afterrun_power_enabled = prefs.getUChar(KEY_CFG_AFTER_POWER_ENABLED, out.afterrun_power_enabled ? 1 : 0) != 0;
  out.afterrun_power = prefs.getUShort(KEY_CFG_AFTER_POWER, out.afterrun_power);
  prefs.end();
  return out;
}

bool saveControl(Preferences& prefs, const ControlSettings& s) {
  if (s.suction > 3) return false;
  if (!prefs.begin(NS_CFG, false)) return false;
  uint8_t blob[14] = {0xC6, 1, s.suction,
                      (uint8_t)s.select_flow, (uint8_t)(s.select_flow >> 8),
                      (uint8_t)s.delay_work_sec, (uint8_t)(s.delay_work_sec >> 8),
                      (uint8_t)s.delay_stand_sec, (uint8_t)(s.delay_stand_sec >> 8),
                      (uint8_t)(s.stand_intakes ? 1 : 0),
                      (uint8_t)(s.afterrun_power_enabled ? 1 : 0),
                      (uint8_t)s.afterrun_power, (uint8_t)(s.afterrun_power >> 8), 0};
  for (size_t i = 0; i < sizeof(blob) - 1; ++i) blob[13] ^= blob[i];
  const bool ok = putBlobVerified(prefs, KEY_CFG_BLOB, blob, sizeof(blob));
  prefs.end();
  return ok;
}

MainInputSettings loadMainInput(Preferences& prefs, uint8_t legacy_jbc_type, uint8_t none_type) {
  MainInputSettings out;
  if (!prefs.begin(NS_CFG, false)) return out;
  const size_t blob_size = prefs.getBytesLength(KEY_MAIN_IN_BLOB);
  if (blob_size) {
    uint8_t blob[6] = {0};
    const bool read = blob_size == sizeof(blob) && prefs.getBytes(KEY_MAIN_IN_BLOB, blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
    uint8_t check = 0;
    for (size_t i = 0; i < sizeof(blob) - 1; ++i) check ^= blob[i];
    if (!read || blob[0] != 0xB5 || blob[1] != 1 || check != blob[5]) {
      out.type = none_type;
      return out;
    }
    out.type = blob[2];
    out.addr = blob[3];
    out.bit = blob[4];
    if (!normalizeMainInput(out)) out = {none_type, 0, 0};
    return out;
  }
  out.type = prefs.getUChar(KEY_MAIN_IN_TYPE, 0xFF);
  if (out.type == 0xFF) out.type = prefs.getUChar(KEY_LEGACY_JBC_INPUT, 1) ? legacy_jbc_type : none_type;
  out.addr = prefs.getUChar(KEY_MAIN_IN_ADDR, 0);
  out.bit = prefs.getUChar(KEY_MAIN_IN_BIT, 0);
  prefs.end();
  if (!normalizeMainInput(out)) out = {none_type, 0, 0};
  return out;
}

bool saveMainInput(Preferences& prefs, uint8_t type, uint8_t addr, uint8_t bit) {
  MainInputSettings s{type, addr, bit};
  if (!normalizeMainInput(s)) return false;
  if (!prefs.begin(NS_CFG, false)) return false;
  uint8_t blob[6] = {0xB5, 1, s.type, s.addr, s.bit, 0};
  for (size_t i = 0; i < sizeof(blob) - 1; ++i) blob[5] ^= blob[i];
  const bool ok = putBlobVerified(prefs, KEY_MAIN_IN_BLOB, blob, sizeof(blob));
  prefs.end();
  return ok;
}

uint8_t loadPreferredOutput(Preferences& prefs) {
  if (!prefs.begin(NS_CFG, false)) return 0;
  const uint8_t addr = prefs.getUChar(KEY_OUT_ADDR, 0);
  prefs.end();
  return addr && (addr < 0x10 || addr > 0x6F) ? 0 : addr;
}

bool savePreferredOutput(Preferences& prefs, uint8_t addr) {
  if (addr && (addr < 0x10 || addr > 0x6F)) return false;
  if (!prefs.begin(NS_CFG, false)) return false;
  const bool ok = putUCharVerified(prefs, KEY_OUT_ADDR, addr);
  prefs.end();
  return ok;
}

InputActionRuleSettings loadRuleOpen(Preferences& prefs, uint8_t index, uint8_t none_source, uint8_t none_target) {
  InputActionRuleSettings out;
  out.source_type = none_source;
  out.target_type = none_target;
  const String blob_key = ruleKey(index, "v1");
  const size_t blob_size = prefs.getBytesLength(blob_key.c_str());
  if (blob_size) {
    uint8_t blob[10] = {0};
    if (blob_size != sizeof(blob) || prefs.getBytes(blob_key.c_str(), blob, sizeof(blob)) != sizeof(blob)) return out;
    uint8_t check = 0;
    for (size_t i = 0; i < sizeof(blob) - 1; ++i) check ^= blob[i];
    if (blob[0] != 0xA7 || blob[1] != 1 || blob[2] > 1 || check != blob[9]) return out;
    out.enabled = blob[2] != 0;
    out.source_type = blob[3];
    out.source_addr = blob[4];
    out.source_bit = blob[5];
    out.target_type = blob[6];
    out.target_addr = blob[7];
    out.target_bit = blob[8];
    return out;
  }
  out.enabled = prefs.getUChar(ruleKey(index, "en").c_str(), 0) != 0;
  out.source_type = prefs.getUChar(ruleKey(index, "st").c_str(), none_source);
  out.source_addr = prefs.getUChar(ruleKey(index, "sa").c_str(), 0);
  out.source_bit = prefs.getUChar(ruleKey(index, "sb").c_str(), 0);
  out.target_type = prefs.getUChar(ruleKey(index, "tt").c_str(), none_target);
  out.target_addr = prefs.getUChar(ruleKey(index, "ta").c_str(), 0);
  out.target_bit = prefs.getUChar(ruleKey(index, "tb").c_str(), 0);
  return out;
}

bool saveRuleOpen(Preferences& prefs, uint8_t index, const InputActionRuleSettings& rule) {
  uint8_t blob[10] = {0xA7, 1, (uint8_t)(rule.enabled ? 1 : 0),
                      rule.source_type, rule.source_addr, rule.source_bit,
                      rule.target_type, rule.target_addr, rule.target_bit, 0};
  for (size_t i = 0; i < sizeof(blob) - 1; ++i) blob[9] ^= blob[i];
  const String key = ruleKey(index, "v1");
  return putBlobVerified(prefs, key.c_str(), blob, sizeof(blob));
}

} // namespace MasterSettingsStore
