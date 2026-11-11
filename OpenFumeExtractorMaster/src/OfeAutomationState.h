#pragma once
#include <stdint.h>
#include <string.h>

// Runtime requests from routing and boolean logic have independent ownership.
// Clearing one producer must not release another producer's active request.
struct OfeAutomationRequests {
  bool eco_inhibit = false;
  uint8_t markers = 0;
  uint8_t display_hold[14] = {};

  static bool validDisplayAddress(unsigned addr) {
    return addr == 0 || (addr >= 0x10 && addr <= 0x6F);
  }
  void holdDisplay(uint8_t addr, bool active) {
    if (!validDisplayAddress(addr)) return;
    const uint8_t bit = uint8_t(1U << (addr & 7));
    if (active) display_hold[addr >> 3] |= bit;
    else display_hold[addr >> 3] &= uint8_t(~bit);
  }
  bool holdsDisplay(uint8_t addr) const {
    return validDisplayAddress(addr) &&
      ((display_hold[0] & 1U) || (display_hold[addr >> 3] & (1U << (addr & 7))));
  }
  void setMarker(uint8_t number, bool active) {
    if (number < 1 || number > 8) return;
    const uint8_t bit = uint8_t(1U << (number - 1));
    if (active) markers |= bit;
    else markers &= uint8_t(~bit);
  }
};
