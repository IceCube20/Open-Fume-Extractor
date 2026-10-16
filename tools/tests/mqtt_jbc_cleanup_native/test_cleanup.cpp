#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "JbcUsbHaCounters.h"
#include "JbcUsbHaControls.h"
#undef assert
#define assert(test) do { if (!(test)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); std::exit(1); } } while (0)
using namespace jbc_rs485;
using String = std::string;
#define F(x) x

static bool mqtt_ha_discovery = true, mqtt_discovery_published = true;
static bool mqtt_discovery_publish_failed = false;
static const char* master_device_id = "test-master";
struct Client { bool online = true; bool connected() const { return online; } } mqtt_client;
struct Registry {
  ModuleRecord modules[ModuleRegistry::MAX_MODULES];
  uint8_t size = 1;
  uint8_t count() const { return size; }
  ModuleRecord& at(uint8_t i) { return modules[i]; }
} registry;
static std::map<std::string, std::string> retained;
static std::vector<std::string> attempts;
static bool fail_publish = false;
static bool mqtt_publish_measured(const char* topic, const char* payload, bool retain) {
  assert(retain);
  attempts.emplace_back(topic);
  if (fail_publish) return false;
  if (*payload) retained[topic] = payload;
  else retained.erase(topic);
  return true;
}
static String mqtt_discovery_topic(const char* component, const char* id, const char* suffix) {
  return String("homeassistant/") + component + '/' + id + '/' + suffix + "/config";
}
static bool mqtt_manifest_save_current();
struct Availability { void forget(uint8_t) {} } mqtt_availability_cache;
#include "generated_cleanup.inc.h"

static MqttDiscoveryManifest previous, current;
static bool mqtt_manifest_save_current() {
  previous = current; mqtt_manifest_previous_valid = true; return true;
}
static MqttManifestEntity entity(const char* component, const char* suffix) {
  MqttManifestEntity e;
  assert(mqtt_manifest_encode_entity(component, suffix, e));
  return e;
}
static std::string topic(uint8_t addr, const char* component, const char* suffix) {
  char full[96]; snprintf(full, sizeof(full), "mod_%02X_%s", addr, suffix);
  return mqtt_discovery_topic(component, master_device_id, full);
}
static void reset(const char* model = "CLM") {
  previous = MqttDiscoveryManifest(); current = MqttDiscoveryManifest();
  mqtt_manifest_previous = &previous; mqtt_manifest_current = &current;
  mqtt_manifest_previous_valid = false; mqtt_manifest_capture_active = false;
  mqtt_manifest_cleanup_active = false; mqtt_manifest_cleanup_module = 0;
  mqtt_manifest_cleanup_entity = 0; mqtt_manifest_cleanup_stage = 0;
  mqtt_discovery_publish_failed = false;
  mqtt_client.online = true; mqtt_ha_discovery = mqtt_discovery_published = true;
  for (auto& s : mqtt_jbc_cleanup) s = MqttJbcCleanupState();
  registry.size = 1;
  ModuleRecord& m = registry.at(0);
  m = ModuleRecord(); m.uid = 123; m.addr = 0x12;
  m.type = MODULE_JBC_USB; m.online = true; m.jbc_link_flags = 1;
  strcpy(m.jbc_usb_model, model); m.jbc_usb_port_count = 1;
  strcpy(current.discovery_prefix, "homeassistant"); strcpy(current.device_id, master_device_id);
  current.module_count = 1; current.modules[0].addr = m.addr;
  retained.clear(); attempts.clear(); fail_publish = false;
}
static void add(const char* component, const char* suffix) {
  auto& mod = current.modules[0]; mod.entities[mod.entity_count++] = entity(component, suffix);
  retained[topic(mod.addr, component, suffix)] = "current";
}
static void finish() {
  unsigned ticks = 0;
  while (mqtt_jbc_cleanup[0].active) {
    attempts.clear(); mqtt_jbc_cleanup_tick();
    assert(attempts.size() <= 2); assert(++ticks < 2000);
  }
  if (!mqtt_jbc_cleanup[0].complete) {
    fprintf(stderr, "Incomplete model %s cursor %u observed %d active %d\n", registry.at(0).jbc_usb_model,
      mqtt_jbc_cleanup[0].cursor, mqtt_jbc_cleanup[0].observed, mqtt_jbc_cleanup[0].active);
  }
  assert(mqtt_jbc_cleanup[0].complete);
}
int main() {
  reset(); mqtt_manifest_current = nullptr; mqtt_jbc_cleanup_schedule();
  mqtt_jbc_cleanup_tick(); assert(attempts.empty());
  // Repair an entire historical catalogue, not just a sample of short keys.
  reset();
  for (uint16_t cursor = 0; cursor < MQTT_JBC_CLEANUP_CANDIDATES; ++cursor) {
    MqttManifestEntity e;
    if (!mqtt_jbc_cleanup_candidate(cursor, e)) continue;
    attempts.clear(); assert(mqtt_manifest_clear_discovery_entity(current, 0x12, e));
    assert(attempts.size() == 1); retained[attempts.front()] = "historical";
  }
  assert(retained.size() > 500);
  add("sensor", "jbc_p1_state"); add("switch", "jbc_cfg_2");
  attempts.clear(); mqtt_jbc_cleanup_schedule(); mqtt_jbc_cleanup_schedule(); finish();
  assert(retained.size() == 2);

  // Old definitions absent from NVS must still be removed, including counters
  // and controls on the wrong HA platform. Current definitions stay untouched.
  for (const char* model : {"DDE", "JTSE", "CLM", "PHSE", "F1", "SF"}) {
    reset(model);
    add("sensor", "jbc_p1_state"); add("sensor", "jbc_p1_cl_counter_work_cycles");
    add("switch", "jbc_cfg_2");
    retained[topic(0x12, "number", "jbc_cfg_2")] = "old component";
    retained[topic(0x12, "sensor", "jbc_p4_counter_hiber_min")] = "old port";
    retained[topic(0x12, "sensor", "jbc_p1_set_flow_permille")] = "old hot air";
    retained[topic(0x12, "sensor", "jbc_tc4_set_temp_c")] = "old preheater";
    retained[topic(0x12, "sensor", "jbc_p1_cl_partial_cleaning_detection_min")] = "long old counter";
    retained[topic(0x20, "sensor", "jbc_p4_counter_hiber_min")] = "other module";
    retained["ofe/module/12/state"] = "state";
    mqtt_jbc_cleanup_schedule();
    assert(!mqtt_jbc_cleanup[0].active && mqtt_jbc_cleanup_needs_confirmation());
    mqtt_jbc_cleanup_schedule(); assert(mqtt_jbc_cleanup[0].active);
    finish(); assert(retained.size() == 5);
    assert(retained.count(topic(0x12, "switch", "jbc_cfg_2")));
    assert(retained.count(topic(0x12, "sensor", "jbc_p1_state")));
    assert(retained.count(topic(0x12, "sensor", "jbc_p1_cl_counter_work_cycles")));
    assert(retained.count("ofe/module/12/state"));
    attempts.clear(); mqtt_jbc_cleanup_schedule(); mqtt_jbc_cleanup_tick();
    assert(!mqtt_jbc_cleanup_needs_confirmation() && attempts.empty());
  }
  // The exact failed retained delete is retried after a transport failure.
  reset(); add("sensor", "jbc_p1_state");
  mqtt_jbc_cleanup_schedule(); mqtt_jbc_cleanup_schedule();
  fail_publish = true;
  while (attempts.empty()) mqtt_jbc_cleanup_tick();
  const auto failed = attempts.back(); const auto cursor = mqtt_jbc_cleanup[0].cursor;
  attempts.clear(); mqtt_jbc_cleanup_tick();
  assert(attempts.front() == failed && mqtt_jbc_cleanup[0].cursor == cursor);
  mqtt_client.online = false; attempts.clear(); mqtt_jbc_cleanup_tick(); assert(attempts.empty());
  mqtt_client.online = true; fail_publish = false; finish();

  // A missing module or incomplete connected station is not confirmed absence.
  for (int mode = 0; mode < 3; ++mode) {
    reset(); add("sensor", "jbc_p1_state");
    previous = current; mqtt_manifest_previous_valid = true;
    current.modules[0].entity_count = 0;
    ModuleRecord& m = registry.at(0);
    if (mode == 0) m.online = false;
    if (mode == 1) m.jbc_usb_model[0] = 0;
    if (mode == 2) m.jbc_usb_port_count = 0;
    mqtt_jbc_cleanup_schedule(); mqtt_jbc_cleanup_tick();
    assert(current.modules[0].entity_count == 1 && attempts.empty());
    assert(!mqtt_jbc_cleanup[0].active);
  }
  // Confirmed station absence deletes all historical station definitions, even
  // without an NVS manifest. Module diagnostics and common activity stay.
  for (bool cached : {false, true}) {
    reset("DDE");
    add("binary_sensor", "jbc_link"); add("binary_sensor", "work"); add("binary_sensor", "stand");
    add("sensor", "cpu");
    retained[topic(0x12, "sensor", "jbc_p2_future_mode")] = "old station";
    retained[topic(0x12, "number", "jbc_cfg_0")] = "old control";
    retained[topic(0x12, "sensor", "jbc_p1_counter_sleep_min")] = "old counter";
    registry.at(0).jbc_link_flags = 0;
    if (!cached) { registry.at(0).jbc_usb_model[0] = 0; registry.at(0).jbc_usb_port_count = 0; }
    mqtt_jbc_cleanup_schedule(); mqtt_jbc_cleanup_tick(); assert(attempts.empty());
    mqtt_jbc_cleanup_schedule(); finish();
    assert(retained.size() == 4);
    assert(retained.count(topic(0x12, "binary_sensor", "jbc_link")));
    registry.at(0).jbc_link_flags = 1;
    strcpy(registry.at(0).jbc_usb_model, "DDE"); registry.at(0).jbc_usb_port_count = 1;
    add("number", "jbc_cfg_0"); mqtt_jbc_cleanup_schedule();
    assert(!mqtt_jbc_cleanup[0].active);
    mqtt_jbc_cleanup_schedule(); finish();
    assert(retained.count(topic(0x12, "number", "jbc_cfg_0")));
  }
  reset(); add("sensor", "heap"); add("sensor", "jbc_p1_state");
  previous = current; mqtt_manifest_previous_valid = true;
  current.modules[0].entity_count = 0; add("sensor", "cpu");
  registry.at(0).jbc_link_flags = 0; mqtt_jbc_cleanup_schedule();
  const auto heap = entity("sensor", "heap"), cpu = entity("sensor", "cpu");
  const auto station = entity("sensor", "jbc_p1_state");
  assert(!mqtt_manifest_module_contains(&current.modules[0], heap));
  assert(mqtt_manifest_module_contains(&current.modules[0], cpu));
  assert(mqtt_manifest_module_contains(&current.modules[0], station));
  // Partial new layouts must match twice; interrupted sweeps resume only after
  // fresh confirmation. Namespace/address/UID changes invalidate old jobs.
  reset(); add("sensor", "jbc_p1_state");
  previous = current; mqtt_manifest_previous_valid = true;
  current.modules[0].entity_count = 0; add("switch", "jbc_cfg_2");
  mqtt_jbc_cleanup_schedule(); assert(current.modules[0].entity_count == previous.modules[0].entity_count);
  current.modules[0].entity_count = 0; add("switch", "jbc_cfg_2");
  mqtt_jbc_cleanup_schedule(); assert(mqtt_jbc_cleanup[0].active);
  registry.at(0).jbc_link_flags = 0; mqtt_jbc_cleanup_tick(); assert(!mqtt_jbc_cleanup[0].active);
  registry.at(0).jbc_link_flags = 1; mqtt_jbc_cleanup_schedule(); assert(!mqtt_jbc_cleanup[0].active);
  current.modules[0].entity_count = 0; add("switch", "jbc_cfg_2");
  mqtt_jbc_cleanup_schedule(); finish();
  strcpy(current.discovery_prefix, "new-ha"); mqtt_jbc_cleanup_schedule(); assert(!mqtt_jbc_cleanup[0].active);
  current.modules[0].entity_count = 0; add("switch", "jbc_cfg_2");
  mqtt_jbc_cleanup_schedule(); assert(mqtt_jbc_cleanup[0].active);
  registry.at(0).uid++; attempts.clear(); mqtt_jbc_cleanup_tick(); assert(attempts.empty());

  reset("DDE"); add("sensor", "jbc_p1_state");
  mqtt_jbc_cleanup_schedule(); mqtt_jbc_cleanup_schedule();
  strcpy(registry.at(0).jbc_usb_model, "CLM");
  attempts.clear(); mqtt_jbc_cleanup_tick();
  assert(attempts.empty() && !mqtt_jbc_cleanup[0].active);

  // Deletion must use the complete topic, not a truncated 40-byte suffix.
  reset(); const char* long_suffix = "jbc_p1_cl_partial_cleaning_detection_min";
  retained[topic(0x12, "sensor", long_suffix)] = "stale";
  mqtt_clear_module_entity(0x12, "sensor", long_suffix); assert(retained.empty());

  // The persistent differential cleanup and orphan repair cooperate, and an
  // incomplete/failed discovery cannot authorize either path to delete topics.
  reset(); add("sensor", "jbc_p4_counter_hiber_min");
  previous = current; mqtt_manifest_previous_valid = true;
  current.modules[0].entity_count = 0; add("sensor", "jbc_p1_state");
  mqtt_jbc_cleanup_schedule();
  current.modules[0].entity_count = 0; add("sensor", "jbc_p1_state");
  mqtt_jbc_cleanup_schedule(); mqtt_manifest_cleanup_active = true;
  mqtt_discovery_published = false; attempts.clear(); mqtt_cleanup_tick(); assert(attempts.empty());
  mqtt_discovery_published = true;
  for (unsigned i = 0; i < 2000 && (mqtt_manifest_cleanup_active || mqtt_jbc_cleanup[0].active); ++i) {
    attempts.clear(); mqtt_cleanup_tick(); assert(attempts.size() <= 12);
  }
  assert(!mqtt_manifest_cleanup_active && mqtt_jbc_cleanup[0].complete);
  assert(retained.size() == 1 && retained.count(topic(0x12, "sensor", "jbc_p1_state")));
  assert(previous.modules[0].entity_count == 1);
  puts("PASS: real MQTT manifest codec, all JBC families, historical orphan cleanup, stable snapshots, bounded work, retries and full topic names");
}
