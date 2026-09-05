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
#define OFE_MODULE_FW_PATCH 66
#define OFE_MODULE_FW_SUFFIX "alpha"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=UNIVERSAL_RS232;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}
static const uint8_t DEFAULT_MODULE_ADDR = 0x50;
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
static char profile_name[32] = "Generic RS232";
static char station_name[32] = "Community device";
static char frame_name[5] = "8N1";
static char protocol_name[16] = "ASCII";
static char line_end_name[8] = "CR";
static char checksum_name[24] = "NONE";
// Binary protocol framing. ASCII/Weller profiles ignore these fields.
static char binary_frame_name[16] = "IDLE";       // IDLE, FIXED, LENGTH_U8, LENGTH_U16_LE, LENGTH_U16_BE
static char binary_start_hex[48] = {0};            // optional sync prefix, e.g. AA55 or AA 55
static uint16_t binary_rx_length = 0;              // FIXED total frame length
static uint8_t binary_length_offset = 0;           // offset of length field
static int16_t binary_length_adjust = 0;           // total frame len = field + adjust
static uint8_t binary_start_bytes[16] = {0};
static uint8_t binary_start_len = 0;
static uint32_t local_baud = LOCAL_BAUD;
static uint32_t local_serial_config = LOCAL_SERIAL_CONFIG;
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

static uint8_t rx_packet[192];
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
  // In ASCII mode these hold literal text/patterns. In BINARY mode they hold
  // hexadecimal byte templates, e.g. "AA 10 {value:u16be}" and "AA 90 ?? ??".
  char poll[80] = {0};
  char match[80] = {0};
  char set_cmd[80] = {0};
  char set_on[80] = {0};
  char set_off[80] = {0};
  uint8_t binary_match_offset = 0;
  uint8_t binary_value_offset = 0;
  uint8_t binary_value_len = 0;
  char binary_value_type[12] = "u8";
  uint8_t binary_match_len = 0;
  uint8_t binary_match_bytes[32] = {0};
  uint8_t binary_match_masks[32] = {0};
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
  uint16_t repeat_on_ms = 0;
  uint16_t repeat_off_ms = 0;
  uint32_t last_repeat_ms = 0;
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
static void profile_store_read_value(ProfileEntity& e, int32_t raw, const char* raw_text);
static const char* profile_time_base_name(uint8_t v);
static const char* profile_time_display_name(uint8_t v);
static const char* profile_map_mode_name(uint8_t v);
static bool profile_uses_dhm(const ProfileEntity& e);
static bool profile_uses_text_output(const ProfileEntity& e);
static bool append_profile_entity_value(uint8_t* p, uint8_t& o, const ProfileEntity& e);
static void normalize_weller_command(String& cmd, const ProfileEntity& e, int32_t value_num);
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

static const char fault_map_text[] =
  "0x0001,warn,Local UART inactive,Lokaler UART inaktiv,0\n";



enum LocalChecksumType : uint8_t {
  LOCAL_CS_NONE = 0,
  LOCAL_CS_WELLER_SUM8 = 1,
  LOCAL_CS_XOR8_HEX = 2,
  LOCAL_CS_SUM8_HEX = 3,
  LOCAL_CS_CRC16_MODBUS_LE = 4,
  LOCAL_CS_XOR8_RAW = 5,
  LOCAL_CS_SUM8_RAW = 6,
  LOCAL_CS_CRC16_CCITT_BE = 7,
  LOCAL_CS_CRC16_CCITT_LE = 8,
};

static LocalChecksumType checksum_type_from_name(const char* name);
static LocalChecksumType active_checksum_type();
static int hex_nibble(uint8_t c);
static bool is_binary_protocol();
static bool binary_refresh_config();
static bool parse_hex_sequence(const char* text, uint8_t* out, uint8_t max_len, uint8_t& out_len, bool allow_wildcards = false, uint8_t* masks = nullptr);
static bool binary_build_template(const char* text, int32_t value, uint8_t* out, uint8_t max_len, uint8_t& out_len);
static bool binary_compile_entity_match(ProfileEntity& e);
static bool binary_extract_entity_value(const ProfileEntity& e, const uint8_t* data, uint8_t len, int32_t& value, char* text, size_t text_len);

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

static LocalChecksumType checksum_type_from_name(const char* name) {
  if (!name || !*name) return LOCAL_CS_NONE;
  if (str_eq_ci(name, "NONE") || str_eq_ci(name, "OFF")) return LOCAL_CS_NONE;
  if (str_eq_ci(name, "WELLER") || str_eq_ci(name, "WELLER_SUM8") || str_eq_ci(name, "WELLER_ASCII")) return LOCAL_CS_WELLER_SUM8;
  if (str_eq_ci(name, "XOR8") || str_eq_ci(name, "XOR8_HEX")) return LOCAL_CS_XOR8_HEX;
  if (str_eq_ci(name, "SUM8") || str_eq_ci(name, "SUM8_HEX")) return LOCAL_CS_SUM8_HEX;
  if (str_eq_ci(name, "CRC16") || str_eq_ci(name, "CRC16_MODBUS") || str_eq_ci(name, "CRC16_MODBUS_LE")) return LOCAL_CS_CRC16_MODBUS_LE;
  if (str_eq_ci(name, "XOR8_RAW") || str_eq_ci(name, "XOR8_BIN")) return LOCAL_CS_XOR8_RAW;
  if (str_eq_ci(name, "SUM8_RAW") || str_eq_ci(name, "SUM8_BIN")) return LOCAL_CS_SUM8_RAW;
  if (str_eq_ci(name, "CRC16_CCITT") || str_eq_ci(name, "CRC16_CCITT_BE")) return LOCAL_CS_CRC16_CCITT_BE;
  if (str_eq_ci(name, "CRC16_CCITT_LE")) return LOCAL_CS_CRC16_CCITT_LE;
  return LOCAL_CS_NONE;
}

static LocalChecksumType active_checksum_type() {
  LocalChecksumType t = checksum_type_from_name(checksum_name);
  if (t != LOCAL_CS_NONE) return t;
  if (str_eq_ci(protocol_name, "WELLER") || str_eq_ci(protocol_name, "WELLER_ASCII")) return LOCAL_CS_WELLER_SUM8;
  return LOCAL_CS_NONE;
}

static const char* checksum_type_label() {
  switch (active_checksum_type()) {
    case LOCAL_CS_WELLER_SUM8: return "WELLER_SUM8";
    case LOCAL_CS_XOR8_HEX: return "XOR8_HEX";
    case LOCAL_CS_SUM8_HEX: return "SUM8_HEX";
    case LOCAL_CS_CRC16_MODBUS_LE: return "CRC16_MODBUS_LE";
    case LOCAL_CS_XOR8_RAW: return "XOR8_RAW";
    case LOCAL_CS_SUM8_RAW: return "SUM8_RAW";
    case LOCAL_CS_CRC16_CCITT_BE: return "CRC16_CCITT_BE";
    case LOCAL_CS_CRC16_CCITT_LE: return "CRC16_CCITT_LE";
    case LOCAL_CS_NONE:
    default: return "NONE";
  }
}

static bool valid_checksum_name(const char* value) {
  if (!value || !*value) return true;
  return checksum_type_from_name(value) != LOCAL_CS_NONE || str_eq_ci(value, "NONE") || str_eq_ci(value, "OFF");
}

static bool is_binary_protocol() {
  return str_eq_ci(protocol_name, "BINARY") || str_eq_ci(protocol_name, "BIN") || str_eq_ci(protocol_name, "BINARY_UART");
}

enum BinaryFrameMode : uint8_t {
  BINARY_FRAME_IDLE = 0,
  BINARY_FRAME_FIXED = 1,
  BINARY_FRAME_LENGTH_U8 = 2,
  BINARY_FRAME_LENGTH_U16_LE = 3,
  BINARY_FRAME_LENGTH_U16_BE = 4,
};

// Explicit prototype prevents the Arduino .ino preprocessor from generating
// this declaration before BinaryFrameMode is known.
static BinaryFrameMode active_binary_frame_mode();

static BinaryFrameMode active_binary_frame_mode() {
  if (str_eq_ci(binary_frame_name, "FIXED")) return BINARY_FRAME_FIXED;
  if (str_eq_ci(binary_frame_name, "LENGTH_U8")) return BINARY_FRAME_LENGTH_U8;
  if (str_eq_ci(binary_frame_name, "LENGTH_U16_LE")) return BINARY_FRAME_LENGTH_U16_LE;
  if (str_eq_ci(binary_frame_name, "LENGTH_U16_BE")) return BINARY_FRAME_LENGTH_U16_BE;
  return BINARY_FRAME_IDLE;
}

static bool valid_binary_frame_name(const char* value) {
  return value && (str_eq_ci(value, "IDLE") || str_eq_ci(value, "FIXED") ||
                   str_eq_ci(value, "LENGTH_U8") || str_eq_ci(value, "LENGTH_U16_LE") ||
                   str_eq_ci(value, "LENGTH_U16_BE"));
}

static bool binary_is_sep(char c) {
  return c == ' ' || c == '\t' || c == ',' || c == ':' || c == '-' || c == '_';
}

static bool parse_hex_sequence(const char* text, uint8_t* out, uint8_t max_len, uint8_t& out_len, bool allow_wildcards, uint8_t* masks) {
  out_len = 0;
  if (!text) return true;
  const char* p = text;
  while (*p) {
    while (*p && binary_is_sep(*p)) ++p;
    if (!*p) break;
    if (allow_wildcards && p[0] == '?' && p[1] == '?') {
      if (out_len >= max_len) return false;
      out[out_len] = 0;
      if (masks) masks[out_len] = 0;
      ++out_len;
      p += 2;
      continue;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    const int hi = hex_nibble((uint8_t)p[0]);
    const int lo = hex_nibble((uint8_t)p[1]);
    if (hi < 0 || lo < 0) return false;
    if (out_len >= max_len) return false;
    out[out_len] = (uint8_t)((hi << 4) | lo);
    if (masks) masks[out_len] = 0xFF;
    ++out_len;
    p += 2;
  }
  return true;
}

static uint16_t crc16_ccitt_false(const uint8_t* data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; data && i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; ++b) crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
  }
  return crc;
}

static bool binary_refresh_config() {
  binary_start_len = 0;
  if (binary_start_hex[0] && !parse_hex_sequence(binary_start_hex, binary_start_bytes, sizeof(binary_start_bytes), binary_start_len, false, nullptr)) return false;
  const BinaryFrameMode mode = active_binary_frame_mode();
  if (mode == BINARY_FRAME_FIXED && (binary_rx_length < 1 || binary_rx_length > sizeof(rx_packet))) return false;
  if ((mode == BINARY_FRAME_LENGTH_U8 || mode == BINARY_FRAME_LENGTH_U16_LE || mode == BINARY_FRAME_LENGTH_U16_BE) && binary_length_offset >= sizeof(rx_packet)) return false;
  if ((mode == BINARY_FRAME_LENGTH_U16_LE || mode == BINARY_FRAME_LENGTH_U16_BE) && binary_length_offset + 1U >= sizeof(rx_packet)) return false;
  return true;
}

static bool binary_append_value(uint8_t* out, uint8_t max_len, uint8_t& len, int32_t value, const char* type) {
  if (!type) return false;
  auto put = [&](uint8_t b) -> bool { if (len >= max_len) return false; out[len++] = b; return true; };
  const uint32_t u = (uint32_t)value;
  if (str_eq_ci(type, "u8") || str_eq_ci(type, "i8")) return put((uint8_t)u);
  if (str_eq_ci(type, "u16le") || str_eq_ci(type, "i16le")) return put((uint8_t)u) && put((uint8_t)(u >> 8));
  if (str_eq_ci(type, "u16be") || str_eq_ci(type, "i16be")) return put((uint8_t)(u >> 8)) && put((uint8_t)u);
  if (str_eq_ci(type, "u32le") || str_eq_ci(type, "i32le")) return put((uint8_t)u) && put((uint8_t)(u >> 8)) && put((uint8_t)(u >> 16)) && put((uint8_t)(u >> 24));
  if (str_eq_ci(type, "u32be") || str_eq_ci(type, "i32be")) return put((uint8_t)(u >> 24)) && put((uint8_t)(u >> 16)) && put((uint8_t)(u >> 8)) && put((uint8_t)u);
  return false;
}

static bool binary_build_template(const char* text, int32_t value, uint8_t* out, uint8_t max_len, uint8_t& out_len) {
  out_len = 0;
  if (!text || !out) return false;
  const char* p = text;
  while (*p) {
    while (*p && binary_is_sep(*p)) ++p;
    if (!*p) break;
    if (*p == '{') {
      const char* end = strchr(p, '}');
      if (!end) return false;
      char token[24];
      const size_t n = (size_t)(end - p - 1);
      if (!n || n >= sizeof(token)) return false;
      memcpy(token, p + 1, n); token[n] = 0;
      if (strncmp(token, "value:", 6) != 0) return false;
      if (!binary_append_value(out, max_len, out_len, value, token + 6)) return false;
      p = end + 1;
      continue;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    const int hi = hex_nibble((uint8_t)p[0]);
    const int lo = hex_nibble((uint8_t)p[1]);
    if (hi < 0 || lo < 0 || out_len >= max_len) return false;
    out[out_len++] = (uint8_t)((hi << 4) | lo);
    p += 2;
  }
  return out_len > 0;
}

static bool binary_value_type_valid(const char* type) {
  return type && (str_eq_ci(type, "u8") || str_eq_ci(type, "i8") || str_eq_ci(type, "u16le") ||
    str_eq_ci(type, "u16be") || str_eq_ci(type, "i16le") || str_eq_ci(type, "i16be") ||
    str_eq_ci(type, "u32le") || str_eq_ci(type, "u32be") || str_eq_ci(type, "i32le") ||
    str_eq_ci(type, "i32be") || str_eq_ci(type, "ascii") || str_eq_ci(type, "hex"));
}

static uint8_t binary_numeric_type_size(const char* type) {
  if (str_eq_ci(type, "u8") || str_eq_ci(type, "i8")) return 1;
  if (str_eq_ci(type, "u16le") || str_eq_ci(type, "u16be") || str_eq_ci(type, "i16le") || str_eq_ci(type, "i16be")) return 2;
  if (str_eq_ci(type, "u32le") || str_eq_ci(type, "u32be") || str_eq_ci(type, "i32le") || str_eq_ci(type, "i32be")) return 4;
  return 0;
}

static bool binary_compile_entity_match(ProfileEntity& e) {
  e.binary_match_len = 0;
  memset(e.binary_match_bytes, 0, sizeof(e.binary_match_bytes));
  memset(e.binary_match_masks, 0, sizeof(e.binary_match_masks));
  if (!e.match[0]) return true;
  return parse_hex_sequence(e.match, e.binary_match_bytes, sizeof(e.binary_match_bytes), e.binary_match_len, true, e.binary_match_masks) && e.binary_match_len > 0;
}

static bool binary_extract_entity_value(const ProfileEntity& e, const uint8_t* data, uint8_t len, int32_t& value, char* text, size_t text_len) {
  if (!data) return false;
  if ((uint16_t)e.binary_match_offset + e.binary_match_len > len) return false;
  for (uint8_t i = 0; i < e.binary_match_len; ++i) {
    if (e.binary_match_masks[i] && data[e.binary_match_offset + i] != e.binary_match_bytes[i]) return false;
  }
  const uint8_t size = binary_numeric_type_size(e.binary_value_type);
  uint8_t n = size ? size : e.binary_value_len;
  if (!n || (uint16_t)e.binary_value_offset + n > len) return false;
  const uint8_t* v = data + e.binary_value_offset;
  if (str_eq_ci(e.binary_value_type, "ascii")) {
    if (text && text_len) {
      size_t c = n < text_len - 1 ? n : text_len - 1;
      for (size_t i = 0; i < c; ++i) text[i] = (v[i] >= 32 && v[i] <= 126) ? (char)v[i] : '.';
      text[c] = 0;
    }
    char tmp[32]; uint8_t c = n < sizeof(tmp)-1 ? n : sizeof(tmp)-1; memcpy(tmp, v, c); tmp[c] = 0; value = atol(tmp);
    return true;
  }
  if (str_eq_ci(e.binary_value_type, "hex")) {
    static const char hx[] = "0123456789ABCDEF";
    if (text && text_len) {
      size_t o = 0;
      for (uint8_t i = 0; i < n && o + 2 < text_len; ++i) { text[o++] = hx[v[i] >> 4]; text[o++] = hx[v[i] & 0x0F]; }
      text[o] = 0;
    }
    uint32_t u = 0; for (uint8_t i = 0; i < n && i < 4; ++i) u = (u << 8) | v[i]; value = (int32_t)u;
    return true;
  }
  uint32_t u = 0;
  const bool le = strstr(e.binary_value_type, "le") != nullptr;
  if (le) { for (int i = (int)n - 1; i >= 0; --i) u = (u << 8) | v[i]; }
  else { for (uint8_t i = 0; i < n; ++i) u = (u << 8) | v[i]; }
  if (str_eq_ci(e.binary_value_type, "i8")) value = (int8_t)u;
  else if (str_eq_ci(e.binary_value_type, "i16le") || str_eq_ci(e.binary_value_type, "i16be")) value = (int16_t)u;
  else value = (int32_t)u;
  if (text && text_len) snprintf(text, text_len, "%ld", (long)value);
  return true;
}

static uint8_t sum8_bytes(const uint8_t* data, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; data && i < len; ++i) sum = (uint8_t)(sum + data[i]);
  return sum;
}

static uint8_t xor8_bytes(const uint8_t* data, uint8_t len) {
  uint8_t x = 0;
  for (uint8_t i = 0; data && i < len; ++i) x ^= data[i];
  return x;
}

static int hex_nibble(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static uint8_t hex_pair_value(uint8_t hi, uint8_t lo, bool& ok) {
  const int h = hex_nibble(hi);
  const int l = hex_nibble(lo);
  ok = h >= 0 && l >= 0;
  return ok ? (uint8_t)((h << 4) | l) : 0;
}

static uint8_t trim_line_end_len(const uint8_t* data, uint8_t len) {
  while (len && data && (data[len - 1] == '\r' || data[len - 1] == '\n')) --len;
  return len;
}

static bool append_active_checksum(uint8_t* out, uint8_t& len, uint8_t max_len) {
  const LocalChecksumType type = active_checksum_type();
  if (type == LOCAL_CS_NONE) return true;

  if (type == LOCAL_CS_WELLER_SUM8) {
    if (len <= 1) return true; // Weller one-byte queries are sent without checksum.
    if (len == 4) {
      if (len + 1 > max_len) return false;
      out[len++] = sum8_bytes(out, 4);
      return true;
    }
    if (len == 5 && (out[4] == sum8_bytes(out, 4))) return true;
    if (len == 5 && (out[0] == 'a' || out[0] == 'A') && out[4] == 'A') {
      if (len + 1 > max_len) return false;
      out[5] = out[4];
      out[4] = sum8_bytes(out, 4);
      len = 6;
      return true;
    }
    if (len == 6 && (out[0] == 'a' || out[0] == 'A') && out[4] == sum8_bytes(out, 4)) return true;
    return true;
  }

  if (type == LOCAL_CS_XOR8_HEX || type == LOCAL_CS_SUM8_HEX) {
    if (len + 2 > max_len) return false;
    const uint8_t cs = (type == LOCAL_CS_XOR8_HEX) ? xor8_bytes(out, len) : sum8_bytes(out, len);
    static const char hex[] = "0123456789ABCDEF";
    out[len++] = (uint8_t)hex[(cs >> 4) & 0x0F];
    out[len++] = (uint8_t)hex[cs & 0x0F];
    return true;
  }

  if (type == LOCAL_CS_CRC16_MODBUS_LE) {
    if (len + 2 > max_len) return false;
    const uint16_t crc = crc16_modbus(out, len);
    out[len++] = (uint8_t)(crc & 0xFF);
    out[len++] = (uint8_t)(crc >> 8);
    return true;
  }

  if (type == LOCAL_CS_XOR8_RAW || type == LOCAL_CS_SUM8_RAW) {
    if (len + 1 > max_len) return false;
    const uint8_t cs = (type == LOCAL_CS_XOR8_RAW) ? xor8_bytes(out, len) : sum8_bytes(out, len);
    out[len++] = cs;
    return true;
  }

  if (type == LOCAL_CS_CRC16_CCITT_BE || type == LOCAL_CS_CRC16_CCITT_LE) {
    if (len + 2 > max_len) return false;
    const uint16_t crc = crc16_ccitt_false(out, len);
    if (type == LOCAL_CS_CRC16_CCITT_LE) { out[len++] = (uint8_t)crc; out[len++] = (uint8_t)(crc >> 8); }
    else { out[len++] = (uint8_t)(crc >> 8); out[len++] = (uint8_t)crc; }
    return true;
  }

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
  const LocalChecksumType type = active_checksum_type();
  if (type == LOCAL_CS_NONE) return true;

  const uint8_t n = is_binary_protocol() ? len : trim_line_end_len(data, len);
  if (type == LOCAL_CS_WELLER_SUM8) {
    if (n == 5 && data[4] == sum8_bytes(data, 4)) return true;
    if (n == 7 && data[6] == sum8_bytes(data, 6)) return true;
    meta2 = 0xEE;
    return false;
  }

  if (type == LOCAL_CS_XOR8_HEX || type == LOCAL_CS_SUM8_HEX) {
    if (n < 3) { meta2 = 0xEF; return false; }
    bool ok = false;
    const uint8_t got = hex_pair_value(data[n - 2], data[n - 1], ok);
    const uint8_t calc = (type == LOCAL_CS_XOR8_HEX) ? xor8_bytes(data, n - 2) : sum8_bytes(data, n - 2);
    if (ok && got == calc) return true;
    meta2 = 0xEE;
    return false;
  }

  if (type == LOCAL_CS_CRC16_MODBUS_LE) {
    if (n < 3) { meta2 = 0xEF; return false; }
    const uint16_t got = (uint16_t)data[n - 2] | ((uint16_t)data[n - 1] << 8);
    const uint16_t calc = crc16_modbus(data, n - 2);
    if (got == calc) return true;
    meta2 = 0xEE;
    return false;
  }

  if (type == LOCAL_CS_XOR8_RAW || type == LOCAL_CS_SUM8_RAW) {
    if (n < 2) { meta2 = 0xEF; return false; }
    const uint8_t got = data[n - 1];
    const uint8_t calc = (type == LOCAL_CS_XOR8_RAW) ? xor8_bytes(data, n - 1) : sum8_bytes(data, n - 1);
    if (got == calc) return true;
    meta2 = 0xEE;
    return false;
  }

  if (type == LOCAL_CS_CRC16_CCITT_BE || type == LOCAL_CS_CRC16_CCITT_LE) {
    if (n < 3) { meta2 = 0xEF; return false; }
    const uint16_t got = (type == LOCAL_CS_CRC16_CCITT_LE)
      ? ((uint16_t)data[n - 2] | ((uint16_t)data[n - 1] << 8))
      : (((uint16_t)data[n - 2] << 8) | (uint16_t)data[n - 1]);
    const uint16_t calc = crc16_ccitt_false(data, n - 2);
    if (got == calc) return true;
    meta2 = 0xEE;
    return false;
  }

  return true;
}

static uint32_t fnv1a32(const uint8_t* data, size_t len) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619UL;
  }
  return h;
}

static const char* line_end_bytes(uint8_t& len) {
  len = 0;
  if (is_binary_protocol()) return "";
  if (strcmp(line_end_name, "CR") == 0) { len = 1; return "\r"; }
  if (strcmp(line_end_name, "LF") == 0) { len = 1; return "\n"; }
  if (strcmp(line_end_name, "CRLF") == 0) { len = 2; return "\r\n"; }
  return "";
}

static bool valid_line_end(const char* value) {
  return value && (strcmp(value, "NONE") == 0 || strcmp(value, "CR") == 0 ||
                   strcmp(value, "LF") == 0 || strcmp(value, "CRLF") == 0);
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

static void desc_append_hex_meta(char* dst, size_t dst_len, const char* key, const char* value) {
  if (!dst || !dst_len || !key || !*key || !value || !*value) return;
  char tmp[192];
  size_t o = 0;
  tmp[o++] = ' ';
  while (*key && o + 1 < sizeof(tmp)) tmp[o++] = *key++;
  if (o + 1 >= sizeof(tmp)) return;
  tmp[o++] = '=';
  static const char hx[] = "0123456789ABCDEF";
  for (const uint8_t* p = (const uint8_t*)value; *p && o + 2 < sizeof(tmp); ++p) {
    tmp[o++] = hx[(*p >> 4) & 0x0F];
    tmp[o++] = hx[*p & 0x0F];
  }
  tmp[o] = 0;
  desc_append(dst, dst_len, tmp);
}

static const char* profile_entity_mode(const ProfileEntity& e) {
  if (str_eq_ci(e.access, "ro") || str_eq_ci(e.access, "rw") || str_eq_ci(e.access, "wo")) return e.access;
  const bool readable = e.match[0] || e.poll[0] || str_eq_ci(e.type, "sensor") || str_eq_ci(e.type, "binary_sensor") || str_eq_ci(e.type, "select");
  const bool writable = str_eq_ci(e.type, "button") || e.set_cmd[0] || e.set_on[0] || e.set_off[0];
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
  const bool compact_trace = profile_entity_count > 8;
  desc_appendf(descriptor_text, sizeof(descriptor_text),
    "schema=1\n"
    "descriptor_limit=%u\n"
    "descriptor_truncated=0\n"
    "module=Universal RS232 Bridge\n"
    "profile=%s\n"
    "station=%s\n"
    "local_bus=UART/RS232/TTL\n"
    "uart=%lu %s\n"
    "protocol=%s\n"
    "checksum=%s\n"
    "line_end=%s\n"
    "profile_entities=%u\n"
    "profile_slots=%u\n"
    "system_entities=master_builtin\n"
    "profile_active=%s\n"
    "trace_schema=%u\n",
    (unsigned)sizeof(descriptor_text), profile_name, station_name, (unsigned long)local_baud, frame_name, protocol_name, checksum_type_label(), line_end_name, profile_entity_count, PROFILE_ENTITY_MAX, profile_entity_count ? "yes" : "no", compact_trace ? 2U : 1U);

  if (is_binary_protocol()) {
    desc_appendf(descriptor_text, sizeof(descriptor_text), "binary_frame=%s\n", binary_frame_name);
    if (binary_start_hex[0]) desc_appendf(descriptor_text, sizeof(descriptor_text), "binary_start=%s\n", binary_start_hex);
    if (active_binary_frame_mode() == BINARY_FRAME_FIXED) desc_appendf(descriptor_text, sizeof(descriptor_text), "binary_rx_length=%u\n", binary_rx_length);
    if (active_binary_frame_mode() == BINARY_FRAME_LENGTH_U8 || active_binary_frame_mode() == BINARY_FRAME_LENGTH_U16_LE || active_binary_frame_mode() == BINARY_FRAME_LENGTH_U16_BE)
      desc_appendf(descriptor_text, sizeof(descriptor_text), "binary_length_offset=%u\nbinary_length_adjust=%d\n", binary_length_offset, binary_length_adjust);
  }
  desc_append(descriptor_text, sizeof(descriptor_text), "entities:\n");

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
    // Compact descriptor v2: id>=20 identifies a profile entity and the
    // fourth token already contains the effective access mode.
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
    if (str_eq_ci(e.type, "number")) {
      desc_appendf(descriptor_text, sizeof(descriptor_text), " min=%ld max=%ld step=%ld", (long)e.min_value, (long)e.max_value, (long)e.step_value);
    }
    if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) {
      desc_appendf(descriptor_text, sizeof(descriptor_text), " value_on=%ld value_off=%ld", (long)e.value_on, (long)e.value_off);
    }
    if (e.options[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " options="); desc_append(descriptor_text, sizeof(descriptor_text), e.options); }
    if (e.values[0]) { desc_append(descriptor_text, sizeof(descriptor_text), " values="); desc_append(descriptor_text, sizeof(descriptor_text), e.values); }

    if (is_binary_protocol()) {
      desc_appendf(descriptor_text, sizeof(descriptor_text), " value_offset=%u value_type=%s", e.binary_value_offset, e.binary_value_type);
      if (e.binary_value_len) desc_appendf(descriptor_text, sizeof(descriptor_text), " value_len=%u", e.binary_value_len);
      if (e.binary_match_offset) desc_appendf(descriptor_text, sizeof(descriptor_text), " match_offset=%u", e.binary_match_offset);
    }

    // Profiles with up to eight entities keep the legacy trace keys for
    // backwards compatibility with older Masters. Larger profiles switch only
    // the trace-only metadata to compact aliases so the fixed descriptor stays
    // useful without wasting bus bandwidth.
    if (compact_trace) {
      if (e.scale_value != 1) desc_appendf(descriptor_text, sizeof(descriptor_text), " sc=%ld", (long)e.scale_value);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "tp", e.poll);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "tm", e.match);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "ts", e.set_cmd);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "tn", e.set_on);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "to", e.set_off);
    } else {
      if (e.scale_value != 1) desc_appendf(descriptor_text, sizeof(descriptor_text), " trace_scale=%ld", (long)e.scale_value);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "trace_poll_hex", e.poll);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "trace_match_hex", e.match);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "trace_set_hex", e.set_cmd);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "trace_on_hex", e.set_on);
      desc_append_hex_meta(descriptor_text, sizeof(descriptor_text), "trace_off_hex", e.set_off);
    }

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
static void profile_repeat_tick();
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
  if (!valid_checksum_name(s.c_str())) s = "NONE";
  clean_copy(checksum_name, sizeof(checksum_name), s.c_str());
  s = prefs.getString("line_end", line_end_name);
  s.toUpperCase();
  if (!valid_line_end(s.c_str())) s = "CR";
  clean_copy(line_end_name, sizeof(line_end_name), s.c_str());
  s = prefs.getString("bin_frame", binary_frame_name);
  s.toUpperCase();
  if (!valid_binary_frame_name(s.c_str())) s = "IDLE";
  clean_copy(binary_frame_name, sizeof(binary_frame_name), s.c_str());
  s = prefs.getString("bin_start", binary_start_hex);
  clean_copy(binary_start_hex, sizeof(binary_start_hex), s.c_str());
  binary_rx_length = prefs.getUShort("bin_rx_len", 0);
  binary_length_offset = prefs.getUChar("bin_len_off", 0);
  binary_length_adjust = prefs.getShort("bin_len_adj", 0);
  binary_refresh_config();
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
  prefs.putString("line_end", line_end_name);
  prefs.putString("bin_frame", binary_frame_name);
  prefs.putString("bin_start", binary_start_hex);
  prefs.putUShort("bin_rx_len", binary_rx_length);
  prefs.putUChar("bin_len_off", binary_length_offset);
  prefs.putShort("bin_len_adj", binary_length_adjust);
  prefs.putUInt("baud", local_baud);
}

static void restart_local_uart() {
  LOCAL.end();
  delay(10);
  LOCAL.begin(local_baud, local_serial_config, LOCAL_RX_PIN, LOCAL_TX_PIN);
}

static uint64_t module_uid() {
  return 0x5000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}

static bool valid_module_addr(uint8_t addr) {
  return addr >= 0x50 && addr <= 0x5F;
}

static uint32_t module_caps() {
  return CAP_FW_UPDATE | CAP_FAULT_REPORT | CAP_LOCAL_TRACE |
         CAP_DESCRIPTOR | CAP_ENTITY_CONTROL | CAP_ENTITY_EVENTS |
         CAP_LOCAL_PROTOCOL | CAP_FAULT_MAP;
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
  uint8_t end_len = 0;
  const char* end = line_end_bytes(end_len);
  uint8_t tx_buf[96];
  uint8_t n = 0;
  for (uint8_t i = 0; data && i < len && n < sizeof(tx_buf); ++i) tx_buf[n++] = data[i];
  if (!append_active_checksum(tx_buf, n, (uint8_t)(sizeof(tx_buf) - end_len))) return;
  if (!n && !end_len) return;
  if (n) LOCAL.write(tx_buf, n);
  if (end_len) LOCAL.write((const uint8_t*)end, end_len);
  LOCAL.flush();
  ++local_tx_count;
  uint8_t trace_buf[32];
  uint8_t tn = 0;
  for (uint8_t i = 0; i < n && tn < sizeof(trace_buf); ++i) trace_buf[tn++] = tx_buf[i];
  for (uint8_t i = 0; i < end_len && tn < sizeof(trace_buf); ++i) trace_buf[tn++] = (uint8_t)end[i];
  last_local_tx_ms = millis();
  bytes_preview(trace_buf, tn, last_local_tx_text, sizeof(last_local_tx_text));
  trace_log(2, 0x4C, (uint8_t)active_checksum_type(), trace_buf, tn);
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

static bool binary_buffer_matches_start_prefix() {
  if (!binary_start_len || !rx_packet_len) return true;
  const uint8_t n = rx_packet_len < binary_start_len ? rx_packet_len : binary_start_len;
  return memcmp(rx_packet, binary_start_bytes, n) == 0;
}

static void binary_resync_start() {
  if (!binary_start_len) return;
  while (rx_packet_len && !binary_buffer_matches_start_prefix()) {
    memmove(rx_packet, rx_packet + 1, --rx_packet_len);
  }
}

// Returns 0 while more data is needed, >0 for the expected total frame length,
// and -1 when the configured length field is invalid for the local RX buffer.
static int16_t binary_expected_frame_len() {
  const BinaryFrameMode mode = active_binary_frame_mode();
  if (mode == BINARY_FRAME_IDLE) return 0;
  if (mode == BINARY_FRAME_FIXED) return (int16_t)binary_rx_length;
  const uint8_t need = (mode == BINARY_FRAME_LENGTH_U8) ? 1U : 2U;
  if ((uint16_t)binary_length_offset + need > rx_packet_len) return 0;
  uint32_t field = 0;
  if (mode == BINARY_FRAME_LENGTH_U8) field = rx_packet[binary_length_offset];
  else if (mode == BINARY_FRAME_LENGTH_U16_LE) field = (uint32_t)rx_packet[binary_length_offset] | ((uint32_t)rx_packet[binary_length_offset + 1] << 8);
  else field = ((uint32_t)rx_packet[binary_length_offset] << 8) | (uint32_t)rx_packet[binary_length_offset + 1];
  const int32_t total = (int32_t)field + (int32_t)binary_length_adjust;
  if (total <= 0 || total > (int32_t)sizeof(rx_packet)) return -1;
  if (binary_start_len && total < binary_start_len) return -1;
  return (int16_t)total;
}

static void binary_rx_bad_length_reset() {
  ++local_rx_count;
  ++local_rx_pattern_errors;
  last_local_rx_any_ms = last_local_activity_ms = millis();
  clean_copy(last_local_rx_status, sizeof(last_local_rx_status), "BAD-LENGTH");
  bytes_preview(rx_packet, rx_packet_len, last_local_rx_text, sizeof(last_local_rx_text));
  trace_log(1, rx_packet_len ? rx_packet[0] : 0x52, 0xEF, rx_packet, rx_packet_len);
  rx_packet_len = 0;
}

static void binary_feed_byte(uint8_t b) {
  if (rx_packet_len >= sizeof(rx_packet)) {
    binary_rx_bad_length_reset();
  }
  rx_packet[rx_packet_len++] = b;
  rx_packet_last_ms = millis();
  binary_resync_start();
  if (!rx_packet_len) return;
  if (binary_start_len && rx_packet_len < binary_start_len) return;
  const int16_t expected = binary_expected_frame_len();
  if (expected < 0) { binary_rx_bad_length_reset(); return; }
  if (expected > 0) {
    if (rx_packet_len == (uint8_t)expected) local_flush_rx_packet();
    else if (rx_packet_len > (uint8_t)expected) binary_rx_bad_length_reset();
  } else if (rx_packet_len >= sizeof(rx_packet)) {
    // IDLE framing cannot accept a frame larger than the local RX buffer.
    local_flush_rx_packet();
  }
}

static void localBusPoll() {
  while (LOCAL.available()) {
    const int c = LOCAL.read();
    if (c < 0) break;
    if (is_binary_protocol()) binary_feed_byte((uint8_t)c);
    else {
      if (rx_packet_len < sizeof(rx_packet)) rx_packet[rx_packet_len++] = (uint8_t)c;
      rx_packet_last_ms = millis();
      if (rx_packet_len >= sizeof(rx_packet)) local_flush_rx_packet();
    }
  }
  // IDLE remains useful for binary protocols that have no length field. Fixed
  // and length-prefixed modes flush immediately and therefore need no gap.
  if (rx_packet_len && (!is_binary_protocol() || active_binary_frame_mode() == BINARY_FRAME_IDLE) &&
      (uint32_t)(millis() - rx_packet_last_ms) >= local_rx_idle_gap_ms()) local_flush_rx_packet();

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
  resp.payload[o++] = MODULE_UNIVERSAL_RS232;
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
  const char fallback[] = "Universal RS232 Bridge";
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
  put_u32_le(payload + o, monotonic_uptime_seconds()); o += 4;
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
  resp.payload[o++] = MODULE_UNIVERSAL_RS232;
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
  if (!wanted || wanted == 5) { if (append_entity_text_safe(resp.payload, o, 5, line_end_name, 0)) ++count; }
  if (!wanted || wanted == 6) { if (append_entity_text_safe(resp.payload, o, 6, checksum_type_label(), 0)) ++count; }
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
  resp.payload[o++] = MODULE_UNIVERSAL_RS232;
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

static void profile_store_read_value(ProfileEntity& e, int32_t raw, const char* raw_text) {
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
  else if (raw_text && *raw_text && e.scale_value == 1 && e.divisor_value == 1 && e.offset_value == 0 && !e.bit_mask && !e.bit_shift && e.time_base == PROFILE_TIME_NONE) clean_copy(e.text_value, sizeof(e.text_value), raw_text);
  else snprintf(e.text_value, sizeof(e.text_value), "%ld", (long)e.value);
}

static void profile_copy_line_value(char* dst, size_t dst_len, const String& value) {
  clean_copy(dst, dst_len, value.c_str());
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
  v = config_value(cfg, "line_end");
  if (!v.length()) v = config_value(cfg, "lineending");
  if (!v.length()) v = config_value(cfg, "ending");
  if (v.length()) {
    v.toUpperCase();
    if (!valid_line_end(v.c_str())) return false;
    profile_copy_line_value(line_end_name, sizeof(line_end_name), v);
  }
  if (is_binary_protocol()) clean_copy(line_end_name, sizeof(line_end_name), "NONE");
  v = config_value(cfg, "binary_frame");
  if (!v.length()) v = config_value(cfg, "frame_mode");
  if (v.length()) {
    v.toUpperCase();
    if (!valid_binary_frame_name(v.c_str())) return false;
    profile_copy_line_value(binary_frame_name, sizeof(binary_frame_name), v);
  } else if (is_binary_protocol()) {
    clean_copy(binary_frame_name, sizeof(binary_frame_name), "IDLE");
  }
  v = config_value(cfg, "binary_start");
  if (!v.length()) v = config_value(cfg, "start_hex");
  if (v.length() || is_binary_protocol()) profile_copy_line_value(binary_start_hex, sizeof(binary_start_hex), v);
  v = config_value(cfg, "binary_rx_length");
  if (!v.length()) v = config_value(cfg, "rx_length");
  if (v.length()) { uint32_t x = (uint32_t)v.toInt(); if (x > sizeof(rx_packet)) return false; binary_rx_length = (uint16_t)x; }
  else if (is_binary_protocol()) binary_rx_length = 0;
  v = config_value(cfg, "binary_length_offset");
  if (!v.length()) v = config_value(cfg, "length_offset");
  if (v.length()) { uint32_t x = (uint32_t)v.toInt(); if (x >= sizeof(rx_packet)) return false; binary_length_offset = (uint8_t)x; }
  else if (is_binary_protocol()) binary_length_offset = 0;
  v = config_value(cfg, "binary_length_adjust");
  if (!v.length()) v = config_value(cfg, "length_adjust");
  if (v.length()) { long x = v.toInt(); if (x < -192 || x > 192) return false; binary_length_adjust = (int16_t)x; }
  else if (is_binary_protocol()) binary_length_adjust = 0;
  if (is_binary_protocol() && !binary_refresh_config()) return false;
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
    String poll = config_value(cfg, (String(prefix) + "poll").c_str());
    String match = config_value(cfg, (String(prefix) + "match").c_str());
    String set_cmd = config_value(cfg, (String(prefix) + "set").c_str());
    String set_on = config_value(cfg, (String(prefix) + "set_on").c_str());
    String set_off = config_value(cfg, (String(prefix) + "set_off").c_str());
    String options = config_value(cfg, (String(prefix) + "options").c_str());
    String values = config_value(cfg, (String(prefix) + "values").c_str());
    if (!type.length() && !name.length() && !poll.length() && !match.length() && !set_cmd.length() && !set_on.length() && !set_off.length() && !options.length()) continue;

    ProfileEntity& e = profile_entities[profile_entity_count];
    e.used = true;
    e.id = PROFILE_ENTITY_BASE_ID + profile_entity_count;
    v = config_value(cfg, (String(prefix) + "id").c_str());
    if (v.length()) {
      uint32_t id = (uint32_t)v.toInt();
      if (id >= 20 && id <= 249) e.id = (uint8_t)id;
    }
    if (!type.length()) type = set_on.length() || set_off.length() ? "switch" : (options.length() ? "select" : "text");
    type.toLowerCase();
    if (type == "binary") type = "binary_sensor";
    if (type != "sensor" && type != "number" && type != "switch" && type != "binary_sensor" && type != "text" && type != "select" && type != "button") return false;
    profile_copy_line_value(e.type, sizeof(e.type), type);
    access.toLowerCase();
    if (access.length() && access != "ro" && access != "rw" && access != "wo") return false;
    if (!access.length()) {
      if (type == "sensor" || type == "binary_sensor") access = "ro";
      else if (type == "button") access = "wo";
      else if (set_cmd.length() || set_on.length() || set_off.length()) access = (poll.length() || match.length()) ? "rw" : "wo";
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
    } else if ((protocol_name[0] && str_eq_ci(protocol_name, "WELLER_ASCII")) &&
               str_eq_ci(e.type, "sensor") && str_eq_ci(e.unit, "rpm")) {
      // Weller D### reports RPM/10. Match the native Weller module.
      e.scale_value = 10;
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
    if (is_binary_protocol()) {
      v = config_value(cfg, (String(prefix) + "match_offset").c_str());
      if (v.length()) { uint32_t x = (uint32_t)v.toInt(); if (x > 191U) return false; e.binary_match_offset = (uint8_t)x; }
      v = config_value(cfg, (String(prefix) + "value_offset").c_str());
      if (v.length()) { uint32_t x = (uint32_t)v.toInt(); if (x > 191U) return false; e.binary_value_offset = (uint8_t)x; }
      v = config_value(cfg, (String(prefix) + "value_type").c_str());
      if (v.length()) { v.toLowerCase(); if (!binary_value_type_valid(v.c_str())) return false; profile_copy_line_value(e.binary_value_type, sizeof(e.binary_value_type), v); }
      v = config_value(cfg, (String(prefix) + "value_len").c_str());
      if (v.length()) { uint32_t x = (uint32_t)v.toInt(); if (x > 31U) return false; e.binary_value_len = (uint8_t)x; }
      if ((str_eq_ci(e.binary_value_type, "ascii") || str_eq_ci(e.binary_value_type, "hex")) && e.binary_value_len == 0) return false;
      if (str_eq_ci(e.binary_value_type, "hex") && e.binary_value_len > 15) return false;
    }
    v = config_value(cfg, (String(prefix) + "poll_ms").c_str());
    if (v.length()) {
      uint32_t ms = (uint32_t)v.toInt();
      if (ms < 250UL) ms = 250UL;
      if (ms > 60000UL) ms = 60000UL;
      e.poll_ms = (uint16_t)ms;
    }
    v = config_value(cfg, (String(prefix) + "repeat_on_ms").c_str());
    if (!v.length()) v = config_value(cfg, (String(prefix) + "repeat_ms").c_str());
    if (v.length()) {
      uint32_t ms = (uint32_t)v.toInt();
      if (ms && ms < 250UL) ms = 250UL;
      if (ms > 60000UL) ms = 60000UL;
      e.repeat_on_ms = (uint16_t)ms;
    }
    v = config_value(cfg, (String(prefix) + "repeat_off_ms").c_str());
    if (v.length()) {
      uint32_t ms = (uint32_t)v.toInt();
      if (ms && ms < 250UL) ms = 250UL;
      if (ms > 60000UL) ms = 60000UL;
      e.repeat_off_ms = (uint16_t)ms;
    }
    profile_copy_line_value(e.options, sizeof(e.options), options);
    profile_copy_line_value(e.values, sizeof(e.values), values);
    profile_copy_line_value(e.poll, sizeof(e.poll), poll);
    profile_copy_line_value(e.match, sizeof(e.match), match);
    profile_copy_line_value(e.set_cmd, sizeof(e.set_cmd), set_cmd);
    profile_copy_line_value(e.set_on, sizeof(e.set_on), set_on);
    profile_copy_line_value(e.set_off, sizeof(e.set_off), set_off);
    if (is_binary_protocol()) {
      if (!binary_compile_entity_match(e)) return false;
      uint8_t tmp[96], tmp_len = 0;
      if (e.poll[0] && !binary_build_template(e.poll, 0, tmp, sizeof(tmp), tmp_len)) return false;
      if (e.set_cmd[0] && !binary_build_template(e.set_cmd, 0, tmp, sizeof(tmp), tmp_len)) return false;
      if (e.set_on[0] && !binary_build_template(e.set_on, e.value_on, tmp, sizeof(tmp), tmp_len)) return false;
      if (e.set_off[0] && !binary_build_template(e.set_off, e.value_off, tmp, sizeof(tmp), tmp_len)) return false;
    }

    // Access is authoritative. Readable entities require a match rule
    // (poll may be omitted for unsolicited protocols). Writable entities need
    // a concrete command path. Extra conflicting fields are ignored at runtime.
    if (profile_entity_readable(e) && !e.match[0]) return false;
    if (profile_entity_writable(e) && !e.set_cmd[0] && !e.set_on[0] && !e.set_off[0]) return false;
    if (!profile_entity_writable(e)) { e.repeat_on_ms = 0; e.repeat_off_ms = 0; }
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

static uint8_t rx_content_len_without_checksum(const uint8_t* data, uint8_t len) {
  uint8_t n = is_binary_protocol() ? len : trim_line_end_len(data, len);
  const LocalChecksumType type = active_checksum_type();
  if ((type == LOCAL_CS_WELLER_SUM8 || type == LOCAL_CS_XOR8_RAW || type == LOCAL_CS_SUM8_RAW) && n > 1) return (uint8_t)(n - 1);
  if ((type == LOCAL_CS_XOR8_HEX || type == LOCAL_CS_SUM8_HEX || type == LOCAL_CS_CRC16_MODBUS_LE ||
       type == LOCAL_CS_CRC16_CCITT_BE || type == LOCAL_CS_CRC16_CCITT_LE) && n > 2) return (uint8_t)(n - 2);
  return n;
}

static bool match_pattern_value(const char* pattern, const uint8_t* data, uint8_t len, int32_t& value, char* text, size_t text_len) {
  if (!pattern || !*pattern || !data) return false;
  const char* brace = strstr(pattern, "{value}");
  if (brace) {
    const size_t prefix_len = (size_t)(brace - pattern);
    const char* suffix = brace + 7;
    const size_t suffix_len = strlen(suffix);
    if (len < prefix_len + suffix_len) return false;
    if (prefix_len && memcmp(data, pattern, prefix_len) != 0) return false;
    if (suffix_len && memcmp(data + len - suffix_len, suffix, suffix_len) != 0) return false;
    char tmp[24];
    size_t n = len - prefix_len - suffix_len;
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, data + prefix_len, n);
    tmp[n] = 0;
    value = atol(tmp);
    if (text && text_len) snprintf(text, text_len, "%s", tmp);
    return true;
  }

  const size_t plen = strlen(pattern);
  if (plen != len) return false;
  char num[18];
  size_t num_len = 0;
  bool saw_hash = false;
  for (size_t i = 0; i < plen; ++i) {
    const char pc = pattern[i];
    const uint8_t dc = data[i];
    if (pc == '#') {
      if (dc < '0' || dc > '9') return false;
      saw_hash = true;
      if (num_len < sizeof(num) - 1) num[num_len++] = (char)dc;
    } else if ((uint8_t)pc != dc) {
      return false;
    }
  }
  num[num_len] = 0;
  if (saw_hash) value = atol(num);
  else value = 0;
  if (text && text_len) {
    uint8_t n = len;
    if (n >= text_len) n = (uint8_t)(text_len - 1);
    for (uint8_t i = 0; i < n; ++i) text[i] = (data[i] >= 32 && data[i] <= 126) ? (char)data[i] : '.';
    text[n] = 0;
  }
  return true;
}

static bool process_profile_rx(const uint8_t* data, uint8_t len) {
  if (!profile_entity_count) return true;
  const uint8_t content_len = rx_content_len_without_checksum(data, len);
  bool matched = false;
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_entity_readable(e) || !e.match[0]) continue;
    int32_t v = 0;
    char text[sizeof(e.text_value)] = {0};
    const bool ok = is_binary_protocol()
      ? binary_extract_entity_value(e, data, content_len, v, text, sizeof(text))
      : match_pattern_value(e.match, data, content_len, v, text, sizeof(text));
    if (!ok) continue;
    matched = true;
    profile_store_read_value(e, v, text);
    e.last_update_ms = millis();
  }
  return matched;
}

static ProfileEntity* profile_find_entity(uint8_t id) {
  for (uint8_t i = 0; i < profile_entity_count; ++i) if (profile_entities[i].used && profile_entities[i].id == id) return &profile_entities[i];
  return nullptr;
}

static String format_i32_width(int32_t value, uint8_t width) {
  char out[18];
  if (width < 1) width = 1;
  if (width > 9) width = 9;
  snprintf(out, sizeof(out), "%0*ld", (int)width, (long)value);
  return String(out);
}

static String apply_value_template(const char* tmpl, const char* value_text, int32_t value_num) {
  String out = tmpl ? String(tmpl) : String();
  // Fixed-width numeric placeholders are intentionally generic.  Widths 1..9
  // cover the common ASCII protocols while keeping the command formatter small.
  // Examples: {value:01}, {value:03}, {value:09}.
  for (uint8_t width = 1; width <= 9; ++width) {
    char token[12];
    snprintf(token, sizeof(token), "{value:0%u}", (unsigned)width);
    out.replace(token, format_i32_width(value_num, width));
  }
  out.replace("{value}", value_text ? String(value_text) : String(value_num));
  return out;
}

static void normalize_weller_command(String& cmd, const ProfileEntity& e, int32_t value_num) {
  if (active_checksum_type() != LOCAL_CS_WELLER_SUM8 || cmd.length() >= 4) return;
  if (!str_eq_ci(e.type, "number") && !str_eq_ci(e.type, "select")) return;
  if (!cmd.length()) return;
  const char c = cmd[0];
  if (c != 'd' && c != 'D' && c != 'f' && c != 'F' && c != 's' && c != 'S') return;
  cmd = String(c) + format_i32_width(value_num, 3);
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
  int32_t command_value_num = num;
  String lower = s;
  lower.toLowerCase();
  bool want_on = lower == "1" || lower == "on" || lower == "true" || lower == "ein" || num == e.value_on;
  bool want_off = lower == "0" || lower == "off" || lower == "false" || lower == "aus" || num == e.value_off;
  String cmd;
  if (str_eq_ci(e.type, "button")) {
    if (e.set_cmd[0]) cmd = apply_value_template(e.set_cmd, s.c_str(), num);
    else return false;
    snprintf(e.text_value, sizeof(e.text_value), "sent");
  } else if (str_eq_ci(e.type, "switch") || str_eq_ci(e.type, "binary_sensor")) {
    if (want_on && e.set_on[0]) { cmd = e.set_on; command_value_num = e.value_on; }
    else if (want_off && e.set_off[0]) { cmd = e.set_off; command_value_num = e.value_off; }
    else if (e.set_cmd[0]) { command_value_num = want_on ? e.value_on : e.value_off; cmd = apply_value_template(e.set_cmd, s.c_str(), command_value_num); }
    else return false;
    e.bool_value = want_on && !want_off;
    e.value = e.bool_value ? e.value_on : e.value_off;
  } else if (e.set_cmd[0]) {
    int32_t display_num = num;
    if (str_eq_ci(e.type, "number")) {
      if (profile_uses_dhm(e)) return false;
      const bool output_power_role = str_eq_ci(e.role, "main_output_power") || str_eq_ci(e.role, "output_power");
      if (!(output_power_role && display_num == 0)) {
        if (display_num < e.min_value) display_num = e.min_value;
        if (display_num > e.max_value) display_num = e.max_value;
      }
      num = profile_inverse_numeric(e, display_num);
    }
    command_value_num = num;
    const String template_text = str_eq_ci(e.type, "number") ? String(num) : s;
    cmd = apply_value_template(e.set_cmd, template_text.c_str(), num);
    normalize_weller_command(cmd, e, num);
    e.value = display_num;
    if (str_eq_ci(e.type, "text") || str_eq_ci(e.type, "select")) clean_copy(e.text_value, sizeof(e.text_value), s.c_str());
    else snprintf(e.text_value, sizeof(e.text_value), "%ld", (long)e.value);
  } else {
    return false;
  }
  if (!cmd.length()) return false;
  const uint32_t write_ms = millis();
  e.last_update_ms = write_ms;
  e.last_repeat_ms = write_ms;
  profile_poll_pause_until_ms = write_ms + 220UL;
  if (preserve_actual) {
    // RW: keep the last confirmed physical state visible until a real response
    // arrives. The command target must never masquerade as readback.
    e.bool_value = old_bool_value;
    e.value = old_value;
    e.last_update_ms = old_update_ms;
    memcpy(e.text_value, old_text_value, sizeof(e.text_value));
    if (e.poll[0]) {
      profile_readback_entity_id = e.id;
      e.last_poll_ms = 0;
    }
  }
  if (is_binary_protocol()) {
    uint8_t binary_cmd[96]; uint8_t binary_len = 0;
    if (!binary_build_template(cmd.c_str(), command_value_num, binary_cmd, sizeof(binary_cmd), binary_len)) return false;
    local_send_line(binary_cmd, binary_len);
  } else {
    local_send_line((const uint8_t*)cmd.c_str(), (uint8_t)cmd.length());
  }
  if (!preserve_actual && !str_eq_ci(e.type, "button")) {
    // WO: the local device has no readback path, so the last successfully
    // emitted command is our runtime target/shadow. Do not persist it to NVS:
    // a reboot intentionally returns outputs to the safe default OFF state.
    e.command_shadow_valid = true;
  }
  return true;
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
static void profile_repeat_tick() {
  if (!profile_entity_count) return;
  const uint32_t now = millis();
  if (profile_poll_pause_until_ms && (int32_t)(now - profile_poll_pause_until_ms) < 0) return;
  for (uint8_t i = 0; i < profile_entity_count; ++i) {
    ProfileEntity& e = profile_entities[i];
    if (!e.used || !profile_entity_writable(e) ||
        (!str_eq_ci(e.type, "switch") && !str_eq_ci(e.type, "binary_sensor"))) continue;
    if (!profile_entity_readable(e) && !e.command_shadow_valid) continue;
    const uint16_t interval = e.bool_value ? e.repeat_on_ms : e.repeat_off_ms;
    if (!interval) continue;
    if (e.last_repeat_ms && (uint32_t)(now - e.last_repeat_ms) < interval) continue;
    String cmd;
    if (e.bool_value && e.set_on[0]) cmd = e.set_on;
    else if (!e.bool_value && e.set_off[0]) cmd = e.set_off;
    else if (e.set_cmd[0]) cmd = apply_value_template(e.set_cmd, e.bool_value ? "1" : "0", e.bool_value ? e.value_on : e.value_off);
    if (!cmd.length()) continue;
    e.last_repeat_ms = now;
    if (is_binary_protocol()) {
      uint8_t binary_cmd[96]; uint8_t binary_len = 0;
      const int32_t repeat_value = e.bool_value ? e.value_on : e.value_off;
      if (!binary_build_template(cmd.c_str(), repeat_value, binary_cmd, sizeof(binary_cmd), binary_len)) continue;
      local_send_line(binary_cmd, binary_len);
    } else {
      local_send_line((const uint8_t*)cmd.c_str(), (uint8_t)cmd.length());
    }
    profile_poll_pause_until_ms = now + 80UL;
    break;
  }
}
static void profile_poll_tick() {
  if (!profile_entity_count) return;
  const uint32_t now = millis();
  if (profile_poll_pause_until_ms && (int32_t)(now - profile_poll_pause_until_ms) < 0) return;
  if ((uint32_t)(now - last_profile_poll_ms) < 80UL) return;
  last_profile_poll_ms = now;
  if (profile_readback_entity_id) {
    ProfileEntity* pending = profile_find_entity(profile_readback_entity_id);
    profile_readback_entity_id = 0;
    if (pending && pending->used && profile_entity_readable(*pending) && pending->poll[0]) {
      pending->last_poll_ms = now;
      if (is_binary_protocol()) {
        uint8_t binary_poll[96]; uint8_t binary_len = 0;
        if (binary_build_template(pending->poll, 0, binary_poll, sizeof(binary_poll), binary_len)) local_send_line(binary_poll, binary_len);
      } else local_send_line((const uint8_t*)pending->poll, (uint8_t)strlen(pending->poll));
      return;
    }
  }
  for (uint8_t tries = 0; tries < profile_entity_count; ++tries) {
    profile_poll_cursor = (uint8_t)(profile_poll_cursor % profile_entity_count);
    ProfileEntity& e = profile_entities[profile_poll_cursor++];
    if (!e.used || !profile_entity_readable(e) || !e.poll[0]) continue;
    if (e.last_poll_ms && (uint32_t)(now - e.last_poll_ms) < e.poll_ms) continue;
    e.last_poll_ms = now;
    if (is_binary_protocol()) {
      uint8_t binary_poll[96]; uint8_t binary_len = 0;
      if (binary_build_template(e.poll, 0, binary_poll, sizeof(binary_poll), binary_len)) local_send_line(binary_poll, binary_len);
    } else local_send_line((const uint8_t*)e.poll, (uint8_t)strlen(e.poll));
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
  v = config_value(cfg, "line_end");
  if (v.length()) {
    v.toUpperCase();
    if (!valid_line_end(v.c_str())) {
      send_status_response(req, STATUS_BAD_VALUE);
      return;
    }
    clean_copy(line_end_name, sizeof(line_end_name), v.c_str());
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
  prefs.begin("ofe-uart", false);
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
    profile_repeat_tick();
    check_output_failsafe();
  } else {
    check_output_failsafe();
  }
  record_loop_time((uint32_t)(micros() - loop_start));
  delay(1);
}
