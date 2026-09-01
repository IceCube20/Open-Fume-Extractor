#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <Update.h>
#include <stdarg.h>
#include "src/ProfileEntityFwd.h"
#ifndef OFE_STATUS_LED_ENABLE
#define OFE_STATUS_LED_ENABLE 1
#endif

#ifndef OFE_STATUS_LED_PIN
#define OFE_STATUS_LED_PIN 4
#endif

#include "src/Rs485PeripheralBus.h"
#ifndef OFE_STATUS_LED_MASTER_TIMEOUT_MS
#define OFE_STATUS_LED_MASTER_TIMEOUT_MS 8000UL
#endif

#include "src/OfeStatusLed.h"

using namespace jbc_rs485;

#ifndef RS485_RX_PIN
#define RS485_RX_PIN 26
#endif

#ifndef RS485_TX_PIN
#define RS485_TX_PIN 25
#endif

#ifndef RS485_BAUD
#define RS485_BAUD 250000
#endif

#ifndef LOCAL_RX_PIN
#define LOCAL_RX_PIN 17
#endif

#ifndef LOCAL_TX_PIN
#define LOCAL_TX_PIN 16
#endif

#ifndef LOCAL_BAUD
#define LOCAL_BAUD 9600
#endif

#ifndef LOCAL_SERIAL_CONFIG
#define LOCAL_SERIAL_CONFIG SERIAL_8N1
#endif

#ifndef DEBUG_SERIAL_ENABLE
#define DEBUG_SERIAL_ENABLE 0
#endif

static const uint16_t HW_VERSION = 0x0100;
#ifndef OFE_STR_HELPER
#define OFE_STR_HELPER(x) #x
#define OFE_STR(x) OFE_STR_HELPER(x)
#endif

#define OFE_MODULE_FW_MAJOR 1
#define OFE_MODULE_FW_MINOR 0
#define OFE_MODULE_FW_PATCH 51
#define OFE_MODULE_FW_SUFFIX "alpha"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=MODBUS_RTU;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}
static const uint8_t DEFAULT_MODULE_ADDR = 0x60;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;
static const uint32_t OUTPUT_FAILSAFE_TIMEOUT_MS = 8000UL;
static const uint16_t FAULT_LOCAL_UART_INACTIVE = 0x0001;

static HardwareSerial RS485(1);
static HardwareSerial LOCAL(2);
static Link bus(RS485);
static Preferences prefs;
static OfeStatusLed ofe_status_leds;

static uint8_t module_addr = DEFAULT_MODULE_ADDR;
static char module_label[24] = {0};
static char profile_name[32] = "Generic Modbus RTU";
static char station_name[32] = "Modbus device";
static char frame_name[5] = "8N1";
static char protocol_name[16] = "MODBUS_RTU";
static char checksum_name[24] = "CRC16_MODBUS_LE";
static uint32_t local_baud = LOCAL_BAUD;
static uint32_t local_serial_config = LOCAL_SERIAL_CONFIG;
static uint8_t modbus_slave = 1;
static uint16_t default_poll_ms = 500;
static bool fw_update_active = false;
static uint32_t fw_update_offset = 0;
#ifndef FW_UPDATE_WRITE_BUFFER_SIZE
#define FW_UPDATE_WRITE_BUFFER_SIZE 1024
#endif
static uint8_t fw_update_write_buffer[FW_UPDATE_WRITE_BUFFER_SIZE];
static size_t fw_update_write_len = 0;

static void fw_update_buffer_reset() {
  fw_update_write_len = 0;
}

static bool fw_update_buffer_flush() {
  if (!fw_update_write_len) return true;
  const size_t n = fw_update_write_len;
  if (Update.write(fw_update_write_buffer, n) != n) return false;
  fw_update_write_len = 0;
  return true;
}

static bool fw_update_buffer_append(const uint8_t* data, size_t len) {
  if (!data && len) return false;
  size_t pos = 0;
  while (pos < len) {
    const size_t free_len = FW_UPDATE_WRITE_BUFFER_SIZE - fw_update_write_len;
    if (!free_len) {
      if (!fw_update_buffer_flush()) return false;
      continue;
    }
    const size_t n = free_len < (len - pos) ? free_len : (len - pos);
    memcpy(fw_update_write_buffer + fw_update_write_len, data + pos, n);
    fw_update_write_len += n;
    pos += n;
  }
  return true;
}
static uint32_t fw_update_last_ms = 0;

static uint32_t local_rx_count = 0;      // all complete local UART packets
static uint32_t local_rx_ok_count = 0;   // checksum/preset accepted packets
static uint32_t local_rx_checksum_errors = 0;
static uint32_t local_rx_pattern_errors = 0;
static uint32_t local_tx_count = 0;
static uint32_t last_local_rx_ms = 0;    // last valid local UART packet
static uint32_t last_local_rx_any_ms = 0; // last complete local UART packet, valid or invalid
static uint32_t last_local_tx_ms = 0;
static uint32_t last_local_activity_ms = 0;
static char last_local_rx_text[32] = "-";
static char last_local_tx_text[32] = "-";
static char last_local_rx_status[20] = "-";
static uint32_t last_master_ms = 0;
static uint16_t fault_mask = 0;
static bool output_failsafe_active = false;

static uint8_t rx_packet[64];
static uint8_t rx_packet_len = 0;
static uint32_t rx_packet_last_ms = 0;

static uint32_t loop_window_ms = 0;
static uint32_t loop_max_us = 0;
static uint32_t last_cpu_sample_ms = 0;
static uint8_t cpu_load_pct = 0;
static uint16_t loop_max_ms = 0;
static TaskStatus_t cpu_task_stats[48];
static configRUN_TIME_COUNTER_TYPE cpu_prev_total = 0;
static configRUN_TIME_COUNTER_TYPE cpu_prev_idle = 0;
static bool cpu_runtime_valid = false;

struct LocalTraceEvent {
  uint32_t ms = 0;
  uint8_t dir = 0; // 1=RX, 2=TX for MasterScheduler
  uint8_t meta1 = 0;
  uint8_t meta2 = 0;
  uint8_t len = 0;
  uint8_t data[32];
};

static LocalTraceEvent trace_events[192];
static uint8_t trace_head = 0;
static uint8_t trace_count = 0;
static uint16_t trace_dropped = 0;
static bool trace_enabled = false;

static bool discover_response_pending = false;
static uint8_t discover_response_dst = ADDR_MASTER;
static uint8_t discover_response_seq = 0;
static uint32_t discover_response_due_ms = 0;
static uint8_t join_announce_left = 3;
static uint32_t next_join_announce_ms = 0;

static char descriptor_text[16384];
static bool descriptor_truncated = false;
static bool descriptor_dirty = true;


static const uint16_t PROFILE_TEXT_MAX = 8192;
static const uint8_t PROFILE_ENTITY_MAX = 32;
static const uint8_t PROFILE_ENTITY_BASE_ID = 20;

enum ProfileTimeBase : uint8_t {
  PROFILE_TIME_NONE = 0,
  PROFILE_TIME_SECONDS = 1,
  PROFILE_TIME_MINUTES = 2,
  PROFILE_TIME_HOURS = 3,
  PROFILE_TIME_DAYS = 4
};

enum ProfileTimeDisplay : uint8_t {
  PROFILE_TIME_RAW = 0,
  PROFILE_TIME_AS_MINUTES = 1,
  PROFILE_TIME_AS_HOURS = 2,
  PROFILE_TIME_AS_DAYS = 3,
  PROFILE_TIME_AS_DHM = 4
};

enum ProfileMapMode : uint8_t {
  PROFILE_MAP_NONE = 0,
  PROFILE_MAP_EXACT = 1,
  PROFILE_MAP_FLAGS = 2
};

struct ProfileEntity {
  bool used = false;
  uint8_t id = 0;
  char type[14] = {0};
  char access[3] = {0};
  char key[18] = {0};
  char name[28] = {0};
  char unit[8] = {0};
  char role[24] = {0};
  int32_t min_value = 0;
  int32_t max_value = 100;
  int32_t step_value = 1;
  int32_t value_on = 1;
  int32_t value_off = 0;
  int32_t scale_value = 1;
  uint32_t divisor_value = 1;
  int32_t offset_value = 0;
  uint32_t bit_mask = 0;
  uint8_t bit_shift = 0;
  uint8_t time_base = PROFILE_TIME_NONE;
  uint8_t time_display = PROFILE_TIME_RAW;
  uint8_t map_mode = PROFILE_MAP_NONE;
  char value_map[192] = {0};
  char map_default[32] = {0};
  char options[160] = {0};
  char values[160] = {0};
  char func[18] = {0};
  char read_func[18] = {0};
  uint16_t reg = 0;
  uint8_t slave = 0;
  int32_t value = 0;
  bool bool_value = false;
  // WO entities have no physical readback. command_shadow_valid marks whether
  // value/bool_value contains an explicit runtime command target rather than
  // just the default-initialized value. This keeps main-output/EVT state from
  // inventing ON/OFF before the first real command.
  bool command_shadow_valid = false;
  char text_value[32] = "-";
  uint32_t last_update_ms = 0;
  uint32_t last_poll_ms = 0;
  uint16_t poll_ms = 1000;
};

static const char* profile_entity_mode(const ProfileEntity& e);
static bool profile_entity_readable(const ProfileEntity& e);
static bool profile_entity_writable(const ProfileEntity& e);
static bool profile_map_exact_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len);
static bool profile_map_flags_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len);
static bool profile_select_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len);
static bool profile_format_mapped_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len);
static int64_t profile_scaled_base_value(const ProfileEntity& e, int32_t raw);
static int64_t profile_base_to_minutes(const ProfileEntity& e, int64_t v);
static int32_t profile_transform_numeric(const ProfileEntity& e, int32_t raw);
static int32_t profile_inverse_numeric(const ProfileEntity& e, int32_t display_value);
static void profile_format_dhm(const ProfileEntity& e, int32_t raw, char* out, size_t out_len);
static void profile_store_read_value(ProfileEntity& e, int32_t raw);
static const char* profile_time_base_name(uint8_t v);
static const char* profile_time_display_name(uint8_t v);
static const char* profile_map_mode_name(uint8_t v);
static bool profile_uses_dhm(const ProfileEntity& e);
static bool profile_uses_text_output(const ProfileEntity& e);
static bool append_profile_entity_value(uint8_t* p, uint8_t& o, const ProfileEntity& e);
static uint8_t entity_read_func_code(const ProfileEntity& e);
static uint8_t entity_write_func_code(const ProfileEntity& e);
static ProfileEntity* modbus_find_entity_for_response(uint8_t slave, uint8_t fc, uint16_t reg);
static bool modbus_send_request(ProfileEntity& e, uint8_t fc, uint16_t value);
static bool profile_output_entity(const ProfileEntity& e);
static bool profile_output_enable_entity(const ProfileEntity& e);
static bool profile_output_power_entity(const ProfileEntity& e);
static bool profile_output_rpm_entity(const ProfileEntity& e);
static uint16_t profile_output_rpm_value();
static bool profile_entity_bool_active(const ProfileEntity& e);
static bool profile_output_enabled_active();
static uint16_t profile_output_power_permille(bool enabled);
static bool local_device_online_now(uint32_t now = millis());
static void update_local_fault_state(uint32_t now = millis());

static ProfileEntity profile_entities[PROFILE_ENTITY_MAX];
static uint8_t profile_entity_count = 0;
static String saved_profile_text;
static String upload_profile_text;
static bool upload_profile_active = false;
static uint32_t upload_profile_expected_len = 0;
static uint32_t upload_profile_expected_crc = 0;
static uint32_t last_profile_poll_ms = 0;
static uint32_t profile_poll_pause_until_ms = 0;
static uint8_t profile_poll_cursor = 0;
static uint8_t profile_readback_entity_id = 0;
static uint8_t pending_modbus_entity_id = 0;
static uint8_t pending_modbus_func = 0;
static uint32_t pending_modbus_ms = 0;

static const char fault_map_text[] =
  "0x0001,warn,Local UART inactive,Lokaler UART inaktiv,0\n";




static bool str_eq_ci(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return false;
  }
  return *a == 0 && *b == 0;
}

static bool valid_checksum_name(const char* value) {
  return value && (str_eq_ci(value, "CRC16") || str_eq_ci(value, "CRC16_MODBUS") || str_eq_ci(value, "CRC16_MODBUS_LE"));
}

static const char* checksum_type_label() {
  return "CRC16_MODBUS_LE";
}

static bool append_active_checksum(uint8_t* out, uint8_t& len, uint8_t max_len) {
  if (len + 2 > max_len) return false;
  const uint16_t crc = crc16_modbus(out, len);
  out[len++] = (uint8_t)(crc & 0xFF);
  out[len++] = (uint8_t)(crc >> 8);
  return true;
}

static void bytes_preview(const uint8_t* data, uint8_t len, char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  if (!data || !len) {
    snprintf(out, out_len, "-");
    return;
  }
  char ascii[13];
  char hex[25];
  uint8_t n = len > 8 ? 8 : len;
  for (uint8_t i = 0; i < n && i < sizeof(ascii) - 1; ++i) {
    const uint8_t c = data[i];
    ascii[i] = (c >= 32 && c <= 126) ? (char)c : '.';
  }
  ascii[n] = 0;
  size_t ho = 0;
  static const char hx[] = "0123456789ABCDEF";
  for (uint8_t i = 0; i < n && ho + 2 < sizeof(hex); ++i) {
    hex[ho++] = hx[(data[i] >> 4) & 0x0F];
    hex[ho++] = hx[data[i] & 0x0F];
  }
  hex[ho] = 0;
  snprintf(out, out_len, "%s [%s%s]", ascii, hex, len > n ? ".." : "");
}

static bool validate_active_rx_checksum(const uint8_t* data, uint8_t len, uint8_t& meta1, uint8_t& meta2) {
  meta1 = data && len ? data[0] : 0;
  meta2 = 0;
  if (!data || len < 4) {
    meta2 = 0xEF;
    return false;
  }
  const uint16_t got = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
  const uint16_t calc = crc16_modbus(data, len - 2);
  if (got == calc) return true;
  meta2 = 0xEE;
  return false;
}

static uint32_t fnv1a32(const uint8_t* data, size_t len) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619UL;
  }
  return h;
}

static void desc_append(char* dst, size_t dst_len, const char* src) {
  if (!dst || !dst_len || !src) return;
  const size_t cur = strlen(dst);
  if (cur >= dst_len - 1) {
    descriptor_truncated = true;
    return;
  }
  const size_t room = dst_len - cur - 1;
  if (strlen(src) > room) descriptor_truncated = true;
  strncat(dst, src, room);
}

static void desc_appendf(char* dst, size_t dst_len, const char* fmt, ...) {
  char tmp[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  tmp[sizeof(tmp) - 1] = 0;
  desc_append(dst, dst_len, tmp);
}

static const char* profile_entity_mode(const ProfileEntity& e) {
  if (str_eq_ci(e.access, "ro") || str_eq_ci(e.access, "rw") || str_eq_ci(e.access, "wo")) return e.access;
  const bool readable = e.read_func[0] || strncmp(e.func, "read_", 5) == 0 || str_eq_ci(e.type, "sensor") || str_eq_ci(e.type, "binary_sensor");
  const bool writable = strncmp(e.func, "write_", 6) == 0 || str_eq_ci(e.type, "button");
  if (readable && writable) return "rw";
  if (writable) return "wo";
  return "ro";
}

static bool profile_entity_readable(const ProfileEntity& e) {
  const char* mode = profile_entity_mode(e);
  return str_eq_ci(mode, "ro") || str_eq_ci(mode, "rw");
}

static bool profile_entity_writable(const ProfileEntity& e) {
  const char* mode = profile_entity_mode(e);
  return str_eq_ci(mode, "wo") || str_eq_ci(mode, "rw");
}

static void rebuild_descriptor() {
  descriptor_text[0] = 0;
  descriptor_truncated = false;
  desc_appendf(descriptor_text, sizeof(descriptor_text),
    "schema=1\n"
    "descriptor_limit=%u\n"
    "descriptor_truncated=0\n"
    "module=Modbus RTU Bridge\n"
    "profile=%s\n"
    "station=%s\n"
    "local_bus=Modbus RTU master\n"
    "uart=%lu %s\n"
    "slave=%u\n"
    "poll_ms=%u\n"
    "protocol=%s\n"
    "checksum=%s\n"
    "profile_entities=%u\n"
    "profile_slots=%u\n"
    "system_entities=master_builtin\n"
    "profile_active=%s\n"
    "entities:\n",
    (unsigned)sizeof(descriptor_text), profile_name, station_name, (unsigned long)local_baud, frame_name, modbus_slave, default_poll_ms, protocol_name, checksum_type_label(), profile_entity_count, PROFILE_ENTITY_MAX, profile_entity_count ? "yes" : "no");

  // Keep the bus descriptor intentionally compact. System/debug entities are
  // fixed for this module type and are added by the Master UI. This keeps
  // CMD_DESCRIPTOR_GET short and reliable even when a profile has many rules.
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    const ProfileEntity& e = profile_entities[i];
    if (!e.used) continue;
    // Preserve native interactive entity types. Only read-value types become
    // text when a terminal transform (DHM or value mapping) produces text.
    const char* dtype = str_eq_ci(e.type, "select") ? "select" :
                        (str_eq_ci(e.type, "button") ? "button" :
                        (str_eq_ci(e.type, "switch") ? "switch" :
                        (str_eq_ci(e.type, "binary_sensor") ? "binary_sensor" :
                        (str_eq_ci(e.type, "text") ? "text" :
                        ((profile_uses_dhm(e) || e.map_mode != PROFILE_MAP_NONE) ? "text" :
                        (str_eq_ci(e.type, "number") ? "number" : "sensor"))))));
    // Compact descriptor: id>=20 identifies a profile entity. The fourth
    // token already carries the effective access mode.
    desc_appendf(descriptor_text, sizeof(descriptor_text),
      "%u %s %s %s source=profile access=%s en=%s",
      e.id, dtype, e.key[0] ? e.key : "entity", profile_entity_mode(e),
      profile_entity_mode(e), e.name[0] ? e.name : e.key);
    if (e.role[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " role="); desc_append(descriptor_text, sizeof(descriptor_text), e.role); }
    if (e.unit[0] && strcmp(e.unit, "-") != 0) { desc_append(descriptor_text, sizeof(descriptor_text), " unit="); desc_append(descriptor_text, sizeof(descriptor_text), e.unit); }
    if (e.scale_value != 1) desc_appendf(descriptor_text, sizeof(descriptor_text), " scale=%ld", (long)e.scale_value);
    if (e.divisor_value != 1) desc_appendf(descriptor_text, sizeof(descriptor_text), " div=%lu", (unsigned long)e.divisor_value);
    if (e.offset_value) desc_appendf(descriptor_text, sizeof(descriptor_text), " off=%ld", (long)e.offset_value);
    if (e.bit_mask) desc_appendf(descriptor_text, sizeof(descriptor_text), " mask=0x%lX", (unsigned long)e.bit_mask);
    if (e.bit_shift) desc_appendf(descriptor_text, sizeof(descriptor_text), " shift=%u", e.bit_shift);
    if (e.time_base != PROFILE_TIME_NONE) desc_appendf(descriptor_text, sizeof(descriptor_text), " tb=%s tf=%s", profile_time_base_name(e.time_base), profile_time_display_name(e.time_display));
    if (e.map_mode != PROFILE_MAP_NONE) {
      desc_appendf(descriptor_text, sizeof(descriptor_text), " map_mode=%s", profile_map_mode_name(e.map_mode));
      // Mapping metadata is useful for the Master's raw-bus diagnostics. Keep
      // it only while there is comfortable descriptor headroom; the module's
      // runtime value is already transformed, so omitting it never changes
      // normal Web/Display/MQTT operation.
      const size_t map_need = strlen(e.value_map) + strlen(e.map_default) + 32U;
      if (strlen(descriptor_text) + map_need < sizeof(descriptor_text) - 96U) {
        if (e.value_map[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " map="); desc_append(descriptor_text, sizeof(descriptor_text), e.value_map); }
        if (e.map_default[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " map_default="); desc_append(descriptor_text, sizeof(descriptor_text), e.map_default); }
      }
    }
    // The default slave already lives in the descriptor header. Keep register
    // and function metadata because the expert view/Builder can use it.
    if (e.slave && e.slave != modbus_slave) desc_appendf(descriptor_text, sizeof(descriptor_text), " slave=%u", e.slave);
    if (e.poll_ms) desc_appendf(descriptor_text, sizeof(descriptor_text), " poll_ms=%u", e.poll_ms);
    desc_appendf(descriptor_text, sizeof(descriptor_text), " reg=0x%04X", e.reg);
    if (e.func[0] && strcmp(e.func, "-") != 0) { desc_append(descriptor_text, sizeof(descriptor_text), " func="); desc_append(descriptor_text, sizeof(descriptor_text), e.func); }
    if (e.read_func[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " read_func="); desc_append(descriptor_text, sizeof(descriptor_text), e.read_func); }
    if (str_eq_ci(e.type, "number")) {
      desc_appendf(descriptor_text, sizeof(descriptor_text), " min=%ld max=%ld step=%ld", (long)e.min_value, (long)e.max_value, (long)e.step_value);
    }
    if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) {
      desc_appendf(descriptor_text, sizeof(descriptor_text), " value_on=%ld value_off=%ld", (long)e.value_on, (long)e.value_off);
    }
    if (e.options[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " options="); desc_append(descriptor_text, sizeof(descriptor_text), e.options); }
    if (e.values[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " values="); desc_append(descriptor_text, sizeof(descriptor_text), e.values); }
    desc_append(descriptor_text, sizeof(descriptor_text), "\n");
  }

  descriptor_text[sizeof(descriptor_text) - 1] = 0;
  if (descriptor_truncated) {
    char* flag = strstr(descriptor_text, "descriptor_truncated=0");
    if (flag) flag[strlen("descriptor_truncated=")] = '1';
  }
  descriptor_dirty = false;
}

static const char* descriptor_current() {
  if (descriptor_dirty || !descriptor_text[0]) rebuild_descriptor();
  return descriptor_text;
}

static uint32_t descriptor_crc() {
  const char* text = descriptor_current();
  return fnv1a32((const uint8_t*)text, strlen(text));
}

static uint32_t serial_config_from_frame(const char* frame) {
  if (!frame) return SERIAL_8N1;
  if (strcmp(frame, "8E1") == 0) return SERIAL_8E1;
  if (strcmp(frame, "8O1") == 0) return SERIAL_8O1;
  if (strcmp(frame, "7E1") == 0) return SERIAL_7E1;
  return SERIAL_8N1;
}

static bool apply_profile_text(const String& cfg, bool persist);
static bool process_profile_rx(const uint8_t* data, uint8_t len);
static void profile_poll_tick();
static ProfileEntity* profile_find_entity(uint8_t id);
static bool profile_send_entity(ProfileEntity& e, const uint8_t* data, uint8_t len);

static void clean_copy(char* dst, size_t dst_len, const char* src) {
  if (!dst || !dst_len) return;
  size_t o = 0;
  if (src) {
    while (*src && o < dst_len - 1) {
      const char c = *src++;
      dst[o++] = ((uint8_t)c < 0x20 || c == '"' || c == '\\') ? ' ' : c;
    }
  }
  dst[o] = 0;
  while (o && dst[o - 1] == ' ') dst[--o] = 0;
}

static void load_profile_config() {
  String s = prefs.getString("profile", profile_name);
  clean_copy(profile_name, sizeof(profile_name), s.c_str());
  s = prefs.getString("station", station_name);
  clean_copy(station_name, sizeof(station_name), s.c_str());
  s = prefs.getString("frame", frame_name);
  clean_copy(frame_name, sizeof(frame_name), s.c_str());
  s = prefs.getString("protocol", protocol_name);
  clean_copy(protocol_name, sizeof(protocol_name), s.c_str());
  s = prefs.getString("checksum", checksum_name);
  s.toUpperCase();
  if (!valid_checksum_name(s.c_str())) s = "CRC16_MODBUS_LE";
  clean_copy(checksum_name, sizeof(checksum_name), s.c_str());
  modbus_slave = prefs.getUChar("slave", 1);
  if (modbus_slave == 0 || modbus_slave > 247) modbus_slave = 1;
  default_poll_ms = prefs.getUShort("poll_ms", 500);
  if (default_poll_ms < 100) default_poll_ms = 100;
  if (default_poll_ms > 60000) default_poll_ms = 60000;
  local_baud = prefs.getUInt("baud", LOCAL_BAUD);
  if (local_baud < 300UL || local_baud > 1000000UL) local_baud = LOCAL_BAUD;
  local_serial_config = serial_config_from_frame(frame_name);
}

static void save_profile_config() {
  prefs.putString("profile", profile_name);
  prefs.putString("station", station_name);
  prefs.putString("frame", frame_name);
  prefs.putString("protocol", protocol_name);
  prefs.putString("checksum", checksum_name);
  prefs.putUChar("slave", modbus_slave);
  prefs.putUShort("poll_ms", default_poll_ms);
  prefs.putUInt("baud", local_baud);
}

static void restart_local_uart() {
  LOCAL.end();
  delay(10);
  LOCAL.begin(local_baud, local_serial_config, LOCAL_RX_PIN, LOCAL_TX_PIN);
}

static uint64_t module_uid() {
  return 0x6000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}

static bool valid_module_addr(uint8_t addr) {
  return addr >= 0x60 && addr <= 0x6F;
}

static uint32_t module_caps() {
  return CAP_FW_UPDATE | CAP_FAULT_REPORT | CAP_LOCAL_TRACE |
         CAP_DESCRIPTOR | CAP_ENTITY_CONTROL | CAP_ENTITY_EVENTS |
         CAP_LOCAL_PROTOCOL | CAP_FAULT_MAP | CAP_MODBUS_RTU;
}

static void sample_cpu_load() {
  configRUN_TIME_COUNTER_TYPE total_runtime = 0;
  const UBaseType_t task_count = uxTaskGetSystemState(
    cpu_task_stats, sizeof(cpu_task_stats) / sizeof(cpu_task_stats[0]), &total_runtime);
  if (!task_count) return;

  configRUN_TIME_COUNTER_TYPE idle_runtime = 0;
  for (UBaseType_t i = 0; i < task_count; ++i) {
    const char* name = cpu_task_stats[i].pcTaskName;
    if (name && strncmp(name, "IDLE", 4) == 0) idle_runtime += cpu_task_stats[i].ulRunTimeCounter;
  }

  if (cpu_runtime_valid) {
    const configRUN_TIME_COUNTER_TYPE elapsed = total_runtime - cpu_prev_total;
    const uint64_t capacity = (uint64_t)elapsed * configNUMBER_OF_CORES;
    uint64_t idle_delta = (configRUN_TIME_COUNTER_TYPE)(idle_runtime - cpu_prev_idle);
    if (idle_delta > capacity) idle_delta = capacity;
    if (capacity) cpu_load_pct = (uint8_t)(((capacity - idle_delta) * 100ULL + capacity / 2ULL) / capacity);
  }
  cpu_prev_total = total_runtime;
  cpu_prev_idle = idle_runtime;
  cpu_runtime_valid = true;
}

static void record_loop_time(uint32_t busy_us) {
  if (busy_us > loop_max_us) loop_max_us = busy_us;
  const uint32_t now = millis();
  if (!loop_window_ms) loop_window_ms = now;
  if ((uint32_t)(now - loop_window_ms) >= 1000UL) {
    loop_max_ms = (uint16_t)((loop_max_us + 999UL) / 1000UL);
    loop_max_us = 0;
    loop_window_ms = now;
  }
  if ((uint32_t)(now - last_cpu_sample_ms) >= 1000UL) {
    sample_cpu_load();
    last_cpu_sample_ms = now;
  }
}

static void fw_update_abort_local() {
  if (fw_update_active) Update.abort();
  fw_update_active = false;
}

static void fw_update_touch() {
  fw_update_last_ms = millis();
}

static void fw_update_check_timeout() {
  if (fw_update_active && (uint32_t)(millis() - fw_update_last_ms) > FW_UPDATE_TIMEOUT_MS) {
    Update.abort();
    fw_update_active = false;
  }
}

static void send_status_response(const Frame& req, Status status) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = req.cmd | 0x80;
  resp.len = 1;
  resp.payload[0] = status;
  bus.send(resp);
}

static void trace_clear() {
  trace_head = 0;
  trace_count = 0;
  trace_dropped = 0;
}

static void trace_log(uint8_t dir, uint8_t meta1, uint8_t meta2, const uint8_t* data, uint8_t len) {
  if (!trace_enabled) return;
  if (len > sizeof(trace_events[0].data)) len = sizeof(trace_events[0].data);
  LocalTraceEvent& ev = trace_events[trace_head];
  ev.ms = millis();
  ev.dir = dir;
  ev.meta1 = meta1;
  ev.meta2 = meta2;
  ev.len = len;
  for (uint8_t i = 0; i < len; ++i) ev.data[i] = data[i];
  trace_head = (uint8_t)((trace_head + 1) % (sizeof(trace_events) / sizeof(trace_events[0])));
  if (trace_count < sizeof(trace_events) / sizeof(trace_events[0])) ++trace_count;
  else ++trace_dropped;
}

static void local_send_raw(const uint8_t* data, uint8_t len) {
  if (!data || !len) return;
  LOCAL.write(data, len);
  LOCAL.flush();
  ++local_tx_count;
  last_local_tx_ms = millis();
  bytes_preview(data, len, last_local_tx_text, sizeof(last_local_tx_text));
  trace_log(2, 0x52, 0, data, len);
}

static void local_send_line(const uint8_t* data, uint8_t len) {
  uint8_t tx_buf[96];
  uint8_t n = 0;
  for (uint8_t i = 0; data && i < len && n < sizeof(tx_buf); ++i) tx_buf[n++] = data[i];
  if (!append_active_checksum(tx_buf, n, sizeof(tx_buf))) return;
  if (!n) return;
  LOCAL.write(tx_buf, n);
  LOCAL.flush();
  ++local_tx_count;
  last_local_tx_ms = millis();
  bytes_preview(tx_buf, n, last_local_tx_text, sizeof(last_local_tx_text));
  trace_log(2, 0x4D, 0, tx_buf, n);
}

static uint16_t local_rx_idle_gap_ms() {
  uint32_t gap = local_baud ? ((35000UL + local_baud - 1UL) / local_baud) : 8UL; // about 3.5 UART chars
  if (gap < 8UL) gap = 8UL;
  if (gap > 60UL) gap = 60UL;
  return (uint16_t)gap;
}

static void local_flush_rx_packet() {
  if (!rx_packet_len) return;
  ++local_rx_count;
  last_local_activity_ms = millis();
  last_local_rx_any_ms = last_local_activity_ms;
  bytes_preview(rx_packet, rx_packet_len, last_local_rx_text, sizeof(last_local_rx_text));
  uint8_t meta1 = 0;
  uint8_t meta2 = 0;
  const bool checksum_ok = validate_active_rx_checksum(rx_packet, rx_packet_len, meta1, meta2);
  if (checksum_ok) {
    ++local_rx_ok_count;
    const bool profile_match_ok = process_profile_rx(rx_packet, rx_packet_len);
    if (!profile_match_ok) {
      ++local_rx_pattern_errors;
      clean_copy(last_local_rx_status, sizeof(last_local_rx_status), "BAD-PATTERN");
      trace_log(1, meta1 ? meta1 : 0x52, 0xEF, rx_packet, rx_packet_len);
    } else {
      last_local_rx_ms = last_local_activity_ms;
      clean_copy(last_local_rx_status, sizeof(last_local_rx_status), "OK");
      trace_log(1, meta1 ? meta1 : 0x52, 0, rx_packet, rx_packet_len);
    }
  } else {
    if (meta2 == 0xEE) {
      ++local_rx_checksum_errors;
      clean_copy(last_local_rx_status, sizeof(last_local_rx_status), "BAD-CS");
    } else {
      ++local_rx_pattern_errors;
      clean_copy(last_local_rx_status, sizeof(last_local_rx_status), "BAD-PATTERN");
    }
    trace_log(1, meta1 ? meta1 : 0x52, meta2 ? meta2 : 0xEE, rx_packet, rx_packet_len);
  }
  rx_packet_len = 0;
}

static void localBusPoll() {
  while (LOCAL.available()) {
    const int c = LOCAL.read();
    if (c < 0) break;
    if (rx_packet_len < sizeof(rx_packet)) rx_packet[rx_packet_len++] = (uint8_t)c;
    rx_packet_last_ms = millis();
    if (rx_packet_len >= sizeof(rx_packet)) local_flush_rx_packet();
  }
  if (rx_packet_len && (uint32_t)(millis() - rx_packet_last_ms) >= local_rx_idle_gap_ms()) local_flush_rx_packet();

  update_local_fault_state();
}

static void copy_label_from_payload(const Frame& req) {
  uint8_t n = req.len;
  if (n > sizeof(module_label) - 1) n = sizeof(module_label) - 1;
  for (uint8_t i = 0; i < n; ++i) {
    char c = (char)req.payload[i];
    module_label[i] = ((uint8_t)c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>') ? ' ' : c;
  }
  module_label[n] = 0;
  while (n > 0 && module_label[n - 1] == ' ') module_label[--n] = 0;
}

static void handle_set_label(const Frame& req) {
  copy_label_from_payload(req);
  bool ok = true;
  if (module_label[0]) ok = prefs.putString("label", module_label) > 0;
  else prefs.remove("label");
  send_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
}

static void handle_fw_begin(const Frame& req) {
  if (req.len < 4) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint32_t size = get_u32_le(req.payload);
  if (fw_update_active && fw_update_offset == 0) {
    fw_update_touch();
    send_status_response(req, STATUS_OK);
    return;
  }
  if (!Update.begin(size ? size : UPDATE_SIZE_UNKNOWN)) {
    fw_update_active = false;
    send_status_response(req, STATUS_BUSY);
    return;
  }
  fw_update_active = true;
  fw_update_offset = 0;
  fw_update_buffer_reset();
  fw_update_touch();
  send_status_response(req, STATUS_OK);
}

static void handle_fw_chunk(const Frame& req) {
  if (!fw_update_active) {
    send_status_response(req, STATUS_BUSY);
    return;
  }
  if (req.len < 5) {
    fw_update_abort_local();
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint32_t offset = get_u32_le(req.payload);
  const uint8_t n = req.len - 4;
  if (offset != fw_update_offset) {
    if (offset < fw_update_offset && (uint32_t)offset + n <= fw_update_offset) {
      fw_update_touch();
      send_status_response(req, STATUS_OK);
      return;
    }
    fw_update_abort_local();
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  if (!fw_update_buffer_append(req.payload + 4, n)) {
    fw_update_abort_local();
    send_status_response(req, STATUS_BUSY);
    return;
  }
  fw_update_offset += n;
  fw_update_touch();
  send_status_response(req, STATUS_OK);
}

static void handle_fw_end(const Frame& req) {
  if (!fw_update_active) {
    send_status_response(req, STATUS_BUSY);
    return;
  }
  const bool ok = fw_update_buffer_flush() && Update.end(true);
  fw_update_active = false;
  send_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
  if (ok) {
    delay(300);
    ESP.restart();
  }
}

static void handle_fw_status(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_FW_STATUS | 0x80;
  resp.len = 6;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = fw_update_active ? 1 : 0;
  put_u32_le(resp.payload + 2, fw_update_offset);
  bus.send(resp);
}

static void handle_info(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_INFO | 0x80;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_MODBUS_RTU;
  resp.payload[o++] = PROTOCOL_VERSION;
  put_u16_le(resp.payload + o, HW_VERSION); o += 2;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = 2;
  uint8_t suffix_len = (uint8_t)strlen(FW_SUFFIX);
  if (suffix_len > 7) suffix_len = 7;
  resp.payload[o++] = suffix_len;
  for (uint8_t i = 0; i < suffix_len && o < MAX_PAYLOAD; ++i) resp.payload[o++] = (uint8_t)FW_SUFFIX[i];
  const char fallback[] = "Modbus RTU Bridge";
  const char* name = module_label[0] ? module_label : (profile_name[0] ? profile_name : fallback);
  while (*name && o < MAX_PAYLOAD) resp.payload[o++] = (uint8_t)*name++;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static void handle_caps(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_CAPS | 0x80;
  resp.len = 5;
  resp.payload[0] = STATUS_OK;
  put_u32_le(resp.payload + 1, module_caps());
  bus.send(resp);
}

static void append_system_telemetry(uint8_t* payload, uint8_t& o) {
  put_u32_le(payload + o, ESP.getFreeHeap()); o += 4;
  put_u32_le(payload + o, ESP.getMinFreeHeap()); o += 4;
  put_u32_le(payload + o, millis() / 1000UL); o += 4;
  payload[o++] = cpu_load_pct;
  put_u16_le(payload + o, loop_max_ms); o += 2;
}

static void handle_telemetry(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_TELEMETRY | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_MODBUS_RTU;
  append_system_telemetry(resp.payload, o);
  resp.payload[o++] = (uint8_t)ofe_status_leds.busEvent();
  resp.payload[o++] = (uint8_t)ofe_status_leds.moduleEvent();
  resp.len = o;
  bus.send(resp);
}

static void handle_status(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_STATUS | 0x80;
  update_local_fault_state();
  const bool output_active = profile_output_enabled_active();
  resp.len = 8;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = output_active ? 1 : 0;
  put_u16_le(resp.payload + 2, profile_output_power_permille(output_active));
  put_u16_le(resp.payload + 4, profile_output_rpm_value());
  put_u16_le(resp.payload + 6, fault_mask);
  bus.send(resp);
}

static void handle_trace_control(const Frame& req) {
  if (req.len < 1) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  trace_enabled = (req.payload[0] & 0x01) != 0;
  if (req.payload[0] & 0x02) trace_clear();
  send_status_response(req, STATUS_OK);
}

static void handle_trace_read(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_TRACE_READ | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = trace_enabled ? 1 : 0;
  put_u16_le(resp.payload + o, trace_dropped); o += 2;
  trace_dropped = 0;
  const uint8_t count_pos = o++;
  uint8_t sent = 0;
  while (trace_count && o + 6 < MAX_PAYLOAD) {
    const uint8_t tail = (uint8_t)((trace_head + (sizeof(trace_events) / sizeof(trace_events[0])) - trace_count) % (sizeof(trace_events) / sizeof(trace_events[0])));
    LocalTraceEvent& ev = trace_events[tail];
    if (o + 6 + ev.len > MAX_PAYLOAD) break;
    uint32_t age = millis() - ev.ms;
    if (age > 65535UL) age = 65535UL;
    put_u16_le(resp.payload + o, (uint16_t)age); o += 2;
    resp.payload[o++] = ev.dir;
    resp.payload[o++] = ev.meta1;
    resp.payload[o++] = ev.meta2;
    resp.payload[o++] = ev.len;
    for (uint8_t i = 0; i < ev.len; ++i) resp.payload[o++] = ev.data[i];
    --trace_count;
    ++sent;
  }
  resp.payload[count_pos] = sent;
  resp.len = o;
  bus.send(resp);
}

static void send_chunked_text(const Frame& req, uint8_t response_cmd, const char* text, uint32_t crc) {
  const uint8_t chunk_size = 160;
  const size_t text_len = strlen(text);
  const uint8_t chunk_count = (uint8_t)((text_len + chunk_size - 1) / chunk_size);
  const uint8_t chunk = req.len ? req.payload[0] : 0;
  if (chunk >= chunk_count && chunk_count) {
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  const size_t offset = (size_t)chunk * chunk_size;
  size_t n = text_len > offset ? text_len - offset : 0;
  if (n > chunk_size) n = chunk_size;
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = response_cmd | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = 1;
  put_u32_le(resp.payload + o, crc); o += 4;
  resp.payload[o++] = chunk;
  resp.payload[o++] = chunk_count ? chunk_count : 1;
  for (size_t i = 0; i < n && o < MAX_PAYLOAD; ++i) resp.payload[o++] = (uint8_t)text[offset + i];
  resp.len = o;
  bus.send(resp);
}

static void handle_descriptor_get(const Frame& req) {
  const char* text = descriptor_current();
  send_chunked_text(req, CMD_DESCRIPTOR_GET, text, descriptor_crc());
}

static void handle_fault_map_get(const Frame& req) {
  send_chunked_text(req, CMD_FAULT_MAP_GET, fault_map_text, fnv1a32((const uint8_t*)fault_map_text, strlen(fault_map_text)));
}

static void handle_profile_get(const Frame& req) {
  const char* text = saved_profile_text.length() ? saved_profile_text.c_str() : "";
  send_chunked_text(req, CMD_PROFILE_GET, text, fnv1a32((const uint8_t*)text, strlen(text)));
}

static void append_entity_bool(uint8_t* p, uint8_t& o, uint8_t id, bool value, uint16_t age_ms) {
  p[o++] = id;
  p[o++] = 1;
  put_u16_le(p + o, age_ms); o += 2;
  p[o++] = value ? 1 : 0;
}

static void append_entity_u32(uint8_t* p, uint8_t& o, uint8_t id, uint32_t value, uint16_t age_ms) {
  p[o++] = id;
  p[o++] = 4;
  put_u16_le(p + o, age_ms); o += 2;
  put_u32_le(p + o, value); o += 4;
}

static void append_entity_text(uint8_t* p, uint8_t& o, uint8_t id, const char* value, uint16_t age_ms) {
  if (!value) value = "";
  uint8_t n = (uint8_t)strlen(value);
  if (n > 31) n = 31;
  p[o++] = id;
  p[o++] = n;
  put_u16_le(p + o, age_ms); o += 2;
  for (uint8_t i = 0; i < n; ++i) p[o++] = (uint8_t)value[i];
}

static uint16_t entity_age_ms(uint32_t last_ms) {
  if (!last_ms) return 0xFFFF;
  uint32_t a = millis() - last_ms;
  if (a > 65535UL) a = 65535UL;
  return (uint16_t)a;
}

static bool can_append_entity(uint8_t o, uint8_t len) {
  return (uint16_t)o + 4U + len <= MAX_PAYLOAD;
}

static bool append_entity_bool_safe(uint8_t* p, uint8_t& o, uint8_t id, bool value, uint16_t age_ms) {
  if (!can_append_entity(o, 1)) return false;
  append_entity_bool(p, o, id, value, age_ms);
  return true;
}

static bool append_entity_u32_safe(uint8_t* p, uint8_t& o, uint8_t id, uint32_t value, uint16_t age_ms) {
  if (!can_append_entity(o, 4)) return false;
  append_entity_u32(p, o, id, value, age_ms);
  return true;
}

static bool append_entity_text_safe(uint8_t* p, uint8_t& o, uint8_t id, const char* value, uint16_t age_ms) {
  uint8_t n = value ? (uint8_t)strlen(value) : 0;
  if (n > 31) n = 31;
  if (!can_append_entity(o, n)) return false;
  append_entity_text(p, o, id, value, age_ms);
  return true;
}

static bool append_profile_entity_value(uint8_t* p, uint8_t& o, const ProfileEntity& e) {
  // Access semantics are authoritative: WO entities are commands only and
  // must never masquerade as readback values on the OFE bus.
  if (!profile_entity_readable(e)) return false;
  const uint16_t age = entity_age_ms(e.last_update_ms);
  if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) return append_entity_bool_safe(p, o, e.id, e.bool_value, age);
  if (profile_uses_text_output(e)) return append_entity_text_safe(p, o, e.id, e.text_value, age);
  return append_entity_u32_safe(p, o, e.id, (uint32_t)e.value, age);
}

static void handle_entity_get(const Frame& req) {
  const uint8_t wanted = req.len ? req.payload[0] : 0;
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_ENTITY_GET | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  const uint8_t count_pos = o++;
  uint8_t count = 0;

  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    ProfileEntity& e = profile_entities[i];
    if (!e.used) continue;
    if (wanted && wanted != e.id) continue;
    if (append_profile_entity_value(resp.payload, o, e)) ++count;
  }

  uint16_t age = entity_age_ms(last_local_rx_ms);
  uint16_t rx_any_age = entity_age_ms(last_local_rx_any_ms);
  uint16_t tx_age = entity_age_ms(last_local_tx_ms);

  if (!wanted || wanted == 1) { if (append_entity_bool_safe(resp.payload, o, 1, last_local_rx_ms && age < 5000, age)) ++count; }
  if (!wanted || wanted == 5) { if (append_entity_text_safe(resp.payload, o, 5, checksum_type_label(), 0)) ++count; }
  if (!wanted || wanted == 7) { if (append_entity_u32_safe(resp.payload, o, 7, local_rx_ok_count, 0)) ++count; }
  if (!wanted || wanted == 8) { if (append_entity_u32_safe(resp.payload, o, 8, local_rx_checksum_errors, 0)) ++count; }
  if (!wanted || wanted == 9) { if (append_entity_u32_safe(resp.payload, o, 9, local_rx_pattern_errors, 0)) ++count; }
  if (!wanted || wanted == 13) { if (append_entity_text_safe(resp.payload, o, 13, last_local_rx_text, rx_any_age)) ++count; }
  if (!wanted || wanted == 14) { if (append_entity_text_safe(resp.payload, o, 14, last_local_tx_text, tx_age)) ++count; }
  if (!wanted || wanted == 15) { if (append_entity_text_safe(resp.payload, o, 15, last_local_rx_status, rx_any_age)) ++count; }
  if (!wanted || wanted == 12) { if (append_entity_bool_safe(resp.payload, o, 12, trace_enabled, 0)) ++count; }
  if (!wanted || wanted == 2) { if (append_entity_u32_safe(resp.payload, o, 2, local_rx_count, 0)) ++count; }
  if (!wanted || wanted == 3) { if (append_entity_u32_safe(resp.payload, o, 3, local_tx_count, 0)) ++count; }
  if (!wanted || wanted == 4) { if (append_entity_u32_safe(resp.payload, o, 4, local_baud, 0)) ++count; }
  resp.payload[count_pos] = count;
  resp.len = o;
  bus.send(resp);
}

static void handle_entity_set(const Frame& req) {
  if (req.len < 2) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint8_t id = req.payload[0];
  const uint8_t n = req.payload[1];
  if (req.len != (uint8_t)(n + 2)) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  ProfileEntity* pe = profile_find_entity(id);
  if (pe) {
    if (!profile_send_entity(*pe, req.payload + 2, n)) {
      send_status_response(req, STATUS_NOT_SUPPORTED);
      return;
    }
    send_status_response(req, STATUS_OK);
    return;
  }
  if (id == 10) {
    local_send_raw(req.payload + 2, n);
    send_status_response(req, STATUS_OK);
    return;
  }
  if (id == 11) {
    local_send_line(req.payload + 2, n);
    send_status_response(req, STATUS_OK);
    return;
  }
  if (id == 12) {
    trace_enabled = n ? (req.payload[2] != 0 && req.payload[2] != '0') : false;
    if (!trace_enabled) trace_clear();
    send_status_response(req, STATUS_OK);
    return;
  }
  send_status_response(req, STATUS_NOT_SUPPORTED);
}

static uint32_t discover_delay_ms(const Frame& req) {
  const uint64_t uid = module_uid();
  const uint8_t round = req.len ? req.payload[0] : req.seq;
  uint32_t mix = (uint32_t)uid ^ (uint32_t)(uid >> 32) ^ 0x7F4A7C15UL;
  mix ^= (uint32_t)round * 0x85EBCA6BUL;
  mix ^= mix >> ((round & 7) + 3);
  mix *= 0xC2B2AE35UL;
  mix ^= mix >> 16;
  return 5UL + (uint32_t)(mix & 0x3F) * 6UL;
}

static void send_discover_response(uint8_t dst, uint8_t seq) {
  Frame resp;
  resp.dst = dst;
  resp.src = module_addr;
  resp.seq = seq;
  resp.cmd = CMD_DISCOVER_MODULES | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_MODBUS_RTU;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u32_le(resp.payload + o, module_caps()); o += 4;
  resp.len = o;
  bus.send(resp);
}

static void handle_discover(const Frame& req) {
  if (fw_update_active) return;
  discover_response_dst = req.src;
  discover_response_seq = req.seq;
  discover_response_due_ms = millis() + discover_delay_ms(req);
  discover_response_pending = true;
}

static void poll_pending_discover_response() {
  if (!discover_response_pending) return;
  if (fw_update_active) {
    discover_response_pending = false;
    return;
  }
  if ((int32_t)(millis() - discover_response_due_ms) < 0) return;
  discover_response_pending = false;
  send_discover_response(discover_response_dst, discover_response_seq);
}

static uint32_t join_delay_ms(uint8_t round) {
  const uint64_t uid = module_uid();
  uint32_t mix = (uint32_t)uid ^ (uint32_t)(uid >> 32) ^ ((uint32_t)round * 0x9E3779B9UL);
  mix ^= mix >> 16;
  return 300UL + (mix % 900UL);
}

static void send_join_announce() {
  send_discover_response(ADDR_MASTER, 0);
}

static void poll_join_announce() {
  if (!join_announce_left) return;
  if ((int32_t)(millis() - next_join_announce_ms) < 0) return;
  send_join_announce();
  --join_announce_left;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
}

static String config_value(const String& cfg, const char* key) {
  String prefix = String(key) + "=";
  int start = 0;
  while (start < cfg.length()) {
    int end = cfg.indexOf('\n', start);
    if (end < 0) end = cfg.length();
    String line = cfg.substring(start, end);
    line.trim();
    if (line.startsWith(prefix)) {
      String value = line.substring(prefix.length());
      value.trim();
      return value;
    }
    start = end + 1;
  }
  return String();
}


static bool str_to_bool(const String& v, bool def = false) {
  String s = v;
  s.trim();
  s.toLowerCase();
  if (!s.length()) return def;
  return s == "1" || s == "true" || s == "yes" || s == "on" || s == "rw" || s == "wo";
}

static void profile_clear_entities() {
  for (uint8_t i = 0; i < PROFILE_ENTITY_MAX; ++i) profile_entities[i] = ProfileEntity();
  profile_entity_count = 0;
  profile_readback_entity_id = 0;
}

static void sanitize_key_copy(char* dst, size_t dst_len, const char* src, uint8_t fallback_index) {
  if (!dst || !dst_len) return;
  size_t o = 0;
  if (src) {
    for (const char* p = src; *p && o < dst_len - 1; ++p) {
      char c = *p;
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') dst[o++] = c;
      else if ((c == ' ' || c == '-' || c == '.') && o && dst[o - 1] != '_') dst[o++] = '_';
    }
  }
  while (o && dst[o - 1] == '_') --o;
  dst[o] = 0;
  if (!dst[0]) snprintf(dst, dst_len, "entity%u", fallback_index);
}

static bool parse_i32(const String& s, int32_t& out) {
  if (!s.length()) return false;
  char* endp = nullptr;
  long v = strtol(s.c_str(), &endp, 10);
  if (endp == s.c_str()) return false;
  out = (int32_t)v;
  return true;
}

static bool parse_u32_profile_value(const String& s, uint32_t& out) {
  if (!s.length()) return false;
  const char* p = s.c_str();
  int base = 10;
  if (s.length() > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
  else if (s.length() > 2 && p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
  char* endp = nullptr;
  unsigned long v = strtoul(p, &endp, base);
  if (endp == p || *endp) return false;
  out = (uint32_t)v;
  return true;
}

static uint8_t profile_parse_time_base(String v) {
  v.trim(); v.toLowerCase();
  if (v == "s" || v == "sec" || v == "second" || v == "seconds") return PROFILE_TIME_SECONDS;
  if (v == "m" || v == "min" || v == "minute" || v == "minutes") return PROFILE_TIME_MINUTES;
  if (v == "h" || v == "hr" || v == "hour" || v == "hours") return PROFILE_TIME_HOURS;
  if (v == "d" || v == "day" || v == "days") return PROFILE_TIME_DAYS;
  return PROFILE_TIME_NONE;
}

static uint8_t profile_parse_time_display(String v) {
  v.trim(); v.toLowerCase();
  if (v == "m" || v == "min" || v == "minutes") return PROFILE_TIME_AS_MINUTES;
  if (v == "h" || v == "hr" || v == "hours") return PROFILE_TIME_AS_HOURS;
  if (v == "d" || v == "days") return PROFILE_TIME_AS_DAYS;
  if (v == "dhm" || v == "duration" || v == "days_hours_minutes") return PROFILE_TIME_AS_DHM;
  return PROFILE_TIME_RAW;
}

static const char* profile_time_base_name(uint8_t v) {
  switch (v) {
    case PROFILE_TIME_SECONDS: return "s";
    case PROFILE_TIME_MINUTES: return "m";
    case PROFILE_TIME_HOURS: return "h";
    case PROFILE_TIME_DAYS: return "d";
    default: return "none";
  }
}

static const char* profile_time_display_name(uint8_t v) {
  switch (v) {
    case PROFILE_TIME_AS_MINUTES: return "m";
    case PROFILE_TIME_AS_HOURS: return "h";
    case PROFILE_TIME_AS_DAYS: return "d";
    case PROFILE_TIME_AS_DHM: return "dhm";
    default: return "raw";
  }
}

static uint8_t profile_parse_map_mode(String v) {
  v.trim(); v.toLowerCase();
  if (v == "exact" || v == "enum" || v == "value") return PROFILE_MAP_EXACT;
  if (v == "flags" || v == "bits" || v == "combine") return PROFILE_MAP_FLAGS;
  return PROFILE_MAP_NONE;
}

static const char* profile_map_mode_name(uint8_t v) {
  switch (v) {
    case PROFILE_MAP_EXACT: return "exact";
    case PROFILE_MAP_FLAGS: return "flags";
    default: return "none";
  }
}

static bool profile_parse_map_number(const char* begin, size_t len, int32_t& out) {
  while (len && (*begin == ' ' || *begin == '\t')) { ++begin; --len; }
  while (len && (begin[len - 1] == ' ' || begin[len - 1] == '\t')) --len;
  if (!len || len >= 40) return false;
  char tmp[40];
  memcpy(tmp, begin, len); tmp[len] = 0;
  bool neg = false;
  const char* p = tmp;
  if (*p == '-') { neg = true; ++p; }
  else if (*p == '+') ++p;
  int base = 10;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
  else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
  if (!*p) return false;
  char* endp = nullptr;
  unsigned long uv = strtoul(p, &endp, base);
  if (endp == p || *endp) return false;
  if (neg) {
    if (uv > 2147483648UL) return false;
    out = (int32_t)(-(int64_t)uv);
  } else {
    if ((uint64_t)uv > 0xFFFFFFFFULL) return false;
    out = (int32_t)(uint32_t)uv;
  }
  return true;
}

static bool profile_map_entry(const char* map, size_t& pos, int32_t& key, char* label, size_t label_len) {
  if (!map || !label || !label_len) return false;
  const size_t total = strlen(map);
  while (pos < total && (map[pos] == '|' || map[pos] == ' ' || map[pos] == '\t')) ++pos;
  if (pos >= total) return false;
  const size_t start = pos;
  while (pos < total && map[pos] != '|') ++pos;
  size_t end = pos;
  if (pos < total && map[pos] == '|') ++pos;
  const char* sep = nullptr;
  for (size_t i = start; i < end; ++i) {
    if (map[i] == '=' || map[i] == ':') { sep = map + i; break; }
  }
  if (!sep) return true; // malformed entry: caller skips it
  if (!profile_parse_map_number(map + start, (size_t)(sep - (map + start)), key)) return true;
  const char* lp = sep + 1;
  const char* le = map + end;
  while (lp < le && (*lp == ' ' || *lp == '\t')) ++lp;
  while (le > lp && (le[-1] == ' ' || le[-1] == '\t')) --le;
  size_t n = (size_t)(le - lp);
  if (n >= label_len) n = label_len - 1;
  memcpy(label, lp, n); label[n] = 0;
  return true;
}

static bool profile_map_exact_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len) {
  size_t pos = 0;
  while (e.value_map[pos]) {
    int32_t key = 0; char label[32] = {0};
    const size_t before = pos;
    if (!profile_map_entry(e.value_map, pos, key, label, sizeof(label))) break;
    if (pos == before) break;
    if (label[0] && key == value) { clean_copy(out, out_len, label); return true; }
  }
  return false;
}

static bool profile_map_flags_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len) {
  if (!out || !out_len) return false;
  out[0] = 0;
  const uint32_t bits = (uint32_t)value;
  size_t pos = 0;
  bool any = false;
  while (e.value_map[pos]) {
    int32_t key_i = 0; char label[32] = {0};
    const size_t before = pos;
    if (!profile_map_entry(e.value_map, pos, key_i, label, sizeof(label))) break;
    if (pos == before) break;
    if (!label[0]) continue;
    const uint32_t key = (uint32_t)key_i;
    const bool match = key == 0 ? bits == 0 : ((bits & key) == key);
    if (!match) continue;
    if (any && strlen(out) + 3 < out_len) strncat(out, " | ", out_len - strlen(out) - 1);
    strncat(out, label, out_len - strlen(out) - 1);
    any = true;
  }
  return any;
}

static bool profile_select_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len) {
  if (!e.options[0] || !e.values[0]) return false;
  const char* vp = e.values;
  const char* op = e.options;
  while (*vp && *op) {
    const char* ve = strchr(vp, '|'); if (!ve) ve = vp + strlen(vp);
    const char* oe = strchr(op, '|'); if (!oe) oe = op + strlen(op);
    int32_t key = 0;
    if (profile_parse_map_number(vp, (size_t)(ve - vp), key) && key == value) {
      size_t n = (size_t)(oe - op); if (n >= out_len) n = out_len - 1;
      memcpy(out, op, n); out[n] = 0; return true;
    }
    vp = *ve ? ve + 1 : ve;
    op = *oe ? oe + 1 : oe;
  }
  return false;
}

static bool profile_format_mapped_text(const ProfileEntity& e, int32_t value, char* out, size_t out_len) {
  bool ok = false;
  if (e.map_mode == PROFILE_MAP_EXACT) ok = profile_map_exact_text(e, value, out, out_len);
  else if (e.map_mode == PROFILE_MAP_FLAGS) ok = profile_map_flags_text(e, value, out, out_len);
  else if (str_eq_ci(e.type, "select")) ok = profile_select_text(e, value, out, out_len);
  if (!ok && e.map_default[0]) { clean_copy(out, out_len, e.map_default); ok = true; }
  return ok;
}

static int32_t profile_clamp_i64(int64_t v) {
  if (v > INT32_MAX) return INT32_MAX;
  if (v < INT32_MIN) return INT32_MIN;
  return (int32_t)v;
}

static int64_t profile_scaled_base_value(const ProfileEntity& e, int32_t raw) {
  int64_t v = (int64_t)raw;
  if (e.bit_mask || e.bit_shift) {
    uint32_t bits = (uint32_t)raw;
    if (e.bit_mask) bits &= e.bit_mask;
    if (e.bit_shift < 32) bits >>= e.bit_shift;
    v = (int64_t)bits;
  }
  const int64_t div = e.divisor_value ? (int64_t)e.divisor_value : 1LL;
  v = (v * (int64_t)e.scale_value) / div;
  v += (int64_t)e.offset_value;
  return v;
}

static int64_t profile_base_to_minutes(const ProfileEntity& e, int64_t v) {
  switch (e.time_base) {
    case PROFILE_TIME_SECONDS: return v / 60LL;
    case PROFILE_TIME_MINUTES: return v;
    case PROFILE_TIME_HOURS: return v * 60LL;
    case PROFILE_TIME_DAYS: return v * 1440LL;
    default: return v;
  }
}

static int32_t profile_transform_numeric(const ProfileEntity& e, int32_t raw) {
  int64_t v = profile_scaled_base_value(e, raw);
  if (e.time_base != PROFILE_TIME_NONE) {
    const int64_t minutes = profile_base_to_minutes(e, v);
    if (e.time_display == PROFILE_TIME_AS_MINUTES) v = minutes;
    else if (e.time_display == PROFILE_TIME_AS_HOURS) v = minutes / 60LL;
    else if (e.time_display == PROFILE_TIME_AS_DAYS) v = minutes / 1440LL;
  }
  return profile_clamp_i64(v);
}

static int32_t profile_inverse_numeric(const ProfileEntity& e, int32_t display_value) {
  int64_t base_value = display_value;
  if (e.time_base != PROFILE_TIME_NONE && e.time_display != PROFILE_TIME_RAW && e.time_display != PROFILE_TIME_AS_DHM) {
    int64_t minutes = display_value;
    if (e.time_display == PROFILE_TIME_AS_HOURS) minutes *= 60LL;
    else if (e.time_display == PROFILE_TIME_AS_DAYS) minutes *= 1440LL;
    switch (e.time_base) {
      case PROFILE_TIME_SECONDS: base_value = minutes * 60LL; break;
      case PROFILE_TIME_MINUTES: base_value = minutes; break;
      case PROFILE_TIME_HOURS: base_value = minutes / 60LL; break;
      case PROFILE_TIME_DAYS: base_value = minutes / 1440LL; break;
      default: break;
    }
  }
  base_value -= (int64_t)e.offset_value;
  if (e.scale_value != 0) base_value = (base_value * (int64_t)(e.divisor_value ? e.divisor_value : 1U)) / (int64_t)e.scale_value;
  return profile_clamp_i64(base_value);
}

static bool profile_uses_dhm(const ProfileEntity& e) {
  return e.time_base != PROFILE_TIME_NONE && e.time_display == PROFILE_TIME_AS_DHM;
}

static bool profile_uses_text_output(const ProfileEntity& e) {
  return profile_uses_dhm(e) || e.map_mode != PROFILE_MAP_NONE ||
         str_eq_ci(e.type, "text") || str_eq_ci(e.type, "select") || str_eq_ci(e.type, "button");
}

static void profile_format_dhm(const ProfileEntity& e, int32_t raw, char* out, size_t out_len) {
  if (!out || !out_len) return;
  int64_t minutes = profile_base_to_minutes(e, profile_scaled_base_value(e, raw));
  const bool neg = minutes < 0;
  if (neg) minutes = -minutes;
  const int64_t days = minutes / 1440LL;
  const int64_t hours = (minutes % 1440LL) / 60LL;
  const int64_t mins = minutes % 60LL;
  snprintf(out, out_len, "%s%lldd %lldh %lldm", neg ? "-" : "", (long long)days, (long long)hours, (long long)mins);
}

static void profile_store_read_value(ProfileEntity& e, int32_t raw) {
  if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) {
    if (e.bit_mask) e.bool_value = (((uint32_t)raw & e.bit_mask) != 0);
    else if (raw == e.value_on) e.bool_value = true;
    else if (raw == e.value_off) e.bool_value = false;
    else e.bool_value = raw != 0;
    e.value = e.bool_value ? e.value_on : e.value_off;
    snprintf(e.text_value, sizeof(e.text_value), e.bool_value ? "on" : "off");
    return;
  }
  e.value = profile_transform_numeric(e, raw);
  int32_t map_value = e.value;
  if (e.map_mode == PROFILE_MAP_FLAGS && (e.bit_mask || e.bit_shift)) {
    uint32_t bits = (uint32_t)raw;
    if (e.bit_mask) bits &= e.bit_mask;
    if (e.bit_shift < 32) bits >>= e.bit_shift;
    map_value = (int32_t)bits;
  }
  if (profile_uses_dhm(e)) profile_format_dhm(e, raw, e.text_value, sizeof(e.text_value));
  else if (profile_format_mapped_text(e, map_value, e.text_value, sizeof(e.text_value))) {}
  else snprintf(e.text_value, sizeof(e.text_value), "%ld", (long)e.value);
}

static void profile_copy_line_value(char* dst, size_t dst_len, const String& value) {
  clean_copy(dst, dst_len, value.c_str());
}
static bool parse_u16_value(const String& s, uint16_t& out) {
  if (!s.length()) return false;
  char* endp = nullptr;
  unsigned long v = strtoul(s.c_str(), &endp, 0);
  if (endp == s.c_str() || v > 0xFFFFUL) return false;
  out = (uint16_t)v;
  return true;
}

static uint8_t modbus_func_code(const char* name) {
  if (!name || !*name) return 0;
  if (str_eq_ci(name, "read_coil") || str_eq_ci(name, "read_coils") || str_eq_ci(name, "coil") || str_eq_ci(name, "0x01")) return 0x01;
  if (str_eq_ci(name, "read_discrete") || str_eq_ci(name, "read_discrete_input") || str_eq_ci(name, "discrete") || str_eq_ci(name, "0x02")) return 0x02;
  if (str_eq_ci(name, "read_holding") || str_eq_ci(name, "read_holding_register") || str_eq_ci(name, "holding") || str_eq_ci(name, "0x03")) return 0x03;
  if (str_eq_ci(name, "read_input") || str_eq_ci(name, "read_input_register") || str_eq_ci(name, "input") || str_eq_ci(name, "0x04")) return 0x04;
  if (str_eq_ci(name, "write_coil") || str_eq_ci(name, "write_single_coil") || str_eq_ci(name, "0x05")) return 0x05;
  if (str_eq_ci(name, "write_holding") || str_eq_ci(name, "write_register") || str_eq_ci(name, "write_single_register") || str_eq_ci(name, "0x06")) return 0x06;
  return 0;
}

static bool modbus_func_is_read(uint8_t fc) {
  return fc >= 0x01 && fc <= 0x04;
}

static bool modbus_func_is_write(uint8_t fc) {
  return fc == 0x05 || fc == 0x06;
}

static uint8_t entity_read_func_code(const ProfileEntity& e) {
  uint8_t fc = modbus_func_code(e.read_func);
  if (!fc) fc = modbus_func_code(e.func);
  return modbus_func_is_read(fc) ? fc : 0;
}

static uint8_t entity_write_func_code(const ProfileEntity& e) {
  uint8_t fc = modbus_func_code(e.func);
  return modbus_func_is_write(fc) ? fc : 0;
}

static bool modbus_send_request(ProfileEntity& e, uint8_t fc, uint16_t value) {
  if (!fc) return false;
  const uint8_t slave = e.slave ? e.slave : modbus_slave;
  if (!slave || slave > 247) return false;
  uint8_t frame[8];
  frame[0] = slave;
  frame[1] = fc;
  frame[2] = (uint8_t)(e.reg >> 8);
  frame[3] = (uint8_t)(e.reg & 0xFF);
  if (modbus_func_is_read(fc)) {
    frame[4] = 0;
    frame[5] = 1;
  } else if (fc == 0x05) {
    const bool on = value != 0;
    frame[4] = on ? 0xFF : 0x00;
    frame[5] = 0x00;
  } else if (fc == 0x06) {
    frame[4] = (uint8_t)(value >> 8);
    frame[5] = (uint8_t)(value & 0xFF);
  } else {
    return false;
  }
  uint8_t len = 6;
  if (!append_active_checksum(frame, len, sizeof(frame))) return false;
  pending_modbus_entity_id = e.id;
  pending_modbus_func = fc;
  pending_modbus_ms = millis();
  local_send_raw(frame, len);
  return true;
}


static uint8_t profile_max_entity_index(const String& cfg) {
  uint8_t max_idx = 0;
  int start = 0;
  while (start < cfg.length()) {
    int end = cfg.indexOf('\n', start);
    if (end < 0) end = cfg.length();
    String line = cfg.substring(start, end);
    line.trim();
    if (line.startsWith("entity.")) {
      const char* p = line.c_str() + 7;
      char* endp = nullptr;
      const long idx = strtol(p, &endp, 10);
      if (endp != p && *endp == '.' && idx >= 1 && idx <= PROFILE_ENTITY_MAX && idx > max_idx) {
        max_idx = (uint8_t)idx;
      }
    }
    start = end + 1;
  }
  return max_idx;
}

static bool apply_profile_text(const String& cfg, bool persist) {
  if (!cfg.length()) {
    profile_clear_entities();
    saved_profile_text = "";
    if (persist) prefs.remove("profile_text");
    descriptor_dirty = true;
    return true;
  }
  if (cfg.length() > PROFILE_TEXT_MAX) return false;

  String v = config_value(cfg, "profile");
  if (!v.length()) v = config_value(cfg, "name");
  if (v.length()) profile_copy_line_value(profile_name, sizeof(profile_name), v);
  v = config_value(cfg, "station");
  if (!v.length()) v = config_value(cfg, "device");
  if (v.length()) profile_copy_line_value(station_name, sizeof(station_name), v);
  v = config_value(cfg, "protocol");
  if (!v.length()) v = config_value(cfg, "mode");
  if (v.length()) profile_copy_line_value(protocol_name, sizeof(protocol_name), v);
  v = config_value(cfg, "checksum");
  if (!v.length()) v = config_value(cfg, "checksum_preset");
  if (v.length()) {
    v.toUpperCase();
    if (!valid_checksum_name(v.c_str())) return false;
    profile_copy_line_value(checksum_name, sizeof(checksum_name), v);
  }
  v = config_value(cfg, "slave");
  if (!v.length()) v = config_value(cfg, "slave_id");
  if (v.length()) {
    uint32_t id = (uint32_t)v.toInt();
    if (id == 0 || id > 247) return false;
    modbus_slave = (uint8_t)id;
  }
  v = config_value(cfg, "poll_ms");
  if (v.length()) {
    uint32_t ms = (uint32_t)v.toInt();
    if (ms < 100UL) ms = 100UL;
    if (ms > 60000UL) ms = 60000UL;
    default_poll_ms = (uint16_t)ms;
  }
  v = config_value(cfg, "frame");
  if (!v.length()) v = config_value(cfg, "uart_frame");
  if (v.length()) {
    v.toUpperCase();
    if (!(v == "8N1" || v == "8E1" || v == "8O1" || v == "7E1")) return false;
    profile_copy_line_value(frame_name, sizeof(frame_name), v);
  }
  v = config_value(cfg, "baud");
  if (!v.length()) v = config_value(cfg, "uart_baud");
  if (v.length()) {
    uint32_t baud = (uint32_t)v.toInt();
    if (baud < 300UL || baud > 1000000UL) return false;
    local_baud = baud;
  }

  profile_clear_entities();
  const uint8_t max_entity_index = profile_max_entity_index(cfg);
  for (uint8_t idx = 1; idx <= max_entity_index; ++idx) {
    char prefix[18];
    snprintf(prefix, sizeof(prefix), "entity.%u.", idx);
    String type = config_value(cfg, (String(prefix) + "type").c_str());
    String name = config_value(cfg, (String(prefix) + "name").c_str());
    String key = config_value(cfg, (String(prefix) + "key").c_str());
    String access = config_value(cfg, (String(prefix) + "access").c_str());
    if (!access.length()) access = config_value(cfg, (String(prefix) + "mode").c_str());
    String role = config_value(cfg, (String(prefix) + "role").c_str());
    String func = config_value(cfg, (String(prefix) + "func").c_str());
    String read_func = config_value(cfg, (String(prefix) + "read_func").c_str());
    String reg = config_value(cfg, (String(prefix) + "reg").c_str());
    if (!reg.length()) reg = config_value(cfg, (String(prefix) + "register").c_str());
    String slave = config_value(cfg, (String(prefix) + "slave").c_str());
    String options = config_value(cfg, (String(prefix) + "options").c_str());
    String values = config_value(cfg, (String(prefix) + "values").c_str());
    if (!type.length() && !name.length() && !func.length() && !read_func.length() && !reg.length() && !options.length()) continue;

    ProfileEntity& e = profile_entities[profile_entity_count];
    e.used = true;
    e.id = PROFILE_ENTITY_BASE_ID + profile_entity_count;
    v = config_value(cfg, (String(prefix) + "id").c_str());
    if (v.length()) {
      uint32_t id = (uint32_t)v.toInt();
      if (id >= 20 && id <= 249) e.id = (uint8_t)id;
    }
    if (!type.length()) {
      const uint8_t fc_tmp = modbus_func_code(func.c_str());
      type = (fc_tmp == 0x01 || fc_tmp == 0x02 || fc_tmp == 0x05) ? "switch" : (options.length() ? "select" : "number");
    }
    type.toLowerCase();
    if (type == "binary") type = "binary_sensor";
    if (type != "sensor" && type != "number" && type != "switch" && type != "binary_sensor" && type != "text" && type != "select" && type != "button") return false;
    profile_copy_line_value(e.type, sizeof(e.type), type);
    access.toLowerCase();
    if (access.length() && access != "ro" && access != "rw" && access != "wo") return false;
    if (!access.length()) {
      const uint8_t fc_tmp = modbus_func_code(func.c_str());
      const uint8_t rfc_tmp = modbus_func_code(read_func.c_str());
      const bool readable = modbus_func_is_read(fc_tmp) || modbus_func_is_read(rfc_tmp);
      const bool writable = modbus_func_is_write(fc_tmp);
      if (readable && writable) access = "rw";
      else if (writable || type == "button") access = "wo";
      else access = "ro";
    }
    profile_copy_line_value(e.access, sizeof(e.access), access);
    if (!key.length()) key = name.length() ? name : String("entity") + idx;
    sanitize_key_copy(e.key, sizeof(e.key), key.c_str(), idx);
    if (!name.length()) name = key;
    profile_copy_line_value(e.name, sizeof(e.name), name);
    role.toLowerCase();
    role.replace("-", "_");
    role.replace(" ", "_");
    if (role.length() > 0 && role != "main_input" && role != "main_output_enable" && role != "main_output_power" && role != "main_output_rpm" && role != "input" && role != "output" && role != "output_enable" && role != "output_power") return false;
    profile_copy_line_value(e.role, sizeof(e.role), role);
    v = config_value(cfg, (String(prefix) + "unit").c_str());
    profile_copy_line_value(e.unit, sizeof(e.unit), v);
    v = config_value(cfg, (String(prefix) + "min").c_str());
    parse_i32(v, e.min_value);
    v = config_value(cfg, (String(prefix) + "max").c_str());
    parse_i32(v, e.max_value);
    v = config_value(cfg, (String(prefix) + "step").c_str());
    parse_i32(v, e.step_value);
    if (e.step_value <= 0) e.step_value = 1;
    v = config_value(cfg, (String(prefix) + "value_on").c_str());
    parse_i32(v, e.value_on);
    v = config_value(cfg, (String(prefix) + "value_off").c_str());
    parse_i32(v, e.value_off);
    v = config_value(cfg, (String(prefix) + "scale").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "multiplier").c_str());
    if (v.length()) {
      parse_i32(v, e.scale_value);
      if (e.scale_value == 0) e.scale_value = 1;
    }
    v = config_value(cfg, (String(prefix) + "divisor").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "divider").c_str());
    if (v.length()) {
      uint32_t div = 1;
      if (parse_u32_profile_value(v, div) && div) e.divisor_value = div;
    }
    v = config_value(cfg, (String(prefix) + "offset").c_str());
    if (v.length()) parse_i32(v, e.offset_value);
    v = config_value(cfg, (String(prefix) + "bitmask").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "mask").c_str());
    if (v.length()) {
      uint32_t mask = 0;
      if (parse_u32_profile_value(v, mask)) e.bit_mask = mask;
    }
    v = config_value(cfg, (String(prefix) + "bit_shift").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "shift").c_str());
    if (v.length()) {
      uint32_t shift = (uint32_t)v.toInt();
      if (shift > 31U) shift = 31U;
      e.bit_shift = (uint8_t)shift;
    }
    v = config_value(cfg, (String(prefix) + "time_base").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "time_unit").c_str());
    if (v.length()) e.time_base = profile_parse_time_base(v);
    v = config_value(cfg, (String(prefix) + "time_display").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "time_format").c_str());
    if (v.length()) e.time_display = profile_parse_time_display(v);
    if (e.time_base == PROFILE_TIME_NONE) e.time_display = PROFILE_TIME_RAW;
    v = config_value(cfg, (String(prefix) + "map_mode").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "mapping_mode").c_str());
    if (v.length()) e.map_mode = profile_parse_map_mode(v);
    v = config_value(cfg, (String(prefix) + "map").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "value_map").c_str());
    if (v.length()) {
      profile_copy_line_value(e.value_map, sizeof(e.value_map), v);
      if (e.map_mode == PROFILE_MAP_NONE) e.map_mode = PROFILE_MAP_EXACT;
    }
    v = config_value(cfg, (String(prefix) + "map_default").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "default_text").c_str());
    if (v.length()) profile_copy_line_value(e.map_default, sizeof(e.map_default), v);
    v = config_value(cfg, (String(prefix) + "poll_ms").c_str());
    if (v.length()) {
      uint32_t ms = (uint32_t)v.toInt();
      if (ms < 100UL) ms = 100UL;
      if (ms > 60000UL) ms = 60000UL;
      e.poll_ms = (uint16_t)ms;
    } else {
      e.poll_ms = default_poll_ms;
    }
    func.toLowerCase();
    read_func.toLowerCase();

    // Derive missing Modbus functions from access instead of silently treating
    // every entity as a read-only register. RW gets a write function plus an
    // explicit/inferred read function; WO never gains an implicit read path.
    if (!func.length()) {
      if (profile_entity_writable(e)) func = (type == "switch" || type == "binary_sensor") ? "write_coil" : "write_holding";
      else func = (type == "switch" || type == "binary_sensor") ? "read_coil" : "read_holding";
    }
    const uint8_t func_code = modbus_func_code(func.c_str());
    if (!func_code) return false;
    if (profile_entity_readable(e) && !read_func.length() && modbus_func_is_write(func_code))
      read_func = (func_code == 0x05) ? "read_coil" : "read_holding";
    if (read_func.length() && !modbus_func_code(read_func.c_str())) return false;
    profile_copy_line_value(e.func, sizeof(e.func), func);
    profile_copy_line_value(e.read_func, sizeof(e.read_func), read_func);
    if (profile_entity_readable(e) && !entity_read_func_code(e)) return false;
    if (profile_entity_writable(e) && !entity_write_func_code(e)) return false;
    uint16_t reg_value = 0;
    if (!parse_u16_value(reg, reg_value)) return false;
    e.reg = reg_value;
    if (slave.length()) {
      uint32_t id = (uint32_t)slave.toInt();
      if (id == 0 || id > 247) return false;
      e.slave = (uint8_t)id;
    }
    profile_copy_line_value(e.options, sizeof(e.options), options);
    profile_copy_line_value(e.values, sizeof(e.values), values);
    snprintf(e.text_value, sizeof(e.text_value), "-");
    ++profile_entity_count;
    if (profile_entity_count >= PROFILE_ENTITY_MAX) break;
  }

  saved_profile_text = cfg;
  local_serial_config = serial_config_from_frame(frame_name);
  if (persist) {
    prefs.putString("profile_text", cfg);
    save_profile_config();
  }
  descriptor_dirty = true;
  restart_local_uart();
  return true;
}

static ProfileEntity* profile_find_entity(uint8_t id) {
  for (uint8_t i = 0; i < profile_entity_count; ++i) if (profile_entities[i].used && profile_entities[i].id == id) return &profile_entities[i];
  return nullptr;
}

static ProfileEntity* modbus_find_entity_for_response(uint8_t slave, uint8_t fc, uint16_t reg) {
  if (pending_modbus_entity_id) {
    ProfileEntity* e = profile_find_entity(pending_modbus_entity_id);
    if (e && (e->slave ? e->slave : modbus_slave) == slave) return e;
  }
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    ProfileEntity& e = profile_entities[i];
    if (!e.used) continue;
    if ((e.slave ? e.slave : modbus_slave) != slave) continue;
    if (e.reg != reg) continue;
    const uint8_t rfc = profile_entity_readable(e) ? entity_read_func_code(e) : 0;
    const uint8_t wfc = profile_entity_writable(e) ? entity_write_func_code(e) : 0;
    if (rfc == fc || wfc == fc) return &e;
  }
  return nullptr;
}

static bool process_profile_rx(const uint8_t* data, uint8_t len) {
  if (!data || len < 5) return false;
  const uint8_t slave = data[0];
  uint8_t fc = data[1];
  if (slave == 0 || slave > 247) return false;
  if (fc & 0x80) {
    pending_modbus_entity_id = 0;
    return true;
  }

  ProfileEntity* e = nullptr;
  uint16_t echo_reg = 0;
  if (fc == 0x05 || fc == 0x06) {
    if (len < 8) return false;
    echo_reg = ((uint16_t)data[2] << 8) | data[3];
    e = modbus_find_entity_for_response(slave, fc, echo_reg);
    if (!e) return true;
    // A write echo confirms transport only; it is not a physical readback.
    // RW is immediately followed by its configured read function. WO has no
    // readback by definition.
    if (profile_entity_readable(*e)) {
      profile_readback_entity_id = e->id;
      e->last_poll_ms = 0;
    }
    pending_modbus_entity_id = 0;
    return true;
  }

  if (fc >= 0x01 && fc <= 0x04) {
    if (len < 5) return false;
    e = pending_modbus_entity_id ? profile_find_entity(pending_modbus_entity_id) : nullptr;
    if (!e) {
      for (uint8_t i = 0; i < profile_entity_count; ++i) {
        if (profile_entities[i].used && profile_entity_readable(profile_entities[i]) &&
            (profile_entities[i].slave ? profile_entities[i].slave : modbus_slave) == slave &&
            entity_read_func_code(profile_entities[i]) == fc) {
          e = &profile_entities[i];
          break;
        }
      }
    }
    if (!e || !profile_entity_readable(*e)) { pending_modbus_entity_id = 0; return true; }
    if (fc == 0x01 || fc == 0x02) {
      if (len < 6 || data[2] < 1) return false;
      profile_store_read_value(*e, (data[3] & 0x01) != 0 ? 1 : 0);
    } else {
      if (len < 7 || data[2] < 2) return false;
      const uint16_t raw = ((uint16_t)data[3] << 8) | data[4];
      profile_store_read_value(*e, raw);
    }
    e->last_update_ms = millis();
    pending_modbus_entity_id = 0;
    return true;
  }
  return true;
}

static bool profile_send_entity(ProfileEntity& e, const uint8_t* data, uint8_t len) {
  if (!profile_entity_writable(e)) return false;
  const bool preserve_actual = profile_entity_readable(e);  // RW: command is not readback
  const bool old_bool_value = e.bool_value;
  const int32_t old_value = e.value;
  const uint32_t old_update_ms = e.last_update_ms;
  char old_text_value[sizeof(e.text_value)];
  memcpy(old_text_value, e.text_value, sizeof(old_text_value));
  char input[34];
  uint8_t n = len;
  if (n >= sizeof(input)) n = sizeof(input) - 1;
  for (uint8_t i = 0; i < n; ++i) input[i] = (char)data[i];
  input[n] = 0;
  String s(input);
  s.trim();
  int32_t num = atol(s.c_str());
  String lower = s;
  lower.toLowerCase();
  bool want_on = lower == "1" || lower == "on" || lower == "true" || lower == "ein" || num == e.value_on;
  bool want_off = lower == "0" || lower == "off" || lower == "false" || lower == "aus" || num == e.value_off;
  const uint8_t fc = entity_write_func_code(e);
  if (!fc) return false;
  uint16_t raw = 0;
  if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) {
    const bool state = want_on && !want_off;
    raw = state ? (uint16_t)e.value_on : (uint16_t)e.value_off;
    if (fc == 0x05) raw = state ? 1 : 0;
    e.bool_value = state;
    e.value = state ? e.value_on : e.value_off;
    snprintf(e.text_value, sizeof(e.text_value), state ? "on" : "off");
  } else if (str_eq_ci(e.type, "button")) {
    raw = e.value_on ? (uint16_t)e.value_on : 1;
    snprintf(e.text_value, sizeof(e.text_value), "sent");
  } else {
    if (profile_uses_dhm(e)) return false;
    int32_t display_num = num;
    const bool output_power_role = str_eq_ci(e.role, "main_output_power") || str_eq_ci(e.role, "output_power");
    if (!(output_power_role && display_num == 0)) {
      if (display_num < e.min_value) display_num = e.min_value;
      if (display_num > e.max_value) display_num = e.max_value;
    }
    int32_t raw_num = profile_inverse_numeric(e, display_num);
    if (raw_num < 0) raw_num = 0;
    if (raw_num > 65535L) raw_num = 65535L;
    raw = (uint16_t)raw_num;
    e.value = display_num;
    snprintf(e.text_value, sizeof(e.text_value), "%ld", (long)e.value);
  }
  const uint32_t write_ms = millis();
  e.last_update_ms = write_ms;
  profile_poll_pause_until_ms = write_ms + 120UL;
  if (preserve_actual) {
    e.bool_value = old_bool_value;
    e.value = old_value;
    e.last_update_ms = old_update_ms;
    memcpy(e.text_value, old_text_value, sizeof(e.text_value));
    profile_readback_entity_id = e.id;
    e.last_poll_ms = 0;
  }
  const bool sent = modbus_send_request(e, fc, raw);
  if (sent && !preserve_actual && !str_eq_ci(e.type, "button")) {
    // WO: keep the last issued command as runtime target/shadow. A Modbus
    // write response confirms transport/device acceptance, but it is not a
    // physical readback; the shadow is therefore deliberately kept separate
    // from RW semantics.
    e.command_shadow_valid = true;
  }
  return sent;
}


static bool profile_output_entity(const ProfileEntity& e) {
  return str_eq_ci(e.role, "main_output_enable") || str_eq_ci(e.role, "output_enable") ||
         str_eq_ci(e.role, "main_output_power") || str_eq_ci(e.role, "output_power");
}

// Only the explicitly selected OFE main-output roles represent the extractor.
// Generic output_* roles remain normal controllable/failsafe outputs, but they
// must not drive the blue EXTRACTOR_ON status LED or main-output telemetry.
static bool profile_output_enable_entity(const ProfileEntity& e) {
  return str_eq_ci(e.role, "main_output_enable");
}

static bool profile_output_power_entity(const ProfileEntity& e) {
  return str_eq_ci(e.role, "main_output_power");
}

static bool profile_output_rpm_entity(const ProfileEntity& e) {
  return str_eq_ci(e.role, "main_output_rpm");
}

static uint16_t profile_output_rpm_value() {
  const ProfileEntity* fallback = nullptr;
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    const ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_entity_readable(e)) continue;
    if (!str_eq_ci(e.type, "sensor") && !str_eq_ci(e.type, "number")) continue;
    if (!profile_output_rpm_entity(e)) {
      // Backwards compatibility for older profiles that only declared unit=rpm.
      if (!fallback && str_eq_ci(e.unit, "rpm")) fallback = &e;
      continue;
    }
    int32_t rpm = e.value;
    if (rpm < 0) rpm = 0;
    if (rpm > 65535L) rpm = 65535L;
    return (uint16_t)rpm;
  }
  if (fallback) {
    int32_t rpm = fallback->value;
    if (rpm < 0) rpm = 0;
    if (rpm > 65535L) rpm = 65535L;
    return (uint16_t)rpm;
  }
  return 0;
}

static bool profile_entity_bool_active(const ProfileEntity& e) {
  // Readable entities use confirmed device state. WO entities use only an
  // explicit command shadow; their zero-initialized value must never be
  // interpreted as an active output (important when value_on is configured 0).
  if (!profile_entity_readable(e) && !e.command_shadow_valid) return false;
  if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) return e.bool_value;
  return e.value != 0 && e.value != e.value_off;
}

static bool profile_output_enabled_active() {
  bool saw_enable = false;
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    const ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_output_enable_entity(e)) continue;
    saw_enable = true;
    if (profile_entity_bool_active(e)) return true;
  }
  if (saw_enable) return false;
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    const ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_output_power_entity(e)) continue;
    // A WO power-only main output has no physical readback. Do not interpret
    // its default/minimum value as ON before an explicit command was sent.
    if (!profile_entity_readable(e) && !e.command_shadow_valid) continue;
    if (e.value > 0) return true;
  }
  return false;
}

static uint16_t profile_output_power_permille(bool enabled) {
  if (!enabled) return 0;
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    const ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_output_power_entity(e)) continue;
    if (!profile_entity_readable(e) && !e.command_shadow_valid) continue;
    int32_t v = e.value;
    if (v < 0) v = 0;
    if (e.max_value <= 100) v *= 10;
    if (v > 1000) v = 1000;
    return (uint16_t)v;
  }
  return 1000;
}

static bool local_device_online_now(uint32_t now) {
  return last_local_rx_ms && (uint32_t)(now - last_local_rx_ms) <= 15000UL;
}

static void update_local_fault_state(uint32_t now) {
  if (last_local_rx_ms && !local_device_online_now(now)) fault_mask |= FAULT_LOCAL_UART_INACTIVE;
  else fault_mask &= (uint16_t)~FAULT_LOCAL_UART_INACTIVE;
}

static void output_off() {
  static const uint8_t off_payload[] = {'0'};
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_output_entity(e)) continue;
    profile_send_entity(e, off_payload, sizeof(off_payload));
  }
  output_failsafe_active = true;
}

static void check_output_failsafe() {
  if (!last_master_ms) return;
  if ((uint32_t)(millis() - last_master_ms) <= OUTPUT_FAILSAFE_TIMEOUT_MS) {
    output_failsafe_active = false;
    return;
  }
  if (!output_failsafe_active) output_off();
}
static void profile_poll_tick() {
  if (!profile_entity_count) return;
  const uint32_t now = millis();
  if (pending_modbus_entity_id && (uint32_t)(now - pending_modbus_ms) < 250UL) return;
  if (pending_modbus_entity_id && (uint32_t)(now - pending_modbus_ms) >= 250UL) pending_modbus_entity_id = 0;
  if (profile_poll_pause_until_ms && (int32_t)(now - profile_poll_pause_until_ms) < 0) return;
  if ((uint32_t)(now - last_profile_poll_ms) < 50UL) return;
  last_profile_poll_ms = now;
  if (profile_readback_entity_id) {
    ProfileEntity* pending = profile_find_entity(profile_readback_entity_id);
    profile_readback_entity_id = 0;
    if (pending && pending->used && profile_entity_readable(*pending)) {
      const uint8_t fc = entity_read_func_code(*pending);
      if (fc) {
        pending->last_poll_ms = now;
        modbus_send_request(*pending, fc, 0);
        return;
      }
    }
  }
  for (uint8_t tries = 0; tries < profile_entity_count; ++tries) {
    profile_poll_cursor = (uint8_t)(profile_poll_cursor % profile_entity_count);
    ProfileEntity& e = profile_entities[profile_poll_cursor++];
    if (!e.used || !profile_entity_readable(e)) continue;
    const uint8_t fc = entity_read_func_code(e);
    if (!fc) continue;
    if (e.last_poll_ms && (uint32_t)(now - e.last_poll_ms) < e.poll_ms) continue;
    e.last_poll_ms = now;
    modbus_send_request(e, fc, 0);
    break;
  }
}

static void handle_save_config(const Frame& req) {
  if (req.len == 0 || req.len >= MAX_PAYLOAD) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }

  String cfg;
  cfg.reserve(req.len + 1);
  for (uint8_t i = 0; i < req.len; ++i) cfg += (char)req.payload[i];

  String v = config_value(cfg, "profile");
  if (v.length()) clean_copy(profile_name, sizeof(profile_name), v.c_str());
  v = config_value(cfg, "station");
  if (v.length()) clean_copy(station_name, sizeof(station_name), v.c_str());
  v = config_value(cfg, "protocol");
  if (v.length()) clean_copy(protocol_name, sizeof(protocol_name), v.c_str());
  v = config_value(cfg, "checksum");
  if (v.length()) {
    v.toUpperCase();
    if (!valid_checksum_name(v.c_str())) {
      send_status_response(req, STATUS_BAD_VALUE);
      return;
    }
    clean_copy(checksum_name, sizeof(checksum_name), v.c_str());
  }
  v = config_value(cfg, "frame");
  if (v.length()) {
    v.toUpperCase();
    if (v == "8N1" || v == "8E1" || v == "8O1" || v == "7E1") clean_copy(frame_name, sizeof(frame_name), v.c_str());
    else {
      send_status_response(req, STATUS_BAD_VALUE);
      return;
    }
  }
  v = config_value(cfg, "baud");
  if (v.length()) {
    uint32_t baud = (uint32_t)v.toInt();
    if (baud < 300UL || baud > 1000000UL) {
      send_status_response(req, STATUS_BAD_VALUE);
      return;
    }
    local_baud = baud;
  }
  v = config_value(cfg, "slave");
  if (!v.length()) v = config_value(cfg, "slave_id");
  if (v.length()) {
    uint32_t id = (uint32_t)v.toInt();
    if (id == 0 || id > 247) {
      send_status_response(req, STATUS_BAD_VALUE);
      return;
    }
    modbus_slave = (uint8_t)id;
  }
  v = config_value(cfg, "poll_ms");
  if (v.length()) {
    uint32_t ms = (uint32_t)v.toInt();
    if (ms < 100UL) ms = 100UL;
    if (ms > 60000UL) ms = 60000UL;
    default_poll_ms = (uint16_t)ms;
  }

  local_serial_config = serial_config_from_frame(frame_name);
  save_profile_config();
  restart_local_uart();
  descriptor_dirty = true;
  send_status_response(req, STATUS_OK);
}

static void handle_profile_begin(const Frame& req) {
  if (req.len < 8) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint32_t total_len = get_u32_le(req.payload);
  const uint32_t crc = get_u32_le(req.payload + 4);
  if (total_len > PROFILE_TEXT_MAX) {
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  upload_profile_text = "";
  upload_profile_text.reserve(total_len + 8);
  upload_profile_active = true;
  upload_profile_expected_len = total_len;
  upload_profile_expected_crc = crc;
  send_status_response(req, STATUS_OK);
}

static void handle_profile_chunk(const Frame& req) {
  if (!upload_profile_active) {
    send_status_response(req, STATUS_BUSY);
    return;
  }
  if (req.len < 2) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint16_t offset = get_u16_le(req.payload);
  const uint8_t n = req.len - 2;
  if (offset != upload_profile_text.length() || (uint32_t)offset + n > upload_profile_expected_len) {
    upload_profile_active = false;
    upload_profile_text = "";
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  for (uint8_t i = 0; i < n; ++i) upload_profile_text += (char)req.payload[2 + i];
  send_status_response(req, STATUS_OK);
}

static void handle_profile_end(const Frame& req) {
  (void)req;
  if (!upload_profile_active) {
    send_status_response(req, STATUS_BUSY);
    return;
  }
  if (upload_profile_text.length() != upload_profile_expected_len) {
    upload_profile_active = false;
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  const uint32_t crc = fnv1a32((const uint8_t*)upload_profile_text.c_str(), upload_profile_text.length());
  if (crc != upload_profile_expected_crc) {
    upload_profile_active = false;
    send_status_response(req, STATUS_CRC_ERROR);
    return;
  }
  if (!apply_profile_text(upload_profile_text, true)) {
    upload_profile_active = false;
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  upload_profile_active = false;
  restart_local_uart();
  descriptor_dirty = true;
  send_status_response(req, STATUS_OK);
}

static void handle_set_address_uid(const Frame& req) {
  if (req.len < 9) return;
  const uint64_t target_uid = get_u64_le(req.payload);
  if (target_uid != module_uid()) return;
  const uint8_t next_addr = req.payload[8];
  if (!valid_module_addr(next_addr)) {
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  module_addr = next_addr;
  prefs.putUChar("addr", module_addr);
  send_status_response(req, STATUS_OK);
  delay(30);
}

static uint8_t status_led_sync_brightness(uint8_t pct) {
  pct = constrain(pct, (uint8_t)10, (uint8_t)100);
  return (uint8_t)((uint16_t)pct * 255U / 100U);
}

static void handle_led_sync(const Frame& req) {
  if (req.src == ADDR_MASTER) last_master_ms = millis();
  if (req.len >= 4) ofe_status_leds.syncClock(get_u32_le(req.payload));
  if (req.len >= 6) ofe_status_leds.setBrightness(req.payload[4] ? status_led_sync_brightness(req.payload[5]) : 0);
}
static void handle_frame(const Frame& req) {
  if (req.dst != module_addr && req.dst != ADDR_BROADCAST && req.dst != ADDR_FACTORY) return;
  if (req.dst == ADDR_BROADCAST && req.cmd == CMD_LED_SYNC) {
    handle_led_sync(req);
    return;
  }
  if (req.dst == ADDR_BROADCAST && req.cmd != CMD_DISCOVER_MODULES && req.cmd != CMD_SET_ADDRESS_UID) return;
  if (req.dst == ADDR_FACTORY && req.cmd != CMD_SET_ADDRESS_UID) return;
  last_master_ms = millis();

  switch (req.cmd) {
    case CMD_PING:
      send_status_response(req, STATUS_OK);
      break;
    case CMD_INFO:
      handle_info(req);
      break;
    case CMD_GET_CAPS:
      handle_caps(req);
      break;
    case CMD_GET_STATUS:
      handle_status(req);
      break;
    case CMD_GET_TELEMETRY:
      handle_telemetry(req);
      break;
    case CMD_DISCOVER_MODULES:
      handle_discover(req);
      break;
    case CMD_SET_ADDRESS_UID:
      handle_set_address_uid(req);
      break;
    case CMD_SET_ADDRESS:
      if (req.len != 1 || !valid_module_addr(req.payload[0])) send_status_response(req, STATUS_BAD_VALUE);
      else {
        module_addr = req.payload[0];
        prefs.putUChar("addr", module_addr);
        send_status_response(req, STATUS_OK);
      }
      break;
    case CMD_SET_LABEL:
      handle_set_label(req);
      break;
    case CMD_SAVE_CONFIG:
      handle_save_config(req);
      break;
    case CMD_TRACE_CONTROL:
      handle_trace_control(req);
      break;
    case CMD_TRACE_READ:
      handle_trace_read(req);
      break;
    case CMD_DESCRIPTOR_GET:
      handle_descriptor_get(req);
      break;
    case CMD_ENTITY_GET:
      handle_entity_get(req);
      break;
    case CMD_ENTITY_SET:
      handle_entity_set(req);
      break;
    case CMD_FAULT_MAP_GET:
      handle_fault_map_get(req);
      break;
    case CMD_PROFILE_GET:
      handle_profile_get(req);
      break;
    case CMD_PROFILE_BEGIN:
      handle_profile_begin(req);
      break;
    case CMD_PROFILE_CHUNK:
      handle_profile_chunk(req);
      break;
    case CMD_PROFILE_END:
      handle_profile_end(req);
      break;
    case CMD_FW_BEGIN:
      handle_fw_begin(req);
      break;
    case CMD_FW_CHUNK:
      handle_fw_chunk(req);
      break;
    case CMD_FW_END:
      handle_fw_end(req);
      break;
    case CMD_FW_STATUS:
      handle_fw_status(req);
      break;
    case CMD_FW_ABORT:
      fw_update_abort_local();
      send_status_response(req, STATUS_OK);
      break;
    case CMD_FW_REBOOT:
      send_status_response(req, STATUS_OK);
      delay(100);
      ESP.restart();
      break;
    default:
      send_status_response(req, STATUS_UNKNOWN_CMD);
      break;
  }
}

static void poll_rs485() {
  Frame req;
  uint8_t frames = 0;
  while (frames < 8 && bus.poll(req)) {
    handle_frame(req);
    ++frames;
  }
  if (frames >= 8) yield();
}

void setup() {
  ofe_keep_module_fw_signature();
  ofe_status_leds.begin();
  bus.setActivityCallback([]() { ofe_status_leds.pulseBusActivity(); });
#if DEBUG_SERIAL_ENABLE
  Serial.begin(115200);
#endif
  prefs.begin("ofe-modbus", false);
  module_addr = prefs.getUChar("addr", DEFAULT_MODULE_ADDR);
  if (!valid_module_addr(module_addr)) module_addr = DEFAULT_MODULE_ADDR;
  String saved_label = prefs.getString("label", "");
  saved_label.toCharArray(module_label, sizeof(module_label));

  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  load_profile_config();
  {
    String pt = prefs.getString("profile_text", "");
    if (pt.length()) apply_profile_text(pt, false);
  }
  LOCAL.begin(local_baud, local_serial_config, LOCAL_RX_PIN, LOCAL_TX_PIN);
  next_join_announce_ms = millis() + join_delay_ms(0);
}

void loop() {
  const uint32_t led_now = millis();
  update_local_fault_state(led_now);
  const bool bus_online = last_master_ms && (uint32_t)(led_now - last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS;
  const bool local_online = local_device_online_now(led_now);
  const bool output_active = profile_output_enabled_active();
  ofe_status_leds.setBusOnline(bus_online);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  const bool critical_fault = (fault_mask & (uint16_t)~FAULT_LOCAL_UART_INACTIVE) != 0;
  ofe_status_leds.setModuleEvent(fault_mask ? (critical_fault ? OFE_LED_EVENT_CRITICAL : OFE_LED_EVENT_WARNING) : (output_active ? OFE_LED_EVENT_EXTRACTOR_ON : (local_online ? OFE_LED_EVENT_DEVICE_ONLINE : OFE_LED_EVENT_WARNING)));
  ofe_status_leds.tick();
  const uint32_t loop_start = micros();
  fw_update_check_timeout();
  poll_rs485();
  poll_pending_discover_response();
  poll_join_announce();
  if (!fw_update_active) {
    localBusPoll();
    profile_poll_tick();
    check_output_failsafe();
  } else {
    check_output_failsafe();
  }
  record_loop_time((uint32_t)(micros() - loop_start));
  delay(1);
}
