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

#ifndef OUTPUT_ENABLE_PIN
#define OUTPUT_ENABLE_PIN 18
#endif

#ifndef FAN_PWM_PIN
#define FAN_PWM_PIN 19
#endif

#ifndef FAN_TACHO_PIN
#define FAN_TACHO_PIN 21
#endif

#ifndef OUTPUT_ACTIVE_HIGH
#define OUTPUT_ACTIVE_HIGH 1
#endif

#ifndef FAN_PWM_ENABLE
#define FAN_PWM_ENABLE 1
#endif

#ifndef FAN_TACHO_ENABLE
#define FAN_TACHO_ENABLE 1
#endif

#ifndef FAN_TACHO_PULSES_PER_REV
#define FAN_TACHO_PULSES_PER_REV 2
#endif

#ifndef FAN_TACHO_STALL_TIMEOUT_MS
#define FAN_TACHO_STALL_TIMEOUT_MS 2500UL
#endif

#ifndef FAN_TACHO_FAULT_MIN_POWER
#define FAN_TACHO_FAULT_MIN_POWER 100
#endif

#ifndef FAN_LOW_RPM_FAULT_MIN_RPM
#define FAN_LOW_RPM_FAULT_MIN_RPM 0
#endif

#ifndef OUTPUT_FAILSAFE_TIMEOUT_MS
#define OUTPUT_FAILSAFE_TIMEOUT_MS 8000UL
#endif

#ifndef GENERIC_IN1_PIN
#define GENERIC_IN1_PIN 32
#endif

#ifndef GENERIC_IN2_PIN
#define GENERIC_IN2_PIN 33
#endif

#ifndef GENERIC_OUT1_PIN
#define GENERIC_OUT1_PIN 22
#endif

#ifndef GENERIC_OUT2_PIN
#define GENERIC_OUT2_PIN 23
#endif

#ifndef GENERIC_IO_ENABLE
#define GENERIC_IO_ENABLE 1
#endif

static const uint16_t HW_VERSION = 0x0100;
#ifndef OFE_STR_HELPER
#define OFE_STR_HELPER(x) #x
#define OFE_STR(x) OFE_STR_HELPER(x)
#endif

#define OFE_MODULE_FW_MAJOR 1
#define OFE_MODULE_FW_MINOR 1
#define OFE_MODULE_FW_PATCH 40
#define OFE_MODULE_FW_SUFFIX "beta"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=FAN_IO_PRO;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}
static const uint16_t FAULT_NO_TACH = 0x0100;
static const uint16_t FAULT_MASTER_TIMEOUT = 0x0200;
static const uint16_t FAULT_LOW_RPM = 0x0400;
static const uint16_t FAULT_FILTER_WARN = 0x0002;
static const uint16_t FAULT_FILTER_FULL = 0x0004;
static const uint16_t FAULT_FILTER_MISSING = 0x0008;
static const uint16_t FAULT_SENSOR = 0x0010;
static const uint8_t FILTER_FLAG_SENSOR_ENABLED = 0x01;
static const uint8_t FILTER_FLAG_SENSOR_OK = 0x02;
static const uint8_t FILTER_FLAG_ZERO_LEARNED = 0x04;
static const uint8_t FILTER_FLAG_CLEAN_LEARNED = 0x08;
static const uint8_t FILTER_FLAG_READY = 0x10;
static const uint8_t FILTER_FLAG_PRESENT = 0x20;
static const uint16_t FILTER_SENSOR_FAULT_DEBOUNCE_MS = 3000;

static HardwareSerial RS485(1);
static Link bus(RS485);
static Preferences prefs;
static OfeStatusLed ofe_status_leds;

static uint8_t module_addr = 0x20;
static char module_label[24] = {0};
static char io_alias[5][19] = {{0}};
static bool fw_update_active = false;
static uint32_t fw_update_last_ms = 0;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;

static void fw_update_abort_local();
static void fw_update_touch();
static void fw_update_check_timeout();
static void sample_cpu_load();
static uint64_t module_uid();
static bool valid_module_addr(uint8_t addr);
static uint16_t sanitize_output_power(uint16_t power, bool allow_zero);
static void remember_output_power(uint16_t power);
static void write_enable_pin(bool enabled);
static void apply_output();
static int16_t read_filter_pressure_raw();
static void update_filter_sensor();
static void load_filter_calibration();
static void save_filter_calibration();
static void handle_pro_calibration(const Frame& req);
static void apply_generic_outputs();
static void generic_outputs_off();
static void output_off();
static void check_output_failsafe();
static void update_rpm();
static void update_generic_inputs();
static void send_status_response(const Frame& req, Status status);
static void handle_fw_begin(const Frame& req);
static void handle_fw_chunk(const Frame& req);
static void handle_fw_end(const Frame& req);
static void handle_fw_status(const Frame& req);
static void handle_fw_abort(const Frame& req);
static void copy_label_from_payload(const Frame& req);
static void handle_set_label(const Frame& req);
static void handle_info(const Frame& req);
static void handle_caps(const Frame& req);
static void handle_status(const Frame& req);
static uint32_t discover_delay_ms(const Frame& req);
static void send_discover_response(uint8_t dst, uint8_t seq);
static void handle_discover(const Frame& req);
static void poll_pending_discover_response();
static uint32_t join_delay_ms(uint8_t round);
static void send_join_announce();
static void poll_join_announce();
static void handle_set_address_uid(const Frame& req);
static void handle_get_io(const Frame& req);
static void handle_set_io(const Frame& req);
static void handle_io_label(const Frame& req);
static void record_loop_time(uint32_t busy_us);
static void append_system_telemetry(uint8_t* payload, uint8_t& o);
static void handle_telemetry(const Frame& req);
static void handle_frame(const Frame& req);
static void poll_rs485();
void setup();
void loop();
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
static const uint16_t DEFAULT_OUTPUT_POWER = 100;
static bool output_enabled = false;
static uint16_t output_power = DEFAULT_OUTPUT_POWER;
static uint16_t manual_output_power = DEFAULT_OUTPUT_POWER;
static bool manual_output_power_dirty = false;
static uint32_t manual_output_power_save_due_ms = 0;
static uint16_t fan_rpm = 0;
static uint16_t fault_mask = 0;
static volatile uint32_t tacho_edges = 0;
static uint32_t last_rpm_ms = 0;
static uint32_t last_tacho_edges = 0;
static uint32_t fan_demand_since_ms = 0;
static bool fan_demand_active = false;
static uint32_t last_master_ms = 0;
static uint16_t io_input_mask = 0;
static uint16_t io_output_mask = 0;
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

static int16_t filter_zero_raw = 0;
static int16_t filter_clean_raw = 0;
static int16_t filter_warn_raw = 350;
static int16_t filter_full_raw = 500;
static int16_t filter_pressure_raw = 0;
static uint16_t filter_saturation_permille = 0;
static bool filter_present = true;
static bool filter_sensor_enabled = false;
static bool pressure_sensor_ok = false;
static uint32_t pressure_sensor_fault_since_ms = 0;
static uint32_t last_filter_sample_ms = 0;

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

static uint64_t module_uid() {
  return 0x2000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}
static bool valid_module_addr(uint8_t addr) {
  return addr >= 0x20 && addr <= 0x2F;
}

static uint16_t sanitize_output_power(uint16_t power, bool allow_zero) {
  if (allow_zero && power == 0) return 0;
  if (power > 1000) power = 1000;
  if (power < 100) power = 100;
  return power;
}

static void remember_output_power(uint16_t power) {
  power = sanitize_output_power(power, false);
  if (manual_output_power == power) return;
  manual_output_power = power;
  manual_output_power_dirty = true;
  manual_output_power_save_due_ms = millis() + 30000UL;
}

static void poll_output_power_save() {
  if (!manual_output_power_dirty) return;
  if ((int32_t)(millis() - manual_output_power_save_due_ms) < 0) return;
  prefs.putUShort("power", manual_output_power);
  manual_output_power_dirty = false;
}


static void IRAM_ATTR tacho_isr() {
  ++tacho_edges;
}

static void write_enable_pin(bool enabled) {
  const bool level = OUTPUT_ACTIVE_HIGH ? enabled : !enabled;
  digitalWrite(OUTPUT_ENABLE_PIN, level ? HIGH : LOW);
}

static void apply_output() {
  const bool demand = output_enabled && output_power >= FAN_TACHO_FAULT_MIN_POWER;
  if (demand && !fan_demand_active) {
    fan_demand_since_ms = millis();
    fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
  } else if (!demand) {
    fan_demand_since_ms = 0;
    fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
  }
  fan_demand_active = demand;

  write_enable_pin(output_enabled);
#if FAN_PWM_ENABLE
  const int duty = output_enabled ? map(output_power, 0, 1000, 0, 255) : 0;
  analogWrite(FAN_PWM_PIN, duty);
#endif
}

static int16_t read_filter_pressure_raw() {
  // TODO: attach a concrete I2C pressure sensor driver here, e.g. Sensirion SDP3x/SDP8xx.
  pressure_sensor_ok = false;
  return 0;
}

static uint8_t filter_calibration_quality() {
  if (!filter_sensor_enabled) return 0;
  if (filter_clean_raw <= 0) return 1;
  if (filter_warn_raw <= filter_clean_raw) return 1;
  if (filter_full_raw <= filter_warn_raw) return 1;
  return 2;
}

static uint8_t filter_status_flags() {
  uint8_t flags = 0;
  if (filter_sensor_enabled) flags |= FILTER_FLAG_SENSOR_ENABLED;
  if (pressure_sensor_ok) flags |= FILTER_FLAG_SENSOR_OK;
  if (filter_zero_raw != 0 || filter_clean_raw != 0) flags |= FILTER_FLAG_ZERO_LEARNED;
  if (filter_clean_raw > 0) flags |= FILTER_FLAG_CLEAN_LEARNED;
  if (filter_calibration_quality() >= 2) flags |= FILTER_FLAG_READY;
  if (filter_present) flags |= FILTER_FLAG_PRESENT;
  return flags;
}

static void clear_filter_faults() {
  fault_mask &= (uint16_t)~(FAULT_FILTER_WARN | FAULT_FILTER_FULL | FAULT_FILTER_MISSING | FAULT_SENSOR);
}

static void update_filter_sensor() {
  if (!filter_sensor_enabled) {
    pressure_sensor_ok = false;
    pressure_sensor_fault_since_ms = 0;
    filter_pressure_raw = 0;
    filter_saturation_permille = 0;
    clear_filter_faults();
    return;
  }

  filter_pressure_raw = read_filter_pressure_raw();
  if (!filter_present) fault_mask |= FAULT_FILTER_MISSING;
  else fault_mask &= (uint16_t)~FAULT_FILTER_MISSING;

  if (!pressure_sensor_ok) {
    if (!pressure_sensor_fault_since_ms) pressure_sensor_fault_since_ms = millis();
    if ((uint32_t)(millis() - pressure_sensor_fault_since_ms) >= FILTER_SENSOR_FAULT_DEBOUNCE_MS) fault_mask |= FAULT_SENSOR;
    fault_mask &= (uint16_t)~(FAULT_FILTER_WARN | FAULT_FILTER_FULL);
    filter_saturation_permille = 0;
    return;
  }

  pressure_sensor_fault_since_ms = 0;
  fault_mask &= (uint16_t)~FAULT_SENSOR;
  int32_t delta = (int32_t)filter_pressure_raw - filter_zero_raw;
  if (delta < 0) delta = 0;
  int32_t span = (int32_t)filter_full_raw - filter_clean_raw;
  if (span < 1) span = 1;
  int32_t sat = ((delta - filter_clean_raw) * 1000L) / span;
  if (sat < 0) sat = 0;
  if (sat > 1000) sat = 1000;
  filter_saturation_permille = (uint16_t)sat;
  if (filter_calibration_quality() < 2) {
    fault_mask &= (uint16_t)~(FAULT_FILTER_WARN | FAULT_FILTER_FULL);
    return;
  }
  if (delta >= filter_full_raw) fault_mask |= FAULT_FILTER_FULL;
  else fault_mask &= (uint16_t)~FAULT_FILTER_FULL;
  if (delta >= filter_warn_raw) fault_mask |= FAULT_FILTER_WARN;
  else fault_mask &= (uint16_t)~FAULT_FILTER_WARN;
}
static void apply_generic_outputs() {
#if GENERIC_IO_ENABLE
  digitalWrite(GENERIC_OUT1_PIN, (io_output_mask & 0x0001) ? HIGH : LOW);
  digitalWrite(GENERIC_OUT2_PIN, (io_output_mask & 0x0002) ? HIGH : LOW);
#endif
}

static void generic_outputs_off() {
  if (!io_output_mask) return;
  io_output_mask = 0;
  apply_generic_outputs();
}

static void output_off() {
  if (!output_enabled && !io_output_mask) return;
  output_enabled = false;
  apply_output();
  generic_outputs_off();
}

static void check_output_failsafe() {
  if (!last_master_ms || (!output_enabled && !io_output_mask)) return;
  if ((uint32_t)(millis() - last_master_ms) <= OUTPUT_FAILSAFE_TIMEOUT_MS) return;
  fault_mask |= FAULT_MASTER_TIMEOUT;
  output_off();
}

static void update_rpm() {
#if FAN_TACHO_ENABLE
  const uint32_t now = millis();
  if ((uint32_t)(now - last_rpm_ms) < 1000UL) return;
  const uint32_t edges = tacho_edges;
  const uint32_t delta = edges - last_tacho_edges;
  last_tacho_edges = edges;
  last_rpm_ms = now;

  uint32_t rpm = 0;
  if (FAN_TACHO_PULSES_PER_REV > 0) {
    rpm = (delta * 60UL) / FAN_TACHO_PULSES_PER_REV;
  }
  if (rpm > 65535UL) rpm = 65535UL;
  fan_rpm = (uint16_t)rpm;

  if (!fan_demand_active) {
    fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
  } else if (fan_rpm > 0) {
    fault_mask &= (uint16_t)~FAULT_NO_TACH;
#if FAN_LOW_RPM_FAULT_MIN_RPM > 0
    if (fan_demand_since_ms && (uint32_t)(now - fan_demand_since_ms) >= FAN_TACHO_STALL_TIMEOUT_MS && fan_rpm < FAN_LOW_RPM_FAULT_MIN_RPM) {
      fault_mask |= FAULT_LOW_RPM;
    } else {
      fault_mask &= (uint16_t)~FAULT_LOW_RPM;
    }
#else
    fault_mask &= (uint16_t)~FAULT_LOW_RPM;
#endif
  } else if (fan_demand_since_ms && (uint32_t)(now - fan_demand_since_ms) >= FAN_TACHO_STALL_TIMEOUT_MS) {
    fault_mask |= FAULT_NO_TACH;
    fault_mask &= (uint16_t)~FAULT_LOW_RPM;
  }
#else
  fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
#endif
}

static void update_generic_inputs() {
#if GENERIC_IO_ENABLE
  uint16_t mask = 0;
  if (digitalRead(GENERIC_IN1_PIN) == LOW) mask |= 0x0001;
  if (digitalRead(GENERIC_IN2_PIN) == LOW) mask |= 0x0002;
  io_input_mask = mask;
#endif
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

static void handle_fw_begin(const Frame& req) {
  if (req.len < 4) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  output_off();
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
  fw_update_touch();
  fw_update_offset = 0;
  fw_update_buffer_reset();
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

static void handle_fw_abort(const Frame& req) {
  fw_update_abort_local();
  send_status_response(req, STATUS_OK);
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

static void handle_info(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_INFO | 0x80;

  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_FAN_IO_PRO;
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
  const char name[] = "Fan/IO Pro";
  const char* shown_name = module_label[0] ? module_label : name;
  while (*shown_name && o < MAX_PAYLOAD) resp.payload[o++] = (uint8_t)*shown_name++;
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
  uint32_t caps = CAP_RELAY_OUTPUT | CAP_FAULT_REPORT | CAP_FW_UPDATE | CAP_FILTER_SENSOR | CAP_PRESSURE_SENSOR | CAP_ANALOG_INPUT;
#if GENERIC_IO_ENABLE
  caps |= CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT;
#endif
#if FAN_PWM_ENABLE
  caps |= CAP_PWM_OUTPUT;
#endif
#if FAN_TACHO_ENABLE
  caps |= CAP_TACHO_INPUT;
#endif
  put_u32_le(resp.payload + 1, caps);
  bus.send(resp);
}

static void handle_status(const Frame& req) {
  update_filter_sensor();
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_STATUS | 0x80;
  resp.len = 12;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = output_enabled ? 1 : 0;
  put_u16_le(resp.payload + 2, output_power);
  put_u16_le(resp.payload + 4, fan_rpm);
  put_u16_le(resp.payload + 6, fault_mask);
  put_u16_le(resp.payload + 8, filter_saturation_permille);
  put_u16_le(resp.payload + 10, (uint16_t)filter_pressure_raw);
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
  resp.payload[o++] = MODULE_FAN_IO_PRO;
  put_u64_le(resp.payload + o, uid); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  uint32_t caps = CAP_RELAY_OUTPUT | CAP_FAULT_REPORT | CAP_FW_UPDATE | CAP_FILTER_SENSOR | CAP_PRESSURE_SENSOR | CAP_ANALOG_INPUT;
#if GENERIC_IO_ENABLE
  caps |= CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT;
#endif
#if FAN_PWM_ENABLE
  caps |= CAP_PWM_OUTPUT;
#endif
#if FAN_TACHO_ENABLE
  caps |= CAP_TACHO_INPUT;
#endif
  put_u32_le(resp.payload + o, caps); o += 4;
  resp.len = (uint8_t)o;
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
  Frame resp;
  resp.dst = ADDR_MASTER;
  resp.src = module_addr;
  resp.seq = 0;
  resp.cmd = CMD_DISCOVER_MODULES | 0x80;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_FAN_IO_PRO;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  uint32_t caps = CAP_RELAY_OUTPUT | CAP_FAULT_REPORT | CAP_FW_UPDATE | CAP_FILTER_SENSOR | CAP_PRESSURE_SENSOR | CAP_ANALOG_INPUT;
#if GENERIC_IO_ENABLE
  caps |= CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT;
#endif
#if FAN_PWM_ENABLE
  caps |= CAP_PWM_OUTPUT;
#endif
#if FAN_TACHO_ENABLE
  caps |= CAP_TACHO_INPUT;
#endif
  put_u32_le(resp.payload + o, caps); o += 4;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static void poll_join_announce() {
  if (!join_announce_left) return;
  if ((int32_t)(millis() - next_join_announce_ms) < 0) return;
  send_join_announce();
  --join_announce_left;
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
  prefs.putUChar("addr", next_addr);
  send_status_response(req, STATUS_OK);
  delay(20);
  module_addr = next_addr;
}

static const char* io_alias_key(uint8_t ch) {
  switch (ch) {
    case 0: return "ia0";
    case 1: return "ia1";
    case 2: return "oa0";
    case 3: return "oa1";
    case 4: return "ma0";
    default: return "iax";
  }
}

static void clean_io_alias(char* dst, size_t dst_len, const uint8_t* src, uint8_t len) {
  if (!dst || dst_len == 0) return;
  size_t o = 0;
  for (uint8_t i = 0; i < len && o + 1 < dst_len; ++i) {
    char c = (char)src[i];
    if (c < 32 || c == 127) continue;
    if (c == '"' || c == '\'' || c == '<' || c == '>') continue;
    dst[o++] = c;
  }
  while (o > 0 && dst[o - 1] == ' ') --o;
  dst[o] = 0;
}

static uint8_t io_alias_len(uint8_t ch) {
  uint8_t n = 0;
  while (n < 18 && io_alias[ch][n]) ++n;
  return n;
}

static void append_io_aliases(uint8_t* payload, uint8_t& o) {
  for (uint8_t ch = 0; ch < 5; ++ch) {
    const uint8_t n = io_alias_len(ch);
    payload[o++] = n;
    if (n) {
      memcpy(payload + o, io_alias[ch], n);
      o += n;
    }
  }
}

static void handle_io_label(const Frame& req) {
  if (req.len < 1) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint8_t ch = req.payload[0];
  if (ch >= 5) {
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  clean_io_alias(io_alias[ch], sizeof(io_alias[ch]), req.payload + 1, req.len - 1);
  bool ok = true;
  if (io_alias[ch][0]) ok = prefs.putString(io_alias_key(ch), io_alias[ch]) > 0;
  else prefs.remove(io_alias_key(ch));
  send_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
}

static void handle_get_io(const Frame& req) {
  update_generic_inputs();
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_IO | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  put_u16_le(resp.payload + o, io_input_mask); o += 2;
  put_u16_le(resp.payload + o, io_output_mask); o += 2;
  put_u16_le(resp.payload + o, fault_mask); o += 2;

  // Backward compatibility:
  // - old Master sends no request payload -> include aliases exactly as before
  // - new live poll sends flags=0 -> only the 7-byte live IO payload
  // - scan/config sends IO_QUERY_INCLUDE_ALIASES -> include all aliases
  const bool include_aliases =
    req.len == 0 || (req.payload[0] & IO_QUERY_INCLUDE_ALIASES) != 0;
  if (include_aliases) append_io_aliases(resp.payload, o);

  resp.len = o;
  bus.send(resp);
}

static void handle_set_io(const Frame& req) {
  if (req.len != 4) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint16_t mask = get_u16_le(req.payload);
  const uint16_t value = get_u16_le(req.payload + 2);
  io_output_mask = (io_output_mask & ~mask) | (value & mask);
  io_output_mask &= 0x0003;
  apply_generic_outputs();
  send_status_response(req, STATUS_OK);
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

static void load_filter_calibration() {
  filter_sensor_enabled = prefs.getBool("f_enabled", false);
  filter_zero_raw = prefs.getShort("f_zero", 0);
  filter_clean_raw = prefs.getShort("f_clean", 0);
  filter_warn_raw = prefs.getShort("f_warn", 350);
  filter_full_raw = prefs.getShort("f_full", 500);
  filter_present = prefs.getBool("f_present", true);
  if (filter_warn_raw <= filter_clean_raw) filter_warn_raw = filter_clean_raw + 250;
  if (filter_full_raw <= filter_warn_raw) filter_full_raw = filter_warn_raw + 150;
}

static void save_filter_calibration() {
  prefs.putBool("f_enabled", filter_sensor_enabled);
  prefs.putShort("f_zero", filter_zero_raw);
  prefs.putShort("f_clean", filter_clean_raw);
  prefs.putShort("f_warn", filter_warn_raw);
  prefs.putShort("f_full", filter_full_raw);
  prefs.putBool("f_present", filter_present);
}

static void handle_pro_calibration(const Frame& req) {
  if (req.len < 1) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint8_t action = req.payload[0];
  if (action == 5) {
    if (req.len != 2) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    filter_sensor_enabled = req.payload[1] != 0;
    if (!filter_sensor_enabled) clear_filter_faults();
    save_filter_calibration();
    send_status_response(req, STATUS_OK);
    return;
  }
  if (action == 6) {
    if (req.len != 2) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    filter_present = req.payload[1] != 0;
    save_filter_calibration();
    send_status_response(req, STATUS_OK);
    return;
  }

  update_filter_sensor();
  switch (action) {
    case 1:
      if (!filter_sensor_enabled || !pressure_sensor_ok) {
        send_status_response(req, STATUS_NOT_SUPPORTED);
        return;
      }
      filter_zero_raw = filter_pressure_raw;
      save_filter_calibration();
      send_status_response(req, STATUS_OK);
      break;
    case 2:
      if (!filter_sensor_enabled || !pressure_sensor_ok) {
        send_status_response(req, STATUS_NOT_SUPPORTED);
        return;
      }
      filter_clean_raw = filter_pressure_raw - filter_zero_raw;
      if (filter_clean_raw < 0) filter_clean_raw = 0;
      if (filter_warn_raw <= filter_clean_raw) filter_warn_raw = filter_clean_raw + 250;
      if (filter_full_raw <= filter_warn_raw) filter_full_raw = filter_warn_raw + 150;
      save_filter_calibration();
      send_status_response(req, STATUS_OK);
      break;
    case 3:
      if (req.len != 5) {
        send_status_response(req, STATUS_BAD_LEN);
        return;
      }
      filter_warn_raw = (int16_t)get_u16_le(req.payload + 1);
      filter_full_raw = (int16_t)get_u16_le(req.payload + 3);
      if (filter_warn_raw <= filter_clean_raw) filter_warn_raw = filter_clean_raw + 1;
      if (filter_full_raw <= filter_warn_raw) filter_full_raw = filter_warn_raw + 1;
      save_filter_calibration();
      send_status_response(req, STATUS_OK);
      break;
    case 4:
      filter_zero_raw = 0;
      filter_clean_raw = 0;
      filter_warn_raw = 350;
      filter_full_raw = 500;
      filter_sensor_enabled = false;
      filter_present = true;
      clear_filter_faults();
      save_filter_calibration();
      send_status_response(req, STATUS_OK);
      break;
    default:
      send_status_response(req, STATUS_BAD_VALUE);
      break;
  }
}
static void handle_telemetry(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_TELEMETRY | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_FAN_IO_PRO;
  append_system_telemetry(resp.payload, o);
  update_filter_sensor();
  put_u16_le(resp.payload + o, filter_saturation_permille); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)filter_pressure_raw); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)filter_zero_raw); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)filter_clean_raw); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)filter_warn_raw); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)filter_full_raw); o += 2;
  resp.payload[o++] = filter_status_flags();
  resp.payload[o++] = filter_calibration_quality();
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
static void handle_frame(const Frame& req) {
  if (req.dst != module_addr && req.dst != ADDR_BROADCAST) return;
  if (req.dst == ADDR_BROADCAST && req.cmd == CMD_LED_SYNC) {
    handle_led_sync(req);
    return;
  }
  if (req.dst == ADDR_BROADCAST) {
    switch (req.cmd) {
      case CMD_DISCOVER_MODULES:
        handle_discover(req);
        break;
      case CMD_SET_ADDRESS_UID:
        handle_set_address_uid(req);
        break;
      default:
        break;
    }
    return;
  }

  if (req.src == ADDR_MASTER) {
    last_master_ms = millis();
    fault_mask &= (uint16_t)~FAULT_MASTER_TIMEOUT;
  }

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

    case CMD_GET_IO:
      handle_get_io(req);
      break;

    case CMD_GET_TELEMETRY:
      handle_telemetry(req);
      break;

    case CMD_SET_IO:
      handle_set_io(req);
      break;

    case CMD_IO_LABEL:
      handle_io_label(req);
      break;

    case CMD_SET_LABEL:
      handle_set_label(req);
      break;

    case CMD_FILTER_CALIBRATION:
    case CMD_SET_OUTPUT:
      handle_pro_calibration(req);
      break;

    case CMD_SET_ENABLE:
      if (req.len != 1) {
        send_status_response(req, STATUS_BAD_LEN);
        break;
      }
      output_enabled = req.payload[0] != 0;
      // Keep the last slider value visible while the relay/PWM output is off.
      // Only the enable flag decides whether the fan actually runs.
      if (output_power < 100) output_power = manual_output_power;
      apply_output();
      send_status_response(req, STATUS_OK);
      break;

    case CMD_SET_POWER:
      if (req.len != 2) {
        send_status_response(req, STATUS_BAD_LEN);
        break;
      }
      {
        const uint16_t requested_power = get_u16_le(req.payload);
        if (requested_power == 0) {
          // Zero is an off/idle sync value from older masters, never a new
          // manual 10% setting. Do not overwrite the remembered slider value.
          output_enabled = false;
          output_power = manual_output_power;
        } else {
          output_power = sanitize_output_power(requested_power, false);
          remember_output_power(output_power);
        }
        if (output_power < 100) output_power = manual_output_power;
      }
      apply_output();
      send_status_response(req, STATUS_OK);
      break;

    case CMD_SET_ADDRESS:
      if (req.len != 1 || !valid_module_addr(req.payload[0])) {
        send_status_response(req, STATUS_BAD_VALUE);
        break;
      }
      {
        const uint8_t next_addr = req.payload[0];
        prefs.putUChar("addr", next_addr);
        send_status_response(req, STATUS_OK);
        delay(20);
        module_addr = next_addr;
      }
      break;

    case CMD_FACTORY_RESET:
      prefs.clear();
      module_addr = 0x20;
      module_label[0] = 0;
      output_off();
      send_status_response(req, STATUS_OK);
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
      handle_fw_abort(req);
      break;

    case CMD_FW_REBOOT:
      send_status_response(req, STATUS_OK);
      delay(150);
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
  Serial.begin(115200);
  delay(300);

  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  prefs.begin("fan-output", false);
  manual_output_power = prefs.getUShort("power", DEFAULT_OUTPUT_POWER);
  manual_output_power = sanitize_output_power(manual_output_power, false);
  output_power = manual_output_power;
  module_addr = prefs.getUChar("addr", 0x20);
  if (!valid_module_addr(module_addr)) {
    module_addr = 0x20;
    prefs.putUChar("addr", module_addr);
  }
  prefs.getString("label", module_label, sizeof(module_label));
  for (uint8_t ch = 0; ch < 5; ++ch) prefs.getString(io_alias_key(ch), io_alias[ch], sizeof(io_alias[ch]));
  if (!io_alias[4][0] && io_alias[2][0]) {
    strncpy(io_alias[4], io_alias[2], sizeof(io_alias[4]) - 1);
    io_alias[4][sizeof(io_alias[4]) - 1] = 0;
    io_alias[2][0] = 0;
    prefs.putString(io_alias_key(4), io_alias[4]);
    prefs.remove(io_alias_key(2));
  }
  load_filter_calibration();

  pinMode(OUTPUT_ENABLE_PIN, OUTPUT);
  write_enable_pin(false);

#if GENERIC_IO_ENABLE
  pinMode(GENERIC_IN1_PIN, INPUT_PULLUP);
  pinMode(GENERIC_IN2_PIN, INPUT_PULLUP);
  pinMode(GENERIC_OUT1_PIN, OUTPUT);
  pinMode(GENERIC_OUT2_PIN, OUTPUT);
  apply_generic_outputs();
#endif

#if FAN_PWM_ENABLE
  pinMode(FAN_PWM_PIN, OUTPUT);
  analogWrite(FAN_PWM_PIN, 0);
#endif

#if FAN_TACHO_ENABLE
  pinMode(FAN_TACHO_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), tacho_isr, FALLING);
#endif

  Serial.println("Fan/IO Pro RS485 module");
  Serial.print("addr=0x");
  if (module_addr < 0x10) Serial.print('0');
  Serial.println(module_addr, HEX);
  join_announce_left = 8;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
}

void loop() {
  ofe_status_leds.setBusOnline(last_master_ms && (uint32_t)(millis() - last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  ofe_status_leds.setModuleEvent(fault_mask ? ((fault_mask == FAULT_FILTER_WARN) ? OFE_LED_EVENT_WARNING : OFE_LED_EVENT_CRITICAL) : (output_enabled ? OFE_LED_EVENT_EXTRACTOR_ON : OFE_LED_EVENT_OFF));
  ofe_status_leds.tick();
  const uint32_t loop_start_us = micros();
  update_rpm();
  update_generic_inputs();
  if ((uint32_t)(millis() - last_filter_sample_ms) >= 500UL) {
    last_filter_sample_ms = millis();
    update_filter_sensor();
  }
  poll_rs485();
  poll_pending_discover_response();
  poll_join_announce();
  fw_update_check_timeout();
  check_output_failsafe();
  poll_output_power_save();
  record_loop_time((uint32_t)(micros() - loop_start_us));
  // Prevent the Arduino loop task from busy-spinning on one CPU core.
  // Placed after runtime measurement so loop_max_ms reports only real work.
  delay(1);
}









