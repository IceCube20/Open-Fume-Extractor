#pragma once

#include <stdint.h>

// Small, platform-independent helpers for routing and boolean inputs.
// The FAE bridge does not always supply a standalone Stand mask: its web
// status already defines Stand as "station linked and no Work". USB bridges
// supply a true per-port Stand mask, which we must keep using.
namespace ofe_automation_inputs {
inline bool validMainInputSelection(unsigned type, unsigned addr, unsigned bit) {
  if (type > 3 || bit >= 255) return false;
  if (type == 0) return addr == 0 && bit == 0;
  if (type == 1) return (addr == 0 || (addr >= 0x10 && addr <= 0x6F)) && bit == 0;
  return addr >= 0x10 && addr <= 0x6F && (type != 2 || bit < 16);
}
inline bool mainInputSelected(uint8_t current_type, uint8_t current_addr, uint8_t current_bit,
                              unsigned type, unsigned addr, unsigned bit) {
  return validMainInputSelection(type, addr, bit) &&
    current_type == type && current_addr == addr && current_bit == bit;
}

inline bool jbcStandActive(bool online, bool fae_bridge, bool linked,
                           bool station_present, uint8_t work_mask, uint8_t stand_mask) {
  if (!online || !linked) return false;
  return fae_bridge ? (station_present && work_mask == 0) : stand_mask != 0;
}

// "Selected" means the module is configured as the preferred main output.
// "Active" additionally requires that the extractor is actually enabled.
// Assigning an output role alone must never energize the Active signal.
inline bool mainOutputSelected(uint8_t preferred_addr, uint8_t module_addr) {
  return module_addr != 0 && preferred_addr == module_addr;
}
inline bool mainOutputActive(uint8_t assigned_addr, uint8_t module_addr, bool extractor_enabled) {
  return module_addr != 0 && assigned_addr == module_addr && extractor_enabled;
}

// A warning and an error on the same module are independent signals. The
// general output fault mask uses 0x0002 for the non-critical filter warning.
inline uint16_t warningFaults(uint16_t faults) { return faults & 0x0002U; }
inline uint16_t errorFaults(uint16_t faults) { return faults & (uint16_t)~0x0002U; }
}  // namespace ofe_automation_inputs
