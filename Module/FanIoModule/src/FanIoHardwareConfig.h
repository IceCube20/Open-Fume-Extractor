#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace ofe_fanio {

static const uint8_t MAX_INPUTS = 8;
static const uint8_t MAX_OUTPUTS = 8;
static const uint8_t PIN_UNUSED = 0xFF;

enum ChannelFlags : uint8_t {
  CHANNEL_ENABLED = 0x01,
  CHANNEL_ACTIVE_LOW = 0x02,
  CHANNEL_PULL_UP = 0x04,
  CHANNEL_PULL_DOWN = 0x08,
};

enum FilterMode : uint8_t {
  FILTER_OFF = 0,
  FILTER_RUNTIME = 1,
  FILTER_PRESSURE = 2,
  FILTER_BOTH = 3,
};

enum PressureSensorType : uint8_t {
  PRESSURE_NONE = 0,
  PRESSURE_ANALOG = 1,
  PRESSURE_SDP3X = 2,
};

enum FanOutputMode : uint8_t {
  FAN_OUTPUT_RELAY = 0,
  FAN_OUTPUT_PWM = 1,
  FAN_OUTPUT_RELAY_PWM = 2,
};

enum FanEnableFlags : uint8_t {
  FAN_ENABLE_ACTIVE_LOW = 0x01,
};

enum FanPwmFlags : uint8_t {
  FAN_PWM_ACTIVE_LOW = 0x01,
  FAN_PWM_OPEN_DRAIN = 0x02,
};

struct ChannelConfig {
  uint8_t pin;
  uint8_t flags;
  char name[19];
};

// Version 1/2 layout, kept only so existing installations migrate without
// losing their GPIO/filter configuration.
struct HardwareConfigV2 {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  ChannelConfig inputs[MAX_INPUTS];
  ChannelConfig outputs[MAX_OUTPUTS];
  uint8_t fan_enable_pin;
  uint8_t fan_pwm_pin;
  uint8_t fan_tacho_pin;
  uint8_t fan_flags;
  uint8_t tacho_pulses_per_rev;
  uint8_t filter_mode;
  uint8_t pressure_type;
  uint8_t pressure_pin_a;
  uint8_t pressure_pin_b;
  uint8_t pressure_i2c_address;
  uint32_t filter_lifetime_minutes;
  uint32_t checksum;
};

struct HardwareConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  ChannelConfig inputs[MAX_INPUTS];
  ChannelConfig outputs[MAX_OUTPUTS];
  uint8_t fan_enable_pin;
  uint8_t fan_pwm_pin;
  uint8_t fan_tacho_pin;
  uint8_t fan_flags;                 // CHANNEL_ENABLED only in V3+
  uint8_t tacho_pulses_per_rev;
  uint8_t fan_output_mode;           // FanOutputMode
  uint8_t fan_enable_flags;          // FanEnableFlags
  uint8_t fan_pwm_flags;             // FanPwmFlags
  uint8_t fan_min_power_percent;     // 1..100, used for PWM modes
  uint8_t fan_max_power_percent;     // 1..100, used for PWM modes
  uint8_t filter_mode;
  uint8_t pressure_type;
  uint8_t pressure_pin_a;
  uint8_t pressure_pin_b;
  uint8_t pressure_i2c_address;
  uint32_t filter_lifetime_minutes;
  uint32_t checksum;
};

static const uint32_t CONFIG_MAGIC = 0x4F464943UL; // OFIC
static const uint16_t CONFIG_VERSION = 3;

inline uint32_t checksumBytes(const void* ptr, size_t len) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(ptr);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

inline uint32_t checksum(const HardwareConfig& cfg) {
  return checksumBytes(&cfg, offsetof(HardwareConfig, checksum));
}

inline uint32_t checksumV2(const HardwareConfigV2& cfg) {
  return checksumBytes(&cfg, offsetof(HardwareConfigV2, checksum));
}

inline void copyName(char* dst, const char* src) {
  if (!dst) return;
  size_t o = 0;
  if (src) {
    while (*src && o < 18) {
      const uint8_t c = (uint8_t)*src++;
      if (c >= 32 && c != 127 && c != '"' && c != '\'' && c != '<' && c != '>') dst[o++] = (char)c;
    }
  }
  while (o && dst[o - 1] == ' ') --o;
  dst[o] = 0;
}

inline bool fanUsesRelay(const HardwareConfig& cfg) {
  return cfg.fan_output_mode == FAN_OUTPUT_RELAY || cfg.fan_output_mode == FAN_OUTPUT_RELAY_PWM;
}

inline bool fanUsesPwm(const HardwareConfig& cfg) {
  return cfg.fan_output_mode == FAN_OUTPUT_PWM || cfg.fan_output_mode == FAN_OUTPUT_RELAY_PWM;
}

inline uint8_t inferFanOutputMode(uint8_t enable_pin, uint8_t pwm_pin) {
  const bool relay = enable_pin != PIN_UNUSED;
  const bool pwm = pwm_pin != PIN_UNUSED;
  if (relay && pwm) return FAN_OUTPUT_RELAY_PWM;
  if (pwm) return FAN_OUTPUT_PWM;
  return FAN_OUTPUT_RELAY;
}

inline void defaults(HardwareConfig& cfg, bool pro) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.size = sizeof(cfg);
  for (uint8_t i = 0; i < MAX_INPUTS; ++i) {
    cfg.inputs[i].pin = PIN_UNUSED;
    char name[8];
    snprintf(name, sizeof(name), "IN%u", (unsigned)i + 1U);
    copyName(cfg.inputs[i].name, name);
  }
  for (uint8_t i = 0; i < MAX_OUTPUTS; ++i) {
    cfg.outputs[i].pin = PIN_UNUSED;
    char name[8];
    snprintf(name, sizeof(name), "OUT%u", (unsigned)i + 1U);
    copyName(cfg.outputs[i].name, name);
  }
  cfg.inputs[0].pin = 32;
  cfg.inputs[0].flags = CHANNEL_ENABLED | CHANNEL_ACTIVE_LOW | CHANNEL_PULL_UP;
  cfg.inputs[1].pin = 33;
  cfg.inputs[1].flags = CHANNEL_ENABLED | CHANNEL_ACTIVE_LOW | CHANNEL_PULL_UP;
  cfg.outputs[0].pin = 22;
  cfg.outputs[0].flags = CHANNEL_ENABLED;
  cfg.outputs[1].pin = 23;
  cfg.outputs[1].flags = CHANNEL_ENABLED;
  cfg.fan_enable_pin = 18;
  cfg.fan_pwm_pin = 19;
  cfg.fan_tacho_pin = 21;
  cfg.fan_flags = CHANNEL_ENABLED;
  cfg.tacho_pulses_per_rev = 2;
  cfg.fan_output_mode = FAN_OUTPUT_RELAY_PWM;
  cfg.fan_enable_flags = 0;
  cfg.fan_pwm_flags = 0;
  cfg.fan_min_power_percent = 10;
  cfg.fan_max_power_percent = 100;
  cfg.filter_mode = pro ? FILTER_BOTH : FILTER_OFF;
  cfg.pressure_type = PRESSURE_NONE;
  cfg.pressure_pin_a = PIN_UNUSED;
  cfg.pressure_pin_b = PIN_UNUSED;
  cfg.pressure_i2c_address = 0x21;
  cfg.filter_lifetime_minutes = 60000UL;
  cfg.checksum = checksum(cfg);
}

inline bool pinAllowed(uint8_t pin, bool output) {
  if (pin == PIN_UNUSED) return true;
  if (pin > 39) return false;
  switch (pin) {
    case 0: case 1: case 2: case 3: case 4: case 5:
    case 6: case 7: case 8: case 9: case 10: case 11:
    case 12: case 15: case 25: case 26:
      return false;
    default:
      break;
  }
  return !output || pin <= 33;
}

inline bool validate(const HardwareConfig& cfg, bool pro) {
  if (cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION || cfg.size != sizeof(cfg)) return false;
  if (cfg.checksum != checksum(cfg)) return false;
  if (cfg.fan_output_mode > FAN_OUTPUT_RELAY_PWM) return false;
  if (cfg.fan_enable_flags & (uint8_t)~FAN_ENABLE_ACTIVE_LOW) return false;
  if (cfg.fan_pwm_flags & (uint8_t)~(FAN_PWM_ACTIVE_LOW | FAN_PWM_OPEN_DRAIN)) return false;
  if (cfg.fan_min_power_percent < 1 || cfg.fan_min_power_percent > 100 ||
      cfg.fan_max_power_percent < 1 || cfg.fan_max_power_percent > 100 ||
      cfg.fan_min_power_percent > cfg.fan_max_power_percent) return false;

  bool used[40] = {false};
  auto take = [&](uint8_t pin, bool output) -> bool {
    if (pin == PIN_UNUSED) return true;
    if (!pinAllowed(pin, output) || used[pin]) return false;
    used[pin] = true;
    return true;
  };
  for (uint8_t i = 0; i < MAX_INPUTS; ++i) if ((cfg.inputs[i].flags & CHANNEL_ENABLED) && !take(cfg.inputs[i].pin, false)) return false;
  for (uint8_t i = 0; i < MAX_OUTPUTS; ++i) if ((cfg.outputs[i].flags & CHANNEL_ENABLED) && !take(cfg.outputs[i].pin, true)) return false;

  if (cfg.fan_flags & CHANNEL_ENABLED) {
    if (fanUsesRelay(cfg)) {
      if (cfg.fan_enable_pin == PIN_UNUSED || !take(cfg.fan_enable_pin, true)) return false;
    }
    if (fanUsesPwm(cfg)) {
      if (cfg.fan_pwm_pin == PIN_UNUSED || !take(cfg.fan_pwm_pin, true)) return false;
    }
  }
  if (cfg.fan_tacho_pin != PIN_UNUSED && !take(cfg.fan_tacho_pin, false)) return false;
  if (cfg.tacho_pulses_per_rev < 1 || cfg.tacho_pulses_per_rev > 16) return false;
  if (pro && cfg.pressure_type == PRESSURE_ANALOG && !take(cfg.pressure_pin_a, false)) return false;
  if (pro && cfg.pressure_type == PRESSURE_SDP3X) {
    if (!take(cfg.pressure_pin_a, true) || !take(cfg.pressure_pin_b, true)) return false;
    if (cfg.pressure_i2c_address < 0x08 || cfg.pressure_i2c_address > 0x77) return false;
  }
  if (cfg.filter_mode > FILTER_BOTH) return false;
  return cfg.filter_lifetime_minutes >= 1UL && cfg.filter_lifetime_minutes <= 1000000UL;
}

inline void migrateV2(const HardwareConfigV2& old_cfg, HardwareConfig& cfg, bool filter_mode_v1) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.size = sizeof(cfg);
  memcpy(cfg.inputs, old_cfg.inputs, sizeof(cfg.inputs));
  memcpy(cfg.outputs, old_cfg.outputs, sizeof(cfg.outputs));
  cfg.fan_enable_pin = old_cfg.fan_enable_pin;
  cfg.fan_pwm_pin = old_cfg.fan_pwm_pin;
  cfg.fan_tacho_pin = old_cfg.fan_tacho_pin;
  cfg.fan_flags = old_cfg.fan_flags & CHANNEL_ENABLED;
  cfg.tacho_pulses_per_rev = old_cfg.tacho_pulses_per_rev;
  cfg.fan_output_mode = inferFanOutputMode(old_cfg.fan_enable_pin, old_cfg.fan_pwm_pin);
  const bool old_active_low = (old_cfg.fan_flags & CHANNEL_ACTIVE_LOW) != 0;
  cfg.fan_enable_flags = old_active_low ? FAN_ENABLE_ACTIVE_LOW : 0;
  cfg.fan_pwm_flags = old_active_low ? FAN_PWM_ACTIVE_LOW : 0;
  cfg.fan_min_power_percent = 10;
  cfg.fan_max_power_percent = 100;
  cfg.filter_mode = old_cfg.filter_mode;
  if (filter_mode_v1 && cfg.filter_mode <= 2) cfg.filter_mode += 1;
  cfg.pressure_type = old_cfg.pressure_type;
  cfg.pressure_pin_a = old_cfg.pressure_pin_a;
  cfg.pressure_pin_b = old_cfg.pressure_pin_b;
  cfg.pressure_i2c_address = old_cfg.pressure_i2c_address;
  cfg.filter_lifetime_minutes = old_cfg.filter_lifetime_minutes;
  cfg.checksum = checksum(cfg);
}

inline bool load(Preferences& prefs, HardwareConfig& cfg, bool pro) {
  const size_t stored_len = prefs.getBytesLength("hwcfg");
  if (stored_len == sizeof(HardwareConfig)) {
    HardwareConfig stored;
    if (prefs.getBytes("hwcfg", &stored, sizeof(stored)) == sizeof(stored) && validate(stored, pro)) {
      cfg = stored;
      return true;
    }
  }

  if (stored_len == sizeof(HardwareConfigV2)) {
    HardwareConfigV2 stored;
    if (prefs.getBytes("hwcfg", &stored, sizeof(stored)) == sizeof(stored)) {
      const bool intact = stored.magic == CONFIG_MAGIC && stored.size == sizeof(stored) && stored.checksum == checksumV2(stored);
      if (intact && (stored.version == 1 || stored.version == 2)) {
        HardwareConfig migrated;
        migrateV2(stored, migrated, stored.version == 1);
        if (validate(migrated, pro)) {
          cfg = migrated;
          prefs.putBytes("hwcfg", &cfg, sizeof(cfg));
          return true;
        }
      }
    }
  }

  defaults(cfg, pro);
  return false;
}

inline bool save(Preferences& prefs, HardwareConfig& cfg, bool pro) {
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.size = sizeof(cfg);
  cfg.checksum = checksum(cfg);
  if (!validate(cfg, pro)) return false;
  return prefs.putBytes("hwcfg", &cfg, sizeof(cfg)) == sizeof(cfg);
}

inline uint16_t enabledMask(const ChannelConfig* channels, uint8_t count) {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < count && i < 16; ++i) if (channels[i].flags & CHANNEL_ENABLED) mask |= (uint16_t)(1U << i);
  return mask;
}

} // namespace ofe_fanio
