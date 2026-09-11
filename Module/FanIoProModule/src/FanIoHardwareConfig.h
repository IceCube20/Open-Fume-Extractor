#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace ofe_fanio {

static const uint8_t MAX_INPUTS = 8;
static const uint8_t MAX_OUTPUTS = 8;
static const uint8_t PIN_UNUSED = 0xFF;

enum ChannelFlags : uint8_t { CHANNEL_ENABLED = 0x01, CHANNEL_ACTIVE_LOW = 0x02, CHANNEL_PULL_UP = 0x04, CHANNEL_PULL_DOWN = 0x08 };
enum FilterMode : uint8_t { FILTER_OFF = 0, FILTER_RUNTIME = 1, FILTER_PRESSURE = 2, FILTER_BOTH = 3 };
enum PressureSensorType : uint8_t { PRESSURE_NONE = 0, PRESSURE_ANALOG = 1, PRESSURE_SDP3X = 2 };

struct ChannelConfig { uint8_t pin; uint8_t flags; char name[19]; };
struct HardwareConfig {
  uint32_t magic; uint16_t version; uint16_t size;
  ChannelConfig inputs[MAX_INPUTS]; ChannelConfig outputs[MAX_OUTPUTS];
  uint8_t fan_enable_pin; uint8_t fan_pwm_pin; uint8_t fan_tacho_pin; uint8_t fan_flags; uint8_t tacho_pulses_per_rev;
  uint8_t filter_mode; uint8_t pressure_type; uint8_t pressure_pin_a; uint8_t pressure_pin_b; uint8_t pressure_i2c_address;
  uint32_t filter_lifetime_minutes; uint32_t checksum;
};

static const uint32_t CONFIG_MAGIC = 0x4F464943UL;
static const uint16_t CONFIG_VERSION = 2;

inline uint32_t checksum(const HardwareConfig& cfg) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&cfg);
  const size_t len = offsetof(HardwareConfig, checksum);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; ++i) { hash ^= bytes[i]; hash *= 16777619UL; }
  return hash;
}

inline void copyName(char* dst, const char* src) {
  if (!dst) return;
  size_t o = 0;
  if (src) while (*src && o < 18) {
    const uint8_t c = (uint8_t)*src++;
    if (c >= 32 && c != 127 && c != '"' && c != '\'' && c != '<' && c != '>') dst[o++] = (char)c;
  }
  while (o && dst[o - 1] == ' ') --o;
  dst[o] = 0;
}

inline void defaults(HardwareConfig& cfg, bool pro) {
  memset(&cfg, 0, sizeof(cfg)); cfg.magic = CONFIG_MAGIC; cfg.version = CONFIG_VERSION; cfg.size = sizeof(cfg);
  for (uint8_t i = 0; i < MAX_INPUTS; ++i) { cfg.inputs[i].pin = PIN_UNUSED; char n[8]; snprintf(n, sizeof(n), "IN%u", i + 1U); copyName(cfg.inputs[i].name, n); }
  for (uint8_t i = 0; i < MAX_OUTPUTS; ++i) { cfg.outputs[i].pin = PIN_UNUSED; char n[8]; snprintf(n, sizeof(n), "OUT%u", i + 1U); copyName(cfg.outputs[i].name, n); }
  cfg.inputs[0].pin = 32; cfg.inputs[0].flags = CHANNEL_ENABLED | CHANNEL_ACTIVE_LOW | CHANNEL_PULL_UP;
  cfg.inputs[1].pin = 33; cfg.inputs[1].flags = CHANNEL_ENABLED | CHANNEL_ACTIVE_LOW | CHANNEL_PULL_UP;
  cfg.outputs[0].pin = 22; cfg.outputs[0].flags = CHANNEL_ENABLED;
  cfg.outputs[1].pin = 23; cfg.outputs[1].flags = CHANNEL_ENABLED;
  cfg.fan_enable_pin = 18; cfg.fan_pwm_pin = 19; cfg.fan_tacho_pin = 21; cfg.fan_flags = CHANNEL_ENABLED; cfg.tacho_pulses_per_rev = 2;
  cfg.filter_mode = pro ? FILTER_BOTH : FILTER_OFF; cfg.pressure_type = PRESSURE_NONE;
  cfg.pressure_pin_a = PIN_UNUSED; cfg.pressure_pin_b = PIN_UNUSED; cfg.pressure_i2c_address = 0x21;
  cfg.filter_lifetime_minutes = 60000UL; cfg.checksum = checksum(cfg);
}

inline bool pinAllowed(uint8_t pin, bool output) {
  if (pin == PIN_UNUSED) return true;
  if (pin > 39) return false;
  switch (pin) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 15: case 25: case 26: return false;
    default: break;
  }
  return !output || pin <= 33;
}

inline bool validate(const HardwareConfig& cfg, bool pro) {
  if (cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION || cfg.size != sizeof(cfg) || cfg.checksum != checksum(cfg)) return false;
  bool used[40] = {false};
  auto take = [&](uint8_t pin, bool output) -> bool { if (pin == PIN_UNUSED) return true; if (!pinAllowed(pin, output) || used[pin]) return false; used[pin] = true; return true; };
  for (uint8_t i = 0; i < MAX_INPUTS; ++i) if ((cfg.inputs[i].flags & CHANNEL_ENABLED) && !take(cfg.inputs[i].pin, false)) return false;
  for (uint8_t i = 0; i < MAX_OUTPUTS; ++i) if ((cfg.outputs[i].flags & CHANNEL_ENABLED) && !take(cfg.outputs[i].pin, true)) return false;
  if ((cfg.fan_flags & CHANNEL_ENABLED) && cfg.fan_enable_pin == PIN_UNUSED && cfg.fan_pwm_pin == PIN_UNUSED) return false;
  if ((cfg.fan_flags & CHANNEL_ENABLED) && !take(cfg.fan_enable_pin, true)) return false;
  if (cfg.fan_pwm_pin != PIN_UNUSED && !take(cfg.fan_pwm_pin, true)) return false;
  if (cfg.fan_tacho_pin != PIN_UNUSED && !take(cfg.fan_tacho_pin, false)) return false;
  if (cfg.tacho_pulses_per_rev < 1 || cfg.tacho_pulses_per_rev > 16) return false;
  if (pro && cfg.pressure_type == PRESSURE_ANALOG && !take(cfg.pressure_pin_a, false)) return false;
  if (pro && cfg.pressure_type == PRESSURE_SDP3X && (!take(cfg.pressure_pin_a, true) || !take(cfg.pressure_pin_b, true) || cfg.pressure_i2c_address < 0x08 || cfg.pressure_i2c_address > 0x77)) return false;
  return cfg.filter_mode <= FILTER_BOTH && cfg.filter_lifetime_minutes >= 1UL && cfg.filter_lifetime_minutes <= 1000000UL;
}

inline bool load(Preferences& prefs, HardwareConfig& cfg, bool pro) {
  HardwareConfig stored;
  if (prefs.getBytesLength("hwcfg") == sizeof(stored) && prefs.getBytes("hwcfg", &stored, sizeof(stored)) == sizeof(stored)) {
    const bool intact = stored.magic == CONFIG_MAGIC && stored.size == sizeof(stored) && stored.checksum == checksum(stored);
    if (intact && stored.version == 1 && stored.filter_mode <= 2) {
      // V1 encoded runtime/pressure/both as 0/1/2. Preserve that selection
      // while adding the new explicit OFF mode at value 0.
      stored.filter_mode += 1;
      stored.version = CONFIG_VERSION;
      stored.checksum = checksum(stored);
      if (validate(stored, pro)) { cfg = stored; prefs.putBytes("hwcfg", &cfg, sizeof(cfg)); return true; }
    } else if (intact && validate(stored, pro)) { cfg = stored; return true; }
  }
  defaults(cfg, pro); return false;
}

inline bool save(Preferences& prefs, HardwareConfig& cfg, bool pro) {
  cfg.magic = CONFIG_MAGIC; cfg.version = CONFIG_VERSION; cfg.size = sizeof(cfg); cfg.checksum = checksum(cfg);
  return validate(cfg, pro) && prefs.putBytes("hwcfg", &cfg, sizeof(cfg)) == sizeof(cfg);
}

inline uint16_t enabledMask(const ChannelConfig* channels, uint8_t count) {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < count && i < 16; ++i) if (channels[i].flags & CHANNEL_ENABLED) mask |= (uint16_t)(1U << i);
  return mask;
}

} // namespace ofe_fanio
