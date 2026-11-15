#pragma once
#include "ModuleRegistry.h"

// A fast reboot can retain the online record. Uptime going backwards makes
// the acknowledged Eco state obsolete (also harmless at an uptime wrap).
static inline bool module_update_uptime(ModuleRecord& rec, uint32_t uptime_s) {
  const bool restarted = rec.telemetry_valid && uptime_s < rec.module_uptime_s;
  if (restarted) {
    rec.eco_mode = false;
    rec.light_sleep = false;
    rec.eco_since_ms = 0;
  }
  rec.module_uptime_s = uptime_s;
  return restarted;
}
