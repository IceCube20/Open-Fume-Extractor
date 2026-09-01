#include "MasterScheduler.h"
#include "MasterDisplayWifi.h"
#include "MasterSettingsStore.h"
#include "MasterBuildConfig.h"
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <esp_heap_caps.h>

using namespace jbc_rs485;


static uint8_t displayUniversalProfileEntityCount(const ModuleRecord& rec);


static ModuleRecord* scheduler_master_record_scratch() {
  // Display list/detail needs a synthetic record for the master itself. Since
  // ModuleRecord contains a 4 KiB descriptor cache, this object must never be a
  // local variable on loopTask's stack. Keep one reusable scratch record in
  // PSRAM (internal heap fallback only when PSRAM is unavailable).
  static ModuleRecord* scratch = nullptr;
  if (scratch) return scratch;
  scratch = static_cast<ModuleRecord*>(
      heap_caps_calloc(1, sizeof(ModuleRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!scratch) {
    scratch = static_cast<ModuleRecord*>(
        heap_caps_calloc(1, sizeof(ModuleRecord), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  return scratch;
}

static String master_ip_string_for_display() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  IPAddress ap = WiFi.softAPIP();
  if (ap != IPAddress(0, 0, 0, 0)) return ap.toString();
  return String("0.0.0.0");
}

static uint32_t fnv1a32_local(const uint8_t* data, size_t len) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619UL;
  }
  return h;
}


#ifndef MODULE_FW_DISPLAY_CHUNK_TIMEOUT_MS
#define MODULE_FW_DISPLAY_CHUNK_TIMEOUT_MS 900UL
#endif

#ifndef MODULE_FW_DISPLAY_CHUNK_ATTEMPTS
#define MODULE_FW_DISPLAY_CHUNK_ATTEMPTS 8
#endif

static const char* fwStatusName(uint8_t status) {
  switch (status) {
    case STATUS_OK: return "ok";
    case STATUS_UNKNOWN_CMD: return "unknown_cmd";
    case STATUS_BAD_LEN: return "bad_len";
    case STATUS_BAD_VALUE: return "bad_value";
    case STATUS_BUSY: return "busy";
    case STATUS_CRC_ERROR: return "crc_error";
    case STATUS_NOT_SUPPORTED: return "not_supported";
    default: return "unknown_status";
  }
}

enum DisplayEventType : uint8_t {
  DISPLAY_EVENT_NONE = 0,
  DISPLAY_EVENT_SUCTION_NEXT = 1,
  DISPLAY_EVENT_CUSTOM_POWER_DELTA = 2,
  DISPLAY_EVENT_DELAY_WORK_DELTA = 3,
  DISPLAY_EVENT_CONTINUOUS_TOGGLE = 4,
  DISPLAY_EVENT_WELLER_SPEED_DELTA = 5,
  DISPLAY_EVENT_WELLER_FAN_TOGGLE = 6,
  DISPLAY_EVENT_WELLER_LIGHT_TOGGLE = 7,
  DISPLAY_EVENT_WELLER_RESET_FILTER = 8,
  DISPLAY_EVENT_IO_OUT_TOGGLE = 9,
  DISPLAY_EVENT_DELAY_STAND_DELTA = 10,
  DISPLAY_EVENT_STAND_INTAKES_TOGGLE = 11,
  DISPLAY_EVENT_WELLER_FILTER_TIME_NEXT = 12,
  DISPLAY_EVENT_WELLER_FILTER_TIME_SET = 13,
  DISPLAY_EVENT_JBC_MODE_SET = 14,
  DISPLAY_EVENT_JBC_POWER_SET = 15,
  DISPLAY_EVENT_JBC_DELAY_WORK_SET = 16,
  DISPLAY_EVENT_JBC_DELAY_STAND_SET = 17,
  DISPLAY_EVENT_WELLER_SPEED_SET = 18,
  DISPLAY_EVENT_OUTPUT_SELECT = 19,
  DISPLAY_EVENT_MODULE_OUTPUT_POWER_SET = 20,
  DISPLAY_EVENT_MODULE_OUTPUT_TOGGLE = 21,
  DISPLAY_EVENT_MAIN_INPUT_SELECT = 22,
  DISPLAY_EVENT_UNIVERSAL_ENTITY_TOGGLE = 23,
  DISPLAY_EVENT_UNIVERSAL_ENTITY_VALUE_SET = 24,
  DISPLAY_EVENT_UNIVERSAL_ENTITY_BUTTON = 25,
  DISPLAY_EVENT_UNIVERSAL_ENTITY_SELECT_SET = 26,
  DISPLAY_EVENT_AFTER_POWER_SET = 27,
  DISPLAY_EVENT_AFTER_POWER_TOGGLE = 28,
};


enum DisplayAlarmCode : uint8_t {
  DISPLAY_ALARM_MODULE_OFFLINE = 1,
  DISPLAY_ALARM_OUTPUT_FAULT = 2,
  DISPLAY_ALARM_JBC_STATION = 3,
  DISPLAY_ALARM_WELLER_LINK = 4,
  DISPLAY_ALARM_JBC_STATUS = 5,
  DISPLAY_ALARM_NO_MAIN_INPUT = 6,
  DISPLAY_ALARM_NO_MAIN_OUTPUT = 7,
};
enum DisplayViewMode : uint8_t {
  DISPLAY_VIEW_HOME = 0,
  DISPLAY_VIEW_MODULE_LIST = 1,
  DISPLAY_VIEW_MODULE_DETAIL = 2,
  DISPLAY_VIEW_ALARMS = 3,
  DISPLAY_VIEW_SYSTEM = 4,
};

static uint16_t clamp_u16_i32(int32_t value, uint16_t low, uint16_t high) {
  if (value < (int32_t)low) return low;
  if (value > (int32_t)high) return high;
  return (uint16_t)value;
}

static const char* schedulerModuleTypeName(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "JBC FAE Bus";
    case MODULE_JBC_USB: return "JBC USB";
    case MODULE_FAN_IO: return "Fan/IO";
    case MODULE_FAN_IO_PRO: return "Fan/IO Pro";
    case MODULE_WELLER_ZERO_SMOG: return "Weller Zero Smog Bus";
    case MODULE_DISPLAY: return "Display";
    case MODULE_UNIVERSAL_RS232: return "Universal RS232 Bridge";
    case MODULE_MODBUS_RTU: return "Modbus RTU Bridge";
    case MODULE_SENSOR_RESERVED: return "Sensor";
    default: return "Module";
  }
}

static bool moduleTypeDefaultAddress(uint8_t type, uint8_t addr) {
  switch (type) {
    case MODULE_JBC_BUS: return addr == 0x10;
    case MODULE_JBC_USB: return addr == 0x11;
    case MODULE_FAN_IO:
    case MODULE_FAN_IO_PRO:
      return addr == 0x20;
    case MODULE_WELLER_ZERO_SMOG: return addr == 0x30;
    case MODULE_DISPLAY: return addr == 0x40;
    case MODULE_UNIVERSAL_RS232: return addr == 0x50;
    case MODULE_MODBUS_RTU: return addr == 0x60;
    default: return false;
  }
}
static void schedulerModuleName(const ModuleRecord& rec, char* out, size_t out_len) {
  if (!out || !out_len) return;
  const char* name = rec.label[0] ? rec.label : (rec.name[0] ? rec.name : schedulerModuleTypeName(rec.type));
  snprintf(out, out_len, "%s 0x%02X", name, rec.addr);
}

static bool contains_ci_ascii(const char* haystack, const char* needle) {
  if (!haystack || !needle || !*needle) return false;
  const size_t nl = strlen(needle);
  for (const char* p = haystack; *p; ++p) {
    size_t i = 0;
    while (i < nl && p[i]) {
      char a = p[i];
      char b = needle[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
      if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
      if (a != b) break;
      ++i;
    }
    if (i == nl) return true;
  }
  return false;
}

static bool parse_universal_descriptor_line(const char* line, uint8_t& id, const char*& type_start, size_t& type_len) {
  if (!line) return false;
  while (*line == ' ' || *line == '	') ++line;
  if (*line < '0' || *line > '9') return false;
  uint16_t v = 0;
  while (*line >= '0' && *line <= '9') {
    v = (uint16_t)(v * 10U + (uint8_t)(*line - '0'));
    if (v > 255) return false;
    ++line;
  }
  if (*line != ' ' && *line != '	') return false;
  while (*line == ' ' || *line == '	') ++line;
  type_start = line;
  while (*line && *line != ' ' && *line != '	') ++line;
  type_len = (size_t)(line - type_start);
  id = (uint8_t)v;
  return type_len > 0;
}

static bool universal_line_type_is(const char* type_start, size_t type_len, const char* type) {
  return strlen(type) == type_len && strncmp(type_start, type, type_len) == 0;
}

static void descriptor_append_bounded(char* dst, size_t dst_len, const char* src, size_t n) {
  if (!dst || !dst_len || !src || !n) return;
  size_t cur = strlen(dst);
  if (cur >= dst_len - 1) return;
  size_t space = dst_len - 1 - cur;
  if (n > space) n = space;
  memcpy(dst + cur, src, n);
  dst[cur + n] = 0;
}

static void descriptor_append_cstr_bounded(char* dst, size_t dst_len, const char* src) {
  if (!src) return;
  descriptor_append_bounded(dst, dst_len, src, strlen(src));
}

static bool descriptor_line_starts_with(const char* line, const char* prefix) {
  if (!line || !prefix) return false;
  while (*line == ' ' || *line == '\t') ++line;
  const size_t n = strlen(prefix);
  return strncmp(line, prefix, n) == 0;
}

static bool descriptor_text_entity_start(const char* text) {
  if (!text) return false;
  while (*text == ' ' || *text == '\t') ++text;
  uint8_t id = 0;
  const char* type_start = nullptr;
  size_t type_len = 0;
  return parse_universal_descriptor_line(text, id, type_start, type_len) && id >= 20;
}

static void normalize_universal_descriptor_text(char* text, size_t text_len) {
  if (!text || text_len < 8 || !text[0]) return;

  // Never put the 4 KiB descriptor work buffer on loopTask's stack. The
  // compatibility normalizer is non-real-time and is an ideal PSRAM user.
  char* normalized = static_cast<char*>(
      heap_caps_malloc(text_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!normalized) {
    // Normalization only repairs legacy/corrupt line_end formatting. If PSRAM
    // is unavailable, skipping this optional repair is safer than exhausting
    // internal DRAM or the loop task stack.
    return;
  }
  normalized[0] = 0;
  bool changed = false;

  const char* p = text;
  while (*p) {
    const char* nl = strchr(p, '\n');
    const size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
    char line[320];
    size_t copy_len = line_len;
    if (copy_len >= sizeof(line)) copy_len = sizeof(line) - 1;
    memcpy(line, p, copy_len);
    line[copy_len] = 0;
    while (copy_len && (line[copy_len - 1] == '\r' || line[copy_len - 1] == '\n')) line[--copy_len] = 0;

    if (descriptor_line_starts_with(line, "line_end=")) {
      char* v = strchr(line, '=');
      if (v) {
        ++v;
        while (*v == ' ' || *v == '\t') ++v;
        if (descriptor_text_entity_start(v)) {
          // Some older/corrupt descriptor transfers ended up as
          //   line_end=20 number ...
          // instead of
          //   line_end=NONE
          //   20 number ...
          // Recover it here so generic role parsing keeps working.
          descriptor_append_cstr_bounded(normalized, text_len, "line_end=NONE\n");
          descriptor_append_cstr_bounded(normalized, text_len, v);
          descriptor_append_cstr_bounded(normalized, text_len, "\n");
          changed = true;
          p = nl ? nl + 1 : p + line_len;
          continue;
        }
        if (*v == 0) {
          descriptor_append_cstr_bounded(normalized, text_len, "line_end=NONE\n");
          changed = true;
          p = nl ? nl + 1 : p + line_len;
          continue;
        }
      }
    }

    descriptor_append_cstr_bounded(normalized, text_len, line);
    descriptor_append_cstr_bounded(normalized, text_len, "\n");
    p = nl ? nl + 1 : p + line_len;
  }

  if (changed) {
    strncpy(text, normalized, text_len - 1);
    text[text_len - 1] = 0;
  }
  heap_caps_free(normalized);
}


enum DisplayUniversalEntityType : uint8_t {
  DISPLAY_UNI_SENSOR = 1,
  DISPLAY_UNI_BINARY_SENSOR = 2,
  DISPLAY_UNI_NUMBER = 3,
  DISPLAY_UNI_SWITCH = 4,
  DISPLAY_UNI_BUTTON = 5,
  DISPLAY_UNI_TEXT = 6,
  DISPLAY_UNI_SELECT = 7,
};

static bool ascii_eq_ci_n(const char* a, size_t alen, const char* b) {
  if (!a || !b) return false;
  const size_t blen = strlen(b);
  if (alen != blen) return false;
  for (size_t i = 0; i < alen; ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return false;
  }
  return true;
}

static uint8_t display_universal_type_code(const char* type, size_t type_len) {
  if (ascii_eq_ci_n(type, type_len, "number")) return DISPLAY_UNI_NUMBER;
  if (ascii_eq_ci_n(type, type_len, "switch")) return DISPLAY_UNI_SWITCH;
  if (ascii_eq_ci_n(type, type_len, "binary_sensor")) return DISPLAY_UNI_BINARY_SENSOR;
  if (ascii_eq_ci_n(type, type_len, "button")) return DISPLAY_UNI_BUTTON;
  if (ascii_eq_ci_n(type, type_len, "text")) return DISPLAY_UNI_TEXT;
  if (ascii_eq_ci_n(type, type_len, "select")) return DISPLAY_UNI_SELECT;
  return DISPLAY_UNI_SENSOR;
}

static bool descriptor_key_value(const char* line, const char* key, char* out, size_t out_len) {
  if (!out || !out_len) return false;
  out[0] = 0;
  if (!line || !key || !*key) return false;
  char needle[24];
  snprintf(needle, sizeof(needle), "%s=", key);
  const size_t needle_len = strlen(needle);
  const char* p = line;
  while ((p = strstr(p, needle)) != nullptr) {
    if (p == line || p[-1] == ' ' || p[-1] == '\t') break;
    p += needle_len;
  }
  if (!p) return false;
  p += needle_len;
  const char* end = p;
  static const char* keys[] = {
    " idx=", " source=", " group=", " ui=", " key=", " en=", " de=", " role=", " unit=", " access=", " mode=",
    " min=", " max=", " step=", " value_on=", " value_off=", " options=", " values=",
    " slave=", " reg=", " func=", " read_func=", " poll_ms=",
    " scale=", " multiplier=", " divisor=", " divider=", " div=", " offset=", " off=",
    " bitmask=", " mask=", " bit_shift=", " shift=",
    " time_base=", " time_display=", " tb=", " tf=", " map_mode=", " map=", " map_default=",
    " repeat_on_ms=", " repeat_off_ms=",
    " trace_poll_hex=", " trace_match_hex=", " trace_set_hex=", " trace_on_hex=", " trace_off_hex=", " trace_scale=",
    " sc=", " tp=", " tm=", " ts=", " tn=", " to="
  };
  while (*end) {
    bool next_key = false;
    if (*end == ' ') {
      for (uint8_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const size_t kl = strlen(keys[i]);
        if (strncmp(end, keys[i], kl) == 0) { next_key = true; break; }
      }
    }
    if (next_key) break;
    ++end;
  }
  size_t n = (size_t)(end - p);
  while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r')) --n;
  if (n >= out_len) n = out_len - 1;
  memcpy(out, p, n);
  out[n] = 0;
  return n > 0;
}

static bool descriptor_access_mode(const char* line, char* out, size_t out_len) {
  if (!out || out_len < 3) return false;
  out[0] = 0;
  char mode[8] = {0};
  descriptor_key_value(line, "access", mode, sizeof(mode));
  if (!mode[0]) descriptor_key_value(line, "mode", mode, sizeof(mode));
  if (!mode[0] && line) {
    char id_s[8], type_s[18], key_s[32];
    sscanf(line, "%7s %17s %31s %7s", id_s, type_s, key_s, mode);
  }
  for (char* q = mode; *q; ++q) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
  if (strcmp(mode, "ro") && strcmp(mode, "rw") && strcmp(mode, "wo")) return false;
  snprintf(out, out_len, "%s", mode);
  return true;
}

static bool descriptor_access_readable(const char* line) {
  char mode[4];
  return descriptor_access_mode(line, mode, sizeof(mode)) && (strcmp(mode, "ro") == 0 || strcmp(mode, "rw") == 0);
}

static bool descriptor_access_writable(const char* line) {
  char mode[4];
  return descriptor_access_mode(line, mode, sizeof(mode)) && (strcmp(mode, "wo") == 0 || strcmp(mode, "rw") == 0);
}

static int16_t descriptor_i16_value(const char* line, const char* key, int16_t fallback) {
  char tmp[16];
  if (!descriptor_key_value(line, key, tmp, sizeof(tmp))) return fallback;
  long v = strtol(tmp, nullptr, 0);
  if (v < -32768L) v = -32768L;
  if (v > 32767L) v = 32767L;
  return (int16_t)v;
}

static bool descriptor_token_at(const char* list, uint8_t wanted, char* out, size_t out_len) {
  if (!out || !out_len) return false;
  out[0] = 0;
  if (!list || !*list) return false;
  uint8_t idx = 0;
  const char* start = list;
  const char* p = list;
  while (true) {
    if (*p == '|' || *p == ';' || *p == 0) {
      if (idx == wanted) {
        const char* end = p;
        while (start < end && (*start == ' ' || *start == '\t')) ++start;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
        size_t n = (size_t)(end - start);
        if (n >= out_len) n = out_len - 1;
        if (n) memcpy(out, start, n);
        out[n] = 0;
        return n > 0;
      }
      if (*p == 0) break;
      ++idx;
      start = p + 1;
    }
    if (*p == 0) break;
    ++p;
  }
  return false;
}


static uint8_t universal_entity_text_value(const UniversalEntityState* state, char* out, size_t out_len);

static bool descriptor_token_matches_state(const char* token, const char* state) {
  if (!token || !*token || !state || !*state) return false;
  if (!strcasecmp(token, state)) return true;
  bool token_numeric = true;
  for (const char* p = token; *p; ++p) {
    if (*p < '0' || *p > '9') { token_numeric = false; break; }
  }
  if (token_numeric) {
    const size_t tl = strlen(token);
    const size_t sl = strlen(state);
    if (sl >= tl && !strcmp(state + sl - tl, token)) return true; // e.g. F144 matches 144
  }
  return false;
}

static bool descriptor_state_number(const char* state, int32_t& out) {
  if (!state || !*state) return false;
  const char* p = state;
  while (*p && !((*p >= '0' && *p <= '9') || *p == '-' || *p == '+')) ++p;
  if (!*p) return false;
  char tmp[16];
  uint8_t n = 0;
  if ((*p == '-' || *p == '+') && n < sizeof(tmp) - 1) tmp[n++] = *p++;
  while (*p >= '0' && *p <= '9' && n < sizeof(tmp) - 1) tmp[n++] = *p++;
  if (!n || (n == 1 && (tmp[0] == '-' || tmp[0] == '+'))) return false;
  tmp[n] = 0;
  out = atol(tmp);
  return true;
}

static bool descriptor_state_numeric_payload(const char* state, int32_t& out) {
  if (!state || !*state) return false;
  const char* p = state;
  while (*p == ' ' || *p == '\t') ++p;
  if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
    const char* next = p + 1;
    if (*next == '-' || *next == '+' || (*next >= '0' && *next <= '9')) p = next;
  }

  char* endp = nullptr;
  long v = strtol(p, &endp, 10);
  if (endp == p) return false;
  while (endp && (*endp == ' ' || *endp == '\t' || *endp == '\r')) ++endp;
  if (endp && *endp) return false;
  out = (int32_t)v;
  return true;
}

static uint16_t descriptor_scale_value(const char* line) {
  int16_t scale = descriptor_i16_value(line, "scale", 0);
  if (!scale) scale = descriptor_i16_value(line, "sc", 0);
  if (!scale) scale = descriptor_i16_value(line, "trace_scale", 0);
  return scale > 1 ? (uint16_t)scale : 1U;
}

static bool descriptor_token_matches_state_scaled(const char* line, const char* token, const char* state) {
  if (descriptor_token_matches_state(token, state)) return true;
  if (!token || !*token) return false;
  char* endp = nullptr;
  const long token_num = strtol(token, &endp, 0);
  while (endp && (*endp == ' ' || *endp == '\t')) ++endp;
  if (!endp || *endp) return false;
  int32_t state_num = 0;
  if (!descriptor_state_number(state, state_num)) return false;
  if (state_num == token_num) return true;
  const uint16_t scale = descriptor_scale_value(line);
  if (scale > 1 && state_num * (int32_t)scale == token_num) return true;
  if (scale > 1 && token_num * (int32_t)scale == state_num) return true;
  return false;
}

static bool descriptor_time_value_text(const char* line, const char* state, char* out, size_t out_len) {
  if (!out || !out_len) return false;
  out[0] = 0;
  char display[12];
  char base[8];
  display[0] = 0;
  base[0] = 0;
  descriptor_key_value(line, "time_display", display, sizeof(display));
  if (!display[0]) descriptor_key_value(line, "tf", display, sizeof(display));
  descriptor_key_value(line, "time_base", base, sizeof(base));
  if (!base[0]) descriptor_key_value(line, "tb", base, sizeof(base));
  if (!display[0] || !base[0] || !contains_ci_ascii(display, "dhm")) return false;

  int32_t value = 0;
  if (!descriptor_state_numeric_payload(state, value)) return false;
  value *= (int32_t)descriptor_scale_value(line);

  int32_t minutes = value;
  if (contains_ci_ascii(base, "s")) minutes = value / 60;
  else if (contains_ci_ascii(base, "h")) minutes = value * 60;
  else if (contains_ci_ascii(base, "d")) minutes = value * 1440;
  if (minutes < 0) minutes = 0;

  const int32_t days = minutes / 1440;
  const int32_t hours = (minutes % 1440) / 60;
  const int32_t mins = minutes % 60;
  if (days > 0 && hours > 0) snprintf(out, out_len, "%ldd %ldh", (long)days, (long)hours);
  else if (days > 0) snprintf(out, out_len, "%ldd", (long)days);
  else if (hours > 0 && mins > 0) snprintf(out, out_len, "%ldh %ldm", (long)hours, (long)mins);
  else if (hours > 0) snprintf(out, out_len, "%ldh", (long)hours);
  else snprintf(out, out_len, "%ldm", (long)mins);
  return out[0] != 0;
}

static bool descriptor_scaled_value_text(const char* line, const char* state, char* out, size_t out_len) {
  if (!out || !out_len) return false;
  out[0] = 0;
  int32_t value = 0;
  if (!descriptor_state_numeric_payload(state, value)) return false;
  const uint16_t scale = descriptor_scale_value(line);
  if (scale <= 1) return false;
  value *= (int32_t)scale;
  snprintf(out, out_len, "%ld", (long)value);
  return true;
}

static bool descriptor_select_index_for_state(const char* line, const UniversalEntityState* state, uint8_t& index) {
  index = 0;
  char state_text[24];
  universal_entity_text_value(state, state_text, sizeof(state_text));
  if (!state_text[0]) return false;
  char values[192];
  char options[192];
  values[0] = 0;
  options[0] = 0;
  descriptor_key_value(line, "values", values, sizeof(values));
  descriptor_key_value(line, "options", options, sizeof(options));
  for (uint8_t i = 0; i < 24; ++i) {
    char token[32];
    if (values[0] && descriptor_token_at(values, i, token, sizeof(token)) && descriptor_token_matches_state_scaled(line, token, state_text)) {
      index = i;
      return true;
    }
    if (options[0] && descriptor_token_at(options, i, token, sizeof(token)) && descriptor_token_matches_state_scaled(line, token, state_text)) {
      index = i;
      return true;
    }
    if ((!values[0] || !descriptor_token_at(values, i, token, sizeof(token))) &&
        (!options[0] || !descriptor_token_at(options, i, token, sizeof(token)))) break;
  }
  return false;
}

static uint8_t descriptor_select_index_or_zero(const char* line, const UniversalEntityState* state) {
  uint8_t index = 0;
  descriptor_select_index_for_state(line, state, index);
  return index;
}

static bool descriptor_select_value_for_index(const char* line, uint8_t index, char* out, size_t out_len) {
  char list[192];
  if (descriptor_key_value(line, "values", list, sizeof(list)) && descriptor_token_at(list, index, out, out_len)) return true;
  if (descriptor_key_value(line, "options", list, sizeof(list)) && descriptor_token_at(list, index, out, out_len)) return true;
  return false;
}

static bool descriptor_select_option_for_index(const char* line, uint8_t index, char* out, size_t out_len) {
  char list[192];
  return descriptor_key_value(line, "options", list, sizeof(list)) && descriptor_token_at(list, index, out, out_len);
}

static bool descriptor_map_text_for_state(const char* line, const char* state, char* out, size_t out_len) {
  if (!out || !out_len) return false;
  out[0] = 0;
  if (!line || !state || !*state) return false;
  char map[180];
  if (!descriptor_key_value(line, "map", map, sizeof(map))) return false;
  char mode[12];
  descriptor_key_value(line, "map_mode", mode, sizeof(mode));
  if (mode[0] && !contains_ci_ascii(mode, "exact")) return false;

  const char* p = map;
  while (p && *p) {
    const char* next = strchr(p, '|');
    const char* end = next ? next : p + strlen(p);
    const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
    if (eq) {
      char token[24];
      size_t token_len = (size_t)(eq - p);
      while (token_len && (p[token_len - 1] == ' ' || p[token_len - 1] == '\t')) --token_len;
      if (token_len >= sizeof(token)) token_len = sizeof(token) - 1;
      memcpy(token, p, token_len);
      token[token_len] = 0;
      if (descriptor_token_matches_state_scaled(line, token, state)) {
        const char* value = eq + 1;
        while (value < end && (*value == ' ' || *value == '\t')) ++value;
        size_t value_len = (size_t)(end - value);
        while (value_len && (value[value_len - 1] == ' ' || value[value_len - 1] == '\t' || value[value_len - 1] == '\r')) --value_len;
        if (value_len >= out_len) value_len = out_len - 1;
        memcpy(out, value, value_len);
        out[value_len] = 0;
        return out[0] != 0;
      }
    }
    p = next ? next + 1 : nullptr;
  }

  return descriptor_key_value(line, "map_default", out, out_len);
}

static void descriptor_display_state_text(const char* line, uint8_t type_code, const UniversalEntityState* state, char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  char raw[32];
  universal_entity_text_value(state, raw, sizeof(raw));
  if (!raw[0]) return;

  // Universal/Modbus bridges already apply the profile-builder transforms
  // (scale, time formatting and value mapping) before exposing entity states.
  // Keep the display path aligned with the web UI: use those finished values
  // directly and only resolve select raw values to their visible option label.
  if (type_code == DISPLAY_UNI_SELECT) {
    uint8_t index = 0;
    if (descriptor_select_index_for_state(line, state, index) && descriptor_select_option_for_index(line, index, out, out_len)) return;
    snprintf(out, out_len, "%s", raw);
    return;
  }
  snprintf(out, out_len, "%s", raw);
}

static bool descriptor_line_for_entity(const ModuleRecord& rec, uint8_t entity_id, char* out, size_t out_len) {
  if (!out || !out_len || !rec.universal_descriptor_valid || !rec.universal_descriptor[0]) return false;
  out[0] = 0;
  const char* scan = rec.universal_descriptor;
  while (scan && *scan) {
    const char* next = strchr(scan, '\n');
    char buf[1024];
    size_t len = next ? (size_t)(next - scan) : strlen(scan);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, scan, len);
    buf[len] = 0;
    uint8_t id = 0;
    const char* type_start = nullptr;
    size_t type_len = 0;
    if (parse_universal_descriptor_line(buf, id, type_start, type_len) && id == entity_id) {
      if (len >= out_len) len = out_len - 1;
      memcpy(out, buf, len);
      out[len] = 0;
      return true;
    }
    scan = next ? next + 1 : nullptr;
  }
  return false;
}

static const UniversalEntityState* find_universal_entity_state(const ModuleRecord& rec, uint8_t id) {
  for (uint8_t i = 0; i < rec.universal_entity_count; ++i) {
    if (rec.universal_entities[i].id == id) return &rec.universal_entities[i];
  }
  return nullptr;
}

static void remember_universal_entity_state(ModuleRecord& rec, uint8_t id, const uint8_t* data, uint8_t len) {
  if (!id) return;
  uint8_t slot = rec.universal_entity_count;
  for (uint8_t i = 0; i < rec.universal_entity_count; ++i) {
    if (rec.universal_entities[i].id == id) { slot = i; break; }
  }
  if (slot >= ModuleRecord::UNIVERSAL_ENTITY_MAX) return;
  UniversalEntityState& e = rec.universal_entities[slot];
  e.id = id;
  e.age_ms = 0;
  e.len = len > sizeof(e.data) ? sizeof(e.data) : len;
  if (e.len && data) memcpy(e.data, data, e.len);
  if (e.len < sizeof(e.data)) memset(e.data + e.len, 0, sizeof(e.data) - e.len);
  if (slot == rec.universal_entity_count) rec.universal_entity_count++;
  rec.universal_entities_valid = true;
  rec.universal_entities_last_ms = millis();
}

static int16_t universal_entity_numeric_value(const UniversalEntityState* state) {
  if (!state || !state->len) return 0;
  int32_t v = 0;
  bool ascii_number = true;
  for (uint8_t i = 0; i < state->len; ++i) {
    const uint8_t c = state->data[i];
    if (!((c >= '0' && c <= '9') || c == '-' || c == '+')) { ascii_number = false; break; }
  }
  if (ascii_number && state->len < 12) {
    char tmp[12];
    memcpy(tmp, state->data, state->len);
    tmp[state->len] = 0;
    v = atol(tmp);
  } else if (state->len == 1) v = state->data[0];
  else if (state->len == 2) v = (int16_t)get_u16_le(state->data);
  else if (state->len >= 4) v = (int32_t)get_u32_le(state->data);
  else {
    char tmp[12];
    uint8_t n = state->len;
    if (n > sizeof(tmp) - 1) n = sizeof(tmp) - 1;
    memcpy(tmp, state->data, n);
    tmp[n] = 0;
    v = atol(tmp);
  }
  if (v < -32768L) v = -32768L;
  if (v > 32767L) v = 32767L;
  return (int16_t)v;
}

static uint8_t universal_entity_text_value(const UniversalEntityState* state, char* out, size_t out_len) {
  if (!out || !out_len) return 0;
  out[0] = 0;
  if (!state || !state->len) return 0;
  uint8_t n = state->len;
  if (n > out_len - 1) n = out_len - 1;
  bool printable = true;
  for (uint8_t i = 0; i < n; ++i) if (state->data[i] < 0x20 || state->data[i] > 0x7E) printable = false;
  if (printable) {
    memcpy(out, state->data, n);
    out[n] = 0;
  } else if (state->len >= 4) {
    snprintf(out, out_len, "%ld", (long)(int32_t)get_u32_le(state->data));
  } else {
    snprintf(out, out_len, "%d", (int)state->data[0]);
  }
  return (uint8_t)strlen(out);
}


static const char* schedulerFaultText(uint16_t faults, uint8_t module_type = MODULE_UNKNOWN) {
  if (!faults) return "OK";
  if ((faults & 0x0001U) && module_type == MODULE_WELLER_ZERO_SMOG) return "Weller device bus error";
  if ((faults & 0x0001U) && (module_type == MODULE_UNIVERSAL_RS232 || module_type == MODULE_MODBUS_RTU)) return "Local device bus inactive";
  if (faults & 0x0100U) return "No speed feedback";
  if (faults & 0x0400U) return "Low RPM";
  if (faults & 0x0200U) return "Master timeout";
  if (faults & 0x0004U) return "Filter full";
  if (faults & 0x0002U) return "Filter warning";
  if (faults & 0x0008U) return "Filter missing";
  if (faults & 0x0010U) return "Sensor fault";
  if (faults & 0x0001U) return "No speed feedback";
  return "Module fault";
}

static const char* schedulerJbcErrorText(uint16_t error) {
  if (!error) return "OK";
  if (error & 0x0001U) return "Filter lifetime expired";
  if (error & 0x0002U) return "Filter lifetime ending";
  if (error & 0x0040U) return "Blower damaged";
  if (error & 0x0800U) return "FAE system error";
  return "JBC FAE error";
}

static const char* traceCommandName(uint8_t cmd) {
  const uint8_t base = cmd & 0x7F;
  switch (base) {
    case CMD_PING: return "PING";
    case CMD_INFO: return "INFO";
    case CMD_GET_CAPS: return "GET_CAPS";
    case CMD_GET_STATUS: return "GET_STATUS";
    case CMD_GET_STATE: return "GET_STATE";
    case CMD_SET_STATE: return "SET_STATE";
    case CMD_GET_TELEMETRY: return "GET_TELEMETRY";
    case CMD_FAST_POLL: return "FAST_POLL";
    case CMD_GET_EVENTS: return "GET_EVENTS";
    case CMD_ACK_EVENTS: return "ACK_EVENTS";
    case CMD_LED_SYNC: return "LED_SYNC";
    case CMD_SET_ADDRESS: return "SET_ADDRESS";
    case CMD_SAVE_CONFIG: return "SAVE_CONFIG";
    case CMD_FACTORY_RESET: return "FACTORY_RESET";
    case CMD_DISCOVER_MODULES: return "DISCOVER";
    case CMD_SET_ADDRESS_UID: return "SET_ADDRESS_UID";
    case CMD_SET_LABEL: return "SET_LABEL";
    case CMD_SET_ENABLE: return "SET_ENABLE";
    case CMD_SET_POWER: return "SET_POWER";
    case CMD_SET_TARGET_RPM: return "SET_TARGET_RPM";
    case CMD_SET_OUTPUT: return "SET_OUTPUT";
    case CMD_GET_IO: return "GET_IO";
    case CMD_SET_IO: return "SET_IO";
    case CMD_FILTER_CALIBRATION: return "FILTER_CAL";
    case CMD_JBC_USB_CONFIG: return "JBC_USB_CONFIG";
    case CMD_FW_BEGIN: return "FW_BEGIN";
    case CMD_FW_CHUNK: return "FW_CHUNK";
    case CMD_FW_END: return "FW_END";
    case CMD_FW_ABORT: return "FW_ABORT";
    case CMD_FW_STATUS: return "FW_STATUS";
    case CMD_FW_REBOOT: return "FW_REBOOT";
    case CMD_DISPLAY_STATUS: return "DISPLAY_STATUS";
    case CMD_DISPLAY_EVENT: return "DISPLAY_EVENT";
    case CMD_DISPLAY_UPDATE: return "DISPLAY_UPDATE";
    case CMD_DISPLAY_DETAIL_PAGE: return "DISPLAY_DETAIL_PAGE";
    case CMD_DISPLAY_ALARMS: return "DISPLAY_ALARMS";
    case CMD_DISPLAY_MODULE_LIST: return "DISPLAY_MODULE_LIST";
    case CMD_DISPLAY_MODULE_DETAIL: return "DISPLAY_MODULE_DETAIL";
    case CMD_DISPLAY_CONFIG: return "DISPLAY_CONFIG";
    case CMD_TRACE_CONTROL: return "TRACE_CONTROL";
    case CMD_TRACE_READ: return "TRACE_READ";
    case CMD_DESCRIPTOR_GET: return "DESCRIPTOR_GET";
    case CMD_ENTITY_GET: return "ENTITY_GET";
    case CMD_ENTITY_SET: return "ENTITY_SET";
    case CMD_ENTITY_EVENT: return "ENTITY_EVENT";
    case CMD_FAULT_MAP_GET: return "FAULT_MAP_GET";
    case CMD_PROFILE_BEGIN: return "PROFILE_BEGIN";
    case CMD_PROFILE_CHUNK: return "PROFILE_CHUNK";
    case CMD_PROFILE_END: return "PROFILE_END";
    case CMD_PROFILE_GET: return "PROFILE_GET";
    case CMD_ERROR: return "ERROR";
    default: return "CMD";
  }
}

static const char* jbcCtrlName(uint8_t ctrl) {
  switch (ctrl) {
    case 0x00: return "M_HS";
    case 0x06: return "M_ACK";
    case 0x10: return "DLE";
    case 0x16: return "CTRL_SYN_P02";
    case 0x1C: return "M_R_DEVICEIDORIGINAL";
    case 0x1D: return "M_R_DISCOVER";
    case 0x1E: return "M_R_DEVICEID";
    case 0x1F: return "M_W_DEVICEID";
    case 0x21: return "M_FIRMWARE";
    case 0x30: return "M_R_SUCTIONLEVEL";
    case 0x31: return "M_W_SUCTIONLEVEL";
    case 0x32: return "M_R_FLOW";
    case 0x33: return "M_R_SPEED";
    case 0x34: return "M_R_SELECTFLOW";
    case 0x35: return "M_W_SELECTFLOW";
    case 0x36: return "M_R_STANDINTAKES";
    case 0x37: return "M_W_STANDINTAKES";
    case 0x38: return "M_R_INTAKEACTIVATION";
    case 0x39: return "M_W_INTAKEACTIVATION";
    case 0x3A: return "M_R_SUCTIONDELAY";
    case 0x3B: return "M_W_SUCTIONDELAY";
    case 0x3C: return "M_R_DELAYTIME";
    case 0x3D: return "M_R_ACTIVATIONPEDAL";
    case 0x3E: return "M_W_ACTIVATIONPEDAL";
    case 0x3F: return "M_R_PEDALMODE";
    case 0x40: return "M_W_PEDALMODE";
    case 0x41: return "M_R_FILTERSTATUS";
    case 0x42: return "M_R_RESETFILTER";
    case 0x44: return "M_R_CONNECTEDPEDAL";
    case 0x45: return "M_R_FILTERSAT";
    case 0x51: return "M_R_PIN";
    case 0x52: return "M_W_PIN";
    case 0x53: return "M_R_STATIONLOCKED";
    case 0x54: return "M_W_STATIONLOCKED";
    case 0x55: return "M_R_BEEP";
    case 0x56: return "M_W_BEEP";
    case 0x57: return "M_R_CONTINUOUSSUCTION";
    case 0x58: return "M_W_CONTINUOUSSUCTION";
    case 0x59: return "M_R_STATERROR";
    case 0x5B: return "M_R_DEVICENAME";
    case 0x5C: return "M_W_DEVICENAME";
    case 0x5D: return "M_R_PINENABLED";
    case 0x5E: return "M_W_PINENABLED";
    case 0x60: return "M_W_WORKINTAKES";
    case 0xE0: return "M_R_USB_CONNECTSTATUS";
    case 0xE1: return "M_W_USB_CONNECTSTATUS";
    default: return "JBC_CTRL";
  }
}

static bool jbcUsbSoldModel(const char* model) {
  if (!model || !*model) return false;
  static const char* const sold_models[] = {
    "CA","CDCF","CDN","CP","CSCV","CDE","CFE","CAE","CPE","CSVE",
    "DD","DDE","DDR","DI","DM","DME","HD","HDE","HDR","LC","NA","NAE",
    "PSE","SM","WS","ALE"
  };
  for (const char* name : sold_models) if (!strcmp(model, name)) return true;
  return false;
}

static const char* jbcUsbCtrlName(uint8_t ctrl, const char* model) {
  if (!jbcUsbSoldModel(model)) return jbcCtrlName(ctrl);
  switch (ctrl) {
    case 0x00: return "M_HS";
    case 0x15: return "M_NACK";
    case 0x21: return "Firmware";
    case 0x30: return "InfoPort";
    case 0x33: return "Levels";
    case 0x35: return "ProfileMode";
    case 0x40: return "SleepDelay";
    case 0x42: return "SleepTemp";
    case 0x44: return "HiberDelay";
    case 0x46: return "AdjustTemp";
    case 0x48: return "Cartridge";
    case 0x50: return "SelectTemp";
    case 0x52: return "TipTemp";
    case 0x53: return "Current";
    case 0x54: return "Power";
    case 0x55: return "ToolType";
    case 0x56: return "ToolLastError";
    case 0x57: return "ToolStatus";
    case 0x59: return "MosTemp";
    case 0x5A: return "DelayTime";
    case 0x60: return "RemoteMode";
    case 0x80: return "ReadContiMode";
    case 0x81: return "WriteContiMode";
    case 0x82: return "ContiInfo";
    case 0x83: return "AlarmMax";
    case 0x85: return "AlarmMin";
    case 0x88: return "LockPort";
    case 0x8A: return "AssistantConfig";
    case 0x8C: return "SolderingResult";
    case 0x8D: return "AssistantWarning";
    case 0x9A: return "SelectedProfile";
    case 0x9C: return "QSTActivate";
    case 0x9E: return "QSTStatus";
    case 0xA0: return "InterfaceConfig";
    case 0xA2: return "MaxTemp";
    case 0xA4: return "MinTemp";
    case 0xA6: return "AutoClean";
    case 0xA8: return "PINEnabled";
    case 0xAA: return "PowerLimit";
    case 0xAC: return "PIN";
    case 0xAE: return "StationError";
    case 0xAF: return "TrafoTemp";
    case 0xB1: return "DeviceName";
    case 0xB7: return "TrafoErrorTemp";
    case 0xB8: return "MosErrorTemp";
    case 0xBA: return "GroundType";
    case 0xBB: return "DateTime";
    case 0xBE: return "StationInterface";
    case 0xC0: return "CounterPlug";
    case 0xC2: return "CounterWork";
    case 0xC4: return "CounterSleep";
    case 0xC6: return "CounterHiber";
    case 0xC8: return "CounterIdle";
    case 0xCA: return "CounterSleepCycles";
    case 0xCC: return "CounterDesoldCycles";
    case 0xD0: return "PartialPlug";
    case 0xD2: return "PartialWork";
    case 0xD4: return "PartialSleep/LockPort(P01)";
    case 0xD6: return "PartialHiber";
    case 0xD8: return "PartialIdle";
    case 0xDA: return "PartialSleepCycles";
    case 0xDC: return "PartialDesoldCycles";
    case 0xE0: return "ReadConnectStatus";
    case 0xE1: return "WriteConnectStatus";
    case 0xE3: return "FrontalConnection";
    case 0xE7: return "Ethernet";
    case 0xF0: return "RobotConfig";
    case 0xF2: return "RobotStatus";
    case 0xF9: return "PeripheralCount";
    case 0xFA: return "PeripheralConfig";
    case 0xFC: return "PeripheralStatus";
    default: return jbcCtrlName(ctrl);
  }
}

static const char* wellerCmdName(uint8_t c) {
  switch (c) {
    case 'A': return "Light state";
    case 'D': return "RPM";
    case 'F': return "Programmed filter time";
    case 'G': return "Filter runtime";
    case 'L': return "Filter status";
    case 'M': return "Fan off";
    case 'N': return "Fan on";
    case 'S': return "Speed";
    case 'V': return "Software version";
    case 'a': return "Light command ack";
    case 'd': return "Set speed";
    case 'f': return "Set filter time";
    case 'g': return "Reset filter";
    default: return "Weller";
  }
}

static void traceBytesHex(const uint8_t* data, uint8_t len, char* out, size_t out_len) {
  if (!out || !out_len) return;
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (uint8_t i = 0; data && i < len && o + 2 < out_len; ++i) {
    out[o++] = hex[data[i] >> 4];
    out[o++] = hex[data[i] & 0x0F];
  }
  out[o] = 0;
}
static uint8_t traceSum8(const uint8_t* data, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; data && i < len; ++i) sum = (uint8_t)(sum + data[i]);
  return sum;
}

static void traceAscii(const uint8_t* data, uint8_t len, char* out, size_t out_len) {
  if (!out || !out_len) return;
  size_t o = 0;
  for (uint8_t i = 0; data && i < len && o + 1 < out_len; ++i) {
    const uint8_t b = data[i];
    out[o++] = (b >= 32 && b <= 126) ? (char)b : '.';
  }
  out[o] = 0;
}
static void tracePayloadText(uint8_t cmd, const uint8_t* data, uint8_t len, char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  const uint8_t base = cmd & 0x7F;
  const bool response = (cmd & 0x80) != 0;
  if (response && len > 0 && data[0] != STATUS_OK) {
    snprintf(out, out_len, "%s status=%s", traceCommandName(cmd), fwStatusName(data[0]));
    return;
  }
  switch (base) {
    case CMD_SET_ENABLE:
      if (!response && len >= 1) snprintf(out, out_len, "SET_ENABLE %s", data[0] ? "on" : "off");
      break;
    case CMD_SET_POWER:
      if (!response && len >= 2) {
        const uint16_t power = get_u16_le(data);
        snprintf(out, out_len, "SET_POWER %u (%u%%)", power, (unsigned)((power + 5) / 10));
      }
      break;
    case CMD_SET_OUTPUT:
      if (!response && len >= 4) {
        const uint16_t mask = get_u16_le(data);
        const uint16_t value = get_u16_le(data + 2);
        snprintf(out, out_len, "SET_OUTPUT mask=0x%X value=0x%X", mask, value);
      }
      break;
    case CMD_GET_STATUS:
      if (response && len >= 8) {
        const uint16_t power = get_u16_le(data + 2);
        const uint16_t rpm = get_u16_le(data + 4);
        const uint16_t fault = get_u16_le(data + 6);
        snprintf(out, out_len, "STATUS %s power=%u rpm=%u fault=0x%X", data[1] ? "on" : "off", power, rpm, fault);
      }
      break;
    case CMD_GET_IO:
      if (response && len >= 5) {
        const uint16_t in_mask = get_u16_le(data + 1);
        const uint16_t out_mask = get_u16_le(data + 3);
        snprintf(out, out_len, "IO in=0x%X out=0x%X", in_mask, out_mask);
      }
      break;
    case CMD_GET_TELEMETRY:
      if (response && len >= 8) {
        uint32_t uptime_s = 0;
        uint16_t loop_ms = 0;
        if (len >= 30 && data[1] == MODULE_WELLER_ZERO_SMOG) {
          const uint16_t runtime = get_u16_le(data + 4);
          const uint16_t programmed = get_u16_le(data + 6);
          uptime_s = get_u32_le(data + 15);
          loop_ms = get_u16_le(data + 28);
          snprintf(out, out_len, "WELLER speed=%u%% filter=%u runtime=%um programmed=%um uptime=%lus loop=%ums", data[2], data[3], runtime, programmed, (unsigned long)uptime_s, loop_ms);
          break;
        }
        uptime_s = get_u32_le(data + 1);
        loop_ms = get_u16_le(data + 5);
        if (len >= 17) {
          uptime_s = get_u32_le(data + 10);
          loop_ms = get_u16_le(data + 15);
        }
        snprintf(out, out_len, "TELEM uptime=%lus loop=%ums", (unsigned long)uptime_s, loop_ms);
      }
      break;
    case CMD_FAST_POLL:
      if (response && len >= 6) {
        snprintf(out, out_len, "FAST seq=%u work=0x%X stand=0x%X flags=0x%X", get_u16_le(data + 1), data[3], data[4], data[5]);
      }
      break;
    case CMD_TRACE_CONTROL:
      if (!response && len >= 1) {
        snprintf(out, out_len, "TRACE_CONTROL %s%s", (data[0] & 0x01) ? "enable" : "disable", (data[0] & 0x02) ? " clear" : "");
      } else if (response && len >= 1) {
        snprintf(out, out_len, "TRACE_CONTROL status=%s", fwStatusName(data[0]));
      }
      break;
    case CMD_TRACE_READ:
      if (response && len >= 5) {
        snprintf(out, out_len, "TRACE_READ active=%u dropped=%u events=%u", data[1], get_u16_le(data + 2), data[4]);
      } else {
        snprintf(out, out_len, "TRACE_READ request");
      }
      break;
    default:
      break;
  }
  if (!out[0]) snprintf(out, out_len, "%s len=%u", traceCommandName(cmd), len);
}

static void traceLocalText(uint8_t addr, MasterScheduler::TraceDirection direction, uint8_t meta1, uint8_t meta2,
                           const uint8_t* data, uint8_t len, bool jbc_usb, const char* jbc_usb_model,
                           char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  const bool tx = direction == MasterScheduler::TRACE_LOCAL_TX;
  if (addr >= 0x10 && addr <= 0x1F && jbc_usb && (meta2 == 0xEE || meta2 == 0xEF || meta2 == 0xF0)) {
    char hex[112] = {0}; char ascii[56] = {0};
    traceBytesHex(data, len, hex, sizeof(hex)); traceAscii(data, len, ascii, sizeof(ascii));
    const char* marker = meta2 == 0xEE ? "BCC ERROR" : (meta2 == 0xEF ? "FRAME ERROR" : "RAW HANDSHAKE");
    snprintf(out, out_len, "JBC USB %s %s len=%u hex=%s ascii=%s", tx ? "TX" : "RX", marker, len,
             hex[0] ? hex : "-", ascii[0] ? ascii : "-");
    return;
  }
  if (addr >= 0x10 && addr <= 0x1F && len >= 4) {
    const uint8_t src = data[0];
    const uint8_t dst = data[1];
    const uint8_t fid = data[2];
    const uint8_t ctrl = data[3];
    const uint8_t declared_len = len >= 5 ? data[4] : 0;
    const uint8_t actual_payload_len = len > 5 ? (uint8_t)(len - 5) : 0;
    char payload_hex[112] = {0};
    char payload_ascii[56] = {0};
    if (actual_payload_len) {
      traceBytesHex(data + 5, actual_payload_len, payload_hex, sizeof(payload_hex));
      traceAscii(data + 5, actual_payload_len, payload_ascii, sizeof(payload_ascii));
    }
    const char* ctrl_name = jbc_usb ? jbcUsbCtrlName(ctrl, jbc_usb_model) : jbcCtrlName(ctrl);
    if (jbc_usb && meta2 == 1) {
      snprintf(out, out_len, "JBC P01 %s %s src=0x%02X dst=0x%02X payload %u%s hex=%s ascii=%s", tx ? "TX" : "RX", ctrl_name, src, dst,
               declared_len, (declared_len && actual_payload_len < declared_len) ? " TRUNC" : "", payload_hex[0] ? payload_hex : "-", payload_ascii[0] ? payload_ascii : "-");
    } else if (jbc_usb && meta2 == 2) {
      snprintf(out, out_len, "JBC P02 %s %s fid=0x%02X src=0x%02X dst=0x%02X payload %u%s hex=%s ascii=%s", tx ? "TX" : "RX", ctrl_name, fid, src, dst,
               declared_len, (declared_len && actual_payload_len < declared_len) ? " TRUNC" : "", payload_hex[0] ? payload_hex : "-", payload_ascii[0] ? payload_ascii : "-");
    } else if (declared_len && actual_payload_len < declared_len) {
      snprintf(out, out_len, "JBC %s %s fid=0x%02X src=0x%02X dst=0x%02X payload %u/%u hex=%s ascii=%s", tx ? "TX" : "RX", ctrl_name, fid, src, dst, actual_payload_len, declared_len, payload_hex[0] ? payload_hex : "-", payload_ascii[0] ? payload_ascii : "-");
    } else {
      snprintf(out, out_len, "JBC %s %s fid=0x%02X src=0x%02X dst=0x%02X payload %u hex=%s ascii=%s", tx ? "TX" : "RX", ctrl_name, fid, src, dst, declared_len, payload_hex[0] ? payload_hex : "-", payload_ascii[0] ? payload_ascii : "-");
    }
    return;
  }
  if (addr >= 0x30 && addr <= 0x3F) {
    char ascii[72];
    traceAscii(data, len, ascii, sizeof(ascii));
    if (len == 1) {
      snprintf(out, out_len, "Weller %s query %s raw=%s", tx ? "TX" : "RX", wellerCmdName(meta1 ? meta1 : (data ? data[0] : 0)), ascii[0] ? ascii : "-");
      return;
    }
    if (len == 5 && data) {
      const uint8_t checksum = data[4];
      const uint8_t calc = traceSum8(data, 4);
      const bool checksum_ok = checksum == calc;
      if (data[1] >= '0' && data[1] <= '9' && data[2] >= '0' && data[2] <= '9' && data[3] >= '0' && data[3] <= '9') {
        const uint16_t val = (uint16_t)((data[1] - '0') * 100 + (data[2] - '0') * 10 + (data[3] - '0'));
        char raw4[5] = { (char)data[0], (char)data[1], (char)data[2], (char)data[3], 0 };
        snprintf(out, out_len, "Weller %s %s value=%u raw=%s checksum frame=0x%02X calc=0x%02X %s", tx ? "TX" : "RX", wellerCmdName(data[0]), val, raw4, checksum, calc, checksum_ok ? "OK" : "BAD");
        return;
      }
    }
    if (len == 6 && data && (data[0] == 'a' || data[0] == 'A')) {
      const uint8_t checksum = data[4];
      const uint8_t calc = traceSum8(data, 4);
      char raw4[5] = { (char)data[0], (char)data[1], (char)data[2], (char)data[3], 0 };
      snprintf(out, out_len, "Weller %s light command raw=%s suffix=%c checksum frame=0x%02X calc=0x%02X %s", tx ? "TX" : "RX", raw4, (char)data[5], checksum, calc, checksum == calc ? "OK" : "BAD");
      return;
    }
    if (len == 7 && data && data[0] == 'a') {
      const uint8_t checksum = data[6];
      const uint8_t calc = traceSum8(data, 6);
      const uint16_t val = (data[1] >= '0' && data[1] <= '9' && data[2] >= '0' && data[2] <= '9' && data[3] >= '0' && data[3] <= '9') ? (uint16_t)((data[1] - '0') * 100 + (data[2] - '0') * 10 + (data[3] - '0')) : 0;
      snprintf(out, out_len, "Weller RX Light ack value=%u raw=%s checksum frame=0x%02X calc=0x%02X %s", val, ascii[0] ? ascii : "-", checksum, calc, checksum == calc ? "OK" : "BAD");
      return;
    }
    if (meta2 == 0xEE) {
      snprintf(out, out_len, "Weller RX checksum BAD raw=%s", ascii[0] ? ascii : "-");
      return;
    }
    snprintf(out, out_len, "Weller %s %s len=%u raw=%s", tx ? "TX" : "RX", wellerCmdName(meta1), len, ascii[0] ? ascii : "-");
    return;
  }
  if (addr >= 0x50 && addr <= 0x5F) {
    char ascii[72];
    char hex[96];
    traceAscii(data, len, ascii, sizeof(ascii));
    traceBytesHex(data, len, hex, sizeof(hex));
    const char* status = meta2 == 0xEE ? "BAD-CS" : (meta2 == 0xEF ? "BAD-PATTERN" : "OK");
    snprintf(out, out_len, "Universal RS232 %s %s m1=0x%02X len=%u ascii=%s hex=%s", tx ? "TX" : "RX", status, meta1, len, ascii[0] ? ascii : "-", hex[0] ? hex : "-");
    return;
  }
  if (addr >= 0x60 && addr <= 0x6F) {
    char ascii[72];
    char hex[96];
    traceAscii(data, len, ascii, sizeof(ascii));
    traceBytesHex(data, len, hex, sizeof(hex));
    const char* status = meta2 == 0xEE ? "BAD-CRC" : (meta2 == 0xEF ? "BAD-FRAME" : "OK");
    snprintf(out, out_len, "Modbus RTU %s %s slave=%u len=%u ascii=%s hex=%s", tx ? "TX" : "RX", status, meta1, len, ascii[0] ? ascii : "-", hex[0] ? hex : "-");
    return;
  }
  snprintf(out, out_len, "local %s m1=0x%02X m2=0x%02X len=%u", tx ? "TX" : "RX", meta1, meta2, len);
}

void MasterScheduler::begin() {
  registry_.clear();
  if (!bus_mutex_) bus_mutex_ = xSemaphoreCreateMutex();

  // Trace storage is a developer feature (~640 KiB). Allocate it lazily when
  // traceStart() is actually used instead of reserving it at every boot.
}

bool MasterScheduler::traceStorageReady() {
  if (trace_events_) return true;
  const size_t bytes = sizeof(TraceEvent) * TRACE_EVENT_CAPACITY;
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM)
  if (psramFound()) {
    trace_events_ = (TraceEvent*)ps_malloc(bytes);
    if (trace_events_) trace_events_psram_ = true;
  }
#endif
  if (!trace_events_) {
    trace_events_ = (TraceEvent*)malloc(bytes);
    trace_events_psram_ = false;
  }
  if (trace_events_) memset(trace_events_, 0, bytes);
  return trace_events_ != nullptr;
}
void MasterScheduler::traceStart(uint8_t target_addr, bool local_trace) {
  if (!traceStorageReady()) return;
  trace_stats_ = TraceStats();
  trace_head_ = 0;
  trace_count_ = 0;
  trace_seq_ = 0;
  trace_stats_.active = true;
  trace_stats_.target_addr = target_addr;
  trace_stats_.started_ms = millis();
  last_trace_client_ms_ = trace_stats_.started_ms;
  trace_local_mode_ = local_trace && target_addr != 0;
  traceSetLocalTarget(trace_local_mode_, true);
  traceLog(target_addr, TRACE_INFO, 0, 0xFF, nullptr, 0, 0, target_addr ? (trace_local_mode_ ? "local trace started" : "rs485 trace started") : "rs485 trace started all");
}

void MasterScheduler::traceStop() {
  if (!trace_stats_.active) return;
  traceLog(trace_stats_.target_addr, TRACE_INFO, 0, 0xFF, nullptr, 0, 0, "trace stopped");
  traceSetLocalTarget(false, false);
  trace_local_mode_ = false;
  trace_stats_.active = false;
}

void MasterScheduler::traceTouch() {
  if (trace_stats_.active) last_trace_client_ms_ = millis();
}
void MasterScheduler::traceClear() {
  const bool active = trace_stats_.active;
  const uint8_t target = trace_stats_.target_addr;
  const bool local_mode = trace_local_mode_;
  trace_stats_ = TraceStats();
  trace_head_ = 0;
  trace_count_ = 0;
  trace_seq_ = 0;
  trace_stats_.active = active;
  trace_stats_.target_addr = target;
  trace_stats_.started_ms = active ? millis() : 0;
  trace_local_mode_ = active && local_mode && target != 0;
  if (active) traceSetLocalTarget(trace_local_mode_, true);
  if (active) traceLog(target, TRACE_INFO, 0, 0xFF, nullptr, 0, 0, "trace cleared");
}


void MasterScheduler::traceSetLocalTarget(bool enabled, bool clear) {
  const uint8_t target = trace_stats_.target_addr;
  if (!enabled || target == 0) {
    trace_local_desired_ = false;
    trace_local_clear_pending_ = clear;
    return;
  }
  const ModuleRecord* rec = registry_.find(target);
  if (!rec || !rec->online || !(rec->caps & CAP_LOCAL_TRACE)) {
    trace_local_desired_ = false;
    trace_local_clear_pending_ = false;
    return;
  }
  if (trace_local_addr_ != target && trace_local_enabled_) {
    trace_local_desired_ = false;
    trace_local_clear_pending_ = false;
  }
  trace_local_addr_ = target;
  trace_local_desired_ = true;
  trace_local_clear_pending_ = trace_local_clear_pending_ || clear || !trace_local_enabled_;
}

bool MasterScheduler::traceLocalControl(uint8_t addr, bool enabled, bool clear) {
  uint8_t payload[1] = {0};
  if (enabled) payload[0] |= 0x01;
  if (clear) payload[0] |= 0x02;
  Frame resp;
  if (!request(addr, CMD_TRACE_CONTROL, payload, 1, resp, 25)) return false;
  return resp.cmd == (CMD_TRACE_CONTROL | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::traceLocalRead(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_TRACE_READ, nullptr, 0, resp, 25)) return false;
  if (resp.cmd != (CMD_TRACE_READ | 0x80) || resp.len < 5 || resp.payload[0] != STATUS_OK) return false;
  const bool active = resp.payload[1] != 0;
  const uint16_t remote_dropped = get_u16_le(resp.payload + 2);
  const uint8_t count = resp.payload[4];
  const ModuleRecord* trace_rec = registry_.find(addr);
  const bool trace_jbc_usb = trace_rec && (trace_rec->type == MODULE_JBC_USB || (trace_rec->caps & CAP_JBC_USB));
  uint8_t o = 5;
  for (uint8_t i = 0; i < count && o + 6 <= resp.len; ++i) {
    const uint16_t age_ms = get_u16_le(resp.payload + o); o += 2;
    const uint8_t dir = resp.payload[o++];
    const uint8_t meta1 = resp.payload[o++];
    const uint8_t meta2 = resp.payload[o++];
    const uint8_t data_len = resp.payload[o++];
    if (o + data_len > resp.len) break;
    char text[96];
    const TraceDirection local_dir = dir == 2 ? TRACE_LOCAL_TX : TRACE_LOCAL_RX;
    traceLocalText(addr, local_dir, meta1, meta2, resp.payload + o, data_len, trace_jbc_usb,
                   trace_jbc_usb && trace_rec ? trace_rec->jbc_usb_model : nullptr, text, sizeof(text));
    traceLog(addr, local_dir, meta1, meta2, resp.payload + o, data_len, age_ms, text);
    o += data_len;
  }
  if (remote_dropped) trace_stats_.dropped_events += remote_dropped;
  if (!active && trace_local_enabled_) trace_local_enabled_ = false;
  return true;
}

void MasterScheduler::tracePollLocal() {
  if (!trace_stats_.active) {
    if (trace_local_enabled_ && trace_local_addr_) {
      traceLocalControl(trace_local_addr_, false, false);
      trace_local_enabled_ = false;
    }
    return;
  }

  const uint8_t target = trace_stats_.target_addr;
  if (!trace_local_mode_) {
    if (trace_local_enabled_ && trace_local_addr_) {
      traceLocalControl(trace_local_addr_, false, false);
      trace_local_enabled_ = false;
    }
    trace_local_desired_ = false;
    return;
  }

  if (target == 0) {
    if (trace_local_enabled_ && trace_local_addr_) {
      traceLocalControl(trace_local_addr_, false, false);
      trace_local_enabled_ = false;
    }
    trace_local_desired_ = false;
    return;
  }

  const ModuleRecord* rec = registry_.find(target);
  const bool supported = rec && rec->online && (rec->caps & CAP_LOCAL_TRACE);
  if (!supported) {
    if (trace_local_enabled_ && trace_local_addr_) {
      traceLocalControl(trace_local_addr_, false, false);
      trace_local_enabled_ = false;
    }
    trace_local_desired_ = false;
    return;
  }

  if (trace_local_addr_ != target) {
    if (trace_local_enabled_ && trace_local_addr_) traceLocalControl(trace_local_addr_, false, false);
    trace_local_addr_ = target;
    trace_local_enabled_ = false;
    trace_local_clear_pending_ = true;
  }
  trace_local_desired_ = true;

  if (trace_local_desired_ && !trace_local_enabled_) {
    if (traceLocalControl(target, true, true)) {
      trace_local_enabled_ = true;
      trace_local_clear_pending_ = false;
      traceLog(target, TRACE_INFO, CMD_TRACE_CONTROL, STATUS_OK, nullptr, 0, 0, "local trace enabled");
    }
    return;
  }

  if (trace_local_enabled_ && trace_local_clear_pending_) {
    if (traceLocalControl(target, true, true)) trace_local_clear_pending_ = false;
    return;
  }

  const uint32_t now = millis();
  if (trace_local_enabled_ && (uint32_t)(now - last_trace_local_poll_ms_) >= 150UL) {
    last_trace_local_poll_ms_ = now;
    traceLocalRead(target);
  }
}
MasterScheduler::TraceStats MasterScheduler::traceStats() const {
  TraceStats stats = trace_stats_;
  stats.stored_events = trace_count_;
  return stats;
}

uint16_t MasterScheduler::traceEventCount() const {
  return trace_count_;
}

uint32_t MasterScheduler::traceOldestSeq() const {
  if (!trace_count_) return 0;
  return trace_seq_ - (uint32_t)trace_count_ + 1UL;
}

uint32_t MasterScheduler::traceNewestSeq() const {
  return trace_seq_;
}

bool MasterScheduler::traceEventAt(uint16_t index, TraceEvent& out) const {
  if (!trace_events_ || index >= trace_count_) return false;
  const uint16_t start = (trace_head_ + TRACE_EVENT_CAPACITY - trace_count_) % TRACE_EVENT_CAPACITY;
  const uint16_t pos = (start + index) % TRACE_EVENT_CAPACITY;
  out = trace_events_[pos];
  return true;
}

bool MasterScheduler::traceMatches(uint8_t addr) const {
  return trace_stats_.active && (trace_stats_.target_addr == 0 || trace_stats_.target_addr == addr);
}

void MasterScheduler::traceLog(uint8_t addr, TraceDirection direction, uint8_t cmd, uint8_t status, const uint8_t* data, uint8_t len, uint16_t latency_ms, const char* text, uint8_t frame_seq) {
  if ((cmd & 0x7F) == CMD_DISPLAY_CONFIG) { data = nullptr; len = 0; text = "display configuration (redacted)"; }
  if (!trace_events_ && !traceStorageReady()) return;
  if (!trace_stats_.active && direction != TRACE_INFO) return;
  if (direction != TRACE_INFO && !traceMatches(addr)) return;

  TraceEvent& ev = trace_events_[trace_head_];
  ev = TraceEvent();
  ev.ms = millis();
  ev.seq = ++trace_seq_;
  ev.frame_seq = frame_seq;
  ev.addr = addr;
  ev.direction = direction;
  ev.cmd = cmd;
  ev.status = status;
  ev.len = len;
  ev.data_len = len > TRACE_DATA_PREVIEW ? TRACE_DATA_PREVIEW : len;
  ev.latency_ms = latency_ms;
  if (data && ev.data_len) memcpy(ev.data, data, ev.data_len);
  if (text && text[0]) {
    strncpy(ev.text, text, sizeof(ev.text) - 1);
    ev.text[sizeof(ev.text) - 1] = 0;
  } else {
    tracePayloadText(cmd, data, len, ev.text, sizeof(ev.text));
  }

  trace_head_ = (trace_head_ + 1) % TRACE_EVENT_CAPACITY;
  if (trace_count_ < TRACE_EVENT_CAPACITY) trace_count_++;
  else trace_stats_.dropped_events++;
  trace_stats_.stored_events = trace_count_;
}
void MasterScheduler::noticeDiscoveryResponse(const Frame& resp) {
  if (resp.dst != ADDR_MASTER || resp.cmd != (CMD_DISCOVER_MODULES | 0x80) || resp.len < 15 || resp.payload[0] != STATUS_OK) return;
  const uint8_t type = resp.payload[1];
  const uint64_t uid = get_u64_le(resp.payload + 2);
  const uint8_t addr = resp.payload[10];
  if (!uid || addr < 0x10 || addr > ADDR_FACTORY) return;
  const ModuleRecord* rec = registry_.find(addr);
  const bool known_same_module = rec && rec->uid == uid && rec->type == type;
  // An authenticated WiFi session announcement also refreshes INFO/CAPS after
  // a display reboot, without inventing an offline transition during handover.
  const bool wifi_join = type == MODULE_DISPLAY && link_.lastRxWasNetwork();
  if (known_same_module && rec->online && rec->caps != 0 && !wifi_join) return;

  // A factory-addressed, address-colliding, or type-default addressed module needs
  // the full discovery / auto-address path. A known offline module that announces
  // again only needs its own address refreshed.
  bool full_scan_needed = (addr == ADDR_FACTORY) || (rec != nullptr && !known_same_module) || moduleTypeDefaultAddress(type, addr);
  if (type == MODULE_DISPLAY && master_display_wifi.active(addr)) full_scan_needed = false;
  if (pending_hotplug_scan_ && pending_hotplug_addr_ != ADDR_INVALID && pending_hotplug_addr_ != addr) full_scan_needed = true;
  if (!pending_hotplug_scan_) {
    Serial.print("JOIN/DISCOVERY addr=0x");
    if (addr < 0x10) Serial.print('0');
    Serial.print(addr, HEX);
    Serial.print(" type=0x");
    Serial.println(type, HEX);
  }
  pending_hotplug_scan_ = true;
  pending_hotplug_full_scan_ = pending_hotplug_full_scan_ || full_scan_needed;
  pending_hotplug_addr_ = full_scan_needed ? ADDR_INVALID : addr;
  pending_hotplug_scan_ms_ = millis();
}

int16_t MasterScheduler::busDiagIndex(uint8_t addr) {
  if (addr < 0x10 || addr > 0x6F) return -1;
  return (int16_t)(addr - 0x10);
}

uint16_t MasterScheduler::ofeWireBytesApprox(const Frame& frame) {
  // Two SOF bytes + VERSION/DST/SRC/SEQ/CMD/LEN + CRC16 = payload + 10.
  // Escaped 0x7E/0x7D bytes can add a very small amount on the actual wire;
  // diagnostics deliberately avoid recalculating CRC/escaping in the hot path.
  return (uint16_t)frame.len + 10U;
}

void MasterScheduler::busDiagRecordTx(const Frame& frame) {
  if (link_.lastTxWasNetwork()) return;
  const uint16_t wire = ofeWireBytesApprox(frame);
  ++ofe_tx_frames_;
  ofe_tx_wire_bytes_ += wire;
  const int16_t idx = busDiagIndex(frame.dst);
  if (idx >= 0) {
    BusModuleDiag& d = bus_module_diag_[idx];
    ++d.tx_frames;
    d.tx_wire_bytes += wire;
    d.last_activity_ms = millis();
  }
}

void MasterScheduler::busDiagRecordRx(const Frame& frame) {
  if (link_.lastRxWasNetwork()) return;
  const uint16_t wire = ofeWireBytesApprox(frame);
  ++ofe_rx_frames_;
  ofe_rx_wire_bytes_ += wire;
  const int16_t idx = busDiagIndex(frame.src);
  if (idx >= 0) {
    BusModuleDiag& d = bus_module_diag_[idx];
    ++d.rx_frames;
    d.rx_wire_bytes += wire;
    d.last_activity_ms = millis();
  }
}

bool MasterScheduler::busModuleDiag(uint8_t addr, BusModuleDiag& out) const {
  const int16_t idx = busDiagIndex(addr);
  if (idx < 0) {
    out = BusModuleDiag();
    return false;
  }
  out = bus_module_diag_[idx];
  return true;
}

struct SchedulerBusLock {
  SemaphoreHandle_t sem = nullptr;
  bool locked = false;
  SchedulerBusLock(SemaphoreHandle_t s, TickType_t wait_ticks) : sem(s) {
    locked = !sem || xSemaphoreTake(sem, wait_ticks) == pdTRUE;
  }
  ~SchedulerBusLock() {
    if (sem && locked) xSemaphoreGive(sem);
  }
};
void MasterScheduler::drainUnsolicitedFrames() {
  SchedulerBusLock bus_lock(bus_mutex_, 0);
  if (!bus_lock.locked) return;
  Frame frame;
  for (uint8_t i = 0; i < 4 && link_.poll(frame); ++i) {
    busDiagRecordRx(frame);
    noticeDiscoveryResponse(frame);
  }
}

void MasterScheduler::setLedConfig(bool enabled, uint8_t brightness_pct) {
  led_enabled_ = enabled;
  led_brightness_pct_ = constrain(brightness_pct, (uint8_t)10, (uint8_t)100);
}


void MasterScheduler::broadcastLedSync(uint32_t now) {
  SchedulerBusLock bus_lock(bus_mutex_, 0);
  if (!bus_lock.locked) return;

  uint8_t payload[6];
  put_u32_le(payload, now);
  payload[4] = led_enabled_ ? 1 : 0;
  payload[5] = constrain(led_brightness_pct_, (uint8_t)10, (uint8_t)100);

  Frame req;
  req.dst = ADDR_BROADCAST;
  req.src = ADDR_MASTER;
  req.seq = seq_++;
  req.cmd = CMD_LED_SYNC;
  req.len = sizeof(payload);
  memcpy(req.payload, payload, sizeof(payload));
  if (trace_stats_.active && trace_stats_.target_addr == 0) {
    trace_stats_.requests++;
    traceLog(ADDR_BROADCAST, TRACE_TX, CMD_LED_SYNC, STATUS_OK, payload, sizeof(payload), 0, "led sync broadcast", req.seq);
  }
  link_.send(req);
  busDiagRecordTx(req);
}

void MasterScheduler::serviceWhileWaiting() {
  const uint32_t now = millis();
  if (service_callback_ && (uint32_t)(now - last_service_callback_ms_) >= 10UL) {
    last_service_callback_ms_ = now;
    service_callback_();
  }
  delay(0);
}

void MasterScheduler::serviceDelay(uint32_t delay_ms) {
  const uint32_t start = millis();
  do {
    serviceWhileWaiting();
  } while ((uint32_t)(millis() - start) < delay_ms);
}
bool MasterScheduler::request(uint8_t dst, uint8_t cmd, const uint8_t* payload, uint8_t len, Frame& resp, uint32_t timeout_ms, bool physical) {
  if (len > MAX_PAYLOAD) return false;
  // The display tunnel uses authenticated UDP. Its normal response is fast,
  // but WiFi/FreeRTOS scheduling can occasionally defer the display task for
  // more than the short RS485 timeout. Waiting longer does not slow successful
  // requests and avoids counting a merely late WLAN response as a bus miss.
  if (!physical && master_display_wifi.active(dst) && timeout_ms < 350UL) timeout_ms = 350UL;
  SchedulerBusLock bus_lock(bus_mutex_, pdMS_TO_TICKS(timeout_ms + 25UL));
  if (!bus_lock.locked) {
    if (traceMatches(dst)) {
      trace_stats_.timeouts++;
      traceLog(dst, TRACE_TIMEOUT, cmd, 0xFF, nullptr, 0, 0, "bus lock timeout");
    }
    return false;
  }
  Frame req;
  req.dst = dst;
  req.src = ADDR_MASTER;
  req.seq = seq_++;
  req.cmd = cmd;
  req.len = len;
  if (len) memcpy(req.payload, payload, len);

  master_display_wifi.forcePhysical(physical);
  link_.send(req);
  master_display_wifi.forcePhysical(false);
  busDiagRecordTx(req);
  ++request_total_;
  request_tx_payload_bytes_ += req.len;
  const int16_t diag_idx = busDiagIndex(dst);
  if (diag_idx >= 0) ++bus_module_diag_[diag_idx].requests;
  if (traceMatches(dst)) {
    trace_stats_.requests++;
    traceLog(dst, TRACE_TX, cmd, 0xFF, req.payload, req.len, 0, nullptr, req.seq);
  }

  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeout_ms) {
    if (link_.poll(resp)) {
      busDiagRecordRx(resp);
      if (resp.dst == ADDR_MASTER && resp.src == dst) {
        if (resp.seq == req.seq) {
          uint32_t elapsed = (uint32_t)(millis() - start);
          if (elapsed > 65535UL) elapsed = 65535UL;
          const uint16_t latency = (uint16_t)elapsed;
          const uint8_t expected_cmd = (uint8_t)(cmd | 0x80);

          if (resp.cmd != expected_cmd) {
            ++request_bad_cmd_total_;
            if (diag_idx >= 0) ++bus_module_diag_[diag_idx].bad_cmd;
            if (traceMatches(dst)) {
              trace_stats_.responses++;
              trace_stats_.bad_cmd++;
              const uint32_t n = trace_stats_.responses;
              trace_stats_.avg_latency_ms = n <= 1 ? latency : ((trace_stats_.avg_latency_ms * (n - 1)) + latency) / n;
              if (latency > trace_stats_.max_latency_ms) trace_stats_.max_latency_ms = latency;
              char msg[64];
              snprintf(msg, sizeof(msg), "ignored cmd rx=0x%02X expected=0x%02X", resp.cmd, expected_cmd);
              traceLog(dst, TRACE_INFO, resp.cmd, resp.len ? resp.payload[0] : 0xFF, resp.payload, resp.len, latency, msg, resp.seq);
            }
            noticeDiscoveryResponse(resp);
            serviceWhileWaiting();
            continue;
          }

          if (diag_idx >= 0) {
            BusModuleDiag& d = bus_module_diag_[diag_idx];
            ++d.responses;
            d.latency_sum_ms += latency;
            d.latency_last_ms = latency;
            if (latency > d.latency_max_ms) d.latency_max_ms = latency;
          }
          if (traceMatches(dst)) {
            trace_stats_.responses++;
            const uint32_t n = trace_stats_.responses;
            trace_stats_.avg_latency_ms = n <= 1 ? latency : ((trace_stats_.avg_latency_ms * (n - 1)) + latency) / n;
            if (latency > trace_stats_.max_latency_ms) trace_stats_.max_latency_ms = latency;
            const uint8_t status = resp.len ? resp.payload[0] : 0xFF;
            traceLog(dst, TRACE_RX, resp.cmd, status, resp.payload, resp.len, latency, nullptr, resp.seq);
          }
          response_rx_payload_bytes_ += resp.len;
          if (ModuleRecord* ok_rec = registry_.find(dst)) {
            ok_rec->consecutive_timeouts = 0;
            ok_rec->last_seen_ms = millis();
          }
          return true;
        }

        ++request_bad_seq_total_;
        if (diag_idx >= 0) ++bus_module_diag_[diag_idx].bad_seq;
        if (traceMatches(dst)) {
          trace_stats_.bad_seq++;
          char msg[56];
          snprintf(msg, sizeof(msg), "ignored seq rx=%u expected=%u", resp.seq, req.seq);
          traceLog(dst, TRACE_INFO, resp.cmd, resp.len ? resp.payload[0] : 0xFF, resp.payload, resp.len, 0, msg, resp.seq);
        }
      } else if (trace_stats_.active && trace_stats_.target_addr == 0 && resp.dst == ADDR_MASTER) {
        traceLog(resp.src, TRACE_RX, resp.cmd, resp.len ? resp.payload[0] : 0xFF, resp.payload, resp.len, 0, "unsolicited", resp.seq);
      }
      noticeDiscoveryResponse(resp);
    }
    serviceWhileWaiting();
  }

  // An absent cable is expected while a display is connected by WiFi.
  if (physical) return false;
  if (diag_idx >= 0) ++bus_module_diag_[diag_idx].timeouts;
  if (traceMatches(dst)) {
    trace_stats_.timeouts++;
    uint32_t timeout_clamped = timeout_ms;
    if (timeout_clamped > 65535UL) timeout_clamped = 65535UL;
    traceLog(dst, TRACE_TIMEOUT, cmd, 0xFF, nullptr, 0, (uint16_t)timeout_clamped, "timeout", req.seq);
  }

  ModuleRecord* rec = registry_.find(dst);
  if (rec) {
    const bool was_online = rec->online;
    if (rec->miss_count < 0xFFFFFFFFUL) rec->miss_count++;
    rec->last_timeout_ms = millis();
    rec->last_timeout_cmd = cmd;
    if (rec->consecutive_timeouts < 255) rec->consecutive_timeouts++;
    // Auto displays wait 3.5 s for the cable, then associate/authenticate WiFi.
    // Keep real misses, but allow this bounded handover before declaring loss.
    const bool hybrid_handover = rec->type == MODULE_DISPLAY && (rec->caps & CAP_DISPLAY_HYBRID) &&
      (uint32_t)(millis() - rec->last_seen_ms) < 8000UL;
    if (rec->consecutive_timeouts >= 5 && !hybrid_handover) rec->online = false;
    // Startup retries are not offline events: count only online -> offline.
    if (was_online && !rec->online && rec->timeout_count < 0xFFFFU) rec->timeout_count++;
    if (was_online && !rec->online) {
      if (rec->caps & CAP_JBC_ACTIVITY) {
        rec->jbc_addr = 0;
        rec->station_addr = 0;
        rec->jbc_link_flags = 0;
        rec->jbc_work_mask = 0;
        rec->jbc_stand_mask = 0;
        rec->jbc_event_seq = 0;
        rec->jbc_filter_life = 0;
        rec->jbc_filter_sat = 0;
        rec->jbc_stat_error = 0;
        rec->jbc_settings_valid = false;
        rec->jbc_device_id_len = 0;
        if (extractor_.jbcState().module_addr == rec->addr) extractor_.clearJbcConnectionState();
      }
      selectRoles();
      if (rec->caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT)) extractor_.markOutputDirty();
      updateJbcAggregate();
      updateInputRouting();
      last_system_jbc_error_ = 0xFFFF;
      last_system_jbc_filter_life_ = 0xFFFF;
      last_system_jbc_filter_sat_ = 0xFFFF;
      last_system_jbc_output_enabled_ = 0xFF;
    } else if (!rec->online && (rec->caps & CAP_JBC_ACTIVITY)) {
      updateJbcAggregate();
    }
  }
  return false;
}

bool MasterScheduler::readInfo(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_INFO, nullptr, 0, resp, 30)) return false;
  if (resp.cmd != (CMD_INFO | 0x80) || resp.len < 18 || resp.payload[0] != STATUS_OK) return false;

  const uint64_t response_uid = get_u64_le(resp.payload + 8);
  ModuleRecord* rec = registry_.bindUidToAddress(response_uid, addr);
  if (!rec) {
    Serial.print("INFO UID/address conflict addr=0x");
    if (addr < 0x10) Serial.print('0');
    Serial.print(addr, HEX);
    Serial.print(" uid=");
    Serial.print((uint32_t)(response_uid >> 32), HEX);
    Serial.println((uint32_t)response_uid, HEX);
    return false;
  }

  const bool was_online = rec->online;
  rec->type = resp.payload[1];
  rec->hw_version = get_u16_le(resp.payload + 3);
  rec->fw_major = resp.payload[5];
  rec->fw_minor = resp.payload[6];
  rec->fw_patch = resp.payload[7];
  rec->fw_suffix[0] = 0;
  rec->uid = response_uid;
  rec->online = true;
  rec->seen_in_scan = true;
  rec->came_online = !was_online;
  rec->consecutive_timeouts = 0;
  rec->last_seen_ms = millis();

  uint8_t name_start = 17;
  if (resp.len > 17 && resp.payload[17] == 1) {
    name_start = 18;
  } else if (resp.len > 18 && resp.payload[17] == 2) {
    uint8_t suffix_len = resp.payload[18];
    if (suffix_len > sizeof(rec->fw_suffix) - 1) suffix_len = sizeof(rec->fw_suffix) - 1;
    if ((uint16_t)19U + suffix_len <= resp.len) {
      if (suffix_len) memcpy(rec->fw_suffix, resp.payload + 19, suffix_len);
      rec->fw_suffix[suffix_len] = 0;
      name_start = 19 + suffix_len;
    } else {
      name_start = resp.len;
    }
  }
  uint8_t name_len = 0;
  if (resp.len > name_start) {
    name_len = resp.len - name_start;
    if (name_len > sizeof(rec->name) - 1) name_len = sizeof(rec->name) - 1;
  }
  if (name_len) memcpy(rec->name, resp.payload + name_start, name_len);
  rec->name[name_len] = 0;

  // Successful GET_INFO responses are intentionally silent. readInfo() is used
  // during normal polling/rescans and printing every success floods the serial
  // monitor (especially for JBC USB). Errors/conflicts are still logged above.
  return true;
}

bool MasterScheduler::readCaps(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_GET_CAPS, nullptr, 0, resp, 30)) return false;
  if (resp.cmd != (CMD_GET_CAPS | 0x80) || resp.len < 5 || resp.payload[0] != STATUS_OK) return false;

  ModuleRecord* rec = registry_.upsert(addr);
  if (!rec) return false;
  rec->caps = get_u32_le(resp.payload + 1);
  rec->online = true;
  rec->seen_in_scan = true;
  rec->consecutive_timeouts = 0;
  rec->last_seen_ms = millis();
  if (rec->came_online && (rec->caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT))) {
    extractor_.markOutputDirty();
  }
  const bool became_online = rec->came_online;
  if (rec->caps & CAP_JBC_BUS) pending_jbc_state_addr_ = addr;
  if ((rec->type == MODULE_UNIVERSAL_RS232 || rec->type == MODULE_MODBUS_RTU)) {
    // Do not load descriptor/entities synchronously while scanning modules.
    // This path also runs during boot before the web server is available; a
    // missing/old Universal or Modbus module can otherwise block startup for a
    // long time because descriptor reads use multi-chunk retries. The normal
    // scheduler tick refreshes these caches asynchronously after boot.
    if (became_online) {
      rec->universal_descriptor_valid = false;
      rec->universal_entities_valid = false;
      rec->universal_entity_count = 0;
      rec->universal_descriptor[0] = 0;
    }
  } else {
    rec->universal_descriptor_valid = false;
    rec->universal_entities_valid = false;
    rec->universal_entity_count = 0;
    rec->universal_descriptor[0] = 0;
  }
  if (became_online && (rec->caps & (CAP_JBC_ACTIVITY | CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_WELLER_INTERFACE | CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT | CAP_DESCRIPTOR | CAP_ENTITY_CONTROL | CAP_ENTITY_EVENTS))) {
    selectRoles();
    updateJbcAggregate();
    updateInputRouting();
    last_system_jbc_error_ = 0xFFFF;
    last_system_jbc_filter_life_ = 0xFFFF;
    last_system_jbc_filter_sat_ = 0xFFFF;
  }
  rec->came_online = false;
  return true;
}

void MasterScheduler::scanKnownModules() {
  autoAddressModules();
  for (uint8_t addr = 0x10; addr <= 0x6F; ++addr) scanAddress(addr);
  selectRoles();
}

void MasterScheduler::requestScanKnownModules(bool with_auto_address, bool prune_missing) {
  scan_job_active_ = true;
  scan_job_auto_address_ = with_auto_address;
  scan_job_auto_done_ = !with_auto_address;
  scan_job_prune_missing_ = prune_missing;
  scan_job_finished_ = false;
  scan_job_next_addr_ = 0x10;
  last_scan_job_step_ms_ = 0;
  registry_.markAllScanUnseen();
}

void MasterScheduler::pollScanJob() {
  if (!scan_job_active_) return;
  const uint32_t now = millis();
  if (last_scan_job_step_ms_ && (uint32_t)(now - last_scan_job_step_ms_) < 5UL) return;

  if (!scan_job_auto_done_) {
    autoAddressModules(!scan_job_prune_missing_);
    scan_job_auto_done_ = true;
    last_scan_job_step_ms_ = millis();
    return;
  }

  if (scan_job_next_addr_ <= 0x6F) {
    scanAddress(scan_job_next_addr_++);
    last_scan_job_step_ms_ = millis();
    return;
  }

  scan_job_active_ = false;
  scan_job_auto_address_ = false;
  scan_job_auto_done_ = true;
  if (scan_job_prune_missing_) {
    const uint8_t removed = registry_.removeScanUnseen();
    if (removed) extractor_.markOutputDirty();
  }
  scan_job_prune_missing_ = false;
  selectRoles();
  updateJbcAggregate();
  updateInputRouting();
  scan_job_finished_ = true;
}

void MasterScheduler::scanAddress(uint8_t addr) {
  if (readInfo(addr)) {
    readCaps(addr);
    ModuleRecord* rec = registry_.find(addr);
    if (rec && (rec->caps & CAP_JBC_BUS)) readJbcState(addr);
    if (rec && (rec->caps & (CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT))) readIoStatus(addr, true);
    if (rec && (rec->caps & CAP_WELLER_INTERFACE)) readTelemetry(addr);
    selectRoles();
    updateJbcAggregate();
    updateInputRouting();
  }
}

void MasterScheduler::probeModule(uint8_t addr) {
  if (addr < 0x10 || addr > 0x6F) return;
  scanAddress(addr);
  selectRoles();
}

void MasterScheduler::setPreferredOutputAddr(uint8_t addr) {
  if (addr != 0 && (addr < 0x10 || addr > 0x6F)) addr = 0;
  preferred_output_addr_ = addr;
  selectRoles();
  if (have_jbc_settings_ && desired_jbc_settings_.select_flow < minSelectFlowForActiveOutput()) {
    setControlSettings(desired_jbc_settings_.suction_level, desired_jbc_settings_.select_flow,
      desired_jbc_settings_.delay_work_sec, desired_jbc_settings_.delay_stand_sec,
      desired_jbc_settings_.stand_intakes != 0, desired_jbc_settings_.continuous != 0);
  }
  extractor_.markOutputDirty();
  Preferences prefs;
  MasterSettingsStore::savePreferredOutput(prefs, addr);
}

void MasterScheduler::setJbcInputEnabled(bool enabled) {
  setMainInputSource(enabled ? INPUT_SRC_JBC_WORK : INPUT_SRC_NONE, 0, 0);
}

bool MasterScheduler::setMainInputSource(uint8_t source_type, uint8_t source_addr, uint8_t source_bit, bool persist) {
  if (source_type > INPUT_SRC_UNIVERSAL_ENTITY) return false;
  if ((source_type == INPUT_SRC_IO_INPUT && source_bit > 15) || source_bit == 0xFF) return false;
  if (source_type == INPUT_SRC_NONE) {
    source_addr = 0;
    source_bit = 0;
  } else if (source_type == INPUT_SRC_JBC_WORK) {
    if (source_addr != 0 && (source_addr < 0x10 || source_addr > 0x6F)) return false;
    source_bit = 0;
  } else if (source_addr < 0x10 || source_addr > 0x6F) {
    return false;
  }
  main_input_source_type_ = source_type;
  main_input_source_addr_ = source_addr;
  main_input_source_bit_ = source_bit;
  updateJbcAggregate();
  if (persist) {
    Preferences prefs;
    MasterSettingsStore::saveMainInput(prefs, main_input_source_type_, main_input_source_addr_, main_input_source_bit_);
  }
  return true;
}

bool MasterScheduler::setIoInputRoute(uint8_t addr, uint8_t bit, bool enabled) {
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !(rec->caps & CAP_INPUT_KEYS) || bit > 1) return false;
  if (bit == 0) rec->route_in1_output = enabled;
  else rec->route_in2_output = enabled;
  updateInputRouting();
  return true;
}

bool MasterScheduler::setInputRule(uint8_t index, const InputActionRule& rule) {
  if (index >= MAX_INPUT_RULES) return false;
  InputActionRule next = rule;
  if (next.source_type > INPUT_SRC_UNIVERSAL_ENTITY) next.source_type = INPUT_SRC_NONE;
  if (next.target_type > INPUT_TGT_EXTRACTOR_ACTION) next.target_type = INPUT_TGT_NONE;
  if ((next.source_type == INPUT_SRC_IO_INPUT && next.source_bit > 15) ||
      (next.target_type == INPUT_TGT_IO_OUTPUT && next.target_bit > 15) ||
      (next.target_type == INPUT_TGT_EXTRACTOR_ACTION &&
       (next.target_bit == EXTRACTOR_ACTION_NONE || next.target_bit > EXTRACTOR_ACTION_LAST)) ||
      next.source_bit == 0xFF || next.target_bit == 0xFF) return false;
  InputActionRule old = input_rules_[index];
  if (old.enabled && old.last_active) {
    if (old.target_type == INPUT_TGT_IO_OUTPUT) {
      ModuleRecord* rec = registry_.find(old.target_addr);
      if (rec && rec->online && (rec->caps & CAP_DIGITAL_OUTPUT)) {
        const uint16_t mask = (uint16_t)(1U << old.target_bit);
        setIoOutput(old.target_addr, mask, 0);
      }
    } else if (old.target_type == INPUT_TGT_UNIVERSAL_ENTITY) {
      const uint8_t value = '0';
      setUniversalEntity(old.target_addr, old.target_bit, &value, 1);
    }
  }
  input_rules_[index] = next;
  input_rules_[index].last_active = false;
  input_rules_[index].edge_armed = false;
  updateInputRouting();
  return true;
}

void MasterScheduler::pollHotplugDiscovery() {
  const uint32_t now = millis();
  if ((uint32_t)(now - last_hotplug_discovery_ms_) < HOTPLUG_DISCOVERY_INTERVAL_MS) return;

  SchedulerBusLock bus_lock(bus_mutex_, 0);
  if (!bus_lock.locked) return;
  last_hotplug_discovery_ms_ = now;

  Frame req;
  req.dst = ADDR_BROADCAST;
  req.src = ADDR_MASTER;
  req.seq = seq_++;
  req.cmd = CMD_DISCOVER_MODULES;
  req.len = 1;
  // Modules derive their delayed discovery slot from this round byte. Do not
  // keep it constant: a changing round prevents two UIDs that hash to the same
  // slot once from colliding on every fallback discovery forever.
  req.payload[0] = req.seq;
  link_.send(req);
  busDiagRecordTx(req);

  // Discovery responses are intentionally delayed by the modules (currently up
  // to about 383 ms). Reserve the OFE bus for that response window so normal
  // polls cannot transmit into an unsolicited discovery response.
  hotplug_discovery_window_until_ms_ = now + HOTPLUG_DISCOVERY_WINDOW_MS;
}

void MasterScheduler::pollOfflineModules() {
  const uint32_t now = millis();
  if ((uint32_t)(now - last_offline_reprobe_ms_) < OFFLINE_REPROBE_INTERVAL_MS) return;
  last_offline_reprobe_ms_ = now;

  const uint8_t count = registry_.count();
  if (!count) return;

  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_offline_probe_index_ >= count) next_offline_probe_index_ = 0;
    const uint8_t index = next_offline_probe_index_++;
    ModuleRecord& rec = registry_.at(index);
    if (rec.online || rec.addr < 0x10 || rec.addr > 0x6F) continue;


    scanAddress(rec.addr);
    ModuleRecord* updated = registry_.find(rec.addr);
    if (updated && updated->online) {
      selectRoles();
      updateJbcAggregate();
      updateInputRouting();
    }
    return;
  }
}

void MasterScheduler::selectRoles() {
  const uint8_t previous_output_addr = active_output_addr_;
  registry_.clearRoles();
  // active_jbc_addr_ remains the address of the legacy FAE/JBC bridge because
  // it is also used for station settings writes. JBC USB is a read-only JBC
  // activity source, so it receives role_jbc for routing/UI but is never made
  // the settings target.
  ModuleRecord* jbc = registry_.firstWithCaps(CAP_JBC_BUS);
  active_jbc_addr_ = jbc ? jbc->addr : JBC_MODULE_ADDR;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    ModuleRecord& rec = registry_.at(i);
    if (rec.caps & CAP_JBC_ACTIVITY) rec.role_jbc = true;
  }

  ModuleRecord* output = nullptr;
  if (preferred_output_addr_) {
    ModuleRecord* preferred = registry_.find(preferred_output_addr_);
    if (preferred && preferred->online && moduleProvidesExtractorOutput(*preferred)) output = preferred;
  } else {
    for (uint8_t i = 0; i < registry_.count(); ++i) {
      ModuleRecord& candidate = registry_.at(i);
      if (candidate.online && (candidate.caps & CAP_WELLER_INTERFACE) && moduleProvidesExtractorOutput(candidate)) { output = &candidate; break; }
    }
    if (!output) {
      for (uint8_t i = 0; i < registry_.count(); ++i) {
        ModuleRecord& candidate = registry_.at(i);
        if (moduleProvidesExtractorOutput(candidate)) { output = &candidate; break; }
      }
    }
  }
  if (output) {
    active_output_addr_ = output->addr;
    output->role_output = true;
  } else {
    active_output_addr_ = 0;
  }

  if (previous_output_addr != active_output_addr_) {
    ModuleRecord* previous = registry_.find(previous_output_addr);
    if (previous && previous->online && previous_output_addr != module_fw_target_) {
      sendOutputPower(previous_output_addr, 0);
      sendOutputEnable(previous_output_addr, false);
      previous->output_status_valid = true;
      previous->output_enabled = false;
      previous->output_power = 0;
      if (previous->caps & (CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT)) readIoStatus(previous_output_addr);
      if (previous->caps & CAP_WELLER_INTERFACE) readTelemetry(previous_output_addr);
    }
    OutputModuleState cleared_output;
    extractor_.updateOutputState(cleared_output);
    syncSystemJbcError();
    extractor_.markOutputDirty();
  }

  const uint16_t min_output_power = minSelectFlowForActiveOutput();
  if (extractor_.afterrunPower() < min_output_power) {
    setAfterrunPowerProfile(extractor_.afterrunPowerProfileEnabled(), extractor_.afterrunPower(), false);
  }
  if (have_jbc_settings_ && desired_jbc_settings_.select_flow < min_output_power) {
    setControlSettings(desired_jbc_settings_.suction_level, desired_jbc_settings_.select_flow,
      desired_jbc_settings_.delay_work_sec, desired_jbc_settings_.delay_stand_sec,
      desired_jbc_settings_.stand_intakes != 0, desired_jbc_settings_.continuous != 0);
  }
}

bool MasterScheduler::fastPollJbc(uint8_t addr) {
  Frame resp;
  uint32_t fast_timeout_ms = FAST_POLL_TIMEOUT_MS;
  if (const ModuleRecord* known = registry_.find(addr)) {
    if (known->caps & CAP_JBC_USB) fast_timeout_ms = JBC_USB_FAST_POLL_TIMEOUT_MS;
    else if (known->caps & CAP_JBC_BUS) fast_timeout_ms = JBC_BUS_FAST_POLL_TIMEOUT_MS;
  }
  if (!request(addr, CMD_FAST_POLL, nullptr, 0, resp, fast_timeout_ms)) return false;
  if (resp.cmd != (CMD_FAST_POLL | 0x80) || resp.len < 6 || resp.payload[0] != STATUS_OK) return false;

  ModuleRecord* rec = registry_.upsert(addr);
  const bool came_online = rec && !rec->online;
  bool identity_refreshed = false;
  if (rec) {
    const bool needs_identity_refresh =
      rec->type == MODULE_UNKNOWN || rec->uid == 0 || rec->fw_major == 0 || (rec->caps & CAP_JBC_ACTIVITY) == 0;
    rec->online = true;
    rec->seen_in_scan = true;
    rec->consecutive_timeouts = 0;
    rec->last_seen_ms = millis();
    if (needs_identity_refresh) {
      if (readInfo(addr)) readCaps(addr);
      rec = registry_.find(addr);
      identity_refreshed = true;
    }
  }

  // A fast-poll response alone is not enough to classify a module as the legacy
  // FAE bridge. JBC USB deliberately uses the same fast state shape but has its
  // own type/capability and must never receive FAE configuration writes.
  if (!rec || !(rec->caps & CAP_JBC_ACTIVITY)) return false;

  FastPollState fast;
  fast.event_seq = get_u16_le(resp.payload + 1);
  fast.work_mask = resp.payload[3];
  fast.stand_mask = resp.payload[4];
  fast.flags = resp.payload[5];

  // The USB bridge advances event_seq continuously with live telemetry. Keep
  // using that sequence for refresh scheduling, but do not treat every new
  // sequence number as a user-visible state transition in the serial log.
  // FAST_FLAG_EVENT_PENDING and FAST_FLAG_STATE_CHANGED are transport/event
  // notification bits. readJbcState()/readJbcUsbState() can legitimately write
  // a frame with those bits cleared back into the same ModuleRecord, so comparing
  // the complete flags byte makes the next fast poll look like another semantic
  // transition. Only stable/user-relevant flag changes belong in the Serial log.
  const uint8_t serial_state_flag_mask =
    FAST_FLAG_CONNECTED | FAST_FLAG_CONTINUOUS | FAST_FLAG_ERROR_PENDING;
  const bool state_changed =
    rec->jbc_work_mask != fast.work_mask ||
    rec->jbc_stand_mask != fast.stand_mask ||
    ((rec->jbc_link_flags ^ fast.flags) & serial_state_flag_mask) != 0;
  const bool sequence_changed = rec->jbc_event_seq != fast.event_seq;
  const bool event_changed = sequence_changed || state_changed;
  rec->jbc_event_seq = fast.event_seq;
  rec->jbc_work_mask = fast.work_mask;
  rec->jbc_stand_mask = fast.stand_mask;
  rec->jbc_link_flags = fast.flags;

  if (came_online || event_changed || identity_refreshed) {
    if (came_online || identity_refreshed) {
      if (came_online) {
        Serial.print("JBC reconnect addr=0x");
        if (addr < 0x10) Serial.print('0');
        Serial.println(addr, HEX);
      }
      if (rec->caps & CAP_JBC_BUS) {
        rec->jbc_settings_valid = false;
        pending_jbc_state_addr_ = addr;
      }
      selectRoles();
      updateJbcAggregate();
      updateInputRouting();
    }
    if (state_changed) {
      Serial.print("JBC state addr=0x");
      if (addr < 0x10) Serial.print('0');
      Serial.print(addr, HEX);
      Serial.print(" work=0x");
      Serial.print(fast.work_mask, HEX);
      Serial.print(" stand=0x");
      Serial.print(fast.stand_mask, HEX);
      Serial.print(" flags=0x");
      Serial.println(fast.flags & serial_state_flag_mask, HEX);
    } else if (serial_debug_log_ && sequence_changed) {
      Serial.print("[DEBUG] JBC fast seq=");
      Serial.print(fast.event_seq);
      Serial.print(" addr=0x");
      if (addr < 0x10) Serial.print('0');
      Serial.println(addr, HEX);
    }
    if (rec->caps & CAP_JBC_BUS) readJbcState(addr);
  }
  if (rec && (rec->caps & CAP_JBC_USB)) {
    const uint32_t now = millis();
    // JBC USB advances event_seq with ordinary live telemetry. FAST_POLL already
    // carries Work/Stand/link state, so a sequence-only change must not trigger
    // another full GET_STATE. Refresh immediately for a real semantic state
    // transition and otherwise at most once per second.
    if (came_online || state_changed || identity_refreshed ||
        (uint32_t)(now - rec->jbc_usb_state_last_ms) >= 1000UL) {
      readJbcUsbState(addr);
    }
  }
  updateJbcAggregate();
  return true;
}

bool MasterScheduler::readJbcState(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_GET_STATE, nullptr, 0, resp, 30)) return false;
  if (resp.cmd != (CMD_GET_STATE | 0x80) || resp.len < 29 || resp.payload[0] != STATUS_OK) return false;

  const uint16_t expected_system_error = systemJbcError();
  const uint16_t expected_filter_life = systemJbcFilterLife();
  const uint16_t expected_filter_sat = systemJbcFilterSaturation();

  JbcModuleState state;
  state.link_flags = resp.payload[1];
  state.jbc_addr = resp.payload[2];
  state.station_addr = resp.payload[3];
  state.base_state = resp.payload[4];
  state.work_mask = resp.payload[5];
  state.stand_mask = resp.payload[6];
  state.suction_level = resp.payload[7];
  state.select_flow = get_u16_le(resp.payload + 8);
  state.actual_flow = get_u16_le(resp.payload + 10);
  state.speed_rpm = get_u16_le(resp.payload + 12);
  state.delay_work_sec = get_u16_le(resp.payload + 14);
  state.delay_stand_sec = get_u16_le(resp.payload + 16);
  state.stand_intakes = resp.payload[18];
  state.continuous = resp.payload[19];
  state.filter_life = get_u16_le(resp.payload + 20);
  state.filter_sat = get_u16_le(resp.payload + 22);
  state.stat_error = get_u16_le(resp.payload + 24);
  state.usb_connect = resp.payload[26];
  last_jbc_state_event_seq_ = get_u16_le(resp.payload + 27);
  size_t jbc_state_tail = 29;
  if (resp.len > 29) {
    state.device_id_len = resp.payload[29];
    if (state.device_id_len > sizeof(state.device_id)) state.device_id_len = sizeof(state.device_id);
    jbc_state_tail = (size_t)30 + state.device_id_len;
    if (jbc_state_tail <= resp.len) {
      memcpy(state.device_id, resp.payload + 30, state.device_id_len);
      if (jbc_state_tail < resp.len) {
        state.extractor_output_active = resp.payload[jbc_state_tail] ? 1U : 0U;
        state.extractor_output_valid = true;
      }
    } else {
      state.device_id_len = 0;
      jbc_state_tail = 29;
    }
  }
  ModuleRecord* rec = registry_.find(addr);
  const bool module_changed_settings = rec && rec->jbc_settings_valid && recordSettingsDiffer(*rec, state);

  const bool jbc_station_settings_write = module_changed_settings &&
    (state.link_flags & FAST_FLAG_CONNECTED) &&
    (state.link_flags & FAST_FLAG_STATE_CHANGED);

  if (!have_jbc_settings_ || module_changed_settings) {
    // First JBC state after boot/reconnect may be stale/default. Adopt it live,
    // but only persist a later, explicit station-side settings write.
    setControlSettings(state.suction_level, state.select_flow, state.delay_work_sec, state.delay_stand_sec, state.stand_intakes != 0, state.continuous != 0, jbc_station_settings_write);
    if (jbc_station_settings_write) persistControlSettingsNow();
    if (module_changed_settings) syncOtherJbcSettings(addr);
  } else if (jbcSettingsDiffer(state, desired_jbc_settings_) || state.stat_error != expected_system_error ||
      state.filter_life != expected_filter_life || state.filter_sat != expected_filter_sat) {
    setJbcSettings(addr,
      desired_jbc_settings_.suction_level,
      desired_jbc_settings_.select_flow,
      desired_jbc_settings_.delay_work_sec,
      desired_jbc_settings_.delay_stand_sec,
      desired_jbc_settings_.stand_intakes != 0,
      desired_jbc_settings_.continuous != 0);
    copyDesiredJbcSettings(state);
    state.stat_error = expected_system_error;
    state.filter_life = expected_filter_life;
    state.filter_sat = expected_filter_sat;
  }

  if (state.stat_error != systemJbcError() || state.filter_life != systemJbcFilterLife() ||
      state.filter_sat != systemJbcFilterSaturation()) {
    setJbcSettings(addr,
      desired_jbc_settings_.suction_level,
      desired_jbc_settings_.select_flow,
      desired_jbc_settings_.delay_work_sec,
      desired_jbc_settings_.delay_stand_sec,
      desired_jbc_settings_.stand_intakes != 0,
      desired_jbc_settings_.continuous != 0);
    state.stat_error = expected_system_error;
    state.filter_life = expected_filter_life;
    state.filter_sat = expected_filter_sat;
  }

  // The JBC module state frame can lag one poll behind a just-sent system error.
  // Keep Web/Display on the master-computed system error and keep retrying via
  // syncSystemJbcError()/setJbcSettings() until the bridge accepts it. Without
  // this, a reported JBC state with stat_error=0 can overwrite the local alarm
  // view even though a peripheral offline alarm is active.
  state.stat_error = expected_system_error;
  state.filter_life = expected_filter_life;
  state.filter_sat = expected_filter_sat;

  if (rec) {
    rec->jbc_addr = state.jbc_addr;
    rec->station_addr = state.station_addr;
    rec->jbc_link_flags = state.link_flags;
    rec->jbc_work_mask = state.work_mask;
    rec->jbc_stand_mask = state.stand_mask;
    rec->jbc_event_seq = last_jbc_state_event_seq_;
    rec->jbc_filter_life = get_u16_le(resp.payload + 20);
    rec->jbc_filter_sat = get_u16_le(resp.payload + 22);
    rec->jbc_stat_error = get_u16_le(resp.payload + 24);
    // Preserve the actual bridge response before rememberJbcSettings() sees the
    // Master-normalized state used by the control layer.
    rec->jbc_dbg_suction_level_rx = resp.payload[7];
    rec->jbc_dbg_select_flow_rx = get_u16_le(resp.payload + 8);
    rec->jbc_dbg_delay_work_sec_rx = get_u16_le(resp.payload + 14);
    rec->jbc_dbg_delay_stand_sec_rx = get_u16_le(resp.payload + 16);
    rec->jbc_dbg_stand_intakes_rx = resp.payload[18];
    rec->jbc_dbg_continuous_rx = resp.payload[19];
    rec->jbc_actual_flow = state.actual_flow;
    rec->jbc_speed_rpm = state.speed_rpm;
    rec->jbc_extractor_output_active = state.extractor_output_active;
    rec->jbc_extractor_output_valid = state.extractor_output_valid;
    rec->jbc_device_id_len = state.device_id_len;
    memcpy(rec->jbc_device_id, state.device_id, state.device_id_len);
    rememberJbcSettings(*rec, state);
  }
  extractor_.updateJbcState(addr, state);
  updateJbcAggregate();
  return true;
}

bool MasterScheduler::readJbcUsbState(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_GET_STATE, nullptr, 0, resp, 40)) return false;
  if (resp.cmd != (CMD_GET_STATE | 0x80) || resp.len < 19 || resp.payload[0] != STATUS_OK) return false;

  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !(rec->caps & CAP_JBC_USB)) return false;

  rec->jbc_link_flags = resp.payload[1];
  rec->jbc_usb_link_state = resp.payload[2];
  rec->jbc_usb_frame_protocol = resp.payload[3];
  rec->jbc_usb_command_protocol = resp.payload[4];
  rec->jbc_addr = resp.payload[5];
  rec->station_addr = resp.payload[6];
  rec->jbc_usb_port_count = min(resp.payload[7], (uint8_t)4);
  rec->jbc_work_mask = resp.payload[8];
  rec->jbc_stand_mask = resp.payload[9];
  rec->jbc_event_seq = get_u16_le(resp.payload + 10);
  rec->jbc_usb_cp_vid = get_u16_le(resp.payload + 12);
  rec->jbc_usb_cp_pid = get_u16_le(resp.payload + 14);
  rec->jbc_usb_model_version = get_u16_le(resp.payload + 16);
  rec->jbc_usb_port_count_from_model = resp.payload[18] != 0;

  size_t o = 19;
  auto read_text = [&](char* dst, size_t dst_size) -> bool {
    if (o >= resp.len || !dst_size) return false;
    const uint8_t n = resp.payload[o++];
    if (o + n > resp.len) return false;
    const size_t copy_n = min((size_t)n, dst_size - 1);
    if (copy_n) memcpy(dst, resp.payload + o, copy_n);
    dst[copy_n] = 0;
    o += n;
    return true;
  };
  if (!read_text(rec->jbc_usb_protocol_text, sizeof(rec->jbc_usb_protocol_text))) return false;
  if (!read_text(rec->jbc_usb_model_raw, sizeof(rec->jbc_usb_model_raw))) return false;
  if (!read_text(rec->jbc_usb_model, sizeof(rec->jbc_usb_model))) return false;
  if (!read_text(rec->jbc_usb_model_type, sizeof(rec->jbc_usb_model_type))) return false;
  if (!read_text(rec->jbc_usb_sw_version, sizeof(rec->jbc_usb_sw_version))) return false;
  if (!read_text(rec->jbc_usb_hw_version, sizeof(rec->jbc_usb_hw_version))) return false;

  const bool has_delay_config = rec->fw_major > 0 || rec->fw_minor > 1 ||
                                (rec->fw_minor == 1 && rec->fw_patch >= 13);
  const uint8_t jbc_port_bytes = has_delay_config ? 15 : 12;
  for (uint8_t i = 0; i < 4; ++i) {
    JbcUsbPortState& p = rec->jbc_usb_ports[i];
    if (o + jbc_port_bytes > resp.len) { p = JbcUsbPortState(); continue; }
    p.valid = resp.payload[o++] != 0;
    p.tool = resp.payload[o++];
    p.error = resp.payload[o++];
    p.status_flags = resp.payload[o++];
    p.temperature = get_u16_le(resp.payload + o); o += 2;
    p.power_permille = get_u16_le(resp.payload + o); o += 2;
    p.time_to_sleep_hibern = get_u16_le(resp.payload + o); o += 2;
    p.future_mode = resp.payload[o++];
    p.detail_flags = resp.payload[o++];
    p.time_to_stop = 0;
    if (has_delay_config) {
      const uint8_t aux0 = resp.payload[o++];
      const uint8_t aux1 = resp.payload[o++];
      const uint8_t aux_flags = resp.payload[o++];
      // JbcUsbModule 0.1.14+: HA/JT/JTSE reuses the station-specific 3-byte
      // suffix for ReceiveFrame02_HA ToolStatus.TimeToStop. Bit7 identifies it.
      if (aux_flags & 0x80) {
        p.time_to_stop = (uint16_t)aux0 | ((uint16_t)aux1 << 8);
        p.sleep_delay_min = 0; p.hiber_delay_min = 0; p.delay_config_flags = 0;
      } else {
        p.sleep_delay_min = aux0;
        p.hiber_delay_min = aux1;
        p.delay_config_flags = aux_flags;
      }
    } else {
      p.sleep_delay_min = 0; p.hiber_delay_min = 0; p.delay_config_flags = 0;
    }
  }
  // 0.1.12+ suffix. Older JBC USB modules end directly after the four ports.
  rec->jbc_usb_station_error = (o + 2 <= resp.len) ? get_u16_le(resp.payload + o) : 0xFFFF;
  rec->jbc_usb_state_last_ms = millis();
  return true;
}

void MasterScheduler::adoptJbcSettings(const JbcModuleState& state) {
  desired_jbc_settings_.suction_level = state.suction_level;
  desired_jbc_settings_.select_flow = state.select_flow;
  desired_jbc_settings_.delay_work_sec = state.delay_work_sec;
  desired_jbc_settings_.delay_stand_sec = state.delay_stand_sec;
  desired_jbc_settings_.stand_intakes = state.stand_intakes;
  desired_jbc_settings_.continuous = state.continuous;
  have_jbc_settings_ = true;
}

bool MasterScheduler::jbcSettingsDiffer(const JbcModuleState& a, const JbcModuleState& b) const {
  return a.suction_level != b.suction_level ||
    a.select_flow != b.select_flow ||
    a.delay_work_sec != b.delay_work_sec ||
    a.delay_stand_sec != b.delay_stand_sec ||
    a.stand_intakes != b.stand_intakes ||
    a.continuous != b.continuous;
}

bool MasterScheduler::recordSettingsDiffer(const ModuleRecord& rec, const JbcModuleState& state) const {
  return rec.jbc_suction_level != state.suction_level ||
    rec.jbc_select_flow != state.select_flow ||
    rec.jbc_delay_work_sec != state.delay_work_sec ||
    rec.jbc_delay_stand_sec != state.delay_stand_sec ||
    rec.jbc_stand_intakes != state.stand_intakes ||
    rec.jbc_continuous != state.continuous;
}

void MasterScheduler::rememberJbcSettings(ModuleRecord& rec, const JbcModuleState& state) {
  rec.jbc_settings_valid = true;
  rec.jbc_suction_level = state.suction_level;
  rec.jbc_select_flow = state.select_flow;
  rec.jbc_delay_work_sec = state.delay_work_sec;
  rec.jbc_delay_stand_sec = state.delay_stand_sec;
  rec.jbc_stand_intakes = state.stand_intakes;
  rec.jbc_continuous = state.continuous;
}

void MasterScheduler::copyDesiredJbcSettings(JbcModuleState& state) const {
  state.suction_level = desired_jbc_settings_.suction_level;
  state.select_flow = desired_jbc_settings_.select_flow;
  state.delay_work_sec = desired_jbc_settings_.delay_work_sec;
  state.delay_stand_sec = desired_jbc_settings_.delay_stand_sec;
  state.stand_intakes = desired_jbc_settings_.stand_intakes;
  state.continuous = desired_jbc_settings_.continuous;
}

void MasterScheduler::syncOtherJbcSettings(uint8_t source_addr) {
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    ModuleRecord& rec = registry_.at(i);
    if (!rec.online || !(rec.caps & CAP_JBC_BUS) || rec.addr == source_addr) continue;
    setJbcSettings(rec.addr,
      desired_jbc_settings_.suction_level,
      desired_jbc_settings_.select_flow,
      desired_jbc_settings_.delay_work_sec,
      desired_jbc_settings_.delay_stand_sec,
      desired_jbc_settings_.stand_intakes != 0,
      desired_jbc_settings_.continuous != 0);
  }
}

uint16_t MasterScheduler::systemJbcError() const {
  // JBC FAE status-error masks from the original emulator table.
  static const uint16_t JBC_STOP_FILTER_LIFE = 1U;   // STOP1
  static const uint16_t JBC_WARN_FILTER_LIFE = 2U;   // WARN1
  static const uint16_t JBC_STOP_BLOWER = 64U;       // STOP5
  static const uint16_t JBC_STOP_SYSTEM = 2048U;     // STOP10
  static const uint16_t FAN_FAULT_NO_TACH = 0x0100U;
  static const uint16_t FAN_FAULT_LOW_RPM = 0x0400U;
  static const uint16_t GENERIC_FILTER_WARN = 0x0002U;
  static const uint16_t GENERIC_FILTER_FULL = 0x0004U;

  bool has_online_jbc = false;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (rec.online && (rec.caps & CAP_JBC_BUS)) {
      has_online_jbc = true;
      break;
    }
  }
  // Do not report a JBC/FAE system error when there is no JBC module on the
  // OFE bus. A display-only setup, for example, must stay alarm-free.
  if (!has_online_jbc) return 0;

  // If at least one known RS485 participant that was already discovered drops
  // off the OFE bus, report this to the JBC station as FAE system error.
  // This intentionally includes display/HMI modules too: a missing known bus
  // participant means the OFE RS485 system is no longer complete.
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (!rec.online && rec.addr >= ADDR_JBC_MIN && rec.addr <= ADDR_MODBUS_MAX &&
        rec.type != MODULE_UNKNOWN) {
      return JBC_STOP_SYSTEM;
    }
  }

  const ModuleRecord* out = registry_.find(active_output_addr_);
  if (!out || !out->online) return JBC_STOP_SYSTEM;

  uint16_t error = 0;
  const uint16_t output_fault = out->output_fault_mask | out->io_fault_mask;
  const bool generic_filter_sensor = (out->caps & CAP_FILTER_SENSOR) != 0;
  if (generic_filter_sensor && (output_fault & GENERIC_FILTER_WARN)) error |= JBC_WARN_FILTER_LIFE;
  if (generic_filter_sensor && (output_fault & GENERIC_FILTER_FULL)) error |= JBC_STOP_FILTER_LIFE;

  if (out->caps & CAP_WELLER_INTERFACE) {
    static const uint16_t WELLER_FILTER_FAULTS = 0x0006U;
    if (output_fault & (uint16_t)~WELLER_FILTER_FAULTS) error |= JBC_STOP_SYSTEM;
    if (out->weller_uart_age_sec == 0xFFFF || out->weller_uart_age_sec > 10) error |= JBC_STOP_SYSTEM;
    if (out->weller_filter_status == 10) error |= JBC_WARN_FILTER_LIFE;
    else if (out->weller_filter_status == 100) error |= JBC_STOP_FILTER_LIFE;
  } else {
    if (output_fault & (FAN_FAULT_NO_TACH | FAN_FAULT_LOW_RPM)) error |= JBC_STOP_BLOWER;
    uint16_t ignored = FAN_FAULT_NO_TACH | FAN_FAULT_LOW_RPM;
    if (generic_filter_sensor) ignored |= GENERIC_FILTER_WARN | GENERIC_FILTER_FULL;
    if (output_fault & (uint16_t)~ignored) error |= JBC_STOP_SYSTEM;
  }
  return error;
}

uint16_t MasterScheduler::systemJbcFilterLife() const {
  const ModuleRecord* out = registry_.find(active_output_addr_);
  if (!out || !out->online) return 0;
  const uint16_t output_fault = out->output_fault_mask | out->io_fault_mask;
  if ((out->caps & CAP_FILTER_SENSOR) && (output_fault & 0x0004U)) return 1000;
  if ((out->caps & CAP_FILTER_SENSOR) && (output_fault & 0x0002U)) return 900;
  if (!(out->caps & CAP_WELLER_INTERFACE)) return 0;
  if (out->weller_programmed_filter_minutes > 0) {
    const uint32_t used = (uint32_t)out->weller_filter_runtime_minutes * 1000UL /
      out->weller_programmed_filter_minutes;
    return (uint16_t)(used > 1000UL ? 1000UL : used);
  }
  if (out->weller_filter_status == 100) return 1000;
  if (out->weller_filter_status == 10) return 900;
  return 0;
}

uint16_t MasterScheduler::systemJbcFilterSaturation() const {
  const ModuleRecord* out = registry_.find(active_output_addr_);
  if (!out || !out->online) return 0;
  const uint16_t output_fault = out->output_fault_mask | out->io_fault_mask;
  if ((out->caps & CAP_FILTER_SENSOR) && (output_fault & 0x0004U)) return 1000;
  if ((out->caps & CAP_FILTER_SENSOR) && (output_fault & 0x0002U)) return 900;
  if (!(out->caps & CAP_WELLER_INTERFACE)) return 0;
  if (out->weller_filter_status == 100) return 1000;
  if (out->weller_filter_status == 10) return 900;
  return 0;
}
uint16_t MasterScheduler::minSelectFlowForActiveOutput() const {
  uint16_t min_flow = 100, max_flow = 1000, step_flow = 10;
  selectFlowBoundsForActiveOutput(min_flow, max_flow, step_flow);
  return min_flow;
}

void MasterScheduler::selectFlowBoundsForActiveOutput(uint16_t& min_flow, uint16_t& max_flow, uint16_t& step_flow) const {
  min_flow = 100;
  max_flow = 1000;
  step_flow = 10;
  const ModuleRecord* out = registry_.find(active_output_addr_);
  if (!out || !out->online) return;
  if (out->caps & CAP_WELLER_INTERFACE) {
    min_flow = 300;
    return;
  }
  if ((out->type == MODULE_UNIVERSAL_RS232 || out->type == MODULE_MODBUS_RTU) && out->universal_descriptor_valid) {
    const char* p = out->universal_descriptor;
    while (p && *p) {
      const char* line = p;
      const char* next = strchr(p, '\n');
      char buf[1024];
      size_t len = next ? (size_t)(next - line) : strlen(line);
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      memcpy(buf, line, len);
      buf[len] = 0;
      uint8_t id = 0;
      const char* type = nullptr;
      size_t type_len = 0;
      if (parse_universal_descriptor_line(buf, id, type, type_len) && id >= 20 &&
          universal_line_type_is(type, type_len, "number") &&
          contains_ci_ascii(buf, "role=main_output_power")) {
        const char* m = strstr(buf, "min=");
        const char* x = strstr(buf, "max=");
        const char* s = strstr(buf, "step=");
        if (m) {
          const long pct = strtol(m + 4, nullptr, 10);
          if (pct > 0 && pct <= 100) min_flow = (uint16_t)pct * 10U;
        }
        if (x) {
          const long pct = strtol(x + 4, nullptr, 10);
          if (pct > 0 && pct <= 100) max_flow = (uint16_t)pct * 10U;
        }
        if (s) {
          const long pct = strtol(s + 5, nullptr, 10);
          if (pct > 0 && pct <= 100) step_flow = (uint16_t)pct * 10U;
        }
        if (max_flow < min_flow) max_flow = min_flow;
        if (!step_flow) step_flow = 10;
        return;
      }
      p = next ? next + 1 : nullptr;
    }
  }
}

bool MasterScheduler::moduleProvidesExtractorOutput(const ModuleRecord& rec) const {
  if (!rec.online) return false;
  if (rec.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT)) return true;
  uint8_t enable_id = 0, power_id = 0;
  return universalFindMainOutputEntities(rec, enable_id, power_id);
}

uint8_t MasterScheduler::autoOutputCandidateAddr() const {
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& candidate = registry_.at(i);
    if (candidate.online && (candidate.caps & CAP_WELLER_INTERFACE) && moduleProvidesExtractorOutput(candidate)) return candidate.addr;
  }
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& candidate = registry_.at(i);
    if (moduleProvidesExtractorOutput(candidate)) return candidate.addr;
  }
  return 0;
}
bool MasterScheduler::universalFindMainOutputEntities(const ModuleRecord& rec, uint8_t& enable_id, uint8_t& power_id) const {
  enable_id = 0;
  power_id = 0;
  if (!rec.online || (rec.type != MODULE_UNIVERSAL_RS232 && rec.type != MODULE_MODBUS_RTU) ||
      !(rec.caps & CAP_ENTITY_CONTROL) || !rec.universal_descriptor_valid) return false;

  // Only explicit main_output_* roles belong to the OFE extractor route.
  // Generic output_* entities remain ordinary profile controls and must never
  // become the main output implicitly or drive EXTRACTOR_ON.
  const char* p = rec.universal_descriptor;
  while (p && *p) {
    const char* line = p;
    const char* next = strchr(p, '\n');
    char buf[1024];
    size_t len = next ? (size_t)(next - line) : strlen(line);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = 0;
    uint8_t id = 0;
    const char* type = nullptr;
    size_t type_len = 0;
    if (parse_universal_descriptor_line(buf, id, type, type_len) && id >= 20) {
      if (!power_id && universal_line_type_is(type, type_len, "number") &&
          contains_ci_ascii(buf, "role=main_output_power")) power_id = id;
      if (!enable_id && universal_line_type_is(type, type_len, "switch") &&
          contains_ci_ascii(buf, "role=main_output_enable")) enable_id = id;
    }
    p = next ? next + 1 : nullptr;
  }
  return enable_id || power_id;
}

bool MasterScheduler::universalSetMainOutput(ModuleRecord& rec, bool enabled, uint16_t power) {
  if (!rec.universal_descriptor_valid) refreshUniversalDescriptor(rec.addr, true);

  uint8_t enable_id = 0, power_id = 0;
  if (!universalFindMainOutputEntities(rec, enable_id, power_id)) {
    refreshUniversalDescriptor(rec.addr, true);
    if (!universalFindMainOutputEntities(rec, enable_id, power_id)) return false;
  }

  auto send_enable = [&](bool on) -> bool {
    if (!enable_id) return true;
    const uint8_t v = on ? '1' : '0';
    return setUniversalEntity(rec.addr, enable_id, &v, 1);
  };

  auto send_power = [&](uint16_t requested_power) -> bool {
    if (!power_id) return true;
    uint16_t pct = requested_power / 10U;
    if (requested_power) {
      const uint16_t min_pct = minSelectFlowForActiveOutput() / 10U;
      if (pct < min_pct) pct = min_pct;
    }
    if (pct > 100U) pct = 100U;
    char text[5];
    snprintf(text, sizeof(text), "%u", pct);
    return setUniversalEntity(rec.addr, power_id, (const uint8_t*)text, (uint8_t)strlen(text));
  };

  bool ok = true;
  if (enable_id && power_id) {
    if (enabled) {
      // Program the speed while the explicit enable is still OFF, then enable.
      // This prevents a brief restart with a stale previous speed.
      ok = send_power(power) && ok;
      ok = send_enable(true) && ok;
    } else {
      // Disable first and leave the stored speed untouched. The next ON writes
      // the desired power before enabling, so no OFF->nonzero contradiction is
      // emitted and devices whose power command also starts the fan stay OFF.
      ok = send_enable(false) && ok;
    }
  } else if (enable_id) {
    ok = send_enable(enabled) && ok;
  } else if (power_id) {
    // Power-only profiles encode ON/OFF in the power command itself.
    ok = send_power(enabled ? power : 0) && ok;
  }

  if (ok) {
    rec.output_status_valid = true;
    rec.output_enabled = enabled;
    rec.output_power = enabled ? power : 0;
  }
  return ok;
}

bool MasterScheduler::universalEntityBoolActive(const ModuleRecord& rec, uint8_t entity_id) const {
  if (!rec.online || (rec.type != MODULE_UNIVERSAL_RS232 && rec.type != MODULE_MODBUS_RTU) || !rec.universal_entities_valid) return false;
  for (uint8_t i = 0; i < rec.universal_entity_count; ++i) {
    const UniversalEntityState& e = rec.universal_entities[i];
    if (e.id != entity_id || e.len == 0) continue;
    if (e.len == 1) return e.data[0] != 0 && e.data[0] != '0';
    return !(e.data[0] == '0' && (e.len == 1 || e.data[1] == 0));
  }
  return false;
}

void MasterScheduler::updateUniversalOutputStateFromEntities(ModuleRecord& rec) {
  if (!rec.online || (rec.type != MODULE_UNIVERSAL_RS232 && rec.type != MODULE_MODBUS_RTU) || !rec.universal_entities_valid) return;

  uint8_t enable_id = 0, power_id = 0;
  universalFindMainOutputEntities(rec, enable_id, power_id);

  bool have_output_state = false;
  OutputModuleState state;
  state.valid = true;
  state.last_update_ms = millis();
  state.enabled = rec.output_enabled;
  state.power = rec.output_power;
  state.rpm = rec.output_rpm;
  state.fault_mask = rec.output_fault_mask;

  if (enable_id) {
    const UniversalEntityState* st = find_universal_entity_state(rec, enable_id);
    if (st && st->len) {
      state.enabled = universalEntityBoolActive(rec, enable_id);
      rec.output_enabled = state.enabled;
      have_output_state = true;
    }
  }

  if (power_id) {
    const UniversalEntityState* st = find_universal_entity_state(rec, power_id);
    if (st && st->len) {
      int16_t pct = universal_entity_numeric_value(st);
      if (pct < 0) pct = 0;
      // Community profiles normally report percent values (0..100). If a bridge
      // later reports the internal OFE 0..1000 range, keep it unchanged.
      uint16_t power = (pct <= 100) ? (uint16_t)pct * 10U : (uint16_t)pct;
      if (power > 1000U) power = 1000U;
      if (!enable_id) {
        // A power-only output uses 0 as OFF and any non-zero value as ON.
        state.enabled = power > 0;
        rec.output_enabled = state.enabled;
      }
      state.power = state.enabled ? power : 0;
      rec.output_power = state.power;
      have_output_state = true;
    }
  } else if (!state.enabled) {
    rec.output_power = 0;
    state.power = 0;
  }

  uint8_t rpm_id = 0;
  uint8_t legacy_rpm_id = 0;
  const char* p = rec.universal_descriptor;
  while (p && *p) {
    const char* line = p;
    const char* next = strchr(p, '\n');
    char buf[1024];
    size_t len = next ? (size_t)(next - line) : strlen(line);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = 0;
    uint8_t id = 0;
    const char* type = nullptr;
    size_t type_len = 0;
    if (parse_universal_descriptor_line(buf, id, type, type_len) && id >= 20) {
      const bool numeric_type = universal_line_type_is(type, type_len, "sensor") || universal_line_type_is(type, type_len, "number");
      if (numeric_type && contains_ci_ascii(buf, "role=main_output_rpm")) {
        rpm_id = id;
        break;
      }
      if (!legacy_rpm_id && numeric_type && (contains_ci_ascii(buf, "unit=rpm") || contains_ci_ascii(buf, "role=output_rpm") ||
                                              contains_ci_ascii(buf, " rpm") || contains_ci_ascii(buf, "drehzahl") || contains_ci_ascii(buf, "fan_rpm"))) {
        legacy_rpm_id = id;
      }
    }
    p = next ? next + 1 : nullptr;
  }
  if (!rpm_id) rpm_id = legacy_rpm_id;
  if (rpm_id) {
    const UniversalEntityState* st = find_universal_entity_state(rec, rpm_id);
    if (st && st->len) {
      int32_t rpm = universal_entity_numeric_value(st);
      // Weller Zero Smog D### reports tenths of RPM, like the native Weller module.
      if (rpm > 0 && rpm < 1000 && contains_ci_ascii(rec.universal_descriptor, "weller")) rpm *= 10;
      if (rpm < 0) rpm = 0;
      if (rpm > 65535L) rpm = 65535L;
      rec.output_rpm = (uint16_t)rpm;
      state.rpm = rec.output_rpm;
      have_output_state = true;
    }
  }

  if (have_output_state) {
    rec.output_status_valid = true;
    if (rec.addr == active_output_addr_) extractor_.updateOutputState(state);
  }
}
void MasterScheduler::syncSystemJbcError(bool force) {
  const uint16_t error = systemJbcError();
  const uint16_t filter_life = systemJbcFilterLife();
  const uint16_t filter_sat = systemJbcFilterSaturation();

  // Feed the emulated JBC FAE with the effective OFE extractor output. Prefer
  // the latest output-module readback when available; otherwise use the
  // Master's effective commanded output (also covers outputs without feedback).
  bool extractor_output_on = extractor_.outputEnabled();
  const ModuleRecord* output = registry_.find(active_output_addr_);
  if (output && output->online && output->output_status_valid) {
    extractor_output_on = output->output_enabled;
  }
  const uint8_t extractor_output_value = extractor_output_on ? 1U : 0U;

  const bool changed = error != last_system_jbc_error_ ||
                       filter_life != last_system_jbc_filter_life_ ||
                       filter_sat != last_system_jbc_filter_sat_ ||
                       extractor_output_value != last_system_jbc_output_enabled_;

  // The ExtractorLogic copy is local state for web/display. Keep it current even
  // when the bridge has not acknowledged the value yet. The per-module readback
  // below is used only for retry decisions.
  if (changed || force) {
    extractor_.updateSystemError(error);
    extractor_.updateSystemFilter(filter_life, filter_sat);
  }

  bool has_online_jbc = false;
  bool bridge_readback_mismatch = false;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (!rec.online || !(rec.caps & CAP_JBC_BUS)) continue;
    has_online_jbc = true;
    if (rec.jbc_stat_error != error || rec.jbc_filter_life != filter_life || rec.jbc_filter_sat != filter_sat) {
      bridge_readback_mismatch = true;
    }
  }

  if (!has_online_jbc) {
    // No physical JBC bridge on the OFE bus: remember the local clean state so a
    // display-only setup does not produce or retry FAE errors.
    last_system_jbc_error_ = error;
    last_system_jbc_filter_life_ = filter_life;
    last_system_jbc_filter_sat_ = filter_sat;
    // Keep output feedback dirty while no FAE bridge is online so a later
    // hot-plug receives the current extractor state immediately.
    last_system_jbc_output_enabled_ = 0xFF;
    return;
  }

  // If no JBC settings were adopted yet, still push the system error using sane
  // defaults. Otherwise an offline peripheral detected immediately after boot can
  // be visible in Web/Display but never reach the JBC bridge until a later setting
  // change happens.
  if (!have_jbc_settings_) {
    JbcModuleState state;
    state.suction_level = 3;       // Low/default OFE level
    state.select_flow = 300;       // 30 %
    state.delay_work_sec = 10;
    state.delay_stand_sec = 0;
    state.stand_intakes = 0;
    state.continuous = 0;
    adoptJbcSettings(state);
  }

  const uint32_t now = millis();
  const bool retry_due = (uint32_t)(now - last_system_jbc_push_ms_) >= 2000UL;
  const bool should_push = force || changed || (bridge_readback_mismatch && retry_due);
  if (!should_push) return;
  last_system_jbc_push_ms_ = now;

  bool all_ok = true;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    ModuleRecord& rec = registry_.at(i);
    if (!rec.online || !(rec.caps & CAP_JBC_BUS)) continue;
    if (!setJbcSettings(rec.addr,
      desired_jbc_settings_.suction_level,
      desired_jbc_settings_.select_flow,
      desired_jbc_settings_.delay_work_sec,
      desired_jbc_settings_.delay_stand_sec,
      desired_jbc_settings_.stand_intakes != 0,
      desired_jbc_settings_.continuous != 0)) {
      all_ok = false;
    }
  }

  // Only acknowledge the new system-error state after it was accepted by all
  // online JBC bridge modules. If later readback disagrees, the retry watchdog
  // above pushes it again on the next 2 s window instead of spamming the bus.
  if (all_ok) {
    last_system_jbc_error_ = error;
    last_system_jbc_filter_life_ = filter_life;
    last_system_jbc_filter_sat_ = filter_sat;
    last_system_jbc_output_enabled_ = extractor_output_value;
  }
}

void MasterScheduler::updateJbcAggregate() {
  uint8_t work = 0;
  uint8_t stand = 0;
  bool continuous = have_jbc_settings_ && desired_jbc_settings_.continuous != 0;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (!rec.online || !(rec.caps & CAP_JBC_ACTIVITY)) continue;
    if (main_input_source_type_ == INPUT_SRC_JBC_WORK && (main_input_source_addr_ == 0 || rec.addr == main_input_source_addr_)) {
      work |= rec.jbc_work_mask;
      stand |= rec.jbc_stand_mask;
    }
  }
  if (extractor_.updateAggregateJbcState(work, stand, continuous)) {
    Serial.print("JBC aggregate work=0x");
    Serial.print(work, HEX);
    Serial.print(" continuous=");
    Serial.println(continuous ? "on" : "off");
  }
  updateInputRouting();
}

bool MasterScheduler::inputRuleSourceActive(const InputActionRule& rule) const {
  if (!rule.enabled) return false;
  if (rule.source_type == INPUT_SRC_JBC_WORK) {
    for (uint8_t i = 0; i < registry_.count(); ++i) {
      const ModuleRecord& rec = registry_.at(i);
      if (!rec.online || !(rec.caps & CAP_JBC_ACTIVITY)) continue;
      if (rule.source_addr != 0 && rec.addr != rule.source_addr) continue;
      if (rec.jbc_work_mask) return true;
    }
    return false;
  }
  if (rule.source_type == INPUT_SRC_IO_INPUT) {
    const ModuleRecord* rec = registry_.find(rule.source_addr);
    if (!rec || !rec->online || !(rec->caps & CAP_INPUT_KEYS)) return false;
    return (rec->io_input_mask & (uint16_t)(1U << rule.source_bit)) != 0;
  }
  if (rule.source_type == INPUT_SRC_UNIVERSAL_ENTITY) {
    const ModuleRecord* rec = registry_.find(rule.source_addr);
    return rec && universalEntityBoolActive(*rec, rule.source_bit);
  }
  return false;
}

bool MasterScheduler::mainInputSourceAvailable() const {
  if (main_input_source_type_ == INPUT_SRC_NONE) return false;
  if (main_input_source_type_ == INPUT_SRC_JBC_WORK) {
    if (main_input_source_addr_ == 0) return onlineJbcModuleCount() > 0;
    const ModuleRecord* rec = registry_.find(main_input_source_addr_);
    return rec && rec->online && (rec->caps & CAP_JBC_ACTIVITY);
  }
  if (main_input_source_type_ == INPUT_SRC_IO_INPUT) {
    const ModuleRecord* rec = registry_.find(main_input_source_addr_);
    return rec && rec->online && (rec->caps & CAP_INPUT_KEYS) && main_input_source_bit_ < 2;
  }
  if (main_input_source_type_ == INPUT_SRC_UNIVERSAL_ENTITY) {
    const ModuleRecord* rec = registry_.find(main_input_source_addr_);
    if (!rec || !rec->online || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) return false;
    if (!rec->universal_entities_valid) return true;
    for (uint8_t i = 0; i < rec->universal_entity_count; ++i) {
      if (rec->universal_entities[i].id == main_input_source_bit_) return true;
    }
    return false;
  }
  return false;
}

bool MasterScheduler::mainInputSourceActive() const {
  if (main_input_source_type_ == INPUT_SRC_IO_INPUT) {
    const ModuleRecord* rec = registry_.find(main_input_source_addr_);
    if (!rec || !rec->online || !(rec->caps & CAP_INPUT_KEYS)) return false;
    return (rec->io_input_mask & (uint16_t)(1U << main_input_source_bit_)) != 0;
  }
  if (main_input_source_type_ == INPUT_SRC_UNIVERSAL_ENTITY) {
    const ModuleRecord* rec = registry_.find(main_input_source_addr_);
    return rec && universalEntityBoolActive(*rec, main_input_source_bit_);
  }
  return false;
}
void MasterScheduler::setLogicExternalInput(bool active) {
  if (logic_external_input_ == active) return;
  logic_external_input_ = active;
  updateInputRouting();
}
void MasterScheduler::applyInputRuleTarget(InputActionRule& rule, bool active) {
  if (!rule.enabled) return;
  if (rule.target_type == INPUT_TGT_EXTRACTOR_ACTION) {
    if (!active) {
      rule.last_active = false;
      rule.edge_armed = true;
    } else if (rule.edge_armed && !rule.last_active) {
      rule.last_active = true;
      queueExtractorAction(rule.target_bit);
    }
    return;
  }
  if (rule.last_active == active) return;
  if (rule.target_type == INPUT_TGT_IO_OUTPUT) {
    ModuleRecord* rec = registry_.find(rule.target_addr);
    if (!rec || !rec->online || !(rec->caps & CAP_DIGITAL_OUTPUT)) return;
    const uint16_t mask = (uint16_t)(1U << rule.target_bit);
    if (setIoOutput(rule.target_addr, mask, active ? mask : 0)) rule.last_active = active;
    return;
  }
  if (rule.target_type == INPUT_TGT_UNIVERSAL_ENTITY) {
    const uint8_t v = active ? '1' : '0';
    if (setUniversalEntity(rule.target_addr, rule.target_bit, &v, 1)) rule.last_active = active;
  }
}

void MasterScheduler::queueExtractorAction(uint8_t action) {
  if (action == EXTRACTOR_ACTION_NONE || action > EXTRACTOR_ACTION_LAST) return;
  pending_extractor_actions_ |= (uint16_t)(1U << action);
}

void MasterScheduler::processPendingExtractorActions() {
  const uint16_t actions = pending_extractor_actions_;
  pending_extractor_actions_ = 0;
  if (!actions) return;

  JbcModuleState state;
  if (have_jbc_settings_) copyDesiredJbcSettings(state);
  else state = extractor_.jbcState();
  const uint8_t previous_level = state.suction_level;
  const uint16_t previous_flow = state.select_flow;

  auto has = [actions](uint8_t action) -> bool {
    return (actions & (uint16_t)(1U << action)) != 0;
  };

  bool changed = false;
  bool direct_level = false;
  // Explicit level selections take precedence over relative actions.
  if (has(EXTRACTOR_ACTION_LEVEL_HIGH)) {
    state.suction_level = 0;
    direct_level = true;
  } else if (has(EXTRACTOR_ACTION_LEVEL_MEDIUM)) {
    state.suction_level = 1;
    direct_level = true;
  } else if (has(EXTRACTOR_ACTION_LEVEL_LOW)) {
    state.suction_level = 2;
    direct_level = true;
  } else if (has(EXTRACTOR_ACTION_LEVEL_CUSTOM)) {
    state.suction_level = 3;
    direct_level = true;
  } else {
    const bool next = has(EXTRACTOR_ACTION_LEVEL_NEXT);
    const bool previous = has(EXTRACTOR_ACTION_LEVEL_PREVIOUS);
    if (next != previous) {
      // The numeric level order is High, Medium, Low, Custom. Moving to the
      // next stronger/user-facing level therefore decrements this index.
      state.suction_level = next
        ? (uint8_t)((state.suction_level + 3U) & 0x03U)
        : (uint8_t)((state.suction_level + 1U) & 0x03U);
      changed = true;
    }
  }

  const int16_t delta_pct =
    (has(EXTRACTOR_ACTION_POWER_PLUS_10) ? 10 : 0) -
    (has(EXTRACTOR_ACTION_POWER_MINUS_10) ? 10 : 0) +
    (has(EXTRACTOR_ACTION_POWER_PLUS_1) ? 1 : 0) -
    (has(EXTRACTOR_ACTION_POWER_MINUS_1) ? 1 : 0);
  if (delta_pct && !direct_level) {
    uint16_t min_flow = 100, max_flow = 1000, step_flow = 10;
    selectFlowBoundsForActiveOutput(min_flow, max_flow, step_flow);
    int32_t target = (int32_t)state.select_flow + (int32_t)delta_pct * 10;
    if (target < min_flow) target = min_flow;
    if (target > max_flow) target = max_flow;
    const int32_t offset = target - min_flow;
    target = min_flow + ((offset + (int32_t)step_flow / 2) / step_flow) * step_flow;
    if (target > max_flow) target = max_flow;
    state.suction_level = 3;
    state.select_flow = (uint16_t)target;
    changed = true;
  }

  if (state.suction_level == 3) {
    uint16_t min_flow = 100, max_flow = 1000, step_flow = 10;
    selectFlowBoundsForActiveOutput(min_flow, max_flow, step_flow);
    if (state.select_flow < min_flow) state.select_flow = min_flow;
    if (state.select_flow > max_flow) state.select_flow = max_flow;
  }
  changed = changed || state.suction_level != previous_level || state.select_flow != previous_flow;
  if (changed) applyJbcSettingsToOnlineModules(state);
}

void MasterScheduler::updateInputRouting() {
  if (applying_input_rules_) return;
  applying_input_rules_ = true;
  bool active = mainInputSourceActive() || logic_external_input_;

  for (uint8_t i = 0; i < MAX_INPUT_RULES; ++i) {
    InputActionRule& rule = input_rules_[i];
    const bool rule_active = inputRuleSourceActive(rule);
    if (rule.enabled && rule.target_type == INPUT_TGT_EXTRACTOR && rule_active) active = true;
    applyInputRuleTarget(rule, rule_active);
  }
  applying_input_rules_ = false;
  if (extractor_.updateExternalInput(active)) {
    Serial.print("Input routing trigger=");
    Serial.println(active ? "on" : "off");
  }
}

uint8_t MasterScheduler::onlineJbcModuleCount() const {
  uint8_t n = 0;
  const uint8_t count = registry_.count();
  for (uint8_t i = 0; i < count; ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (rec.online && (rec.caps & CAP_JBC_ACTIVITY)) ++n;
  }
  return n;
}

bool MasterScheduler::pollNextJbc() {
  const uint8_t count = registry_.count();

  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_jbc_poll_index_ >= count) next_jbc_poll_index_ = 0;
    ModuleRecord& rec = registry_.at(next_jbc_poll_index_++);
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    if (rec.online && (rec.caps & CAP_JBC_ACTIVITY)) return fastPollJbc(rec.addr);
  }

  // No known online JBC module: do not hammer the default address at the fast-poll cadence.
  // The fallback probe is only for first discovery / recovery and uses a slow backoff.
  if (module_fw_active_ && active_jbc_addr_ == module_fw_target_) return false;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_default_jbc_probe_ms_) < JBC_DEFAULT_PROBE_INTERVAL_MS) return false;
  last_default_jbc_probe_ms_ = now;
  return fastPollJbc(active_jbc_addr_);
}

bool MasterScheduler::readNextJbcState() {
  if (pending_jbc_state_addr_) {
    const uint8_t addr = pending_jbc_state_addr_;
    ModuleRecord* rec = registry_.find(addr);
    if (!rec || !rec->online || !(rec->caps & CAP_JBC_BUS)) {
      pending_jbc_state_addr_ = 0;
    } else if (!(module_fw_active_ && addr == module_fw_target_)) {
      if (readJbcState(addr)) pending_jbc_state_addr_ = 0;
      return true;
    }
  }

  const uint8_t count = registry_.count();

  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_jbc_state_index_ >= count) next_jbc_state_index_ = 0;
    ModuleRecord& rec = registry_.at(next_jbc_state_index_++);
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    if (rec.online && (rec.caps & CAP_JBC_BUS)) return readJbcState(rec.addr);
  }

  if (module_fw_active_ && active_jbc_addr_ == module_fw_target_) return false;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_default_jbc_state_probe_ms_) < JBC_DEFAULT_STATE_PROBE_INTERVAL_MS) return false;
  last_default_jbc_state_probe_ms_ = now;
  return readJbcState(active_jbc_addr_);
}

bool MasterScheduler::pollNextWeller() {
  const uint8_t count = registry_.count();
  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_weller_poll_index_ >= count) next_weller_poll_index_ = 0;
    ModuleRecord& rec = registry_.at(next_weller_poll_index_++);
    if (!rec.online || !(rec.caps & CAP_WELLER_INTERFACE)) continue;
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    // Weller IO/status remains on the 1 s device-specific slot. General
    // telemetry (including LED state) is handled by the fast telemetry rotor.
    readIoStatus(rec.addr);
    return true;
  }
  return false;
}

bool MasterScheduler::pollNextUniversal() {
  const uint8_t count = registry_.count();
  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_universal_poll_index_ >= count) next_universal_poll_index_ = 0;
    ModuleRecord& rec = registry_.at(next_universal_poll_index_++);
    if (!rec.online || (rec.type != MODULE_UNIVERSAL_RS232 && rec.type != MODULE_MODBUS_RTU)) continue;
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    bool did = false;
    if ((rec.caps & CAP_DESCRIPTOR) && !rec.universal_descriptor_valid) did = refreshUniversalDescriptor(rec.addr, true) || did;
    else if (rec.caps & CAP_DESCRIPTOR) did = refreshUniversalDescriptor(rec.addr, false) || did;
    if (rec.caps & (CAP_ENTITY_CONTROL | CAP_ENTITY_EVENTS)) did = readUniversalEntities(rec.addr) || did;
    return did;
  }
  return false;
}

bool MasterScheduler::pollNextTelemetry() {
  const uint8_t count = registry_.count();
  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_telemetry_index_ >= count) next_telemetry_index_ = 0;
    ModuleRecord& rec = registry_.at(next_telemetry_index_++);
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    if (!rec.online) continue;
    readTelemetry(rec.addr);
    return true;
  }
  return false;
}

bool MasterScheduler::pollNextIoStatus() {
  const uint8_t count = registry_.count();
  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_io_poll_index_ >= count) next_io_poll_index_ = 0;
    ModuleRecord& rec = registry_.at(next_io_poll_index_++);
    if (!rec.online || !(rec.caps & (CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT))) continue;
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    return readIoStatus(rec.addr);
  }
  return false;
}

bool MasterScheduler::pollNextOutputStatus() {
  const uint8_t count = registry_.count();
  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_output_status_index_ >= count) next_output_status_index_ = 0;
    ModuleRecord& rec = registry_.at(next_output_status_index_++);
    if (!rec.online) continue;
    const bool native_output = (rec.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT)) != 0;
    const bool community_output =
      (rec.type == MODULE_UNIVERSAL_RS232 || rec.type == MODULE_MODBUS_RTU) &&
      moduleProvidesExtractorOutput(rec);
    if (!native_output && !community_output) continue;
    if (module_fw_active_ && rec.addr == module_fw_target_) continue;
    const bool ok = readOutputStatus(rec.addr);
    if (ok && native_output && (rec.caps & (CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT))) readIoStatus(rec.addr);
    return ok;
  }
  return false;
}
bool MasterScheduler::readOutputStatus(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_GET_STATUS, nullptr, 0, resp, 35)) return false;
  if (resp.cmd != (CMD_GET_STATUS | 0x80) || resp.len < 8 || resp.payload[0] != STATUS_OK) return false;

  OutputModuleState state;
  state.enabled = resp.payload[1] != 0;
  state.power = get_u16_le(resp.payload + 2);
  state.rpm = get_u16_le(resp.payload + 4);
  state.fault_mask = get_u16_le(resp.payload + 6);
  if (addr == active_output_addr_) extractor_.updateOutputState(state);
  ModuleRecord* rec = registry_.find(addr);
  if (rec) {
    rec->output_status_valid = true;
    rec->output_enabled = state.enabled;
    rec->output_power = state.power;
    rec->output_rpm = state.rpm;
    rec->output_fault_mask = state.fault_mask;

    // Match native output-module behaviour: reading CMD_GET_STATUS must only
    // update the observed module state. A direct command from the module card
    // (including a WO command shadow) is allowed to remain active. The master
    // overwrites the output only when ExtractorLogic itself changes its target
    // and marks the output dirty (WORK/continuous/afterrun/manual extractor
    // control). Do not continuously reconcile community outputs here.

    if ((rec->caps & CAP_FILTER_SENSOR) && resp.len >= 12) {
      rec->fanio_filter_saturation_permille = get_u16_le(resp.payload + 8);
      rec->fanio_filter_pressure_raw = (int16_t)get_u16_le(resp.payload + 10);
    }
  }
  return true;
}

bool MasterScheduler::readIoStatus(uint8_t addr, bool include_aliases) {
  const uint8_t query_flags = include_aliases ? IO_QUERY_INCLUDE_ALIASES : 0;
  Frame resp;
  if (!request(addr, CMD_GET_IO, &query_flags, 1, resp, 50)) return false;
  if (resp.cmd != (CMD_GET_IO | 0x80) || resp.len < 7 || resp.payload[0] != STATUS_OK) return false;

  if (include_aliases) ++full_io_poll_total_;
  else ++compact_io_poll_total_;

  ModuleRecord* rec = registry_.find(addr);
  if (!rec) return false;

  const uint16_t old_input_mask = rec->io_input_mask;
  rec->io_input_mask = get_u16_le(resp.payload + 1);
  rec->io_output_mask = get_u16_le(resp.payload + 3);
  rec->io_fault_mask = get_u16_le(resp.payload + 5);

  // Alias strings are static configuration. A compact live reply deliberately
  // stops at byte 7; in that case preserve the already cached aliases.
  if (resp.len > 7) {
    char* aliases[5] = {
      rec->io_in1_alias,
      rec->io_in2_alias,
      rec->io_out1_alias,
      rec->io_out2_alias,
      rec->io_main_alias
    };
    for (uint8_t i = 0; i < 5; ++i) aliases[i][0] = 0;

    uint8_t o = 7;
    for (uint8_t i = 0; i < 5; ++i) {
      if (o >= resp.len) break;
      uint8_t n = resp.payload[o++];
      if (n > 18) n = 18;
      if (o + n > resp.len) break;
      memcpy(aliases[i], resp.payload + o, n);
      aliases[i][n] = 0;
      o += n;
    }
  }

  // Input routing only depends on input state, not on output/fault bits or
  // aliases. Avoid walking/applying every routing rule on unchanged 250 ms IO
  // polls. Scan/online/config paths still call updateInputRouting explicitly.
  if (rec->io_input_mask != old_input_mask) updateInputRouting();
  return true;
}

bool MasterScheduler::readTelemetry(uint8_t addr) {
  Frame resp;
  if (!request(addr, CMD_GET_TELEMETRY, nullptr, 0, resp, 50)) return false;
  if (resp.cmd != (CMD_GET_TELEMETRY | 0x80) || resp.len < 13 || resp.payload[0] != STATUS_OK) return false;

  ModuleRecord* rec = registry_.find(addr);
  if (!rec) return false;
  if (resp.payload[1] == MODULE_WELLER_ZERO_SMOG) {
    rec->weller_speed_percent = resp.payload[2];
    rec->weller_filter_status = resp.payload[3];
    rec->weller_filter_runtime_minutes = get_u16_le(resp.payload + 4);
    rec->weller_programmed_filter_minutes = get_u16_le(resp.payload + 6);
    rec->weller_uart_age_sec = get_u16_le(resp.payload + 8);
    rec->weller_version = get_u16_le(resp.payload + 10);
    rec->weller_work_light = resp.payload[12];
    if (resp.len >= 15) rec->weller_fan_rpm = get_u16_le(resp.payload + 13);
    if (resp.len >= 30) {
      rec->telemetry_valid = true;
      rec->module_heap_free = get_u32_le(resp.payload + 15);
      rec->module_heap_min = get_u32_le(resp.payload + 19);
      rec->module_uptime_s = get_u32_le(resp.payload + 23);
      rec->module_cpu_load_pct = resp.payload[27];
      rec->module_loop_max_ms = get_u16_le(resp.payload + 28);
    }
  } else if (resp.payload[1] == MODULE_JBC_USB && resp.len >= 37) {
    // JbcUsbModule extended telemetry:
    // heap/min/uptime, USB RX/TX bytes, JBC RX/TX frames and protocol errors.
    rec->telemetry_valid = true;
    rec->module_heap_free = get_u32_le(resp.payload + 2);
    rec->module_heap_min = get_u32_le(resp.payload + 6);
    rec->module_uptime_s = get_u32_le(resp.payload + 10);
    rec->module_cpu_load_pct = 0;
    rec->module_loop_max_ms = 0;
    rec->jbc_usb_usb_rx_bytes = get_u32_le(resp.payload + 14);
    rec->jbc_usb_usb_tx_bytes = get_u32_le(resp.payload + 18);
    rec->jbc_usb_rx_frames = get_u32_le(resp.payload + 22);
    rec->jbc_usb_tx_frames = get_u32_le(resp.payload + 26);
    rec->jbc_usb_usb_errors = get_u16_le(resp.payload + 30);
    rec->jbc_usb_bcc_errors = get_u16_le(resp.payload + 32);
    rec->jbc_usb_frame_errors = get_u16_le(resp.payload + 34);
    const bool split_jbc_diag_supported =
      rec->fw_major > 1 ||
      (rec->fw_major == 1 && (rec->fw_minor > 1 || (rec->fw_minor == 1 && rec->fw_patch >= 55)));
    if (!split_jbc_diag_supported) {
      rec->jbc_usb_decode_errors = 0;
      rec->jbc_usb_handshake_errors = 0;
    }
    if (resp.len >= 63) {
      rec->jbc_usb_cp_baud = get_u32_le(resp.payload + 39);
      rec->jbc_usb_cp_line_ctl = get_u16_le(resp.payload + 43);
      rec->jbc_usb_cp_mdmsts = resp.payload[45];
      rec->jbc_usb_cp_comm_errors = get_u32_le(resp.payload + 46);
      rec->jbc_usb_cp_hold_reasons = get_u32_le(resp.payload + 50);
      rec->jbc_usb_cp_in_queue = get_u32_le(resp.payload + 54);
      rec->jbc_usb_cp_out_queue = get_u32_le(resp.payload + 58);
      rec->jbc_usb_cp_diag_valid = resp.payload[62] != 0;
      // JbcUsbModule 0.1.9+: byte 63 is the raw JBC Device-ID/UUID length.
      // Keep compatibility with older module firmware that ends at byte 62.
      rec->jbc_usb_device_id_len = 0;
      memset(rec->jbc_usb_device_id, 0, sizeof(rec->jbc_usb_device_id));
      if (resp.len >= 64) {
        const uint8_t uid_len = min(resp.payload[63], (uint8_t)sizeof(rec->jbc_usb_device_id));
        const size_t uid_end = (size_t)64 + uid_len;
        if (uid_end <= resp.len) {
          rec->jbc_usb_device_id_len = uid_len;
          if (uid_len) memcpy(rec->jbc_usb_device_id, resp.payload + 64, uid_len);
          // JbcUsbModule 0.1.10+: standard OFE CPU + loop-max suffix.
          if (uid_end + 3 <= resp.len) {
            rec->module_cpu_load_pct = resp.payload[uid_end];
            rec->module_loop_max_ms = get_u16_le(resp.payload + uid_end + 1);
            // JbcUsbModule detail telemetry. 0.1.17 = SOLD/DDE, 0.1.18 = HA/JT/JTSE.
            size_t q = uid_end + 3;
            if (q + 3 <= resp.len && resp.payload[q] == 0xD7) {
              const uint8_t ext_ver = resp.payload[q + 1];
              const uint8_t ports = min(resp.payload[q + 2], (uint8_t)4);
              q += 3;
              auto get_u24 = [&](const uint8_t* p) -> uint32_t {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
              };
              // One USB host address can see different JBC stations over its
              // lifetime.  Clear the opposite station-family extensions as
              // soon as D7 identifies the new family so stale diagnostics do
              // not survive a hot swap or station power cycle.
              if (ext_ver == 5) {
                rec->jbc_usb_sold_readonly_flags = 0;
                rec->jbc_usb_sold_station_diag_flags = 0;
                rec->jbc_usb_sold_extra_station_flags = 0;
                rec->jbc_usb_ha_station_diag_flags = 0;
                rec->jbc_usb_ha_security_flags = 0;
                for (uint8_t si=1; si<4; ++si) rec->jbc_usb_ports[si].cl_flags = 0;
              } else if (ext_ver == 2) {
                rec->jbc_usb_sold_readonly_flags = 0;
                rec->jbc_usb_sold_remote_mode = false;
                rec->jbc_usb_sold_temp_unit = 0;
                rec->jbc_usb_sold_n2_mode = false;
                rec->jbc_usb_sold_help_text = false;
                rec->jbc_usb_sold_power_limit = 0;
                rec->jbc_usb_sold_beep = false;
                memset(rec->jbc_usb_sold_interface, 0, sizeof(rec->jbc_usb_sold_interface));
                rec->jbc_usb_sold_graph_temp_max = rec->jbc_usb_sold_graph_temp_min = rec->jbc_usb_sold_graph_temp_range = 0;
                rec->jbc_usb_sold_graph_power_max = rec->jbc_usb_sold_graph_power_min = 0;
                rec->jbc_usb_sold_autoclean = false; rec->jbc_usb_sold_autoclean_temp = rec->jbc_usb_sold_autoclean_seconds = 0;
                rec->jbc_usb_sold_ground_type = 0;
                memset(rec->jbc_usb_sold_station_interface, 0, sizeof(rec->jbc_usb_sold_station_interface));
                memset(rec->jbc_usb_sold_datetime, 0, sizeof(rec->jbc_usb_sold_datetime));
                memset(rec->jbc_usb_sold_ethernet, 0, sizeof(rec->jbc_usb_sold_ethernet));
                memset(rec->jbc_usb_sold_frontal, 0, sizeof(rec->jbc_usb_sold_frontal));
                for (uint8_t si = 0; si < 4; ++si) {
                  rec->jbc_usb_ports[si].sold_readonly_port_flags = 0;
                  rec->jbc_usb_ports[si].sold_feeder_flags = 0;
                }
              } else {
                for (uint8_t si=0; si<4; ++si) rec->jbc_usb_ports[si].cl_flags = 0;
                rec->jbc_usb_ha_security_flags = 0;
                rec->jbc_usb_ha_pin[0] = 0;
                rec->jbc_usb_ha_beep = false;
                rec->jbc_usb_ha_station_diag_flags = 0;
                for (uint8_t si = 0; si < 4; ++si) rec->jbc_usb_ports[si].ha_diag_flags = 0;
              }
              if (ext_ver == 5) {
                // JbcUsbModule 0.1.40+: CLM/CLMU one-port read-only state.
                // D7 header already consumed; record = port + flags + 2 states + 10*u32.
                if (ports >= 1 && q + 45 <= resp.len) {
                  const uint8_t pi = resp.payload[q++];
                  if (pi < 4) {
                    JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                    ps.cl_flags = get_u16_le(resp.payload + q); q += 2;
                    ps.cl_motors_on = resp.payload[q++] != 0;
                    ps.cl_door_open = resp.payload[q++] != 0;
                    ps.cl_counter_plug_min = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_counter_cleaning_continuous_min = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_counter_cleaning_detection_min = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_counter_work_cycles = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_counter_door_open_cycles = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_partial_plug_min = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_partial_cleaning_continuous_min = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_partial_cleaning_detection_min = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_partial_work_cycles = get_u32_le(resp.payload+q); q+=4;
                    ps.cl_partial_door_open_cycles = get_u32_le(resp.payload+q); q+=4;
                  } else q += 44;
                }
              } else if (ext_ver == 1) {
                for (uint8_t i = 0; i < ports && q + 22 <= resp.len; ++i) {
                  JbcUsbPortState& ps = rec->jbc_usb_ports[i];
                  ps.detail_value_flags = resp.payload[q++];
                  ps.selected_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.sleep_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.adjust_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                  ps.counter_plug_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_work_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_sleep_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_hiber_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_idle_min = get_u24(resp.payload + q); q += 3;
                }
              } else if (ext_ver == 3 || ext_ver == 4) {
                // v3 (0.1.19): levels/cycles. v4 (0.1.20): same indexed
                // record plus read-only cartridge metadata and A/B diagnostics.
                // 0.1.23 deliberately keeps v4 and appends a 2-byte optional
                // station-QST suffix so older Masters still parse the 60-byte record.
                const size_t sold_record_size = ext_ver == 4 ? 60U : 39U;
                for (uint8_t r = 0; r < ports && q + sold_record_size <= resp.len; ++r) {
                  const uint8_t pi = resp.payload[q++];
                  if (pi >= 4) { q += sold_record_size - 1U; continue; }
                  JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                  ps.detail_value_flags = get_u16_le(resp.payload + q); q += 2;
                  ps.selected_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.sleep_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.adjust_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                  ps.counter_plug_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_work_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_sleep_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_hiber_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_idle_min = get_u24(resp.payload + q); q += 3;
                  ps.counter_sleep_cycles = get_u24(resp.payload + q); q += 3;
                  ps.counter_desold_cycles = get_u24(resp.payload + q); q += 3;
                  ps.levels_on = resp.payload[q++];
                  ps.selected_level = resp.payload[q++];
                  const uint8_t lom = resp.payload[q++];
                  for (uint8_t lv = 0; lv < 3; ++lv) ps.level_on[lv] = (lom & (1U << lv)) ? 1 : 0;
                  for (uint8_t lv = 0; lv < 3; ++lv) { ps.level_temp[lv] = get_u16_le(resp.payload + q); q += 2; }
                  if (ext_ver == 4) {
                    ps.cartridge_on = resp.payload[q++];
                    ps.cartridge_jbc_code = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_adjust_300 = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_adjust_400 = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_group = resp.payload[q++];
                    ps.cartridge_family = resp.payload[q++];
                    ps.tip_temp_a = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.tip_temp_b = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_ma_a = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_ma_b = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_power_permille_a = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.cartridge_power_permille_b = (int16_t)get_u16_le(resp.payload + q); q += 2;
                  }
                }
                if (ext_ver == 4) {
                  if (q + 2 <= resp.len) {
                    rec->jbc_usb_qst_valid_flags = resp.payload[q++];
                    rec->jbc_usb_qst_state_flags = resp.payload[q++];
                  } else {
                    rec->jbc_usb_qst_valid_flags = 0;
                    rec->jbc_usb_qst_state_flags = 0;
                  }
                }
                // JbcUsbModule 0.1.26+: optional D8/v1 SOLD diagnostic suffix.
                // It follows the legacy v4+QST block so 1.8.36 and older can ignore it.
                if (ext_ver == 4 && q + 24 <= resp.len && resp.payload[q] == 0xD8 && resp.payload[q + 1] == 1) {
                  q += 2;
                  const uint8_t pi = resp.payload[q++];
                  if (pi < 4) {
                    JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                    ps.sold_diag_flags = resp.payload[q++];
                    ps.sold_mos_temp = get_u16_le(resp.payload + q); q += 2;
                    ps.sold_tool_type = resp.payload[q++];
                    ps.sold_tool_last_error = resp.payload[q++];
                    ps.sold_alarm_max_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.sold_alarm_max_delay_tenth_sec = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.sold_alarm_min_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                    ps.sold_alarm_min_delay_tenth_sec = (int16_t)get_u16_le(resp.payload + q); q += 2;
                  } else {
                    q += 13;
                  }
                  rec->jbc_usb_sold_station_diag_flags = resp.payload[q++];
                  rec->jbc_usb_sold_trafo_temp = get_u16_le(resp.payload + q); q += 2;
                  rec->jbc_usb_sold_trafo_error_temp = get_u16_le(resp.payload + q); q += 2;
                  rec->jbc_usb_sold_mos_error_temp = get_u16_le(resp.payload + q); q += 2;
                  rec->jbc_usb_sold_control_mode = resp.payload[q++] != 0;
                }
              } else if (ext_ver == 2) {
                // HA does not expose the SOLD QST feature. A different station
                // can be attached to the same JBC USB module address, so clear
                // stale SOLD/QST station state as soon as HA D7 is received.
                rec->jbc_usb_qst_valid_flags = 0;
                rec->jbc_usb_qst_state_flags = 0;
                rec->jbc_usb_sold_station_diag_flags = 0;
                rec->jbc_usb_sold_control_mode = false;
                rec->jbc_usb_sold_extra_station_flags = 0;
                rec->jbc_usb_sold_readonly_flags = 0;
                rec->jbc_usb_sold_pin[0] = 0;
                rec->jbc_usb_sold_peripheral_count = 0;
                rec->jbc_usb_sold_peripheral_transmitted = 0;
                for (uint8_t si = 0; si < 4; ++si) {
                  rec->jbc_usb_ports[si].sold_extra_flags = 0;
                  rec->jbc_usb_ports[si].sold_readonly_port_flags = 0; rec->jbc_usb_ports[si].sold_feeder_flags = 0;
                  rec->jbc_usb_sold_peripherals[si] = JbcUsbSoldPeripheralState();
                }
                for (uint8_t i = 0; i < ports && q + 54 <= resp.len; ++i) {
                  JbcUsbPortState& ps = rec->jbc_usb_ports[i];
                  ps.ha_value_flags = get_u16_le(resp.payload + q); q += 2;
                  ps.protection_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.selected_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.selected_flow_permille = get_u16_le(resp.payload + q); q += 2;
                  ps.selected_ext_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.actual_ext_temp = get_u16_le(resp.payload + q); q += 2;
                  ps.ha_adjust_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                  ps.configured_time_to_stop = get_u16_le(resp.payload + q); q += 2;
                  ps.external_tc_mode = resp.payload[q++];
                  ps.start_mode = resp.payload[q++];
                  ps.profile_mode = resp.payload[q++];
                  ps.levels_on = resp.payload[q++];
                  ps.selected_level = resp.payload[q++];
                  for (uint8_t lv = 0; lv < 3; ++lv) {
                    ps.level_on[lv] = resp.payload[q++];
                    ps.level_temp[lv] = get_u16_le(resp.payload + q); q += 2;
                    ps.level_flow_permille[lv] = get_u16_le(resp.payload + q); q += 2;
                    ps.level_ext_temp[lv] = get_u16_le(resp.payload + q); q += 2;
                  }
                  ps.ha_counter_plug_min = get_u24(resp.payload + q); q += 3;
                  ps.ha_counter_work_min = get_u24(resp.payload + q); q += 3;
                  ps.ha_counter_work_cycles = get_u24(resp.payload + q); q += 3;
                  ps.ha_counter_suction_cycles = get_u24(resp.payload + q); q += 3;
                }
              }
            }
            // JbcUsbModule 0.1.26 may send D8/v1 as a standalone alternating
            // telemetry extension (D7 port detail on one request, D8 DLL diagnostics
            // for the same port on the next). Parse it here as well as the combined
            // form accepted above.
            if (q + 24 <= resp.len && resp.payload[q] == 0xD8 && resp.payload[q + 1] == 1) {
              q += 2;
              const uint8_t pi = resp.payload[q++];
              if (pi < 4) {
                JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                ps.sold_diag_flags = resp.payload[q++];
                ps.sold_mos_temp = get_u16_le(resp.payload + q); q += 2;
                ps.sold_tool_type = resp.payload[q++];
                ps.sold_tool_last_error = resp.payload[q++];
                ps.sold_alarm_max_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                ps.sold_alarm_max_delay_tenth_sec = (int16_t)get_u16_le(resp.payload + q); q += 2;
                ps.sold_alarm_min_temp = (int16_t)get_u16_le(resp.payload + q); q += 2;
                ps.sold_alarm_min_delay_tenth_sec = (int16_t)get_u16_le(resp.payload + q); q += 2;
              } else {
                q += 13;
              }
              rec->jbc_usb_sold_station_diag_flags = resp.payload[q++];
              rec->jbc_usb_sold_trafo_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_sold_trafo_error_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_sold_mos_error_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_sold_control_mode = resp.payload[q++] != 0;
            }
            // JbcUsbModule 0.1.33+: alternating DA/v1 SOLD completion record.
            // It carries partial counters and model-gated profile/assistant data
            // without changing the legacy D7/D8 layouts.
            if (q + 33 <= resp.len && resp.payload[q] == 0xDA && resp.payload[q + 1] == 1) {
              auto get_u24_sold = [&](const uint8_t* p) -> uint32_t {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
              };
              q += 2;
              const uint8_t pi = resp.payload[q++];
              const uint16_t flags = get_u16_le(resp.payload + q); q += 2;
              const uint32_t partial_plug = get_u24_sold(resp.payload + q); q += 3;
              const uint32_t partial_work = get_u24_sold(resp.payload + q); q += 3;
              const uint32_t partial_sleep = get_u24_sold(resp.payload + q); q += 3;
              const uint32_t partial_hiber = get_u24_sold(resp.payload + q); q += 3;
              const uint32_t partial_idle = get_u24_sold(resp.payload + q); q += 3;
              const uint32_t partial_sleep_cycles = get_u24_sold(resp.payload + q); q += 3;
              const uint32_t partial_desold_cycles = get_u24_sold(resp.payload + q); q += 3;
              const uint8_t profile_mode = resp.payload[q++];
              const bool assistant_on = resp.payload[q++] != 0;
              const int16_t assistant_warning = (int16_t)get_u16_le(resp.payload + q); q += 2;
              const int16_t assistant_error = (int16_t)get_u16_le(resp.payload + q); q += 2;
              const uint8_t profile_raw_len = resp.payload[q++];
              if (q + profile_raw_len <= resp.len) {
                if (pi < 4) {
                  JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                  ps.sold_extra_flags = flags;
                  ps.sold_partial_plug_min = partial_plug;
                  ps.sold_partial_work_min = partial_work;
                  ps.sold_partial_sleep_min = partial_sleep;
                  ps.sold_partial_hiber_min = partial_hiber;
                  ps.sold_partial_idle_min = partial_idle;
                  ps.sold_partial_sleep_cycles = partial_sleep_cycles;
                  ps.sold_partial_desold_cycles = partial_desold_cycles;
                  ps.sold_profile_mode = profile_mode;
                  ps.sold_assistant_on = assistant_on;
                  ps.sold_assistant_warning = assistant_warning;
                  ps.sold_assistant_error = assistant_error;
                  const uint8_t profile_len = min(profile_raw_len, (uint8_t)12);
                  memset(ps.sold_selected_profile, 0, sizeof(ps.sold_selected_profile));
                  if (profile_len) memcpy(ps.sold_selected_profile, resp.payload + q, profile_len);
                  ps.sold_selected_profile[profile_len] = 0;
                }
                q += profile_raw_len;
              }
            }
            // JbcUsbModule 0.1.33+: DB/v1 SOLD station completion record.
            // PIN bytes are retained in ModuleRecord but WebStatus exposes them
            // only while the Master developer mode is enabled.
            if (q + 21 <= resp.len && resp.payload[q] == 0xDB && resp.payload[q + 1] == 1) {
              q += 2;
              rec->jbc_usb_sold_extra_station_flags = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_sold_min_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_sold_max_temp = get_u16_le(resp.payload + q); q += 2;
              memcpy(rec->jbc_usb_sold_pin, resp.payload + q, 4); q += 4;
              rec->jbc_usb_sold_pin[4] = 0;
              memcpy(rec->jbc_usb_sold_robot_config, resp.payload + q, 7); q += 7;
              rec->jbc_usb_sold_peripheral_count = resp.payload[q++];
              const uint8_t transmitted_raw = resp.payload[q++];
              const uint8_t transmitted = min(transmitted_raw, (uint8_t)4);
              rec->jbc_usb_sold_peripheral_transmitted = transmitted;
              if (q + (size_t)transmitted_raw * 8U <= resp.len) {
                for (uint8_t id = 0; id < transmitted; ++id) {
                  JbcUsbSoldPeripheralState& sp = rec->jbc_usb_sold_peripherals[id];
                  sp.flags = resp.payload[q++];
                  sp.version = resp.payload[q++];
                  sp.type = resp.payload[q++];
                  sp.port = resp.payload[q++];
                  sp.function = resp.payload[q++];
                  sp.activation = resp.payload[q++];
                  sp.delay = resp.payload[q++];
                  sp.pd_status = resp.payload[q++];
                }
                q += (size_t)(transmitted_raw - transmitted) * 8U;
                for (uint8_t id = transmitted; id < 4; ++id) rec->jbc_usb_sold_peripherals[id] = JbcUsbSoldPeripheralState();
              }
            }
            // JbcUsbModule 0.1.36+: D3/v1 completes CPeripheralData with the
            // 4-char Hash_MCU_UID and 14-char DateTime fields from 0xFA.
            if (q + 3 <= resp.len && resp.payload[q] == 0xD3 && resp.payload[q + 1] == 1) {
              q += 2;
              const uint8_t count_raw = resp.payload[q++];
              const uint8_t count = min(count_raw, (uint8_t)4);
              if (q + (size_t)count_raw * 18U <= resp.len) {
                for (uint8_t id = 0; id < count; ++id) {
                  JbcUsbSoldPeripheralState& sp = rec->jbc_usb_sold_peripherals[id];
                  memcpy(sp.hash_mcu_uid, resp.payload + q, 4); q += 4; sp.hash_mcu_uid[4] = 0;
                  memcpy(sp.datetime, resp.payload + q, 14); q += 14; sp.datetime[14] = 0;
                }
                q += (size_t)(count_raw - count) * 18U;
              }
            }
            // JbcUsbModule 0.1.34+: DD/v1 per-port safe read-only SOLD extras.
            if (q + 17 <= resp.len && resp.payload[q] == 0xDD && resp.payload[q + 1] == 1) {
              q += 2; const uint8_t pi=resp.payload[q++];
              if(pi<4){JbcUsbPortState& ps=rec->jbc_usb_ports[pi];ps.sold_readonly_port_flags=get_u16_le(resp.payload+q);q+=2;ps.sold_fixed_temp=get_u16_le(resp.payload+q);q+=2;ps.sold_fixed_temp_on=resp.payload[q++]!=0;ps.sold_assistant_warning_code=resp.payload[q++];ps.sold_result_similarity=(int16_t)get_u16_le(resp.payload+q);q+=2;ps.sold_result_tenths=(int16_t)get_u16_le(resp.payload+q);q+=2;ps.sold_result_energy=(int16_t)get_u16_le(resp.payload+q);q+=2;ps.sold_direct_power_permille=get_u16_le(resp.payload+q);q+=2;}
              else q += 14;
            }
            // DC/v1 carries scalar station/interface/configuration reads.
            if (q + 40 <= resp.len && resp.payload[q] == 0xDC && resp.payload[q + 1] == 1) {
              q+=2;rec->jbc_usb_sold_readonly_flags=get_u32_le(resp.payload+q);q+=4;rec->jbc_usb_sold_remote_mode=resp.payload[q++]!=0;rec->jbc_usb_sold_temp_unit=resp.payload[q++];rec->jbc_usb_sold_n2_mode=resp.payload[q++]!=0;rec->jbc_usb_sold_help_text=resp.payload[q++]!=0;rec->jbc_usb_sold_power_limit=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_beep=resp.payload[q++]!=0;memcpy(rec->jbc_usb_sold_interface,resp.payload+q,7);q+=7;rec->jbc_usb_sold_graph_temp_max=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_graph_temp_min=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_graph_temp_range=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_graph_power_max=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_graph_power_min=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_autoclean=resp.payload[q++]!=0;rec->jbc_usb_sold_autoclean_temp=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_autoclean_seconds=get_u16_le(resp.payload+q);q+=2;rec->jbc_usb_sold_ground_type=resp.payload[q++];memcpy(rec->jbc_usb_sold_station_interface,resp.payload+q,4);q+=4;
            }
            // DF/v1 carries larger/rare station records (date/time, Ethernet,
            // frontal interface text) separately to stay below RS485 MAX_PAYLOAD.
            if (q + 35 <= resp.len && resp.payload[q] == 0xDF && resp.payload[q + 1] == 1) {
              q+=2;q+=2;memcpy(rec->jbc_usb_sold_datetime,resp.payload+q,7);q+=7;memcpy(rec->jbc_usb_sold_ethernet,resp.payload+q,23);q+=23;const uint8_t raw_n=resp.payload[q++];if(q+raw_n<=resp.len){const uint8_t n=min(raw_n,(uint8_t)20);memset(rec->jbc_usb_sold_frontal,0,sizeof(rec->jbc_usb_sold_frontal));if(n)memcpy(rec->jbc_usb_sold_frontal,resp.payload+q,n);rec->jbc_usb_sold_frontal[n]=0;q+=raw_n;}
            }
            // JbcUsbModule 0.1.34+: D5/v1 ALE Tin Feeder read-only config/programs + live motor state.
            if (q + 79 <= resp.len && resp.payload[q] == 0xD5 && resp.payload[q + 1] == 1) {
              q += 2; const uint8_t pi = resp.payload[q++];
              if (pi < 4) {
                JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                ps.sold_feeder_flags = get_u16_le(resp.payload + q); q += 2;
                ps.sold_feeder_working_mode = resp.payload[q++];
                ps.sold_feeder_selected_program = resp.payload[q++];
                ps.sold_feeder_delivery_length = get_u16_le(resp.payload + q); q += 2;
                ps.sold_feeder_delivery_speed = get_u16_le(resp.payload + q); q += 2;
                ps.sold_feeder_tin_diameter = resp.payload[q++];
                ps.sold_feeder_remove_length = resp.payload[q++];
                ps.sold_feeder_speed_length_readonly = resp.payload[q++] != 0;
                ps.sold_feeder_selectable_programs = get_u16_le(resp.payload + q); q += 2;
                ps.sold_feeder_clogging_detection = resp.payload[q++] != 0;
                ps.sold_feeder_motor_on = resp.payload[q++] != 0;
                ps.sold_feeder_motor_direction = resp.payload[q++];
                for (uint8_t pg=0; pg<5; ++pg) for (uint8_t st=0; st<3; ++st) { ps.sold_feeder_program_length[pg][st]=get_u16_le(resp.payload+q); q+=2; }
                for (uint8_t pg=0; pg<5; ++pg) for (uint8_t st=0; st<3; ++st) { ps.sold_feeder_program_speed[pg][st]=get_u16_le(resp.payload+q); q+=2; }
              } else q += 76;
            }
            // JbcUsbModule 0.1.34+: D4/v1 unique grouped k26 counter extensions.
            if (q + 61 <= resp.len && resp.payload[q] == 0xD4 && resp.payload[q + 1] == 1) {
              q += 2; const uint8_t pi=resp.payload[q++];
              if(pi<4){JbcUsbPortState& ps=rec->jbc_usb_ports[pi];ps.sold_special_counter_flags=get_u16_le(resp.payload+q);q+=2;ps.sold_tin_deliver_cycles=get_u32_le(resp.payload+q);q+=4;ps.sold_tin_length=get_u32_le(resp.payload+q);q+=4;ps.sold_partial_tin_deliver_cycles=get_u32_le(resp.payload+q);q+=4;ps.sold_partial_tin_length=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_sold_number=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_energy_delivered=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_sold_total=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_sold_per_min=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_sold_ok=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_partial_sold_number=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_partial_energy_delivered=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_partial_sold_total=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_partial_sold_per_min=get_u32_le(resp.payload+q);q+=4;ps.sold_cde_partial_sold_ok=get_u32_le(resp.payload+q);q+=4;}
              else q += 58;
            }
            // JbcUsbModule 0.1.29+: alternating D9/v1 HA diagnostics.
            // D7/v2 remains unchanged; D9 carries redundant direct JBC reads,
            // partial counters and station-level HA diagnostics/configuration.
            if (q + 53 <= resp.len && resp.payload[q] == 0xD9 && resp.payload[q + 1] == 1) {
              auto get_u24_ha = [&](const uint8_t* p) -> uint32_t {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
              };
              q += 2;
              const uint8_t pi = resp.payload[q++];
              if (pi < 4) {
                JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                ps.ha_diag_flags = get_u16_le(resp.payload + q); q += 2;
                ps.ha_diag_air_temp = get_u16_le(resp.payload + q); q += 2;
                ps.ha_diag_power_permille = get_u16_le(resp.payload + q); q += 2;
                ps.ha_diag_flow_permille = get_u16_le(resp.payload + q); q += 2;
                ps.ha_diag_tool = resp.payload[q++];
                ps.ha_diag_error = resp.payload[q++];
                ps.ha_diag_status = resp.payload[q++];
                ps.ha_diag_heater_state = resp.payload[q++];
                ps.ha_diag_suction_state = resp.payload[q++];
                ps.ha_partial_plug_min = get_u24_ha(resp.payload + q); q += 3;
                ps.ha_partial_work_min = get_u24_ha(resp.payload + q); q += 3;
                ps.ha_partial_work_cycles = get_u24_ha(resp.payload + q); q += 3;
                ps.ha_partial_suction_cycles = get_u24_ha(resp.payload + q); q += 3;
              } else {
                q += 25;
              }
              rec->jbc_usb_ha_station_diag_flags = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ha_remote_mode = resp.payload[q++] != 0;
              rec->jbc_usb_ha_temp_unit = resp.payload[q++];
              rec->jbc_usb_ha_max_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ha_min_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ha_max_flow = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ha_min_flow = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ha_max_ext_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ha_min_ext_temp = get_u16_le(resp.payload + q); q += 2;
              memcpy(rec->jbc_usb_ha_robot_config, resp.payload + q, 7); q += 7;
              rec->jbc_usb_ha_robot_status = resp.payload[q++] != 0;
              const uint8_t profile_raw_len = resp.payload[q++];
              if (q + profile_raw_len <= resp.len) {
                const uint8_t profile_len = min(profile_raw_len, (uint8_t)12);
                memset(rec->jbc_usb_ha_selected_profile, 0, sizeof(rec->jbc_usb_ha_selected_profile));
                if (profile_len) memcpy(rec->jbc_usb_ha_selected_profile, resp.payload + q, profile_len);
                rec->jbc_usb_ha_selected_profile[profile_len] = 0;
                q += profile_raw_len;
              }
            }
            // JbcUsbModule 0.1.34+: DE/v1 HA PIN/Beep completion.
            if (q + 8 <= resp.len && resp.payload[q] == 0xDE && resp.payload[q + 1] == 1) {
              q+=2;rec->jbc_usb_ha_security_flags=resp.payload[q++];memcpy(rec->jbc_usb_ha_pin,resp.payload+q,4);q+=4;rec->jbc_usb_ha_pin[4]=0;rec->jbc_usb_ha_beep=resp.payload[q++]!=0;
            }
            // JbcUsbModule 0.1.43+: PH/Preheater complete UpdateData_PH read-only records.
            // E4 = station/TC scalar state, E5 = one port + global/partial counters,
            // E6/E7 = indexed Profile/ProfileTeach chunks accumulated over telemetry polls.
            if (q + 59 <= resp.len && resp.payload[q] == 0xE4 && resp.payload[q + 1] == 1) {
              q += 2;
              // Hot-swapping a PH onto the same USB bridge must not leave SOLD/HA/CL
              // family-specific detail visible in the Master.
              rec->jbc_usb_qst_valid_flags = 0; rec->jbc_usb_qst_state_flags = 0;
              rec->jbc_usb_sold_station_diag_flags = 0; rec->jbc_usb_sold_extra_station_flags = 0;
              rec->jbc_usb_sold_readonly_flags = 0; rec->jbc_usb_ha_station_diag_flags = 0;
              rec->jbc_usb_ha_security_flags = 0;
              for (uint8_t si=0; si<4; ++si) {
                rec->jbc_usb_ports[si].cl_flags = 0;
                rec->jbc_usb_ports[si].sold_extra_flags = 0;
                rec->jbc_usb_ports[si].sold_readonly_port_flags = 0;
                rec->jbc_usb_ports[si].sold_feeder_flags = 0;
                rec->jbc_usb_ports[si].ha_value_flags = 0;
                rec->jbc_usb_ports[si].ha_diag_flags = 0;
                rec->jbc_usb_ports[si].sf_flags = 0;
              }
              rec->jbc_usb_sf_station_flags = 0; rec->jbc_usb_sf_conti_valid=false;
              rec->jbc_usb_fe_service_flags=0; rec->jbc_usb_ph_remote_valid=false; rec->jbc_usb_ph_conti_valid=false;
              rec->jbc_usb_ph_station_flags = get_u32_le(resp.payload + q); q += 4;
              rec->jbc_usb_ph_max_power = (int16_t)get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ph_min_power = (int16_t)get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ph_max_temp = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_ph_min_temp = get_u16_le(resp.payload + q); q += 2;
              memcpy(rec->jbc_usb_ph_pin, resp.payload + q, 4); q += 4; rec->jbc_usb_ph_pin[4] = 0;
              rec->jbc_usb_ph_beep = resp.payload[q++] != 0;
              memcpy(rec->jbc_usb_ph_robot_config, resp.payload + q, 7); q += 7;
              rec->jbc_usb_ph_profile_points_setting = resp.payload[q++];
              rec->jbc_usb_ph_profile_consignment = resp.payload[q++];
              rec->jbc_usb_ph_profile_tc_regulation = resp.payload[q++];
              rec->jbc_usb_ph_profile_teach_interval = (int16_t)get_u16_le(resp.payload + q); q += 2;
              for (uint8_t tc=0; tc<4; ++tc) {
                JbcUsbPhTcState& tcs = rec->jbc_usb_ph_tc[tc];
                tcs.flags = resp.payload[q++];
                tcs.actual_temp = get_u16_le(resp.payload + q); q += 2;
                tcs.warning = resp.payload[q++];
                tcs.mode = resp.payload[q++];
                tcs.selected_temp = get_u16_le(resp.payload + q); q += 2;
              }
            }
            if (q + 70 <= resp.len && resp.payload[q] == 0xE5 && resp.payload[q + 1] == 1) {
              q += 2; const uint8_t pi = resp.payload[q++];
              if (pi < 4) {
                JbcUsbPortState& ps = rec->jbc_usb_ports[pi];
                ps.ph_flags = get_u16_le(resp.payload + q); q += 2;
                ps.ph_work_mode = resp.payload[q++];
                ps.ph_heater_status = resp.payload[q++];
                ps.ph_configured_time_to_stop = get_u32_le(resp.payload + q); q += 4;
                ps.ph_selected_power = get_u16_le(resp.payload + q); q += 2;
                ps.ph_active_zones = resp.payload[q++];
                ps.ph_counter_plug_min = get_u32_le(resp.payload + q); q += 4;
                ps.ph_counter_work_min_power = get_u32_le(resp.payload + q); q += 4;
                ps.ph_counter_work_min_temp = get_u32_le(resp.payload + q); q += 4;
                ps.ph_counter_work_min_profile = get_u32_le(resp.payload + q); q += 4;
                ps.ph_counter_work_cycles_power = get_u32_le(resp.payload + q); q += 4;
                ps.ph_counter_work_cycles_temp = get_u32_le(resp.payload + q); q += 4;
                ps.ph_counter_work_cycles_profile = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_plug_min = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_work_min_power = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_work_min_temp = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_work_min_profile = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_work_cycles_power = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_work_cycles_temp = get_u32_le(resp.payload + q); q += 4;
                ps.ph_partial_work_cycles_profile = get_u32_le(resp.payload + q); q += 4;
              } else q += 67;
            }
            if (q + 5 <= resp.len && resp.payload[q] == 0xE6 && resp.payload[q + 1] == 1) {
              q += 2; const uint8_t total=resp.payload[q++], start=resp.payload[q++], count=resp.payload[q++];
              const size_t bytes=(size_t)count*4U;
              if (total <= 47 && start <= total && (uint16_t)start + count <= total && q + bytes <= resp.len) {
                rec->jbc_usb_ph_profile_count = total;
                for (uint8_t i=0; i<count; ++i) {
                  const uint8_t idx=(uint8_t)(start+i);
                  rec->jbc_usb_ph_profile_time[idx]=(int16_t)get_u16_le(resp.payload+q); q+=2;
                  rec->jbc_usb_ph_profile_value[idx]=(int16_t)get_u16_le(resp.payload+q); q+=2;
                }
              } else if (q + bytes <= resp.len) q += bytes;
            }
            if (q + 5 <= resp.len && resp.payload[q] == 0xE7 && resp.payload[q + 1] == 1) {
              q += 2; const uint8_t total=resp.payload[q++], start=resp.payload[q++], count=resp.payload[q++];
              const size_t bytes=(size_t)count*2U;
              if (total <= 94 && start <= total && (uint16_t)start + count <= total && q + bytes <= resp.len) {
                rec->jbc_usb_ph_teach_count = total;
                for (uint8_t i=0; i<count; ++i) {
                  const uint8_t idx=(uint8_t)(start+i);
                  rec->jbc_usb_ph_teach_value[idx]=(int16_t)get_u16_le(resp.payload+q); q+=2;
                }
              } else if (q + bytes <= resp.len) q += bytes;
            }
            // JbcUsbModule 0.1.44+: FE complete UpdateData_FE read-only records.
            // E8 = station scalar state, E9 = one FE port + global/partial grouped counters.
            if (q + 11 <= resp.len && resp.payload[q] == 0xE8 && resp.payload[q + 1] == 1) {
              q += 2;
              // Clear stale family-specific extension state when a FE is hot-swapped
              // onto the same physical USB bridge.
              rec->jbc_usb_qst_valid_flags=0; rec->jbc_usb_qst_state_flags=0;
              rec->jbc_usb_sold_station_diag_flags=0; rec->jbc_usb_sold_extra_station_flags=0;
              rec->jbc_usb_sold_readonly_flags=0; rec->jbc_usb_ha_station_diag_flags=0;
              rec->jbc_usb_ha_security_flags=0; rec->jbc_usb_ph_station_flags=0;
              for(uint8_t si=0;si<4;++si){
                rec->jbc_usb_ports[si].cl_flags=0; rec->jbc_usb_ports[si].sold_extra_flags=0;
                rec->jbc_usb_ports[si].sold_readonly_port_flags=0; rec->jbc_usb_ports[si].sold_feeder_flags=0;
                rec->jbc_usb_ports[si].ha_value_flags=0; rec->jbc_usb_ports[si].ha_diag_flags=0;
                rec->jbc_usb_ports[si].ph_flags=0; rec->jbc_usb_ports[si].sf_flags=0;
              }
              rec->jbc_usb_sf_station_flags=0; rec->jbc_usb_sf_conti_valid=false;
              rec->jbc_usb_ph_remote_valid=false;rec->jbc_usb_ph_conti_valid=false;
              rec->jbc_usb_fe_station_flags=get_u16_le(resp.payload+q);q+=2;
              memcpy(rec->jbc_usb_fe_robot_config,resp.payload+q,7);q+=7;
            }
            if (q + 51 <= resp.len && resp.payload[q] == 0xE9 && resp.payload[q + 1] == 1) {
              q+=2; const uint8_t pi=resp.payload[q++];
              if(pi<4){
                JbcUsbPortState& ps=rec->jbc_usb_ports[pi];
                ps.fe_flags=get_u16_le(resp.payload+q);q+=2;
                ps.fe_time_to_stop_work=get_u16_le(resp.payload+q);q+=2; ps.fe_time_to_stop_stand=get_u16_le(resp.payload+q);q+=2;
                ps.fe_pedal_action=resp.payload[q++]; ps.fe_pedal_mode=resp.payload[q++];
                ps.fe_counter_plug_min=get_u32_le(resp.payload+q);q+=4; ps.fe_counter_idle_min=get_u32_le(resp.payload+q);q+=4;
                ps.fe_counter_work_intake_min=get_u32_le(resp.payload+q);q+=4; ps.fe_counter_stand_intake_min=get_u32_le(resp.payload+q);q+=4;
                ps.fe_counter_work_cycles=get_u32_le(resp.payload+q);q+=4; ps.fe_partial_plug_min=get_u32_le(resp.payload+q);q+=4;
                ps.fe_partial_idle_min=get_u32_le(resp.payload+q);q+=4; ps.fe_partial_work_intake_min=get_u32_le(resp.payload+q);q+=4;
                ps.fe_partial_stand_intake_min=get_u32_le(resp.payload+q);q+=4; ps.fe_partial_work_cycles=get_u32_le(resp.payload+q);q+=4;
              } else q+=48;
            }
            // JbcUsbModule 0.1.45+: SF complete UpdateData_SF read-only records.
            // EA = station settings/list, EB = single-port live/tool/counters,
            // EC = chunked program table (35 programs, 21 bytes each).
            if (q + 51 <= resp.len && resp.payload[q] == 0xEA && resp.payload[q + 1] == 1) {
              q += 2;
              rec->jbc_usb_qst_valid_flags=0; rec->jbc_usb_qst_state_flags=0;
              rec->jbc_usb_sold_station_diag_flags=0; rec->jbc_usb_sold_extra_station_flags=0;
              rec->jbc_usb_sold_readonly_flags=0; rec->jbc_usb_ha_station_diag_flags=0;
              rec->jbc_usb_ha_security_flags=0; rec->jbc_usb_ph_station_flags=0; rec->jbc_usb_fe_station_flags=0;
              rec->jbc_usb_fe_service_flags=0;rec->jbc_usb_ph_remote_valid=false;rec->jbc_usb_ph_conti_valid=false;
              for(uint8_t si=0;si<4;++si){
                rec->jbc_usb_ports[si].cl_flags=0; rec->jbc_usb_ports[si].sold_extra_flags=0;
                rec->jbc_usb_ports[si].sold_readonly_port_flags=0; rec->jbc_usb_ports[si].sold_feeder_flags=0;
                rec->jbc_usb_ports[si].ha_value_flags=0; rec->jbc_usb_ports[si].ha_diag_flags=0;
                rec->jbc_usb_ports[si].ph_flags=0; rec->jbc_usb_ports[si].fe_flags=0;
                if(si>0) rec->jbc_usb_ports[si].sf_flags=0;
              }
              rec->jbc_usb_sf_station_flags=get_u16_le(resp.payload+q);q+=2;
              memcpy(rec->jbc_usb_sf_pin,resp.payload+q,4);q+=4;rec->jbc_usb_sf_pin[4]=0;
              rec->jbc_usb_sf_length_unit=resp.payload[q++];
              memcpy(rec->jbc_usb_sf_robot_config,resp.payload+q,7);q+=7;
              memcpy(rec->jbc_usb_sf_program_list,resp.payload+q,35);q+=35;
            }
            if (q + 62 <= resp.len && resp.payload[q] == 0xEB && resp.payload[q + 1] == 1) {
              q+=2; const uint8_t pi=resp.payload[q++];
              if(pi<4){
                JbcUsbPortState& ps=rec->jbc_usb_ports[pi];
                ps.sf_flags=get_u16_le(resp.payload+q);q+=2;
                ps.sf_speed_tenth_mm_s=get_u16_le(resp.payload+q);q+=2;
                ps.sf_length_tenth_mm=get_u16_le(resp.payload+q);q+=2;
                ps.sf_feeding_state=resp.payload[q++];
                ps.sf_feeding_value_raw=get_u16_le(resp.payload+q);q+=2;
                ps.sf_feeding_selected_program=resp.payload[q++];
                ps.sf_current_program_step=resp.payload[q++];
                ps.sf_counter_tin_length=get_u64_le(resp.payload+q);q+=8;
                ps.sf_counter_plug_min=get_u32_le(resp.payload+q);q+=4;
                ps.sf_counter_work_min=get_u32_le(resp.payload+q);q+=4;
                ps.sf_counter_idle_min=get_u32_le(resp.payload+q);q+=4;
                ps.sf_counter_work_cycles=get_u32_le(resp.payload+q);q+=4;
                ps.sf_partial_tin_length=get_u64_le(resp.payload+q);q+=8;
                ps.sf_partial_plug_min=get_u32_le(resp.payload+q);q+=4;
                ps.sf_partial_work_min=get_u32_le(resp.payload+q);q+=4;
                ps.sf_partial_idle_min=get_u32_le(resp.payload+q);q+=4;
                ps.sf_partial_work_cycles=get_u32_le(resp.payload+q);q+=4;
              } else q+=59;
            }
            if (q + 5 <= resp.len && resp.payload[q] == 0xEC && resp.payload[q + 1] == 1) {
              q+=2; const uint8_t total=resp.payload[q++], start=resp.payload[q++], count=resp.payload[q++];
              const size_t bytes=(size_t)count*21U;
              if(total<=35 && start<=total && (uint16_t)start+count<=total && q+bytes<=resp.len){
                for(uint8_t i=0;i<count;++i){
                  const uint8_t idx=(uint8_t)(start+i); JbcUsbSfProgramState& pg=rec->jbc_usb_sf_programs[idx];
                  pg.flags=resp.payload[q++]; memcpy(pg.name,resp.payload+q,8);q+=8; pg.name[8]=0;
                  for(int8_t k=7;k>=0 && (pg.name[k]==' '||pg.name[k]=='\0');--k) pg.name[k]=0;
                  for(uint8_t st=0;st<3;++st){pg.length[st]=get_u16_le(resp.payload+q);q+=2;pg.speed[st]=get_u16_le(resp.payload+q);q+=2;}
                }
              } else if(q+bytes<=resp.len) q+=bytes;
            }
            // JbcUsbModule 0.1.53+: ED/v1 safe public-read completion.
            if (q + 3 <= resp.len && resp.payload[q] == 0xED && resp.payload[q + 1] == 1) {
              const uint8_t family=resp.payload[q+2];
              if(family==1 && q+27<=resp.len){
                q+=3;const uint8_t pi=resp.payload[q++];
                rec->jbc_usb_fe_service_flags=get_u16_le(resp.payload+q);q+=2;
                rec->jbc_usb_fe_flow_x_mil=get_u16_le(resp.payload+q);q+=2;
                rec->jbc_usb_fe_speed_rpm=get_u16_le(resp.payload+q);q+=2;
                rec->jbc_usb_fe_selected_flow_x_mil=get_u16_le(resp.payload+q);q+=2;
                rec->jbc_usb_fe_filter_status=get_u16_le(resp.payload+q);q+=2;
                memcpy(rec->jbc_usb_fe_pin,resp.payload+q,4);q+=4;rec->jbc_usb_fe_pin[4]=0;
                rec->jbc_usb_fe_beep=resp.payload[q++]!=0;
                if(pi<4){JbcUsbPortState& ps=rec->jbc_usb_ports[pi];ps.fe_service_flags=get_u16_le(resp.payload+q);q+=2;
                  ps.fe_stand_intakes=resp.payload[q++];ps.fe_suction_delay_work=get_u16_le(resp.payload+q);q+=2;
                  ps.fe_suction_delay_stand=get_u16_le(resp.payload+q);q+=2;ps.fe_pedal_connected=resp.payload[q++];}
                else q+=8;
              }else if(family==2 && q+8<=resp.len){q+=3;rec->jbc_usb_ph_remote_valid=resp.payload[q++]!=0;
                rec->jbc_usb_ph_remote_mode=resp.payload[q++]!=0;rec->jbc_usb_ph_conti_valid=resp.payload[q++]!=0;
                rec->jbc_usb_ph_conti_speed=resp.payload[q++];rec->jbc_usb_ph_conti_ports=resp.payload[q++];
              }else if(family==3 && q+6<=resp.len){q+=3;rec->jbc_usb_sf_conti_valid=resp.payload[q++]!=0;
                rec->jbc_usb_sf_conti_speed=resp.payload[q++];rec->jbc_usb_sf_conti_ports=resp.payload[q++];}
            }
            // JbcUsbModule 0.1.21+: common Settings.Name suffix after any D7
            // extension. D6 = marker, version, byte length, name bytes.
            if (q + 3 <= resp.len && resp.payload[q] == 0xD6 && resp.payload[q + 1] >= 1) {
              const uint8_t raw_name_len = resp.payload[q + 2];
              const uint8_t name_len = raw_name_len > 16U ? (uint8_t)16 : raw_name_len;
              if (q + 3U + raw_name_len <= resp.len) {
                memset(rec->jbc_usb_station_name, 0, sizeof(rec->jbc_usb_station_name));
                if (name_len) memcpy(rec->jbc_usb_station_name, resp.payload + q + 3, name_len);
                rec->jbc_usb_station_name[name_len] = 0;
                q += 3U + raw_name_len;
              }
            }
            // JbcUsbModule 1.1.55+: EE/v1 separates true transport framing
            // from command-payload decode and discovery/handshake validation.
            if (q + 8 <= resp.len && resp.payload[q] == 0xEE && resp.payload[q + 1] == 1) {
              q += 2;
              rec->jbc_usb_frame_errors = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_decode_errors = get_u16_le(resp.payload + q); q += 2;
              rec->jbc_usb_handshake_errors = get_u16_le(resp.payload + q); q += 2;
            }
            // JbcUsbModule 1.1.56+: EF/v1 exposes the last decode mismatch and
            // the three commands responsible for most decode errors.
            if (q + 16 <= resp.len && resp.payload[q] == 0xEF && resp.payload[q + 1] == 1) {
              q += 2;
              rec->jbc_usb_decode_last_cmd = resp.payload[q++];
              rec->jbc_usb_decode_last_got_len = resp.payload[q++];
              rec->jbc_usb_decode_last_expected_min = resp.payload[q++];
              rec->jbc_usb_decode_last_expected_max = resp.payload[q++];
              const uint8_t top_n = resp.payload[q++];
              for (uint8_t rank = 0; rank < 3; ++rank) {
                rec->jbc_usb_decode_top_cmd[rank] = resp.payload[q++];
                rec->jbc_usb_decode_top_count[rank] = get_u16_le(resp.payload + q); q += 2;
              }
              (void)top_n;
            }
          }
        }
      }
    } else {
      rec->jbc_usb_cp_diag_valid = false;
      rec->jbc_usb_device_id_len = 0;
      memset(rec->jbc_usb_device_id, 0, sizeof(rec->jbc_usb_device_id));
      rec->jbc_usb_station_name[0] = 0;
    }
  } else if (resp.len >= 17) {
    rec->telemetry_valid = true;
    rec->module_heap_free = get_u32_le(resp.payload + 2);
    rec->module_heap_min = get_u32_le(resp.payload + 6);
    rec->module_uptime_s = get_u32_le(resp.payload + 10);
    rec->module_cpu_load_pct = resp.payload[14];
    rec->module_loop_max_ms = get_u16_le(resp.payload + 15);
    if (resp.payload[1] == MODULE_DISPLAY && resp.len >= 21) {
      rec->display_brightness_pct = resp.payload[18];
      rec->display_language = resp.payload[19];
      rec->display_theme = resp.payload[20];
      if (resp.len >= 22) rec->display_screensaver_min = resp.payload[21];
    }
    if ((rec->caps & CAP_FILTER_SENSOR) && resp.len >= 27) {
      rec->fanio_filter_saturation_permille = get_u16_le(resp.payload + 17);
      rec->fanio_filter_pressure_raw = (int16_t)get_u16_le(resp.payload + 19);
      rec->fanio_filter_zero_raw = (int16_t)get_u16_le(resp.payload + 21);
      rec->fanio_filter_clean_raw = (int16_t)get_u16_le(resp.payload + 23);
      if (resp.len >= 29) {
        rec->fanio_filter_warn_raw = (int16_t)get_u16_le(resp.payload + 25);
        rec->fanio_filter_full_raw = (int16_t)get_u16_le(resp.payload + 27);
      } else {
        rec->fanio_filter_warn_raw = 350;
        rec->fanio_filter_full_raw = (int16_t)get_u16_le(resp.payload + 25);
      }
      if (resp.len >= 31) {
        rec->fanio_filter_flags = resp.payload[29];
        rec->fanio_filter_cal_quality = resp.payload[30];
      } else {
        rec->fanio_filter_flags = 0;
        rec->fanio_filter_cal_quality = 0;
      }
    }
  }

  // LED status is appended to telemetry in a backward-compatible suffix.
  // Older module firmware simply ends at the legacy payload length.
  rec->led_status_valid = false;
  uint8_t led_pos = 0xFF;
  switch (resp.payload[1]) {
    case MODULE_WELLER_ZERO_SMOG: led_pos = 30; break;
    case MODULE_DISPLAY: led_pos = 22; break;
    case MODULE_FAN_IO_PRO: led_pos = 31; break;
    case MODULE_JBC_USB: led_pos = 37; break;
    default: led_pos = 17; break;
  }
  if (led_pos != 0xFF && resp.len >= (uint8_t)(led_pos + 2)) {
    rec->led_ofe_event = resp.payload[led_pos];
    rec->led_evt_event = resp.payload[led_pos + 1];
    rec->led_status_valid = rec->led_ofe_event <= 13 && rec->led_evt_event <= 13;
  }
  return true;
}

bool MasterScheduler::sendOutputEnable(uint8_t addr, bool enabled) {
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online) return false;
  if ((rec->type == MODULE_UNIVERSAL_RS232 || rec->type == MODULE_MODBUS_RTU)) {
    if (!rec->universal_descriptor_valid) refreshUniversalDescriptor(rec->addr, true);
    uint8_t enable_id = 0, power_id = 0;
    if (!universalFindMainOutputEntities(*rec, enable_id, power_id)) return false;

    bool ok = true;
    if (enable_id) {
      const uint8_t v = enabled ? '1' : '0';
      ok = setUniversalEntity(rec->addr, enable_id, &v, 1);
    } else if (power_id) {
      // Power-only profiles use 0% as OFF and the current requested power as ON.
      uint16_t target = enabled ? extractor_.outputPower() : 0;
      if (enabled && !target) target = rec->output_power;
      if (enabled && !target) target = minSelectFlowForActiveOutput();
      uint16_t pct = target / 10U;
      if (target && pct < minSelectFlowForActiveOutput() / 10U) pct = minSelectFlowForActiveOutput() / 10U;
      if (pct > 100U) pct = 100U;
      char text[5];
      snprintf(text, sizeof(text), "%u", pct);
      ok = setUniversalEntity(rec->addr, power_id, (const uint8_t*)text, (uint8_t)strlen(text));
    }

    if (ok) {
      rec->output_status_valid = true;
      rec->output_enabled = enabled;
      if (!enabled) rec->output_power = 0;
    }
    return ok;
  }
  Frame resp;
  uint8_t payload[] = { enabled ? 1U : 0U };
  if (!request(addr, CMD_SET_ENABLE, payload, sizeof(payload), resp, 35)) return false;
  return resp.cmd == (CMD_SET_ENABLE | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::sendOutputPower(uint8_t addr, uint16_t power) {
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online) return false;
  if ((rec->type == MODULE_UNIVERSAL_RS232 || rec->type == MODULE_MODBUS_RTU)) {
    if (!rec->universal_descriptor_valid) refreshUniversalDescriptor(rec->addr, true);
    uint8_t enable_id = 0, power_id = 0;
    if (!universalFindMainOutputEntities(*rec, enable_id, power_id)) return false;
    if (!power_id) return true;

    uint16_t pct = power / 10U;
    if (power) {
      const uint16_t min_pct = minSelectFlowForActiveOutput() / 10U;
      if (pct < min_pct) pct = min_pct;
    }
    if (pct > 100U) pct = 100U;
    char text[5];
    snprintf(text, sizeof(text), "%u", pct);
    const bool ok = setUniversalEntity(rec->addr, power_id, (const uint8_t*)text, (uint8_t)strlen(text));
    if (ok && rec->output_enabled) rec->output_power = power;
    return ok;
  }
  Frame resp;
  uint8_t payload[2];
  put_u16_le(payload, power);
  if (!request(addr, CMD_SET_POWER, payload, sizeof(payload), resp, 35)) return false;
  return resp.cmd == (CMD_SET_POWER | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::moduleFwBegin(uint8_t addr, uint32_t size) {
  last_fw_error_[0] = 0;
  {
    // Snapshot the peer on the same lock used by UDP receive and bus requests.
    SchedulerBusLock bus_lock(bus_mutex_, pdMS_TO_TICKS(1000));
    if (!bus_lock.locked) {
      snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_BEGIN bus busy");
      return false;
    }
    master_display_wifi.beginFirmware(addr);
  }
  module_fw_active_ = true;
  module_fw_target_ = addr;
  display_update_active_ = true;
  display_update_target_ = addr;
  display_update_progress_ = 0;
  last_fw_chunk_attempts_ = 0;
  fw_chunk_retry_count_ = 0;

  ModuleRecord* rec = registry_.find(addr);
  const bool display_target = rec && (rec->caps & CAP_DISPLAY);
  const uint8_t attempts = display_target ? 4 : 3;
  const uint32_t timeout_ms = display_target ? 5000UL : 3000UL;

  uint8_t payload[4];
  put_u32_le(payload, size);
  Frame resp;
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
    if (request(addr, CMD_FW_BEGIN, payload, sizeof(payload), resp, timeout_ms)) {
      const bool ok = resp.cmd == (CMD_FW_BEGIN | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
      if (ok) return true;
      snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_BEGIN %s", fwStatusName(resp.len ? resp.payload[0] : 255));
      moduleFwAbort(addr);
      return false;
    }
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_BEGIN timeout retry %u/%u", attempt + 1, attempts);
    serviceDelay(display_target ? 80 : 30);
  }

  moduleFwAbort(addr);
  snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_BEGIN timeout");
  return false;
}

bool MasterScheduler::moduleFwChunk(uint8_t addr, uint32_t offset, const uint8_t* data, uint8_t len) {
  last_fw_error_[0] = 0;
  if (!module_fw_active_ || module_fw_target_ != addr) {
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_CHUNK no active OTA");
    return false;
  }
  if (!data || len == 0 || len > (MAX_PAYLOAD - 4)) {
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_CHUNK bad len %u", len);
    moduleFwAbort(addr);
    return false;
  }
  ModuleRecord* rec = registry_.find(addr);
  const bool display_target = rec && (rec->caps & CAP_DISPLAY);
  // Normal module FW_CHUNK responses are usually in the 10..160 ms range.
  // Waiting a fixed 3 seconds for a single lost ACK created a very visible bus
  // blackout before the idempotent same-offset retry.  Use a short first retry
  // and progressively larger windows only when the target is genuinely busy.
  static const uint16_t kModuleChunkRetryTimeoutMs[] = {220, 320, 500, 800, 1200, 1600};
  const uint8_t attempts = display_target
      ? (uint8_t)MODULE_FW_DISPLAY_CHUNK_ATTEMPTS
      : (uint8_t)(sizeof(kModuleChunkRetryTimeoutMs) / sizeof(kModuleChunkRetryTimeoutMs[0]));

  uint8_t payload[MAX_PAYLOAD];
  put_u32_le(payload, offset);
  memcpy(payload + 4, data, len);
  Frame resp;
  last_fw_chunk_attempts_ = 0;
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
    const uint32_t timeout_ms = display_target
        ? (uint32_t)MODULE_FW_DISPLAY_CHUNK_TIMEOUT_MS
        : (uint32_t)kModuleChunkRetryTimeoutMs[attempt];
    last_fw_chunk_attempts_ = (uint8_t)(attempt + 1);
    if (request(addr, CMD_FW_CHUNK, payload, (uint8_t)(len + 4), resp, timeout_ms)) {
      const bool ok = resp.cmd == (CMD_FW_CHUNK | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
      if (ok) return true;
      snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_CHUNK %s", fwStatusName(resp.len ? resp.payload[0] : 255));
      moduleFwAbort(addr);
      return false;
    }
    if ((uint8_t)(attempt + 1) < attempts) ++fw_chunk_retry_count_;
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_CHUNK timeout retry %u/%u", attempt + 1, attempts);
    serviceDelay(display_target ? 10 : 15);
  }

  moduleFwAbort(addr);
  snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_CHUNK timeout");
  return false;
}

bool MasterScheduler::moduleFwEnd(uint8_t addr) {
  last_fw_error_[0] = 0;
  if (!module_fw_active_ || module_fw_target_ != addr) {
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_END no active OTA");
    notifyDisplayUpdate(false, addr, 0);
    return false;
  }

  Frame resp;
  const bool wifi_update = master_display_wifi.firmwareWireless(addr);
  bool received = false;
  for (uint8_t attempt=0; attempt<(wifi_update ? 3 : 1) && !received; ++attempt)
    received = request(addr, CMD_FW_END, nullptr, 0, resp, 3000);
  if (!received) {
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_END timeout");
    moduleFwAbort(addr);
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_END timeout");
    return false;
  }

  const bool ok = resp.cmd == (CMD_FW_END | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (!ok) {
    snprintf(last_fw_error_, sizeof(last_fw_error_), "FW_END %s", fwStatusName(resp.len ? resp.payload[0] : 255));
    moduleFwAbort(addr);
    return false;
  }

  if (wifi_update) {
    // The display holds the committed image for END retries and reboots on this
    // acknowledgement, or after its bounded fallback if this command is lost.
    Frame reboot_response;
    request(addr, CMD_FW_REBOOT, nullptr, 0, reboot_response, 1000);
  }
  master_display_wifi.endFirmware();
  module_fw_active_ = false;
  module_fw_target_ = ADDR_INVALID;

  ModuleRecord* rec = registry_.find(addr);
  if (rec) {
    rec->online = false;
    rec->came_online = false;
    if (rec->caps & CAP_JBC_BUS) rec->jbc_settings_valid = false;
    rec->consecutive_timeouts = 5;
    rec->telemetry_valid = false;
    rec->output_status_valid = false;
    rec->universal_descriptor_valid = false;
    rec->universal_entities_valid = false;
    rec->universal_entity_count = 0;
    rec->universal_descriptor[0] = 0;
    rec->last_seen_ms = 0;
  }
  last_offline_reprobe_ms_ = 0;
  selectRoles();
  updateJbcAggregate();
  updateInputRouting();
  extractor_.markOutputDirty();
  last_system_jbc_error_ = 0xFFFF;
  last_system_jbc_filter_life_ = 0xFFFF;
  last_system_jbc_filter_sat_ = 0xFFFF;

  notifyDisplayUpdate(false, addr, 100);
  return true;
}

bool MasterScheduler::moduleFwAbort(uint8_t addr) {
  ModuleRecord* rec = registry_.find(addr);
  const bool display_target = rec && (rec->caps & CAP_DISPLAY);
  const uint32_t timeout_ms = display_target ? 1200UL : 700UL;
  bool acknowledged = false;

  // FW_ABORT is idempotent in every module firmware. Retry it because the
  // target may still be committing the last flash block when cancellation is
  // requested and can miss the first short request window.
  for (uint8_t attempt = 0; attempt < 3 && !acknowledged; ++attempt) {
    Frame resp;
    const bool got_ack = request(addr, CMD_FW_ABORT, nullptr, 0, resp, timeout_ms);
    acknowledged = got_ack && resp.cmd == (CMD_FW_ABORT | 0x80) &&
                   resp.len >= 1 && resp.payload[0] == STATUS_OK;
    if (!acknowledged && attempt < 2) serviceDelay(display_target ? 40 : 20);
  }

  master_display_wifi.endFirmware();
  module_fw_active_ = false;
  module_fw_target_ = ADDR_INVALID;
  notifyDisplayUpdate(false, addr, 0);
  return acknowledged;
}

bool MasterScheduler::moduleReboot(uint8_t addr) {
  Frame resp;
  return request(addr, CMD_FW_REBOOT, nullptr, 0, resp, 500) &&
    resp.cmd == (CMD_FW_REBOOT | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::sendDisplayStatus(uint8_t addr) {
  if (!firmwareUpdateActive()) {
    if (master_display_wifi.probeDue(addr)) {
      Frame probe;
      const bool ok = request(addr, CMD_PING, nullptr, 0, probe, 12, true) &&
        probe.len && probe.payload[0] == STATUS_OK;
      master_display_wifi.probeResult(addr, ok);
    }
    const ModuleRecord* display = registry_.find(addr);
    if (display && (display->caps & CAP_DISPLAY_HYBRID) && master_display_wifi.provisioningDue(addr)) {
      Frame reply;
      const uint8_t query = 0;
      if (request(addr, CMD_DISPLAY_CONFIG, &query, 1, reply, 30) &&
          reply.len == 7 && reply.payload[0] == STATUS_OK && reply.payload[1]) {
        ofe_wifi::Config config;
        if (master_display_wifi.configuration(addr, config)) {
          config.mode = reply.payload[2];
          uint8_t digest[32], key[16] = {};
          ofe_wifi::mac(key, reinterpret_cast<const uint8_t*>(&config), sizeof(config), digest);
          if (get_u32_le(digest) != get_u32_le(reply.payload + 3)) {
            uint8_t provision[1 + sizeof(config)]; provision[0] = 1;
            memcpy(provision + 1, &config, sizeof(config));
            request(addr, CMD_DISPLAY_CONFIG, provision, sizeof(provision), reply, 100);
          }
        }
      }
    }
  }
  uint8_t payload[MAX_PAYLOAD] = {0};
  payload[0] = extractor_.outputEnabled() ? 1 : 0;
  put_u16_le(payload + 1, extractor_.outputPower());
  payload[3] = extractor_.workMask();
  uint32_t afterrun_s = (extractor_.afterrunLeftMs() + 999UL) / 1000UL;
  if (afterrun_s > 65535UL) afterrun_s = 65535UL;
  put_u16_le(payload + 4, (uint16_t)afterrun_s);
  payload[6] = registry_.count();

  bool jbc_connected = false;
  bool weller_connected = false;
  bool jbc_present = false;
  bool weller_present = false;
  bool fan_present = false;
  bool output_present = false;
  uint16_t fan_rpm = extractor_.outputState().valid ? extractor_.outputState().rpm : 0;
  uint8_t jbc_inputs = 0;
  uint8_t active_jbc_addr = extractor_.jbcState().jbc_addr;
  uint8_t active_station_addr = extractor_.jbcState().station_addr;
  uint8_t weller_speed = 0;
  uint8_t weller_filter = 0;
  uint16_t weller_runtime = 0;
  uint16_t weller_programmed = 0;
  uint8_t weller_light = 0;
  uint16_t active_io_input_mask = 0;
  uint16_t active_io_output_mask = 0;
  uint16_t active_io_fault_mask = 0;
  uint8_t active_output_enabled = 0;
  uint16_t active_output_power = 0;
  uint16_t active_output_rpm = 0;
  uint16_t active_output_fault = 0;
  uint16_t weller_version = 0;
  uint8_t jbc_link_flags = 0;
  uint8_t jbc_work_mask = 0;
  uint8_t jbc_stand_mask = 0;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (!rec.online) continue;
    if (rec.caps & CAP_JBC_ACTIVITY) {
      jbc_present = true;
      jbc_inputs++;
      if (rec.jbc_link_flags & FAST_FLAG_CONNECTED) {
        jbc_connected = true;
        if (!active_jbc_addr) active_jbc_addr = rec.jbc_addr;
        if (!active_station_addr) active_station_addr = rec.station_addr;
      }
      if (!jbc_link_flags || rec.role_jbc) {
        jbc_link_flags = rec.jbc_link_flags;
        jbc_work_mask = rec.jbc_work_mask;
        jbc_stand_mask = rec.jbc_stand_mask;
      }
    }
    if (rec.caps & CAP_WELLER_INTERFACE) {
      weller_present = true;
      if (rec.weller_uart_age_sec != 0xFFFF && rec.weller_uart_age_sec <= 10) weller_connected = true;
      if (rec.role_output) {
        weller_speed = rec.weller_speed_percent;
        weller_filter = rec.weller_filter_status;
        weller_runtime = rec.weller_filter_runtime_minutes;
        weller_programmed = rec.weller_programmed_filter_minutes;
        weller_light = rec.weller_work_light;
        weller_version = rec.weller_version;
        if (rec.weller_fan_rpm) fan_rpm = rec.weller_fan_rpm;
      }
    }
    const bool entity_output = moduleProvidesExtractorOutput(rec) &&
      (rec.type == MODULE_UNIVERSAL_RS232 || rec.type == MODULE_MODBUS_RTU);
    if ((rec.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_TACHO_INPUT)) &&
        !(rec.caps & CAP_WELLER_INTERFACE) && !entity_output) fan_present = true;
    if ((rec.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_WELLER_INTERFACE)) || entity_output) output_present = true;
    if (rec.role_output && rec.output_status_valid && rec.output_rpm) fan_rpm = rec.output_rpm;
    if (rec.addr == active_output_addr_) {
      active_io_input_mask = rec.io_input_mask;
      active_io_output_mask = rec.io_output_mask;
      active_io_fault_mask = rec.io_fault_mask;
      active_output_enabled = rec.output_enabled ? 1 : 0;
      active_output_power = rec.output_power;
      active_output_rpm = rec.output_rpm ? rec.output_rpm : rec.weller_fan_rpm;
      active_output_fault = rec.output_fault_mask | rec.io_fault_mask;
      if (rec.caps & CAP_WELLER_INTERFACE) {
        weller_version = rec.weller_version;
        if (rec.weller_fan_rpm) active_output_rpm = rec.weller_fan_rpm;
      }
    }
  }
  DisplayAlarmSnapshot alarm_snapshot;
  buildDisplayAlarmSnapshot(jbc_present, alarm_snapshot);

  payload[7] = jbc_connected ? 1 : 0;
  payload[8] = weller_connected ? 1 : 0;
  put_u16_le(payload + 9, fan_rpm);
  payload[11] = display_update_active_ ? 1 : 0;
  payload[12] = display_update_target_;
  payload[13] = display_update_progress_;
  payload[14] = active_output_addr_;
  payload[15] = jbc_inputs;
  payload[16] = extractor_.continuous() ? 1 : 0;
  payload[17] = desired_jbc_settings_.suction_level;
  put_u16_le(payload + 18, desired_jbc_settings_.select_flow);
  put_u16_le(payload + 20, desired_jbc_settings_.delay_work_sec);
  payload[22] = active_jbc_addr;
  payload[23] = active_station_addr;
  payload[24] = weller_speed;
  payload[25] = weller_filter;
  put_u16_le(payload + 26, weller_runtime);
  put_u16_le(payload + 28, weller_programmed);
  payload[30] = weller_light;
  payload[31] = extractor_.externalInputActive() ? 1 : 0;
  payload[32] = (jbc_present ? 0x01 : 0) |
                (weller_present ? 0x02 : 0) |
                (fan_present ? 0x04 : 0) |
                (output_present ? 0x08 : 0);
  put_u16_le(payload + 33, active_io_input_mask);
  put_u16_le(payload + 35, active_io_output_mask);
  put_u16_le(payload + 37, active_io_fault_mask);
  payload[39] = active_output_enabled;
  put_u16_le(payload + 40, active_output_power);
  put_u16_le(payload + 42, active_output_rpm);
  put_u16_le(payload + 44, active_output_fault);
  put_u16_le(payload + 46, weller_version);
  payload[48] = jbc_link_flags;
  payload[49] = jbc_work_mask;
  payload[50] = jbc_stand_mask;
  payload[51] = jbcInputEnabled() ? 1 : 0; // JBC input routing enabled, not the selected output address.
  put_u16_le(payload + 52, desired_jbc_settings_.delay_stand_sec);
  payload[54] = desired_jbc_settings_.stand_intakes ? 1 : 0;

  uint8_t payload_len = 55;
  auto payload_can_write = [&](uint16_t n) -> bool {
    return (uint16_t)payload_len + n <= MAX_PAYLOAD;
  };
  auto payload_write_u8 = [&](uint8_t value) -> bool {
    if (!payload_can_write(1)) return false;
    payload[payload_len++] = value;
    return true;
  };
  auto payload_write_u16 = [&](uint16_t value) -> bool {
    if (!payload_can_write(2)) return false;
    put_u16_le(payload + payload_len, value);
    payload_len += 2;
    return true;
  };
  auto payload_write_u32 = [&](uint32_t value) -> bool {
    if (!payload_can_write(4)) return false;
    put_u32_le(payload + payload_len, value);
    payload_len += 4;
    return true;
  };
  auto payload_write_bytes = [&](const void* data, uint8_t len) -> bool {
    if (!len) return true;
    if (!data || !payload_can_write(len)) return false;
    memcpy(payload + payload_len, data, len);
    payload_len += len;
    return true;
  };
  auto payload_reserve_u8 = [&](uint8_t& pos) -> bool {
    if (!payload_can_write(1)) return false;
    pos = payload_len;
    payload[payload_len++] = 0;
    return true;
  };
  ModuleRecord* display = registry_.find(addr);
  // Keep a compact alarm copy in DISPLAY_STATUS for backward-compatible displays.
  // Current displays also receive the full alarm cache via CMD_DISPLAY_ALARMS.
  const uint8_t display_alarm_item_count = alarm_snapshot.item_count;
  const uint8_t display_alarm_critical_mask = alarm_snapshot.critical_mask;
  if (!payload_write_u8(5) || // Display extension v5: v4 data plus afterrun power profile.
      !payload_write_u8(preferred_output_addr_) || // 0 = Auto, otherwise manually selected output module.
      !payload_write_u16(jbc_present ? alarm_snapshot.jbc_error : 0) ||
      !payload_write_u8(0xA9) ||
      !payload_write_u8(alarm_snapshot.alarm_count) ||
      !payload_write_u8(display_alarm_item_count) ||
      !payload_write_u8(display_alarm_critical_mask)) {
    return false;
  }
  for (uint8_t i = 0; i < display_alarm_item_count && (uint16_t)payload_len + 6U <= MAX_PAYLOAD; ++i) {
    payload_write_u8(alarm_snapshot.items[i].addr);
    payload_write_u8(alarm_snapshot.items[i].type);
    payload_write_u8(alarm_snapshot.items[i].code);
    payload_write_u8(0);
    payload_write_u16(alarm_snapshot.items[i].value);
  }
  if (!payload_write_u8(0xAA) ||
      !payload_write_u8(main_input_source_type_) ||
      !payload_write_u8(main_input_source_addr_) ||
      !payload_write_u8(main_input_source_bit_)) {
    return false;
  }
  if (!payload_write_u8(0xAB) ||
      !payload_write_u8(extractor_.afterrunPowerProfileEnabled() ? 1 : 0) ||
      !payload_write_u16(extractor_.afterrunPower())) {
    return false;
  }
  if (!payload_write_u8(0xA8) ||
      !payload_write_u8(autoOutputCandidateAddr())) {
    return false;
  }

  // A7: compact list of all currently connected JBC stations for the display
  // screensaver. Each entry is flags plus a zero-padded four-character model.
  // Known JBC model names fit in four characters (DDE, JTSE, PHXL, F4W, ...).
  if (payload_can_write(2U + 7U)) {
    const uint8_t marker_pos = payload_len;
    payload_write_u8(0xA7);
    const uint8_t count_pos = payload_len;
    payload_write_u8(0);
    uint8_t station_count = 0;
    for (uint8_t i = 0; i < registry_.count() && station_count < 16; ++i) {
      const ModuleRecord& rec = registry_.at(i);
      if (!rec.online || !(rec.caps & CAP_JBC_ACTIVITY) ||
          !(rec.jbc_link_flags & FAST_FLAG_CONNECTED)) continue;
      // Keep seven bytes free for the clock extension appended below.
      if (!payload_can_write(5U + 7U)) break;

      uint8_t flags = 0;
      if (rec.jbc_work_mask) flags |= 0x01;
      if (rec.jbc_stand_mask) flags |= 0x02;
      const bool is_usb = rec.type == MODULE_JBC_USB || (rec.caps & CAP_JBC_USB);
      const bool station_error = is_usb
        ? (rec.jbc_usb_station_error != 0xFFFFU && rec.jbc_usb_station_error != 0)
        : rec.jbc_stat_error != 0;
      if (station_error) flags |= 0x04;

      char model[4] = {0, 0, 0, 0};
      const char* source = nullptr;
      if (is_usb) {
        if (rec.jbc_usb_model[0] && strcmp(rec.jbc_usb_model, "-") != 0) source = rec.jbc_usb_model;
        else if (rec.jbc_usb_model_raw[0] && strcmp(rec.jbc_usb_model_raw, "-") != 0) source = rec.jbc_usb_model_raw;
        else source = "USB";
      } else if (rec.station_addr >= 0x18 && rec.station_addr <= 0x21) {
        source = "DDE";
      } else if (rec.station_addr >= 0x12 && rec.station_addr <= 0x15) {
        source = "JTSE";
      } else {
        source = "JBC";
      }
      for (uint8_t c = 0; c < sizeof(model) && source[c] && source[c] != '_'; ++c) model[c] = source[c];
      payload_write_u8(flags);
      payload_write_bytes(model, sizeof(model));
      ++station_count;
    }
    payload[count_pos] = station_count;
    if (!station_count) payload_len = marker_pos;
  }
  // DISPLAY_STATUS is now strictly Home/Live data only.
  // Module lists, module details and Universal/Modbus entities are sent via
  // dedicated display frames so missing blocks never clear unrelated UI caches.

  if ((uint16_t)payload_len + 7U <= MAX_PAYLOAD) {
    uint8_t hour = 0xFF;
    uint8_t minute = 0xFF;
    uint8_t day = 0xFF;
    uint8_t month = 0xFF;
    uint16_t year = 0xFFFF;
    time_t now = time(nullptr);
    struct tm local_now;
    if (now > 1700000000 && localtime_r(&now, &local_now)) {
      hour = (uint8_t)local_now.tm_hour;
      minute = (uint8_t)local_now.tm_min;
      day = (uint8_t)local_now.tm_mday;
      month = (uint8_t)(local_now.tm_mon + 1);
      year = (uint16_t)(local_now.tm_year + 1900);
    }
    payload_write_u8(0xA6);
    payload_write_u8(hour);
    payload_write_u8(minute);
    payload_write_u8(day);
    payload_write_u8(month);
    payload_write_u16(year);
  }

  Frame resp;
  if (!request(addr, CMD_DISPLAY_STATUS, payload, payload_len, resp, 100)) return false;
  const bool ok = resp.cmd == (CMD_DISPLAY_STATUS | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (!ok) return false;
  if (display) {
    display->online = true;
    display->consecutive_timeouts = 0;
    display->last_seen_ms = millis();
  }
  uint8_t response_view_arg = display ? display->display_view_arg : 0;
  bool display_requested_universal_page = false;
  uint8_t requested_universal_addr = 0;
  uint8_t requested_universal_start = display ? display->display_universal_entity_start : 0;
  bool display_requested_module_list = false;
  uint8_t requested_module_list_start = 0;
  bool display_requested_module_detail = false;
  uint8_t requested_module_detail_addr = 0;
  if (display && resp.len >= 6) {
    display->display_view_mode = resp.payload[4] <= DISPLAY_VIEW_SYSTEM ? resp.payload[4] : DISPLAY_VIEW_HOME;
    display->display_view_arg = resp.payload[5];
    response_view_arg = display->display_view_arg;
    uint8_t rp = 6;
    while (rp < resp.len) {
      const uint8_t marker = resp.payload[rp++];
      if (marker == 0xAC && rp < resp.len) {
        // Legacy/current detail request: page belongs to the reported detail arg.
        display->display_universal_entity_start = resp.payload[rp++];
        requested_universal_addr = display->display_view_arg;
        requested_universal_start = display->display_universal_entity_start;
        display_requested_universal_page = true;
      } else if (marker == 0xAD && rp + 1 < resp.len) {
        // Background entity-cache request: does not change the Display view.
        requested_universal_addr = resp.payload[rp++];
        requested_universal_start = resp.payload[rp++];
        display->display_universal_entity_start = requested_universal_start;
        display_requested_universal_page = true;
      } else if (marker == 0xAE && rp < resp.len) {
        // Background module-list request; keep the visible view untouched.
        requested_module_list_start = resp.payload[rp++];
        display_requested_module_list = true;
      } else if (marker == 0xAF && rp < resp.len) {
        // Background module-detail request; keep the visible view untouched.
        requested_module_detail_addr = resp.payload[rp++];
        display_requested_module_detail = true;
      } else {
        break;
      }
    }
  }
  if (resp.len >= 4) {
    const uint8_t event_type = resp.payload[1];
    const int16_t event_value = (int16_t)get_u16_le(resp.payload + 2);
    handleDisplayEvent(event_type, event_value, response_view_arg);
  }
  if (display) {
    // Structured display data is now sent on dedicated frames. These frames only
    // update their own cache on the Display and cannot clear Home/detail data.
    sendDisplayAlarms(addr, alarm_snapshot);
    if (display->display_view_mode == DISPLAY_VIEW_MODULE_LIST) {
      sendDisplayModuleList(addr, display->display_view_arg);
    } else if (display_requested_module_list) {
      sendDisplayModuleList(addr, requested_module_list_start);
    }
    if (display->display_view_mode == DISPLAY_VIEW_MODULE_DETAIL) {
      sendDisplayModuleDetail(addr, display->display_view_arg);
    } else if (display_requested_module_detail) {
      sendDisplayModuleDetail(addr, requested_module_detail_addr);
    }
  }
  if (display && display_requested_universal_page) {
    if (!requested_universal_addr) requested_universal_addr = response_view_arg;
    const ModuleRecord* detail_rec = registry_.find(requested_universal_addr);
    if (detail_rec && (detail_rec->type == MODULE_UNIVERSAL_RS232 || detail_rec->type == MODULE_MODBUS_RTU)) {
      // Send exactly the page requested by the Display. Page size is variable
      // because text/select metadata changes how many entities fit in MAX_PAYLOAD.
      // The Display advances its cursor by the actual returned count.
      sendDisplayUniversalEntityPage(addr, *detail_rec, requested_universal_start);
    }
  }
  return true;
}


void MasterScheduler::buildDisplayAlarmSnapshot(
    bool jbc_present,
    DisplayAlarmSnapshot& snapshot) const {
  snapshot = DisplayAlarmSnapshot();
  bool active_output_fault_alarm = false;

  auto set_alarm = [&](uint8_t addr, uint8_t type, uint8_t code, uint16_t value, bool critical) {
    if (snapshot.item_count < DISPLAY_ALARM_MAX_ITEMS) {
      DisplayAlarmItem& item = snapshot.items[snapshot.item_count];
      item.addr = addr;
      item.type = type;
      item.code = code;
      item.value = value;
      if (critical) snapshot.critical_mask |= (uint8_t)(1U << snapshot.item_count);
      ++snapshot.item_count;
    }
    if (snapshot.alarm_count < 99) ++snapshot.alarm_count;
  };

  if (!mainInputSourceAvailable()) {
    set_alarm(ADDR_MASTER, MODULE_UNKNOWN, DISPLAY_ALARM_NO_MAIN_INPUT, 0, false);
  }
  if (active_output_addr_ == 0) {
    set_alarm(ADDR_MASTER, MODULE_UNKNOWN, DISPLAY_ALARM_NO_MAIN_OUTPUT, preferred_output_addr_, false);
  }

  for (uint8_t i = 0; i < registry_.count(); ++i) {
    const ModuleRecord& rec = registry_.at(i);
    if (!rec.online) {
      set_alarm(rec.addr, rec.type, DISPLAY_ALARM_MODULE_OFFLINE, 0, true);
      continue;
    }

    uint16_t faults = rec.output_fault_mask | rec.io_fault_mask;
    const bool bridge_link_fault =
      (rec.type == MODULE_UNIVERSAL_RS232 || rec.type == MODULE_MODBUS_RTU) &&
      (faults & 0x0001U);
    const uint16_t output_fault_bits = 0x071FU;

    if (rec.addr != active_output_addr_ && !rec.role_output && !rec.output_enabled) {
      faults = (uint16_t)(
        (faults & (uint16_t)~output_fault_bits) |
        (bridge_link_fault ? 0x0001U : 0));
    }

    if (faults) {
      const bool critical = faults != 0x0002U;
      set_alarm(rec.addr, rec.type, DISPLAY_ALARM_OUTPUT_FAULT, faults, critical);
      if (rec.addr == active_output_addr_) active_output_fault_alarm = true;
      continue;
    }

    if ((rec.caps & CAP_JBC_BUS) &&
        (!(rec.jbc_link_flags & FAST_FLAG_CONNECTED) || !rec.station_addr)) {
      set_alarm(rec.addr, rec.type, DISPLAY_ALARM_JBC_STATION, 0, false);
      continue;
    }

    if ((rec.caps & CAP_JBC_USB) &&
        !(rec.jbc_link_flags & FAST_FLAG_CONNECTED)) {
      set_alarm(rec.addr, rec.type, DISPLAY_ALARM_JBC_STATION, 0, false);
      continue;
    }

    if ((rec.caps & CAP_WELLER_INTERFACE) &&
        (rec.weller_uart_age_sec == 0xFFFF || rec.weller_uart_age_sec > 10)) {
      set_alarm(rec.addr, rec.type, DISPLAY_ALARM_WELLER_LINK, rec.weller_uart_age_sec, false);
      continue;
    }
  }

  snapshot.jbc_error = systemJbcError();
  if (active_output_addr_ && jbc_present && snapshot.jbc_error && !active_output_fault_alarm) {
    set_alarm(
      ADDR_MASTER,
      MODULE_UNKNOWN,
      DISPLAY_ALARM_JBC_STATUS,
      snapshot.jbc_error,
      (snapshot.jbc_error & (uint16_t)~0x0002U) != 0);
  }
}

bool MasterScheduler::sendDisplayAlarms(
    uint8_t display_addr,
    const DisplayAlarmSnapshot& snapshot) {
  uint8_t payload[MAX_PAYLOAD];
  uint8_t len = 0;
  payload[len++] = 1; // frame version
  payload[len++] = snapshot.alarm_count;
  payload[len++] = snapshot.item_count;
  payload[len++] = snapshot.critical_mask;

  for (uint8_t i = 0;
       i < snapshot.item_count && len + 6 <= MAX_PAYLOAD;
       ++i) {
    payload[len++] = snapshot.items[i].addr;
    payload[len++] = snapshot.items[i].type;
    payload[len++] = snapshot.items[i].code;
    payload[len++] = 0;
    put_u16_le(payload + len, snapshot.items[i].value);
    len += 2;
  }

  Frame resp;
  if (!request(display_addr, CMD_DISPLAY_ALARMS, payload, len, resp, 40)) return false;
  return resp.cmd == (CMD_DISPLAY_ALARMS | 0x80) &&
         resp.len >= 1 &&
         resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::sendDisplayModuleList(uint8_t display_addr, uint8_t start_index) {
  uint8_t payload[MAX_PAYLOAD];
  uint8_t payload_len = 0;
  auto payload_can_write = [&](uint16_t n) -> bool { return (uint16_t)payload_len + n <= MAX_PAYLOAD; };
  auto payload_write_u8 = [&](uint8_t value) -> bool { if (!payload_can_write(1)) return false; payload[payload_len++] = value; return true; };
  auto payload_write_u32 = [&](uint32_t value) -> bool { if (!payload_can_write(4)) return false; put_u32_le(payload + payload_len, value); payload_len += 4; return true; };
  auto payload_write_bytes = [&](const void* data, uint8_t len) -> bool { if (!len) return true; if (!data || !payload_can_write(len)) return false; memcpy(payload + payload_len, data, len); payload_len += len; return true; };

  const uint8_t total = registry_.count() + 1;
  const uint8_t start = start_index < total ? start_index : 0;
  if (!payload_write_u8(1) || !payload_write_u8(total) || !payload_write_u8(start)) return false;
  const uint8_t count_pos = payload_len;
  if (!payload_write_u8(0)) return false;
  uint8_t entries = 0;
  for (uint8_t i = start; i < total && entries < 6; ++i) {
    const ModuleRecord* rec = nullptr;
    if (i == 0) {
      ModuleRecord* master_rec = scheduler_master_record_scratch();
      if (!master_rec) return false;
      memset(master_rec, 0, sizeof(*master_rec));
      master_rec->addr = ADDR_MASTER;
      master_rec->type = MODULE_UNKNOWN;
      master_rec->online = true;
      master_rec->fw_major = master_fw_major_;
      master_rec->fw_minor = master_fw_minor_;
      master_rec->fw_patch = master_fw_patch_;
      strncpy(master_rec->fw_suffix, master_fw_suffix_, sizeof(master_rec->fw_suffix) - 1);
      master_rec->module_uptime_s = millis() / 1000UL;
      master_rec->module_heap_free = ESP.getFreeHeap();
      master_rec->module_cpu_load_pct = master_cpu_load_pct_;
      master_rec->module_loop_max_ms = master_loop_max_ms_;
      strncpy(master_rec->name, master_name_, sizeof(master_rec->name) - 1);
      rec = master_rec;
    } else {
      rec = &registry_.at(i - 1);
    }
    const char* shown = rec->label[0] ? rec->label : rec->name;
    uint8_t name_len = shown ? strlen(shown) : 0;
    if (name_len > 31) name_len = 31;
    uint8_t suffix_len = rec->fw_suffix[0] ? (uint8_t)strlen(rec->fw_suffix) : 0;
    if (suffix_len > sizeof(rec->fw_suffix) - 1) suffix_len = sizeof(rec->fw_suffix) - 1;
    if (!payload_can_write((uint16_t)16U + suffix_len + name_len)) break;
    payload_write_u8(rec->addr);
    payload_write_u8(rec->type);
    payload_write_u8((rec->online ? 0x01 : 0) | (rec->role_jbc ? 0x02 : 0) | (rec->role_output ? 0x04 : 0));
    payload_write_u8(rec->fw_major);
    payload_write_u8(rec->fw_minor);
    payload_write_u8(rec->fw_patch);
    payload_write_u8(suffix_len);
    payload_write_bytes(rec->fw_suffix, suffix_len);
    payload_write_u32(rec->caps);
    payload_write_u32(rec->module_uptime_s);
    payload_write_u8(name_len);
    payload_write_bytes(shown, name_len);
    entries++;
  }
  payload[count_pos] = entries;
  Frame resp;
  if (!request(display_addr, CMD_DISPLAY_MODULE_LIST, payload, payload_len, resp, 60)) return false;
  return resp.cmd == (CMD_DISPLAY_MODULE_LIST | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::sendDisplayModuleDetail(uint8_t display_addr, uint8_t target_addr) {
  uint8_t payload[MAX_PAYLOAD];
  uint8_t payload_len = 0;
  auto payload_can_write = [&](uint16_t n) -> bool { return (uint16_t)payload_len + n <= MAX_PAYLOAD; };
  auto payload_write_u8 = [&](uint8_t value) -> bool { if (!payload_can_write(1)) return false; payload[payload_len++] = value; return true; };
  auto payload_write_u16 = [&](uint16_t value) -> bool { if (!payload_can_write(2)) return false; put_u16_le(payload + payload_len, value); payload_len += 2; return true; };
  auto payload_write_u32 = [&](uint32_t value) -> bool { if (!payload_can_write(4)) return false; put_u32_le(payload + payload_len, value); payload_len += 4; return true; };
  auto payload_write_bytes = [&](const void* data, uint8_t len) -> bool { if (!len) return true; if (!data || !payload_can_write(len)) return false; memcpy(payload + payload_len, data, len); payload_len += len; return true; };

  if (!payload_write_u8(1)) return false; // frame version
  const ModuleRecord* rec = nullptr;
  if (target_addr == ADDR_MASTER) {
    ModuleRecord* master_rec = scheduler_master_record_scratch();
    if (!master_rec) return false;
    memset(master_rec, 0, sizeof(*master_rec));
    master_rec->addr = ADDR_MASTER;
    master_rec->type = MODULE_UNKNOWN;
    master_rec->online = true;
    master_rec->fw_major = master_fw_major_;
    master_rec->fw_minor = master_fw_minor_;
    master_rec->fw_patch = master_fw_patch_;
    strncpy(master_rec->fw_suffix, master_fw_suffix_, sizeof(master_rec->fw_suffix) - 1);
    master_rec->module_uptime_s = millis() / 1000UL;
    master_rec->module_heap_free = ESP.getFreeHeap();
    master_rec->module_cpu_load_pct = master_cpu_load_pct_;
    master_rec->module_loop_max_ms = master_loop_max_ms_;
    strncpy(master_rec->name, master_name_, sizeof(master_rec->name) - 1);
    rec = master_rec;
  } else {
    rec = registry_.find(target_addr);
  }
  if (!rec) {
    payload_write_u8(0);
  } else {
    const bool detail_is_universal = rec->type == MODULE_UNIVERSAL_RS232 || rec->type == MODULE_MODBUS_RTU;
    const bool detail_is_jbc_usb = rec->type == MODULE_JBC_USB || (rec->caps & CAP_JBC_USB);
    if (detail_is_universal && (rec->caps & CAP_DESCRIPTOR) && !rec->universal_descriptor_valid) {
      refreshUniversalDescriptor(rec->addr, true);
      const ModuleRecord* refreshed = registry_.find(rec->addr);
      if (refreshed) rec = refreshed;
    }
    uint8_t suffix_len = rec->fw_suffix[0] ? (uint8_t)strlen(rec->fw_suffix) : 0;
    if (suffix_len > sizeof(rec->fw_suffix) - 1) suffix_len = sizeof(rec->fw_suffix) - 1;
    const char* shown = rec->label[0] ? rec->label : rec->name;
    uint8_t name_len = shown ? strlen(shown) : 0;
    if (name_len > 39) name_len = 39;
    uint8_t device_id_len = rec->jbc_device_id_len;
    if (device_id_len > sizeof(rec->jbc_device_id)) device_id_len = sizeof(rec->jbc_device_id);
    // Universal profiles and JBC USB core detail do not need the long Device-ID
    // field on the display wire. Omitting it guarantees room for B5 core data.
    if (detail_is_universal || detail_is_jbc_usb) device_id_len = 0;
    const uint16_t detail_base_len = (uint16_t)1U + 63U + suffix_len + name_len + 1U + device_id_len;
    if (!payload_can_write(detail_base_len)) {
      payload_write_u8(0);
    } else {
      payload_write_u8(1);
      payload_write_u8(rec->addr);
      payload_write_u8(rec->type);
      payload_write_u8((rec->online ? 0x01 : 0) | (rec->role_jbc ? 0x02 : 0) | (rec->role_output ? 0x04 : 0));
      payload_write_u8(rec->fw_major);
      payload_write_u8(rec->fw_minor);
      payload_write_u8(rec->fw_patch);
      payload_write_u8(suffix_len);
      payload_write_bytes(rec->fw_suffix, suffix_len);
      payload_write_u32(rec->caps);
      payload_write_u32(rec->module_uptime_s);
      payload_write_u32(rec->module_heap_free);
      payload_write_u8(rec->module_cpu_load_pct);
      payload_write_u16(rec->module_loop_max_ms);
      payload_write_u16(rec->io_input_mask);
      payload_write_u16(rec->io_output_mask);
      payload_write_u16(rec->io_fault_mask);
      payload_write_u8(rec->output_enabled ? 1 : 0);
      payload_write_u16(rec->output_power);
      payload_write_u16(rec->output_rpm);
      payload_write_u16(rec->output_fault_mask);
      payload_write_u8(rec->jbc_addr);
      payload_write_u8(rec->station_addr);
      payload_write_u8(rec->jbc_link_flags);
      payload_write_u8(rec->jbc_work_mask);
      payload_write_u8(rec->jbc_stand_mask);
      payload_write_u8(rec->jbc_suction_level);
      payload_write_u16(rec->jbc_select_flow);
      payload_write_u16(rec->jbc_delay_work_sec);
      payload_write_u16(rec->jbc_delay_stand_sec);
      payload_write_u8(rec->jbc_stand_intakes);
      payload_write_u8(rec->jbc_continuous);
      payload_write_u8(rec->weller_speed_percent);
      payload_write_u8(rec->weller_filter_status);
      payload_write_u16(rec->weller_filter_runtime_minutes);
      payload_write_u16(rec->weller_programmed_filter_minutes);
      payload_write_u16(rec->weller_fan_rpm);
      payload_write_u16(rec->weller_version);
      payload_write_u8(rec->weller_work_light);
      payload_write_u16(rec->weller_uart_age_sec);
      payload_write_u8(name_len);
      payload_write_bytes(shown, name_len);
      payload_write_u8(device_id_len);
      payload_write_bytes(rec->jbc_device_id, device_id_len);

      // B5/v1: compact JBC USB core telemetry for the OFE displays.  Keep this
      // deliberately small: the full DLL-faithful diagnostics remain in the
      // Master/Web UI and are not pushed over the normal display detail path.
      if (detail_is_jbc_usb) {
        const JbcUsbCoreState core = jbc_usb_core_state(*rec);
        uint8_t ext[72];
        uint8_t ep = 0;
        auto ext_can = [&](uint8_t n) -> bool { return (uint16_t)ep + n <= sizeof(ext); };
        auto ext_u8 = [&](uint8_t v) -> bool { if (!ext_can(1)) return false; ext[ep++] = v; return true; };
        auto ext_u16 = [&](uint16_t v) -> bool { if (!ext_can(2)) return false; put_u16_le(ext + ep, v); ep += 2; return true; };
        auto pct8 = [](uint16_t permille) -> uint8_t {
          uint16_t pct = (uint16_t)((permille + 5U) / 10U);
          return pct > 100U ? 100U : (uint8_t)pct;
        };
        auto temp_c = [](uint16_t raw, bool valid) -> uint16_t { return valid ? (uint16_t)(raw / 9U) : 0xFFFFU; };

        ext_u8(0xB5);
        ext_u8(1); // extension version
        ext_u8(core.family);
        uint8_t core_flags = 0;
        if (core.linked) core_flags |= 0x01;
        if (core.work_active) core_flags |= 0x02;
        if (core.stand_active) core_flags |= 0x04;
        if (core.station_error_valid) core_flags |= 0x08;
        if (core.connect_mode_valid) core_flags |= 0x10;
        if (core.control_mode) core_flags |= 0x20;
        if (core.continuous_valid) core_flags |= 0x40;
        if (core.continuous_on) core_flags |= 0x80;
        ext_u8(core_flags);
        ext_u16(core.station_error_valid ? core.station_error : 0);
        ext_u8(core.port_count);
        uint8_t model_len = rec->jbc_usb_model[0] ? (uint8_t)strlen(rec->jbc_usb_model) : 0;
        if (model_len > 8) model_len = 8;
        ext_u8(model_len);
        if (model_len && ext_can(model_len)) { memcpy(ext + ep, rec->jbc_usb_model, model_len); ep += model_len; }

        if (core.family == JBC_USB_CORE_SOLD) {
          for (uint8_t i = 0; i < core.port_count && i < 4; ++i) {
            const JbcUsbCorePort& port = core.ports[i];
            ext_u8(port.valid ? 0x01 : 0);
            ext_u8(port.state);
            ext_u8(port.tool);
            ext_u8(port.error);
            ext_u16(temp_c(port.actual_temp, port.valid));
            ext_u16(temp_c(port.selected_temp, port.selected_temp_valid));
            ext_u8(pct8(port.power_permille));
          }
        } else if (core.family == JBC_USB_CORE_HA) {
          for (uint8_t i = 0; i < core.port_count && i < 4; ++i) {
            const JbcUsbCorePort& port = core.ports[i];
            ext_u8(port.valid ? 0x01 : 0);
            ext_u8(port.state);
            ext_u8(port.error);
            ext_u16(temp_c(port.actual_temp, port.valid));
            ext_u16(temp_c(port.selected_temp, port.selected_temp_valid));
            ext_u8(pct8(port.power_permille));
            ext_u8(pct8(port.flow_permille));
            ext_u16((uint16_t)((port.time_to_stop + 9U) / 10U));
          }
        } else if (core.family == JBC_USB_CORE_CL) {
          const JbcUsbCorePort& port = core.ports[0];
          ext_u8(port.mode);
          uint8_t f = 0;
          if (port.motors_valid) f |= 0x01;
          if (port.motors_on) f |= 0x02;
          if (port.door_valid) f |= 0x04;
          if (port.door_open) f |= 0x08;
          ext_u8(f);
        } else if (core.family == JBC_USB_CORE_PH) {
          for (uint8_t i = 0; i < 4; ++i) {
            ext_u16(temp_c(core.ph_tc[i].actual_temp, core.ph_tc[i].actual_valid));
            ext_u16(temp_c(core.ph_tc[i].selected_temp, core.ph_tc[i].selected_valid));
          }
          const JbcUsbCorePort& port = core.ports[0];
          uint8_t f = 0;
          if (port.heater_valid) f |= 0x01;
          if (port.heater_on) f |= 0x02;
          if (port.selected_power_valid) f |= 0x04;
          if (port.active_zones_valid) f |= 0x08;
          ext_u8(f);
          ext_u8(pct8(port.selected_power_permille));
          ext_u8(pct8(port.power_permille));
          ext_u8(port.active_zones);
          ext_u8(port.mode);
          ext_u16((uint16_t)((port.time_to_stop + 9U) / 10U));
        } else if (core.family == JBC_USB_CORE_FE) {
          const JbcUsbCorePort& port = core.ports[0];
          ext_u8(port.mode);
          uint8_t f = 0;
          if (port.intake_work_valid) f |= 0x01;
          if (port.intake_work_on) f |= 0x02;
          if (port.intake_stand_valid) f |= 0x04;
          if (port.intake_stand_on) f |= 0x08;
          ext_u8(f);
          ext_u16(port.fe_time_to_stop_work);
          ext_u16(port.fe_time_to_stop_stand);
        } else if (core.family == JBC_USB_CORE_SF) {
          const JbcUsbCorePort& port = core.ports[0];
          uint8_t f = 0;
          if (port.sf_feeding_valid) f |= 0x01;
          if (port.sf_feeding) f |= 0x02;
          if (port.sf_tool_enabled_valid) f |= 0x04;
          if (port.sf_tool_enabled) f |= 0x08;
          if (port.sf_speed_valid) f |= 0x10;
          if (port.sf_length_valid) f |= 0x20;
          ext_u8(f);
          ext_u8(port.sf_selected_program);
          ext_u16(port.sf_speed_tenth_mm_s);
          ext_u16(port.sf_length_tenth_mm);
          ext_u8(port.state);
        }
        if (payload_can_write(ep)) payload_write_bytes(ext, ep);
      }

      if ((rec->caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_DIGITAL_OUTPUT | CAP_INPUT_KEYS)) != 0) {
        const char* aliases[5] = {
          rec->io_main_alias,
          rec->io_in1_alias,
          rec->io_in2_alias,
          rec->io_out1_alias,
          rec->io_out2_alias
        };
        uint8_t alias_len[5] = {0, 0, 0, 0, 0};
        uint16_t alias_payload_len = 1;
        for (uint8_t i = 0; i < 5; ++i) {
          while (alias_len[i] < 18 && aliases[i] && aliases[i][alias_len[i]]) ++alias_len[i];
          alias_payload_len += 1U + alias_len[i];
        }
        if (payload_can_write(alias_payload_len)) {
          payload_write_u8(0xB4); // IO alias extension: main, IN1, IN2, OUT1, OUT2
          for (uint8_t i = 0; i < 5; ++i) {
            payload_write_u8(alias_len[i]);
            payload_write_bytes(aliases[i], alias_len[i]);
          }
        }
      }
      if (rec->addr == ADDR_MASTER) {
        String ip = master_ip_string_for_display();
        uint8_t ip_len = (uint8_t)ip.length();
        if (ip_len > 15) ip_len = 15;
        if (payload_can_write((uint16_t)2U + ip_len)) {
          payload_write_u8(0xB3); // Master detail extension: current IP text
          payload_write_u8(ip_len);
          payload_write_bytes(ip.c_str(), ip_len);
        }
      }
      if ((rec->caps & CAP_FILTER_SENSOR) && payload_can_write(11)) {
        payload_write_u8(0xB1);
        payload_write_u16(rec->fanio_filter_saturation_permille);
        payload_write_u16((uint16_t)rec->fanio_filter_pressure_raw);
        payload_write_u16((uint16_t)rec->fanio_filter_zero_raw);
        payload_write_u16((uint16_t)rec->fanio_filter_clean_raw);
        payload_write_u16((uint16_t)rec->fanio_filter_full_raw);
      }
      // B6/v1: friendly JBC USB per-port supplement.  B5/v1 stays wire-compatible
      // with older displays; old display firmware simply stops at this unknown trailing
      // marker after it has already consumed all legacy extensions.
      if (detail_is_jbc_usb) {
        const JbcUsbCoreState core = jbc_usb_core_state(*rec);
        const uint16_t ext_len = (uint16_t)4U + (uint16_t)core.port_count * 3U;
        if (payload_can_write(ext_len)) {
          payload_write_u8(0xB6);
          payload_write_u8(1); // extension version
          payload_write_u8(core.family);
          payload_write_u8(core.port_count);
          for (uint8_t i = 0; i < core.port_count && i < 4; ++i) {
            const JbcUsbCorePort& port = core.ports[i];
            payload_write_u8(port.valid ? 1U : 0U);
            payload_write_u8(port.tool);
            payload_write_u8(port.error);
          }
        }
      }
    }
  }
  Frame resp;
  if (!request(display_addr, CMD_DISPLAY_MODULE_DETAIL, payload, payload_len, resp, 60)) return false;
  return resp.cmd == (CMD_DISPLAY_MODULE_DETAIL | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

static uint8_t displayUniversalProfileEntityCount(const ModuleRecord& rec) {
  uint8_t total_entities = 0;
  if (rec.universal_descriptor_valid && rec.universal_descriptor[0]) {
    const char* scan = rec.universal_descriptor;
    while (scan && *scan) {
      const char* line = scan;
      const char* next = strchr(scan, '\n');
      char buf[220];
      size_t len = next ? (size_t)(next - line) : strlen(line);
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      memcpy(buf, line, len);
      buf[len] = 0;
      uint8_t id = 0;
      const char* type_start = nullptr;
      size_t type_len = 0;
      if (parse_universal_descriptor_line(buf, id, type_start, type_len) && id >= 20) ++total_entities;
      scan = next ? next + 1 : nullptr;
    }
  }
  return total_entities;
}

bool MasterScheduler::sendDisplayUniversalEntityPage(uint8_t display_addr, const ModuleRecord& rec, uint8_t start_entity) {
  if (rec.type != MODULE_UNIVERSAL_RS232 && rec.type != MODULE_MODBUS_RTU) return false;

  uint8_t total_entities = displayUniversalProfileEntityCount(rec);
  const char* scan = nullptr;
  if (start_entity >= total_entities) start_entity = 0;

  uint8_t payload[MAX_PAYLOAD];
  uint8_t payload_len = 0;
  auto payload_can_write = [&](uint16_t n) -> bool {
    return (uint16_t)payload_len + n <= MAX_PAYLOAD;
  };
  auto payload_write_u8 = [&](uint8_t value) -> bool {
    if (!payload_can_write(1)) return false;
    payload[payload_len++] = value;
    return true;
  };
  auto payload_write_u16 = [&](uint16_t value) -> bool {
    if (!payload_can_write(2)) return false;
    put_u16_le(payload + payload_len, value);
    payload_len += 2;
    return true;
  };
  auto payload_write_u32 = [&](uint32_t value) -> bool {
    if (!payload_can_write(4)) return false;
    put_u32_le(payload + payload_len, value);
    payload_len += 4;
    return true;
  };
  auto payload_write_bytes = [&](const void* data, uint8_t len) -> bool {
    if (!len) return true;
    if (!data || !payload_can_write(len)) return false;
    memcpy(payload + payload_len, data, len);
    payload_len += len;
    return true;
  };
  auto payload_reserve_u8 = [&](uint8_t& pos) -> bool {
    if (!payload_can_write(1)) return false;
    pos = payload_len;
    payload[payload_len++] = 0;
    return true;
  };

  auto descriptor_display_writable_control = [&](const char* descriptor_line, uint8_t type_code) -> bool {
    if (type_code != DISPLAY_UNI_SWITCH && type_code != DISPLAY_UNI_NUMBER &&
        type_code != DISPLAY_UNI_SELECT && type_code != DISPLAY_UNI_BUTTON && type_code != DISPLAY_UNI_TEXT) return false;
    char mode[4];
    if (descriptor_access_mode(descriptor_line, mode, sizeof(mode)))
      return strcmp(mode, "wo") == 0 || strcmp(mode, "rw") == 0;
    // Legacy descriptors without access metadata only: retain the old inference.
    if (type_code == DISPLAY_UNI_BUTTON) return true;
    char func[24] = {0};
    descriptor_key_value(descriptor_line, "func", func, sizeof(func));
    return contains_ci_ascii(func, "write_");
  };

  if (!payload_write_u8(2) || // detail-page format v2: adds select options string
      !payload_write_u8(rec.addr) ||
      !payload_write_u8(rec.type) ||
      !payload_write_u8((rec.online ? 0x01 : 0) | (rec.role_jbc ? 0x02 : 0) | (rec.role_output ? 0x04 : 0)) ||
      !payload_write_u32(rec.universal_descriptor_crc) ||
      !payload_write_u8(total_entities) ||
      !payload_write_u8(start_entity)) return false;
  uint8_t count_pos = 0;
  if (!payload_reserve_u8(count_pos)) return false;

  uint8_t written = 0;
  uint8_t entity_index = 0;
  bool full = false;
  bool emitted_after_start = false;

  for (uint8_t pass = 0; total_entities && pass < 2 && !full; ++pass) {
    scan = rec.universal_descriptor;
    while (scan && *scan) {
      const char* line = scan;
      const char* next = strchr(scan, '\n');
      char buf[1024];
      size_t len = next ? (size_t)(next - line) : strlen(line);
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      memcpy(buf, line, len);
      buf[len] = 0;
      scan = next ? next + 1 : nullptr;

      uint8_t id = 0;
      const char* type_start = nullptr;
      size_t type_len = 0;
      if (!parse_universal_descriptor_line(buf, id, type_start, type_len) || id < 20) continue;

      const uint8_t type_code = display_universal_type_code(type_start, type_len);
      const bool priority_control = descriptor_display_writable_control(buf, type_code);
      if ((pass == 0) != priority_control) continue;
      if (entity_index++ < start_entity) continue;
      emitted_after_start = true;

      uint8_t flags = 0;
      char access_mode[4];
      const bool has_access = descriptor_access_mode(buf, access_mode, sizeof(access_mode));
      if (has_access) {
        if (!strcmp(access_mode, "ro") || !strcmp(access_mode, "rw")) flags |= 0x01;
        if (!strcmp(access_mode, "wo") || !strcmp(access_mode, "rw")) flags |= 0x02;
      } else {
        // Backwards compatibility for old descriptors that predate explicit access.
        char func[24] = {0};
        char read_func[24] = {0};
        descriptor_key_value(buf, "func", func, sizeof(func));
        descriptor_key_value(buf, "read_func", read_func, sizeof(read_func));
        if (contains_ci_ascii(func, "read_") || contains_ci_ascii(read_func, "read_") ||
            type_code == DISPLAY_UNI_SENSOR || type_code == DISPLAY_UNI_BINARY_SENSOR || type_code == DISPLAY_UNI_TEXT) flags |= 0x01;
        if (contains_ci_ascii(func, "write_") || type_code == DISPLAY_UNI_BUTTON) flags |= 0x02;
      }

      const ModuleRecord* display = registry_.find(display_addr);
      const bool german = display && display->display_language == 1;
      char label[32];
      if (!descriptor_key_value(buf, german ? "de" : "en", label, sizeof(label))) {
        if (!descriptor_key_value(buf, german ? "en" : "de", label, sizeof(label))) {
          char id_s[8], type_s[18], key_s[32], mode_s[8];
          key_s[0] = 0;
          sscanf(buf, "%7s %17s %31s %7s", id_s, type_s, key_s, mode_s);
          snprintf(label, sizeof(label), "%s", key_s[0] ? key_s : "Entity");
        }
      }
      char unit[6];
      descriptor_key_value(buf, "unit", unit, sizeof(unit));
      const int16_t min_v = descriptor_i16_value(buf, "min", type_code == DISPLAY_UNI_NUMBER ? 0 : 0);
      const int16_t max_v = descriptor_i16_value(buf, "max", type_code == DISPLAY_UNI_NUMBER ? 100 : 0);
      int16_t step_v = descriptor_i16_value(buf, "step", 1);
      if (step_v <= 0) step_v = 1;
      const UniversalEntityState* state = (flags & 0x01) ? find_universal_entity_state(rec, id) : nullptr;
      int16_t value_v = universal_entity_numeric_value(state);
      if (type_code == DISPLAY_UNI_SWITCH || type_code == DISPLAY_UNI_BINARY_SENSOR) value_v = universalEntityBoolActive(rec, id) ? 1 : 0;
      if (type_code == DISPLAY_UNI_SELECT) value_v = descriptor_select_index_or_zero(buf, state);
      char state_text[32];
      descriptor_display_state_text(buf, type_code, state, state_text, sizeof(state_text));
      if (!(type_code == DISPLAY_UNI_TEXT || type_code == DISPLAY_UNI_SELECT || type_code == DISPLAY_UNI_BUTTON || type_code == DISPLAY_UNI_SENSOR || type_code == DISPLAY_UNI_BINARY_SENSOR)) {
        state_text[0] = 0;
      }
      uint8_t label_len = (uint8_t)strlen(label);
      if (label_len > 31) label_len = 31;
      uint8_t unit_len = (uint8_t)strlen(unit);
      if (unit_len > 5) unit_len = 5;
      uint8_t state_len = (uint8_t)strlen(state_text);
      if (state_len > 31) state_len = 31;
      char options_text[160];
      options_text[0] = 0;
      if (type_code == DISPLAY_UNI_SELECT) {
        descriptor_key_value(buf, "options", options_text, sizeof(options_text));
        if (!options_text[0]) descriptor_key_value(buf, "values", options_text, sizeof(options_text));
      }
      uint8_t options_len = (uint8_t)strlen(options_text);
      if (options_len > sizeof(options_text) - 1) options_len = sizeof(options_text) - 1;

      const uint16_t entry_base_len = (uint16_t)15U + label_len + unit_len + state_len;
      uint16_t entry_len = entry_base_len + options_len;
      if (!payload_can_write(entry_len)) {
        if (written) {
          // Finish this page and let the Display request this entity as the
          // first item of the next page, where the full frame is available.
          full = true;
          break;
        }
        // A single Select can carry a long option list. Preserve as many whole
        // options as fit rather than refusing the entity forever.
        const uint16_t room = (uint16_t)(MAX_PAYLOAD - payload_len);
        if (entry_base_len > room) { full = true; break; }
        uint16_t fit = room - entry_base_len;
        if (fit < options_len) {
          options_len = (uint8_t)fit;
          while (options_len && options_len < strlen(options_text) &&
                 options_text[options_len] != '|') --options_len;
          if (options_len && options_text[options_len] == '|') --options_len;
        }
        entry_len = entry_base_len + options_len;
      }
      payload_write_u8(id);
      payload_write_u8(type_code);
      payload_write_u8(flags);
      payload_write_u16((uint16_t)min_v);
      payload_write_u16((uint16_t)max_v);
      payload_write_u16((uint16_t)step_v);
      payload_write_u16((uint16_t)value_v);
      payload_write_u8(label_len);
      payload_write_bytes(label, label_len);
      payload_write_u8(unit_len);
      payload_write_bytes(unit, unit_len);
      payload_write_u8(state_len);
      payload_write_bytes(state_text, state_len);
      payload_write_u8(options_len);
      payload_write_bytes(options_text, options_len);
      ++written;
    }
  }

  // If the Display requested a stale start index and no entity could be emitted,
  // retry once from zero instead of sending an empty page forever.
  if (!written && start_entity && !emitted_after_start) return sendDisplayUniversalEntityPage(display_addr, rec, 0);
  payload[count_pos] = written;

  Frame resp;
  if (!request(display_addr, CMD_DISPLAY_DETAIL_PAGE, payload, payload_len, resp, 60)) return false;
  return resp.cmd == (CMD_DISPLAY_DETAIL_PAGE | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::sendDisplayUpdate(uint8_t addr, bool active, uint8_t target_addr, uint8_t progress,
                                        bool wait_for_ack) {
  if (progress > 100) progress = 100;
  uint8_t payload[64] = { active ? 1U : 0U, target_addr, progress };
  payload[3] = 0xFF; // Versioned extension marker.
  put_u32_le(payload + 4, display_update_speed_bps_);
  uint8_t len = 8;
  const ModuleRecord* target = registry_.find(target_addr);
  const char* name = target_addr == ADDR_MASTER ? master_name_ : nullptr;
  if (target) {
    if (target->label[0]) name = target->label;
    else if (target->name[0]) name = target->name;
  }
  if (name) {
    while (len < sizeof(payload) && *name) payload[len++] = (uint8_t)*name++;
  }

  if (!wait_for_ack) {
    // Module OTA owns the physical half-duplex bus. A WLAN display still needs
    // progress information, so inject one authenticated tunnel frame without
    // waiting for its reply. The WiFi route consumes the frame before UART.
    SchedulerBusLock bus_lock(bus_mutex_, 0);
    if (!bus_lock.locked || !master_display_wifi.active(addr)) return false;
    Frame req;
    req.dst = addr;
    req.src = ADDR_MASTER;
    req.seq = seq_++;
    req.cmd = CMD_DISPLAY_UPDATE;
    req.len = len;
    memcpy(req.payload, payload, len);
    link_.send(req);
    const bool sent_wirelessly = link_.lastTxWasNetwork();
    busDiagRecordTx(req);
    return sent_wirelessly;
  }

  Frame resp;
  if (!request(addr, CMD_DISPLAY_UPDATE, payload, len, resp, 40)) return false;
  return resp.cmd == (CMD_DISPLAY_UPDATE | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
}

void MasterScheduler::notifyDisplayUpdate(bool active, uint8_t target_addr, uint8_t progress, uint32_t speed_bps) {
  static uint32_t last_notify_ms = 0;
  static bool last_sent_active = false;
  static uint8_t last_sent_target = ADDR_INVALID;
  static uint8_t last_sent_progress = 255;
  static uint32_t last_sent_speed_bps = 0;

  display_update_active_ = active;
  display_update_target_ = target_addr;
  display_update_progress_ = progress > 100 ? 100 : progress;
  if (active) {
    display_update_speed_bps_ = speed_bps;
  } else if (display_update_progress_ >= 100) {
    // FW_END is emitted inside moduleFwEnd(), where the Web OTA layer cannot
    // pass its final sample. Preserve the last measured rate for the display's
    // five-second completed state unless an explicit final rate is supplied.
    if (speed_bps) display_update_speed_bps_ = speed_bps;
  } else {
    display_update_speed_bps_ = 0;
  }

  const uint32_t now = millis();
  bool should_send = false;
  if (!active) {
    should_send = last_sent_active || last_sent_target != target_addr || last_sent_progress != display_update_progress_;
  } else if (!last_sent_active || last_sent_target != target_addr || display_update_progress_ == 0 || display_update_progress_ >= 100) {
    should_send = true;
  } else if (display_update_progress_ < last_sent_progress || (uint8_t)(display_update_progress_ - last_sent_progress) >= 5) {
    should_send = true;
  } else if (display_update_speed_bps_ != last_sent_speed_bps && (uint32_t)(now - last_notify_ms) >= 250UL) {
    should_send = true;
  } else if ((uint32_t)(now - last_notify_ms) >= 750UL) {
    should_send = true;
  }
  if (!should_send) return;

  last_notify_ms = now;
  last_sent_active = active;
  last_sent_target = target_addr;
  last_sent_progress = display_update_progress_;
  last_sent_speed_bps = display_update_speed_bps_;

  for (uint8_t i = 0; i < registry_.count(); ++i) {
    ModuleRecord& rec = registry_.at(i);
    if (!rec.online || !(rec.caps & CAP_DISPLAY) || rec.addr == target_addr) continue;
    const bool wifi_during_module_ota = module_fw_active_ && master_display_wifi.active(rec.addr);
    // Wired displays can snoop the module OTA itself. Suppress their explicit
    // progress request while OTA is active, but keep WLAN displays informed via
    // a network-only, fire-and-forget frame.
    if (module_fw_active_ && !wifi_during_module_ota) continue;
    sendDisplayUpdate(rec.addr, display_update_active_, display_update_target_,
                      display_update_progress_, !wifi_during_module_ota);
  }
}

bool MasterScheduler::applyJbcSettingsToOnlineModules(const JbcModuleState& state) {
  setControlSettings(state.suction_level, state.select_flow, state.delay_work_sec, state.delay_stand_sec, state.stand_intakes != 0, state.continuous != 0);
  bool ok = true;
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    ModuleRecord& rec = registry_.at(i);
    if (!rec.online || !(rec.caps & CAP_JBC_BUS)) continue;
    if (!setJbcSettings(rec.addr,
      state.suction_level,
      state.select_flow,
      state.delay_work_sec,
      state.delay_stand_sec,
      state.stand_intakes != 0,
      state.continuous != 0)) ok = false;
  }
  return ok;
}

void MasterScheduler::handleDisplayEvent(uint8_t type, int16_t value, uint8_t display_view_arg) {
  if (type == DISPLAY_EVENT_NONE) return;

  if (type == DISPLAY_EVENT_OUTPUT_SELECT) {
    const uint8_t addr = (uint8_t)value;
    ModuleRecord* rec = addr ? registry_.find(addr) : nullptr;
    if (addr && (!rec || !rec->online || !moduleProvidesExtractorOutput(*rec))) return;
    setPreferredOutputAddr(addr);
    return;
  }

  if (type == DISPLAY_EVENT_MAIN_INPUT_SELECT) {
    const uint16_t encoded = (uint16_t)value;
    const uint8_t source_type = (uint8_t)((encoded >> 14) & 0x03);
    const uint8_t source_bit = (uint8_t)((encoded >> 8) & 0x3F);
    const uint8_t source_addr = (uint8_t)(encoded & 0xFF);
    setMainInputSource(source_type, source_addr, source_bit);
    return;
  }

  if (type == DISPLAY_EVENT_SUCTION_NEXT ||
      type == DISPLAY_EVENT_CUSTOM_POWER_DELTA ||
      type == DISPLAY_EVENT_DELAY_WORK_DELTA ||
      type == DISPLAY_EVENT_DELAY_STAND_DELTA ||
      type == DISPLAY_EVENT_STAND_INTAKES_TOGGLE ||
      type == DISPLAY_EVENT_CONTINUOUS_TOGGLE ||
      type == DISPLAY_EVENT_JBC_MODE_SET ||
      type == DISPLAY_EVENT_JBC_POWER_SET ||
      type == DISPLAY_EVENT_JBC_DELAY_WORK_SET ||
      type == DISPLAY_EVENT_JBC_DELAY_STAND_SET) {
    JbcModuleState state;
    if (have_jbc_settings_) copyDesiredJbcSettings(state);
    else state = extractor_.jbcState();

    if (type == DISPLAY_EVENT_SUCTION_NEXT) {
      state.suction_level = (state.suction_level >= 3) ? 0 : state.suction_level + 1;
      if (state.suction_level == 3 && state.select_flow < minSelectFlowForActiveOutput()) state.select_flow = minSelectFlowForActiveOutput();
    } else if (type == DISPLAY_EVENT_CUSTOM_POWER_DELTA) {
      state.suction_level = 3;
      uint16_t percent = state.select_flow >= 100 ? (state.select_flow + 5U) / 10U : state.select_flow;
      const uint16_t min_percent = minSelectFlowForActiveOutput() / 10U;
      if (percent < min_percent) percent = min_percent;
      if (percent > 100) percent = 100;
      percent = clamp_u16_i32((int32_t)percent + value, min_percent, 100);
      state.select_flow = percent * 10;
    } else if (type == DISPLAY_EVENT_DELAY_WORK_DELTA) {
      state.delay_work_sec = clamp_u16_i32((int32_t)state.delay_work_sec + value, 0, 600);
    } else if (type == DISPLAY_EVENT_DELAY_STAND_DELTA) {
      state.delay_stand_sec = clamp_u16_i32((int32_t)state.delay_stand_sec + value, 0, 600);
    } else if (type == DISPLAY_EVENT_STAND_INTAKES_TOGGLE) {
      state.stand_intakes = !state.stand_intakes;
    } else if (type == DISPLAY_EVENT_CONTINUOUS_TOGGLE) {
      state.continuous = !state.continuous;
    } else if (type == DISPLAY_EVENT_JBC_MODE_SET) {
      state.suction_level = value < 0 ? 0 : (value > 3 ? 3 : (uint8_t)value);
      if (state.suction_level == 3 && state.select_flow < minSelectFlowForActiveOutput()) state.select_flow = minSelectFlowForActiveOutput();
    } else if (type == DISPLAY_EVENT_JBC_POWER_SET) {
      state.suction_level = 3;
      state.select_flow = clamp_u16_i32(value, minSelectFlowForActiveOutput() / 10U, 100) * 10U;
    } else if (type == DISPLAY_EVENT_JBC_DELAY_WORK_SET) {
      state.delay_work_sec = clamp_u16_i32(value, 0, 600);
    } else if (type == DISPLAY_EVENT_JBC_DELAY_STAND_SET) {
      state.delay_stand_sec = clamp_u16_i32(value, 0, 600);
    }
    applyJbcSettingsToOnlineModules(state);
    persistControlSettingsNow();
    return;
  }

  if (type == DISPLAY_EVENT_AFTER_POWER_SET) {
    const uint16_t min_percent = minSelectFlowForActiveOutput() / 10U;
    const uint16_t percent = clamp_u16_i32(value, min_percent, 100);
    setAfterrunPowerProfile(extractor_.afterrunPowerProfileEnabled(), percent * 10U);
    return;
  }

  if (type == DISPLAY_EVENT_AFTER_POWER_TOGGLE) {
    setAfterrunPowerProfile(!extractor_.afterrunPowerProfileEnabled(), extractor_.afterrunPower());
    return;
  }
  if (type == DISPLAY_EVENT_UNIVERSAL_ENTITY_TOGGLE ||
      type == DISPLAY_EVENT_UNIVERSAL_ENTITY_BUTTON ||
      type == DISPLAY_EVENT_UNIVERSAL_ENTITY_VALUE_SET ||
      type == DISPLAY_EVENT_UNIVERSAL_ENTITY_SELECT_SET) {
    ModuleRecord* rec = registry_.find(display_view_arg);
    if (!rec || !rec->online ||
        (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU) ||
        !(rec->caps & CAP_ENTITY_CONTROL)) return;
    uint8_t entity_id = 0;
    char text[10];
    if (type == DISPLAY_EVENT_UNIVERSAL_ENTITY_VALUE_SET || type == DISPLAY_EVENT_UNIVERSAL_ENTITY_SELECT_SET) {
      const uint16_t encoded_entity = (uint16_t)value;
      entity_id = (uint8_t)((encoded_entity >> 8) & 0xFF);
      const uint8_t set_value = (uint8_t)(encoded_entity & 0xFF);
      if (type == DISPLAY_EVENT_UNIVERSAL_ENTITY_SELECT_SET) {
        char line[1024];
        if (!descriptor_line_for_entity(*rec, entity_id, line, sizeof(line)) ||
            !descriptor_select_value_for_index(line, set_value, text, sizeof(text))) {
          return;
        }
      } else {
        snprintf(text, sizeof(text), "%u", set_value);
      }
    } else {
      entity_id = (uint8_t)value;
      if (type == DISPLAY_EVENT_UNIVERSAL_ENTITY_BUTTON) {
        snprintf(text, sizeof(text), "1");
      } else {
        const bool active = universalEntityBoolActive(*rec, entity_id);
        snprintf(text, sizeof(text), active ? "0" : "1");
      }
    }
    if (!entity_id) return;
    setUniversalEntity(rec->addr, entity_id, (const uint8_t*)text, (uint8_t)strlen(text));
    return;
  }

  const uint16_t encoded = (uint16_t)value;
  const uint8_t requested_addr = (uint8_t)(encoded >> 8);
  const int8_t requested_value = (int8_t)(encoded & 0xFF);
  ModuleRecord* out = registry_.find(requested_addr ? requested_addr : active_output_addr_);
  if (!out || !out->online) return;

  if (type == DISPLAY_EVENT_MODULE_OUTPUT_POWER_SET) {
    if (!moduleProvidesExtractorOutput(*out)) return;
    const uint16_t power = (uint16_t)clamp_u16_i32(requested_value, 10, 100) * 10U;
    setModulePower(out->addr, power);
    return;
  }

  if (type == DISPLAY_EVENT_MODULE_OUTPUT_TOGGLE) {
    if (!moduleProvidesExtractorOutput(*out)) return;
    uint16_t power = out->output_power;
    if (power < 100) power = 100;
    setModuleOutput(out->addr, !out->output_enabled, power);
    return;
  }

  if (type == DISPLAY_EVENT_WELLER_SPEED_SET) {
    if (!(out->caps & CAP_WELLER_INTERFACE)) return;
    setWellerSpeed(out->addr, clamp_u16_i32(requested_value, 30, 100));
    return;
  }
  if (type == DISPLAY_EVENT_WELLER_SPEED_DELTA) {
    if (!(out->caps & CAP_WELLER_INTERFACE)) return;
    uint8_t percent = out->weller_speed_percent ? out->weller_speed_percent : (extractor_.outputPower() / 10);
    percent = clamp_u16_i32((int32_t)percent + requested_value, 30, 100);
    setWellerSpeed(out->addr, percent);
    return;
  }

  if (type == DISPLAY_EVENT_WELLER_FAN_TOGGLE) {
    const uint16_t mask = 0x0001;
    setIoOutput(out->addr, mask, (out->io_output_mask & mask) ? 0 : mask);
    return;
  }

  if (type == DISPLAY_EVENT_WELLER_LIGHT_TOGGLE) {
    const uint16_t mask = 0x0002;
    setIoOutput(out->addr, mask, (out->io_output_mask & mask) ? 0 : mask);
    return;
  }

  if (type == DISPLAY_EVENT_WELLER_RESET_FILTER) {
    if (out->caps & CAP_WELLER_INTERFACE) resetWellerFilter(out->addr);
    return;
  }

  if (type == DISPLAY_EVENT_WELLER_FILTER_TIME_NEXT) {
    if (!(out->caps & CAP_WELLER_INTERFACE)) return;
    static const uint16_t presets[] = {60, 120, 240, 480, 720, 1440, 2880, 4320, 5760, 7200, 8640, 9600};
    uint16_t next = presets[0];
    for (uint8_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
      if (out->weller_programmed_filter_minutes < presets[i]) {
        next = presets[i];
        break;
      }
      if (i == (sizeof(presets) / sizeof(presets[0])) - 1) next = presets[0];
    }
    setWellerFilterRuntime(out->addr, next);
    return;
  }

  if (type == DISPLAY_EVENT_WELLER_FILTER_TIME_SET) {
    if (!(out->caps & CAP_WELLER_INTERFACE)) return;
    const uint8_t preset = (uint8_t)requested_value;
    uint16_t minutes = 0;
    if (preset < 23) minutes = (uint16_t)(preset + 1U) * 60U;
    else if (preset < 29) minutes = (uint16_t)(preset - 22U) * 1440U;
    else if (preset == 29) minutes = 9600U;
    if (minutes) setWellerFilterRuntime(out->addr, minutes);
    return;
  }
  if (type == DISPLAY_EVENT_IO_OUT_TOGGLE) {
    const uint8_t bit = requested_value < 0 ? 0 : (uint8_t)requested_value;
    if (!(out->caps & CAP_DIGITAL_OUTPUT) || bit > 15) return;
    const uint16_t mask = (uint16_t)(1U << bit);
    setIoOutput(out->addr, mask, (out->io_output_mask & mask) ? 0 : mask);
  }
}

void MasterScheduler::pushDisplayStatus() {
  // RS485 firmware update owns the half-duplex bus. Normal display-status polls
  // during OTA can delay or collide with target-module ACKs.
  if (module_fw_active_ || display_update_active_) return;

  const uint8_t count = registry_.count();
  for (uint8_t tries = 0; tries < count; ++tries) {
    if (next_display_index_ >= count) next_display_index_ = 0;
    ModuleRecord& rec = registry_.at(next_display_index_++);
    if (!rec.online || !(rec.caps & CAP_DISPLAY)) continue;
    if (display_update_active_ && rec.addr == display_update_target_) continue;
    sendDisplayStatus(rec.addr);
    return;
  }
}

void MasterScheduler::setAfterrunPowerProfile(bool enabled, uint16_t power, bool persist) {
  if (power > 1000) power = 1000;
  const uint16_t min_power = minSelectFlowForActiveOutput();
  if (power < min_power) power = min_power;
  extractor_.setAfterrunPowerProfile(enabled, power);
  extractor_.markOutputDirty();
  if (persist) {
    Preferences prefs;
    MasterSettingsStore::saveAfterrunPower(prefs, enabled, power);
  }
}
void MasterScheduler::setControlSettings(uint8_t suction, uint16_t select_flow, uint16_t delay_work, uint16_t delay_stand, bool stand_intakes, bool continuous, bool persist) {
  JbcModuleState state;
  state.suction_level = suction > 3 ? 3 : suction;
  state.select_flow = select_flow > 1000 ? 1000 : select_flow;
  const uint16_t min_select_flow = minSelectFlowForActiveOutput();
  if (state.select_flow < min_select_flow) state.select_flow = min_select_flow;
  state.delay_work_sec = delay_work;
  state.delay_stand_sec = delay_stand;
  state.stand_intakes = stand_intakes ? 1 : 0;
  state.continuous = continuous ? 1 : 0;
  adoptJbcSettings(state);
  extractor_.updateControlSettings(state);
  for (uint8_t i = 0; i < registry_.count(); ++i) {
    ModuleRecord& rec = registry_.at(i);
    if (rec.caps & CAP_JBC_BUS) rememberJbcSettings(rec, state);
  }
  updateJbcAggregate();
  if (persist) scheduleControlSettingsPersist();
}

void MasterScheduler::scheduleControlSettingsPersist() {
  const uint32_t now = millis();
  if (!control_persist_dirty_) control_persist_first_dirty_ms_ = now;
  control_persist_dirty_ = true;
  control_persist_due_ms_ = now + 3000UL;
  const uint32_t max_due = control_persist_first_dirty_ms_ + 30000UL;
  if ((int32_t)(control_persist_due_ms_ - max_due) > 0) control_persist_due_ms_ = max_due;
}

void MasterScheduler::flushControlSettingsPersist(bool force) {
  if (!control_persist_dirty_) return;
  const uint32_t now = millis();
  if (!force && (int32_t)(now - control_persist_due_ms_) < 0) return;
  MasterSettingsStore::ControlSettings s;
  s.suction = desired_jbc_settings_.suction_level;
  s.select_flow = desired_jbc_settings_.select_flow;
  s.delay_work_sec = desired_jbc_settings_.delay_work_sec;
  s.delay_stand_sec = desired_jbc_settings_.delay_stand_sec;
  s.stand_intakes = desired_jbc_settings_.stand_intakes != 0;
  s.afterrun_power_enabled = extractor_.afterrunPowerProfileEnabled();
  s.afterrun_power = extractor_.afterrunPower();
  Preferences prefs;
  if (MasterSettingsStore::saveControl(prefs, s)) {
    control_persist_dirty_ = false;
    control_persist_first_dirty_ms_ = 0;
  }
}

void MasterScheduler::persistControlSettingsNow() {
  if (!control_persist_dirty_) scheduleControlSettingsPersist();
  flushControlSettingsPersist(true);
}
bool MasterScheduler::setJbcSettings(uint8_t addr, uint8_t suction, uint16_t select_flow, uint16_t delay_work, uint16_t delay_stand, bool stand_intakes, bool continuous) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  uint8_t payload[16];
  payload[0] = suction > 3 ? 3 : suction;
  if (select_flow > 1000) select_flow = 1000;
  put_u16_le(payload + 1, select_flow);
  put_u16_le(payload + 3, delay_work);
  put_u16_le(payload + 5, delay_stand);
  payload[7] = stand_intakes ? 1 : 0;
  payload[8] = continuous ? 1 : 0;
  put_u16_le(payload + 9, systemJbcError());
  put_u16_le(payload + 11, systemJbcFilterLife());
  put_u16_le(payload + 13, systemJbcFilterSaturation());
  bool extractor_output_on = extractor_.outputEnabled();
  const ModuleRecord* output = registry_.find(active_output_addr_);
  if (output && output->online && output->output_status_valid) extractor_output_on = output->output_enabled;
  payload[15] = extractor_output_on ? 1U : 0U;
  Frame resp;
  if (!request(addr, CMD_SET_STATE, payload, sizeof(payload), resp, 100)) return false;
  const bool ok = resp.cmd == (CMD_SET_STATE | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) {
    JbcModuleState state;
    state.suction_level = payload[0];
    state.select_flow = select_flow;
    state.delay_work_sec = delay_work;
    state.delay_stand_sec = delay_stand;
    state.stand_intakes = payload[7];
    state.continuous = payload[8];
    state.stat_error = get_u16_le(payload + 9);
    state.filter_life = get_u16_le(payload + 11);
    state.filter_sat = get_u16_le(payload + 13);
    adoptJbcSettings(state);
    ModuleRecord* rec = registry_.find(addr);
    if (rec) rememberJbcSettings(*rec, state);
  }
  return ok;
}

bool MasterScheduler::setIoOutput(uint8_t addr, uint16_t mask, uint16_t value) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_DIGITAL_OUTPUT)) return false;
  uint8_t payload[4];
  put_u16_le(payload, mask);
  put_u16_le(payload + 2, value);
  Frame resp;
  if (!request(addr, CMD_SET_IO, payload, sizeof(payload), resp, 50)) return false;
  const bool ok = resp.cmd == (CMD_SET_IO | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) readIoStatus(addr);
  return ok;
}

bool MasterScheduler::setJbcUsbStationName(uint8_t addr, const char* name) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online ||
      !(rec->type == MODULE_JBC_USB || (rec->caps & CAP_JBC_USB)) || !name) return false;

  uint8_t len = 0;
  while (name[len] && len < 17U) ++len;
  if (len > 16U) return false;
  static const char allowed[] = " 0123456789QWERTYUIOPASDFGHJKLMNBVCXZ'!?$%&@-=,.;()[]";
  for (uint8_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)name[i];
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c == 0U || c >= 0x80U || !strchr(allowed, c)) return false;
  }

  uint8_t payload[17];
  payload[0] = JBC_USB_CONFIG_STATION_NAME;
  if (len) memcpy(payload + 1, name, len);
  Frame resp;
  if (!request(addr, CMD_JBC_USB_CONFIG, payload, (uint8_t)(len + 1U), resp, 180)) return false;
  return resp.cmd == (CMD_JBC_USB_CONFIG | 0x80) &&
         resp.len >= 1U && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::setJbcUsbConfig(uint8_t addr, const uint8_t* data, uint8_t len) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online ||
      !(rec->type == MODULE_JBC_USB || (rec->caps & CAP_JBC_USB)) ||
      !data || len < 1U || len > MAX_PAYLOAD) return false;
  Frame resp;
  if (!request(addr, CMD_JBC_USB_CONFIG, data, len, resp, 220)) return false;
  return resp.cmd == (CMD_JBC_USB_CONFIG | 0x80) &&
         resp.len >= 1U && resp.payload[0] == STATUS_OK;
}

bool MasterScheduler::setIoAlias(uint8_t addr, uint8_t channel, const char* alias) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & (CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT))) return false;
  if (channel > 4) return false;
  uint8_t payload[20];
  payload[0] = channel;
  uint8_t n = 0;
  if (alias) {
    while (alias[n] && n < 18) ++n;
    memcpy(payload + 1, alias, n);
  }
  Frame resp;
  if (!request(addr, CMD_IO_LABEL, payload, (uint8_t)(n + 1), resp, 120)) return false;
  const bool ok = resp.cmd == (CMD_IO_LABEL | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) readIoStatus(addr, true);
  return ok;
}
bool MasterScheduler::setModulePower(uint8_t addr, uint16_t power) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !moduleProvidesExtractorOutput(*rec)) return false;
  if ((rec->type == MODULE_UNIVERSAL_RS232 || rec->type == MODULE_MODBUS_RTU)) return sendOutputPower(addr, power);
  if (rec->caps & CAP_WELLER_INTERFACE) return false;
  if (power > 1000) power = 1000;
  if (power < 100) power = 100;
  const bool ok = sendOutputPower(addr, power);
  if (ok) readOutputStatus(addr);
  return ok;
}

bool MasterScheduler::setModuleOutput(uint8_t addr, bool enabled, uint16_t power) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !moduleProvidesExtractorOutput(*rec)) return false;
  if (power > 1000) power = 1000;
  if ((rec->type == MODULE_UNIVERSAL_RS232 || rec->type == MODULE_MODBUS_RTU)) {
    return universalSetMainOutput(*rec, enabled, power);
  }
  if (!(rec->caps & CAP_WELLER_INTERFACE)) {
    if (enabled && power < 100) power = 100;
  }
  const bool power_ok = sendOutputPower(addr, power);
  const bool enable_ok = sendOutputEnable(addr, enabled);
  const bool ok = power_ok && enable_ok;
  if (ok) {
    readOutputStatus(addr);
    if (rec->caps & (CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT)) readIoStatus(addr);
  }
  return ok;
}

bool MasterScheduler::setWellerSpeed(uint8_t addr, uint8_t percent) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_WELLER_INTERFACE)) return false;
  if (percent < 30) percent = 30;
  if (percent > 100) percent = 100;
  uint8_t payload[2] = { 1, percent };
  Frame resp;
  if (!request(addr, CMD_SET_OUTPUT, payload, sizeof(payload), resp, 50)) return false;
  const bool ok = resp.cmd == (CMD_SET_OUTPUT | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) readTelemetry(addr);
  return ok;
}

bool MasterScheduler::resetWellerFilter(uint8_t addr) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_WELLER_INTERFACE)) return false;
  uint8_t payload[1] = { 2 };
  Frame resp;
  if (!request(addr, CMD_SET_OUTPUT, payload, sizeof(payload), resp, 50)) return false;
  const bool ok = resp.cmd == (CMD_SET_OUTPUT | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) readTelemetry(addr);
  return ok;
}

bool MasterScheduler::setWellerFilterRuntime(uint8_t addr, uint16_t minutes) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_WELLER_INTERFACE)) return false;
  if (minutes < 60U) minutes = 60U;
  if (minutes > 9990U) minutes = 9990U;
  uint8_t payload[3];
  payload[0] = 3;
  put_u16_le(payload + 1, minutes);
  Frame resp;
  if (!request(addr, CMD_SET_OUTPUT, payload, sizeof(payload), resp, 50)) return false;
  const bool ok = resp.cmd == (CMD_SET_OUTPUT | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) readTelemetry(addr);
  return ok;
}

bool MasterScheduler::calibrateFanIoProFilter(uint8_t addr, uint8_t action, uint16_t warn_raw, uint16_t full_raw) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_FILTER_SENSOR)) return false;
  uint8_t payload[5];
  uint8_t len = 1;
  payload[0] = action;
  if (action == 3) {
    put_u16_le(payload + 1, warn_raw);
    put_u16_le(payload + 3, full_raw);
    len = 5;
  } else if (action == 5 || action == 6) {
    payload[1] = warn_raw ? 1U : 0U;
    len = 2;
  }
  Frame resp;
  if (!request(addr, CMD_FILTER_CALIBRATION, payload, len, resp, 120)) return false;
  const bool ok = resp.cmd == (CMD_FILTER_CALIBRATION | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (ok) readTelemetry(addr);
  return ok;
}
bool MasterScheduler::setDisplaySettings(uint8_t addr, uint8_t brightness, uint8_t language, uint8_t theme, uint8_t screensaver_min) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || !(rec->caps & CAP_DISPLAY)) return false;

  const bool screensaver_requested = screensaver_min != 0xFF;
  const bool screensaver_valid = !screensaver_requested ||
    screensaver_min == 0 || screensaver_min == 1 || screensaver_min == 2 ||
    screensaver_min == 5 || screensaver_min == 10;
  if (!screensaver_valid) return false;

  uint8_t payload[4] = {
    brightness > 100 ? 0xFF : (brightness < 10 ? 10 : brightness),
    language > 1 ? 0xFF : language,
    theme > 1 ? 0xFF : theme,
    screensaver_requested ? screensaver_min : 0xFF
  };

  Frame resp;
  if (!request(addr, CMD_SET_STATE, payload, sizeof(payload), resp, 100)) return false;
  bool ok = resp.cmd == (CMD_SET_STATE | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;

  // Older display firmwares only understood brightness/language/theme (3 bytes).
  // Never use that compatibility fallback when the caller explicitly changes
  // the screensaver time; otherwise the Web UI can report success although the
  // Display never received the fourth byte.
  if (!ok && !screensaver_requested && resp.cmd == (CMD_SET_STATE | 0x80) &&
      resp.len >= 1 && resp.payload[0] == STATUS_BAD_LEN) {
    if (!request(addr, CMD_SET_STATE, payload, 3, resp, 100)) return false;
    ok = resp.cmd == (CMD_SET_STATE | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  }

  if (ok) {
    // New display firmwares echo the applied values as:
    // [STATUS_OK, brightness, language, theme, screensaver_min]. Use the echo
    // when present so the registry mirrors the exact setting accepted by the
    // Display. Fall back to the request payload for older 3-byte responses.
    const uint8_t applied_brightness = resp.len >= 5 ? resp.payload[1] : payload[0];
    const uint8_t applied_language = resp.len >= 5 ? resp.payload[2] : payload[1];
    const uint8_t applied_theme = resp.len >= 5 ? resp.payload[3] : payload[2];
    const uint8_t applied_screensaver = resp.len >= 5 ? resp.payload[4] : payload[3];

    if (applied_brightness != 0xFF) rec->display_brightness_pct = applied_brightness;
    if (applied_language != 0xFF) rec->display_language = applied_language;
    if (applied_theme != 0xFF) rec->display_theme = applied_theme;
    if (applied_screensaver != 0xFF) rec->display_screensaver_min = applied_screensaver;
    readTelemetry(addr);
  }
  return ok;
}

bool MasterScheduler::setUniversalProfile(uint8_t addr, const char* profile, const char* station, uint32_t baud, const char* frame, const char* protocol, const char* checksum, const char* line_end, const char* profile_text) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) return false;
  if (!profile) profile = "";
  if (!station) station = "";
  if (!frame) frame = "8N1";
  if (!protocol) protocol = "ASCII";
  if (!checksum) checksum = "NONE";
  if (!line_end) line_end = "CR";
  if (baud < 300UL || baud > 1000000UL) return false;

  char payload[MAX_PAYLOAD];
  const int n = snprintf(payload, sizeof(payload),
                         "profile=%s\nstation=%s\nbaud=%lu\nframe=%s\nprotocol=%s\nchecksum=%s\nline_end=%s\n",
                         profile, station, (unsigned long)baud, frame, protocol, checksum, line_end);
  if (n <= 0 || n >= (int)sizeof(payload)) return false;

  Frame resp;
  bool ok = false;
  for (uint8_t attempt = 0; attempt < 2 && !ok; ++attempt) {
    if (request(addr, CMD_SAVE_CONFIG, (const uint8_t*)payload, (uint8_t)n, resp, 1500)) {
      ok = resp.cmd == (CMD_SAVE_CONFIG | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
    }
    if (!ok) delay(20);
  }
  if (ok && profile_text) {
    ok = uploadUniversalProfileText(addr, profile_text);
  }
  if (ok) {
    readInfo(addr);
    readTelemetry(addr);
    rec->universal_descriptor_valid = false;
    rec->universal_descriptor_crc = 0;
    rec->universal_descriptor_chunks = 0;
    rec->universal_descriptor[0] = 0;
    rec->universal_entities_valid = false;
    rec->universal_entity_count = 0;
    memset(rec->universal_entities, 0, sizeof(rec->universal_entities));
    refreshUniversalDescriptor(addr, true);
    readUniversalEntities(addr);
  }
  return ok;
}

bool MasterScheduler::uploadUniversalProfileText(uint8_t addr, const char* profile_text) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) return false;
  if (!profile_text) return false;
  const size_t total = strlen(profile_text);
  if (total > 8192U) return false;
  const uint32_t crc = fnv1a32_local((const uint8_t*)profile_text, total);

  uint8_t begin_payload[8];
  put_u32_le(begin_payload, (uint32_t)total);
  put_u32_le(begin_payload + 4, crc);
  Frame resp;
  if (!request(addr, CMD_PROFILE_BEGIN, begin_payload, sizeof(begin_payload), resp, 750)) {
    Serial.printf("[PROFILE] BEGIN timeout addr=0x%02X bytes=%u\n", addr, (unsigned)total);
    return false;
  }
  if (resp.cmd != (CMD_PROFILE_BEGIN | 0x80) || resp.len < 1 || resp.payload[0] != STATUS_OK) {
    Serial.printf("[PROFILE] BEGIN rejected addr=0x%02X status=%u\n", addr, resp.len ? resp.payload[0] : 255U);
    return false;
  }

  size_t offset = 0;
  while (offset < total) {
    uint8_t payload[MAX_PAYLOAD];
    const size_t chunk = min((size_t)120, total - offset);
    put_u16_le(payload, (uint16_t)offset);
    memcpy(payload + 2, profile_text + offset, chunk);
    if (!request(addr, CMD_PROFILE_CHUNK, payload, (uint8_t)(chunk + 2), resp, 750)) {
      Serial.printf("[PROFILE] CHUNK timeout addr=0x%02X offset=%u/%u\n", addr, (unsigned)offset, (unsigned)total);
      return false;
    }
    if (resp.cmd != (CMD_PROFILE_CHUNK | 0x80) || resp.len < 1 || resp.payload[0] != STATUS_OK) {
      Serial.printf("[PROFILE] CHUNK rejected addr=0x%02X offset=%u status=%u\n", addr, (unsigned)offset, resp.len ? resp.payload[0] : 255U);
      return false;
    }
    offset += chunk;
    delay(4);
  }

  // PROFILE_END performs full profile parsing, NVS persistence and UART restart
  // on the module before it acknowledges. Larger 7+ entity profiles can exceed
  // the old 1200 ms window even though the module is functioning correctly.
  if (!request(addr, CMD_PROFILE_END, nullptr, 0, resp, 5000)) {
    Serial.printf("[PROFILE] END timeout addr=0x%02X bytes=%u\n", addr, (unsigned)total);
    return false;
  }
  if (resp.cmd != (CMD_PROFILE_END | 0x80) || resp.len < 1 || resp.payload[0] != STATUS_OK) {
    Serial.printf("[PROFILE] END rejected addr=0x%02X status=%u\n", addr, resp.len ? resp.payload[0] : 255U);
    return false;
  }
  Serial.printf("[PROFILE] upload OK addr=0x%02X bytes=%u\n", addr, (unsigned)total);
  return true;
}

bool MasterScheduler::readUniversalProfileText(uint8_t addr, char* out, size_t out_len, uint32_t* out_crc, bool* out_truncated) {
  if (out && out_len) out[0] = 0;
  if (out_crc) *out_crc = 0;
  if (out_truncated) *out_truncated = false;
  if (module_fw_active_ && addr == module_fw_target_) return false;
  if (!out || out_len < 2) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU)) return false;

  size_t total = 0;
  uint32_t crc = 0;
  uint8_t chunk_count = 1;
  bool truncated = false;

  for (uint8_t chunk = 0; chunk < chunk_count && chunk < 64; ++chunk) {
    Frame resp;
    const uint8_t payload[1] = { chunk };
    bool ok = false;
    for (uint8_t attempt = 0; attempt < 3 && !ok; ++attempt) {
      ok = request(addr, CMD_PROFILE_GET, payload, 1, resp, chunk == 0 ? 900 : 450);
      if (!ok) delay(4);
    }
    if (!ok) { truncated = true; break; }
    if (resp.cmd != (CMD_PROFILE_GET | 0x80) || resp.len < 8 || resp.payload[0] != STATUS_OK) { truncated = true; break; }
    const uint8_t schema = resp.payload[1];
    if (schema != 1) { truncated = true; break; }
    const uint32_t this_crc = get_u32_le(resp.payload + 2);
    const uint8_t resp_chunk = resp.payload[6];
    const uint8_t resp_count = resp.payload[7] ? resp.payload[7] : 1;
    if (resp_chunk != chunk) { truncated = true; break; }
    if (chunk == 0) {
      crc = this_crc;
      chunk_count = resp_count;
      if (chunk_count > 64) { truncated = true; chunk_count = 64; }
    } else if (this_crc != crc || resp_count != chunk_count) {
      truncated = true;
      break;
    }
    size_t n = resp.len - 8;
    if (total + n >= out_len) {
      n = out_len - 1 - total;
      truncated = true;
    }
    if (n) {
      memcpy(out + total, resp.payload + 8, n);
      total += n;
      out[total] = 0;
    }
    delay(2);
  }

  if (out_crc) *out_crc = crc;
  if (out_truncated) *out_truncated = truncated;
  return total > 0 || !truncated;
}

bool MasterScheduler::refreshUniversalDescriptor(uint8_t addr, bool force) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online ||
      (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU) ||
      !(rec->caps & CAP_DESCRIPTOR)) return false;

  const uint32_t now = millis();
  if (!force && rec->universal_descriptor_valid &&
      (uint32_t)(now - rec->universal_descriptor_last_ms) < 60000UL) return true;

  // v1.7.65 stack fix:
  // The old code placed a 2048-byte temporary descriptor on loopTask's stack
  // and kept it alive while selectRoles()/updateInputRouting() executed.
  // The ModuleRecord already owns an equally-sized descriptor cache, so receive
  // directly into that cache instead of duplicating it on the stack.
  const bool was_valid = rec->universal_descriptor_valid;
  const uint32_t old_crc = rec->universal_descriptor_crc;
  char* text = rec->universal_descriptor;
  const size_t text_capacity = sizeof(rec->universal_descriptor);
  text[0] = 0;

  size_t total = 0;
  uint32_t crc = 0;
  uint8_t chunk_count = 1;
  bool truncated = false;

  for (uint8_t chunk = 0; chunk < chunk_count && chunk < 128; ++chunk) {
    Frame resp;
    const uint8_t payload[1] = { chunk };
    bool ok = false;
    for (uint8_t attempt = 0; attempt < 3 && !ok; ++attempt) {
      ok = request(addr, CMD_DESCRIPTOR_GET, payload, 1, resp,
                   chunk == 0 ? 900 : 400);
      if (!ok) delay(4);
    }
    if (!ok) {
      truncated = true;
      break;
    }
    if (resp.cmd != (CMD_DESCRIPTOR_GET | 0x80) ||
        resp.len < 8 ||
        resp.payload[0] != STATUS_OK) {
      truncated = true;
      break;
    }

    const uint8_t schema = resp.payload[1];
    if (schema != 1) {
      truncated = true;
      break;
    }

    const uint32_t this_crc = get_u32_le(resp.payload + 2);
    const uint8_t resp_chunk = resp.payload[6];
    const uint8_t resp_count = resp.payload[7] ? resp.payload[7] : 1;
    if (resp_chunk != chunk) {
      truncated = true;
      break;
    }

    if (chunk == 0) {
      crc = this_crc;
      chunk_count = resp_count;
      if (chunk_count > 128) {
        truncated = true;
        chunk_count = 128;
      }
    } else if (this_crc != crc || resp_count != chunk_count) {
      truncated = true;
      break;
    }

    size_t n = resp.len - 8;
    if (total + n >= text_capacity) {
      n = text_capacity - 1 - total;
      truncated = true;
    }
    if (n) {
      memcpy(text + total, resp.payload + 8, n);
      total += n;
      text[total] = 0;
    }
    delay(2);

    if (total + 1 >= text_capacity) break;
  }

  normalize_universal_descriptor_text(text, text_capacity);

  rec->universal_descriptor_crc = crc;
  rec->universal_descriptor_chunks = chunk_count;
  rec->universal_descriptor_valid = total > 0;
  rec->universal_descriptor_last_ms = now;

  if (truncated) {
    const char suffix[] = "\n# descriptor truncated in master cache\n";
    const size_t cur = strlen(text);
    if (cur + sizeof(suffix) < text_capacity) strcat(text, suffix);
  }

  const bool descriptor_changed = was_valid != rec->universal_descriptor_valid ||
                                  old_crc != rec->universal_descriptor_crc;
  if (!rec->universal_descriptor_valid || descriptor_changed) {
    // Descriptor/profile changed: cached entity IDs may now point to different
    // definitions. Repopulate them from the next ENTITY_GET poll.
    rec->universal_entities_valid = false;
    rec->universal_entity_count = 0;
    memset(rec->universal_entities, 0, sizeof(rec->universal_entities));
    if (descriptor_changed) {
      selectRoles();
      updateInputRouting();
    }
  }

  return rec->universal_descriptor_valid;
}

bool MasterScheduler::readUniversalEntities(uint8_t addr) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU) || !(rec->caps & (CAP_ENTITY_CONTROL | CAP_ENTITY_EVENTS))) return false;

  uint8_t expected_profile_entities = 0;
  if (rec->universal_descriptor_valid && rec->universal_descriptor[0]) {
    const char* p = rec->universal_descriptor;
    while (p && *p) {
      const char* next = strchr(p, '\n');
      char buf[1024];
      size_t n = next ? (size_t)(next - p) : strlen(p);
      if (n >= sizeof(buf)) n = sizeof(buf) - 1;
      memcpy(buf, p, n);
      buf[n] = 0;
      uint8_t id = 0;
      const char* type_start = nullptr;
      size_t type_len = 0;
      if (parse_universal_descriptor_line(buf, id, type_start, type_len) && id >= 20) {
        char access_mode[4];
        const bool has_access = descriptor_access_mode(buf, access_mode, sizeof(access_mode));
        if ((!has_access || descriptor_access_readable(buf)) &&
            expected_profile_entities < ModuleRecord::UNIVERSAL_ENTITY_MAX) {
          ++expected_profile_entities;
        }
      }
      p = next ? next + 1 : nullptr;
    }
  }

  auto cached_debug_count = [&]() -> uint8_t {
    uint8_t count = 0;
    for (uint8_t i = 0; i < rec->universal_entity_count; ++i) {
      if (rec->universal_entities[i].id < 20) ++count;
    }
    return count;
  };

  auto seen_set = [](uint8_t* bits, uint8_t id) {
    if (bits) bits[id >> 3] |= (uint8_t)(1U << (id & 7U));
  };
  auto seen_get = [](const uint8_t* bits, uint8_t id) -> bool {
    return bits && (bits[id >> 3] & (uint8_t)(1U << (id & 7U))) != 0;
  };

  auto merge_response = [&](const Frame& response, uint8_t* seen_profile) -> bool {
    if (response.cmd != (CMD_ENTITY_GET | 0x80) || response.len < 2 || response.payload[0] != STATUS_OK) return false;
    const uint8_t reported = response.payload[1];
    uint8_t o = 2;
    for (uint8_t i = 0; i < reported && o + 4 <= response.len; ++i) {
      const uint8_t id = response.payload[o++];
      const uint8_t len = response.payload[o++];
      const uint16_t age = get_u16_le(response.payload + o); o += 2;
      if (o + len > response.len) break;

      bool may_store = true;
      if (id < 20 && !find_universal_entity_state(*rec, id)) {
        // Reserve enough of the Master's 32 state slots for every profile
        // entity. Debug states may use only the remaining slots.
        const uint8_t debug_limit =
          expected_profile_entities >= ModuleRecord::UNIVERSAL_ENTITY_MAX
            ? 0
            : (uint8_t)(ModuleRecord::UNIVERSAL_ENTITY_MAX - expected_profile_entities);
        if (cached_debug_count() >= debug_limit) may_store = false;
      }

      if (may_store) {
        remember_universal_entity_state(*rec, id, response.payload + o, len);
        // remember_universal_entity_state() intentionally sets age=0 for
        // immediate local writes. Restore the module-reported age for polls.
        for (uint8_t s = 0; s < rec->universal_entity_count; ++s) {
          if (rec->universal_entities[s].id == id) {
            rec->universal_entities[s].age_ms = age;
            break;
          }
        }
      }
      if (seen_profile && id >= 20) seen_set(seen_profile, id);
      o += len;
    }
    return true;
  };

  uint8_t seen_bulk[32] = {0};
  Frame resp;
  if (!request(addr, CMD_ENTITY_GET, nullptr, 0, resp, 60)) return false;
  if (!merge_response(resp, seen_bulk)) return false;

  // A 192-byte ENTITY_GET frame comfortably carries many numeric/bool profile
  // values, but long Text/Select values can fill the frame before every
  // descriptor entity is present. Repair a bounded batch of omitted profile
  // values so MQTT and Display receive a complete current entity set without
  // waiting tens of seconds for large community profiles.
  if (expected_profile_entities && rec->universal_descriptor_valid) {
    const uint8_t repair_limit = 6;
    uint8_t repaired = 0;
    for (uint8_t pass = 0; pass < 2 && repaired < repair_limit; ++pass) {
      const char* p = rec->universal_descriptor;
      while (p && *p && repaired < repair_limit) {
        const char* next = strchr(p, '\n');
        char buf[1024];
        size_t n = next ? (size_t)(next - p) : strlen(p);
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, p, n);
        buf[n] = 0;
        uint8_t id = 0;
        const char* type_start = nullptr;
        size_t type_len = 0;
        char access_mode[4];
        const bool has_access = descriptor_access_mode(buf, access_mode, sizeof(access_mode));
        if (parse_universal_descriptor_line(buf, id, type_start, type_len) &&
            id >= 20 && (!has_access || descriptor_access_readable(buf)) &&
            !seen_get(seen_bulk, id) &&
            (pass || id >= universal_entity_repair_cursor_)) {
          const uint8_t payload[1] = { id };
          Frame one;
          if (request(addr, CMD_ENTITY_GET, payload, 1, one, 60)) {
            merge_response(one, nullptr);
            seen_set(seen_bulk, id);
            ++repaired;
          }
          universal_entity_repair_cursor_ = id >= 249 ? 20 : (uint8_t)(id + 1);
        }
        p = next ? next + 1 : nullptr;
      }
    }
  }

  rec->universal_entities_valid = rec->universal_entity_count > 0;
  rec->universal_entities_last_ms = millis();
  updateUniversalOutputStateFromEntities(*rec);
  return true;
}

bool MasterScheduler::setUniversalEntity(uint8_t addr, uint8_t entity_id, const uint8_t* data, uint8_t len) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  ModuleRecord* rec = registry_.find(addr);
  if (!rec || !rec->online || (rec->type != MODULE_UNIVERSAL_RS232 && rec->type != MODULE_MODBUS_RTU) || !(rec->caps & CAP_ENTITY_CONTROL)) return false;
  if (len > MAX_PAYLOAD - 2) return false;

  // Explicit profile access is authoritative for every caller (Web, MQTT,
  // Logic and Display). Legacy descriptors without access keep the historical
  // behaviour for backwards compatibility.
  bool has_access = false;
  bool readable = true;
  bool writable = true;
  char descriptor_line[1024];
  if (descriptor_line_for_entity(*rec, entity_id, descriptor_line, sizeof(descriptor_line))) {
    char access_mode[4];
    if (descriptor_access_mode(descriptor_line, access_mode, sizeof(access_mode))) {
      has_access = true;
      readable = !strcmp(access_mode, "ro") || !strcmp(access_mode, "rw");
      writable = !strcmp(access_mode, "wo") || !strcmp(access_mode, "rw");
    }
  }
  if (has_access && !writable) return false;

  uint8_t payload[MAX_PAYLOAD];
  payload[0] = entity_id;
  payload[1] = len;
  if (len && data) memcpy(payload + 2, data, len);
  Frame resp;
  if (!request(addr, CMD_ENTITY_SET, payload, (uint8_t)(len + 2), resp, 150)) return false;
  const bool ok = resp.cmd == (CMD_ENTITY_SET | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
  if (!ok) return false;

  if (!has_access) {
    // Old module descriptors did not distinguish actual state from a command
    // target. Preserve their optimistic shadow behaviour.
    remember_universal_entity_state(*rec, entity_id, data, len);
    updateUniversalOutputStateFromEntities(*rec);
    readUniversalEntities(addr);
    remember_universal_entity_state(*rec, entity_id, data, len);
    updateUniversalOutputStateFromEntities(*rec);
    return true;
  }

  if (!readable) {
    // WO has no physical readback by definition. Keep the commanded target as
    // a runtime shadow. Web/MQTT may expose that shadow explicitly as the last
    // commanded target, while RW remains the only confirmed readback path.
    remember_universal_entity_state(*rec, entity_id, data, len);
    updateUniversalOutputStateFromEntities(*rec);
    return true;
  }

  // RW must be state-driven. Do not overwrite the module's real value with the
  // transmitted command after a read. The bridge schedules an immediate local
  // readback; the normal fast entity telemetry will replace the cached state.
  readUniversalEntities(addr);
  updateUniversalOutputStateFromEntities(*rec);
  return true;
}

bool MasterScheduler::setModuleLabel(uint8_t addr, const char* label) {
  if (module_fw_active_ && addr == module_fw_target_) return false;
  uint8_t payload[24];
  uint8_t len = 0;
  if (label) {
    while (len < sizeof(payload) - 1 && *label) payload[len++] = (uint8_t)*label++;
  }
  payload[len] = 0;
  Frame resp;
  bool ok = false;
  for (uint8_t attempt = 0; attempt < 2 && !ok; ++attempt) {
    if (request(addr, CMD_SET_LABEL, payload, len, resp, 180)) {
      ok = resp.cmd == (CMD_SET_LABEL | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK;
    }
    if (!ok) delay(20);
  }
  if (ok) readInfo(addr);
  return ok;
}

bool MasterScheduler::setModuleAddress(uint8_t old_addr, uint8_t new_addr) {
  if (master_display_wifi.active(old_addr) || master_display_wifi.active(new_addr)) return false;
  if (module_fw_active_) return false;
  if (new_addr == ADDR_BROADCAST || new_addr == ADDR_MASTER || new_addr == ADDR_INVALID) return false;

  bool ok = false;
  {
    SchedulerBusLock bus_lock(bus_mutex_, pdMS_TO_TICKS(300));
    if (!bus_lock.locked) return false;

    Frame req;
    req.dst = old_addr;
    req.src = ADDR_MASTER;
    req.seq = seq_++;
    req.cmd = CMD_SET_ADDRESS;
    req.len = 1;
    req.payload[0] = new_addr;
    link_.send(req);
    busDiagRecordTx(req);

    Frame resp;
    const uint32_t start = millis();
    while ((uint32_t)(millis() - start) < 250UL) {
      if (link_.poll(resp)) {
        busDiagRecordRx(resp);
        const bool src_ok = resp.src == old_addr || resp.src == new_addr;
        if (resp.dst == ADDR_MASTER && src_ok && resp.seq == req.seq && resp.cmd == (CMD_SET_ADDRESS | 0x80) && resp.len >= 1 && resp.payload[0] == STATUS_OK) {
          ok = true;
          break;
        }
      }
      serviceWhileWaiting();
    }
  }
  if (!ok) return false;

  ModuleRecord* old_rec = registry_.find(old_addr);
  const uint64_t moved_uid = old_rec ? old_rec->uid : 0;

  if (moved_uid) {
    registry_.bindUidToAddress(moved_uid, new_addr);
  } else if (old_rec) {
    old_rec->online = false;
  }

  delay(50);
  scanAddress(new_addr);
  selectRoles();
  return true;
}

struct DiscoveredModule {
  uint64_t uid = 0;
  uint8_t addr = 0;
  uint8_t type = MODULE_UNKNOWN;
  bool known_before = false;
  uint8_t remembered_addr = ADDR_INVALID;
  uint8_t discovery_order = 0;
};

static bool contains_uid(const DiscoveredModule* modules, uint8_t count, uint64_t uid) {
  for (uint8_t i = 0; i < count; ++i) {
    if (modules[i].uid == uid) return true;
  }
  return false;
}

static uint8_t preferred_addr_start(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS:
    case MODULE_JBC_USB: return 0x10;
    case MODULE_FAN_IO:
    case MODULE_FAN_IO_PRO:
      return 0x20;
    case MODULE_WELLER_ZERO_SMOG:
      return 0x30;
    case MODULE_DISPLAY:
      return 0x40;
    case MODULE_UNIVERSAL_RS232:
      return 0x50;
    case MODULE_MODBUS_RTU:
      return 0x60;
    default:
      return 0x40;
  }
}

static uint8_t preferred_addr_end(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS:
    case MODULE_JBC_USB: return 0x1F;
    case MODULE_FAN_IO:
    case MODULE_FAN_IO_PRO:
      return 0x2F;
    case MODULE_WELLER_ZERO_SMOG:
      return 0x3F;
    case MODULE_DISPLAY:
      return 0x4F;
    case MODULE_UNIVERSAL_RS232:
      return 0x5F;
    case MODULE_MODBUS_RTU:
      return 0x6F;
    default:
      return 0x6F;
  }
}

static bool address_in_preferred_range(uint8_t type, uint8_t addr) {
  return addr >= preferred_addr_start(type) && addr <= preferred_addr_end(type);
}

static bool same_preferred_address_range(uint8_t type_a, uint8_t type_b) {
  return preferred_addr_start(type_a) == preferred_addr_start(type_b) &&
         preferred_addr_end(type_a) == preferred_addr_end(type_b);
}

static bool discovered_before_for_compact_address(const DiscoveredModule& a, const DiscoveredModule& b) {
  // First come, first served. Display resolution/subtype has no priority.
  if (a.known_before != b.known_before) return a.known_before;

  if (a.known_before && b.known_before) {
    const bool a_old_ok = address_in_preferred_range(a.type, a.remembered_addr);
    const bool b_old_ok = address_in_preferred_range(b.type, b.remembered_addr);
    if (a_old_ok != b_old_ok) return a_old_ok;
    if (a_old_ok && a.remembered_addr != b.remembered_addr) {
      return a.remembered_addr < b.remembered_addr;
    }
  }

  if (a.discovery_order != b.discovery_order) {
    return a.discovery_order < b.discovery_order;
  }
  if (a.addr != b.addr) return a.addr < b.addr;
  return a.uid < b.uid;
}

static uint8_t compact_preferred_addr(const DiscoveredModule* modules, uint8_t count, uint8_t index) {
  if (!modules || index >= count) return 0;
  const uint8_t start_addr = preferred_addr_start(modules[index].type);
  const uint8_t end_addr = preferred_addr_end(modules[index].type);
  uint8_t slot = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (i == index ||
        !same_preferred_address_range(modules[i].type, modules[index].type)) continue;
    if (discovered_before_for_compact_address(modules[i], modules[index])) ++slot;
  }
  const uint16_t desired = (uint16_t)start_addr + slot;
  if (desired <= end_addr) return (uint8_t)desired;
  return 0;
}

uint8_t MasterScheduler::autoAddressModules(bool preserve_remembered) {
  SchedulerBusLock bus_lock(bus_mutex_, pdMS_TO_TICKS(500));
  if (!bus_lock.locked) return 0;
  DiscoveredModule found[ModuleRegistry::MAX_MODULES];
  uint8_t found_count = 0;

  for (uint8_t round = 0; round < 8; ++round) {
    Frame req;
    req.dst = ADDR_BROADCAST;
    req.src = ADDR_MASTER;
    req.seq = seq_++;
    req.cmd = CMD_DISCOVER_MODULES;
    req.len = 1;
    req.payload[0] = round;
    link_.send(req);
    busDiagRecordTx(req);

    const uint32_t start = millis();
    while ((uint32_t)(millis() - start) < 420UL) {
      Frame resp;
      if (link_.poll(resp)) {
        busDiagRecordRx(resp);
        if (resp.dst != ADDR_MASTER || resp.seq != req.seq || resp.cmd != (CMD_DISCOVER_MODULES | 0x80) || resp.len < 15 || resp.payload[0] != STATUS_OK) continue;
        const uint64_t uid = get_u64_le(resp.payload + 2);
        if (!uid || contains_uid(found, found_count, uid) || found_count >= ModuleRegistry::MAX_MODULES) continue;
        found[found_count].type = resp.payload[1];
        found[found_count].uid = uid;
        found[found_count].addr = resp.payload[10];
        found[found_count].discovery_order = found_count;

        for (uint8_t r = 0; r < registry_.count(); ++r) {
          const ModuleRecord& remembered = registry_.at(r);
          if (!remembered.uid || remembered.uid != uid) continue;
          found[found_count].known_before = true;
          found[found_count].remembered_addr = remembered.addr;
          break;
        }

        Serial.print("DISC uid=");
        Serial.print((uint32_t)(uid >> 32), HEX);
        Serial.print((uint32_t)uid, HEX);
        Serial.print(" addr=0x");
        if (found[found_count].addr < 0x10) Serial.print('0');
        Serial.print(found[found_count].addr, HEX);
        Serial.print(" type=0x");
        Serial.println(found[found_count].type, HEX);
        found_count++;
      }
      serviceWhileWaiting();
    }
  }

  uint8_t changed = 0;
  Serial.print("DISC found=");
  Serial.println(found_count);

  // During automatic JOIN/DISCOVERY we must preserve remembered offline
  // modules *and their addresses*. Otherwise a new module could be compacted
  // onto an offline module's address and overwrite that registry identity even
  // though prune_missing=false.
  //
  // A manual Web scan with prune_missing=true deliberately passes
  // preserve_remembered=false, keeping the previous compact/reclaim behavior.
  uint8_t desired_addr[ModuleRegistry::MAX_MODULES] = {0};
  // Wireless displays do not answer physical broadcast discovery. Their live
  // addresses must still be reserved, including during a manual compact scan.
  for (uint8_t a = 0x40; a <= 0x4F; ++a) {
    if (master_display_wifi.active(a)) { preserve_remembered = true; break; }
  }

  if (preserve_remembered) {
    for (uint8_t i = 0; i < found_count; ++i) {
      uint8_t slot = 0;
      for (uint8_t j = 0; j < found_count; ++j) {
        if (j == i ||
            !same_preferred_address_range(found[j].type, found[i].type)) continue;
        if (discovered_before_for_compact_address(found[j], found[i])) ++slot;
      }

      const uint8_t start_addr = preferred_addr_start(found[i].type);
      const uint8_t end_addr = preferred_addr_end(found[i].type);

      // Only remembered modules absent from THIS discovery reserve addresses.
      // Current online modules are counted by 'slot' exactly once.
      for (uint16_t candidate = start_addr; candidate <= end_addr; ++candidate) {
        bool reserved_by_offline = false;
        for (uint8_t r = 0; r < registry_.count(); ++r) {
          const ModuleRecord& remembered = registry_.at(r);
          if (!remembered.uid || remembered.addr != (uint8_t)candidate) continue;
          if (!contains_uid(found, found_count, remembered.uid)) {
            reserved_by_offline = true;
            break;
          }
        }

        if (reserved_by_offline) continue;
        if (slot) {
          --slot;
          continue;
        }
        desired_addr[i] = (uint8_t)candidate;
        break;
      }
    }
  }


  // Dependency-aware re-addressing: move a module only when its target is not
  // currently occupied by another discovered module. This is important when a
  // new display initially sits at 0x40 while the older display must eventually
  // own 0x40: the new one is moved to 0x41 first, then the old one to 0x40.
  bool move_done[ModuleRegistry::MAX_MODULES] = {false};
  uint8_t move_done_count = 0;

  while (move_done_count < found_count) {
    int8_t selected = -1;

    for (uint8_t i = 0; i < found_count; ++i) {
      if (move_done[i]) continue;

      const uint8_t target = preserve_remembered
        ? desired_addr[i]
        : compact_preferred_addr(found, found_count, i);

      if (!target || found[i].addr == target) {
        move_done[i] = true;
        ++move_done_count;
        continue;
      }

      bool target_occupied = false;
      for (uint8_t j = 0; j < found_count; ++j) {
        if (j == i) continue;
        if (found[j].addr == target) {
          target_occupied = true;
          break;
        }
      }

      if (!target_occupied) {
        selected = (int8_t)i;
        break;
      }
    }

    if (selected < 0) {
      Serial.println("DISC readdress cycle/conflict; preserving remaining addresses");
      break;
    }

    const uint8_t i = (uint8_t)selected;
    const uint8_t next_addr = preserve_remembered
      ? desired_addr[i]
      : compact_preferred_addr(found, found_count, i);

    uint8_t payload[9];
    put_u64_le(payload, found[i].uid);
    payload[8] = next_addr;

    Frame req;
    req.dst = ADDR_BROADCAST;
    req.src = ADDR_MASTER;
    req.seq = seq_++;
    req.cmd = CMD_SET_ADDRESS_UID;
    req.len = sizeof(payload);
    memcpy(req.payload, payload, sizeof(payload));
    link_.send(req);
    busDiagRecordTx(req);

    const uint32_t start = millis();
    bool ok = false;
    while ((uint32_t)(millis() - start) < 250UL) {
      Frame resp;
      if (link_.poll(resp)) {
        busDiagRecordRx(resp);
        const bool src_ok = resp.src == found[i].addr || resp.src == next_addr;
        if (resp.dst == ADDR_MASTER && src_ok && resp.seq == req.seq &&
            resp.cmd == (CMD_SET_ADDRESS_UID | 0x80) &&
            resp.len >= 1 && resp.payload[0] == STATUS_OK) {
          ok = true;
          break;
        }
      }
      serviceWhileWaiting();
    }

    if (ok) {
      Serial.print("DISC set uid=");
      Serial.print((uint32_t)(found[i].uid >> 32), HEX);
      Serial.print((uint32_t)found[i].uid, HEX);
      Serial.print(" new=0x");
      if (next_addr < 0x10) Serial.print('0');
      Serial.println(next_addr, HEX);

      registry_.bindUidToAddress(found[i].uid, next_addr);
      found[i].addr = next_addr;
      changed++;
      delay(40);
      scanAddress(next_addr);
    }

    move_done[i] = true;
    ++move_done_count;
  }

  return changed;
}

void MasterScheduler::pushOutputIfNeeded() {
  if (!extractor_.outputDirty()) return;
  if (!active_output_addr_) { extractor_.clearOutputDirty(); return; }
  if (module_fw_active_ && active_output_addr_ == module_fw_target_) return;
  ModuleRecord* active = registry_.find(active_output_addr_);
  if (active && (active->type == MODULE_UNIVERSAL_RS232 || active->type == MODULE_MODBUS_RTU)) {
    // Community main outputs are profile-defined and may be WO. Send one
    // coherent target state so power-only profiles never receive OFF followed
    // immediately by a non-zero power command.
    if (universalSetMainOutput(*active, extractor_.outputEnabled(), extractor_.outputPower())) {
      extractor_.clearOutputDirty();
    }
    return;
  }

  bool enable_ok = false;
  bool power_ok = false;
  if (extractor_.outputEnabled()) {
    power_ok = sendOutputPower(active_output_addr_, extractor_.outputPower());
    enable_ok = sendOutputEnable(active_output_addr_, true);
  } else {
    enable_ok = sendOutputEnable(active_output_addr_, false);
    power_ok = sendOutputPower(active_output_addr_, extractor_.outputPower());
  }
  if (enable_ok && power_ok) extractor_.clearOutputDirty();
}

void MasterScheduler::pollOneBackgroundJob(uint32_t now) {
  // Several slow poll intervals are harmonics of 500 ms. Running every due
  // request in one tick creates periodic latency bursts that can approach the
  // JBC fast-poll interval. Rotate through the background classes and execute
  // at most one of them per loop. With the master's 1 ms cooperative delay,
  // simultaneously due jobs are normally spread over only a few milliseconds.
  enum BackgroundPollSlot : uint8_t {
    BG_OUTPUT_STATUS = 0,
    BG_WELLER,
    BG_UNIVERSAL,
    BG_TELEMETRY,
    BG_JBC_STATE,
    BG_DISPLAY_STATUS,
    BG_COUNT,
  };

  for (uint8_t tries = 0; tries < BG_COUNT; ++tries) {
    const uint8_t slot = next_background_poll_slot_;
    next_background_poll_slot_ = (uint8_t)((next_background_poll_slot_ + 1U) % BG_COUNT);

    switch (slot) {
      case BG_OUTPUT_STATUS:
        if ((uint32_t)(now - last_output_status_ms_) >= 250UL) {
          last_output_status_ms_ = now;
          pollNextOutputStatus();
          return;
        }
        break;

      case BG_WELLER:
        if ((uint32_t)(now - last_weller_poll_ms_) >= 1000UL) {
          last_weller_poll_ms_ = now;
          pollNextWeller();
          return;
        }
        break;

      case BG_UNIVERSAL:
        if ((uint32_t)(now - last_universal_poll_ms_) >= 1000UL) {
          last_universal_poll_ms_ = now;
          pollNextUniversal();
          return;
        }
        break;

      case BG_TELEMETRY:
        // LED state is part of CMD_GET_TELEMETRY. Rotate one online module
        // every 75 ms so the web LED visualization follows the real LEDs
        // quickly without accelerating the heavyweight /state JSON refresh.
        // At 250 kbit/s this adds only a small bus load while keeping even a
        // full eight-module installation comfortably below ~1 s LED latency.
        if ((uint32_t)(now - last_module_telemetry_ms_) >= 75UL) {
          last_module_telemetry_ms_ = now;
          pollNextTelemetry();
          return;
        }
        break;

      case BG_JBC_STATE:
        if ((uint32_t)(now - last_jbc_state_ms_) >= 500UL) {
          last_jbc_state_ms_ = now;
          readNextJbcState();
          return;
        }
        break;

      case BG_DISPLAY_STATUS:
        if ((uint32_t)(now - last_display_status_ms_) >= DISPLAY_STATUS_SLOT_MS) {
          last_display_status_ms_ = now;
          pushDisplayStatus();
          return;
        }
        break;

      default:
        next_background_poll_slot_ = 0;
        break;
    }
  }
}

void MasterScheduler::tick() {
  const uint32_t now = millis();
  processPendingExtractorActions();
  extractor_.tick();
  drainUnsolicitedFrames();

  // A fallback DISCOVER is the one intentional exception to the normal
  // master-request/module-response ownership rule. While its delayed responses
  // are expected, keep the master silent and only drain incoming frames.
  if (hotplug_discovery_window_until_ms_ != 0) {
    if ((int32_t)(now - hotplug_discovery_window_until_ms_) < 0) return;
    hotplug_discovery_window_until_ms_ = 0;
  }

  if ((uint32_t)(now - last_led_sync_ms_) >= 1000UL) {
    last_led_sync_ms_ = now;
    broadcastLedSync(now);
  }
  const bool ota_active = module_fw_active_;

  // During RS485 OTA we pause discovery/full scans, but keep live control and
  // polling for all non-target modules so outputs can still react.
  if (!ota_active) {
    if (!scan_job_active_) {
      pollHotplugDiscovery();
      // pollHotplugDiscovery() may have opened a reserved response window in
      // this very tick. Do not start an offline reprobe or any other request
      // until that window has elapsed.
      if (hotplug_discovery_window_until_ms_ != 0 &&
          (int32_t)(millis() - hotplug_discovery_window_until_ms_) < 0) return;
      pollOfflineModules();
    }

    if (pending_hotplug_scan_ && (uint32_t)(now - pending_hotplug_scan_ms_) >= 600UL) {
      pending_hotplug_scan_ = false;
      const bool full_scan = pending_hotplug_full_scan_;
      const uint8_t hotplug_addr = pending_hotplug_addr_;
      pending_hotplug_full_scan_ = false;
      pending_hotplug_addr_ = ADDR_INVALID;
      if (full_scan || hotplug_addr == ADDR_INVALID) {
        // Automatic JOIN/DISCOVERY may add or refresh modules, but it must never
        // prune remembered offline modules. Pruning is reserved for an explicit
        // user-triggered module scan.
        requestScanKnownModules(true, false);
      } else {
        scanAddress(hotplug_addr);
        selectRoles();
        updateJbcAggregate();
      }
    }

    pollScanJob();
  }

  uint32_t fast_poll_interval_ms = JBC_FAST_POLL_PER_MODULE_MS;
  const uint8_t online_jbc_count = onlineJbcModuleCount();
  if (online_jbc_count > 1) fast_poll_interval_ms = JBC_FAST_POLL_PER_MODULE_MS / online_jbc_count;
  if (fast_poll_interval_ms < FAST_POLL_MIN_INTERVAL_MS) fast_poll_interval_ms = FAST_POLL_MIN_INTERVAL_MS;
  if ((uint32_t)(now - last_fast_poll_ms_) >= fast_poll_interval_ms) {
    last_fast_poll_ms_ = now;
    pollNextJbc();
  }

  pushOutputIfNeeded();

  if (ota_active) {
    // Keep the real-time control path alive during module OTA, but leave the
    // bus mostly to FW_CHUNK frames. Status, telemetry, display and descriptor
    // polls resume directly after FW_END so the update stream has no periodic
    // idle gaps from background polling.
    flushControlSettingsPersist(false);
    return;
  }

  // Keep safety/control synchronization ahead of non-critical status work.
  syncSystemJbcError();

  // Do not stack all periodic status requests into the same loop iteration.
  pollOneBackgroundJob(now);
  tracePollLocal();
  flushControlSettingsPersist(false);

  // Module scans run as small background jobs so live control, display updates,
  // LEDs and web/MQTT service do not stall during normal discovery.
}
