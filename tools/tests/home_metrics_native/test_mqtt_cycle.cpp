#include <cstdint>
#include <cassert>
#include <cstdio>
struct ModuleRecord { bool online = true; uint8_t addr = 0; };
struct {
  ModuleRecord records[17];
  uint8_t size = 17;
  uint8_t count() const { return size; }
  const ModuleRecord& at(uint8_t index) const { return records[index]; }
} registry;
static struct { bool connected() { return true; } } mqtt_client;
static bool mqtt_state_module_cycle_pending = false;
static uint8_t mqtt_state_module_order[17], mqtt_state_module_count = 0, mqtt_state_module_cursor = 0;
static uint32_t mqtt_state_module_last_ms = 0, now_ms = 0;
static constexpr uint32_t MASTER_MQTT_MODULE_PUBLISH_GAP_MS = 25;
static uint32_t millis(){return now_ms;}
static uint8_t mqtt_sorted_module_indices(uint8_t* order, uint8_t size) {
  assert(size >= registry.count());
  for (uint8_t i = 0; i < registry.count(); ++i) order[i] = i;
  return registry.count();
}
static unsigned published[17] = {}, offline[17] = {};
static void mqtt_publish_module_state(const ModuleRecord& rec) { ++published[rec.addr]; }
static void mqtt_publish_module_offline(uint8_t addr) { ++offline[addr]; }
#include "mqtt_cycle.inc.h"
int main() {
  for (uint8_t i = 0; i < registry.count(); ++i) registry.records[i].addr = i;
  registry.records[8].online = false;
  // Slow publishing/CPU pressure: a full cycle takes >350 ms. Calling the
  // real master-state start block every 350 ms must not reset its cursor.
  for (now_ms = 0; now_ms < 7000; now_ms += 70) {
    if (now_ms % 350 == 0) begin_cycle();
    mqtt_publish_module_cycle_tick();
  }
  for (unsigned i = 0; i < 17; ++i) assert(i == 8 ? offline[i] >= 4 : published[i] >= 4);
  mqtt_state_module_cycle_pending = false; registry.size = 0;
  begin_cycle(); assert(!mqtt_state_module_cycle_pending);
  puts("PASS: actual MQTT state-cycle blocks: 17 modules, slow consumer, no restart starvation, offline and empty registry");
}
