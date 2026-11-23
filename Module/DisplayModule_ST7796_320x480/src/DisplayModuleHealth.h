#pragma once
#include <stdint.h>

namespace ofe_module_health {
// Spare bits in existing packets; old receivers keep their original layout.
constexpr uint8_t SUMMARY_WARNING = 0x20, SUMMARY_ERROR = 0x40, SUMMARY_KNOWN = 0x80;
// JBC activity records use bits 0..4 for native flags; bits 5..7 carry MCU health.
constexpr uint8_t JBC_WARNING = SUMMARY_WARNING, JBC_ERROR = SUMMARY_ERROR, JBC_KNOWN = SUMMARY_KNOWN;
constexpr uint8_t LIVE_WARNING = 0x08, LIVE_ERROR = 0x10, LIVE_KNOWN = 0x20;
constexpr uint16_t METRICS_WARNING = 0x2000, METRICS_ERROR = 0x4000, METRICS_KNOWN = 0x8000;
inline uint8_t summary(bool warning, bool error) {
  return SUMMARY_KNOWN | (warning ? SUMMARY_WARNING : 0) | (error ? SUMMARY_ERROR : 0);
}
inline uint8_t live(bool warning, bool error) {
  return LIVE_KNOWN | (warning ? LIVE_WARNING : 0) | (error ? LIVE_ERROR : 0);
}
inline uint16_t metrics(bool warning, bool error) {
  return METRICS_KNOWN | (warning ? METRICS_WARNING : 0) | (error ? METRICS_ERROR : 0);
}
}
