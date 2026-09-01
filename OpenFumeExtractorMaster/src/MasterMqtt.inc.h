#pragma once

// MQTT/Home Assistant integration. Included from the master sketch so it can
// use the sketch-local static configuration while the main file stays readable.
static void mqtt_service_task(void* parameter) {
  (void)parameter;
  for (;;) {
    mqtt_loop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void ui_config_load() {
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  String lang = master_prefs.getString(MasterSettingsStore::KEY_LANG, "de");
  developer_mode_enabled = master_prefs.getBool(MasterSettingsStore::KEY_DEV_MODE, false);
  master_prefs.end();
  if (lang != "en") lang = "de";
  lang.toCharArray(web_lang, sizeof(web_lang));
}

static void web_handle_state() {
  const bool include_universal_descriptor = web.hasArg("desc") && web.arg("desc") == "1";
  const bool include_heap_diag = developer_mode_enabled;
  heap_diag_set_context(HEAP_DIAG_CTX_WEB, include_universal_descriptor ? "state+descriptor" : "state");
  heap_diag_sample("state_begin");
  String payload = build_state_json(include_universal_descriptor, include_heap_diag);
  heap_diag_sample("state_built");
  web.send(200, "application/json", payload);
  heap_diag_sample("state_sent");
  heap_diag_clear_context(HEAP_DIAG_CTX_WEB);
}

static void web_handle_led_state() {
  // Tiny high-rate endpoint used only for the two LED words on the status
  // page. Keep this independent from /state so LED responsiveness does not
  // multiply the large status JSON allocations.
  String json;
  json.reserve(256 + registry.count() * 72);
  json += "{\"uptime_ms\":"; json += millis();
  json += ",\"enabled\":"; json += status_led_enabled ? "true" : "false";
  json += ",\"master_ofe\":"; json += (uint8_t)ofe_status_leds.busEvent();
  json += ",\"master_evt\":"; json += (uint8_t)ofe_status_leds.moduleEvent();
  json += ",\"modules\":[";
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (i) json += ',';
    const bool ota_target = scheduler.moduleFirmwareUpdateActive() && scheduler.moduleFirmwareUpdateTarget() == m.addr;
    json += "{\"addr\":"; json += m.addr;
    json += ",\"online\":"; json += m.online ? "true" : "false";
    json += ",\"valid\":"; json += (m.led_status_valid || ota_target) ? "true" : "false";
    json += ",\"ofe\":"; json += m.led_ofe_event;
    json += ",\"evt\":"; json += ota_target ? (uint8_t)OFE_LED_EVENT_FW_UPDATE : m.led_evt_event;
    json += '}';
  }
  json += "]}";
  web.send(200, "application/json", json);
}
static void web_handle_developer_mode() {
  if (!web.hasArg("enabled")) {
    web.send(400, "text/plain; charset=utf-8", "missing enabled");
    return;
  }
  const bool enabled = web.arg("enabled") == "1" || web.arg("enabled") == "true";
  if (enabled && web.arg("password") != String(OFE_DEVELOPER_PASSWORD)) {
    web.send(403, "text/plain; charset=utf-8", web_is_german() ? "Falsches Entwicklerpasswort" : "Wrong developer password");
    return;
  }
  developer_mode_enabled = enabled;
  Preferences prefs;
  prefs.begin(MasterSettingsStore::NS_CFG, false);
  prefs.putBool(MasterSettingsStore::KEY_DEV_MODE, enabled);
  prefs.end();
  if (enabled) heap_diag_enable();
  web.send(200, "application/json", enabled ? "{\"ok\":true,\"enabled\":true}" : "{\"ok\":true,\"enabled\":false}");
}
static String mqtt_topic_path(const char* leaf) {
  String base = mqtt_base_topic;
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (!base.length()) base = "open-fume-extractor";
  base += "/";
  base += leaf;
  return base;
}

static String mqtt_discovery_topic(const char* component, const char* object, const char* suffix) {
  String prefix = mqtt_discovery_prefix;
  prefix.trim();
  while (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
  if (!prefix.length()) prefix = "homeassistant";
  String topic = prefix;
  topic += "/";
  topic += component;
  topic += "/";
  topic += object;
  topic += "/";
  topic += suffix;
  topic += "/config";
  return topic;
}


// ---------------------------------------------------------------------------
// Home Assistant module discovery manifest
//
// Older firmware deleted every possible entity for every unused address after
// each scan (and repeated the sweep three times). That could generate well over
// 100k retained MQTT delete publishes even when nothing had changed.
//
// v1.7.40 records only the module discovery entities that it actually intends
// to publish. The previous manifest stays in NVS until all stale entries have
// been deleted successfully. A scan with no entity/address changes therefore
// schedules zero retained deletes.
// ---------------------------------------------------------------------------

static const uint32_t MQTT_HA_MANIFEST_MAGIC = 0x4F464548UL; // "OFEH"
static const uint8_t MQTT_HA_MANIFEST_VERSION = 1;
static const uint8_t MQTT_HA_MANIFEST_MAX_ENTITIES = 72;

enum MqttManifestComponent : uint8_t {
  MQTT_MAN_COMP_NONE = 0,
  MQTT_MAN_COMP_SENSOR = 1,
  MQTT_MAN_COMP_BINARY_SENSOR = 2,
  MQTT_MAN_COMP_SWITCH = 3,
  MQTT_MAN_COMP_NUMBER = 4,
  MQTT_MAN_COMP_SELECT = 5,
  MQTT_MAN_COMP_BUTTON = 6,
  MQTT_MAN_COMP_TEXT = 7,
};

enum MqttManifestEntityKind : uint8_t {
  MQTT_MAN_ENTITY_STATIC = 0,
  MQTT_MAN_ENTITY_DYNAMIC = 1,
};

struct MqttManifestEntity {
  uint8_t component = 0;
  uint8_t kind = 0;
  uint8_t key = 0;
};

struct MqttManifestModule {
  uint8_t addr = 0;
  uint8_t entity_count = 0;
  MqttManifestEntity entities[MQTT_HA_MANIFEST_MAX_ENTITIES];
};

struct MqttDiscoveryManifest {
  uint32_t magic = MQTT_HA_MANIFEST_MAGIC;
  uint16_t struct_size = 0;
  uint8_t version = MQTT_HA_MANIFEST_VERSION;
  uint8_t module_count = 0;
  char discovery_prefix[33] = {0};
  char device_id[40] = {0};
  char base_topic[65] = {0};
  MqttManifestModule modules[ModuleRegistry::MAX_MODULES];
};

static MqttDiscoveryManifest* mqtt_manifest_previous = nullptr;
static MqttDiscoveryManifest* mqtt_manifest_current = nullptr;
static bool mqtt_manifest_load_attempted = false;
static bool mqtt_manifest_previous_valid = false;
static bool mqtt_manifest_capture_active = false;

static bool mqtt_manifest_cleanup_active = false;
static uint8_t mqtt_manifest_cleanup_module = 0;
static uint8_t mqtt_manifest_cleanup_entity = 0;
static uint8_t mqtt_manifest_cleanup_stage = 0;

static uint32_t mqtt_manifest_seed_total = 0;
static uint32_t mqtt_manifest_save_total = 0;
static uint32_t mqtt_manifest_cleanup_cycles = 0;
static uint32_t mqtt_manifest_delete_total = 0;
static uint32_t mqtt_manifest_delete_failed = 0;
static uint32_t mqtt_manifest_untracked_total = 0;
static uint32_t mqtt_manifest_last_stale_count = 0;
static uint32_t mqtt_manifest_last_delete_count = 0;

static void* mqtt_manifest_alloc(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  if (p) memset(p, 0, bytes);
  return p;
}

static bool mqtt_manifest_ensure_storage() {
  if (!mqtt_manifest_previous) {
    mqtt_manifest_previous =
      static_cast<MqttDiscoveryManifest*>(mqtt_manifest_alloc(sizeof(MqttDiscoveryManifest)));
  }
  if (!mqtt_manifest_current) {
    mqtt_manifest_current =
      static_cast<MqttDiscoveryManifest*>(mqtt_manifest_alloc(sizeof(MqttDiscoveryManifest)));
  }
  return mqtt_manifest_previous && mqtt_manifest_current;
}

static void mqtt_normalize_topic_root(
    const char* source,
    const char* fallback,
    char* out,
    size_t out_len) {
  if (!out || !out_len) return;
  String value = source ? String(source) : String();
  value.trim();
  while (value.endsWith("/")) value.remove(value.length() - 1);
  if (!value.length()) value = fallback;
  value.toCharArray(out, out_len);
}

static void mqtt_manifest_fill_namespace(MqttDiscoveryManifest& manifest) {
  mqtt_normalize_topic_root(
    mqtt_discovery_prefix, "homeassistant",
    manifest.discovery_prefix, sizeof(manifest.discovery_prefix));
  mqtt_normalize_topic_root(
    mqtt_base_topic, "open-fume-extractor",
    manifest.base_topic, sizeof(manifest.base_topic));
  strncpy(manifest.device_id, master_device_id, sizeof(manifest.device_id) - 1);
  manifest.device_id[sizeof(manifest.device_id) - 1] = 0;
}

static bool mqtt_manifest_valid(const MqttDiscoveryManifest& manifest) {
  if (manifest.magic != MQTT_HA_MANIFEST_MAGIC) return false;
  if (manifest.version != MQTT_HA_MANIFEST_VERSION) return false;
  if (manifest.struct_size != sizeof(MqttDiscoveryManifest)) return false;
  if (manifest.module_count > ModuleRegistry::MAX_MODULES) return false;
  for (uint8_t i = 0; i < manifest.module_count; ++i) {
    if (!manifest.modules[i].addr) return false;
    if (manifest.modules[i].entity_count > MQTT_HA_MANIFEST_MAX_ENTITIES) return false;
  }
  return true;
}

static void mqtt_manifest_load_once() {
  if (mqtt_manifest_load_attempted) return;
  mqtt_manifest_load_attempted = true;
  if (!mqtt_manifest_ensure_storage()) return;

  Preferences prefs;
  if (!prefs.begin("mqtt-ha", true)) return;
  const size_t stored_len = prefs.getBytesLength("manifest");
  if (stored_len == sizeof(MqttDiscoveryManifest)) {
    const size_t got = prefs.getBytes(
      "manifest", mqtt_manifest_previous, sizeof(MqttDiscoveryManifest));
    mqtt_manifest_previous_valid =
      got == sizeof(MqttDiscoveryManifest) &&
      mqtt_manifest_valid(*mqtt_manifest_previous);
  }
  prefs.end();

  if (!mqtt_manifest_previous_valid) {
    memset(mqtt_manifest_previous, 0, sizeof(MqttDiscoveryManifest));
  }
}

static bool mqtt_manifest_save_current() {
  if (!mqtt_manifest_current || !mqtt_manifest_valid(*mqtt_manifest_current)) return false;

  // If the intended entity set is byte-identical, do not burn an unnecessary
  // NVS write just because discovery was republished after reconnect.
  if (mqtt_manifest_previous_valid &&
      memcmp(
        mqtt_manifest_previous,
        mqtt_manifest_current,
        sizeof(MqttDiscoveryManifest)) == 0) {
    return true;
  }

  Preferences prefs;
  if (!prefs.begin("mqtt-ha", false)) return false;
  const size_t written = prefs.putBytes(
    "manifest", mqtt_manifest_current, sizeof(MqttDiscoveryManifest));
  prefs.end();
  if (written != sizeof(MqttDiscoveryManifest)) return false;

  memcpy(
    mqtt_manifest_previous,
    mqtt_manifest_current,
    sizeof(MqttDiscoveryManifest));
  mqtt_manifest_previous_valid = true;
  ++mqtt_manifest_save_total;
  return true;
}

static uint8_t mqtt_manifest_component_code(const char* component) {
  if (!component) return MQTT_MAN_COMP_NONE;
  if (!strcmp(component, "sensor")) return MQTT_MAN_COMP_SENSOR;
  if (!strcmp(component, "binary_sensor")) return MQTT_MAN_COMP_BINARY_SENSOR;
  if (!strcmp(component, "switch")) return MQTT_MAN_COMP_SWITCH;
  if (!strcmp(component, "number")) return MQTT_MAN_COMP_NUMBER;
  if (!strcmp(component, "select")) return MQTT_MAN_COMP_SELECT;
  if (!strcmp(component, "button")) return MQTT_MAN_COMP_BUTTON;
  if (!strcmp(component, "text")) return MQTT_MAN_COMP_TEXT;
  return MQTT_MAN_COMP_NONE;
}

static const char* mqtt_manifest_component_text(uint8_t component) {
  switch (component) {
    case MQTT_MAN_COMP_SENSOR: return "sensor";
    case MQTT_MAN_COMP_BINARY_SENSOR: return "binary_sensor";
    case MQTT_MAN_COMP_SWITCH: return "switch";
    case MQTT_MAN_COMP_NUMBER: return "number";
    case MQTT_MAN_COMP_SELECT: return "select";
    case MQTT_MAN_COMP_BUTTON: return "button";
    case MQTT_MAN_COMP_TEXT: return "text";
    default: return nullptr;
  }
}

// Static suffixes used by mqtt_publish_module_discovery(). Dynamic universal
// entities are stored by their numeric entity id instead.
static uint8_t mqtt_manifest_static_key(const char* suffix) {
  if (!suffix) return 0;
  static const char* keys[] = {
    nullptr,
    "online", "jbc_link", "work", "stand", "in1", "in2", "weller_link",
    "firmware", "cpu", "heap", "uptime", "station", "device_id", "rpm",
    "fault", "filter", "filter_runtime", "filter_time",
    "out1", "out2", "relay_fan", "fan", "light",
    "power", "speed", "brightness", "language", "theme", "screensaver",
    // JBC USB core telemetry. Append-only: stored manifest numeric keys from
    // older releases must keep their meaning.
    "jbc_model", "jbc_family", "jbc_station_error", "jbc_connect_mode",
    "jbc_p1_state", "jbc_p1_error", "jbc_p1_temp_c", "jbc_p1_set_temp_c", "jbc_p1_power_permille", "jbc_p1_flow_permille",
    "jbc_p2_state", "jbc_p2_error", "jbc_p2_temp_c", "jbc_p2_set_temp_c", "jbc_p2_power_permille", "jbc_p2_flow_permille",
    "jbc_p3_state", "jbc_p3_error", "jbc_p3_temp_c", "jbc_p3_set_temp_c", "jbc_p3_power_permille", "jbc_p3_flow_permille",
    "jbc_p4_state", "jbc_p4_error", "jbc_p4_temp_c", "jbc_p4_set_temp_c", "jbc_p4_power_permille", "jbc_p4_flow_permille",
    "jbc_cleaner_mode", "jbc_motors", "jbc_door",
    "jbc_tc1_temp_c", "jbc_tc2_temp_c", "jbc_tc3_temp_c", "jbc_tc4_temp_c",
    "jbc_heater", "jbc_selected_power", "jbc_active_zones",
    "jbc_suction_level", "jbc_continuous", "jbc_intake_work", "jbc_intake_stand",
    "jbc_feeding", "jbc_program", "jbc_speed", "jbc_length", "jbc_tool_enabled",
    // v2 friendly tool entities. Append only.
    "jbc_p1_tool", "jbc_p2_tool", "jbc_p3_tool", "jbc_p4_tool",
    // JBC USB class-specific timers. Append only.
    "jbc_p1_future_mode", "jbc_p1_transition_countdown_s", "jbc_p1_time_to_stop_ds",
    "jbc_p2_future_mode", "jbc_p2_transition_countdown_s", "jbc_p2_time_to_stop_ds",
    "jbc_p3_future_mode", "jbc_p3_transition_countdown_s", "jbc_p3_time_to_stop_ds",
    "jbc_p4_future_mode", "jbc_p4_transition_countdown_s", "jbc_p4_time_to_stop_ds",
    "jbc_time_to_stop"
  };
  for (uint8_t i = 1; i < sizeof(keys) / sizeof(keys[0]); ++i) {
    if (!strcmp(keys[i], suffix)) return i;
  }
  return 0;
}

static const char* mqtt_manifest_static_suffix(uint8_t key) {
  static const char* keys[] = {
    nullptr,
    "online", "jbc_link", "work", "stand", "in1", "in2", "weller_link",
    "firmware", "cpu", "heap", "uptime", "station", "device_id", "rpm",
    "fault", "filter", "filter_runtime", "filter_time",
    "out1", "out2", "relay_fan", "fan", "light",
    "power", "speed", "brightness", "language", "theme", "screensaver",
    // JBC USB core telemetry. Append-only: stored manifest numeric keys from
    // older releases must keep their meaning.
    "jbc_model", "jbc_family", "jbc_station_error", "jbc_connect_mode",
    "jbc_p1_state", "jbc_p1_error", "jbc_p1_temp_c", "jbc_p1_set_temp_c", "jbc_p1_power_permille", "jbc_p1_flow_permille",
    "jbc_p2_state", "jbc_p2_error", "jbc_p2_temp_c", "jbc_p2_set_temp_c", "jbc_p2_power_permille", "jbc_p2_flow_permille",
    "jbc_p3_state", "jbc_p3_error", "jbc_p3_temp_c", "jbc_p3_set_temp_c", "jbc_p3_power_permille", "jbc_p3_flow_permille",
    "jbc_p4_state", "jbc_p4_error", "jbc_p4_temp_c", "jbc_p4_set_temp_c", "jbc_p4_power_permille", "jbc_p4_flow_permille",
    "jbc_cleaner_mode", "jbc_motors", "jbc_door",
    "jbc_tc1_temp_c", "jbc_tc2_temp_c", "jbc_tc3_temp_c", "jbc_tc4_temp_c",
    "jbc_heater", "jbc_selected_power", "jbc_active_zones",
    "jbc_suction_level", "jbc_continuous", "jbc_intake_work", "jbc_intake_stand",
    "jbc_feeding", "jbc_program", "jbc_speed", "jbc_length", "jbc_tool_enabled",
    // v2 friendly tool entities. Append only.
    "jbc_p1_tool", "jbc_p2_tool", "jbc_p3_tool", "jbc_p4_tool",
    // JBC USB class-specific timers. Append only.
    "jbc_p1_future_mode", "jbc_p1_transition_countdown_s", "jbc_p1_time_to_stop_ds",
    "jbc_p2_future_mode", "jbc_p2_transition_countdown_s", "jbc_p2_time_to_stop_ds",
    "jbc_p3_future_mode", "jbc_p3_transition_countdown_s", "jbc_p3_time_to_stop_ds",
    "jbc_p4_future_mode", "jbc_p4_transition_countdown_s", "jbc_p4_time_to_stop_ds",
    "jbc_time_to_stop"
  };
  return key < sizeof(keys) / sizeof(keys[0]) ? keys[key] : nullptr;
}

static bool mqtt_manifest_encode_entity(
    const char* component,
    const char* suffix,
    MqttManifestEntity& entity) {
  entity = MqttManifestEntity();
  entity.component = mqtt_manifest_component_code(component);
  if (!entity.component || !suffix || !suffix[0]) return false;

  if (!strncmp(suffix, "entity_", 7)) {
    char* end = nullptr;
    const unsigned long id = strtoul(suffix + 7, &end, 10);
    if (end && *end == 0 && id >= 20UL && id <= 255UL) {
      entity.kind = MQTT_MAN_ENTITY_DYNAMIC;
      entity.key = (uint8_t)id;
      return true;
    }
  }

  entity.kind = MQTT_MAN_ENTITY_STATIC;
  entity.key = mqtt_manifest_static_key(suffix);
  return entity.key != 0;
}

static bool mqtt_manifest_entity_equal(
    const MqttManifestEntity& a,
    const MqttManifestEntity& b) {
  return a.component == b.component && a.kind == b.kind && a.key == b.key;
}

static MqttManifestModule* mqtt_manifest_find_module(
    MqttDiscoveryManifest& manifest,
    uint8_t addr) {
  for (uint8_t i = 0; i < manifest.module_count; ++i) {
    if (manifest.modules[i].addr == addr) return &manifest.modules[i];
  }
  return nullptr;
}

static const MqttManifestModule* mqtt_manifest_find_module_const(
    const MqttDiscoveryManifest& manifest,
    uint8_t addr) {
  for (uint8_t i = 0; i < manifest.module_count; ++i) {
    if (manifest.modules[i].addr == addr) return &manifest.modules[i];
  }
  return nullptr;
}

static MqttManifestModule* mqtt_manifest_note_module(uint8_t addr) {
  if (!mqtt_manifest_capture_active || !mqtt_manifest_current || !addr) return nullptr;
  if (MqttManifestModule* existing =
        mqtt_manifest_find_module(*mqtt_manifest_current, addr)) {
    return existing;
  }
  if (mqtt_manifest_current->module_count >= ModuleRegistry::MAX_MODULES) return nullptr;

  MqttManifestModule& module =
    mqtt_manifest_current->modules[mqtt_manifest_current->module_count++];
  memset(&module, 0, sizeof(module));
  module.addr = addr;
  return &module;
}

static void mqtt_manifest_note_entity(
    uint8_t addr,
    const char* component,
    const char* suffix) {
  if (!mqtt_manifest_capture_active) return;

  MqttManifestEntity entity;
  if (!mqtt_manifest_encode_entity(component, suffix, entity)) {
    ++mqtt_manifest_untracked_total;
    return;
  }

  MqttManifestModule* module = mqtt_manifest_note_module(addr);
  if (!module) {
    ++mqtt_manifest_untracked_total;
    return;
  }

  for (uint8_t i = 0; i < module->entity_count; ++i) {
    if (mqtt_manifest_entity_equal(module->entities[i], entity)) return;
  }

  if (module->entity_count >= MQTT_HA_MANIFEST_MAX_ENTITIES) {
    ++mqtt_manifest_untracked_total;
    return;
  }
  module->entities[module->entity_count++] = entity;
}

static bool mqtt_manifest_module_contains(
    const MqttManifestModule* module,
    const MqttManifestEntity& entity) {
  if (!module) return false;
  for (uint8_t i = 0; i < module->entity_count; ++i) {
    if (mqtt_manifest_entity_equal(module->entities[i], entity)) return true;
  }
  return false;
}

static void mqtt_manifest_begin_capture() {
  mqtt_manifest_load_once();
  if (!mqtt_manifest_ensure_storage()) return;

  memset(mqtt_manifest_current, 0, sizeof(MqttDiscoveryManifest));
  mqtt_manifest_current->magic = MQTT_HA_MANIFEST_MAGIC;
  mqtt_manifest_current->struct_size = sizeof(MqttDiscoveryManifest);
  mqtt_manifest_current->version = MQTT_HA_MANIFEST_VERSION;
  mqtt_manifest_fill_namespace(*mqtt_manifest_current);
  mqtt_manifest_capture_active = true;
}

static String mqtt_discovery_topic_for_namespace(
    const char* prefix,
    const char* component,
    const char* object,
    const char* suffix) {
  String topic = prefix && prefix[0] ? String(prefix) : String("homeassistant");
  topic += '/';
  topic += component;
  topic += '/';
  topic += object;
  topic += '/';
  topic += suffix;
  topic += F("/config");
  return topic;
}

static String mqtt_topic_for_base_namespace(
    const char* base,
    uint8_t addr,
    const char* leaf) {
  String topic = base && base[0] ? String(base) : String("open-fume-extractor");
  char path[40];
  snprintf(path, sizeof(path), "/module/%02X/%s", addr, leaf);
  topic += path;
  return topic;
}

static bool mqtt_manifest_clear_discovery_entity(
    const MqttDiscoveryManifest& old_manifest,
    uint8_t addr,
    const MqttManifestEntity& entity) {
  const char* component = mqtt_manifest_component_text(entity.component);
  if (!component) return true;

  char suffix[24];
  if (entity.kind == MQTT_MAN_ENTITY_DYNAMIC) {
    snprintf(suffix, sizeof(suffix), "entity_%u", entity.key);
  } else {
    const char* static_suffix = mqtt_manifest_static_suffix(entity.key);
    if (!static_suffix) return true;
    strncpy(suffix, static_suffix, sizeof(suffix) - 1);
    suffix[sizeof(suffix) - 1] = 0;
  }

  char full_suffix[40];
  snprintf(full_suffix, sizeof(full_suffix), "mod_%02X_%s", addr, suffix);
  const String topic = mqtt_discovery_topic_for_namespace(
    old_manifest.discovery_prefix,
    component,
    old_manifest.device_id,
    full_suffix);

  if (!mqtt_client.publish(topic.c_str(), "", true)) {
    ++mqtt_manifest_delete_failed;
    return false;
  }

  ++mqtt_manifest_delete_total;
  ++mqtt_manifest_last_delete_count;
  return true;
}

static bool mqtt_manifest_clear_old_module_value(
    const MqttDiscoveryManifest& old_manifest,
    uint8_t addr,
    const char* leaf) {
  const String topic = mqtt_topic_for_base_namespace(
    old_manifest.base_topic, addr, leaf);
  if (!mqtt_client.publish(topic.c_str(), "", true)) {
    ++mqtt_manifest_delete_failed;
    return false;
  }
  ++mqtt_manifest_delete_total;
  ++mqtt_manifest_last_delete_count;
  return true;
}

static uint32_t mqtt_manifest_count_stale_entities() {
  if (!mqtt_manifest_previous_valid ||
      !mqtt_manifest_previous ||
      !mqtt_manifest_current) return 0;

  uint32_t stale = 0;
  const bool base_changed =
    strcmp(
      mqtt_manifest_previous->base_topic,
      mqtt_manifest_current->base_topic) != 0;

  for (uint8_t mi = 0; mi < mqtt_manifest_previous->module_count; ++mi) {
    const MqttManifestModule& old_module =
      mqtt_manifest_previous->modules[mi];
    const MqttManifestModule* current_module =
      mqtt_manifest_find_module_const(*mqtt_manifest_current, old_module.addr);

    for (uint8_t ei = 0; ei < old_module.entity_count; ++ei) {
      if (!mqtt_manifest_module_contains(current_module, old_module.entities[ei])) {
        ++stale;
      }
    }

    // If an address vanished from the registry completely, its old retained
    // module state/status can be deleted too. Offline remembered modules remain
    // in the current manifest with zero discovery entities, so their offline
    // state is intentionally retained.
    if (base_changed ||
        (!current_module && !mqtt_registry_has_addr(old_module.addr))) {
      stale += 2;
    }
  }
  return stale;
}

static void mqtt_manifest_finish_cleanup() {
  mqtt_manifest_cleanup_active = false;
  mqtt_manifest_cleanup_module = 0;
  mqtt_manifest_cleanup_entity = 0;
  mqtt_manifest_cleanup_stage = 0;
  mqtt_manifest_save_current();
}

static void mqtt_manifest_end_capture_and_reconcile() {
  mqtt_manifest_capture_active = false;
  if (!mqtt_manifest_current || !mqtt_manifest_valid(*mqtt_manifest_current)) return;

  mqtt_manifest_last_delete_count = 0;

  // Migration from v1.7.39: that release already performed the exhaustive
  // legacy cleanup. Seed the exact manifest once, then all future reconciliation
  // is differential.
  if (!mqtt_manifest_previous_valid) {
    if (mqtt_manifest_save_current()) ++mqtt_manifest_seed_total;
    mqtt_manifest_last_stale_count = 0;
    mqtt_manifest_cleanup_active = false;
    return;
  }

  mqtt_manifest_last_stale_count = mqtt_manifest_count_stale_entities();
  if (!mqtt_manifest_last_stale_count) {
    mqtt_manifest_cleanup_active = false;
    mqtt_manifest_save_current();
    return;
  }

  mqtt_manifest_cleanup_module = 0;
  mqtt_manifest_cleanup_entity = 0;
  mqtt_manifest_cleanup_stage = 0;
  mqtt_manifest_cleanup_active = true;
  ++mqtt_manifest_cleanup_cycles;
}

static String mqtt_config_url() {
  if (WiFi.status() == WL_CONNECTED) {
    String url = F("http://");
    url += WiFi.localIP().toString();
    url += F("/");
    return url;
  }
  String url = F("http://");
  url += master_hostname;
  url += F(".local/");
  return url;
}

static String mqtt_device_json() {
  String ids = master_device_id;
  String json = F("{\"ids\":[\"");
  json += ids;
  json += F("\"],\"name\":\"0x01 Open Fume Extractor Master\",\"mf\":\"IceCube20\",\"mdl\":\"Open Fume Extractor Master\",\"sw\":\"");
  json += MASTER_FW_VERSION;
  json += F("\",\"cu\":\"");
  json += mqtt_config_url();
  json += F("\"}");
  return json;
}

static String mqtt_module_device_json(const ModuleRecord& m) {
  String ids = master_device_id;
  ids += F("_mod_");
  if (m.addr < 0x10) ids += '0';
  String hxAddr = String(m.addr, HEX);
  hxAddr.toUpperCase();
  ids += hxAddr;
  String mqttName = F("0x");
  mqttName += hxAddr;
  mqttName += ' ';
  mqttName += module_display_name(m);
  String json = F("{\"ids\":[\"");
  json += ids;
  json += F("\"],\"name\":\"");
  json += json_escape(mqttName.c_str());
  json += F("\",\"mf\":\"IceCube20\",\"mdl\":\"");
  json += module_type_name_for(m);
  json += F("\",\"sw\":\"");
  json += mqtt_fw_string(m);
  json += F("\",\"via_device\":\"");
  json += master_device_id;
  json += F("\",\"cu\":\"");
  json += mqtt_config_url();
  json += F("\"}");
  return json;
}
static const char* mqtt_txt(const char* de, const char* en) {
  return web_text(de, en);
}

static const char* mqtt_suction_name(uint8_t level) {
  switch (level) {
    case 0: return mqtt_txt("Hoch", "High");
    case 1: return mqtt_txt("Mittel", "Medium");
    case 2: return mqtt_txt("Niedrig", "Low");
    default: return mqtt_txt("Benutzer", "Custom");
  }
}

static const char* mqtt_state_text(int state) {
  switch (state) {
    case 0: return mqtt_txt("verbunden", "connected");
    case -1: return mqtt_txt("getrennt", "disconnected");
    case -2: return mqtt_txt("Verbindung fehlgeschlagen", "connect failed");
    case -3: return mqtt_txt("Verbindung verloren", "connection lost");
    case -4: return mqtt_txt("Timeout", "timeout");
    case 1: return mqtt_txt("falsches Protokoll", "bad protocol");
    case 2: return mqtt_txt("falsche Client-ID", "bad client id");
    case 3: return mqtt_txt("Server nicht verfügbar", "server unavailable");
    case 4: return mqtt_txt("falsche Zugangsdaten", "bad credentials");
    case 5: return mqtt_txt("nicht autorisiert", "not authorized");
    default: return mqtt_txt("unbekannt", "unknown");
  }
}

static void mqtt_publish_discovery_entity(const char* component, const char* suffix, const char* name,
                                          const char* value_template, const char* unit = nullptr,
                                          const char* device_class = nullptr,
                                          const char* state_class = nullptr,
                                          bool binary = false) {
  if (!mqtt_ha_discovery || !mqtt_client.connected()) return;
  String object = master_device_id;
  object += "_";
  object += suffix;
  String payload = F("{\"name\":\"");
  payload += name;
  payload += F("\",\"uniq_id\":\"");
  payload += object;
  payload += F("\",\"stat_t\":\"");
  payload += mqtt_topic_path("state");
  payload += F("\",\"avty_t\":\"");
  payload += mqtt_topic_path("status");
  payload += F("\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"val_tpl\":\"");
  payload += value_template;
  payload += F("\"");
  if (binary) {
    payload += F(",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"");
  }
  if (unit && unit[0]) {
    payload += F(",\"unit_of_meas\":\"");
    payload += unit;
    payload += F("\"");
  }
  if (device_class && device_class[0]) {
    payload += F(",\"dev_cla\":\"");
    payload += device_class;
    payload += F("\"");
  }
  if (state_class && state_class[0]) {
    payload += F(",\"stat_cla\":\"");
    payload += state_class;
    payload += F("\"");
  }
  payload += F(",\"dev\":");
  payload += mqtt_device_json();
  payload += F("}");
  if (!mqtt_client.publish(mqtt_discovery_topic(component, master_device_id, suffix).c_str(), payload.c_str(), true))
    mqtt_discovery_publish_failed = true;
}

static void mqtt_publish_discovery_control(const char* component, const char* suffix, const char* name,
                                           const char* value_template, const char* command_leaf,
                                           const char* extra_json) {
  if (!mqtt_ha_discovery || !mqtt_client.connected()) return;
  String object = master_device_id;
  object += "_";
  object += suffix;
  String payload = F("{\"name\":\"");
  payload += name;
  payload += F("\",\"uniq_id\":\"");
  payload += object;
  payload += F("\",\"stat_t\":\"");
  payload += mqtt_topic_path("state");
  payload += F("\",\"cmd_t\":\"");
  payload += mqtt_topic_path(command_leaf);
  payload += F("\",\"avty_t\":\"");
  payload += mqtt_topic_path("status");
  payload += F("\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"val_tpl\":\"");
  payload += value_template;
  payload += F("\"");
  if (extra_json && extra_json[0]) payload += extra_json;
  payload += F(",\"dev\":");
  payload += mqtt_device_json();
  payload += F("}");
  if (!mqtt_client.publish(mqtt_discovery_topic(component, master_device_id, suffix).c_str(), payload.c_str(), true))
    mqtt_discovery_publish_failed = true;
}


static String mqtt_module_topic(uint8_t addr, const char* leaf) {
  char path[40];
  snprintf(path, sizeof(path), "module/%02X/%s", addr, leaf ? leaf : "state");
  return mqtt_topic_path(path);
}

static String mqtt_module_command_leaf(uint8_t addr, const char* action) {
  char path[48];
  snprintf(path, sizeof(path), "cmd/module/%02X/%s", addr, action ? action : "set");
  return String(path);
}

static String mqtt_fw_string(const ModuleRecord& m) {
  String fw;
  fw += m.fw_major;
  fw += '.';
  fw += m.fw_minor;
  fw += '.';
  fw += m.fw_patch;
  if (m.fw_suffix[0]) fw += m.fw_suffix;
  return fw;
}

static uint8_t mqtt_sorted_module_indices(uint8_t* order, uint8_t max_count) {
  const uint8_t count = registry.count() < max_count ? registry.count() : max_count;
  for (uint8_t i = 0; i < count; ++i) order[i] = i;
  for (uint8_t i = 1; i < count; ++i) {
    const uint8_t key = order[i];
    const uint8_t key_addr = registry.at(key).addr;
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && registry.at(order[j]).addr > key_addr) {
      order[j + 1] = order[j];
      --j;
    }
    order[j + 1] = key;
  }
  return count;
}
static const char* mqtt_weller_filter_text(uint8_t status) {
  if (status == 1) return mqtt_txt("sehr gut", "very good");
  if (status == 10) return mqtt_txt("bald wechseln", "change soon");
  if (status == 100) return mqtt_txt("Filter wechseln", "change filter");
  return mqtt_txt("unbekannt", "unknown");
}

static String mqtt_io_input_label(const ModuleRecord& m, uint8_t bit) {
  const char* alias = bit ? m.io_in2_alias : m.io_in1_alias;
  if (alias && alias[0]) return String(alias);
  return String(bit ? "IN2" : "IN1");
}

static bool mqtt_universal_role_matches(const MqttUniversalEntityDef& def, const char* a, const char* b = nullptr) {
  String role = def.role;
  role.toLowerCase();
  return role == a || (b && role == b);
}

static bool mqtt_universal_input_def(const MqttUniversalEntityDef& def) {
  if (!def.valid || def.id < 20) return false;
  String type = def.type;
  String mode = def.mode;
  type.toLowerCase();
  mode.toLowerCase();
  const bool readable_bool = (type == "binary_sensor" || type == "switch") && mode.indexOf('r') >= 0;
  return readable_bool && mqtt_universal_role_matches(def, "main_input", "input");
}

static bool mqtt_universal_output_def(const MqttUniversalEntityDef& def) {
  if (!def.valid || def.id < 20) return false;
  String role = def.role;
  role.toLowerCase();
  return role == "main_output_enable" || role == "main_output_power" || role == "output_enable" || role == "output_power" || role == "output";
}

static bool mqtt_module_provides_extractor_output(const ModuleRecord& m) {
  if (!m.online) return false;
  if (m.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT)) return true;
  if ((m.type != MODULE_UNIVERSAL_RS232 && m.type != MODULE_MODBUS_RTU) ||
      !(m.caps & CAP_ENTITY_CONTROL) || !m.universal_descriptor_valid) return false;
  bool has_enable = false;
  bool has_power = false;
  const char* scan = m.universal_descriptor;
  while (scan && *scan) {
    const char* next = strchr(scan, '\n');
    char line[1024];
    size_t len = next ? (size_t)(next - scan) : strlen(scan);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, scan, len);
    line[len] = 0;
    MqttUniversalEntityDef def;
    if (mqtt_parse_universal_descriptor_line(line, def) && mqtt_universal_output_def(def)) {
      String type = def.type;
      String role = def.role;
      type.toLowerCase();
      role.toLowerCase();
      if (type == "switch" && (role == "main_output_enable" || role == "output_enable" || role == "output")) has_enable = true;
      if (type == "number" && (role == "main_output_power" || role == "output_power")) has_power = true;
      if (has_enable || has_power) return true;
    }
    scan = next ? next + 1 : nullptr;
  }
  return false;
}

static void mqtt_append_option(String& extra, const String& option) {
  extra += '"';
  extra += json_escape(option.c_str());
  extra += '"';
}

static String mqtt_route_input_code(uint8_t source_type, uint8_t source_addr, uint8_t source_bit) {
  return String(source_type) + "," + String(source_addr) + "," + String(source_bit);
}

static bool mqtt_parse_route_input_code(const String& value, uint8_t& source_type, uint8_t& source_addr, uint8_t& source_bit) {
  String v = value;
  v.trim();
  if (!v.length()) return false;
  if (v == "0") {
    source_type = MasterScheduler::INPUT_SRC_NONE;
    source_addr = 0;
    source_bit = 0;
    return true;
  }
  int p1 = v.indexOf(':');
  int p2 = p1 >= 0 ? v.indexOf(':', p1 + 1) : -1;
  if (p1 < 0 || p2 < 0) {
    p1 = v.indexOf(',');
    p2 = p1 >= 0 ? v.indexOf(',', p1 + 1) : -1;
  }
  if (p1 < 0 || p2 < 0) return false;
  source_type = (uint8_t)strtoul(v.substring(0, p1).c_str(), nullptr, 0);
  source_addr = (uint8_t)strtoul(v.substring(p1 + 1, p2).c_str(), nullptr, 0);
  source_bit = (uint8_t)strtoul(v.substring(p2 + 1).c_str(), nullptr, 0);
  return source_type <= MasterScheduler::INPUT_SRC_UNIVERSAL_ENTITY;
}
static String mqtt_route_addr_label(uint8_t addr) {
  if (!addr) return String();
  const ModuleRecord* m = registry.find(addr);
  if (m) return module_addr_display_name(*m);
  char buf[5];
  snprintf(buf, sizeof(buf), "0x%02X", addr);
  return String(buf);
}

static String mqtt_unavailable_route_label(const char* de, const char* en, uint8_t addr) {
  String out = mqtt_txt(de, en);
  String detail = mqtt_route_addr_label(addr);
  if (detail.length()) {
    out += " (";
    out += detail;
    out += ")";
  }
  return out;
}

static bool mqtt_has_online_jbc_modules() {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (m.online && (m.caps & CAP_JBC_ACTIVITY)) return true;
  }
  return false;
}
static String mqtt_main_input_label(uint8_t source_type, uint8_t source_addr, uint8_t source_bit) {
  if (source_type == MasterScheduler::INPUT_SRC_NONE) return mqtt_txt("Kein Eingang", "No input");
  if (source_type == MasterScheduler::INPUT_SRC_JBC_WORK) {
    if (!source_addr) return mqtt_has_online_jbc_modules() ? mqtt_txt("Alle JBC-Module", "All JBC modules") : mqtt_txt("Kein Eingang (Alle JBC-Module)", "No input (All JBC modules)");
    const ModuleRecord* m = registry.find(source_addr);
    return (m && m->online && (m->caps & CAP_JBC_ACTIVITY)) ? module_addr_display_name(*m) : mqtt_unavailable_route_label("Kein Eingang", "No input", source_addr);
  }
  if (source_type == MasterScheduler::INPUT_SRC_IO_INPUT) {
    const ModuleRecord* m = registry.find(source_addr);
    if (!m || !m->online || !(m->caps & CAP_INPUT_KEYS)) {
      String out = mqtt_unavailable_route_label("Kein Eingang", "No input", source_addr);
      if (m) { out += " - "; out += mqtt_io_input_label(*m, source_bit); }
      return out;
    }
    String out = module_addr_display_name(*m);
    out += " - ";
    out += mqtt_io_input_label(*m, source_bit);
    return out;
  }
  if (source_type == MasterScheduler::INPUT_SRC_UNIVERSAL_ENTITY) {
    const ModuleRecord* m = registry.find(source_addr);
    String suffix;
    if (m && m->universal_descriptor_valid) {
      const char* scan = m->universal_descriptor;
      while (scan && *scan) {
        const char* next = strchr(scan, '\n');
        char line[1024];
        size_t len = next ? (size_t)(next - scan) : strlen(scan);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, scan, len);
        line[len] = 0;
        MqttUniversalEntityDef def;
        if (mqtt_parse_universal_descriptor_line(line, def) && def.id == source_bit) {
          suffix = def.label.length() ? def.label : def.key;
          break;
        }
        scan = next ? next + 1 : nullptr;
      }
    }
    if (!suffix.length()) suffix = String("Entity ") + String(source_bit);
    if (!m || !m->online || !(m->type == MODULE_UNIVERSAL_RS232 || m->type == MODULE_MODBUS_RTU) || !m->universal_descriptor_valid) {
      String out = mqtt_unavailable_route_label("Kein Eingang", "No input", source_addr);
      out += " - ";
      out += suffix;
      return out;
    }
    String out = module_addr_display_name(*m);
    out += " - ";
    out += suffix;
    return out;
  }
  return mqtt_txt("Kein Eingang", "No input");
}

static String mqtt_main_output_unavailable_label(uint8_t addr) {
  return mqtt_unavailable_route_label("Kein Ausgang", "No output", addr);
}

static String mqtt_main_output_label(uint8_t addr) {
  if (!addr) {
    String out = String("Auto");
    const uint8_t effective = scheduler.autoOutputCandidateAddr();
    if (effective) {
      out += " (";
      out += mqtt_route_addr_label(effective);
      out += ")";
    } else {
      out += " (";
      out += mqtt_main_output_unavailable_label();
      out += ")";
    }
    return out;
  }
  const ModuleRecord* m = registry.find(addr);
  return (m && m->online && mqtt_module_provides_extractor_output(*m)) ? module_addr_display_name(*m) : mqtt_main_output_unavailable_label(addr);
}

static String mqtt_main_input_options_extra() {
  String extra = F(",\"options\":[");
  bool first = true;
  auto add = [&](const String& option) {
    if (!first) extra += ',';
    first = false;
    mqtt_append_option(extra, option);
  };
  add(mqtt_main_input_label(MasterScheduler::INPUT_SRC_NONE, 0, 0));
  if (mqtt_has_online_jbc_modules()) add(mqtt_main_input_label(MasterScheduler::INPUT_SRC_JBC_WORK, 0, 0));
  uint8_t order[ModuleRegistry::MAX_MODULES];
  const uint8_t count = mqtt_sorted_module_indices(order, sizeof(order));
  for (uint8_t oi = 0; oi < count; ++oi) {
    const ModuleRecord& m = registry.at(order[oi]);
    if (!m.online) continue;
    if (m.caps & CAP_JBC_ACTIVITY) add(mqtt_main_input_label(MasterScheduler::INPUT_SRC_JBC_WORK, m.addr, 0));
    if (m.caps & CAP_INPUT_KEYS) {
      add(mqtt_main_input_label(MasterScheduler::INPUT_SRC_IO_INPUT, m.addr, 0));
      add(mqtt_main_input_label(MasterScheduler::INPUT_SRC_IO_INPUT, m.addr, 1));
    }
    if ((m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) && m.universal_descriptor_valid) {
      const char* scan = m.universal_descriptor;
      while (scan && *scan) {
        const char* next = strchr(scan, '\n');
        char line[1024];
        size_t len = next ? (size_t)(next - scan) : strlen(scan);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, scan, len);
        line[len] = 0;
        MqttUniversalEntityDef def;
        if (mqtt_parse_universal_descriptor_line(line, def) && mqtt_universal_input_def(def)) add(mqtt_main_input_label(MasterScheduler::INPUT_SRC_UNIVERSAL_ENTITY, m.addr, def.id));
        scan = next ? next + 1 : nullptr;
      }
    }
  }
  const String current = mqtt_main_input_label(
    scheduler.mainInputSourceType(),
    scheduler.mainInputSourceAddr(),
    scheduler.mainInputSourceBit());
  bool current_present = false;
  if (current.length()) {
    String scan_options = extra;
    for (int pos = 0; pos >= 0 && !current_present;) {
      pos = scan_options.indexOf('\"', pos);
      if (pos < 0) break;
      int end = scan_options.indexOf('\"', pos + 1);
      if (end < 0) break;
      if (scan_options.substring(pos + 1, end) == current) current_present = true;
      pos = end + 1;
    }
    if (!current_present) add(current);
  }
  extra += F("],\"icon\":\"mdi:source-branch\"");
  return extra;
}

static String mqtt_main_output_options_extra() {
  String extra = F(",\"options\":[");
  bool first = true;
  auto add = [&](const String& option) {
    if (!first) extra += ',';
    first = false;
    mqtt_append_option(extra, option);
  };
  add(mqtt_main_output_label(0));
  const uint8_t preferred = scheduler.preferredOutputAddr();
  if (preferred) {
    const ModuleRecord* pm = registry.find(preferred);
    if (!pm || !pm->online || !mqtt_module_provides_extractor_output(*pm)) add(mqtt_main_output_unavailable_label(scheduler.preferredOutputAddr()));
  }
  uint8_t order[ModuleRegistry::MAX_MODULES];
  const uint8_t count = mqtt_sorted_module_indices(order, sizeof(order));
  for (uint8_t oi = 0; oi < count; ++oi) {
    const ModuleRecord& m = registry.at(order[oi]);
    if (m.online && mqtt_module_provides_extractor_output(m)) add(mqtt_main_output_label(m.addr));
  }
  extra += F("],\"icon\":\"mdi:fan\"");
  return extra;
}

static String mqtt_route_options_signature_now() {
  String sig;
  sig.reserve(192);
  sig += web_lang;
  sig += '|';
  sig += scheduler.activeOutputMinSelectFlow();
  sig += '|';
  sig += mqtt_main_input_options_extra();
  sig += '|';
  sig += mqtt_main_output_options_extra();
  return sig;
}

static void mqtt_mark_discovery_dirty() {
  mqtt_discovery_published = false;
  mqtt_next_discovery_check_ms = 0;
}

static bool mqtt_find_main_input_by_label(const String& value, uint8_t& source_type, uint8_t& source_addr, uint8_t& source_bit) {
  if (mqtt_parse_route_input_code(value, source_type, source_addr, source_bit)) return true;
  if (value == mqtt_main_input_label(MasterScheduler::INPUT_SRC_NONE, 0, 0)) { source_type = MasterScheduler::INPUT_SRC_NONE; source_addr = 0; source_bit = 0; return true; }
  if (mqtt_has_online_jbc_modules() && value == mqtt_main_input_label(MasterScheduler::INPUT_SRC_JBC_WORK, 0, 0)) { source_type = MasterScheduler::INPUT_SRC_JBC_WORK; source_addr = 0; source_bit = 0; return true; }
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (!m.online) continue;
    if ((m.caps & CAP_JBC_ACTIVITY) && value == mqtt_main_input_label(MasterScheduler::INPUT_SRC_JBC_WORK, m.addr, 0)) { source_type = MasterScheduler::INPUT_SRC_JBC_WORK; source_addr = m.addr; source_bit = 0; return true; }
    if (m.caps & CAP_INPUT_KEYS) {
      for (uint8_t bit = 0; bit < 2; ++bit) if (value == mqtt_main_input_label(MasterScheduler::INPUT_SRC_IO_INPUT, m.addr, bit)) { source_type = MasterScheduler::INPUT_SRC_IO_INPUT; source_addr = m.addr; source_bit = bit; return true; }
    }
    if ((m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) && m.universal_descriptor_valid) {
      const char* scan = m.universal_descriptor;
      while (scan && *scan) {
        const char* next = strchr(scan, '\n');
        char line[1024];
        size_t len = next ? (size_t)(next - scan) : strlen(scan);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, scan, len);
        line[len] = 0;
        MqttUniversalEntityDef def;
        if (mqtt_parse_universal_descriptor_line(line, def) && mqtt_universal_input_def(def) && value == mqtt_main_input_label(MasterScheduler::INPUT_SRC_UNIVERSAL_ENTITY, m.addr, def.id)) { source_type = MasterScheduler::INPUT_SRC_UNIVERSAL_ENTITY; source_addr = m.addr; source_bit = def.id; return true; }
        scan = next ? next + 1 : nullptr;
      }
    }
  }
  return false;
}

static bool mqtt_find_main_output_by_label(const String& value, uint8_t& addr) {
  if (value == mqtt_main_output_label(0) || value == mqtt_main_output_unavailable_label() || value == "0" || value == "Auto" || value.startsWith("Auto (")) { addr = 0; return true; }
  if (value.startsWith("0x") || value.startsWith("0X")) {
    addr = (uint8_t)strtoul(value.c_str(), nullptr, 0);
    return addr == 0 || (addr >= 0x10 && addr <= 0x6F);
  }
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (m.online && mqtt_module_provides_extractor_output(m) && value == mqtt_main_output_label(m.addr)) { addr = m.addr; return true; }
  }
  return false;
}


static bool mqtt_ascii_ci_eq(const String& a, const char* b) {
  return a.equalsIgnoreCase(String(b));
}

static String mqtt_universal_clean_text(String text) {
  text.replace("_", " ");
  text.trim();
  return text;
}

static bool mqtt_descriptor_meta_key_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static const char* mqtt_descriptor_next_meta(const char* start, const char* end) {
  if (!start || !end) return end;
  for (const char* p = start; p < end; ++p) {
    if (*p != ' ') continue;
    const char* q = p + 1;
    if (q >= end || !(((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == '_'))) continue;
    while (q < end && mqtt_descriptor_meta_key_char(*q)) ++q;
    if (q < end && *q == '=') return p;
  }
  return end;
}

static String mqtt_descriptor_meta(const char* line, const char* key) {
  if (!line || !key || !key[0]) return String();
  String needle = String(key) + "=";
  const char* start = nullptr;
  const size_t needle_len = needle.length();
  if (strncmp(line, needle.c_str(), needle_len) == 0) {
    start = line + needle_len;
  } else {
    String token = String(" ") + needle;
    const char* hit = strstr(line, token.c_str());
    if (!hit) return String();
    start = hit + token.length();
  }
  const char* line_end = line + strlen(line);
  const char* end = mqtt_descriptor_next_meta(start, line_end);
  String out;
  while (start < end) out += *start++;
  out.trim();
  return out;
}

static String mqtt_span_string(const char* start, const char* end) {
  String out;
  while (start && end && start < end) out += *start++;
  return out;
}

static bool mqtt_parse_universal_descriptor_line(const char* line, MqttUniversalEntityDef& out) {
  out = MqttUniversalEntityDef();
  if (!line) return false;
  while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
  if (*line < '0' || *line > '9') return false;
  char* end = nullptr;
  unsigned long id = strtoul(line, &end, 10);
  if (!end || end == line || id > 255) return false;
  while (*end == ' ' || *end == '\t') ++end;
  const char* type_start = end;
  while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') ++end;
  String type = mqtt_span_string(type_start, end);
  while (*end == ' ' || *end == '\t') ++end;
  const char* key_start = end;
  while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') ++end;
  String key = mqtt_span_string(key_start, end);
  while (*end == ' ' || *end == '\t') ++end;
  const char* mode_start = end;
  while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') ++end;
  String mode = mqtt_span_string(mode_start, end);
  out.valid = id >= 20;
  out.id = (uint8_t)id;
  out.type = type;
  out.key = key;
  out.mode = mode;
  String de = mqtt_descriptor_meta(line, "de");
  String en = mqtt_descriptor_meta(line, "en");
  out.label = mqtt_universal_clean_text(de.length() ? de : (en.length() ? en : key));
  out.unit = mqtt_descriptor_meta(line, "unit");
  out.min_value = mqtt_descriptor_meta(line, "min");
  out.max_value = mqtt_descriptor_meta(line, "max");
  out.step = mqtt_descriptor_meta(line, "step");
  out.options = mqtt_descriptor_meta(line, "options");
  out.values = mqtt_descriptor_meta(line, "values");
  out.role = mqtt_descriptor_meta(line, "role");
  out.access = mqtt_descriptor_meta(line, "access");
  return out.valid;
}

static String mqtt_universal_entity_access(const MqttUniversalEntityDef& def) {
  String mode = def.access.length() ? def.access : def.mode;
  mode.trim();
  mode.toLowerCase();
  if (mode != "ro" && mode != "rw" && mode != "wo") mode = def.mode;
  mode.trim();
  mode.toLowerCase();
  return mode;
}

static bool mqtt_universal_entity_readable(const MqttUniversalEntityDef& def) {
  const String mode = mqtt_universal_entity_access(def);
  return mode == "ro" || mode == "rw";
}

static bool mqtt_universal_entity_writable(const MqttUniversalEntityDef& def) {
  const String mode = mqtt_universal_entity_access(def);
  return mode == "wo" || mode == "rw";
}

static String mqtt_universal_entity_value(const UniversalEntityState& e) {
  if (!e.len) return String();
  bool printable = true;
  for (uint8_t i = 0; i < e.len; ++i) {
    const uint8_t c = e.data[i];
    if (c < 32 || c > 126) { printable = false; break; }
  }
  if (printable) {
    String out;
    for (uint8_t i = 0; i < e.len; ++i) out += (char)e.data[i];
    out.trim();
    return out;
  }
  uint32_t v = 0;
  const uint8_t n = e.len > 4 ? 4 : e.len;
  for (uint8_t i = 0; i < n; ++i) v |= ((uint32_t)e.data[i]) << (8U * i);
  return String(v);
}

static bool mqtt_universal_descriptor_def_for_entity(const ModuleRecord& m, uint8_t entity_id, MqttUniversalEntityDef& out) {
  if (!(m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) || !m.universal_descriptor_valid) return false;
  const char* p = m.universal_descriptor;
  while (p && *p) {
    const char* next = strchr(p, '\n');
    char line[1024];
    size_t len = next ? (size_t)(next - p) : strlen(p);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    if (mqtt_parse_universal_descriptor_line(line, out) && out.id == entity_id) return true;
    p = next ? next + 1 : nullptr;
  }
  return false;
}

static String mqtt_universal_list_token(const String& list, uint8_t index) {
  uint8_t pos = 0;
  uint8_t cur = 0;
  while (pos <= list.length()) {
    int next = list.indexOf('|', pos);
    String item = next >= 0 ? list.substring(pos, next) : list.substring(pos);
    item.trim();
    if (cur == index) return item;
    if (next < 0) break;
    pos = (uint8_t)(next + 1);
    ++cur;
  }
  return String();
}

static String mqtt_universal_select_label(const MqttUniversalEntityDef& def, const String& raw) {
  if (!def.options.length()) return raw;
  const String values = def.values.length() ? def.values : def.options;
  for (uint8_t i = 0; i < 32; ++i) {
    String label = mqtt_universal_list_token(def.options, i);
    String value = mqtt_universal_list_token(values, i);
    if (!label.length() && !value.length()) break;
    if (!value.length()) value = label;
    if (raw == value || raw == label) return label.length() ? label : raw;
    if (raw.length() > value.length() && raw.substring(raw.length() - value.length()) == value) return label.length() ? label : raw;
  }
  return raw;
}

static String mqtt_universal_select_command_value(const MqttUniversalEntityDef& def, const String& selected) {
  if (!def.options.length() || !def.values.length()) return selected;
  for (uint8_t i = 0; i < 32; ++i) {
    String label = mqtt_universal_list_token(def.options, i);
    String value = mqtt_universal_list_token(def.values, i);
    if (!label.length() && !value.length()) break;
    if (!value.length()) value = label;
    if (selected == label || selected == value) return value;
  }
  return selected;
}

static String mqtt_universal_entity_value_for_def(const ModuleRecord& m, const UniversalEntityState& e) {
  MqttUniversalEntityDef def;
  const bool has_def = mqtt_universal_descriptor_def_for_entity(m, e.id, def);
  String raw = mqtt_universal_entity_value(e);
  raw.trim();
  if (!has_def) return raw;
  if (mqtt_ascii_ci_eq(def.type, "switch") || mqtt_ascii_ci_eq(def.type, "binary_sensor")) {
    const bool on = raw == "1" || raw == "ON" || raw == "on" || raw == "true" || raw == "100";
    return on ? String("1") : String("0");
  }
  if (mqtt_ascii_ci_eq(def.type, "select")) return mqtt_universal_select_label(def, raw);
  return raw;
}

static const UniversalEntityState* mqtt_universal_state_by_id(const ModuleRecord& m, uint8_t id) {
  for (uint8_t i = 0; i < m.universal_entity_count && i < ModuleRecord::UNIVERSAL_ENTITY_MAX; ++i) {
    if (m.universal_entities[i].id == id) return &m.universal_entities[i];
  }
  return nullptr;
}

static String mqtt_universal_entity_extra(const MqttUniversalEntityDef& def, const char* component) {
  String extra;
  if (!strcmp(component, "switch") || !strcmp(component, "binary_sensor")) {
    if (!strcmp(component, "switch")) extra += F(",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\"");
    return extra;
  }
  if (!strcmp(component, "number")) {
    extra += F(",\"min\":"); extra += def.min_value.length() ? def.min_value : String(0);
    extra += F(",\"max\":"); extra += def.max_value.length() ? def.max_value : String(100);
    extra += F(",\"step\":"); extra += def.step.length() ? def.step : String(1);
    extra += F(",\"mode\":\"slider\"");
  }
  if (!strcmp(component, "select") && def.options.length()) {
    extra += F(",\"options\":[");
    String opts = def.options;
    opts.replace(";", "|");
    uint16_t pos = 0;
    bool first = true;
    while (pos <= opts.length()) {
      int next = opts.indexOf('|', pos);
      String item = next >= 0 ? opts.substring(pos, next) : opts.substring(pos);
      item.trim();
      if (item.length()) { if (!first) extra += ','; first = false; extra += '"'; extra += json_escape(item.c_str()); extra += '"'; }
      if (next < 0) break;
      pos = (uint16_t)(next + 1);
    }
    extra += ']';
  }
  if (def.unit.length()) { extra += F(",\"unit_of_meas\":\""); extra += json_escape(def.unit.c_str()); extra += '"'; }
  return extra;
}

static void mqtt_clear_module_entity(uint8_t addr, const char* component, const char* suffix);

static void mqtt_publish_universal_descriptor_entities(const ModuleRecord& m, const String& base) {
  if (!(m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) || !m.universal_descriptor_valid) return;
  bool seen_entity_id[256] = {false};
  const char* p = m.universal_descriptor;
  while (p && *p) {
    const char* next = strchr(p, '\n');
    char line[1024];
    size_t len = next ? (size_t)(next - p) : strlen(p);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    MqttUniversalEntityDef def;
    if (mqtt_parse_universal_descriptor_line(line, def)) {
      if (seen_entity_id[def.id]) { p = next ? next + 1 : nullptr; continue; }
      seen_entity_id[def.id] = true;
      const bool readable = mqtt_universal_entity_readable(def);
      const bool writable = mqtt_universal_entity_writable(def);
      const bool is_switch = mqtt_ascii_ci_eq(def.type, "switch");
      const bool is_binary = mqtt_ascii_ci_eq(def.type, "binary_sensor");
      const bool is_number = mqtt_ascii_ci_eq(def.type, "number");
      const bool is_select = mqtt_ascii_ci_eq(def.type, "select");
      const bool is_button = mqtt_ascii_ci_eq(def.type, "button");
      const bool is_sensor = mqtt_ascii_ci_eq(def.type, "sensor");
      const bool is_text = mqtt_ascii_ci_eq(def.type, "text");
      const char* component = "sensor";
      if (is_switch) component = writable ? "switch" : "binary_sensor";
      else if (is_binary) component = "binary_sensor";
      else if (is_number) component = writable ? "number" : "sensor";
      else if (is_select) component = (writable && def.options.length()) ? "select" : "sensor";
      else if (is_button && writable) component = "button";
      else if (is_text && writable) component = "text";
      else if (is_sensor) component = "sensor";
      char suffix[16];
      snprintf(suffix, sizeof(suffix), "entity_%u", def.id);
      String name = base + " " + (def.label.length() ? def.label : def.key);
      String tpl;
      if (!strcmp(component, "switch") || !strcmp(component, "binary_sensor")) tpl = "{{ 'ON' if value_json.entity_" + String(def.id) + " in ['1','ON','on','true','100'] or value_json.entity_" + String(def.id) + " == 1 else 'OFF' }}";
      else tpl = "{{ value_json.entity_" + String(def.id) + " }}";
      String extra = mqtt_universal_entity_extra(def, component);
      String cmd;
      if (writable && strcmp(component, "sensor") && strcmp(component, "binary_sensor")) cmd = mqtt_module_command_leaf(m.addr, suffix);
      // WO switches still need a normal Home Assistant toggle. Publish the
      // master's command shadow as state, but never present it as device
      // readback. With a state_topic HA does not mark the switch as assumed
      // state (lightning / crossed-lightning buttons). Other WO entity types
      // remain command-only.
      const bool wo_switch_shadow = is_switch && writable && !readable;
      // If a profile edit changes the Home Assistant component for this stable
      // entity id, the persisted discovery manifest deletes exactly the old
      // component after the new desired set has been captured.
      const bool stateful = (readable || wo_switch_shadow) && strcmp(component, "button") != 0;
      mqtt_publish_module_entity(m, component, suffix, name, tpl.c_str(), extra.c_str(), cmd, stateful);
      if (is_switch && writable && !readable) {
        // Older experimental WO-switch discovery used button-style controls.
        // Remove those retained topics so HA converges to exactly one shadow-state switch.
        mqtt_clear_module_entity(m.addr, "button", suffix);
        char legacy_on[24];
        char legacy_off[24];
        snprintf(legacy_on, sizeof(legacy_on), "entity_%u_on", def.id);
        snprintf(legacy_off, sizeof(legacy_off), "entity_%u_off", def.id);
        mqtt_clear_module_entity(m.addr, "button", legacy_on);
        mqtt_clear_module_entity(m.addr, "button", legacy_off);
      }
    }
    p = next ? next + 1 : nullptr;
  }
}

static String mqtt_alias_or(const char* alias, const char* fallback) {
  return (alias && alias[0]) ? String(alias) : String(fallback);
}

static uint16_t mqtt_universal_descriptor_entity_fingerprint(const ModuleRecord& m) {
  if (!(m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) || !m.universal_descriptor_valid) return 0;
  uint16_t fp = 0x4F45U;
  const char* p = m.universal_descriptor;
  while (p && *p) {
    const char* next = strchr(p, '\n');
    char line[1024];
    size_t len = next ? (size_t)(next - p) : strlen(p);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    MqttUniversalEntityDef def;
    if (mqtt_parse_universal_descriptor_line(line, def)) {
      fp = (uint16_t)((fp << 5) | (fp >> 11));
      fp ^= ((uint16_t)def.id << 8) ^ (uint16_t)def.type.length() ^ ((uint16_t)def.mode.length() << 3);
      fp ^= (uint16_t)def.key.length() << 1;
      fp ^= (uint16_t)def.label.length() << 4;
    }
    p = next ? next + 1 : nullptr;
  }
  return fp;
}

static uint8_t mqtt_universal_descriptor_entity_count(const ModuleRecord& m) {
  if (!(m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) || !m.universal_descriptor_valid) return 0;
  uint8_t count = 0;
  bool seen_entity_id[256] = {false};
  const char* p = m.universal_descriptor;
  while (p && *p) {
    const char* next = strchr(p, '\n');
    char line[1024];
    size_t len = next ? (size_t)(next - p) : strlen(p);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    MqttUniversalEntityDef def;
    if (mqtt_parse_universal_descriptor_line(line, def) && !seen_entity_id[def.id]) {
      seen_entity_id[def.id] = true;
      if (count < 255) ++count;
    }
    p = next ? next + 1 : nullptr;
  }
  return count;
}

static String mqtt_module_discovery_signature_now() {
  String sig;
  sig.reserve(160);
  sig += F("schema:mqtt_jbc_core_v2|");
  sig += F("lang:");
  sig += web_lang;
  sig += F("|flowmin:");
  sig += scheduler.activeOutputMinSelectFlow();
  sig += '|';
  uint8_t order[ModuleRegistry::MAX_MODULES];
  const uint8_t count = mqtt_sorted_module_indices(order, sizeof(order));
  for (uint8_t oi = 0; oi < count; ++oi) {
    const ModuleRecord& m = registry.at(order[oi]);
    sig += m.addr;
    sig += ':';
    sig += m.type;
    sig += ':';
    sig += m.uid ? uid_hex(m.uid) : String("0");
    sig += ':';
    sig += m.online ? '1' : '0';
    sig += ':';
    sig += m.label[0] ? m.label : m.name;
    sig += ':';
    sig += mqtt_fw_string(m);
    if (m.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_DIGITAL_OUTPUT | CAP_INPUT_KEYS)) {
      sig += ':'; sig += m.io_main_alias;
      sig += ':'; sig += m.io_in1_alias;
      sig += ':'; sig += m.io_in2_alias;
      sig += ':'; sig += m.io_out1_alias;
      sig += ':'; sig += m.io_out2_alias;
    }
    if (m.type == MODULE_JBC_USB || (m.caps & CAP_JBC_USB)) {
      const JbcUsbCoreState core = jbc_usb_core_state(m);
      sig += ':'; sig += m.jbc_usb_model;
      sig += ':'; sig += core.family;
      sig += ':'; sig += core.port_count;
    }
    if (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) {
      sig += ':';
      sig += m.universal_descriptor_crc;
      sig += ':';
      sig += mqtt_universal_descriptor_entity_count(m);
      sig += ':';
      sig += mqtt_universal_descriptor_entity_fingerprint(m);
      sig += ':';
      sig += m.universal_entity_count;
    }
    sig += '|';
  }
  return sig;
}

static void mqtt_publish_discovery_custom(const char* component, const char* suffix, const char* name,
                                          const String& state_topic, const char* value_template,
                                          const String& command_topic, const char* extra_json) {
  if (!mqtt_ha_discovery || !mqtt_client.connected()) return;
  String object = master_device_id;
  object += "_";
  object += suffix;
  String payload = F("{\"name\":\"");
  payload += json_escape(name);
  payload += F("\",\"uniq_id\":\"");
  payload += object;
  payload += F("\",\"stat_t\":\"");
  payload += state_topic;
  if (command_topic.length()) {
    payload += F("\",\"cmd_t\":\"");
    payload += command_topic;
  }
  payload += F("\",\"avty_t\":\"");
  payload += mqtt_topic_path("status");
  payload += F("\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"val_tpl\":\"");
  payload += value_template;
  payload += F("\"");
  if (extra_json && extra_json[0]) payload += extra_json;
  payload += F(",\"dev\":");
  payload += mqtt_device_json();
  payload += F("}");
  mqtt_client.publish(mqtt_discovery_topic(component, master_device_id, suffix).c_str(), payload.c_str(), true);
}

static void mqtt_publish_module_entity(const ModuleRecord& m, const char* component, const char* suffix, const String& name,
                                       const char* value_template, const char* extra_json = nullptr,
                                       const String& command_leaf = String(), bool stateful = true) {
  char full_suffix[40];
  snprintf(full_suffix, sizeof(full_suffix), "mod_%02X_%s", m.addr, suffix);
  String object = master_device_id;
  object += "_";
  object += full_suffix;
  String cmd_topic;
  if (command_leaf.length()) cmd_topic = mqtt_topic_path(command_leaf.c_str());
  String payload = F("{\"name\":\"");
  payload += json_escape(name.c_str());
  payload += F("\",\"uniq_id\":\"");
  payload += object;
  payload += F("\"");
  if (stateful) {
    payload += F(",\"stat_t\":\"");
    payload += mqtt_module_topic(m.addr, "state");
    payload += F("\"");
  }
  if (cmd_topic.length()) {
    payload += F(",\"cmd_t\":\"");
    payload += cmd_topic;
    payload += F("\"");
  }
  payload += F(",\"avty_t\":\"");
  payload += mqtt_module_topic(m.addr, "status");
  payload += F("\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"");
  if (stateful && value_template && value_template[0]) {
    payload += F(",\"val_tpl\":\"");
    payload += value_template;
    payload += F("\"");
  }
  if (extra_json && extra_json[0]) payload += extra_json;
  payload += F(",\"dev\":");
  payload += mqtt_module_device_json(m);
  payload += F("}");

  mqtt_manifest_note_entity(m.addr, component, suffix);
  if (!mqtt_client.publish(mqtt_discovery_topic(component, master_device_id, full_suffix).c_str(), payload.c_str(), true))
    mqtt_discovery_publish_failed = true;
}

static void mqtt_clear_module_entity(uint8_t addr, const char* component, const char* suffix) {
  char full_suffix[40];
  snprintf(full_suffix, sizeof(full_suffix), "mod_%02X_%s", addr, suffix);
  if (!mqtt_client.publish(mqtt_discovery_topic(component, master_device_id, full_suffix).c_str(), "", true))
    mqtt_discovery_publish_failed = true;
}

static void mqtt_clear_module_state(uint8_t addr) {
  // Remove retained module values. Availability is handled separately so stale
  // Home-Assistant entities never stay available when a module disappears.
  mqtt_client.publish(mqtt_module_topic(addr, "state").c_str(), "", true);
}

static void mqtt_publish_module_offline(uint8_t addr) {
  if (!mqtt_client.connected()) return;
  mqtt_client.publish(mqtt_module_topic(addr, "status").c_str(), "offline", true);
  char json[80];
  snprintf(json, sizeof(json), "{\"addr\":%u,\"online\":false}", (unsigned)addr);
  mqtt_client.publish(mqtt_module_topic(addr, "state").c_str(), json, true);
}

static void mqtt_publish_all_availability_offline() {
  if (!mqtt_client.connected()) return;
  mqtt_client.publish(mqtt_topic_path("status").c_str(), "offline", true);
  for (uint8_t i = 0; i < registry.count(); ++i) {
    mqtt_publish_module_offline(registry.at(i).addr);
  }
  mqtt_client.loop();
}

static bool mqtt_registry_has_addr(uint8_t addr) {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    if (registry.at(i).addr == addr) return true;
  }
  return false;
}
static bool mqtt_registry_has_online_addr(uint8_t addr) {
  for (uint8_t i = 0; i < registry.count(); ++i) {
    const ModuleRecord& m = registry.at(i);
    if (m.addr == addr && m.online) return true;
  }
  return false;
}


static void mqtt_cleanup_tick() {
  if (!mqtt_ha_discovery ||
      !mqtt_client.connected() ||
      !mqtt_manifest_cleanup_active ||
      !mqtt_manifest_previous_valid ||
      !mqtt_manifest_previous ||
      !mqtt_manifest_current) {
    return;
  }

  const uint8_t max_topics_per_tick = 10;
  uint8_t sent = 0;

  while (sent < max_topics_per_tick &&
         mqtt_manifest_cleanup_active &&
         mqtt_client.connected()) {
    if (mqtt_manifest_cleanup_module >= mqtt_manifest_previous->module_count) {
      mqtt_manifest_finish_cleanup();
      break;
    }

    const MqttManifestModule& old_module =
      mqtt_manifest_previous->modules[mqtt_manifest_cleanup_module];
    const MqttManifestModule* current_module =
      mqtt_manifest_find_module_const(*mqtt_manifest_current, old_module.addr);

    // Stage 0: delete only discovery entities that really disappeared or
    // changed component/type.
    if (mqtt_manifest_cleanup_stage == 0) {
      if (mqtt_manifest_cleanup_entity < old_module.entity_count) {
        const MqttManifestEntity& old_entity =
          old_module.entities[mqtt_manifest_cleanup_entity];

        if (!mqtt_manifest_module_contains(current_module, old_entity)) {
          if (!mqtt_manifest_clear_discovery_entity(
                *mqtt_manifest_previous, old_module.addr, old_entity)) {
            break; // retry this exact retained clear on the next MQTT tick
          }
          ++sent;
        }

        ++mqtt_manifest_cleanup_entity;
        continue;
      }

      mqtt_manifest_cleanup_stage = 1;
      continue;
    }

    const bool base_changed =
      strcmp(
        mqtt_manifest_previous->base_topic,
        mqtt_manifest_current->base_topic) != 0;
    const bool removed_from_registry =
      !current_module && !mqtt_registry_has_addr(old_module.addr);
    const bool clear_old_values = base_changed || removed_from_registry;

    // Stage 1/2: clear retained module state/status only when the address was
    // actually removed, or when the MQTT base namespace changed. Offline
    // remembered modules deliberately keep their retained offline values.
    if (mqtt_manifest_cleanup_stage == 1) {
      if (clear_old_values) {
        if (!mqtt_manifest_clear_old_module_value(
              *mqtt_manifest_previous, old_module.addr, "state")) {
          break;
        }
        ++sent;
      }
      mqtt_manifest_cleanup_stage = 2;
      continue;
    }

    if (mqtt_manifest_cleanup_stage == 2) {
      if (clear_old_values) {
        if (!mqtt_manifest_clear_old_module_value(
              *mqtt_manifest_previous, old_module.addr, "status")) {
          break;
        }
        ++sent;
      }
      mqtt_manifest_cleanup_stage = 3;
      continue;
    }

    ++mqtt_manifest_cleanup_module;
    mqtt_manifest_cleanup_entity = 0;
    mqtt_manifest_cleanup_stage = 0;
  }
}


static String mqtt_jbc_hex_error(const char* prefix, uint16_t value, uint8_t width = 2) {
  char buf[24];
  if (width <= 2) snprintf(buf, sizeof(buf), "%s0x%02X", prefix, (unsigned)(value & 0xFFU));
  else snprintf(buf, sizeof(buf), "%s0x%04X", prefix, (unsigned)value);
  return String(buf);
}

static String mqtt_jbc_station_error_text(bool valid, uint16_t value) {
  if (!valid) return String("-");
  if (value == 0) return String("OK");
  const char* known = jbc_usb_core_known_station_error_name(value);
  return known ? String(known) : mqtt_jbc_hex_error("ERROR_", value, 4);
}

static String mqtt_jbc_tool_name(uint8_t family, const char* model, uint8_t raw_tool) {
  const uint8_t generic = jbc_usb_core_generic_tool_id(family, model, raw_tool);
  if (!generic) return String(mqtt_txt("Kein Werkzeug", "No tool"));
  const char* known = jbc_usb_core_known_tool_name(generic);
  if (known) return String(known);
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02X", (unsigned)generic);
  return String(buf);
}

static String mqtt_jbc_tool_error_text(uint8_t family, bool valid, uint8_t raw_error) {
  if (!valid) return String("-");
  const uint8_t code = jbc_usb_core_tool_error_code(family, raw_error);
  if (code == 0) return String("OK");
  const char* known = jbc_usb_core_known_tool_error_name(code);
  return known ? String(known) : mqtt_jbc_hex_error("ERROR_", code);
}

static const char* mqtt_jbc_state_text(uint8_t state) {
  if (state == JBC_USB_STATE_NO_TOOL) return mqtt_txt("KEIN TOOL", "NO TOOL");
  return jbc_usb_core_state_name(state);
}

static const char* mqtt_jbc_core_cl_mode(uint8_t mode) {
  switch (mode) {
    case 1: return "DETECTION";
    case 2: return "CONTINUOUS";
    case 4: return "CALIBRATING";
    default: return "UNKNOWN";
  }
}

static const char* mqtt_jbc_core_fe_level(uint8_t level) {
  switch (level) {
    case 0: return "HIGH";
    case 1: return "MEDIUM";
    case 2: return "LOW";
    case 3: return "CUSTOM";
    default: return "UNKNOWN";
  }
}

static String mqtt_jbc_future_mode_text(uint8_t mode) {
  if (mode == (uint8_t)'S') return String("SLEEP");
  if (mode == (uint8_t)'H') return String("HIBERNATION");
  if (mode == (uint8_t)'N' || mode == 0) return String("NONE");
  char raw[8];
  snprintf(raw, sizeof(raw), "0x%02X", (unsigned)mode);
  return String(raw);
}

static String mqtt_jbc_port_suffix(uint8_t port, const char* leaf) {
  String out = F("jbc_p");
  out += (uint8_t)(port + 1U);
  out += '_';
  out += leaf;
  return out;
}

static String mqtt_jbc_port_name(const String& base, uint8_t port, const char* de, const char* en) {
  String out = base;
  out += F(" Port ");
  out += (uint8_t)(port + 1U);
  out += ' ';
  out += mqtt_txt(de, en);
  return out;
}

static void mqtt_clear_jbc_usb_incompatible_discovery(
    const ModuleRecord& m, uint8_t family, uint8_t port_count) {
  // The station behind a JBC USB module can change without changing the OFE
  // module address. HA discovery topics are retained, so remove every entity
  // that is not part of the newly detected station class. The manifest still
  // performs the same reconciliation persistently across reconnects/reboots.
  for (uint8_t i = 0; i < 4; ++i) {
    const bool active = i < port_count;
    const bool soldering = active && family == JBC_USB_CORE_SOLD;
    const bool hot_air = active && family == JBC_USB_CORE_HA;
    const bool common_tool = soldering || hot_air;

    if (!active) {
      const String state = mqtt_jbc_port_suffix(i, "state");
      const String error = mqtt_jbc_port_suffix(i, "error");
      mqtt_clear_module_entity(m.addr, "sensor", state.c_str());
      mqtt_clear_module_entity(m.addr, "sensor", error.c_str());
    }
    if (!common_tool) {
      const String tool = mqtt_jbc_port_suffix(i, "tool");
      const String temp = mqtt_jbc_port_suffix(i, "temp_c");
      const String set_temp = mqtt_jbc_port_suffix(i, "set_temp_c");
      const String power = mqtt_jbc_port_suffix(i, "power_permille");
      mqtt_clear_module_entity(m.addr, "sensor", tool.c_str());
      mqtt_clear_module_entity(m.addr, "sensor", temp.c_str());
      mqtt_clear_module_entity(m.addr, "sensor", set_temp.c_str());
      mqtt_clear_module_entity(m.addr, "sensor", power.c_str());
    }
    if (!soldering) {
      const String future = mqtt_jbc_port_suffix(i, "future_mode");
      const String countdown = mqtt_jbc_port_suffix(i, "transition_countdown_s");
      mqtt_clear_module_entity(m.addr, "sensor", future.c_str());
      mqtt_clear_module_entity(m.addr, "sensor", countdown.c_str());
    }
    if (!hot_air) {
      const String flow = mqtt_jbc_port_suffix(i, "flow_permille");
      const String stop = mqtt_jbc_port_suffix(i, "time_to_stop_ds");
      mqtt_clear_module_entity(m.addr, "sensor", flow.c_str());
      mqtt_clear_module_entity(m.addr, "sensor", stop.c_str());
    }
  }

  if (family != JBC_USB_CORE_CL) {
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_cleaner_mode");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_motors");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_door");
  }
  if (family != JBC_USB_CORE_PH) {
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_tc1_temp_c");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_tc2_temp_c");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_tc3_temp_c");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_tc4_temp_c");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_heater");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_selected_power");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_active_zones");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_time_to_stop");
  }
  if (family != JBC_USB_CORE_FE) {
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_suction_level");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_continuous");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_intake_work");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_intake_stand");
  }
  if (family != JBC_USB_CORE_SF) {
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_feeding");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_program");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_speed");
    mqtt_clear_module_entity(m.addr, "sensor", "jbc_length");
    mqtt_clear_module_entity(m.addr, "binary_sensor", "jbc_tool_enabled");
  }
}

static void mqtt_publish_jbc_usb_core_discovery(const ModuleRecord& m, const String& base) {
  const JbcUsbCoreState c = jbc_usb_core_state(m);
  if (!c.valid) return;
  mqtt_clear_jbc_usb_incompatible_discovery(m, c.family, c.port_count);
  mqtt_publish_module_entity(m, "binary_sensor", "jbc_link", base + String(mqtt_txt(" JBC Verbindung", " JBC Link")), "{{ 'ON' if value_json.jbc_link else 'OFF' }}", ",\"dev_cla\":\"connectivity\"");
  mqtt_publish_module_entity(m, "sensor", "jbc_model", base + String(mqtt_txt(" Stationsmodell", " Station model")), "{{ value_json.jbc_model }}");
  mqtt_publish_module_entity(m, "sensor", "jbc_family", base + String(mqtt_txt(" Gerätefamilie", " Device family")), "{{ value_json.jbc_family }}");
  mqtt_publish_module_entity(m, "binary_sensor", "work", base + String(mqtt_txt(" Work", " Work")), "{{ 'ON' if value_json.work_active else 'OFF' }}", ",\"icon\":\"mdi:soldering-iron\"");
  mqtt_publish_module_entity(m, "binary_sensor", "stand", base + String(mqtt_txt(" Stand", " Stand")), "{{ 'ON' if value_json.stand_active else 'OFF' }}", ",\"icon\":\"mdi:car-brake-parking\"");
  mqtt_publish_module_entity(m, "sensor", "jbc_station_error", base + String(mqtt_txt(" Stationsfehler", " Station error")), "{{ value_json.jbc_station_error_text }}", ",\"icon\":\"mdi:alert-circle-outline\"");
  mqtt_publish_module_entity(m, "sensor", "jbc_connect_mode", base + String(mqtt_txt(" Verbindungsmodus", " Connection mode")), "{{ value_json.jbc_connect_mode }}");

  for (uint8_t i = 0; i < c.port_count && i < 4; ++i) {
    const String state_suffix = mqtt_jbc_port_suffix(i, "state");
    mqtt_publish_module_entity(m, "sensor", state_suffix.c_str(), mqtt_jbc_port_name(base, i, "Status", "State"), (String("{{ value_json.") + state_suffix + " }}").c_str());
    if (c.family == JBC_USB_CORE_SOLD || c.family == JBC_USB_CORE_HA) {
      const String tool_suffix = mqtt_jbc_port_suffix(i, "tool");
      const String tool_key = tool_suffix + "_name";
      mqtt_publish_module_entity(m, "sensor", tool_suffix.c_str(), mqtt_jbc_port_name(base, i, "Werkzeug", "Tool"), (String("{{ value_json.") + tool_key + " }}").c_str(), ",\"icon\":\"mdi:soldering-iron\"");
    }
    const String error_suffix = mqtt_jbc_port_suffix(i, "error");
    const String error_key = error_suffix + "_text";
    mqtt_publish_module_entity(m, "sensor", error_suffix.c_str(), mqtt_jbc_port_name(base, i, "Fehler", "Error"), (String("{{ value_json.") + error_key + " }}").c_str(), ",\"icon\":\"mdi:alert-circle-outline\"");

    if (c.family == JBC_USB_CORE_SOLD || c.family == JBC_USB_CORE_HA) {
      const String temp_suffix = mqtt_jbc_port_suffix(i, "temp_c");
      mqtt_publish_module_entity(m, "sensor", temp_suffix.c_str(), mqtt_jbc_port_name(base, i, "Temperatur", "Temperature"), (String("{{ value_json.") + temp_suffix + " }}").c_str(), ",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\"");
      const String set_suffix = mqtt_jbc_port_suffix(i, "set_temp_c");
      mqtt_publish_module_entity(m, "sensor", set_suffix.c_str(), mqtt_jbc_port_name(base, i, "Solltemperatur", "Set temperature"), (String("{{ value_json.") + set_suffix + " }}").c_str(), ",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\"");
      const String power_suffix = mqtt_jbc_port_suffix(i, "power_permille");
      mqtt_publish_module_entity(m, "sensor", power_suffix.c_str(), mqtt_jbc_port_name(base, i, "Leistung", "Power"), (String("{{ (value_json.") + power_suffix + " | int / 10) | round(1) }}").c_str(), ",\"unit_of_meas\":\"%\",\"stat_cla\":\"measurement\"");
      if (c.family == JBC_USB_CORE_SOLD) {
        const String future_suffix = mqtt_jbc_port_suffix(i, "future_mode");
        mqtt_publish_module_entity(m, "sensor", future_suffix.c_str(), mqtt_jbc_port_name(base, i, "Nächster Modus", "Next mode"), (String("{{ value_json.") + future_suffix + " }}").c_str(), ",\"icon\":\"mdi:progress-clock\"");
        const String countdown_suffix = mqtt_jbc_port_suffix(i, "transition_countdown_s");
        mqtt_publish_module_entity(m, "sensor", countdown_suffix.c_str(), mqtt_jbc_port_name(base, i, "Zeit bis Moduswechsel", "Time to mode change"), (String("{% set s = value_json.") + countdown_suffix + " | int(0) %}{{ '%d min %02d s' | format(s // 60, s % 60) }}").c_str(), ",\"icon\":\"mdi:timer-sand\"");
      } else if (c.family == JBC_USB_CORE_HA) {
        const String flow_suffix = mqtt_jbc_port_suffix(i, "flow_permille");
        mqtt_publish_module_entity(m, "sensor", flow_suffix.c_str(), mqtt_jbc_port_name(base, i, "Luftstrom", "Flow"), (String("{{ (value_json.") + flow_suffix + " | int / 10) | round(1) }}").c_str(), ",\"unit_of_meas\":\"%\",\"stat_cla\":\"measurement\"");
        const String stop_suffix = mqtt_jbc_port_suffix(i, "time_to_stop_ds");
        mqtt_publish_module_entity(m, "sensor", stop_suffix.c_str(), mqtt_jbc_port_name(base, i, "Time to stop", "Time to stop"), (String("{% set ds = value_json.") + stop_suffix + " | int(0) %}{% set s = (ds + 9) // 10 %}{{ '%d min %02d s' | format(s // 60, s % 60) }}").c_str(), ",\"icon\":\"mdi:timer-sand\"");
      }
    }
  }

  if (c.family == JBC_USB_CORE_CL) {
    mqtt_publish_module_entity(m, "sensor", "jbc_cleaner_mode", base + String(mqtt_txt(" Reinigungsmodus", " Cleaner mode")), "{{ value_json.jbc_cleaner_mode }}");
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_motors", base + String(mqtt_txt(" Motoren", " Motors")), "{{ 'ON' if value_json.jbc_motors else 'OFF' }}", ",\"icon\":\"mdi:fan\"");
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_door", base + String(mqtt_txt(" Tür offen", " Door open")), "{{ 'ON' if value_json.jbc_door_open else 'OFF' }}", ",\"dev_cla\":\"door\"");
  } else if (c.family == JBC_USB_CORE_PH) {
    for (uint8_t tc = 0; tc < 4; ++tc) {
      String suffix = F("jbc_tc"); suffix += (uint8_t)(tc + 1U); suffix += F("_temp_c");
      String name = base + F(" TC"); name += (uint8_t)(tc + 1U); name += String(mqtt_txt(" Temperatur", " Temperature"));
      mqtt_publish_module_entity(m, "sensor", suffix.c_str(), name, (String("{{ value_json.") + suffix + " }}").c_str(), ",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\"");
    }
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_heater", base + String(mqtt_txt(" Heizung", " Heater")), "{{ 'ON' if value_json.jbc_heater else 'OFF' }}", ",\"icon\":\"mdi:radiator\"");
    mqtt_publish_module_entity(m, "sensor", "jbc_selected_power", base + String(mqtt_txt(" Sollleistung", " Selected power")), "{{ (value_json.jbc_selected_power_permille | int / 10) | round(1) }}", ",\"unit_of_meas\":\"%\"");
    mqtt_publish_module_entity(m, "sensor", "jbc_active_zones", base + String(mqtt_txt(" Aktive Zonen", " Active zones")), "{{ value_json.jbc_active_zones }}");
    mqtt_publish_module_entity(m, "sensor", "jbc_time_to_stop", base + String(" Time to stop"), "{% set ds = value_json.jbc_time_to_stop_ds | int(0) %}{% set s = (ds + 9) // 10 %}{{ '%d min %02d s' | format(s // 60, s % 60) }}", ",\"icon\":\"mdi:timer-sand\"");
  } else if (c.family == JBC_USB_CORE_FE) {
    mqtt_publish_module_entity(m, "sensor", "jbc_suction_level", base + String(mqtt_txt(" Absaugstufe", " Suction level")), "{{ value_json.jbc_suction_level }}");
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_continuous", base + String(mqtt_txt(" Dauerabsaugung", " Continuous suction")), "{{ 'ON' if value_json.jbc_continuous else 'OFF' }}", ",\"icon\":\"mdi:fan\"");
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_intake_work", base + String(mqtt_txt(" WORK Ansaugung", " WORK intake")), "{{ 'ON' if value_json.jbc_intake_work else 'OFF' }}");
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_intake_stand", base + String(mqtt_txt(" STAND Ansaugung", " STAND intake")), "{{ 'ON' if value_json.jbc_intake_stand else 'OFF' }}");
  } else if (c.family == JBC_USB_CORE_SF) {
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_feeding", base + String(mqtt_txt(" Zinnzufuhr", " Feeding")), "{{ 'ON' if value_json.jbc_feeding else 'OFF' }}", ",\"icon\":\"mdi:ray-start-arrow\"");
    mqtt_publish_module_entity(m, "sensor", "jbc_program", base + String(mqtt_txt(" Programm", " Program")), "{{ value_json.jbc_program }}");
    mqtt_publish_module_entity(m, "sensor", "jbc_speed", base + String(mqtt_txt(" Geschwindigkeit", " Speed")), "{{ (value_json.jbc_speed_tenth_mm_s | int / 10) | round(1) }}", ",\"unit_of_meas\":\"mm/s\",\"stat_cla\":\"measurement\"");
    mqtt_publish_module_entity(m, "sensor", "jbc_length", base + String(mqtt_txt(" Länge", " Length")), "{{ (value_json.jbc_length_tenth_mm | int / 10) | round(1) }}", ",\"unit_of_meas\":\"mm\"");
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_tool_enabled", base + String(mqtt_txt(" Werkzeug aktiviert", " Tool enabled")), "{{ 'ON' if value_json.jbc_tool_enabled else 'OFF' }}");
  }
}


static void mqtt_publish_module_discovery(const ModuleRecord& m) {
  const uint8_t addr = m.addr;
  String hxAddr = String(addr, HEX);
  hxAddr.toUpperCase();
  String base = F("0x");
  if (addr < 16) base += '0';
  base += hxAddr;
  base += ' ';
  base += module_display_name(m);
  mqtt_publish_module_entity(m, "binary_sensor", "online", base + String(mqtt_txt(" RS485", " RS485")), "{{ 'ON' if value_json.online else 'OFF' }}", ",\"dev_cla\":\"connectivity\"");
  mqtt_publish_module_entity(m, "sensor", "firmware", base + String(mqtt_txt(" Firmware", " Firmware")), "{{ value_json.fw }}");
  mqtt_publish_module_entity(m, "sensor", "cpu", base + String(mqtt_txt(" CPU", " CPU")), "{{ value_json.cpu }}", ",\"unit_of_meas\":\"%\",\"stat_cla\":\"measurement\"");
  mqtt_publish_module_entity(m, "sensor", "heap", base + String(mqtt_txt(" Heap", " Heap")), "{{ value_json.heap_kb }}", ",\"unit_of_meas\":\"KB\",\"dev_cla\":\"data_size\",\"stat_cla\":\"measurement\"");
  mqtt_publish_module_entity(m, "sensor", "uptime", base + String(mqtt_txt(" Laufzeit", " Uptime")), "{{ value_json.uptime_text }}");

  if (m.type == MODULE_JBC_BUS || (m.caps & CAP_JBC_BUS)) {
    mqtt_publish_module_entity(m, "binary_sensor", "jbc_link", base + String(mqtt_txt(" JBC Verbindung", " JBC Link")), "{{ 'ON' if value_json.jbc_link else 'OFF' }}", ",\"dev_cla\":\"connectivity\"");
    mqtt_publish_module_entity(m, "binary_sensor", "work", base + String(mqtt_txt(" Work", " Work")), "{{ 'ON' if value_json.work_active else 'OFF' }}", ",\"icon\":\"mdi:soldering-iron\"");
    mqtt_publish_module_entity(m, "binary_sensor", "stand", base + String(mqtt_txt(" Stand", " Stand")), "{{ 'ON' if value_json.stand_active else 'OFF' }}", ",\"icon\":\"mdi:soldering-iron\"");
    mqtt_publish_module_entity(m, "sensor", "station", base + String(mqtt_txt(" Station", " Station")), "{{ value_json.station }}");
    mqtt_publish_module_entity(m, "sensor", "device_id", base + String(mqtt_txt(" Device ID", " Device ID")), "{{ value_json.device_id }}");
  }

  if (m.type == MODULE_JBC_USB || (m.caps & CAP_JBC_USB)) {
    mqtt_publish_jbc_usb_core_discovery(m, base);
  }

  if (m.type != MODULE_WELLER_ZERO_SMOG && (m.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_DIGITAL_OUTPUT | CAP_INPUT_KEYS))) {
    mqtt_publish_module_entity(m, "binary_sensor", "in1", mqtt_alias_or(m.io_in1_alias, "IN1"), "{{ 'ON' if value_json.in1 else 'OFF' }}");
    mqtt_publish_module_entity(m, "binary_sensor", "in2", mqtt_alias_or(m.io_in2_alias, "IN2"), "{{ 'ON' if value_json.in2 else 'OFF' }}");
    mqtt_publish_module_entity(m, "switch", "out1", mqtt_alias_or(m.io_out1_alias, "OUT1"), "{{ 'ON' if value_json.out1 else 'OFF' }}", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\"", mqtt_module_command_leaf(addr, "io_out1"));
    mqtt_publish_module_entity(m, "switch", "out2", mqtt_alias_or(m.io_out2_alias, "OUT2"), "{{ 'ON' if value_json.out2 else 'OFF' }}", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\"", mqtt_module_command_leaf(addr, "io_out2"));
    mqtt_publish_module_entity(m, "switch", "relay_fan", mqtt_alias_or(m.io_main_alias, "Relay/Fan"), "{{ 'ON' if value_json.enabled else 'OFF' }}", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"icon\":\"mdi:fan\"", mqtt_module_command_leaf(addr, "relay_fan"));
    mqtt_publish_module_entity(m, "number", "power", base + String(mqtt_txt(" Leistung", " Power")), "{{ value_json.power_pct }}", ",\"min\":10,\"max\":100,\"step\":1,\"mode\":\"slider\",\"unit_of_meas\":\"%\",\"icon\":\"mdi:fan\"", mqtt_module_command_leaf(addr, "output_power"));
    mqtt_publish_module_entity(m, "sensor", "rpm", base + String(mqtt_txt(" RPM", " RPM")), "{{ value_json.rpm }}", ",\"unit_of_meas\":\"rpm\",\"stat_cla\":\"measurement\"");
    mqtt_publish_module_entity(m, "sensor", "fault", base + String(mqtt_txt(" Fehler", " Fault")), "{{ value_json.fault }}");
  }

  if (m.type == MODULE_WELLER_ZERO_SMOG) {
    mqtt_publish_module_entity(m, "binary_sensor", "weller_link", base + String(mqtt_txt(" Weller Verbindung", " Weller Link")), "{{ 'ON' if value_json.weller_link else 'OFF' }}", ",\"dev_cla\":\"connectivity\"");
    mqtt_publish_module_entity(m, "number", "speed", base + String(mqtt_txt(" Drehzahl", " Speed")), "{{ value_json.speed }}", ",\"min\":30,\"max\":100,\"step\":1,\"mode\":\"slider\",\"unit_of_meas\":\"%\",\"icon\":\"mdi:fan\"", mqtt_module_command_leaf(addr, "weller_speed"));
    mqtt_publish_module_entity(m, "switch", "fan", base + String(mqtt_txt(" Lüfter", " Fan")), "{{ 'ON' if value_json.fan else 'OFF' }}", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\"", mqtt_module_command_leaf(addr, "weller_fan"));
    mqtt_publish_module_entity(m, "switch", "light", base + String(mqtt_txt(" Licht", " Light")), "{{ 'ON' if value_json.light else 'OFF' }}", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\"", mqtt_module_command_leaf(addr, "weller_light"));
    mqtt_publish_module_entity(m, "sensor", "rpm", base + String(mqtt_txt(" RPM", " RPM")), "{{ value_json.rpm }}", ",\"unit_of_meas\":\"rpm\",\"stat_cla\":\"measurement\"");
    mqtt_publish_module_entity(m, "sensor", "filter", base + String(mqtt_txt(" Filter", " Filter")), "{{ value_json.filter_text }}");
    mqtt_publish_module_entity(m, "sensor", "filter_runtime", base + String(mqtt_txt(" Filterlaufzeit", " Filter Runtime")), "{{ value_json.filter_runtime_text }}");
    mqtt_publish_module_entity(m, "sensor", "filter_time", base + String(mqtt_txt(" Filterzeit", " Filter Time")), "{{ value_json.filter_programmed_text }}");
  }

  if (m.type == MODULE_DISPLAY || (m.caps & CAP_DISPLAY)) {
    mqtt_publish_module_entity(m, "number", "brightness", base + String(mqtt_txt(" Helligkeit", " Brightness")), "{{ value_json.brightness }}", ",\"min\":10,\"max\":100,\"step\":1,\"mode\":\"slider\",\"unit_of_meas\":\"%\",\"icon\":\"mdi:brightness-6\"", mqtt_module_command_leaf(addr, "display_brightness"));
    mqtt_publish_module_entity(m, "select", "language", base + String(mqtt_txt(" Sprache", " Language")), "{{ value_json.language }}", ",\"options\":[\"English\",\"Deutsch\"],\"icon\":\"mdi:translate\"", mqtt_module_command_leaf(addr, "display_language"));
    mqtt_publish_module_entity(m, "select", "theme", base + String(mqtt_txt(" Farbschema", " Theme")), "{{ value_json.theme }}", ",\"options\":[\"Dark\",\"Light\"],\"icon\":\"mdi:theme-light-dark\"", mqtt_module_command_leaf(addr, "display_theme"));
    mqtt_publish_module_entity(m, "select", "screensaver", base + String(mqtt_txt(" Ruhemodus", " Screensaver")), "{{ value_json.screensaver }}", ",\"options\":[\"Off\",\"1 min\",\"2 min\",\"5 min\",\"10 min\"],\"icon\":\"mdi:monitor-screenshot\"", mqtt_module_command_leaf(addr, "display_screensaver"));
  }

  // Universal/Modbus profile changes are reconciled by the persisted manifest:
  // only entities that existed previously but are no longer desired are cleared.
  mqtt_publish_universal_descriptor_entities(m, base);
}

static void mqtt_publish_discovery() {
  if (!mqtt_ha_discovery || !mqtt_client.connected()) return;
  heap_diag_set_context(HEAP_DIAG_CTX_MQTT, "discovery");

  // Capture the exact module discovery set produced by this pass.
  // A failed retained publish must not be treated as a completed HA discovery.
  mqtt_discovery_publish_failed = false;
  mqtt_manifest_begin_capture();
  heap_diag_sample("mqtt_discovery_begin");
  mqtt_publish_discovery_entity("binary_sensor", "output", mqtt_txt("Ausgang", "Output"), "{{ \'ON\' if value_json.output else \'OFF\' }}", nullptr, nullptr, nullptr, true);
  mqtt_publish_discovery_entity("sensor", "power", mqtt_txt("Angeforderte Leistung", "Requested Power"), "{{ (value_json.power | int / 10) | round(0) }}", "%", nullptr, "measurement");
  mqtt_publish_discovery_entity("sensor", "afterrun", mqtt_txt("Nachlauf", "Afterrun"), "{{ value_json.afterrun_text }}");
  mqtt_publish_discovery_entity("sensor", "modules", mqtt_txt("Module", "Modules"), "{{ value_json.modules }}", nullptr, nullptr, "measurement");
  mqtt_publish_discovery_entity("sensor", "jbc_error", mqtt_txt("JBC Fehler", "JBC Error"), "{{ value_json.jbc_error }}", nullptr, nullptr, "measurement");
  mqtt_publish_discovery_entity("sensor", "jbc_error_text", mqtt_txt("JBC Fehlertext", "JBC Error Text"), "{{ value_json.jbc_error_text }}");
  mqtt_publish_discovery_entity("sensor", "alarm_count", mqtt_txt("Alarm Anzahl", "Alarm Count"), "{{ value_json.alarm_count }}", nullptr, nullptr, "measurement");
  mqtt_publish_discovery_entity("sensor", "alarm_text", mqtt_txt("Alarm Text", "Alarm Text"), "{{ value_json.alarm_text }}");
  mqtt_publish_discovery_entity("sensor", "wifi_rssi", mqtt_txt("WLAN Signal", "WiFi RSSI"), "{{ value_json.wifi_rssi }}", "dBm", "signal_strength", "measurement");
  mqtt_publish_discovery_entity("sensor", "ip", mqtt_txt("IP Adresse", "IP Address"), "{{ value_json.ip }}");
  mqtt_publish_discovery_entity("sensor", "datetime", mqtt_txt("Datum/Zeit", "Date Time"), "{{ value_json.datetime }}");
  mqtt_publish_discovery_entity("sensor", "mqtt_state", mqtt_txt("MQTT Status", "MQTT State"), "{{ value_json.mqtt_state_text }}");
  mqtt_publish_discovery_entity("sensor", "mqtt_state_code", mqtt_txt("MQTT Statuscode", "MQTT State Code"), "{{ value_json.mqtt_state }}", nullptr, nullptr, "measurement");
  mqtt_publish_discovery_entity("sensor", "heap", mqtt_txt("Freier Heap", "Free Heap"), "{{ (value_json.heap | int / 1024) | round(0) }}", "KB", "data_size", "measurement");
  mqtt_publish_discovery_entity("sensor", "cpu", mqtt_txt("CPU Last", "CPU Load"), "{{ value_json.cpu }}", "%", nullptr, "measurement");
  mqtt_publish_discovery_entity("sensor", "firmware", "Firmware", "{{ value_json.fw }}");
  mqtt_publish_discovery_control("switch", "status_led_enabled", mqtt_txt("Status-LEDs", "Status LEDs"), "{{ 'ON' if value_json.status_led_enabled else 'OFF' }}", "cmd/status_led_enabled", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"icon\":\"mdi:led-on\"");
  mqtt_publish_discovery_control("number", "status_led_brightness", mqtt_txt("Status-LED Helligkeit", "Status LED brightness"), "{{ value_json.status_led_brightness }}", "cmd/status_led_brightness", ",\"min\":10,\"max\":100,\"step\":1,\"mode\":\"slider\",\"unit_of_meas\":\"%\",\"icon\":\"mdi:brightness-6\"");
  mqtt_publish_discovery_control("number", "afterrun_stand", mqtt_txt("Nachlauf Stand", "Afterrun stand"), "{{ value_json.delay_stand }}", "cmd/delay_stand", ",\"min\":0,\"max\":3600,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"s\",\"icon\":\"mdi:timer-outline\"");
  mqtt_publish_discovery_control("number", "afterrun_work", mqtt_txt("Nachlauf Work", "Afterrun work"), "{{ value_json.delay_work }}", "cmd/delay_work", ",\"min\":0,\"max\":3600,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"s\",\"icon\":\"mdi:timer-cog-outline\"");
  String selected_flow_extra = F(",\"min\":");
  selected_flow_extra += scheduler.activeOutputMinSelectFlow() / 10U;
  selected_flow_extra += F(",\"max\":100,\"step\":1,\"mode\":\"slider\",\"unit_of_meas\":\"%\",\"icon\":\"mdi:fan\"");
  mqtt_publish_discovery_control("number", "selected_flow", mqtt_txt("Benutzerleistung", "Selected flow"), "{{ value_json.custom_power }}", "cmd/custom_power", selected_flow_extra.c_str());
  String afterrun_power_extra = F(",\"min\":");
  afterrun_power_extra += scheduler.activeOutputMinSelectFlow() / 10U;
  afterrun_power_extra += F(",\"max\":100,\"step\":1,\"mode\":\"slider\",\"unit_of_meas\":\"%\",\"icon\":\"mdi:fan-clock\"");
  mqtt_publish_discovery_control("number", "afterrun_power", mqtt_txt("Nachlaufleistung", "Afterrun power"), "{{ value_json.afterrun_power }}", "cmd/afterrun_power", afterrun_power_extra.c_str());
  mqtt_publish_discovery_control("switch", "afterrun_power_enabled", mqtt_txt("Nachlaufleistung aktiv", "Afterrun power enabled"), "{{ 'ON' if value_json.afterrun_power_enabled else 'OFF' }}", "cmd/afterrun_power_enabled", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"icon\":\"mdi:fan-clock\"");
  mqtt_publish_discovery_control("switch", "continuous_suction", mqtt_txt("Dauerlauf", "Continuous suction"), "{{ 'ON' if value_json.continuous_set else 'OFF' }}", "cmd/continuous", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"icon\":\"mdi:fan-auto\"");
  mqtt_publish_discovery_control("switch", "stand_intakes", mqtt_txt("Stand Intakes", "Stand intakes"), "{{ 'ON' if value_json.stand_intakes else 'OFF' }}", "cmd/stand_intakes", ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"icon\":\"mdi:soldering-iron\"");
  String suction_level_extra = web_is_german()
    ? F(",\"options\":[\"Hoch\",\"Mittel\",\"Niedrig\",\"Benutzer\"],\"icon\":\"mdi:fan-speed-3\"")
    : F(",\"options\":[\"High\",\"Medium\",\"Low\",\"Custom\"],\"icon\":\"mdi:fan-speed-3\"");
  mqtt_publish_discovery_control("select", "suction_level", mqtt_txt("Absaugstufe", "Suction level set"), "{{ value_json.suction_name }}", "cmd/suction_level", suction_level_extra.c_str());
  String main_input_extra = mqtt_main_input_options_extra();
  mqtt_publish_discovery_control("select", "main_input", mqtt_txt("Haupteingang", "Main input"), "{{ value_json.main_input_name }}", "cmd/main_input", main_input_extra.c_str());
  String main_output_extra = mqtt_main_output_options_extra();
  mqtt_publish_discovery_control("select", "main_output", mqtt_txt("Hauptausgang Absaugung", "Main extractor output"), "{{ value_json.main_output_name }}", "cmd/main_output", main_output_extra.c_str());
  uint8_t order[ModuleRegistry::MAX_MODULES];
  const uint8_t count = mqtt_sorted_module_indices(order, sizeof(order));
  for (uint8_t oi = 0; oi < count; ++oi) {
    const ModuleRecord& m = registry.at(order[oi]);

    // Keep offline remembered addresses in the manifest with zero discovery
    // entities. Their old entities are removed, but their retained offline
    // state/status remains until a manual module scan actually removes them.
    mqtt_manifest_note_module(m.addr);
    if (m.online) mqtt_publish_module_discovery(m);
    else mqtt_publish_module_offline(m.addr);
  }

  if (mqtt_discovery_publish_failed) {
    // Do not persist/reconcile a partial desired set and do not advance the
    // discovery signature. mqtt_loop() will retry the full pass.
    mqtt_manifest_capture_active = false;
    mqtt_discovery_published = false;
    mqtt_next_discovery_check_ms = millis() - 4000UL;
    heap_diag_sample("mqtt_discovery_retry");
  } else {
    mqtt_manifest_end_capture_and_reconcile();
    mqtt_discovery_signature = mqtt_module_discovery_signature_now();
    mqtt_discovery_published = true;
    heap_diag_sample("mqtt_discovery_end");
  }
}

static bool mqtt_payload_bool(const String& value) {
  return value == "1" || value == "ON" || value == "on" || value == "true" || value == "True";
}

static void mqtt_apply_command(const String& leaf, const String& value) {
  const JbcModuleState& cs = scheduler.controlSettings();
  uint8_t suction = cs.suction_level;
  uint16_t select_flow = cs.select_flow ? cs.select_flow : 1000;
  uint16_t delay_work = cs.delay_work_sec;
  uint16_t delay_stand = cs.delay_stand_sec;
  bool stand_intakes = cs.stand_intakes != 0;
  bool continuous = cs.continuous != 0;

  if (leaf == "suction_level") {
    if (value == "High" || value == "high" || value == "Hoch" || value == "hoch" || value == "0") suction = 0;
    else if (value == "Medium" || value == "medium" || value == "Mittel" || value == "mittel" || value == "1") suction = 1;
    else if (value == "Low" || value == "low" || value == "Niedrig" || value == "niedrig" || value == "2") suction = 2;
    else suction = 3;
  } else if (leaf == "custom_power") {
    uint16_t percent = (uint16_t)strtoul(value.c_str(), nullptr, 10);
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    select_flow = percent * 10U;
  } else if (leaf == "afterrun_power") {
    uint16_t percent = (uint16_t)strtoul(value.c_str(), nullptr, 10);
    const uint16_t min_percent = scheduler.activeOutputMinSelectFlow() / 10U;
    if (percent < min_percent) percent = min_percent;
    if (percent > 100) percent = 100;
    master_cmd_set_afterrun_power_profile(scheduler.afterrunPowerProfileEnabled(), percent * 10U, true);
    mqtt_last_publish_ms = 0;
    return;
  } else if (leaf == "afterrun_power_enabled") {
    master_cmd_set_afterrun_power_profile(mqtt_payload_bool(value), scheduler.afterrunPower(), true);
    mqtt_last_publish_ms = 0;
    return;
  } else if (leaf == "delay_work") {
    uint32_t v = strtoul(value.c_str(), nullptr, 10);
    if (v > 3600UL) v = 3600UL;
    delay_work = (uint16_t)v;
  } else if (leaf == "delay_stand") {
    uint32_t v = strtoul(value.c_str(), nullptr, 10);
    if (v > 3600UL) v = 3600UL;
    delay_stand = (uint16_t)v;
  } else if (leaf == "stand_intakes") {
    stand_intakes = mqtt_payload_bool(value);
  } else if (leaf == "continuous") {
    continuous = mqtt_payload_bool(value);
  } else if (leaf == "status_led_enabled") {
    save_status_led_config(mqtt_payload_bool(value), status_led_brightness_pct);
    mqtt_last_publish_ms = 0;
    return;
  } else if (leaf == "status_led_brightness") {
    uint32_t pct = strtoul(value.c_str(), nullptr, 10);
    if (pct < 10UL) pct = 10UL;
    if (pct > 100UL) pct = 100UL;
    save_status_led_config(status_led_enabled, (uint8_t)pct);
    mqtt_last_publish_ms = 0;
    return;
  } else if (leaf == "main_input") {
    uint8_t source_type = 0;
    uint8_t source_addr = 0;
    uint8_t source_bit = 0;
    if (!mqtt_find_main_input_by_label(value, source_type, source_addr, source_bit)) return;
    if (!master_cmd_set_main_input(source_type, source_addr, source_bit)) return;
    mqtt_last_publish_ms = 0;
    return;
  } else if (leaf == "main_output") {
    uint8_t addr = 0;
    if (!mqtt_find_main_output_by_label(value, addr)) return;
    master_cmd_set_preferred_output(addr);
    mqtt_last_publish_ms = 0;
    return;
  } else {
    return;
  }
  apply_control_settings(suction, select_flow, delay_work, delay_stand, stand_intakes, continuous, true);
  master_cmd_persist_control_settings();
  mqtt_last_publish_ms = 0;
}

static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
  String prefix = mqtt_topic_path("cmd/");
  String t(topic);
  if (!t.startsWith(prefix)) return;
  String leaf = t.substring(prefix.length());
  String value;
  value.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) value += (char)payload[i];
  value.trim();
  if (mqtt_apply_module_command(leaf, value)) return;
  mqtt_apply_command(leaf, value);
}

static bool mqtt_config_ready() {
  return mqtt_enabled && mqtt_host[0] && WiFi.status() == WL_CONNECTED;
}

static bool mqtt_ca_looks_valid() {
  return mqtt_ca_cert.indexOf("-----BEGIN CERTIFICATE-----") >= 0 &&
         mqtt_ca_cert.indexOf("-----END CERTIFICATE-----") >= 0;
}

static void mqtt_configure_client() {
  if (mqtt_tls_enabled) {
    mqtt_tls_client.stop();
    mqtt_tls_verify_enabled = mqtt_ca_looks_valid();
    if (mqtt_tls_verify_enabled) {
      mqtt_tls_client.setCACert(mqtt_ca_cert.c_str());
    } else {
      mqtt_tls_client.setInsecure();
    }
    mqtt_client.setClient(mqtt_tls_client);
    mqtt_client_tls_active = true;
  } else {
    mqtt_tls_verify_enabled = false;
    mqtt_client.setClient(mqtt_plain_client);
    mqtt_client_tls_active = false;
  }
  mqtt_client.setServer(mqtt_host, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
  mqtt_client.setBufferSize(4096);
  // PubSubClient uses a blocking socket during connect/publish. Keep the
  // timeout short so an unreachable broker cannot stall the system for 15 s
  // per retry. The MQTT task is separate from the web task, but this also
  // protects boot and diagnostics responsiveness.
  mqtt_client.setSocketTimeout(2);
  mqtt_client.setKeepAlive(15);
}

static bool mqtt_connect_if_needed() {
  if (!mqtt_config_ready()) {
    if (mqtt_client.connected()) {
      mqtt_publish_all_availability_offline();
      mqtt_client.disconnect();
    }
    mqtt_was_connected = false;
    mqtt_discovery_published = false;
    return false;
  }
  if (mqtt_client.connected()) return true;
  const uint32_t now = millis();
  if ((int32_t)(now - mqtt_next_connect_ms) < 0) return false;
  mqtt_next_connect_ms = now + 5000UL;
  heap_diag_set_context(HEAP_DIAG_CTX_MQTT, "connect");
  heap_diag_sample("mqtt_connect_begin");
  mqtt_configure_client();
  char client_id[48];
  snprintf(client_id, sizeof(client_id), "%s", master_hostname);
  String will_topic = mqtt_topic_path("status");
  bool ok = false;
  if (mqtt_user[0]) ok = mqtt_client.connect(client_id, mqtt_user, mqtt_password, will_topic.c_str(), 0, true, "offline");
  else ok = mqtt_client.connect(client_id, will_topic.c_str(), 0, true, "offline");
  mqtt_last_state = mqtt_client.state();
  if (ok) {
    mqtt_client.publish(will_topic.c_str(), "online", true);
    mqtt_client.subscribe(mqtt_topic_path("cmd/#").c_str());
    mqtt_was_connected = true;
    mqtt_last_publish_ms = 0;
    mqtt_publish_discovery();
  } else {
    Serial.print("MQTT connect failed state=");
    Serial.print(mqtt_last_state);
    Serial.print(" host=");
    Serial.print(mqtt_host);
    Serial.print(":");
    Serial.print(mqtt_port);
    Serial.print(" tls=");
    Serial.print(mqtt_tls_enabled ? (mqtt_tls_verify_enabled ? "verified" : "insecure") : "off");
    Serial.print(" user=");
    Serial.println(mqtt_user[0] ? "set" : "empty");
  }
  heap_diag_sample(ok ? "mqtt_connect_ok" : "mqtt_connect_failed");
  heap_diag_clear_context(HEAP_DIAG_CTX_MQTT);
  return ok;
}


static bool mqtt_mask_bit(uint16_t mask, uint8_t bit) {
  return (mask & (uint16_t)(1U << bit)) != 0;
}

static const char* mqtt_display_language_text(uint8_t language) {
  return language == 1 ? "Deutsch" : "English";
}

static const char* mqtt_display_theme_text(uint8_t theme) {
  return theme == 1 ? "Light" : "Dark";
}

static const char* mqtt_display_screensaver_text(uint8_t minutes) {
  switch (minutes) {
    case 1: return "1 min";
    case 2: return "2 min";
    case 5: return "5 min";
    case 10: return "10 min";
    default: return "Off";
  }
}

static uint8_t mqtt_display_screensaver_value(const String& value) {
  if (value == "1 min" || value == "1") return 1;
  if (value == "2 min" || value == "2") return 2;
  if (value == "5 min" || value == "5") return 5;
  if (value == "10 min" || value == "10") return 10;
  return 0;
}

static void mqtt_publish_module_state(const ModuleRecord& m) {
  if (m.online) mqtt_client.publish(mqtt_module_topic(m.addr, "status").c_str(), "online", true);
  else mqtt_client.publish(mqtt_module_topic(m.addr, "status").c_str(), "offline", true);
  String json;
  json.reserve(3400);
  json += "{";
  json += "\"addr\":"; json += m.addr;
  json += ",\"online\":"; json += m.online ? "true" : "false";
  json += ",\"type\":"; json += m.type;
  json += ",\"type_name\":\""; json += module_type_name_for(m); json += "\"";
  json += ",\"name\":\""; json += json_escape(module_display_name(m).c_str()); json += "\"";
  json += ",\"fw\":\""; json += mqtt_fw_string(m); json += "\"";
  json += ",\"cpu\":"; json += m.module_cpu_load_pct;
  json += ",\"heap_kb\":"; json += (uint32_t)((m.module_heap_free + 512UL) / 1024UL);
  json += ",\"uptime_s\":"; json += m.module_uptime_s;
  json += ",\"uptime_text\":\""; json += duration_text_seconds(m.module_uptime_s); json += "\"";
  json += ",\"loop_ms\":"; json += m.module_loop_max_ms;
  json += ",\"fault\":\"";
  const uint16_t combined_fault = m.io_fault_mask | m.output_fault_mask;
  json += output_fault_text_for_module(combined_fault, m.type);

  json += "\"";

  if (m.type == MODULE_JBC_BUS || (m.caps & CAP_JBC_BUS)) {
    json += ",\"jbc_link\":"; json += (m.jbc_link_flags & FAST_FLAG_CONNECTED) ? "true" : "false";
    json += ",\"station\":\""; json += station_type_name(m.station_addr); if (m.station_addr) { json += " 0x"; json += String(m.station_addr, HEX); } json += "\"";
    json += ",\"jbc_addr\":"; json += m.jbc_addr;
    json += ",\"work_active\":"; json += m.jbc_work_mask ? "true" : "false";
    json += ",\"stand_active\":"; json += m.jbc_stand_mask ? "true" : "false";
    json += ",\"jbc_filter_life_rx\":"; json += m.jbc_filter_life;
    json += ",\"jbc_filter_sat_rx\":"; json += m.jbc_filter_sat;
    json += ",\"jbc_stat_error_rx\":"; json += m.jbc_stat_error;
    json += ",\"work_mask\":"; json += m.jbc_work_mask;
    json += ",\"stand_mask\":"; json += m.jbc_stand_mask;
    json += ",\"jbc_filter_life_rx\":"; json += m.jbc_filter_life;
    json += ",\"jbc_filter_sat_rx\":"; json += m.jbc_filter_sat;
    json += ",\"jbc_stat_error_rx\":"; json += m.jbc_stat_error;
    json += ",\"device_id\":\""; json += json_escape(bytes_ascii(m.jbc_device_id, m.jbc_device_id_len).c_str()); json += "\"";
  }

  if (m.type == MODULE_JBC_USB || (m.caps & CAP_JBC_USB)) {
    const JbcUsbCoreState c = jbc_usb_core_state(m);
    json += ",\"jbc_link\":"; json += c.linked ? "true" : "false";
    json += ",\"jbc_model\":\""; json += json_escape(m.jbc_usb_model); json += "\"";
    json += ",\"jbc_family\":\""; json += jbc_usb_core_family_name(c.family); json += "\"";
    json += ",\"jbc_port_count\":"; json += c.port_count;
    json += ",\"work_active\":"; json += c.work_active ? "true" : "false";
    json += ",\"stand_active\":"; json += c.stand_active ? "true" : "false";
    json += ",\"jbc_station_error\":"; if (c.station_error_valid) json += c.station_error; else json += -1;
    json += ",\"jbc_station_error_text\":\""; json += json_escape(mqtt_jbc_station_error_text(c.station_error_valid, c.station_error).c_str()); json += "\"";
    json += ",\"jbc_connect_mode\":\""; json += c.connect_mode_valid ? (c.control_mode ? "CONTROL" : "MONITOR") : "-"; json += "\"";

    for (uint8_t i = 0; i < c.port_count && i < 4; ++i) {
      const JbcUsbCorePort& port = c.ports[i];
      const String prefix = String(F("jbc_p")) + String((uint8_t)(i + 1U)) + '_';
      json += ",\""; json += prefix; json += "valid\":"; json += port.valid ? "true" : "false";
      json += ",\""; json += prefix; json += "state\":\""; json += port.valid ? mqtt_jbc_state_text(port.state) : mqtt_txt("KEINE DATEN", "NO DATA"); json += "\"";
      json += ",\""; json += prefix; json += "tool\":"; if (port.valid) json += port.tool; else json += -1;
      json += ",\""; json += prefix; json += "tool_name\":\""; json += json_escape(port.valid ? mqtt_jbc_tool_name(c.family, m.jbc_usb_model, port.tool).c_str() : "-"); json += "\"";
      json += ",\""; json += prefix; json += "error\":"; if (port.valid) json += port.error; else json += -1;
      json += ",\""; json += prefix; json += "error_text\":\""; json += json_escape(mqtt_jbc_tool_error_text(c.family, port.valid, port.error).c_str()); json += "\"";
      if (c.family == JBC_USB_CORE_SOLD || c.family == JBC_USB_CORE_HA) {
        json += ",\""; json += prefix; json += "temp_c\":"; if (port.valid) json += (uint16_t)(port.actual_temp / 9U); else json += -1;
        json += ",\""; json += prefix; json += "set_temp_c\":"; if (port.selected_temp_valid) json += (uint16_t)(port.selected_temp / 9U); else json += -1;
        json += ",\""; json += prefix; json += "power_permille\":"; if (port.valid) json += port.power_permille; else json += -1;
        if (c.family == JBC_USB_CORE_SOLD) {
          json += ",\""; json += prefix; json += "future_mode\":\"";
          json += json_escape(port.valid ? mqtt_jbc_future_mode_text(port.future_mode).c_str() : "-"); json += "\"";
          json += ",\""; json += prefix; json += "transition_countdown_s\":";
          if (port.valid) json += port.transition_countdown_s; else json += -1;
        } else if (c.family == JBC_USB_CORE_HA) {
          json += ",\""; json += prefix; json += "flow_permille\":"; if (port.valid) json += port.flow_permille; else json += -1;
          json += ",\""; json += prefix; json += "set_flow_permille\":"; if (port.selected_flow_valid) json += port.selected_flow_permille; else json += -1;
          json += ",\""; json += prefix; json += "time_to_stop_ds\":"; json += port.time_to_stop;
        }
      }
    }

    if (c.family == JBC_USB_CORE_CL) {
      const JbcUsbCorePort& port = c.ports[0];
      json += ",\"jbc_cleaner_mode\":\""; json += mqtt_jbc_core_cl_mode(port.mode); json += "\"";
      json += ",\"jbc_motors\":"; json += (port.motors_valid && port.motors_on) ? "true" : "false";
      json += ",\"jbc_door_open\":"; json += (port.door_valid && port.door_open) ? "true" : "false";
    } else if (c.family == JBC_USB_CORE_PH) {
      const JbcUsbCorePort& port = c.ports[0];
      for (uint8_t tc = 0; tc < 4; ++tc) {
        json += ",\"jbc_tc"; json += (uint8_t)(tc + 1U); json += "_temp_c\":";
        if (c.ph_tc[tc].actual_valid) json += (uint16_t)(c.ph_tc[tc].actual_temp / 9U); else json += -1;
        json += ",\"jbc_tc"; json += (uint8_t)(tc + 1U); json += "_set_temp_c\":";
        if (c.ph_tc[tc].selected_valid) json += (uint16_t)(c.ph_tc[tc].selected_temp / 9U); else json += -1;
      }
      json += ",\"jbc_heater\":"; json += (port.heater_valid && port.heater_on) ? "true" : "false";
      json += ",\"jbc_heater_power_permille\":"; json += port.power_permille;
      json += ",\"jbc_selected_power_permille\":"; if (port.selected_power_valid) json += port.selected_power_permille; else json += -1;
      json += ",\"jbc_active_zones\":"; if (port.active_zones_valid) json += port.active_zones; else json += -1;
      json += ",\"jbc_work_mode\":"; json += port.mode;
      json += ",\"jbc_time_to_stop_ds\":"; json += port.time_to_stop;
    } else if (c.family == JBC_USB_CORE_FE) {
      const JbcUsbCorePort& port = c.ports[0];
      json += ",\"jbc_suction_level\":\""; json += mqtt_jbc_core_fe_level(port.mode); json += "\"";
      json += ",\"jbc_continuous\":"; json += (c.continuous_valid && c.continuous_on) ? "true" : "false";
      json += ",\"jbc_intake_work\":"; json += (port.intake_work_valid && port.intake_work_on) ? "true" : "false";
      json += ",\"jbc_intake_stand\":"; json += (port.intake_stand_valid && port.intake_stand_on) ? "true" : "false";
      json += ",\"jbc_time_to_stop_work_raw\":"; json += port.fe_time_to_stop_work;
      json += ",\"jbc_time_to_stop_stand_raw\":"; json += port.fe_time_to_stop_stand;
    } else if (c.family == JBC_USB_CORE_SF) {
      const JbcUsbCorePort& port = c.ports[0];
      json += ",\"jbc_feeding\":"; json += (port.sf_feeding_valid && port.sf_feeding) ? "true" : "false";
      json += ",\"jbc_program\":"; json += port.sf_selected_program;
      json += ",\"jbc_speed_tenth_mm_s\":"; json += port.sf_speed_tenth_mm_s;
      json += ",\"jbc_length_tenth_mm\":"; json += port.sf_length_tenth_mm;
      json += ",\"jbc_tool_enabled\":"; json += (port.sf_tool_enabled_valid && port.sf_tool_enabled) ? "true" : "false";
    }
  }

  if (m.type != MODULE_WELLER_ZERO_SMOG && (m.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_DIGITAL_OUTPUT | CAP_INPUT_KEYS))) {
    json += ",\"enabled\":"; json += m.output_enabled ? "true" : "false";
    json += ",\"main_label\":\""; json += json_escape(mqtt_alias_or(m.io_main_alias, "Relay/Fan").c_str()); json += "\"";
    json += ",\"in1_label\":\""; json += json_escape(mqtt_alias_or(m.io_in1_alias, "IN1").c_str()); json += "\"";
    json += ",\"in2_label\":\""; json += json_escape(mqtt_alias_or(m.io_in2_alias, "IN2").c_str()); json += "\"";
    json += ",\"out1_label\":\""; json += json_escape(mqtt_alias_or(m.io_out1_alias, "OUT1").c_str()); json += "\"";
    json += ",\"out2_label\":\""; json += json_escape(mqtt_alias_or(m.io_out2_alias, "OUT2").c_str()); json += "\"";
    json += ",\"power\":"; json += m.output_power;
    json += ",\"power_pct\":"; json += (uint16_t)((m.output_power + 5U) / 10U);
    json += ",\"rpm\":"; json += (m.output_rpm ? m.output_rpm : m.weller_fan_rpm);
    json += ",\"in1\":"; json += mqtt_mask_bit(m.io_input_mask, 0) ? "true" : "false";
    json += ",\"in2\":"; json += mqtt_mask_bit(m.io_input_mask, 1) ? "true" : "false";
    json += ",\"out1\":"; json += mqtt_mask_bit(m.io_output_mask, 0) ? "true" : "false";
    json += ",\"out2\":"; json += mqtt_mask_bit(m.io_output_mask, 1) ? "true" : "false";
  }

  if (m.type == MODULE_WELLER_ZERO_SMOG) {
    const bool weller_link = m.weller_uart_age_sec != 0xFFFF && m.weller_uart_age_sec <= 10;
    json += ",\"weller_link\":"; json += weller_link ? "true" : "false";
    json += ",\"speed\":"; json += m.weller_speed_percent ? m.weller_speed_percent : 30;
    json += ",\"rpm\":"; json += m.weller_fan_rpm;
    json += ",\"fan\":"; json += mqtt_mask_bit(m.io_output_mask, 0) ? "true" : "false";
    json += ",\"light\":"; json += (mqtt_mask_bit(m.io_output_mask, 1) || m.weller_work_light) ? "true" : "false";
    json += ",\"filter_status\":"; json += m.weller_filter_status;
    json += ",\"filter_text\":\""; json += mqtt_weller_filter_text(m.weller_filter_status); json += "\"";
    json += ",\"filter_runtime_min\":"; json += m.weller_filter_runtime_minutes;
    json += ",\"filter_runtime_text\":\""; json += duration_text_minutes(m.weller_filter_runtime_minutes); json += "\"";
    json += ",\"filter_programmed_min\":"; json += m.weller_programmed_filter_minutes;
    json += ",\"filter_programmed_text\":\""; json += duration_text_minutes(m.weller_programmed_filter_minutes); json += "\"";
    json += ",\"sw\":"; json += m.weller_version;
  }

  if (m.type == MODULE_DISPLAY || (m.caps & CAP_DISPLAY)) {
    json += ",\"brightness\":"; json += m.display_brightness_pct ? m.display_brightness_pct : 85;
    json += ",\"language\":\""; json += mqtt_display_language_text(m.display_language); json += "\"";
    json += ",\"theme\":\""; json += mqtt_display_theme_text(m.display_theme); json += "\"";
    json += ",\"screensaver\":\""; json += mqtt_display_screensaver_text(m.display_screensaver_min); json += "\"";
    json += ",\"view_mode\":"; json += m.display_view_mode;
  }

  if (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) {
    bool emitted_entity_id[256] = {false};
    if (m.universal_descriptor_valid) {
      const char* p = m.universal_descriptor;
      while (p && *p) {
        const char* next = strchr(p, '\n');
        char line[1024];
        size_t len = next ? (size_t)(next - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;
        MqttUniversalEntityDef def;
        if (mqtt_parse_universal_descriptor_line(line, def) && !emitted_entity_id[def.id]) {
          emitted_entity_id[def.id] = true;
          const bool readable = mqtt_universal_entity_readable(def);
          const bool wo_switch_shadow = mqtt_ascii_ci_eq(def.type, "switch") &&
                                        mqtt_universal_entity_writable(def) && !readable;
          if (readable || wo_switch_shadow) {
            json += ",\"entity_"; json += def.id; json += "\":\"";
            const UniversalEntityState* e = mqtt_universal_state_by_id(m, def.id);
            if (e) json += json_escape(mqtt_universal_entity_value_for_def(m, *e).c_str());
            else if (wo_switch_shadow) json += '0';
            json += "\"";
          }
        }
        p = next ? next + 1 : nullptr;
      }
    }
    for (uint8_t i = 0; i < m.universal_entity_count && i < ModuleRecord::UNIVERSAL_ENTITY_MAX; ++i) {
      const UniversalEntityState& e = m.universal_entities[i];
      if (emitted_entity_id[e.id]) continue;
      emitted_entity_id[e.id] = true;
      json += ",\"entity_"; json += e.id; json += "\":\"";
      json += json_escape(mqtt_universal_entity_value_for_def(m, e).c_str());
      json += "\"";
    }
  }
  json += "}";
  mqtt_client.publish(mqtt_module_topic(m.addr, "state").c_str(), json.c_str(), true);
}

static bool mqtt_apply_module_command(const String& leaf, const String& value) {
  if (!leaf.startsWith("module/")) return false;
  int slash = leaf.indexOf('/', 7);
  if (slash < 0) return true;
  String addr_text = leaf.substring(7, slash);
  uint8_t addr = (uint8_t)strtoul(addr_text.c_str(), nullptr, 16);
  String action = leaf.substring(slash + 1);
  ModuleRecord* m = registry.find(addr);
  if (!m || !m->online) return true;

  if (action.startsWith("entity_")) {
    uint8_t entity_id = (uint8_t)strtoul(action.substring(7).c_str(), nullptr, 10);
    if (entity_id >= 20) {
      String out = value;
      if (out == "ON" || out == "on" || out == "true" || out == "True") out = "1";
      else if (out == "OFF" || out == "off" || out == "false" || out == "False") out = "0";
      MqttUniversalEntityDef def;
      const bool has_def = mqtt_universal_descriptor_def_for_entity(*m, entity_id, def);
      if (has_def && !mqtt_universal_entity_writable(def)) return true;
      if (has_def && mqtt_ascii_ci_eq(def.type, "select")) out = mqtt_universal_select_command_value(def, out);
      uint8_t out_len = (uint8_t)(out.length() > 30 ? 30 : out.length());
      master_cmd_set_universal_entity(addr, entity_id, (const uint8_t*)out.c_str(), out_len);
    }
  } else if (action == "io_out1" || action == "weller_fan") {
    master_cmd_set_io_output(addr, 0x0001, mqtt_payload_bool(value) ? 0x0001 : 0);
  } else if (action == "io_out2" || action == "weller_light") {
    master_cmd_set_io_output(addr, 0x0002, mqtt_payload_bool(value) ? 0x0002 : 0);
  } else if (action == "relay_fan") {
    uint16_t power = m->output_power ? m->output_power : 100U;
    master_cmd_set_module_output(addr, mqtt_payload_bool(value), power);
  } else if (action == "output_power") {
    uint16_t percent = (uint16_t)strtoul(value.c_str(), nullptr, 10);
    if (percent > 100) percent = 100;
    master_cmd_set_module_power(addr, (uint16_t)(percent * 10U));
  } else if (action == "weller_speed") {
    uint16_t percent = (uint16_t)strtoul(value.c_str(), nullptr, 10);
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    master_cmd_set_weller_speed(addr, (uint8_t)percent);
  } else if (action == "display_brightness") {
    uint16_t brightness = (uint16_t)strtoul(value.c_str(), nullptr, 10);
    if (brightness < 10) brightness = 10;
    if (brightness > 100) brightness = 100;
    master_cmd_set_display_settings(addr, (uint8_t)brightness, m->display_language, m->display_theme, m->display_screensaver_min);
  } else if (action == "display_language") {
    uint8_t lang = (value == "Deutsch" || value == "de" || value == "1") ? 1 : 0;
    master_cmd_set_display_settings(addr, m->display_brightness_pct ? m->display_brightness_pct : 85, lang, m->display_theme, m->display_screensaver_min);
  } else if (action == "display_theme") {
    uint8_t theme = (value == "Light" || value == "light" || value == "1") ? 1 : 0;
    master_cmd_set_display_settings(addr, m->display_brightness_pct ? m->display_brightness_pct : 85, m->display_language, theme, m->display_screensaver_min);
  } else if (action == "display_screensaver") {
    master_cmd_set_display_settings(addr, m->display_brightness_pct ? m->display_brightness_pct : 85, m->display_language, m->display_theme, mqtt_display_screensaver_value(value));
  }
  mqtt_last_publish_ms = 0;
  return true;
}

static void mqtt_publish_state(bool force = false) {
  if (!mqtt_connect_if_needed()) return;
  const uint32_t now = millis();
  if (!force && (uint32_t)(now - mqtt_last_publish_ms) < 1000UL) return;
  mqtt_last_publish_ms = now;
  heap_diag_set_context(HEAP_DIAG_CTX_MQTT, "publish_state");
  heap_diag_sample("mqtt_state_begin");

  const JbcModuleState& js = extractor.jbcState();
  const JbcModuleState& cs = scheduler.controlSettings();
  const uint16_t system_jbc_error = scheduler.systemJbcError();
  const uint16_t custom_power = customPowerFromSelectFlow(cs.select_flow ? cs.select_flow : js.select_flow);
  const MasterAlarmJson alarms = build_master_alarm_json();
  const uint8_t alarm_count = alarms.count;
  String alarm_text_json = json_escape(alarms.text.c_str());
  String dt;
  time_t now_ts = time(nullptr);
  struct tm now_tm;
  if (now_ts > 1700000000 && localtime_r(&now_ts, &now_tm)) {
    char dt_buf[24];
    snprintf(dt_buf, sizeof(dt_buf), "%04d-%02d-%02d %02d:%02d:%02d", now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
    dt = dt_buf;
  }
  String afterrun_text = duration_text_seconds((uint32_t)(extractor.afterrunLeftMs() / 1000UL));
  String main_input_name = json_escape(mqtt_main_input_label(scheduler.mainInputSourceType(), scheduler.mainInputSourceAddr(), scheduler.mainInputSourceBit()).c_str());
  String main_output_name = json_escape(mqtt_main_output_label(scheduler.preferredOutputAddr()).c_str());
  String master_ip = current_master_ip_string();
  char payload[2600];
  snprintf(payload, sizeof(payload),
           "{\"fw\":\"%s\",\"output\":%s,\"power\":%u,\"work_mask\":%u,\"afterrun_s\":%lu,\"afterrun_text\":\"%s\",\"modules\":%u,\"jbc_error\":%u,\"jbc_error_text\":\"%s\",\"alarm_count\":%u,\"alarm_text\":\"%s\",\"alarms\":%s,\"wifi_rssi\":%d,\"ip\":\"%s\",\"datetime\":\"%s\",\"mqtt_state\":%d,\"mqtt_state_text\":\"%s\",\"heap\":%lu,\"cpu\":%u,\"status_led_enabled\":%s,\"status_led_brightness\":%u,\"main_input_name\":\"%s\",\"main_output_name\":\"%s\","
           "\"suction_level\":%u,\"suction_name\":\"%s\",\"custom_power\":%u,\"select_flow\":%u,\"afterrun_power_enabled\":%s,\"afterrun_power\":%u,\"delay_work\":%u,\"delay_stand\":%u,\"stand_intakes\":%s,\"continuous_set\":%s}",
           MASTER_FW_VERSION,
           extractor.outputEnabled() ? "true" : "false",
           (unsigned)extractor.outputPower(),
           (unsigned)js.work_mask,
           (unsigned long)extractor.afterrunLeftMs() / 1000UL,
           afterrun_text.c_str(),
           (unsigned)registry.count(),
           (unsigned)system_jbc_error,
           jbc_error_text(system_jbc_error).c_str(),
           (unsigned)alarm_count,
           alarm_text_json.c_str(),
           alarms.strings_json.c_str(),
           WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
           master_ip.c_str(),
           dt.c_str(),
           mqtt_last_state,
           mqtt_state_text(mqtt_last_state),
           (unsigned long)ESP.getFreeHeap(),
           (unsigned)cpu_load_pct,
           status_led_enabled ? "true" : "false",
           (unsigned)status_led_brightness_pct,
           main_input_name.c_str(),
           main_output_name.c_str(),
           (unsigned)cs.suction_level,
           mqtt_suction_name(cs.suction_level),
           (unsigned)custom_power,
           (unsigned)cs.select_flow,
           scheduler.afterrunPowerProfileEnabled() ? "true" : "false",
           (unsigned)((scheduler.afterrunPower() + 5U) / 10U),
           (unsigned)cs.delay_work_sec,
           (unsigned)cs.delay_stand_sec,
           cs.stand_intakes ? "true" : "false",
           cs.continuous ? "true" : "false");
  mqtt_client.publish(mqtt_topic_path("state").c_str(), payload, true);
  uint8_t order[ModuleRegistry::MAX_MODULES];
  const uint8_t count = mqtt_sorted_module_indices(order, sizeof(order));
  for (uint8_t oi = 0; oi < count; ++oi) {
    const ModuleRecord& m = registry.at(order[oi]);
    if (m.online) mqtt_publish_module_state(m);
    else mqtt_publish_module_offline(m.addr);
  }
  heap_diag_sample("mqtt_state_end");
  heap_diag_clear_context(HEAP_DIAG_CTX_MQTT);
}

static void mqtt_loop() {
  if (!mqtt_enabled) {
    if (mqtt_client.connected()) {
      mqtt_publish_all_availability_offline();
      mqtt_client.disconnect();
    }
    mqtt_was_connected = false;
    mqtt_discovery_published = false;
    return;
  }
  if (mqtt_client.connected()) {
    mqtt_client.loop();
    mqtt_cleanup_tick();
    if (mqtt_ha_discovery) {
      const uint32_t now = millis();
      if (!mqtt_discovery_published || (uint32_t)(now - mqtt_next_discovery_check_ms) >= 5000UL) {
        mqtt_next_discovery_check_ms = now;
        const String sig = mqtt_module_discovery_signature_now();
        if (!mqtt_discovery_published || sig != mqtt_discovery_signature) mqtt_publish_discovery();
      }
    }
    mqtt_publish_state(false);
  } else {
    mqtt_connect_if_needed();
  }
}
