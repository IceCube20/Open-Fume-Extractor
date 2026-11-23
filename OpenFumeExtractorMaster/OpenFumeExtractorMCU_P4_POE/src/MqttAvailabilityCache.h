#pragma once

#include <stdint.h>
#include <string.h>

// MQTT task owns this session-local cache: two bits per module address.
class MqttAvailabilityCache {
public:
  void reset() { memset(states_, 0, sizeof(states_)); }
  bool needsPublish(uint8_t addr, bool online) const {
    const uint8_t shift = (addr & 3U) * 2U;
    return ((states_[addr >> 2U] >> shift) & 3U) != (online ? 2U : 1U);
  }
  void published(uint8_t addr, bool online) { set(addr, online ? 2U : 1U); }
  void forget(uint8_t addr) { set(addr, 0); }
private:
  void set(uint8_t addr, uint8_t value) {
    const uint8_t shift = (addr & 3U) * 2U;
    uint8_t& state = states_[addr >> 2U];
    state = (uint8_t)((state & ~(3U << shift)) | (value << shift));
  }
  uint8_t states_[64] = {};
};
