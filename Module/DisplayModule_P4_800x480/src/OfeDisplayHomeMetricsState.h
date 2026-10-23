#pragma once
#include "DisplayHomeMetrics.h"
static ofe_home_metrics::Cache display_home_metrics_cache;
static portMUX_TYPE display_home_metrics_mux = portMUX_INITIALIZER_UNLOCKED;
static bool home_metrics_get(uint8_t addr, uint8_t type, ofe_home_metrics::State& state, uint32_t& received) {
  portENTER_CRITICAL(&display_home_metrics_mux);
  const bool found = display_home_metrics_cache.get(addr, type, state, received);
  portEXIT_CRITICAL(&display_home_metrics_mux);
  return found;
}
static void home_metrics_clear() {
  portENTER_CRITICAL(&display_home_metrics_mux);
  display_home_metrics_cache.clear();
  portEXIT_CRITICAL(&display_home_metrics_mux);
}

