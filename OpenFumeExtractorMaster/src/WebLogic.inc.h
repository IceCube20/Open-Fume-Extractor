#pragma once

// Boolean logic storage, compiler, runtime and web editor page. Included from
// the master sketch to avoid Arduino auto-prototype issues with local types.
static constexpr uint8_t LOGIC_DEF_MAX = 32;
static constexpr uint16_t LOGIC_CHUNK_SIZE = 1200;
static constexpr uint8_t LOGIC_CHUNK_MAX = 24;
static constexpr uint8_t LOGIC_RT_MAX_NODES = 32;
static constexpr uint8_t LOGIC_RT_MAX_LINKS = 64;
static constexpr uint8_t LOGIC_RT_MAX_OUTPUTS = 32;
static constexpr uint32_t LOGIC_RT_INTERVAL_MS = 100UL;

enum LogicRtNodeType : uint8_t {
  LOGIC_NODE_UNKNOWN = 0,
  LOGIC_NODE_INPUT,
  LOGIC_NODE_OUTPUT,
  LOGIC_NODE_AND,
  LOGIC_NODE_OR,
  LOGIC_NODE_NOT,
  LOGIC_NODE_TOGGLE,
  LOGIC_NODE_SR,
  LOGIC_NODE_TON,
  LOGIC_NODE_TOF,
  LOGIC_NODE_PULSE,
  LOGIC_NODE_CLOCK,
};

struct LogicRtState {
  bool mem = false;
  bool last = false;
  uint32_t timer_ms = 0;
};

struct LogicRtNode {
  char id[16] = {0};
  char signal[48] = {0};
  uint32_t delay_ms = 0;
  uint32_t delay2_ms = 0;
  LogicRtState state;
  uint8_t type = LOGIC_NODE_UNKNOWN;
  bool value = false;
};

struct LogicRtLink {
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t port = 0;
};

struct LogicCompiledDefinition {
  bool used = false;
  bool enabled = false;
  uint8_t node_count = 0;
  uint8_t link_count = 0;
  char name[64] = {0};
  LogicRtNode* nodes = nullptr;
  LogicRtLink* links = nullptr;
};

struct LogicRtOutput {
  char signal[48] = {0};
  bool active = false;
};

static LogicCompiledDefinition logic_rt_defs[LOGIC_DEF_MAX];
static SemaphoreHandle_t logic_rt_mutex = nullptr;
static volatile uint32_t logic_rt_used_mask = 0;
static volatile bool logic_rt_cache_ready = false;
static LogicRtOutput logic_rt_last_outputs[LOGIC_RT_MAX_OUTPUTS];
static uint8_t logic_rt_last_output_count = 0;
static bool logic_rt_last_main_output = false;
static uint32_t logic_rt_last_tick_ms = 0;
static uint32_t logic_rt_last_run_ms = 0;
static uint32_t logic_rt_last_exec_us = 0;
static uint32_t logic_rt_max_exec_us = 0;

static String default_logic_json() {
  return F("{\"schema\":1,\"name\":\"Absaugung Logik\",\"enabled\":false,\"nodes\":[],\"links\":[]}");
}

static String logic_slot_key(uint8_t slot) {
  return MasterSettingsStore::logicSlotKey(slot);
}

static String logic_slot_count_key(uint8_t slot) {
  return MasterSettingsStore::logicSlotCountKey(slot);
}

static String logic_slot_part_key(uint8_t slot, uint8_t part) {
  return MasterSettingsStore::logicSlotPartKey(slot, part);
}

static String logic_slot_path(uint8_t slot) {
  return String("/logic/slot") + String(slot) + String(".json");
}

static String logic_load_legacy_nvs_open(uint8_t slot) {
  String value;
  const uint8_t chunks = master_prefs.getUChar(logic_slot_count_key(slot).c_str(), 0);
  if (chunks > 0 && chunks <= LOGIC_CHUNK_MAX) {
    value.reserve((uint32_t)chunks * LOGIC_CHUNK_SIZE);
    for (uint8_t i = 0; i < chunks; ++i) value += master_prefs.getString(logic_slot_part_key(slot, i).c_str(), String(""));
  } else {
    value = master_prefs.getString(logic_slot_key(slot).c_str(), String(""));
    if (!value.length() && slot == 0) value = master_prefs.getString(MasterSettingsStore::KEY_LOGIC_LEGACY_JSON, String(""));
  }
  value.trim();
  return value;
}

static void logic_remove_legacy_nvs_open(uint8_t slot) {
  master_prefs.remove(logic_slot_key(slot).c_str());
  master_prefs.remove(logic_slot_count_key(slot).c_str());
  for (uint8_t i = 0; i < LOGIC_CHUNK_MAX; ++i) master_prefs.remove(logic_slot_part_key(slot, i).c_str());
  if (slot == 0) master_prefs.remove(MasterSettingsStore::KEY_LOGIC_LEGACY_JSON);
}

static bool logic_save_legacy_nvs_open(uint8_t slot, const String& stored) {
  if (stored.length() > (uint32_t)LOGIC_CHUNK_SIZE * LOGIC_CHUNK_MAX) return false;
  const uint8_t chunks = (uint8_t)((stored.length() + LOGIC_CHUNK_SIZE - 1) / LOGIC_CHUNK_SIZE);
  bool ok = chunks > 0 && chunks <= LOGIC_CHUNK_MAX;
  logic_remove_legacy_nvs_open(slot);
  if (ok && master_prefs.putUChar(logic_slot_count_key(slot).c_str(), chunks) == 0) ok = false;
  for (uint8_t i = 0; ok && i < chunks; ++i) {
    const uint32_t from = (uint32_t)i * LOGIC_CHUNK_SIZE;
    const uint32_t to = (from + LOGIC_CHUNK_SIZE < stored.length()) ? from + LOGIC_CHUNK_SIZE : stored.length();
    const String part = stored.substring(from, to);
    if (master_prefs.putString(logic_slot_part_key(slot, i).c_str(), part) != part.length()) ok = false;
  }
  return ok;
}

static bool logic_save_legacy_nvs(uint8_t slot, const String& stored) {
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  const bool ok = logic_save_legacy_nvs_open(slot, stored);
  master_prefs.end();
  return ok;
}

static bool logic_save_fs(uint8_t slot, const String& stored) {
  if (!logic_fs_ready || slot >= LOGIC_DEF_MAX) return false;
  LittleFS.mkdir("/logic");
  File f = LittleFS.open(logic_slot_path(slot), "w");
  if (!f) return false;
  const size_t written = f.print(stored);
  f.close();
  return written == stored.length();
}

static String logic_load_fs(uint8_t slot) {
  if (!logic_fs_ready || slot >= LOGIC_DEF_MAX) return String("");
  File f = LittleFS.open(logic_slot_path(slot), "r");
  if (!f) return String("");
  String value = f.readString();
  f.close();
  value.trim();
  return value;
}

static bool logic_slot_exists_storage(uint8_t slot) {
  if (slot >= LOGIC_DEF_MAX) return false;
  if (logic_fs_ready && LittleFS.exists(logic_slot_path(slot))) return true;
  master_prefs.begin(MasterSettingsStore::NS_CFG, true);
  const String value = logic_load_legacy_nvs_open(slot);
  master_prefs.end();
  return value.length() > 0;
}

static String load_logic_slot_json(uint8_t slot) {
  if (slot >= LOGIC_DEF_MAX) slot = 0;
  String value = logic_load_fs(slot);
  if (!value.length()) {
    master_prefs.begin(MasterSettingsStore::NS_CFG, true);
    value = logic_load_legacy_nvs_open(slot);
    master_prefs.end();
  }
  value.trim();
  return value.length() ? value : default_logic_json();
}

static uint8_t active_logic_slot() {
  master_prefs.begin(MasterSettingsStore::NS_CFG, true);
  uint8_t slot = master_prefs.getUChar(MasterSettingsStore::KEY_LOGIC_ACTIVE, 0);
  master_prefs.end();
  return slot < LOGIC_DEF_MAX ? slot : 0;
}

static void set_active_logic_slot(uint8_t slot) {
  if (slot >= LOGIC_DEF_MAX) slot = 0;
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  master_prefs.putUChar(MasterSettingsStore::KEY_LOGIC_ACTIVE, slot);
  master_prefs.end();
}

static String logic_json_name(const String& value, uint8_t slot) {
  String fallback = slot == 0 ? web_text("Absaugung Logik", "Extractor logic") : String("Logic ") + String(slot + 1);
  String name = json_get_string_field(value, "name", fallback);
  name.trim();
  return name.length() ? name : fallback;
}

static void logic_str_copy(char* dst, size_t dst_len, const String& src) {
  if (!dst || !dst_len) return;
  src.substring(0, dst_len - 1).toCharArray(dst, dst_len);
  dst[dst_len - 1] = 0;
}

static int logic_find_matching(const String& src, int start, char open_ch, char close_ch) {
  if (start < 0 || start >= (int)src.length() || src[start] != open_ch) return -1;
  bool in_string = false;
  bool esc = false;
  int depth = 0;
  for (int i = start; i < (int)src.length(); ++i) {
    const char c = src[i];
    if (in_string) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') { in_string = true; continue; }
    if (c == open_ch) ++depth;
    else if (c == close_ch && --depth == 0) return i;
  }
  return -1;
}

static bool logic_next_object(const String& src, int& pos, String& obj) {
  int start = src.indexOf('{', pos);
  if (start < 0) return false;
  int end = logic_find_matching(src, start, '{', '}');
  if (end < 0) return false;
  obj = src.substring(start, end + 1);
  pos = end + 1;
  return true;
}

static uint8_t logic_node_type_from_string(const String& type) {
  if (type == "input") return LOGIC_NODE_INPUT;
  if (type == "output") return LOGIC_NODE_OUTPUT;
  if (type == "and") return LOGIC_NODE_AND;
  if (type == "or") return LOGIC_NODE_OR;
  if (type == "not") return LOGIC_NODE_NOT;
  if (type == "toggle") return LOGIC_NODE_TOGGLE;
  if (type == "sr") return LOGIC_NODE_SR;
  if (type == "ton") return LOGIC_NODE_TON;
  if (type == "tof") return LOGIC_NODE_TOF;
  if (type == "pulse") return LOGIC_NODE_PULSE;
  if (type == "clock") return LOGIC_NODE_CLOCK;
  return LOGIC_NODE_UNKNOWN;
}

static int8_t logic_find_node(const LogicRtNode* nodes, uint8_t count, const char* id) {
  if (!nodes || !id || !id[0]) return -1;
  for (uint8_t i = 0; i < count; ++i) if (strcmp(nodes[i].id, id) == 0) return (int8_t)i;
  return -1;
}

static void* logic_rt_alloc(size_t bytes) {
  if (!bytes) return nullptr;
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  return p;
}

static void logic_def_release(LogicCompiledDefinition& def) {
  if (def.nodes) heap_caps_free(def.nodes);
  if (def.links) heap_caps_free(def.links);
  def = LogicCompiledDefinition();
}

static bool logic_rt_lock(TickType_t timeout = portMAX_DELAY) {
  if (!logic_rt_mutex) logic_rt_mutex = xSemaphoreCreateMutex();
  return !logic_rt_mutex || xSemaphoreTake(logic_rt_mutex, timeout) == pdTRUE;
}

static void logic_rt_unlock() {
  if (logic_rt_mutex) xSemaphoreGive(logic_rt_mutex);
}

static bool logic_compile_json(const String& json, uint8_t slot, LogicCompiledDefinition& out) {
  out = LogicCompiledDefinition();
  out.used = true;
  out.enabled = json_get_bool_field(json, "enabled", false);
  logic_str_copy(out.name, sizeof(out.name), logic_json_name(json, slot));

  out.nodes = static_cast<LogicRtNode*>(logic_rt_alloc(sizeof(LogicRtNode) * LOGIC_RT_MAX_NODES));
  out.links = static_cast<LogicRtLink*>(logic_rt_alloc(sizeof(LogicRtLink) * LOGIC_RT_MAX_LINKS));
  if (!out.nodes || !out.links) {
    logic_def_release(out);
    return false;
  }
  memset(out.nodes, 0, sizeof(LogicRtNode) * LOGIC_RT_MAX_NODES);
  memset(out.links, 0, sizeof(LogicRtLink) * LOGIC_RT_MAX_LINKS);

  int key = json.indexOf("\"nodes\"");
  int arr = key >= 0 ? json.indexOf('[', key) : -1;
  int end = arr >= 0 ? logic_find_matching(json, arr, '[', ']') : -1;
  if (arr >= 0 && end >= 0) {
    const String part = json.substring(arr + 1, end);
    int pos = 0;
    String obj;
    while (out.node_count < LOGIC_RT_MAX_NODES && logic_next_object(part, pos, obj)) {
      LogicRtNode& n = out.nodes[out.node_count];
      const String id = json_get_string_field(obj, "id", String(""));
      const String type = json_get_string_field(obj, "type", String(""));
      logic_str_copy(n.id, sizeof(n.id), id);
      logic_str_copy(n.signal, sizeof(n.signal), json_get_string_field(obj, "signal", String("")));
      n.delay_ms = json_get_u32_field(obj, "delay", 0);
      n.delay2_ms = json_get_u32_field(obj, "delay2", 0);
      n.type = logic_node_type_from_string(type);
      n.value = false;
      if (n.id[0] && type.length()) ++out.node_count;
      else memset(&n, 0, sizeof(n));
    }
  }

  key = json.indexOf("\"links\"");
  arr = key >= 0 ? json.indexOf('[', key) : -1;
  end = arr >= 0 ? logic_find_matching(json, arr, '[', ']') : -1;
  if (arr >= 0 && end >= 0) {
    const String part = json.substring(arr + 1, end);
    int pos = 0;
    String obj;
    while (out.link_count < LOGIC_RT_MAX_LINKS && logic_next_object(part, pos, obj)) {
      const String from_id = json_get_string_field(obj, "from", String(""));
      const String to_id = json_get_string_field(obj, "to", String(""));
      const int8_t from = logic_find_node(out.nodes, out.node_count, from_id.c_str());
      const int8_t to = logic_find_node(out.nodes, out.node_count, to_id.c_str());
      if (from < 0 || to < 0) continue;
      LogicRtLink& l = out.links[out.link_count++];
      l.from = (uint8_t)from;
      l.to = (uint8_t)to;
      l.port = (uint8_t)json_get_u32_field(obj, "port", 0);
    }
  }
  return true;
}

static void logic_transfer_state(const LogicCompiledDefinition& old_def, LogicCompiledDefinition& new_def) {
  if (!old_def.nodes || !new_def.nodes) return;
  for (uint8_t n = 0; n < new_def.node_count; ++n) {
    for (uint8_t o = 0; o < old_def.node_count; ++o) {
      if (new_def.nodes[n].type == old_def.nodes[o].type && strcmp(new_def.nodes[n].id, old_def.nodes[o].id) == 0) {
        new_def.nodes[n].state = old_def.nodes[o].state;
        break;
      }
    }
  }
}

static void logic_cache_install(uint8_t slot, LogicCompiledDefinition& fresh) {
  if (slot >= LOGIC_DEF_MAX) { logic_def_release(fresh); return; }
  LogicCompiledDefinition old;
  if (!logic_rt_lock()) { logic_def_release(fresh); return; }
  old = logic_rt_defs[slot];
  logic_transfer_state(old, fresh);
  logic_rt_defs[slot] = fresh;
  fresh = LogicCompiledDefinition();
  logic_rt_used_mask |= (1UL << slot);
  logic_rt_unlock();
  logic_def_release(old);
}

static void logic_cache_remove_slot(uint8_t slot) {
  if (slot >= LOGIC_DEF_MAX) return;
  LogicCompiledDefinition old;
  if (!logic_rt_lock()) return;
  old = logic_rt_defs[slot];
  logic_rt_defs[slot] = LogicCompiledDefinition();
  logic_rt_used_mask &= ~(1UL << slot);
  logic_rt_unlock();
  logic_def_release(old);
}

static void logic_cache_clear_all() {
  logic_rt_cache_ready = false;
  for (uint8_t slot = 0; slot < LOGIC_DEF_MAX; ++slot) logic_cache_remove_slot(slot);
  logic_rt_last_output_count = 0;
  logic_rt_last_main_output = false;
}

static void logic_cache_reload_all() {
  heap_diag_set_context(HEAP_DIAG_CTX_LOOP, "logic_cache");
  heap_diag_sample("logic_cache_begin");
  if (!logic_rt_mutex) logic_rt_mutex = xSemaphoreCreateMutex();
  logic_cache_clear_all();
  uint8_t loaded = 0;
  for (uint8_t slot = 0; slot < LOGIC_DEF_MAX; ++slot) {
    if (!logic_slot_exists_storage(slot)) continue;
    const String json = load_logic_slot_json(slot);
    LogicCompiledDefinition compiled;
    if (logic_compile_json(json, slot, compiled)) {
      logic_cache_install(slot, compiled);
      ++loaded;
    } else {
      Serial.printf("[LOGIC] slot %u cache allocation failed\n", (unsigned)slot);
    }
  }
  logic_rt_last_tick_ms = millis();
  logic_rt_last_run_ms = 0;
  logic_rt_cache_ready = true;
  Serial.printf("[LOGIC] %u definition(s) cached, heap=%u KB, psram=%u KB\n",
                (unsigned)loaded,
                (unsigned)(ESP.getFreeHeap() / 1024U),
                (unsigned)(ESP.getFreePsram() / 1024U));
  heap_diag_sample("logic_cache_end");
  heap_diag_clear_context(HEAP_DIAG_CTX_LOOP);
}

static bool logic_slot_exists(uint8_t slot) {
  if (slot >= LOGIC_DEF_MAX) return false;
  if (logic_rt_cache_ready) return (logic_rt_used_mask & (1UL << slot)) != 0;
  return logic_slot_exists_storage(slot);
}

static uint8_t first_used_logic_slot(uint8_t fallback = 0) {
  const uint32_t mask = logic_rt_cache_ready ? logic_rt_used_mask : 0;
  if (logic_rt_cache_ready) {
    for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) if (mask & (1UL << i)) return i;
  } else {
    for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) if (logic_slot_exists_storage(i)) return i;
  }
  return fallback < LOGIC_DEF_MAX ? fallback : 0;
}

static String logic_name_norm(String name) {
  name.trim();
  name.toLowerCase();
  return name;
}

static bool logic_name_exists(const String& name, uint8_t ignore_slot) {
  const String wanted = logic_name_norm(name);
  if (!wanted.length()) return false;
  if (logic_rt_cache_ready && logic_rt_lock(pdMS_TO_TICKS(1000))) {
    for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) {
      if (i == ignore_slot || !logic_rt_defs[i].used) continue;
      if (logic_name_norm(String(logic_rt_defs[i].name)) == wanted) {
        logic_rt_unlock();
        return true;
      }
    }
    logic_rt_unlock();
    return false;
  }
  for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) {
    if (i == ignore_slot || !logic_slot_exists_storage(i)) continue;
    if (logic_name_norm(logic_json_name(load_logic_slot_json(i), i)) == wanted) return true;
  }
  return false;
}

static String load_logic_json() {
  return load_logic_slot_json(active_logic_slot());
}

static bool save_logic_slot_json(uint8_t slot, const String& value) {
  if (slot >= LOGIC_DEF_MAX) slot = 0;
  const String stored = value.length() ? value : default_logic_json();

  LogicCompiledDefinition compiled;
  if (!logic_compile_json(stored, slot, compiled)) return false;

  bool ok = logic_save_fs(slot, stored);
  if (!ok) ok = logic_save_legacy_nvs(slot, stored);
  if (!ok) {
    logic_def_release(compiled);
    return false;
  }
  String verify = load_logic_slot_json(slot);
  verify.trim();
  if (verify != stored) {
    logic_def_release(compiled);
    return false;
  }
  logic_cache_install(slot, compiled);
  return true;
}

static void save_logic_json(const String& value) {
  save_logic_slot_json(active_logic_slot(), value);
}

static void remove_logic_slot_storage_open(uint8_t slot) {
  if (logic_fs_ready) LittleFS.remove(logic_slot_path(slot));
  logic_remove_legacy_nvs_open(slot);
}

static void remove_logic_slot_storage(uint8_t slot) {
  if (slot >= LOGIC_DEF_MAX) return;
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  remove_logic_slot_storage_open(slot);
  master_prefs.end();
  logic_cache_remove_slot(slot);
}

static void logic_migrate_legacy_slots_to_fs() {
  if (!logic_fs_ready) return;
  for (uint8_t slot = 0; slot < LOGIC_DEF_MAX; ++slot) {
    if (LittleFS.exists(logic_slot_path(slot))) continue;
    master_prefs.begin(MasterSettingsStore::NS_CFG, true);
    String legacy = logic_load_legacy_nvs_open(slot);
    master_prefs.end();
    if (legacy.length() && logic_save_fs(slot, legacy)) {
      master_prefs.begin(MasterSettingsStore::NS_CFG, false);
      logic_remove_legacy_nvs_open(slot);
      master_prefs.end();
    }
  }
}

static void web_handle_logic_list() {
  const uint8_t active = active_logic_slot();
  String json;
  json.reserve(3600);
  json += "{\"active\":"; json += active; json += ",\"items\":[";
  const bool locked = logic_rt_cache_ready && logic_rt_lock(pdMS_TO_TICKS(1000));
  for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) {
    const bool used = locked ? logic_rt_defs[i].used : logic_slot_exists_storage(i);
    String name;
    if (locked && used) name = String(logic_rt_defs[i].name);
    else if (used) name = logic_json_name(load_logic_slot_json(i), i);
    else name = i == 0 ? web_text("Absaugung Logik", "Extractor logic") : String("Logic ") + String(i + 1);
    if (i) json += ',';
    json += "{\"slot\":"; json += i;
    json += ",\"used\":"; json += used ? "true" : "false";
    json += ",\"name\":\""; json += json_escape(name.c_str()); json += "\"";
    json += "}";
  }
  if (locked) logic_rt_unlock();
  json += "]}";
  web.send(200, "application/json; charset=utf-8", json);
}

static void web_handle_logic_select() {
  uint8_t slot = web.hasArg("slot") ? (uint8_t)strtoul(web.arg("slot").c_str(), nullptr, 0) : 0;
  if (slot >= LOGIC_DEF_MAX) { web.send(400, "text/plain; charset=utf-8", "bad slot"); return; }
  set_active_logic_slot(slot);
  web.send(200, "text/plain; charset=utf-8", "ok");
}

static void web_handle_logic_new() {
  String name = web.hasArg("name") ? web.arg("name") : String("");
  name.trim();
  if (!name.length()) name = web_text("Neue Logik", "New logic");
  if (logic_name_exists(name, 0xFF)) { web.send(409, "text/plain; charset=utf-8", web_text("Name ist schon vergeben", "Name already exists")); return; }
  uint8_t slot = 0xFF;
  for (uint8_t i = 0; i < LOGIC_DEF_MAX; ++i) {
    if (!logic_slot_exists(i)) { slot = i; break; }
  }
  if (slot == 0xFF) { web.send(409, "text/plain; charset=utf-8", "no free logic slot"); return; }
  String value = F("{\"schema\":1,\"name\":\"");
  value += json_escape(name.c_str());
  value += F("\",\"enabled\":false,\"nodes\":[],\"links\":[]}");
  if (!save_logic_slot_json(slot, value)) {
    web.send(507, "text/plain; charset=utf-8", String("logic slot ") + String(slot) + " write/cache failed");
    return;
  }
  set_active_logic_slot(slot);
  web.send(200, "text/plain; charset=utf-8", String(slot));
}

static void web_handle_logic_delete() {
  uint8_t slot = web.hasArg("slot") ? (uint8_t)strtoul(web.arg("slot").c_str(), nullptr, 0) : active_logic_slot();
  if (slot >= LOGIC_DEF_MAX) { web.send(400, "text/plain; charset=utf-8", "bad slot"); return; }
  master_prefs.begin(MasterSettingsStore::NS_CFG, false);
  const uint8_t old_active = master_prefs.getUChar(MasterSettingsStore::KEY_LOGIC_ACTIVE, 0);
  remove_logic_slot_storage_open(slot);
  master_prefs.end();
  logic_cache_remove_slot(slot);
  const uint8_t next = (old_active == slot) ? first_used_logic_slot(0) : old_active;
  set_active_logic_slot(next);
  web.send(200, "text/plain; charset=utf-8", String(next));
}

static void web_handle_logic_json() {
  uint8_t slot = web.hasArg("slot") ? (uint8_t)strtoul(web.arg("slot").c_str(), nullptr, 0) : active_logic_slot();
  if (web.method() == HTTP_GET) {
    if (slot >= LOGIC_DEF_MAX) { web.send(400, "text/plain; charset=utf-8", "bad slot"); return; }
    web.send(200, "application/json; charset=utf-8", load_logic_slot_json(slot));
    return;
  }
  String body = web.hasArg("plain") ? web.arg("plain") : String("");
  if (!body.length() && web.hasArg("logic_json")) body = web.arg("logic_json");
  if (!body.length()) {
    for (uint8_t i = 0; i < web.args(); ++i) {
      const String arg_name = web.argName(i);
      if (arg_name != "slot" && arg_name != "logic_slot") { body = web.arg(i); break; }
    }
  }
  body.trim();
  if (!body.length()) { web.send(400, "text/plain; charset=utf-8", "missing logic json"); return; }
  if (body.length() > 24000) { web.send(413, "text/plain; charset=utf-8", "logic json too large"); return; }
  if (body.indexOf("\"nodes\"") < 0 || body.indexOf("\"links\"") < 0) { web.send(400, "text/plain; charset=utf-8", "invalid logic graph"); return; }
  const uint32_t body_slot = json_get_u32_field(body, "slot", slot);
  if (body_slot >= LOGIC_DEF_MAX) { web.send(400, "text/plain; charset=utf-8", "bad slot"); return; }
  slot = (uint8_t)body_slot;
  const String name = logic_json_name(body, slot);
  if (logic_name_exists(name, slot)) { web.send(409, "text/plain; charset=utf-8", web_text("Name ist schon vergeben", "Name already exists")); return; }
  if (!save_logic_slot_json(slot, body)) { web.send(507, "text/plain; charset=utf-8", String("logic slot ") + String(slot) + " write/cache failed"); return; }
  set_active_logic_slot(slot);
  web.send(200, "text/plain; charset=utf-8", String("logic saved slot ") + String(slot));
}

static bool logic_signal_active(const char* signal) {
  if (!signal || !signal[0] || strcmp(signal, "manual") == 0) return false;
  unsigned addr = 0, bit = 0;
  int consumed = 0;
  if (sscanf(signal, "jbc:%u%n", &addr, &consumed) == 1 && signal[consumed] == 0) {
    for (uint8_t i = 0; i < registry.count(); ++i) {
      const ModuleRecord& rec = registry.at(i);
      if (!rec.online || !(rec.caps & CAP_JBC_ACTIVITY)) continue;
      if (addr && rec.addr != addr) continue;
      if (rec.jbc_work_mask) return true;
    }
    return false;
  }
  consumed = 0;
  if (sscanf(signal, "io:%u:%u%n", &addr, &bit, &consumed) == 2 && signal[consumed] == 0) {
    const ModuleRecord* rec = registry.find((uint8_t)addr);
    return rec && rec->online && (rec->caps & CAP_INPUT_KEYS) && bit < 16 && (rec->io_input_mask & (uint16_t)(1U << bit));
  }
  consumed = 0;
  if (sscanf(signal, "uni:%u:entity:%u%n", &addr, &bit, &consumed) == 2 && signal[consumed] == 0 && bit <= 255) {
    const ModuleRecord* rec = registry.find((uint8_t)addr);
    if (!rec || !rec->online || !rec->universal_entities_valid) return false;
    for (uint8_t i = 0; i < rec->universal_entity_count; ++i) {
      const UniversalEntityState& e = rec->universal_entities[i];
      if (e.id != (uint8_t)bit || !e.len) continue;
      // Profile switch/binary_sensor entities are transported as a one-byte
      // boolean. Keep a text fallback for older/custom bridge firmware.
      if (e.len == 1) return e.data[0] != 0 && e.data[0] != '0';
      char text[33];
      const uint8_t n = e.len < sizeof(text) ? e.len : (uint8_t)(sizeof(text) - 1);
      memcpy(text, e.data, n);
      text[n] = 0;
      String v(text);
      v.trim();
      v.toLowerCase();
      return v.length() && v != "0" && v != "off" && v != "false" && v != "aus";
    }
    return false;
  }
  consumed = 0;
  if (sscanf(signal, "uni:%u:input%n", &addr, &consumed) == 1 && signal[consumed] == 0) {
    const ModuleRecord* rec = registry.find((uint8_t)addr);
    if (!rec || !rec->online || !rec->universal_entities_valid) return false;
    for (uint8_t i = 0; i < rec->universal_entity_count; ++i) if (rec->universal_entities[i].len && rec->universal_entities[i].data[0] && rec->universal_entities[i].data[0] != '0') return true;
  }
  return false;
}

static bool logic_port_value(const LogicCompiledDefinition& def, uint8_t node_index, uint8_t port) {
  for (uint8_t i = 0; i < def.link_count; ++i) {
    const LogicRtLink& link = def.links[i];
    if (link.to == node_index && link.port == port && link.from < def.node_count && def.nodes[link.from].value) return true;
  }
  return false;
}

static uint8_t logic_linked_count(const LogicCompiledDefinition& def, uint8_t node_index, uint8_t* true_count) {
  uint8_t total = 0;
  uint8_t on = 0;
  for (uint8_t i = 0; i < def.link_count; ++i) {
    const LogicRtLink& link = def.links[i];
    if (link.to != node_index || link.from >= def.node_count) continue;
    ++total;
    if (def.nodes[link.from].value) ++on;
  }
  if (true_count) *true_count = on;
  return total;
}

static void logic_collect_output(LogicRtOutput* outputs, uint8_t& output_count, const char* signal, bool active) {
  if (!signal || !signal[0]) return;
  for (uint8_t i = 0; i < output_count; ++i) {
    if (strcmp(outputs[i].signal, signal) == 0) {
      outputs[i].active = outputs[i].active || active;
      return;
    }
  }
  if (output_count >= LOGIC_RT_MAX_OUTPUTS) return;
  strncpy(outputs[output_count].signal, signal, sizeof(outputs[output_count].signal) - 1);
  outputs[output_count].signal[sizeof(outputs[output_count].signal) - 1] = 0;
  outputs[output_count].active = active;
  ++output_count;
}

static bool logic_last_output_value(const char* signal, bool& value) {
  for (uint8_t i = 0; i < logic_rt_last_output_count; ++i) {
    if (strcmp(logic_rt_last_outputs[i].signal, signal) == 0) {
      value = logic_rt_last_outputs[i].active;
      return true;
    }
  }
  return false;
}

static uint8_t logic_extractor_action_from_signal(const char* signal) {
  if (!signal) return MasterScheduler::EXTRACTOR_ACTION_NONE;
  if (strcmp(signal, "extractor:action:level_next") == 0) return MasterScheduler::EXTRACTOR_ACTION_LEVEL_NEXT;
  if (strcmp(signal, "extractor:action:level_previous") == 0) return MasterScheduler::EXTRACTOR_ACTION_LEVEL_PREVIOUS;
  if (strcmp(signal, "extractor:action:level_high") == 0) return MasterScheduler::EXTRACTOR_ACTION_LEVEL_HIGH;
  if (strcmp(signal, "extractor:action:level_medium") == 0) return MasterScheduler::EXTRACTOR_ACTION_LEVEL_MEDIUM;
  if (strcmp(signal, "extractor:action:level_low") == 0) return MasterScheduler::EXTRACTOR_ACTION_LEVEL_LOW;
  if (strcmp(signal, "extractor:action:level_custom") == 0) return MasterScheduler::EXTRACTOR_ACTION_LEVEL_CUSTOM;
  if (strcmp(signal, "extractor:action:power_plus_1") == 0) return MasterScheduler::EXTRACTOR_ACTION_POWER_PLUS_1;
  if (strcmp(signal, "extractor:action:power_minus_1") == 0) return MasterScheduler::EXTRACTOR_ACTION_POWER_MINUS_1;
  if (strcmp(signal, "extractor:action:power_plus_10") == 0) return MasterScheduler::EXTRACTOR_ACTION_POWER_PLUS_10;
  if (strcmp(signal, "extractor:action:power_minus_10") == 0) return MasterScheduler::EXTRACTOR_ACTION_POWER_MINUS_10;
  return MasterScheduler::EXTRACTOR_ACTION_NONE;
}

static void logic_apply_output_signal(const char* signal, bool active) {
  if (!signal || !signal[0]) return;
  const uint8_t extractor_action = logic_extractor_action_from_signal(signal);
  if (extractor_action != MasterScheduler::EXTRACTOR_ACTION_NONE) {
    if (active) scheduler.queueExtractorAction(extractor_action);
    return;
  }
  unsigned addr = 0, bit = 0;
  int consumed = 0;
  if (strcmp(signal, "main_output") == 0) {
    scheduler.setLogicExternalInput(active);
    logic_rt_last_main_output = active;
    return;
  }
  if (sscanf(signal, "io:%u:main%n", &addr, &consumed) == 1 && signal[consumed] == 0) {
    scheduler.setModuleOutput((uint8_t)addr, active, scheduler.controlSettings().select_flow);
    return;
  }
  consumed = 0;
  if (sscanf(signal, "io:%u:%u%n", &addr, &bit, &consumed) == 2 && signal[consumed] == 0 && bit >= 2 && bit < 16) {
    const uint16_t mask = (uint16_t)(1U << (bit - 2));
    scheduler.setIoOutput((uint8_t)addr, mask, active ? mask : 0);
    return;
  }
  consumed = 0;
  if (sscanf(signal, "weller:%u:fan%n", &addr, &consumed) == 1 && signal[consumed] == 0) {
    scheduler.setIoOutput((uint8_t)addr, 0x0001, active ? 0x0001 : 0);
    return;
  }
  consumed = 0;
  if (sscanf(signal, "weller:%u:light%n", &addr, &consumed) == 1 && signal[consumed] == 0) {
    scheduler.setIoOutput((uint8_t)addr, 0x0002, active ? 0x0002 : 0);
    return;
  }
  consumed = 0;
  if (sscanf(signal, "uni:%u:switch:%u%n", &addr, &bit, &consumed) == 2 && signal[consumed] == 0 && bit <= 255) {
    const uint8_t value = active ? (uint8_t)'1' : (uint8_t)'0';
    scheduler.setUniversalEntity((uint8_t)addr, (uint8_t)bit, &value, 1);
    return;
  }
  consumed = 0;
  if (sscanf(signal, "uni:%u:button:%u%n", &addr, &bit, &consumed) == 2 && signal[consumed] == 0 && bit <= 255) {
    // A button has no OFF command. Trigger it only on the rising logic edge;
    // logic_runtime_tick() already calls this function only when a signal
    // changes, so the held-high state is not repeatedly transmitted.
    if (active) {
      const uint8_t value = (uint8_t)'1';
      scheduler.setUniversalEntity((uint8_t)addr, (uint8_t)bit, &value, 1);
    }
    return;
  }
  consumed = 0;
  if (sscanf(signal, "uni:%u:output%n", &addr, &consumed) == 1 && signal[consumed] == 0) {
    scheduler.setModuleOutput((uint8_t)addr, active, scheduler.controlSettings().select_flow);
    return;
  }
}

static void logic_eval_definition(LogicCompiledDefinition& def, uint32_t dt_ms, LogicRtOutput* outputs, uint8_t& output_count) {
  if (!def.used || !def.enabled || !def.nodes || !def.node_count) return;
  for (uint8_t pass = 0; pass < 8; ++pass) {
    for (uint8_t i = 0; i < def.node_count; ++i) {
      LogicRtNode& n = def.nodes[i];
      LogicRtState& st = n.state;
      bool v = false;
      uint8_t linked_on = 0;
      const uint8_t linked_total = logic_linked_count(def, i, &linked_on);
      const bool p0 = logic_port_value(def, i, 0);
      const bool p1 = logic_port_value(def, i, 1);
      const bool p2 = logic_port_value(def, i, 2);
      switch (n.type) {
        case LOGIC_NODE_INPUT: v = logic_signal_active(n.signal); break;
        case LOGIC_NODE_AND: v = linked_total && linked_on == linked_total; break;
        case LOGIC_NODE_OR: v = linked_on > 0; break;
        case LOGIC_NODE_NOT: v = linked_total ? !p0 : false; break;
        case LOGIC_NODE_TOGGLE:
          if (p2) st.mem = false;
          else if (p1) st.mem = true;
          else if (p0 && !st.last) st.mem = !st.mem;
          st.last = p0;
          v = st.mem;
          break;
        case LOGIC_NODE_SR:
          if (p1) st.mem = false;
          if (p0) st.mem = true;
          v = st.mem;
          break;
        case LOGIC_NODE_TON:
          if (p0) {
            if (pass == 7) {
              const uint32_t sum = st.timer_ms + dt_ms;
              st.timer_ms = (sum < st.timer_ms || sum > 0x7FFFFFFFUL) ? 0x7FFFFFFFUL : sum;
            }
            v = st.timer_ms >= n.delay_ms;
          } else {
            if (pass == 7) st.timer_ms = 0;
            v = false;
          }
          break;
        case LOGIC_NODE_TOF:
          if (p0) {
            if (pass == 7) st.timer_ms = n.delay_ms;
            v = true;
          } else {
            if (pass == 7) st.timer_ms = (st.timer_ms > dt_ms) ? (st.timer_ms - dt_ms) : 0;
            v = st.timer_ms > 0;
          }
          break;
        case LOGIC_NODE_PULSE:
          v = p0 && !st.last;
          if (pass == 7) st.last = p0;
          break;
        case LOGIC_NODE_CLOCK: {
          const uint32_t on_ms = n.delay_ms ? n.delay_ms : 1000UL;
          const uint32_t off_ms = n.delay2_ms ? n.delay2_ms : on_ms;
          if (!p0) {
            if (pass == 7) {
              st.mem = false;
              st.last = false;
              st.timer_ms = 0;
            }
            v = false;
          } else {
            if (pass == 7) {
              if (!st.last) {
                st.mem = true;
                st.timer_ms = 0;
              } else {
                const uint32_t sum = st.timer_ms + dt_ms;
                st.timer_ms = (sum < st.timer_ms || sum > 0x7FFFFFFFUL) ? 0x7FFFFFFFUL : sum;
                for (uint8_t guard = 0; guard < 8; ++guard) {
                  const uint32_t phase_ms = st.mem ? on_ms : off_ms;
                  if (st.timer_ms < phase_ms) break;
                  st.timer_ms -= phase_ms;
                  st.mem = !st.mem;
                }
              }
              st.last = true;
            }
            v = st.mem;
          }
          break;
        }
        case LOGIC_NODE_OUTPUT: v = p0; break;
        default: v = false; break;
      }
      n.value = v;
    }
  }
  for (uint8_t i = 0; i < def.node_count; ++i) {
    if (def.nodes[i].type == LOGIC_NODE_OUTPUT) logic_collect_output(outputs, output_count, def.nodes[i].signal, def.nodes[i].value);
  }
}

static void logic_runtime_tick() {
  if (!logic_rt_cache_ready) return;
  const uint32_t now = millis();
  if ((uint32_t)(now - logic_rt_last_run_ms) < LOGIC_RT_INTERVAL_MS) return;
  const uint32_t start_us = micros();
  const uint32_t elapsed_ms = logic_rt_last_tick_ms ? (uint32_t)(now - logic_rt_last_tick_ms) : 0;
  const uint32_t dt_ms = elapsed_ms > 500UL ? 500UL : elapsed_ms;
  logic_rt_last_tick_ms = now;
  logic_rt_last_run_ms = now;

  static LogicRtOutput outputs[LOGIC_RT_MAX_OUTPUTS];
  memset(outputs, 0, sizeof(outputs));
  uint8_t output_count = 0;

  if (!logic_rt_lock(pdMS_TO_TICKS(20))) return;
  const uint32_t mask = logic_rt_used_mask;
  for (uint8_t slot = 0; slot < LOGIC_DEF_MAX; ++slot) {
    if (mask & (1UL << slot)) logic_eval_definition(logic_rt_defs[slot], dt_ms, outputs, output_count);
  }
  logic_rt_unlock();

  bool main_present = false;
  for (uint8_t i = 0; i < output_count; ++i) {
    if (strcmp(outputs[i].signal, "main_output") == 0) main_present = true;
    bool last = false;
    const bool known = logic_last_output_value(outputs[i].signal, last);
    const bool is_action = logic_extractor_action_from_signal(outputs[i].signal) != MasterScheduler::EXTRACTOR_ACTION_NONE;
    // A definition loaded while already high must first return low. This keeps
    // event outputs from changing settings unexpectedly after boot or reload.
    if ((!is_action || known) && (!known || last != outputs[i].active)) {
      logic_apply_output_signal(outputs[i].signal, outputs[i].active);
    }
  }
  for (uint8_t i = 0; i < logic_rt_last_output_count; ++i) {
    bool still_present = false;
    for (uint8_t j = 0; j < output_count; ++j) {
      if (strcmp(logic_rt_last_outputs[i].signal, outputs[j].signal) == 0) { still_present = true; break; }
    }
    if (!still_present) logic_apply_output_signal(logic_rt_last_outputs[i].signal, false);
  }
  if (!main_present && logic_rt_last_main_output) scheduler.setLogicExternalInput(false);
  logic_rt_last_output_count = output_count;
  for (uint8_t i = 0; i < output_count; ++i) logic_rt_last_outputs[i] = outputs[i];

  logic_rt_last_exec_us = (uint32_t)(micros() - start_us);
  if (logic_rt_last_exec_us > logic_rt_max_exec_us) logic_rt_max_exec_us = logic_rt_last_exec_us;
}

static void web_handle_logic() {
  String html;
  html.reserve(34000);
  web_shell_begin(html, web_text("Logik Designer", "Logic Designer"), web_text("Automation", "Automation"), "logic");
  html += F(R"HTML(
<style>
main{max-width:calc(100vw - 48px)!important;width:calc(100vw - 48px)!important}.logic-top{display:grid;grid-template-columns:280px minmax(980px,1fr) 300px;gap:14px;align-items:start}.logic-panel{border:1px solid var(--line);background:var(--card);border-radius:8px;padding:14px}.logic-toolbar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}.logic-enable{display:inline-flex;align-items:center;gap:8px;min-height:42px;padding:0 12px;border:1px solid #303640;border-radius:8px;background:#10151b;color:#dce5ef;font-weight:800}.logic-enable input{width:18px;height:18px;accent-color:#31c85a}.logic-palette{display:grid;grid-template-columns:1fr 1fr;gap:8px}.logic-palette button{min-height:46px;padding:7px 8px;gap:8px;justify-content:flex-start;align-items:center}.logic-chip{display:inline-flex;align-items:center;justify-content:center;width:29px;height:29px;border-radius:8px;background:#0d141b;border:1px solid #355069;color:#64e29a;font-weight:900;font-size:11px;letter-spacing:0}.logic-workspace{position:relative;height:calc(100vh - 286px);min-height:790px;border:1px solid #303640;background:#11151a;border-radius:8px;overflow:auto}.logic-canvas{position:relative;width:2800px;height:1700px;background:radial-gradient(circle,#303844 1px,transparent 1px);background-size:22px 22px}.logic-svg{position:absolute;left:0;top:0;width:2800px;height:1700px;pointer-events:auto}.logic-node{position:absolute;width:138px;min-height:118px;border:1px solid #3a4552;background:#20262e;border-radius:8px;box-shadow:0 8px 22px rgba(0,0,0,.24);user-select:none;touch-action:none;z-index:2}.logic-node.is-on{border-color:#59c77a;box-shadow:0 0 0 2px rgba(89,199,122,.18),0 8px 22px rgba(0,0,0,.24)}.logic-node.live-on{box-shadow:0 0 0 2px rgba(49,200,90,.24),0 8px 22px rgba(0,0,0,.24)}.logic-node.live-offline{opacity:.72}.logic-node h3{margin:0;padding:7px 34px;border-bottom:1px solid #303844;font-size:13px;min-height:38px;text-align:center}.logic-symbol{position:absolute;left:7px;top:7px;width:24px;height:24px;border-radius:8px;background:#10161d;border:1px solid #3a4552;color:#77dd96;display:flex;align-items:center;justify-content:center;font-weight:900;font-size:10px;letter-spacing:.02em}.logic-node small{display:block;color:#92a0af;font-size:10px;margin-top:1px}.logic-node .body{padding:8px 9px;color:#dce5ef;font-size:12px;line-height:1.2}.logic-live{display:inline-flex;align-items:center;gap:6px;margin-top:7px;padding:4px 7px;border:1px solid #313b46;border-radius:999px;background:#10161d;color:#98a8b8;font-size:11px;font-weight:850}.logic-live-dot{width:8px;height:8px;border-radius:50%;background:#596473}.logic-live.on{border-color:#2f7d4a;color:#dff9e8}.logic-live.on .logic-live-dot{background:#31c85a;box-shadow:0 0 10px rgba(49,200,90,.7)}.logic-live.off .logic-live-dot{background:#6a7280}.logic-live.offline{border-color:#65413f;color:#f1b5ae}.logic-live.offline .logic-live-dot{background:#d54a45}.logic-inline-sim{margin-top:7px;display:flex;align-items:center;justify-content:space-between;gap:8px}.logic-port{position:absolute;width:12px;height:12px;border-radius:50%;background:#4a90d9;border:2px solid #cfe6ff;transform:translateY(-50%);cursor:crosshair;z-index:4}.logic-port:hover,.logic-port.hot{background:#59c77a}.logic-port-name{position:absolute;left:-38px;width:30px;text-align:right;transform:translateY(-50%);color:#9aabbc;font-size:10px;font-weight:850;pointer-events:none}.logic-in{left:-7px}.logic-out{right:-7px;top:66px}.logic-node[data-type=input] .logic-in,.logic-node[data-type=input] .logic-port-name{display:none}.logic-node[data-type=output] .logic-out{display:none}.logic-selected{outline:2px solid #f4c25b}.logic-line{stroke:#526072;stroke-width:4;fill:none;pointer-events:stroke;cursor:pointer}.logic-line:hover{stroke:#7fa1c7}.logic-line.is-on{stroke:#59c77a}.logic-line.selected{stroke:#f4c25b;stroke-width:6}.logic-line.pending{stroke:#f4c25b;stroke-dasharray:8 8;pointer-events:none}.logic-inspector input,.logic-inspector select{min-height:38px}.sim-row{display:grid;grid-template-columns:minmax(0,1fr) auto auto;align-items:center;gap:8px;border-bottom:1px solid #29313a;padding:8px 0}.sim-row:last-child{border-bottom:0}.switch.mini{width:46px;height:25px;min-height:25px;padding:0;border-radius:999px;background:#343b45;border:0;position:relative;color:transparent}.switch.mini:after{content:"";position:absolute;width:21px;height:21px;left:2px;top:2px;border-radius:50%;background:#fff}.switch.mini.on{background:#31c85a}.switch.mini.on:after{left:23px}.logic-status{min-height:22px;color:#9ba5b1;font-size:12px;margin-top:8px}.logic-note{font-size:12px;color:#9ba5b1;line-height:1.4}@media(max-width:1200px){main{max-width:calc(100vw - 24px)!important;width:calc(100vw - 24px)!important}.logic-top{grid-template-columns:1fr}.logic-workspace{height:720px;min-height:620px}}</style>
<section class="logic-top">
  <div class="logic-panel"><h2 data-l="blocks">Bausteine</h2><p class="logic-note" data-l="note">Baustein anlegen, Ausgangspunkt halten und auf den Eingang des Zielbausteins ziehen.</p><div class="logic-palette"><button onclick="addNode('input')"><span class="logic-chip">I</span><span data-l="input">Input</span></button><button onclick="addNode('output')"><span class="logic-chip">O</span><span data-l="output">Output</span></button><button class="secondary" onclick="addNode('and')"><span class="logic-chip">&amp;</span><span data-l="and">UND</span></button><button class="secondary" onclick="addNode('or')"><span class="logic-chip">&gt;=1</span><span data-l="or">ODER</span></button><button class="secondary" onclick="addNode('not')"><span class="logic-chip">!</span><span data-l="not">NICHT</span></button><button class="secondary" onclick="addNode('toggle')"><span class="logic-chip">T</span><span data-l="toggle">Toggle</span></button><button class="secondary" onclick="addNode('sr')"><span class="logic-chip">SR</span><span data-l="sr">SR-Latch</span></button><button class="secondary" onclick="addNode('ton')"><span class="logic-chip">TON</span><span>TON</span></button><button class="secondary" onclick="addNode('tof')"><span class="logic-chip">TOFF</span><span>TOFF</span></button><button class="secondary" onclick="addNode('pulse')"><span class="logic-chip">P</span><span data-l="pulse">Pulse</span></button><button class="secondary" onclick="addNode('clock')"><span class="logic-chip">CLK</span><span data-l="clock">Takt</span></button></div><div class="logic-toolbar" style="margin-top:12px"><button onclick="saveGraph()" data-l="save">Speichern</button><button class="secondary" onclick="loadGraph()" data-l="load">Laden</button><button class="secondary" onclick="newLogicDef()" data-l="new">Neu</button><button class="secondary" onclick="resetGraph()" data-l="clear">Leeren</button></div><div id="logic_msg" class="logic-status"></div></div>
  <div class="logic-panel"><div class="logic-toolbar"><select id="logic_slot" style="max-width:190px" onchange="selectLogicDef(this.value)"></select><input id="logic_name" style="max-width:260px" value="Absaugung Logik"><label class="logic-enable"><input id="logic_enabled" type="checkbox" onchange="editGraphEnabled()"><span data-l="enabledDef">Aktiv</span></label><button class="secondary" onclick="newLogicDef()" data-l="newDef">Neue Definition</button><button class="secondary" onclick="deleteLogicDef()" data-l="deleteDef">Definition Löschen</button><button class="secondary" onclick="autoLayout()" data-l="auto">Auto Layout</button><button class="secondary" onclick="deleteSelected()" data-l="delete">Löschen</button></div><div id="logic_workspace" class="logic-workspace"><div id="logic_canvas" class="logic-canvas"><svg id="logic_svg" class="logic-svg"></svg></div></div></div>
  <div class="logic-panel logic-inspector"><h2 data-l="props">Eigenschaften</h2><label><span data-l="name">Name</span><input id="node_name" oninput="editSelected()"></label><label><span data-l="signal">Signal / Ziel</span><select id="node_signal" onchange="editSelected()"></select></label><label id="node_delay_label"><span id="node_delay_title" data-l="timer">Timer</span><div style="display:grid;grid-template-columns:minmax(0,1fr) 86px;gap:8px"><input id="node_delay" type="number" min="0" max="9999" step="1" oninput="editSelected()"><select id="node_delay_unit" onchange="editSelected()"><option value="1">ms</option><option value="1000">s</option><option value="60000">min</option><option value="3600000">h</option></select></div><div id="node_delay2_row" style="display:none;grid-template-columns:minmax(0,1fr) 86px;gap:8px;margin-top:8px"><input id="node_delay2" type="number" min="0" max="9999" step="1" oninput="editSelected()"><select id="node_delay2_unit" onchange="editSelected()"><option value="1">ms</option><option value="1000">s</option><option value="60000">min</option><option value="3600000">h</option></select></div></label><div class="logic-toolbar"><button class="secondary" onclick="duplicateSelected()" data-l="duplicate">Duplizieren</button><button class="secondary" onclick="clearLinks()" data-l="clearLinks">Verbindungen Löschen</button></div><h2 style="margin-top:18px" data-l="simulation">Simulation</h2><div id="sim_inputs"></div><div id="sim_outputs" style="margin-top:8px"></div></div>
</section><script>
let graph={schema:1,name:'Absaugung Logik',enabled:false,nodes:[],links:[]},sel=null,selLink=null,linkFrom=null,lineDrag=null,drag=null,linkDrag=null,lastTick=performance.now(),currentLogicSlot=-1;try{let qs=new URLSearchParams(location.search);if(qs.has('slot'))currentLogicSlot=Number(qs.get('slot'))}catch(e){}
let liveState=null;const signals={inputs:[],outputs:[]}, NODE_W=138, NODE_OUT_Y=66;
function msg(t){document.getElementById('logic_msg').textContent=t||''}
function nid(){return 'n'+Math.random().toString(36).slice(2,8)}
function logicLang(){let s=document.getElementById('lang_sel');return s&&s.value?s.value:(document.documentElement.lang||'de')}
function logicDe(){return logicLang()!=='en'}
function t(de,en){return logicDe()?de:en}
const L={blocks:['Bausteine','Blocks'],note:['Baustein anlegen, Ausgangspunkt halten und auf den Eingang des Zielbausteins ziehen.','Add a block, hold an output point, then drag it to the target input.'],newDef:['Neue Definition','New definition'],deleteDef:['Definition Löschen','Delete definition'],input:['Input','Input'],output:['Output','Output'],and:['UND','AND'],or:['ODER','OR'],not:['NICHT','NOT'],toggle:['Toggle','Toggle'],sr:['RS-Latch','RS latch'],pulse:['Pulse','Pulse'],clock:['Takt','Clock'],onTime:['Ein-Zeit','On time'],offTime:['Aus-Zeit','Off time'],auto:['Auto Layout','Auto layout'],delete:['L\u00f6schen','Delete'],props:['Eigenschaften','Properties'],name:['Name','Name'],signal:['Signal / Ziel','Signal / target'],timer:['Timer','Timer'],duplicate:['Duplizieren','Duplicate'],clearLinks:['Verbindungen l\u00f6schen','Delete links'],simulation:['Simulation','Simulation'],save:['Speichern','Save'],load:['Laden','Load'],new:['Neu','New'],clear:['Leeren','Clear'],enabledDef:['Aktiv','Enabled']};
function applyLogicLang(){document.querySelectorAll('[data-l]').forEach(e=>{let v=L[e.dataset.l];if(v)e.textContent=logicDe()?v[0]:v[1]})}
function defaultNames(type){return {input:['Input','Input'],output:['Output','Output'],and:['UND','AND'],or:['ODER','OR'],not:['NICHT','NOT'],toggle:['Toggle','Toggle'],sr:['RS-Latch','RS latch'],ton:['TON','TON'],tof:['TOFF','TOFF'],pulse:['Pulse','Pulse'],clock:['Takt','Clock']}[type]||[]}
function retitleDefaultNodes(){graph.nodes.forEach(n=>{let d=defaultNames(n.type);if(d.includes(n.name))n.name=labelType(n.type)});let name=document.getElementById('logic_name');if(name&&['Absaugung Logik','Extractor logic'].includes(name.value)){name.value=defaultGraphName();graph.name=name.value}}
async function refreshLogicLanguage(){await loadSignals();retitleDefaultNodes();applyLogicLang();render();loadLogicDefs()}
function setupLogicLangSync(){let s=document.getElementById('lang_sel');if(s&&!s.dataset.logicSync){s.dataset.logicSync='1';s.addEventListener('change',()=>setTimeout(refreshLogicLanguage,80))}}
function labelType(k){return ({input:t('Input','Input'),output:t('Output','Output'),and:t('UND','AND'),or:t('ODER','OR'),not:t('NICHT','NOT'),toggle:t('Toggle','Toggle'),sr:t('RS-Latch','RS latch'),ton:'TON',tof:'TOFF',pulse:t('Pulse','Pulse'),clock:t('Takt','Clock')})[k]||k}
function symbolType(k){return ({input:'IN',output:'OUT',and:'&',or:'>=1',not:'!',toggle:'T',sr:'RS',ton:'TON',tof:'TOFF',pulse:'^',clock:'CLK'})[k]||'?'}
function defaultGraphName(){return t('Absaugung Logik','Extractor logic')}
function emptyGraph(name=defaultGraphName()){return {schema:1,name,enabled:true,nodes:[],links:[]}}function editGraphEnabled(){let e=document.getElementById('logic_enabled');graph.enabled=!!(e&&e.checked)}
function nodeDefaults(type){let n={id:nid(),type,x:120+graph.nodes.length*28,y:120+graph.nodes.length*24,name:labelType(type),signal:'',delay:1000,delay2:1000,value:false,inputMode:'switch',last:false,mem:false,until:0};if(type==='input')n.signal=signals.inputs[0]?.id||'manual';if(type==='output')n.signal=signals.outputs[0]?.id||'main_output';return n}
function addNode(type){graph.nodes.push(nodeDefaults(type));sel=graph.nodes[graph.nodes.length-1].id;selLink=null;render()}
function resetGraph(){if(!confirm(t('Aktuelle Definition wirklich leeren?','Really clear current definition?')))return;graph=emptyGraph(defaultGraphName());sel=null;selLink=null;linkFrom=null;lineDrag=null;drag=null;linkDrag=null;let name=document.getElementById('logic_name');if(name)name.value=graph.name;let en=document.getElementById('logic_enabled');if(en)en.checked=!!graph.enabled;render();applyLogicLang();msg(t('Leere Definition angelegt','Empty definition created'))}function moduleAddr(m){return '0x'+Number(m.addr).toString(16).toUpperCase().padStart(2,'0')}
function moduleLogicName(m){return (m.display_name||m.name||m.type_name||'Modul')}
function aliasOr(v,fallback){v=(v||'').trim();return v.length?v:fallback}
function ioLogicInputName(m,bit){return moduleAddr(m)+' '+moduleLogicName(m)+' - '+aliasOr(bit?m.io_in2_alias:m.io_in1_alias,'IN'+(bit+1))}
function ioLogicOutputName(m,bit){return moduleAddr(m)+' '+moduleLogicName(m)+' - '+aliasOr(bit?m.io_out2_alias:m.io_out1_alias,'OUT'+(bit+1))}
function ioLogicMainName(m){return moduleAddr(m)+' '+moduleLogicName(m)+' - '+aliasOr(m.io_main_alias,'Relais/Fan')}
function liveModule(addr){let d=liveState||{};return (d.modules||[]).find(m=>Number(m.addr)===Number(addr))||null}
function bitSet(mask,bit){return (Number(mask||0)&(1<<Number(bit||0)))!==0}
function liveEntityBool(m,roles){let defs=m&&m.universal_entity_defs||[],ents=m&&m.universal_entities||[];roles=(roles||[]).map(x=>String(x).toLowerCase());for(let d of defs){let role=String(d.meta&&d.meta.role||'').toLowerCase();if(roles.includes(role)){let e=ents.find(x=>Number(x.id)===Number(d.id));if(e&&typeof e.value_bool!=='undefined')return !!e.value_bool;if(e&&typeof e.value!=='undefined')return Number(e.value)!==0}}let e=ents.find(x=>typeof x.value_bool!=='undefined');return e?!!e.value_bool:false}
function logicUniversalDefs(m){return Array.isArray(m&&m.universal_entity_defs)?m.universal_entity_defs:[]}function logicUniversalMode(d){return String((d&&d.mode)||(d&&d.meta&&d.meta.access)||'ro').toLowerCase()}function logicUniversalLabel(m,d){let a=moduleAddr(m),name=moduleLogicName(m),label=String(d&&d.label||d&&d.key||('Entity '+Number(d&&d.id||0)));return a+' '+name+' - '+label+' [#'+Number(d&&d.id||0)+']'}function liveEntityById(m,id){return (m&&m.universal_entities||[]).find(e=>Number(e.id)===Number(id))||null}function liveEntityBoolById(m,id){let e=liveEntityById(m,id);if(!e)return false;if(typeof e.value_bool!=='undefined')return !!e.value_bool;if(typeof e.value!=='undefined')return Number(e.value)!==0;let v=String(e.text||e.value_text||'').trim().toLowerCase();return !!v&&v!=='0'&&v!=='off'&&v!=='false'&&v!=='aus'}
function liveSignalInfo(signal){let d=liveState||{};signal=String(signal||'');if(!signal||signal==='manual')return {known:false,on:false,online:true};if(signal==='main_output')return {known:true,on:!!d.output_enabled,online:true};let m;if(signal.startsWith('jbc:')){let a=Number(signal.split(':')[1]||0);m=liveModule(a);return {known:!!m,on:!!(m&&m.online&&Number(m.jbc_work_mask||0)),online:!!(m&&m.online)}}if(signal.startsWith('io:')){let p=signal.split(':'),a=Number(p[1]||0),b=p[2];m=liveModule(a);if(!m)return {known:false,on:false,online:false};if(b==='main')return {known:true,on:!!(m.online&&m.module_output_enabled),online:!!m.online};let bit=Number(b||0);let on=bit<2?bitSet(m.io_input_mask,bit):bitSet(m.io_output_mask,bit-2);return {known:true,on:!!(m.online&&on),online:!!m.online}}if(signal.startsWith('weller:')){let p=signal.split(':'),a=Number(p[1]||0),kind=p[2];m=liveModule(a);if(!m)return {known:false,on:false,online:false};let on=kind==='fan'?bitSet(m.io_output_mask,0):(bitSet(m.io_output_mask,1)||!!m.weller_work_light);return {known:true,on:!!(m.online&&on),online:!!m.online}}if(signal.startsWith('uni:')){let p=signal.split(':'),a=Number(p[1]||0),kind=p[2],id=Number(p[3]||0);m=liveModule(a);if(!m)return {known:false,on:false,online:false};if(kind==='entity'||kind==='switch')return {known:!!(liveEntityById(m,id)||logicUniversalDefs(m).some(x=>Number(x.id)===id)),on:!!(m.online&&liveEntityBoolById(m,id)),online:!!m.online};if(kind==='button')return {known:logicUniversalDefs(m).some(x=>Number(x.id)===id),on:false,online:!!m.online};let on=kind==='output'?(!!m.module_output_enabled||liveEntityBool(m,['main_output_enable','output_enable','output'])):liveEntityBool(m,['main_input','input']);return {known:true,on:!!(m.online&&on),online:!!m.online}}return {known:false,on:false,online:true}}
function liveBadge(n){if(!(n.type==='input'||n.type==='output')||!n.signal||n.signal==='manual'||n.signal.startsWith('extractor:action:'))return '';let info=liveSignalInfo(n.signal),cls=!info.online?'offline':(info.on?'on':'off'),text=!info.online?t('offline','offline'):(info.on?t('live an','live on'):t('live aus','live off'));return `<div class="logic-live ${cls}" data-live="${escHtml(n.id)}"><span class="logic-live-dot"></span><span>${text}</span></div>`}
function refreshLiveBadges(){document.querySelectorAll('.logic-node').forEach(e=>{let n=graph.nodes.find(x=>x.id===e.dataset.id);if(!n)return;let info=liveSignalInfo(n.signal);e.classList.toggle('live-on',!!(info.known&&info.online&&info.on));e.classList.toggle('live-offline',!!(info.known&&!info.online));let b=e.querySelector('.logic-live');if(!b)return;let cls=!info.online?'offline':(info.on?'on':'off');b.className='logic-live '+cls;let s=b.querySelector('span:last-child');if(s)s.textContent=!info.online?t('offline','offline'):(info.on?t('live an','live on'):t('live aus','live off'))})}
async function refreshLiveState(){try{let r=await fetch('/state',{cache:'no-store'});liveState=await r.json();refreshLiveBadges()}catch(e){}}
function extractorActionSignals(){return [{id:'extractor:action:level_next',name:t('Nächste Absaugstufe','Cycle suction level')},{id:'extractor:action:level_previous',name:t('Vorherige Absaugstufe','Previous suction level')},{id:'extractor:action:level_high',name:t('Absaugstufe Hoch','Suction level High')},{id:'extractor:action:level_medium',name:t('Absaugstufe Mittel','Suction level Medium')},{id:'extractor:action:level_low',name:t('Absaugstufe Niedrig','Suction level Low')},{id:'extractor:action:level_custom',name:t('Absaugstufe Benutzer','Suction level Custom')},{id:'extractor:action:power_plus_1',name:t('Benutzerleistung +1 %','Custom power +1%')},{id:'extractor:action:power_minus_1',name:t('Benutzerleistung -1 %','Custom power -1%')},{id:'extractor:action:power_plus_10',name:t('Benutzerleistung +10 %','Custom power +10%')},{id:'extractor:action:power_minus_10',name:t('Benutzerleistung -10 %','Custom power -10%')}]}function baseLogicOutputs(){return [{id:'main_output',name:t('Hauptausgang Absaugung','Main extractor output')}].concat(extractorActionSignals())}
async function loadSignals(){try{let r=await fetch('/state',{cache:'no-store'}),d=await r.json();liveState=d;signals.inputs=[{id:'manual',name:t('Simulation Eingang','Simulation input')}];signals.outputs=baseLogicOutputs();(d.modules||[]).forEach(m=>{let a=moduleAddr(m),name=moduleLogicName(m);if((Number(m.caps||0)&16777217)!=0)signals.inputs.push({id:'jbc:'+m.addr,name:a+' '+name+' Work'});if(m.type==2||m.type==3){signals.inputs.push({id:'io:'+m.addr+':0',name:ioLogicInputName(m,0)});signals.inputs.push({id:'io:'+m.addr+':1',name:ioLogicInputName(m,1)});signals.outputs.push({id:'io:'+m.addr+':2',name:ioLogicOutputName(m,0)});signals.outputs.push({id:'io:'+m.addr+':3',name:ioLogicOutputName(m,1)});signals.outputs.push({id:'io:'+m.addr+':main',name:ioLogicMainName(m)});}if(m.type==5){signals.outputs.push({id:'weller:'+m.addr+':fan',name:a+' '+name+' '+t('L\u00fcfter','Fan')});signals.outputs.push({id:'weller:'+m.addr+':light',name:a+' '+name+' Licht'});}if(m.type==7||m.type==8){let defs=logicUniversalDefs(m),addedIn=0,addedOut=0;defs.forEach(e=>{let id=Number(e.id||0),type=String(e.type||'').toLowerCase(),mode=logicUniversalMode(e),label=logicUniversalLabel(m,e);if(id>=20&&mode.includes('r')&&(type==='binary_sensor'||type==='switch')){signals.inputs.push({id:'uni:'+m.addr+':entity:'+id,name:label});addedIn++}if(id>=20&&mode.includes('w')&&type==='switch'){signals.outputs.push({id:'uni:'+m.addr+':switch:'+id,name:label});addedOut++}if(id>=20&&mode.includes('w')&&type==='button'){signals.outputs.push({id:'uni:'+m.addr+':button:'+id,name:label+' '+t('(Impuls)','(pulse)')});addedOut++}});signals.inputs.push({id:'uni:'+m.addr+':input',name:a+' '+name+' '+t('Sammel-Input (Legacy)','Aggregate input (legacy)')});signals.outputs.push({id:'uni:'+m.addr+':output',name:a+' '+name+' '+t('Hauptausgang (Legacy)','Main output (legacy)')});}})}catch(e){signals.inputs=[{id:'manual',name:t('Simulation Eingang','Simulation input')}];signals.outputs=baseLogicOutputs()}}function selectedLogicSlot(){let s=document.getElementById('logic_slot');let v=(s&&s.value!=='')?Number(s.value):Number(currentLogicSlot);return Number.isFinite(v)&&v>=0?v:0}
async function loadLogicDefs(preferSlot=null){try{let r=await fetch('/logic/list',{cache:'no-store'}),d=await r.json(),selEl=document.getElementById('logic_slot');let items=(d.items||[]).filter(x=>x.used);let selected=Number.isFinite(Number(preferSlot))&&Number(preferSlot)>=0?Number(preferSlot):(Number(currentLogicSlot)>=0?Number(currentLogicSlot):(selEl&&selEl.value!==''?Number(selEl.value):Number(d.active??0)));if(!items.some(x=>Number(x.slot)===selected))selected=items.length?Number(d.active??items[0].slot??0):selected;currentLogicSlot=Number.isFinite(selected)&&selected>=0?Number(selected):0;if(selEl&&document.activeElement!==selEl){if(items.length){selEl.innerHTML=items.map(x=>`<option value="${x.slot}" ${Number(x.slot)===Number(currentLogicSlot)?'selected':''}>${escHtml(x.name)}</option>`).join('');selEl.value=String(currentLogicSlot)}else{selEl.innerHTML=`<option value="${currentLogicSlot}">${t('Neue Definition','New definition')}</option>`;selEl.value=String(currentLogicSlot)}}return d}catch(e){return null}}
async function selectLogicDef(slot){currentLogicSlot=Number(slot||0);await fetch('/logic/select?slot='+encodeURIComponent(currentLogicSlot),{method:'POST',cache:'no-store'});await loadGraph(currentLogicSlot)}
async function newLogicDef(){let n=prompt(t('Name der neuen Definition','Name of the new definition'),defaultGraphName());if(!n)return;let r=await fetch('/logic/new?name='+encodeURIComponent(n),{method:'POST',cache:'no-store'});let txt=await r.text();if(!r.ok){alert(txt);return}currentLogicSlot=Number(txt||0);let selEl=document.getElementById('logic_slot');if(selEl)selEl.value=String(currentLogicSlot);await loadLogicDefs(currentLogicSlot);await loadGraph(currentLogicSlot)}
async function deleteLogicDef(){let slot=selectedLogicSlot();if(!confirm(t('Definition wirklich Löschen?','Really delete definition?')))return;let r=await fetch('/logic/delete?slot='+encodeURIComponent(slot),{method:'POST',cache:'no-store'});let txt=await r.text();if(!r.ok){alert(txt);return}currentLogicSlot=Number(txt||0);await loadLogicDefs(currentLogicSlot);await loadGraph(currentLogicSlot)}
function escHtml(v){return String(v||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function loadGraph(slot=currentLogicSlot){await loadSignals();currentLogicSlot=Number(slot??currentLogicSlot??0);if(!Number.isFinite(currentLogicSlot)||currentLogicSlot<0)currentLogicSlot=0;await loadLogicDefs(currentLogicSlot);try{let r=await fetch('/logic/json?slot='+encodeURIComponent(currentLogicSlot),{cache:'no-store'});graph=await r.json();graph.slot=currentLogicSlot;if(!graph.nodes)graph=emptyGraph(defaultGraphName());graph.nodes.forEach(n=>{if(n.type==='input'&&!n.inputMode)n.inputMode='switch'});graph.links=(graph.links||[]).map(l=>({from:l.from,to:l.to,port:Number(l.port||0)}));let nameEl=document.getElementById('logic_name'),graphName=graph.name||defaultGraphName();if(['Absaugung Logik','Extractor logic'].includes(graphName)){graphName=defaultGraphName();graph.name=graphName}if(nameEl)nameEl.value=graphName;let enEl=document.getElementById('logic_enabled');if(enEl)enEl.checked=!!graph.enabled;let selEl=document.getElementById('logic_slot');if(selEl)selEl.value=String(currentLogicSlot);msg(t('Geladen','Loaded'))}catch(e){graph=emptyGraph(defaultGraphName());let name=document.getElementById('logic_name');if(name)name.value=graph.name;let en=document.getElementById('logic_enabled');if(en)en.checked=!!graph.enabled;msg(t('Neue leere Logik angelegt','New empty logic created'))}render();applyLogicLang()}
async function saveGraph(){graph.name=document.getElementById('logic_name').value||defaultGraphName();let en=document.getElementById('logic_enabled');graph.enabled=!!(en&&en.checked);currentLogicSlot=selectedLogicSlot();graph.slot=currentLogicSlot;let r=await fetch('/logic/json/save?slot='+encodeURIComponent(currentLogicSlot),{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:JSON.stringify(graph),cache:'no-store'});let txt=await r.text();msg(r.ok?t('Gespeichert','Saved'):txt);if(!r.ok){alert(txt);return}await loadLogicDefs(currentLogicSlot);await loadGraph(currentLogicSlot)}function inputPorts(type){if(type==='input')return[];if(type==='and'||type==='or')return['A','B','C','D'];if(type==='sr')return['S','R'];if(type==='toggle')return['T','S','R'];return['IN']}
function inputPortY(type,port){let p=inputPorts(type);if(p.length<=1)return NODE_OUT_Y;if(p.length===2)return 52+port*28;return 39+port*18}
function linksTo(id,port){return graph.links.filter(l=>l.to===id&&Number(l.port||0)===port)}
function portValue(n,values,port){return linksTo(n.id,port).some(l=>!!values[l.from])}
function linkedValues(n,values){return graph.links.filter(l=>l.to===n.id).map(l=>!!values[l.from])}
function evalGraph(){let now=performance.now(),dt=Math.min(500,now-lastTick);lastTick=now;let values={};for(let pass=0;pass<8;pass++){graph.nodes.forEach(n=>{let linked=linkedValues(n,values),p0=portValue(n,values,0),p1=portValue(n,values,1),v=false;if(n.type==='input')v=!!n.value;else if(n.type==='and')v=linked.length?linked.every(Boolean):false;else if(n.type==='or')v=linked.some(Boolean);else if(n.type==='not')v=linked.length?!p0:false;else if(n.type==='toggle'){let p2=portValue(n,values,2);if(p2)n.mem=false;else if(p1)n.mem=true;else if(p0&&!n.last)n.mem=!n.mem;n.last=!!p0;v=n.mem}else if(n.type==='sr'){if(p1)n.mem=false;if(p0)n.mem=true;v=n.mem}else if(n.type==='ton'){if(p0){if(pass===7)n.until=(n.until||0)+dt;v=(n.until||0)>=Number(n.delay||0)}else{if(pass===7)n.until=0;v=false}}else if(n.type==='tof'){if(p0){if(pass===7)n.until=Number(n.delay||0);v=true}else{if(pass===7)n.until=Math.max(0,(n.until||0)-dt);v=(n.until||0)>0}}else if(n.type==='pulse'){v=p0&&!n.last;if(pass===7)n.last=!!p0}else if(n.type==='clock'){let on=Math.max(1,Number(n.delay||1000)),off=Math.max(1,Number(n.delay2||on));if(!p0){if(pass===7){n.mem=false;n.last=false;n.until=0}v=false}else{if(pass===7){if(!n.last){n.mem=true;n.until=0}else{n.until=(n.until||0)+dt;for(let guard=0;guard<8;guard++){let phase=n.mem?on:off;if((n.until||0)<phase)break;n.until-=phase;n.mem=!n.mem}}n.last=true}v=!!n.mem}}else if(n.type==='output')v=!!p0;values[n.id]=v})}return values}
function portPoint(n,out,port=0){return {x:n.x+(out?NODE_W:0),y:n.y+(out?NODE_OUT_Y:inputPortY(n.type,port))}}
function screenToCanvas(ev){let c=document.getElementById('logic_canvas'),r=c.getBoundingClientRect();return{x:ev.clientX-r.left,y:ev.clientY-r.top}}
function addLink(from,to,port){if(!from||!to||from===to)return;port=Number(port||0);graph.links=graph.links.filter(l=>!(l.from===from&&l.to===to&&Number(l.port||0)===port));graph.links.push({from,to,port});msg(t('Verbunden mit ','Connected to ')+(inputPorts(graph.nodes.find(n=>n.id===to)?.type||'')[port]||'IN'))}
function startLink(id,out,port=0){linkFrom={id,out:!!out,port:Number(port||0)};linkDrag=null;msg(t('Verbindung ziehen...','Drag link...'));drawLinks()}
function finishLink(n,isOut,port=0){if(!linkFrom)return;port=Number(port||0);if(linkFrom.out&&!isOut)addLink(linkFrom.id,n.id,port);else if(!linkFrom.out&&isOut)addLink(n.id,linkFrom.id,linkFrom.port);linkFrom=null;linkDrag=null;render()}
function connectTo(n,port){finishLink(n,false,port)}
function inlineInputControl(n){let mode=n.inputMode||'switch';if(mode==='button')return `<button class="secondary" onpointerdown="pressInput('${n.id}')" onpointerup="releaseInput('${n.id}')" onpointerleave="releaseInput('${n.id}')">${t('Taster','Button')}</button>`;return `<button class="switch mini ${n.value?'on':''}" onclick="toggleInput('${n.id}')">.</button>`}
function nodeBody(n){let txt=signalName(n)+timerText(n)+liveBadge(n);if(n.type==='input')txt+=`<div class="logic-inline-sim"><span>${(n.inputMode||'switch')==='button'?t('Taster','Button'):t('Schalter','Switch')}</span>${inlineInputControl(n)}</div>`;return txt}
function render(){let c=document.getElementById('logic_canvas');c.querySelectorAll('.logic-node').forEach(e=>e.remove());let vals=evalGraph();graph.nodes.forEach(n=>{let live=liveSignalInfo(n.signal);let e=document.createElement('div');e.className='logic-node '+(vals[n.id]?'is-on ':'')+(live.known&&live.online&&live.on?'live-on ':'')+(live.known&&!live.online?'live-offline ':'')+(sel===n.id?'logic-selected':'');e.dataset.type=n.type;e.dataset.id=n.id;e.style.left=n.x+'px';e.style.top=n.y+'px';let ports=inputPorts(n.type);let showPinName=ports.length>1;let pins=ports.map((p,i)=>`<span class="logic-port logic-in" data-port="${i}" title="${p}" style="top:${inputPortY(n.type,i)}px"></span>${showPinName?`<span class="logic-port-name" style="top:${inputPortY(n.type,i)}px">${p}</span>`:''}`).join('');let out=n.type==='output'?'':`<span class="logic-port logic-out" title="Output"></span>`;e.innerHTML=`<span class="logic-symbol">${symbolType(n.type)}</span>${pins}${out}<h3>${nodeTitle(n)}<small>${labelType(n.type)}</small></h3><div class="body">${nodeBody(n)}</div>`;e.addEventListener('pointerdown',ev=>{if(ev.target.classList.contains('logic-port')||ev.target.closest('button,select,input'))return;sel=n.id;selLink=null;drag={id:n.id,dx:ev.clientX-n.x,dy:ev.clientY-n.y};e.setPointerCapture(ev.pointerId);markSelected();renderInspector();ev.preventDefault()});e.addEventListener('pointermove',ev=>{if(!drag||drag.id!==n.id)return;n.x=Math.max(0,Math.min(2600,ev.clientX-drag.dx));n.y=Math.max(0,Math.min(1580,ev.clientY-drag.dy));e.style.left=n.x+'px';e.style.top=n.y+'px';drawLinks()});e.addEventListener('pointerup',()=>{drag=null;renderInspector()});let outPort=e.querySelector('.logic-out');if(outPort){outPort.onpointerdown=ev=>{ev.stopPropagation();ev.preventDefault();startLink(n.id,true,0);linkDrag=screenToCanvas(ev)};outPort.onpointerup=ev=>{ev.stopPropagation();finishLink(n,true,0)}}e.querySelectorAll('.logic-in').forEach(pin=>{pin.onpointerdown=ev=>{ev.stopPropagation();ev.preventDefault();startLink(n.id,false,Number(pin.dataset.port||0));linkDrag=screenToCanvas(ev)};pin.onpointerup=ev=>{ev.stopPropagation();finishLink(n,false,Number(pin.dataset.port||0))};pin.onclick=ev=>{ev.stopPropagation();if(linkFrom)finishLink(n,false,Number(pin.dataset.port||0));else{sel=n.id;selLink=null;markSelected();renderInspector()}}});c.appendChild(e)});drawLinks(vals);renderInspector();renderSim(vals)}function markSelected(){document.querySelectorAll('.logic-node').forEach(e=>e.classList.toggle('logic-selected',e.dataset.id===sel))}
function drawLinks(vals={}){let svg=document.getElementById('logic_svg');if(!svg)return;svg.innerHTML='';graph.links.forEach((l,i)=>{let a=graph.nodes.find(n=>n.id===l.from),b=graph.nodes.find(n=>n.id===l.to);if(!a||!b)return;let p1=portPoint(a,true,0),p2=portPoint(b,false,Number(l.port||0));let path=document.createElementNS('http://www.w3.org/2000/svg','path');path.setAttribute('d',routePath(p1,p2,i,a.id,b.id));path.setAttribute('class','logic-line '+(vals[l.from]?'is-on ':'')+(selLink===i?'selected':''));path.onpointerdown=ev=>{ev.stopPropagation();sel=null;selLink=i;lineDrag={from:l.from,to:l.to,port:Number(l.port||0),clientX:ev.clientX,clientY:ev.clientY};renderInspector();drawLinks(vals)};svg.appendChild(path)});if(linkFrom&&linkDrag){let n=graph.nodes.find(x=>x.id===linkFrom.id);if(n){let p1=portPoint(n,linkFrom.out,linkFrom.port),p2=linkDrag;if(!linkFrom.out){let t=p1;p1=p2;p2=t}let path=document.createElementNS('http://www.w3.org/2000/svg','path');path.setAttribute('d',previewPath(p1,p2));path.setAttribute('class','logic-line pending');svg.appendChild(path)}}}function nodeRect(n){let h=Math.max(116,Number(n.h||116));return {l:n.x-22,r:n.x+NODE_W+22,t:n.y-18,b:n.y+h+18}}
function segCrossRect(a,b,r){let pad=2;if(Math.abs(a.y-b.y)<0.1){let x1=Math.min(a.x,b.x),x2=Math.max(a.x,b.x);return a.y>=r.t-pad&&a.y<=r.b+pad&&x2>=r.l-pad&&x1<=r.r+pad}if(Math.abs(a.x-b.x)<0.1){let y1=Math.min(a.y,b.y),y2=Math.max(a.y,b.y);return a.x>=r.l-pad&&a.x<=r.r+pad&&y2>=r.t-pad&&y1<=r.b+pad}return false}
function pathClear(points,fromId,toId){let last=points.length-2;for(let i=0;i<points.length-1;i++){for(let n of graph.nodes){let endpoint=(n.id===fromId||n.id===toId);if(endpoint&&((n.id===fromId&&i===0)||(n.id===toId&&i===last)))continue;if(segCrossRect(points[i],points[i+1],nodeRect(n)))return false}}return true}
function cleanPts(pts){let out=[];pts.forEach(p=>{let q={x:Math.round(p.x),y:Math.round(p.y)};let last=out[out.length-1];if(!last||last.x!==q.x||last.y!==q.y)out.push(q)});return out}
function simplifyPts(pts){pts=cleanPts(pts);let changed=true;while(changed){changed=false;for(let i=1;i<pts.length-1;i++){let a=pts[i-1],b=pts[i],c=pts[i+1];if((a.x===b.x&&b.x===c.x)||(a.y===b.y&&b.y===c.y)){pts.splice(i,1);changed=true;break}}}return pts}
function scorePath(pts){pts=simplifyPts(pts);let bends=Math.max(0,pts.length-2),len=0;for(let i=1;i<pts.length;i++)len+=Math.abs(pts[i].x-pts[i-1].x)+Math.abs(pts[i].y-pts[i-1].y);let drift=pts.length?Math.abs(pts[0].y-pts[pts.length-1].y):0;return bends*12000+len+drift}
function pathD(points){points=simplifyPts(points);return 'M'+points.map((p,i)=>(i?`L${p.x},${p.y}`:`${p.x},${p.y}`)).join(' ')}
function previewPath(p1,p2){let lead=p1.x+44,near=p2.x-34,mid=(lead+near)/2;return pathD([{x:p1.x,y:p1.y},{x:lead,y:p1.y},{x:mid,y:p1.y},{x:mid,y:p2.y},{x:p2.x,y:p2.y}])}
function uniqNums(a){let out=[];a.forEach(v=>{v=Math.round(v);if(v>=12&&v<=1688&&!out.some(x=>Math.abs(x-v)<8))out.push(v)});return out}
function routeCandidates(p1,p2,i){let sx=p1.x,sy=p1.y,tx=p2.x,ty=p2.y;let sxLead=sx+44;let txLead=Math.max(16,tx-48);let rects=graph.nodes.map(nodeRect);let ys=[sy,ty,(sy+ty)/2,Math.min(sy,ty)-64,Math.max(sy,ty)+64];rects.forEach(r=>{ys.push(r.t-30,r.b+30)});ys=uniqNums(ys);ys.sort((a,b)=>Math.abs(a-(sy+ty)/2)-Math.abs(b-(sy+ty)/2));let xs=[sxLead,txLead,(sxLead+txLead)/2,Math.max(sxLead,txLead)+92+(i%8)*24,Math.max(sxLead,txLead)+180+(i%8)*28,Math.max(16,Math.min(sxLead,txLead)-92-(i%8)*24)];rects.forEach(r=>{xs.push(r.l-32,r.r+32)});xs=uniqNums(xs);xs.sort((a,b)=>Math.abs(a-(sxLead+txLead)/2)-Math.abs(b-(sxLead+txLead)/2));let c=[];ys.forEach(y=>c.push(cleanPts([{x:sx,y:sy},{x:sxLead,y:sy},{x:sxLead,y:y},{x:txLead,y:y},{x:txLead,y:ty},{x:tx,y:ty}])));xs.forEach(x=>c.push(cleanPts([{x:sx,y:sy},{x:sxLead,y:sy},{x:x,y:sy},{x:x,y:ty},{x:txLead,y:ty},{x:tx,y:ty}])));ys.slice(0,10).forEach(y=>xs.slice(0,10).forEach(x=>c.push(cleanPts([{x:sx,y:sy},{x:sxLead,y:sy},{x:sxLead,y:y},{x:x,y:y},{x:x,y:ty},{x:txLead,y:ty},{x:tx,y:ty}]))));return c}
function routePath(p1,p2,i=0,fromId='',toId=''){let candidates=routeCandidates(p1,p2,i).filter(pts=>pathClear(pts,fromId,toId));if(candidates.length){candidates.sort((a,b)=>scorePath(a)-scorePath(b));return pathD(candidates[0])}let rects=graph.nodes.map(nodeRect),minY=18,maxY=1680;rects.forEach(r=>{minY=Math.min(minY,r.t-50);maxY=Math.max(maxY,r.b+50)});let txLead=Math.max(16,p2.x-48),sxLead=p1.x+44;let rescue=[Math.max(18,minY),Math.min(1680,maxY),Math.max(18,Math.min(p1.y,p2.y)-140),Math.min(1680,Math.max(p1.y,p2.y)+140)].map(y=>cleanPts([{x:p1.x,y:p1.y},{x:sxLead,y:p1.y},{x:sxLead,y:y},{x:txLead,y:y},{x:txLead,y:p2.y},{x:p2.x,y:p2.y}])).filter(pts=>pathClear(pts,fromId,toId));if(rescue.length){rescue.sort((a,b)=>scorePath(a)-scorePath(b));return pathD(rescue[0])}let y=Math.max(18,Math.min(p1.y,p2.y)-140-(i%10)*30);let pts=cleanPts([{x:p1.x,y:p1.y},{x:sxLead,y:p1.y},{x:sxLead,y:y},{x:txLead,y:y},{x:txLead,y:p2.y},{x:p2.x,y:p2.y}]);return pathD(pts)}function blockIndex(n,type){return graph.nodes.filter(x=>x.type===type).findIndex(x=>x.id===n.id)+1}
function nodeNo(n){return n.type==='input'?'I'+blockIndex(n,'input'):(n.type==='output'?'O'+blockIndex(n,'output'):'')}
function nodeTitle(n){let no=nodeNo(n);let name=n.name||labelType(n.type);return no?no+' '+name:name}function signalName(n){let list=n.type==='input'?signals.inputs:(n.type==='output'?signals.outputs:[]);let s=list.find(x=>x.id===n.signal);return s?s.name:''}
function bestDelayUnit(ms){ms=Number(ms||0);if(ms&&ms%3600000===0)return 3600000;if(ms&&ms%60000===0)return 60000;if(ms&&ms%1000===0)return 1000;return 1}
function delayUnitName(u){return u===3600000?'h':(u===60000?'min':(u===1000?'s':'ms'))}
function delayTextValue(ms){let u=bestDelayUnit(ms);return (Number(ms||0)/u)+' '+delayUnitName(u)}
function delayText(n){return delayTextValue(n.delay||0)}
function runtimeText(n){let ms=Number(n.delay||0),u=bestDelayUnit(ms),raw=n.type==='ton'?Math.max(0,ms-Number(n.until||0)):Math.max(0,Number(n.until||0));let v=Math.ceil(raw/u);return v+' '+delayUnitName(u)}
function clockRuntimeText(n){let on=Math.max(1,Number(n.delay||1000)),off=Math.max(1,Number(n.delay2||on)),phase=n.mem?on:off,u=bestDelayUnit(phase),raw=Math.max(0,phase-Number(n.until||0));return (n.mem?t('an','on'):t('aus','off'))+' / '+Math.ceil(raw/u)+' '+delayUnitName(u)}
function timerText(n){if(n.type==='clock')return '<br>'+t('Ein','On')+': '+delayTextValue(n.delay||1000)+'<br>'+t('Aus','Off')+': '+delayTextValue(n.delay2||n.delay||1000)+'<br>'+t('Rest','Remaining')+': '+clockRuntimeText(n);return (n.type==='ton'||n.type==='tof')?'<br>'+t('Zeit','Time')+': '+delayText(n)+'<br>'+t('Rest','Remaining')+': '+runtimeText(n):''}
function renderInspector(){let n=graph.nodes.find(x=>x.id===sel),name=document.getElementById('node_name'),sig=document.getElementById('node_signal'),delay=document.getElementById('node_delay'),unit=document.getElementById('node_delay_unit'),delay2=document.getElementById('node_delay2'),unit2=document.getElementById('node_delay2_unit'),delay2Row=document.getElementById('node_delay2_row'),delayTitle=document.getElementById('node_delay_title'),delayLabel=document.getElementById('node_delay_label');name.value=n?n.name||'':(selLink!==null?t('Verbindung','Link'):'');let u=n?bestDelayUnit(n.delay||0):1,u2=n?bestDelayUnit(n.delay2||n.delay||0):1;delay.value=n?Number(n.delay||0)/u:'';unit.value=String(u);if(delay2)delay2.value=n?Number(n.delay2||n.delay||0)/u2:'';if(unit2)unit2.value=String(u2);if(delayTitle)delayTitle.textContent=n&&n.type==='clock'?t('Ein-Zeit / Aus-Zeit','On time / off time'):t('Timer','Timer');sig.innerHTML='';let list=n&&n.type==='input'?signals.inputs:(n&&n.type==='output'?signals.outputs:[]);list.forEach(s=>{let o=document.createElement('option');o.value=s.id;o.textContent=s.name;o.selected=n.signal===s.id;sig.appendChild(o)});sig.disabled=!list.length;name.disabled=delay.disabled=unit.disabled=!n;if(delay2)delay2.disabled=!n;if(unit2)unit2.disabled=!n;let timed=n&&(n.type==='ton'||n.type==='tof'||n.type==='clock');delayLabel.style.display=timed?'block':'none';if(delay2Row)delay2Row.style.display=n&&n.type==='clock'?'grid':'none'}
function editSelected(){let n=graph.nodes.find(x=>x.id===sel);if(!n)return;n.name=document.getElementById('node_name').value;n.signal=document.getElementById('node_signal').value;let u=Number(document.getElementById('node_delay_unit').value||1);n.delay=Math.max(0,Math.round(Number(document.getElementById('node_delay').value||0)*u));let d2=document.getElementById('node_delay2'),u2=document.getElementById('node_delay2_unit');if(d2&&u2)n.delay2=Math.max(0,Math.round(Number(d2.value||0)*Number(u2.value||1)));render()}
function renderSim(vals){if(document.activeElement&&document.activeElement.closest&&document.activeElement.closest('#sim_inputs')&&document.activeElement.tagName==='SELECT')return;let si=document.getElementById('sim_inputs'),so=document.getElementById('sim_outputs');si.innerHTML='';so.innerHTML='';graph.nodes.filter(n=>n.type==='input').forEach(n=>{let r=document.createElement('div');r.className='sim-row';let mode=n.inputMode||'switch';let title=nodeTitle(n);let btn=mode==='button'?`<button class="secondary" onpointerdown="pressInput('${n.id}')" onpointerup="releaseInput('${n.id}')" onpointerleave="releaseInput('${n.id}')">${t('Taster','Button')}</button>`:`<button class="switch mini ${n.value?'on':''}" onclick="toggleInput('${n.id}')">.</button>`;r.innerHTML=`<span><b>${title}</b><small style="display:block;color:#84909d">${signalName(n)}</small></span><select onchange="setInputMode('${n.id}',this.value)"><option value="switch" ${mode==='switch'?'selected':''}>${t('Schalter','Switch')}</option><option value="button" ${mode==='button'?'selected':''}>${t('Taster','Button')}</option></select>${btn}`;si.appendChild(r)});graph.nodes.filter(n=>n.type==='output').forEach(n=>{let r=document.createElement('div');r.className='sim-row';r.innerHTML=`<span><b>${nodeTitle(n)}</b><small style="display:block;color:#84909d">${signalName(n)}</small></span><span></span><b class="${vals[n.id]?'on':'off'}">${vals[n.id]?t('an','on'):t('aus','off')}</b>`;so.appendChild(r)})}function setInputMode(id,mode){let n=graph.nodes.find(x=>x.id===id);if(!n)return;n.inputMode=mode;if(mode==='button')n.value=false;render()}
function toggleInput(id){let n=graph.nodes.find(x=>x.id===id);if(n)n.value=!n.value;render()}
function pressInput(id){let n=graph.nodes.find(x=>x.id===id);if(n){n.value=true;render()}}
function releaseInput(id){let n=graph.nodes.find(x=>x.id===id);if(n&&n.inputMode==='button'){n.value=false;render()}}
function deleteSelected(){if(selLink!==null){graph.links.splice(selLink,1);selLink=null;render();return}if(!sel)return;graph.nodes=graph.nodes.filter(n=>n.id!==sel);graph.links=graph.links.filter(l=>l.from!==sel&&l.to!==sel);sel=null;render()}
function duplicateSelected(){let n=graph.nodes.find(x=>x.id===sel);if(!n)return;let c=JSON.parse(JSON.stringify(n));c.id=nid();c.x+=44;c.y+=44;c.name+=' '+t('Kopie','copy');graph.nodes.push(c);sel=c.id;selLink=null;render()}
function clearLinks(){if(selLink!==null){graph.links.splice(selLink,1);selLink=null;render();return}if(!sel)return;graph.links=graph.links.filter(l=>l.from!==sel&&l.to!==sel);render()}
function clearPending(){linkFrom=null;linkDrag=null;msg('');drawLinks()}
function autoLayout(){let xs={input:120,and:560,or:560,not:560,toggle:560,sr:560,ton:560,tof:560,pulse:560,clock:560,output:1020};let rows={};graph.nodes.forEach(n=>{let x=xs[n.type]||560;rows[x]=(rows[x]||0)+1;n.x=x;n.y=50+rows[x]*118});render()}
window.addEventListener('pointermove',ev=>{if(lineDrag){let dx=ev.clientX-lineDrag.clientX,dy=ev.clientY-lineDrag.clientY;if(Math.hypot(dx,dy)>6){let d=lineDrag;lineDrag=null;let removed=false;graph.links=graph.links.filter(l=>{if(!removed&&l.from===d.from&&l.to===d.to&&Number(l.port||0)===d.port){removed=true;return false}return true});startLink(d.from,true,0);linkDrag=screenToCanvas(ev);drawLinks();return}}if(!linkFrom)return;linkDrag=screenToCanvas(ev);drawLinks()});
window.addEventListener('pointerup',ev=>{lineDrag=null;if(linkFrom){let target=document.elementFromPoint(ev.clientX,ev.clientY),pin=target&&target.closest?target.closest('.logic-in,.logic-out'):null;if(pin){let nodeEl=pin.closest('.logic-node'),n=nodeEl&&graph.nodes.find(x=>x.id===nodeEl.dataset.id);if(n)finishLink(n,pin.classList.contains('logic-out'),Number(pin.dataset.port||0));}else{linkFrom=null;linkDrag=null;drawLinks();msg('')}}let changed=false;graph.nodes.forEach(n=>{if(n.type==='input'&&n.inputMode==='button'&&n.value){n.value=false;changed=true}});if(changed)render();});
setInterval(refreshLiveState,1000);setInterval(()=>{let vals=evalGraph();drawLinks(vals);if(!(document.activeElement&&document.activeElement.closest&&document.activeElement.closest('#sim_inputs')))renderSim(vals);document.querySelectorAll('.logic-node').forEach(e=>{let n=graph.nodes.find(x=>x.id===e.dataset.id);if(n){e.classList.toggle('is-on',!!vals[n.id]);if(n.type==='ton'||n.type==='tof'||n.type==='clock'){let b=e.querySelector('.body');if(b)b.innerHTML=nodeBody(n)}}});refreshLiveBadges();},200);setupLogicLangSync();applyLogicLang();loadGraph();</script>
)HTML");
  web_shell_end(html);
  web.send(200, "text/html; charset=utf-8", html);
}
