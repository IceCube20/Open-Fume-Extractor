#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <Update.h>
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
#define RS485_BAUD 250000 // 230400 Standart
#endif

#ifndef JBC_RX_PIN
#define JBC_RX_PIN 17
#endif

#ifndef JBC_TX_PIN
#define JBC_TX_PIN 16
#endif

#ifndef JBC_DEBUG_RX
#define JBC_DEBUG_RX 0
#endif

static const uint16_t HW_VERSION = 0x0100;
#ifndef OFE_STR_HELPER
#define OFE_STR_HELPER(x) #x
#define OFE_STR(x) OFE_STR_HELPER(x)
#endif

#define OFE_MODULE_FW_MAJOR 1
#define OFE_MODULE_FW_MINOR 1
#define OFE_MODULE_FW_PATCH 59
#define OFE_MODULE_FW_SUFFIX "beta"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=JBC_BUS;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}

static const uint8_t DLE = 0x10;
static const uint8_t STX = 0x02;
static const uint8_t ETX = 0x03;
static const uint8_t CTRL_SYN_P02 = 0x16;

namespace jbc_base {
static const uint8_t M_ACK = 6;
static const uint8_t M_NACK = 21;
static const uint8_t M_SYN = 22;
}

namespace jbc_fe {
static const uint8_t M_HS = 0;
static const uint8_t M_ACK = 6;
static const uint8_t M_FIRMWARE = 33;
static const uint8_t M_R_DEVICEIDORIGINAL = 28;
static const uint8_t M_R_DISCOVER = 29;
static const uint8_t M_R_DEVICEID = 30;
static const uint8_t M_W_DEVICEID = 31;
static const uint8_t M_R_INTAKEACTIVATION = 56;
static const uint8_t M_W_INTAKEACTIVATION = 57;
static const uint8_t M_R_SUCTIONLEVEL = 48;
static const uint8_t M_W_SUCTIONLEVEL = 49;
static const uint8_t M_R_FLOW = 50;
static const uint8_t M_R_SPEED = 51;
static const uint8_t M_R_SELECTFLOW = 52;
static const uint8_t M_W_SELECTFLOW = 53;
static const uint8_t M_R_STANDINTAKES = 54;
static const uint8_t M_W_STANDINTAKES = 55;
static const uint8_t M_R_SUCTIONDELAY = 58;
static const uint8_t M_W_SUCTIONDELAY = 59;
static const uint8_t M_R_DELAYTIME = 60;
static const uint8_t M_R_ACTIVATIONPEDAL = 61;
static const uint8_t M_W_ACTIVATIONPEDAL = 62;
static const uint8_t M_R_PEDALMODE = 63;
static const uint8_t M_W_PEDALMODE = 64;
static const uint8_t M_R_FILTERSTATUS = 65;
static const uint8_t M_R_RESETFILTER = 66;
static const uint8_t M_R_CONNECTEDPEDAL = 68;
static const uint8_t M_R_FILTERSAT = 69;
static const uint8_t M_R_PIN = 81;
static const uint8_t M_W_PIN = 82;
static const uint8_t M_R_STATIONLOCKED = 83;
static const uint8_t M_W_STATIONLOCKED = 84;
static const uint8_t M_R_BEEP = 85;
static const uint8_t M_W_BEEP = 86;
static const uint8_t M_R_CONTINUOUSSUCTION = 87;
static const uint8_t M_W_CONTINUOUSSUCTION = 88;
static const uint8_t M_R_STATERROR = 89;
static const uint8_t M_R_DEVICENAME = 91;
static const uint8_t M_W_DEVICENAME = 92;
static const uint8_t M_R_PINENABLED = 93;
static const uint8_t M_W_PINENABLED = 94;
static const uint8_t M_W_WORKINTAKES = 96;
static const uint8_t M_R_USB_CONNECTSTATUS = 224;
static const uint8_t M_W_USB_CONNECTSTATUS = 225;
}

enum BaseState {
  BASE_IDLE,
  BASE_SEEN_NAK,
  BASE_SENT_SYN,
  BASE_GOT_ACK1,
  BASE_SENT_ACK2,
  BASE_GOT_SOH,
  BASE_P02_ACTIVE
};

enum RxState {
  WAIT_DLE,
  WAIT_STX,
  IN_FRAME
};

static HardwareSerial RS485(1);
static HardwareSerial JBC(2);
static Link bus(RS485);
static Preferences prefs;
static OfeStatusLed ofe_status_leds;

static uint8_t module_addr = 0x10;
static char module_label[24] = {0};
static bool fw_update_active = false;
static uint32_t fw_update_last_ms = 0;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;

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
static uint8_t my_addr = 0x91;
static uint8_t station_addr = 0x00;
static bool addr_locked = false;
static BaseState base_state = BASE_IDLE;
static RxState rx_state = WAIT_DLE;
static uint32_t last_syn_ms = 0;
static uint32_t last_master_ms = 0;
static uint32_t last_jbc_rx_ms = 0;
static uint32_t last_jbc_frame_ms = 0;
static uint8_t join_announce_left = 0;
static uint32_t next_join_announce_ms = 0;
static bool discover_response_pending = false;
static uint8_t discover_response_dst = ADDR_MASTER;
static uint8_t discover_response_seq = 0;
static uint32_t discover_response_due_ms = 0;
static uint32_t loop_window_ms = 0;
static uint32_t loop_max_us = 0;
static uint8_t cpu_load_pct = 0;
static uint16_t loop_max_ms = 0;
static TaskStatus_t cpu_task_stats[48];
static configRUN_TIME_COUNTER_TYPE cpu_prev_total = 0;
static configRUN_TIME_COUNTER_TYPE cpu_prev_idle = 0;
static bool cpu_runtime_valid = false;

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

static const size_t P02_MAX = 261;
static uint8_t p02_buf[P02_MAX];
static size_t p02_len = 0;
static bool p02_escape = false;
static bool p02_have_len = false;
static uint8_t p02_data_len = 0;

static uint8_t work_mask = 0;
static uint8_t stand_mask = 0;
// Effective OFE extractor output reported by the Master. This is deliberately
// separate from work_mask/stand_mask: those masks are JBC intake requests, while
// M_R_INTAKEACTIVATION must report the state of the FAE we emulate.
static uint8_t extractor_output_active = 0;
static uint8_t fast_flags = 0;
static uint16_t event_seq = 1;

static uint8_t suction_level = 3;
static uint16_t select_flow = 100;
static uint16_t actual_flow = 0;
static uint16_t speed_rpm = 0;
static uint8_t stand_intakes = 1;
static uint16_t delay_work_sec = 10;
static uint16_t delay_stand_sec = 0;
static uint8_t pedal_act = 0;
static uint8_t pedal_mode = 1;
static uint16_t filter_life = 0;
static uint16_t filter_sat = 0;
static uint16_t stat_error = 0;
static uint8_t continuous = 0;
static uint8_t usb_connect = 1;
static uint8_t connected_pedal = 1;
static uint8_t station_locked = 0;
static uint8_t beep = 1;
static uint8_t pin_enabled = 0;
static char pin_code[5] = "0000";
static char device_name[17] = "JBC FAE Emulator";

static uint8_t device_id[64];
static uint8_t device_id_len = 32;

static const char FW_STR[] = "02:F2:EMU_02:8881031:0051123";
static const char DEFAULT_DEVICE_ID[] = "849158E467A340159646170D6B1595EF";

static uint64_t module_uid() {
  return 0x1000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}
static bool valid_module_addr(uint8_t addr) {
  return addr >= 0x10 && addr <= 0x1F;
}


static void cleanup_legacy_runtime_prefs() {
  // These keys existed in older builds, but they are runtime state now.
  // Persistent JBC-module NVS is intentionally limited to:
  //   addr, label, jbc_addr, devid
  prefs.remove("suction");
  prefs.remove("sel_flow");
  prefs.remove("stand_in");
  prefs.remove("dly_work");
  prefs.remove("dly_stand");
  prefs.remove("cont");
}

static uint8_t bcc_calc_xor(const uint8_t* p, size_t n) {
  uint8_t b = 0x01;
  for (size_t i = 0; i < n; ++i) b ^= p[i];
  return b;
}

static size_t stuff_bytes(const uint8_t* in, size_t n, uint8_t* out, size_t out_max) {
  size_t o = 0;
  for (size_t i = 0; i < n && o < out_max; ++i) {
    const uint8_t b = in[i];
    out[o++] = b;
    if (b == DLE && o < out_max) out[o++] = DLE;
  }
  return o;
}


static const uint8_t LOCAL_TRACE_CAPACITY = 192;
static const uint8_t LOCAL_TRACE_PREVIEW = 48;

struct LocalTraceEvent {
  uint32_t ms;
  uint8_t dir;
  uint8_t meta1;
  uint8_t meta2;
  uint8_t len;
  uint8_t data[LOCAL_TRACE_PREVIEW];
};

static LocalTraceEvent local_trace[LOCAL_TRACE_CAPACITY];
static uint8_t local_trace_head = 0;
static uint8_t local_trace_count = 0;
static uint16_t local_trace_dropped = 0;
static bool local_trace_enabled = false;
static void rs485_status_response(const Frame& req, Status status);

static void local_trace_clear() {
  local_trace_head = 0;
  local_trace_count = 0;
  local_trace_dropped = 0;
}

static void local_trace_log(uint8_t dir, uint8_t meta1, uint8_t meta2, const uint8_t* data, size_t len) {
  if (!local_trace_enabled) return;
  LocalTraceEvent& ev = local_trace[local_trace_head];
  ev.ms = millis();
  ev.dir = dir;
  ev.meta1 = meta1;
  ev.meta2 = meta2;
  ev.len = (uint8_t)(len > LOCAL_TRACE_PREVIEW ? LOCAL_TRACE_PREVIEW : len);
  if (ev.len && data) memcpy(ev.data, data, ev.len);
  local_trace_head = (uint8_t)((local_trace_head + 1) % LOCAL_TRACE_CAPACITY);
  if (local_trace_count < LOCAL_TRACE_CAPACITY) ++local_trace_count;
  else if (local_trace_dropped < 0xFFFF) ++local_trace_dropped;
}

static void rs485_trace_control(const Frame& req) {
  if (req.len < 1) {
    rs485_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint8_t flags = req.payload[0];
  local_trace_enabled = (flags & 0x01) != 0;
  if (flags & 0x02) local_trace_clear();
  rs485_status_response(req, STATUS_OK);
}

static void rs485_trace_read(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_TRACE_READ | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = local_trace_enabled ? 1 : 0;
  put_u16_le(resp.payload + o, local_trace_dropped); o += 2;
  const uint8_t count_pos = o++;
  uint8_t sent = 0;
  while (local_trace_count && o + 6 <= MAX_PAYLOAD) {
    const uint8_t pos = (uint8_t)((local_trace_head + LOCAL_TRACE_CAPACITY - local_trace_count) % LOCAL_TRACE_CAPACITY);
    const LocalTraceEvent& ev = local_trace[pos];
    const uint32_t age = millis() - ev.ms;
    const uint16_t age_ms = age > 0xFFFFUL ? 0xFFFF : (uint16_t)age;
    const uint8_t n = ev.len > LOCAL_TRACE_PREVIEW ? LOCAL_TRACE_PREVIEW : ev.len;
    if (o + 6 + n > MAX_PAYLOAD) break;
    put_u16_le(resp.payload + o, age_ms); o += 2;
    resp.payload[o++] = ev.dir;
    resp.payload[o++] = ev.meta1;
    resp.payload[o++] = ev.meta2;
    resp.payload[o++] = n;
    if (n) memcpy(resp.payload + o, ev.data, n);
    o += n;
    --local_trace_count;
    ++sent;
  }
  resp.payload[count_pos] = sent;
  resp.len = o;
  local_trace_dropped = 0;
  bus.send(resp);
}
static void refresh_fast_error_flag() {
  if (stat_error || filter_life >= 900 || filter_sat >= 900) fast_flags |= FAST_FLAG_ERROR_PENDING;
  else fast_flags &= (uint8_t)~FAST_FLAG_ERROR_PENDING;
}

static void mark_fast_changed() {
  ++event_seq;
  refresh_fast_error_flag();
  // Do not set FAST_FLAG_CONNECTED here. This helper is also used for
  // local setting changes coming from the RS485 master, for example
  // Continuous suction. The station link state must only be set from
  // real JBC traffic in handle_p02_frame(), otherwise enabling Continuous
  // while the station is disconnected makes the master/display think the
  // JBC station is connected.
  fast_flags |= FAST_FLAG_STATE_CHANGED | FAST_FLAG_EVENT_PENDING;
}

static void send_p02(const uint8_t* payload, size_t n) {
  uint8_t tx[2 + P02_MAX * 2 + 4];
  size_t o = 0;
  const uint8_t bcc = bcc_calc_xor(payload, n);

  tx[o++] = DLE;
  tx[o++] = STX;
  o += stuff_bytes(payload, n, tx + o, sizeof(tx) - o - 4);
  tx[o++] = bcc;
  if (bcc == DLE) tx[o++] = DLE;
  tx[o++] = DLE;
  tx[o++] = ETX;
  JBC.write(tx, o);
  JBC.flush();
  local_trace_log(2, n > 3 ? payload[3] : 0, n > 2 ? payload[2] : 0, payload, n);
}

static uint8_t reply_src_for_dst(uint8_t req_dst) {
  (void)req_dst;
  return my_addr;
}

static void send_hs_ack(uint8_t src_addr, uint8_t dst, uint8_t fid) {
  uint8_t pl[] = { src_addr, dst, fid, jbc_fe::M_HS, 0x01, 0x06 };
  send_p02(pl, sizeof(pl));
}

static void send_syn_p02(uint8_t src_addr, uint8_t dst, uint8_t fid) {
  uint8_t pl[] = { src_addr, dst, fid, CTRL_SYN_P02, 0x01, 0x06 };
  send_p02(pl, sizeof(pl));
}

static void send_firmware(uint8_t src_addr, uint8_t dst, uint8_t fid) {
  uint8_t pl[5 + 64];
  size_t o = 0;
  const uint8_t len = (uint8_t)strlen(FW_STR);
  pl[o++] = src_addr;
  pl[o++] = dst;
  pl[o++] = fid;
  pl[o++] = jbc_fe::M_FIRMWARE;
  pl[o++] = len;
  memcpy(pl + o, FW_STR, len);
  o += len;
  send_p02(pl, o);
}

static void send_device_id(uint8_t src_addr, uint8_t dst, uint8_t fid) {
  uint8_t pl[5 + 64];
  size_t o = 0;
  pl[o++] = src_addr;
  pl[o++] = dst;
  pl[o++] = fid;
  pl[o++] = jbc_fe::M_R_DEVICEID;
  pl[o++] = device_id_len;
  memcpy(pl + o, device_id, device_id_len);
  o += device_id_len;
  send_p02(pl, o);
}

static void send_ack(uint8_t src_addr, uint8_t dst, uint8_t fid) {
  uint8_t pl[] = { src_addr, dst, fid, jbc_fe::M_ACK, 0x01, 0x06 };
  send_p02(pl, sizeof(pl));
}

static void send_read_reply(uint8_t src_addr, uint8_t dst, uint8_t fid, uint8_t ctrl, const uint8_t* data, uint8_t len) {
  uint8_t pl[5 + 16];
  size_t o = 0;
  pl[o++] = src_addr;
  pl[o++] = dst;
  pl[o++] = fid;
  pl[o++] = ctrl;
  pl[o++] = len;
  if (len) {
    memcpy(pl + o, data, len);
    o += len;
  }
  send_p02(pl, o);
}

static void le16(uint8_t* out, uint16_t v) {
  out[0] = (uint8_t)(v & 0xFF);
  out[1] = (uint8_t)(v >> 8);
}

static const char* ctrl_name(uint8_t ctrl) {
  switch (ctrl) {
    case jbc_fe::M_HS: return "M_HS";
    case CTRL_SYN_P02: return "CTRL_SYN_P02";
    case jbc_fe::M_FIRMWARE: return "M_FIRMWARE";
    case jbc_fe::M_R_DEVICEIDORIGINAL: return "M_R_DEVICEIDORIGINAL";
    case jbc_fe::M_R_DISCOVER: return "M_R_DISCOVER";
    case jbc_fe::M_R_DEVICEID: return "M_R_DEVICEID";
    case jbc_fe::M_W_DEVICEID: return "M_W_DEVICEID";
    case jbc_fe::M_R_SUCTIONLEVEL: return "M_R_SUCTIONLEVEL";
    case jbc_fe::M_W_SUCTIONLEVEL: return "M_W_SUCTIONLEVEL";
    case jbc_fe::M_R_FLOW: return "M_R_FLOW";
    case jbc_fe::M_R_SPEED: return "M_R_SPEED";
    case jbc_fe::M_R_SELECTFLOW: return "M_R_SELECTFLOW";
    case jbc_fe::M_W_SELECTFLOW: return "M_W_SELECTFLOW";
    case jbc_fe::M_R_STANDINTAKES: return "M_R_STANDINTAKES";
    case jbc_fe::M_W_STANDINTAKES: return "M_W_STANDINTAKES";
    case jbc_fe::M_R_INTAKEACTIVATION: return "M_R_INTAKEACTIVATION";
    case jbc_fe::M_W_INTAKEACTIVATION: return "M_W_INTAKEACTIVATION";
    case jbc_fe::M_R_SUCTIONDELAY: return "M_R_SUCTIONDELAY";
    case jbc_fe::M_W_SUCTIONDELAY: return "M_W_SUCTIONDELAY";
    case jbc_fe::M_R_DELAYTIME: return "M_R_DELAYTIME";
    case jbc_fe::M_R_ACTIVATIONPEDAL: return "M_R_ACTIVATIONPEDAL";
    case jbc_fe::M_W_ACTIVATIONPEDAL: return "M_W_ACTIVATIONPEDAL";
    case jbc_fe::M_R_PEDALMODE: return "M_R_PEDALMODE";
    case jbc_fe::M_W_PEDALMODE: return "M_W_PEDALMODE";
    case jbc_fe::M_R_FILTERSTATUS: return "M_R_FILTERSTATUS";
    case jbc_fe::M_R_FILTERSAT: return "M_R_FILTERSAT";
    case jbc_fe::M_R_CONTINUOUSSUCTION: return "M_R_CONTINUOUSSUCTION";
    case jbc_fe::M_W_CONTINUOUSSUCTION: return "M_W_CONTINUOUSSUCTION";
    case jbc_fe::M_R_STATERROR: return "M_R_STATERROR";
    case jbc_fe::M_W_WORKINTAKES: return "M_W_WORKINTAKES";
    default: return "?";
  }
}

static void debug_rx_frame(uint8_t src, uint8_t dst, uint8_t fid, uint8_t ctrl, const uint8_t* data, uint8_t len) {
#if JBC_DEBUG_RX
  Serial.print("JBC RX src=0x");
  if (src < 0x10) Serial.print('0');
  Serial.print(src, HEX);
  Serial.print(" dst=0x");
  if (dst < 0x10) Serial.print('0');
  Serial.print(dst, HEX);
  Serial.print(" fid=0x");
  if (fid < 0x10) Serial.print('0');
  Serial.print(fid, HEX);
  Serial.print(" ctrl=0x");
  if (ctrl < 0x10) Serial.print('0');
  Serial.print(ctrl, HEX);
  Serial.print(" ");
  Serial.print(ctrl_name(ctrl));
  Serial.print(" len=");
  Serial.print(len);
  Serial.print(" data=");
  for (uint8_t i = 0; i < len && data; ++i) {
    if (i) Serial.print(' ');
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
  Serial.println();
#else
  (void)src; (void)dst; (void)fid; (void)ctrl; (void)data; (void)len;
#endif
}

static bool is_for_us(uint8_t dst) {
  return dst == 0x00 || dst == my_addr;
}

static bool is_fae_a_addr(uint8_t addr) {
  return addr == 0x18 || addr == 0x12;
}

static void handle_p02_frame(const uint8_t* f, size_t n) {
  if (n < 4) return;

  const uint8_t src = f[0];
  const uint8_t dst = f[1];
  const uint8_t fid = f[2];
  const uint8_t ctrl = f[3];
  const uint8_t len = (n >= 5) ? f[4] : 0;
  const uint8_t* data = (n >= 5 && len) ? f + 5 : nullptr;
  const uint8_t reply_to = src == 0x00 ? 0x00 : src;
  const uint8_t reply_src_addr = reply_src_for_dst(dst);
  const uint32_t now = millis();

  debug_rx_frame(src, dst, fid, ctrl, data, len);
  local_trace_log(1, ctrl, fid, f, n);

  if (is_fae_a_addr(dst) && (!addr_locked || dst == my_addr)) station_addr = dst;

  if (ctrl == jbc_fe::M_HS) {
    if (dst != 0x00 && (addr_locked ? dst != my_addr : !is_fae_a_addr(dst))) return;
    if (!addr_locked && dst != 0x00) {
      my_addr = dst;
      station_addr = dst;
      prefs.putUChar("jbc_addr", my_addr);
      addr_locked = true;
      Serial.print("JBC adopt addr=0x");
      Serial.println(my_addr, HEX);
    }
    send_hs_ack(reply_src_addr, reply_to, fid);
    last_jbc_frame_ms = now;
    fast_flags |= FAST_FLAG_CONNECTED;
    return;
  }

  if (!is_for_us(dst)) return;

  last_jbc_frame_ms = now;
  fast_flags |= FAST_FLAG_CONNECTED;

  if (ctrl == CTRL_SYN_P02) {
    send_syn_p02(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_FIRMWARE) {
    send_firmware(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_R_DEVICEID || ctrl == jbc_fe::M_R_DEVICEIDORIGINAL || ctrl == jbc_fe::M_R_DISCOVER) {
    send_device_id(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_DEVICEID && data && len >= 1) {
    device_id_len = len > sizeof(device_id) ? sizeof(device_id) : len;
    memcpy(device_id, data, device_id_len);
    prefs.putBytes("devid", device_id, device_id_len);
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_INTAKEACTIVATION && data && len >= 2) {
    // Real DDE -> FAE wire format: [OnOff, Port]. 0x39 is specifically the
    // WORK-intake activation command. Keep the per-port request mask so the
    // OFE Master can aggregate the demand, but do not use it as 0x38 readback.
    const uint8_t val = data[0] ? 1U : 0U;
    const uint8_t port = data[1];
    send_ack(reply_src_addr, reply_to, fid);

    if (port < 8) {
      const uint8_t bit = (uint8_t)(1U << port);
      const uint8_t old_work = work_mask;
      if (val) work_mask |= bit;
      else work_mask &= (uint8_t)~bit;
      if (old_work != work_mask) {
        mark_fast_changed();
        Serial.print("JBC work intake port=");
        Serial.print(port);
        Serial.print(" on=");
        Serial.print(val);
        Serial.print(" mask=0x");
        Serial.println(work_mask, HEX);
      }
    }
    return;
  }

  if (ctrl == jbc_fe::M_W_SUCTIONLEVEL && data && len >= 1) {
    suction_level = data[0] > 3 ? 3 : data[0];
    send_ack(reply_src_addr, reply_to, fid);
    mark_fast_changed();
    return;
  }

  if (ctrl == jbc_fe::M_W_SELECTFLOW && data && len >= 2) {
    select_flow = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    if (select_flow > 1000) select_flow = 1000;
    send_ack(reply_src_addr, reply_to, fid);
    mark_fast_changed();
    return;
  }

  if (ctrl == jbc_fe::M_W_STANDINTAKES && data && len >= 1) {
    stand_intakes = data[0] ? 1 : 0;
    send_ack(reply_src_addr, reply_to, fid);
    mark_fast_changed();
    return;
  }

  if (ctrl == jbc_fe::M_W_WORKINTAKES && data && len >= 1) {
    const uint8_t old_work = work_mask;
    work_mask = data[0] ? 0x01 : 0x00;
    send_ack(reply_src_addr, reply_to, fid);
    if (old_work != work_mask) {
      mark_fast_changed();
      Serial.print("JBC workintakes work=0x");
      Serial.println(work_mask, HEX);
    }
    return;
  }

  if (ctrl == jbc_fe::M_W_SUCTIONDELAY && data && len >= 4) {
    const uint16_t v = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    const uint8_t intake = data[3];
    if (intake == 0) delay_work_sec = v;
    else if (intake == 1) delay_stand_sec = v;
    send_ack(reply_src_addr, reply_to, fid);
    mark_fast_changed();
    return;
  }

  if (ctrl == jbc_fe::M_W_ACTIVATIONPEDAL && data && len >= 2) {
    pedal_act = data[0] ? 1 : 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_PEDALMODE && data && len >= 2) {
    pedal_mode = data[0] > 2 ? 2 : data[0];
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_CONTINUOUSSUCTION && data && len >= 1) {
    continuous = data[0] ? 1 : 0;
    if (continuous) fast_flags |= FAST_FLAG_CONTINUOUS;
    else fast_flags &= (uint8_t)~FAST_FLAG_CONTINUOUS;
    send_ack(reply_src_addr, reply_to, fid);
    mark_fast_changed();
    return;
  }

  if (ctrl == jbc_fe::M_W_USB_CONNECTSTATUS && data && len >= 1) {
    usb_connect = data[0] ? 1 : 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_STATIONLOCKED && data && len >= 1) {
    station_locked = data[0] ? 1 : 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_BEEP && data && len >= 1) {
    beep = data[0] ? 1 : 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_PINENABLED && data && len >= 1) {
    pin_enabled = data[0] ? 1 : 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_PIN && data && len >= 4) {
    memcpy(pin_code, data, 4);
    pin_code[4] = 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_W_DEVICENAME && data && len >= 1) {
    const uint8_t copy_len = len >= sizeof(device_name) ? sizeof(device_name) - 1 : len;
    memcpy(device_name, data, copy_len);
    device_name[copy_len] = 0;
    send_ack(reply_src_addr, reply_to, fid);
    return;
  }

  if (ctrl == jbc_fe::M_R_INTAKEACTIVATION) {
    const uint8_t port = (len >= 1 && data) ? data[0] : 0;
    const uint8_t intake = (len >= 2 && data) ? data[1] : 0;
    // We emulate the FAE itself, not a command register. Report whether the
    // effective OFE extractor output is really ON. Echo Port/Intake exactly as
    // the real FAE does on the DDE peripheral bus.
    uint8_t out[] = { extractor_output_active ? 1U : 0U, port, intake };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_SUCTIONLEVEL) {
    uint8_t out[] = { suction_level };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_SELECTFLOW) {
    uint8_t out[2];
    le16(out, select_flow);
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_FLOW) {
    uint8_t out[2];
    le16(out, actual_flow);
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_SPEED) {
    uint8_t out[2];
    le16(out, speed_rpm);
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_STANDINTAKES) {
    uint8_t out[] = { stand_intakes };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_SUCTIONDELAY) {
    uint8_t port = (len >= 1 && data) ? data[0] : 0;
    uint8_t intake = (len >= 2 && data) ? data[1] : 0;
    uint16_t v = intake == 0 ? delay_work_sec : delay_stand_sec;
    uint8_t out[4];
    le16(out, v);
    out[2] = port;
    out[3] = intake;
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_DELAYTIME) {
    uint8_t port = (len >= 1 && data) ? data[0] : 0;
    uint8_t intake = (len >= 2 && data) ? data[1] : 0;
    uint8_t out[4] = { 0, 0, port, intake };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_ACTIVATIONPEDAL) {
    uint8_t port = (len >= 1 && data) ? data[0] : 0;
    uint8_t out[] = { pedal_act, port };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_PEDALMODE) {
    uint8_t port = (len >= 1 && data) ? data[0] : 0;
    uint8_t out[] = { pedal_mode, port };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_FILTERSTATUS) {
    uint8_t out[2];
    le16(out, filter_life);
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_FILTERSAT) {
    uint8_t out[2];
    le16(out, filter_sat);
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_RESETFILTER) {
    filter_life = 0;
    filter_sat = 0;
    uint8_t out[] = { 0 };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_CONNECTEDPEDAL) {
    uint8_t port = (len >= 1 && data) ? data[0] : 0;
    uint8_t out[] = { connected_pedal, port };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_CONTINUOUSSUCTION) {
    uint8_t out[] = { continuous };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_USB_CONNECTSTATUS) {
    uint8_t out[] = { usb_connect };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_STATERROR) {
    uint8_t out[2];
    le16(out, stat_error);
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_PIN) {
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, (const uint8_t*)pin_code, 4);
    return;
  }

  if (ctrl == jbc_fe::M_R_STATIONLOCKED) {
    uint8_t out[] = { station_locked };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_BEEP) {
    uint8_t out[] = { beep };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_PINENABLED) {
    uint8_t out[] = { pin_enabled };
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, out, sizeof(out));
    return;
  }

  if (ctrl == jbc_fe::M_R_DEVICENAME) {
    send_read_reply(reply_src_addr, reply_to, fid, ctrl, (const uint8_t*)device_name, (uint8_t)strlen(device_name));
    return;
  }

#if JBC_DEBUG_RX
  Serial.print("JBC unhandled ctrl=0x");
  if (ctrl < 0x10) Serial.print('0');
  Serial.println(ctrl, HEX);
#endif
}

static void reset_p02_rx() {
  rx_state = WAIT_DLE;
  p02_len = 0;
  p02_escape = false;
  p02_have_len = false;
  p02_data_len = 0;
}

static void poll_p02() {
  while (JBC.available()) {
    const uint8_t by = (uint8_t)JBC.read();
    last_jbc_rx_ms = millis();

    switch (rx_state) {
      case WAIT_DLE:
        if (by == DLE) rx_state = WAIT_STX;
        break;

      case WAIT_STX:
        if (by == STX) {
          rx_state = IN_FRAME;
          p02_len = 0;
          p02_escape = false;
          p02_have_len = false;
          p02_data_len = 0;
        } else if (by != DLE) {
          rx_state = WAIT_DLE;
        }
        break;

      case IN_FRAME:
        if (!p02_escape) {
          if (by == DLE) {
            p02_escape = true;
          } else if (p02_len < P02_MAX) {
            p02_buf[p02_len++] = by;
            if (!p02_have_len && p02_len == 5) {
              p02_data_len = p02_buf[4];
              p02_have_len = true;
            }
            if (p02_have_len && p02_len == (size_t)(5 + p02_data_len + 1)) {
              const uint8_t rx_bcc = p02_buf[p02_len - 1];
              const size_t no_bcc = 5 + p02_data_len;
              if (rx_bcc == bcc_calc_xor(p02_buf, no_bcc)) handle_p02_frame(p02_buf, no_bcc);
              reset_p02_rx();
            }
          } else {
            reset_p02_rx();
          }
        } else {
          if (by == DLE) {
            if (p02_len < P02_MAX) p02_buf[p02_len++] = DLE;
            p02_escape = false;
          } else if (by == ETX) {
            if (p02_len >= 6) {
              const uint8_t rx_bcc = p02_buf[p02_len - 1];
              const size_t no_bcc = p02_len - 1;
              if (rx_bcc == bcc_calc_xor(p02_buf, no_bcc)) handle_p02_frame(p02_buf, no_bcc);
            }
            reset_p02_rx();
          } else if (by == STX) {
            rx_state = IN_FRAME;
            p02_len = 0;
            p02_escape = false;
            p02_have_len = false;
            p02_data_len = 0;
          } else {
            reset_p02_rx();
          }
        }
        break;
    }
  }
}

static void poll_base() {
  while (JBC.available()) {
    const uint8_t by = (uint8_t)JBC.read();
    last_jbc_rx_ms = millis();

    if (by == DLE) {
      base_state = BASE_P02_ACTIVE;
      rx_state = WAIT_STX;
      return;
    }

    switch (base_state) {
      case BASE_IDLE:
        if (by == jbc_base::M_NACK) base_state = BASE_SEEN_NAK;
        break;
      case BASE_SEEN_NAK:
        if ((uint32_t)(millis() - last_syn_ms) >= 1500UL) {
          JBC.write(jbc_base::M_SYN);
          JBC.flush();
          last_syn_ms = millis();
          base_state = BASE_SENT_SYN;
        }
        break;
      case BASE_SENT_SYN:
        if (by == jbc_base::M_ACK) base_state = BASE_GOT_ACK1;
        break;
      case BASE_GOT_ACK1:
        JBC.write(jbc_base::M_ACK);
        JBC.flush();
        base_state = BASE_SENT_ACK2;
        break;
      case BASE_SENT_ACK2:
        if (by == 0x01) base_state = BASE_GOT_SOH;
        break;
      case BASE_GOT_SOH:
        base_state = BASE_P02_ACTIVE;
        rx_state = WAIT_DLE;
        break;
      case BASE_P02_ACTIVE:
        return;
    }
  }
}

static void poll_jbc() {
  if (base_state == BASE_P02_ACTIVE) poll_p02();
  else poll_base();

  if (last_jbc_rx_ms && (uint32_t)(millis() - last_jbc_rx_ms) > 1500UL) {
    base_state = BASE_IDLE;
    reset_p02_rx();
    addr_locked = false;
    station_addr = 0;
    fast_flags &= (uint8_t)~FAST_FLAG_CONNECTED;
    if (work_mask || stand_mask) {
      work_mask = 0;
      stand_mask = 0;
      mark_fast_changed();
    }
    last_jbc_rx_ms = 0;
    last_jbc_frame_ms = 0;
  }
}

static void rs485_status_response(const Frame& req, Status status) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = req.cmd | 0x80;
  resp.len = 1;
  resp.payload[0] = status;
  bus.send(resp);
}

static void rs485_fw_begin(const Frame& req) {
  if (req.len < 4) {
    rs485_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint32_t size = get_u32_le(req.payload);
  if (fw_update_active && fw_update_offset == 0) {
    fw_update_touch();
    rs485_status_response(req, STATUS_OK);
    return;
  }
  if (!Update.begin(size ? size : UPDATE_SIZE_UNKNOWN)) {
    fw_update_active = false;
    rs485_status_response(req, STATUS_BUSY);
    return;
  }
  fw_update_active = true;
  fw_update_touch();
  fw_update_offset = 0;
  fw_update_buffer_reset();
  rs485_status_response(req, STATUS_OK);
}

static void rs485_fw_chunk(const Frame& req) {
  if (!fw_update_active) {
    rs485_status_response(req, STATUS_BUSY);
    return;
  }
  if (req.len < 5) {
    fw_update_abort_local();
    rs485_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint32_t offset = get_u32_le(req.payload);
  const uint8_t n = req.len - 4;
  if (offset != fw_update_offset) {
    if (offset < fw_update_offset && (uint32_t)offset + n <= fw_update_offset) {
      fw_update_touch();
      rs485_status_response(req, STATUS_OK);
      return;
    }
    fw_update_abort_local();
    rs485_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  if (!fw_update_buffer_append(req.payload + 4, n)) {
    fw_update_abort_local();
    rs485_status_response(req, STATUS_BUSY);
    return;
  }
  fw_update_offset += n;
  fw_update_touch();
  rs485_status_response(req, STATUS_OK);
}

static void rs485_fw_end(const Frame& req) {
  if (!fw_update_active) {
    rs485_status_response(req, STATUS_BUSY);
    return;
  }
  const bool ok = fw_update_buffer_flush() && Update.end(true);
  fw_update_active = false;
  rs485_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
  if (ok) {
    delay(300);
    ESP.restart();
  }
}

static void rs485_fw_status(const Frame& req) {
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

static void rs485_fw_abort(const Frame& req) {
  fw_update_abort_local();
  rs485_status_response(req, STATUS_OK);
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

static void rs485_set_label(const Frame& req) {
  copy_label_from_payload(req);
  bool ok = true;
  if (module_label[0]) ok = prefs.putString("label", module_label) > 0;
  else prefs.remove("label");
  rs485_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
}

static void rs485_info(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_INFO | 0x80;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_JBC_BUS;
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
  const char name[] = "JBC FAE Bus";
  const char* shown_name = module_label[0] ? module_label : name;
  while (*shown_name && o < MAX_PAYLOAD) resp.payload[o++] = (uint8_t)*shown_name++;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static void rs485_caps(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_CAPS | 0x80;
  resp.len = 5;
  resp.payload[0] = STATUS_OK;
  put_u32_le(resp.payload + 1, CAP_JBC_BUS | CAP_FW_UPDATE | CAP_FAULT_REPORT | CAP_LOCAL_TRACE);
  bus.send(resp);
}

static void rs485_fast_poll(const Frame& req) {
  refresh_fast_error_flag();
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_FAST_POLL | 0x80;
  resp.len = 6;
  resp.payload[0] = STATUS_OK;
  put_u16_le(resp.payload + 1, event_seq);
  resp.payload[3] = work_mask;
  resp.payload[4] = stand_mask;
  resp.payload[5] = fast_flags;
  bus.send(resp);
}

static void rs485_get_state(const Frame& req) {
  refresh_fast_error_flag();
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_STATE | 0x80;
  resp.len = 29 + 1 + device_id_len;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = fast_flags;
  resp.payload[o++] = my_addr;
  resp.payload[o++] = station_addr;
  resp.payload[o++] = (uint8_t)base_state;
  resp.payload[o++] = work_mask;
  resp.payload[o++] = stand_mask;
  resp.payload[o++] = suction_level;
  put_u16_le(resp.payload + o, select_flow); o += 2;
  put_u16_le(resp.payload + o, actual_flow); o += 2;
  put_u16_le(resp.payload + o, speed_rpm); o += 2;
  put_u16_le(resp.payload + o, delay_work_sec); o += 2;
  put_u16_le(resp.payload + o, delay_stand_sec); o += 2;
  resp.payload[o++] = stand_intakes;
  resp.payload[o++] = continuous;
  put_u16_le(resp.payload + o, filter_life); o += 2;
  put_u16_le(resp.payload + o, filter_sat); o += 2;
  put_u16_le(resp.payload + o, stat_error); o += 2;
  resp.payload[o++] = usb_connect;
  put_u16_le(resp.payload + o, event_seq); o += 2;
  resp.payload[o++] = device_id_len;
  memcpy(resp.payload + o, device_id, device_id_len);
  o += device_id_len;
  // v1.1.59+: developer/readback extension. Keep it after the variable Device ID
  // so older Masters continue to parse the legacy GET_STATE layout unchanged.
  resp.payload[o++] = extractor_output_active ? 1U : 0U;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static uint32_t discover_delay_ms(const Frame& req) {
  const uint64_t uid = module_uid();
  const uint8_t round = req.len ? req.payload[0] : req.seq;
  uint32_t mix = (uint32_t)uid ^ (uint32_t)(uid >> 32) ^ 0x9E3779B9UL;
  mix ^= (uint32_t)round * 0x85EBCA6BUL;
  mix ^= mix >> ((round & 7) + 3);
  mix *= 0xC2B2AE35UL;
  mix ^= mix >> 16;
  const uint8_t slot = (uint8_t)(mix & 0x3F);
  return 5UL + (uint32_t)slot * 6UL;
}

static void send_discover_response(uint8_t dst, uint8_t seq) {
  const uint64_t uid = module_uid();
  Frame resp;
  resp.dst = dst;
  resp.src = module_addr;
  resp.seq = seq;
  resp.cmd = CMD_DISCOVER_MODULES | 0x80;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_JBC_BUS;
  put_u64_le(resp.payload + o, uid); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u32_le(resp.payload + o, CAP_JBC_BUS | CAP_FW_UPDATE | CAP_FAULT_REPORT | CAP_LOCAL_TRACE); o += 4;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static void rs485_discover(const Frame& req) {
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
  Frame resp;
  resp.dst = ADDR_MASTER;
  resp.src = module_addr;
  resp.seq = 0;
  resp.cmd = CMD_DISCOVER_MODULES | 0x80;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_JBC_BUS;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u32_le(resp.payload + o, CAP_JBC_BUS | CAP_FW_UPDATE | CAP_FAULT_REPORT | CAP_LOCAL_TRACE); o += 4;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static void poll_join_announce() {
  if (!join_announce_left) return;
  if ((int32_t)(millis() - next_join_announce_ms) < 0) return;
  send_join_announce();
  --join_announce_left;
}

static void rs485_set_address_uid(const Frame& req) {
  if (req.len < 9) return;
  const uint64_t target_uid = get_u64_le(req.payload);
  if (target_uid != module_uid()) return;
  const uint8_t next_addr = req.payload[8];
  if (!valid_module_addr(next_addr)) {
    rs485_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  prefs.putUChar("addr", next_addr);
  rs485_status_response(req, STATUS_OK);
  delay(20);
  module_addr = next_addr;
}

static void rs485_set_state(const Frame& req) {
  if (req.len < 9) {
    rs485_status_response(req, STATUS_BAD_LEN);
    return;
  }

  const uint8_t old_suction_level = suction_level;
  const uint16_t old_select_flow = select_flow;
  const uint16_t old_delay_work_sec = delay_work_sec;
  const uint16_t old_delay_stand_sec = delay_stand_sec;
  const uint8_t old_stand_intakes = stand_intakes;
  const uint8_t old_continuous = continuous;
  const uint16_t old_stat_error = stat_error;
  const uint16_t old_filter_life = filter_life;
  const uint16_t old_filter_sat = filter_sat;
  const uint8_t old_extractor_output_active = extractor_output_active;

  suction_level = req.payload[0] > 3 ? 3 : req.payload[0];
  select_flow = get_u16_le(req.payload + 1);
  if (select_flow > 1000) select_flow = 1000;
  delay_work_sec = get_u16_le(req.payload + 3);
  delay_stand_sec = get_u16_le(req.payload + 5);
  stand_intakes = req.payload[7] ? 1 : 0;
  continuous = req.payload[8] ? 1 : 0;
  if (req.len >= 11) stat_error = get_u16_le(req.payload + 9);
  if (req.len >= 15) {
    filter_life = get_u16_le(req.payload + 11);
    filter_sat = get_u16_le(req.payload + 13);
  }
  // v1.1.58+: optional Master feedback byte. Older Masters still send the
  // legacy 15-byte state and therefore leave the safe OFF default unchanged.
  if (req.len >= 16) extractor_output_active = req.payload[15] ? 1U : 0U;
  if (continuous) fast_flags |= FAST_FLAG_CONTINUOUS;
  else fast_flags &= (uint8_t)~FAST_FLAG_CONTINUOUS;

  const bool runtime_settings_changed =
    suction_level != old_suction_level ||
    select_flow != old_select_flow ||
    delay_work_sec != old_delay_work_sec ||
    delay_stand_sec != old_delay_stand_sec ||
    stand_intakes != old_stand_intakes ||
    continuous != old_continuous;
  const bool system_status_changed =
    stat_error != old_stat_error ||
    filter_life != old_filter_life ||
    filter_sat != old_filter_sat ||
    extractor_output_active != old_extractor_output_active;

  // Runtime values are supplied by the Master / station and must not be
  // persisted in NVS. Only addr, label, jbc_addr and devid survive reboot.
  if (runtime_settings_changed || system_status_changed) mark_fast_changed();
  else refresh_fast_error_flag();
  rs485_status_response(req, STATUS_OK);
}

static void record_loop_time(uint32_t busy_us) {
  if (busy_us > loop_max_us) loop_max_us = busy_us;
  const uint32_t now = millis();
  if ((uint32_t)(now - loop_window_ms) >= 1000UL) {
    uint32_t max_ms = (loop_max_us + 999UL) / 1000UL;
    if (max_ms > 65535UL) max_ms = 65535UL;
    loop_max_ms = (uint16_t)max_ms;
    sample_cpu_load();
    loop_window_ms = now;
    loop_max_us = 0;
  }
}

static void append_system_telemetry(uint8_t* payload, uint8_t& o) {
  put_u32_le(payload + o, ESP.getFreeHeap()); o += 4;
  put_u32_le(payload + o, ESP.getMinFreeHeap()); o += 4;
  put_u32_le(payload + o, millis() / 1000UL); o += 4;
  payload[o++] = cpu_load_pct;
  put_u16_le(payload + o, loop_max_ms); o += 2;
}

static void rs485_get_telemetry(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_TELEMETRY | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_JBC_BUS;
  append_system_telemetry(resp.payload, o);
  resp.payload[o++] = (uint8_t)ofe_status_leds.busEvent();
  resp.payload[o++] = (uint8_t)ofe_status_leds.moduleEvent();
  resp.len = o;
  bus.send(resp);
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
static void handle_rs485(const Frame& req) {
  if (req.dst != module_addr && req.dst != ADDR_BROADCAST) return;
  if (req.dst == ADDR_BROADCAST && req.cmd == CMD_LED_SYNC) {
    handle_led_sync(req);
    return;
  }
  if (req.src == ADDR_MASTER) last_master_ms = millis();
  if (req.dst == ADDR_BROADCAST) {
    switch (req.cmd) {
      case CMD_DISCOVER_MODULES:
        rs485_discover(req);
        break;
      case CMD_SET_ADDRESS_UID:
        rs485_set_address_uid(req);
        break;
      default:
        break;
    }
    return;
  }

  switch (req.cmd) {
    case CMD_PING:
      rs485_status_response(req, STATUS_OK);
      break;
    case CMD_INFO:
      rs485_info(req);
      break;
    case CMD_GET_CAPS:
      rs485_caps(req);
      break;
    case CMD_FAST_POLL:
      rs485_fast_poll(req);
      break;
    case CMD_GET_STATE:
      rs485_get_state(req);
      break;
    case CMD_SET_STATE:
      rs485_set_state(req);
      break;
    case CMD_SET_LABEL:
      rs485_set_label(req);
      break;
    case CMD_GET_TELEMETRY:
      rs485_get_telemetry(req);
      break;
    case CMD_TRACE_CONTROL:
      rs485_trace_control(req);
      break;
    case CMD_TRACE_READ:
      rs485_trace_read(req);
      break;
    case CMD_SET_ADDRESS:
      if (req.len != 1 || !valid_module_addr(req.payload[0])) {
        rs485_status_response(req, STATUS_BAD_VALUE);
        break;
      }
      {
        const uint8_t next_addr = req.payload[0];
        prefs.putUChar("addr", next_addr);
        rs485_status_response(req, STATUS_OK);
        delay(20);
        module_addr = next_addr;
      }
      break;
    case CMD_FW_BEGIN:
      rs485_fw_begin(req);
      break;
    case CMD_FW_CHUNK:
      rs485_fw_chunk(req);
      break;
    case CMD_FW_END:
      rs485_fw_end(req);
      break;
    case CMD_FW_STATUS:
      rs485_fw_status(req);
      break;

    case CMD_FW_ABORT:
      rs485_fw_abort(req);
      break;

    case CMD_FW_REBOOT:
      rs485_status_response(req, STATUS_OK);
      delay(150);
      ESP.restart();
      break;
    default:
      rs485_status_response(req, STATUS_UNKNOWN_CMD);
      break;
  }
}

static void poll_rs485() {
  Frame req;
  uint8_t frames = 0;
  while (frames < 8 && bus.poll(req)) {
    handle_rs485(req);
    ++frames;
  }
  if (frames >= 8) yield();
}

void setup() {
  ofe_keep_module_fw_signature();
  ofe_status_leds.begin();
  bus.setActivityCallback([]() { ofe_status_leds.pulseBusActivity(); });
  Serial.begin(115200);
  delay(300);

  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  JBC.begin(250000, SERIAL_8E1, JBC_RX_PIN, JBC_TX_PIN);

  prefs.begin("jbc-bus", false);
  module_addr = prefs.getUChar("addr", 0x10);
  if (!valid_module_addr(module_addr)) {
    module_addr = 0x10;
    prefs.putUChar("addr", module_addr);
  }
  prefs.getString("label", module_label, sizeof(module_label));
  my_addr = prefs.getUChar("jbc_addr", 0x91);
  if (my_addr != 0x91 && !is_fae_a_addr(my_addr)) {
    my_addr = 0x91;
    prefs.putUChar("jbc_addr", my_addr);
  }
  cleanup_legacy_runtime_prefs();

  size_t n = prefs.getBytesLength("devid");
  if (n > 0 && n <= sizeof(device_id)) {
    device_id_len = prefs.getBytes("devid", device_id, sizeof(device_id));
  } else {
    memcpy(device_id, DEFAULT_DEVICE_ID, 32);
    device_id_len = 32;
  }

  Serial.println("JBC Bus RS485 module");
  Serial.print("rs485 addr=0x");
  Serial.println(module_addr, HEX);
  Serial.print("jbc addr=0x");
  Serial.println(my_addr, HEX);
  join_announce_left = 8;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
}

void loop() {
  const uint32_t led_now = millis();
  const bool bus_online = last_master_ms && (uint32_t)(led_now - last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS;
  const bool jbc_online = (fast_flags & FAST_FLAG_CONNECTED) && last_jbc_frame_ms && (uint32_t)(led_now - last_jbc_frame_ms) <= 2500UL;
  ofe_status_leds.setBusOnline(bus_online);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  ofe_status_leds.setModuleEvent(stat_error ? (((stat_error & (uint16_t)~0x0002U) != 0) ? OFE_LED_EVENT_CRITICAL : OFE_LED_EVENT_WARNING) : (work_mask ? OFE_LED_EVENT_WORK_ACTIVE : (jbc_online ? OFE_LED_EVENT_DEVICE_ONLINE : OFE_LED_EVENT_WARNING)));
  ofe_status_leds.tick();
  const uint32_t loop_start_us = micros();
  // Keep the OFE master-facing bus responsive even when the local JBC parser
  // has a busy burst. Service RS485 before and after local-device work, just
  // like the JBC USB module does.
  poll_rs485();
  poll_jbc();
  poll_rs485();
  poll_pending_discover_response();
  poll_join_announce();
  fw_update_check_timeout();
  record_loop_time((uint32_t)(micros() - loop_start_us));
  // Prevent the Arduino loop task from busy-spinning on one CPU core.
  // Placed after runtime measurement so loop_max_ms reports only real work.
  delay(1);
}







