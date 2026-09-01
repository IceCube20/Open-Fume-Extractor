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

#ifndef WELLER_RX_PIN
#define WELLER_RX_PIN 17
#endif

#ifndef WELLER_TX_PIN
#define WELLER_TX_PIN 16
#endif

#ifndef WELLER_BAUD
#define WELLER_BAUD 1200
#endif

#ifndef OUTPUT_FAILSAFE_TIMEOUT_MS
#define OUTPUT_FAILSAFE_TIMEOUT_MS 8000UL
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
#define OFE_MODULE_FW_MINOR 1
#define OFE_MODULE_FW_PATCH 73
#define OFE_MODULE_FW_SUFFIX "beta"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=WELLER_ZERO_SMOG;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}

static HardwareSerial RS485(1);
static HardwareSerial WELLER(2);
static Link bus(RS485);
static Preferences prefs;
static OfeStatusLed ofe_status_leds;

static const uint8_t DEFAULT_MODULE_ADDR = 0x30;
static uint8_t module_addr = DEFAULT_MODULE_ADDR;
static char module_label[24] = {0};
static bool fw_update_active = false;
static uint32_t fw_update_last_ms = 0;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;

#line 60 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void fw_update_abort_local();
#line 65 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void fw_update_touch();
#line 69 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void fw_update_check_timeout();
#line 106 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void sample_cpu_load();
#line 152 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint64_t module_uid();
#line 155 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static bool valid_module_addr(uint8_t addr);
#line 160 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint8_t cs_bytes(const char* data, uint8_t len);
#line 166 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint8_t cs4(const char* cmd);
#line 170 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_queue_raw(const char* data, uint8_t len);
#line 183 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_clear_tx_queue();
#line 209 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void local_trace_clear();
#line 215 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void local_trace_log(uint8_t dir, uint8_t meta1, uint8_t meta2, const uint8_t* data, uint8_t len);
#line 229 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_trace_control(const Frame& req);
#line 240 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_trace_read(const Frame& req);
#line 274 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_weller_tx();
#line 285 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static bool weller_single_queued(char c);
#line 292 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_send_query(char c);
#line 297 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_send_checked4(char prefix, uint16_t value);
#line 308 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_send_speed(uint8_t percent);
#line 320 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_set_filter_runtime(uint16_t minutes);
#line 332 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_send_light(bool enabled);
#line 342 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_reset_filter_runtime();
#line 348 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint8_t power_to_percent(uint16_t power);
#line 356 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_send_fan_state(bool enabled);
#line 363 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void weller_apply_output();
#line 376 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_fan_keepalive();
#line 384 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void output_off();
#line 391 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void check_output_failsafe();
#line 397 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static bool is_digit(char c);
#line 401 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void publish_weller_frame(char typ, uint16_t val);
#line 433 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void consume_weller_rx(uint8_t used);
#line 442 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void publish_weller_light_ack(uint16_t val);
#line 449 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_weller_rx();
#line 503 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_weller_queries();
#line 534 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_fw_begin(const Frame& req);
#line 557 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_fw_chunk(const Frame& req);
#line 589 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_fw_end(const Frame& req);
#line 603 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_fw_status(const Frame& req);
#line 616 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_fw_abort(const Frame& req);
#line 621 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void copy_label_from_payload(const Frame& req);
#line 632 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_set_label(const Frame& req);
#line 639 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_info(const Frame& req);
#line 668 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint32_t module_caps();
#line 672 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_caps(const Frame& req);
#line 684 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_status(const Frame& req);
#line 702 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void record_loop_time(uint32_t busy_us);
#line 718 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void append_system_telemetry(uint8_t* payload, uint8_t& o);
#line 726 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_telemetry(const Frame& req);
#line 754 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_get_io(const Frame& req);
#line 768 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_set_io(const Frame& req);
#line 788 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_set_output(const Frame& req);
#line 827 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint32_t discover_delay_ms(const Frame& req);
#line 839 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void send_discover_response(uint8_t dst, uint8_t seq);
#line 859 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_discover(const Frame& req);
#line 867 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_pending_discover_response();
#line 878 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static uint32_t join_delay_ms(uint8_t round);
#line 885 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void send_join_announce();
#line 904 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_join_announce();
#line 911 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_set_address_uid(const Frame& req);
#line 925 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void handle_frame(const Frame& req);
#line 1041 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
static void poll_rs485();
#line 1046 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
void setup();
#line 1077 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
void loop();
#line 60 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\WellerZeroSmogModule\\WellerZeroSmogModule.ino"
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
static bool output_enabled = false;
static uint16_t output_power = 0;
static uint8_t target_speed_percent = 30;
static uint8_t manual_speed_percent = 30;
static bool manual_speed_dirty = false;
static uint32_t manual_speed_save_due_ms = 0;
static uint8_t reported_speed_percent = 0;
static uint16_t fan_rpm = 0;
static uint16_t filter_runtime_minutes = 0;
static uint16_t programmed_filter_minutes = 0;
static uint8_t filter_status_code = 0;
static uint16_t weller_version = 0;
static uint8_t work_light_state = 0;
static uint16_t io_output_mask = 0;
static uint16_t fault_mask = 0;
static uint32_t fan_on_since_ms = 0;
static uint32_t last_master_ms = 0;
static uint32_t last_weller_rx_ms = 0;
static uint32_t last_fast_poll_ms = 0;
static uint32_t last_medium_poll_ms = 0;
static uint32_t last_slow_poll_ms = 0;
static uint32_t last_weller_tx_ms = 0;
static uint32_t last_fan_command_ms = 0;
static uint32_t last_weller_recovery_ms = 0;
static uint32_t weller_probe_due_ms = 0;
static const uint32_t WELLER_RECONNECT_GRACE_MS = 12000UL;
static const uint32_t WELLER_RECONNECT_RETRY_MS = 8000UL;
static const uint32_t WELLER_LINE_IDLE_MS = 650UL;
static const uint32_t WELLER_PROBE_DELAY_MS = 900UL;
static const uint16_t WELLER_FAULT_BUS = 0x0001U;
static const uint16_t WELLER_FAULT_SPEED_FEEDBACK = 0x0100U;
static const uint32_t WELLER_RPM_GRACE_MS = 8000UL;
static uint32_t loop_window_ms = 0;
static uint32_t loop_max_us = 0;
static uint32_t last_cpu_sample_ms = 0;
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
static char rx_buf[80];
static uint8_t rx_len = 0;

static const uint8_t WELLER_TX_QUEUE_SIZE = 20;
static const uint8_t WELLER_TX_MAX_LEN = 6;
static const uint32_t WELLER_TX_GAP_MS = 150;

struct WellerTxPacket {
  char data[WELLER_TX_MAX_LEN];
  uint8_t len;
};

static WellerTxPacket weller_tx_queue[WELLER_TX_QUEUE_SIZE];
static uint8_t weller_tx_head = 0;
static uint8_t weller_tx_tail = 0;
static uint8_t weller_tx_count = 0;
static uint8_t join_announce_left = 0;
static uint32_t next_join_announce_ms = 0;
static bool discover_response_pending = false;
static uint8_t discover_response_dst = ADDR_MASTER;
static uint8_t discover_response_seq = 0;
static uint32_t discover_response_due_ms = 0;

static uint64_t module_uid() {
  return 0x3000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}
static bool valid_module_addr(uint8_t addr) {
  return addr >= 0x30 && addr <= 0x3F;
}


static uint8_t cs_bytes(const char* data, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len; ++i) sum = (uint8_t)(sum + (uint8_t)data[i]);
  return sum;
}

static uint8_t cs4(const char* cmd) {
  return cs_bytes(cmd, 4);
}

static void weller_queue_raw(const char* data, uint8_t len) {
  if (!len || len > WELLER_TX_MAX_LEN) return;
  if (weller_tx_count >= WELLER_TX_QUEUE_SIZE) {
    weller_tx_tail = (uint8_t)((weller_tx_tail + 1) % WELLER_TX_QUEUE_SIZE);
    --weller_tx_count;
  }
  WellerTxPacket& pkt = weller_tx_queue[weller_tx_head];
  memcpy(pkt.data, data, len);
  pkt.len = len;
  weller_tx_head = (uint8_t)((weller_tx_head + 1) % WELLER_TX_QUEUE_SIZE);
  ++weller_tx_count;
}

static void weller_clear_tx_queue() {
  weller_tx_head = 0;
  weller_tx_tail = 0;
  weller_tx_count = 0;
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
static void send_status_response(const Frame& req, Status status);

static void local_trace_clear() {
  local_trace_head = 0;
  local_trace_count = 0;
  local_trace_dropped = 0;
}

static void local_trace_log(uint8_t dir, uint8_t meta1, uint8_t meta2, const uint8_t* data, uint8_t len) {
  if (!local_trace_enabled) return;
  LocalTraceEvent& ev = local_trace[local_trace_head];
  ev.ms = millis();
  ev.dir = dir;
  ev.meta1 = meta1;
  ev.meta2 = meta2;
  ev.len = len > LOCAL_TRACE_PREVIEW ? LOCAL_TRACE_PREVIEW : len;
  if (ev.len && data) memcpy(ev.data, data, ev.len);
  local_trace_head = (uint8_t)((local_trace_head + 1) % LOCAL_TRACE_CAPACITY);
  if (local_trace_count < LOCAL_TRACE_CAPACITY) ++local_trace_count;
  else if (local_trace_dropped < 0xFFFF) ++local_trace_dropped;
}

static void handle_trace_control(const Frame& req) {
  if (req.len < 1) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint8_t flags = req.payload[0];
  local_trace_enabled = (flags & 0x01) != 0;
  if (flags & 0x02) local_trace_clear();
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
static void poll_weller_tx() {
  if (!weller_tx_count) return;
  if ((uint32_t)(millis() - last_weller_tx_ms) < WELLER_TX_GAP_MS) return;
  WellerTxPacket& pkt = weller_tx_queue[weller_tx_tail];
  WELLER.write((const uint8_t*)pkt.data, pkt.len);
  local_trace_log(2, pkt.len ? (uint8_t)pkt.data[0] : 0, 0, (const uint8_t*)pkt.data, pkt.len);
  last_weller_tx_ms = millis();
  weller_tx_tail = (uint8_t)((weller_tx_tail + 1) % WELLER_TX_QUEUE_SIZE);
  --weller_tx_count;
}

static bool weller_single_queued(char c) {
  for (uint8_t i = 0, pos = weller_tx_tail; i < weller_tx_count; ++i, pos = (uint8_t)((pos + 1) % WELLER_TX_QUEUE_SIZE)) {
    if (weller_tx_queue[pos].len == 1 && weller_tx_queue[pos].data[0] == c) return true;
  }
  return false;
}

static void weller_send_query(char c) {
  if (weller_single_queued(c)) return;
  weller_queue_raw(&c, 1);
}

static void weller_mark_idle(uint32_t idle_ms) {
  WELLER.end();
  pinMode(WELLER_RX_PIN, INPUT_PULLUP);
  pinMode(WELLER_TX_PIN, OUTPUT);
  digitalWrite(WELLER_TX_PIN, HIGH);
  delay(idle_ms);
  WELLER.begin(WELLER_BAUD, SERIAL_8N1, WELLER_RX_PIN, WELLER_TX_PIN);
}

static void weller_schedule_probe(uint32_t delay_ms) {
  weller_probe_due_ms = millis() + delay_ms;
}

static void weller_queue_startup_queries() {
  weller_send_query('D');
  weller_send_query('S');
  weller_send_query('L');
  weller_send_query('A');
  weller_send_query('V');
  weller_send_query('F');
  weller_send_query('G');
}

static void weller_restart_local_bus(bool preserve_output) {
  while (WELLER.available()) (void)WELLER.read();
  weller_mark_idle(WELLER_LINE_IDLE_MS);
  rx_len = 0;
  weller_clear_tx_queue();
  last_weller_tx_ms = 0;
  last_fast_poll_ms = 0;
  last_medium_poll_ms = 0;
  last_slow_poll_ms = 0;
  last_fan_command_ms = 0;
  last_weller_recovery_ms = millis();

  if (preserve_output && output_enabled) {
    weller_send_fan_state(true);
    weller_send_speed(target_speed_percent ? target_speed_percent : manual_speed_percent);
  } else {
    weller_send_fan_state(false);
  }
  weller_schedule_probe(WELLER_PROBE_DELAY_MS);
}

static void weller_send_checked4(char prefix, uint16_t value) {
  if (value > 999) value = 999;
  char cmd[5];
  cmd[0] = prefix;
  cmd[1] = (char)('0' + (value / 100) % 10);
  cmd[2] = (char)('0' + (value / 10) % 10);
  cmd[3] = (char)('0' + value % 10);
  cmd[4] = (char)cs4(cmd);
  weller_queue_raw(cmd, 5);
}

static void weller_send_speed(uint8_t percent) {
  if (percent < 30) percent = 30;
  if (percent > 100) percent = 100;
  char cmd[5];
  cmd[0] = 'd';
  cmd[1] = (char)('0' + (percent / 100) % 10);
  cmd[2] = (char)('0' + (percent / 10) % 10);
  cmd[3] = (char)('0' + percent % 10);
  cmd[4] = (char)cs4(cmd);
  weller_queue_raw(cmd, sizeof(cmd));
}

static void weller_set_filter_runtime(uint16_t minutes) {
  // The Weller stores programmed filter lifetime in 10-minute steps. Keep the
  // web/display side from accidentally writing tiny limits such as 20 minutes,
  // which immediately makes a used filter look "full" on the Weller itself.
  if (minutes < 60U) minutes = 60U;
  if (minutes > 9990U) minutes = 9990U;
  uint16_t steps = (minutes + 5U) / 10U;
  if (steps < 6U) steps = 6U;
  if (steps > 999U) steps = 999U;
  weller_send_checked4('f', steps);
}

static void weller_send_light(bool enabled) {
  const char* cmd = enabled ? "a100" : "a000";
  char out[6];
  memcpy(out, cmd, 4);
  out[4] = (char)cs4(out);
  out[5] = 'A';
  weller_queue_raw(out, sizeof(out));
  work_light_state = enabled ? 1 : 0;
}

static void weller_reset_filter_runtime() {
  // The Weller owns the real filter runtime counter. Send the reset command,
  // then wait for the next G/L replies before changing the displayed values.
  weller_send_checked4('g', 0);
}

static uint8_t power_to_percent(uint16_t power) {
  if (power == 0) return 0;
  uint32_t pct = ((uint32_t)power * 100UL + 500UL) / 1000UL;
  if (pct < 30) pct = 30;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

static void weller_send_fan_state(bool enabled) {
  weller_send_query(enabled ? 'N' : 'M');
  last_fan_command_ms = millis();
  if (enabled) {
    io_output_mask |= 0x0001;
    if (!fan_on_since_ms) fan_on_since_ms = millis();
  } else {
    io_output_mask &= (uint16_t)~0x0001U;
    fan_on_since_ms = 0;
    fault_mask &= (uint16_t)~WELLER_FAULT_SPEED_FEEDBACK;
  }
}

static void weller_apply_output() {
  weller_clear_tx_queue();
  if (output_enabled) {
    weller_send_fan_state(true);
    target_speed_percent = output_power ? power_to_percent(output_power) : manual_speed_percent;
    if (target_speed_percent < 30) target_speed_percent = 30;
    if (target_speed_percent > 100) target_speed_percent = 100;
    weller_send_speed(target_speed_percent);
  } else {
    weller_send_fan_state(false);
  }
}

static void poll_fan_keepalive() {
  // Match the proven ESPHome interface: repeat the current fan state. This
  // makes a missed N/M byte self-healing and keeps the Weller controller in the
  // intended state without touching the RS485 bus.
  if ((uint32_t)(millis() - last_fan_command_ms) < 5000UL) return;
  weller_send_fan_state(output_enabled);
}

static void output_off() {
  if (!output_enabled && output_power == 0 && !(io_output_mask & 0x0001)) return;
  output_enabled = false;
  output_power = 0;
  weller_apply_output();
}

static void check_output_failsafe() {
  if (!last_master_ms || (!output_enabled && !(io_output_mask & 0x0001))) return;
  if ((uint32_t)(millis() - last_master_ms) <= OUTPUT_FAILSAFE_TIMEOUT_MS) return;
  output_off();
}

static void poll_manual_speed_save() {
  if (!manual_speed_dirty) return;
  if ((int32_t)(millis() - manual_speed_save_due_ms) < 0) return;
  prefs.putUChar("speed", manual_speed_percent);
  manual_speed_dirty = false;
}

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static void publish_weller_frame(char typ, uint16_t val) {
  last_weller_rx_ms = millis();
  switch (typ) {
    case 'D':
      fan_rpm = val * 10U;
      break;
    case 'S':
      reported_speed_percent = (uint8_t)(val > 100 ? 100 : val);
      break;
    case 'G':
      filter_runtime_minutes = val * 10U;
      break;
    case 'F':
      programmed_filter_minutes = val * 10U;
      break;
    case 'L':
      filter_status_code = (uint8_t)(val > 255 ? 255 : val);
      fault_mask &= (uint16_t)~0x0006U;
      if (val == 10) fault_mask |= 0x0002U;
      if (val == 100) fault_mask |= 0x0004U;
      break;
    case 'V':
      weller_version = val;
      break;
    case 'A':
      work_light_state = val >= 100 ? 1 : 0;
      break;
    default:
      break;
  }
}

static void consume_weller_rx(uint8_t used) {
  if (used >= rx_len) {
    rx_len = 0;
    return;
  }
  rx_len -= used;
  memmove(rx_buf, rx_buf + used, rx_len);
}

static void publish_weller_light_ack(uint16_t val) {
  last_weller_rx_ms = millis();
  work_light_state = val >= 100 ? 1 : 0;
  if (work_light_state) io_output_mask |= 0x0002;
  else io_output_mask &= (uint16_t)~0x0002U;
}

static void poll_weller_rx() {
  while (WELLER.available()) {
    const char c = (char)WELLER.read();
    if (rx_len >= sizeof(rx_buf)) rx_len = 0;
    rx_buf[rx_len++] = c;

    while (rx_len > 0) {
      // Weller sends the work-light acknowledgement as: aDDD<space>A<CS>.
      // The old ESPHome parser had this special case; without it the light
      // state can look stale and the lower-case frame constantly resyncs us.
      if (rx_buf[0] == 'a') {
        if (rx_len < 7) break;
        if (is_digit(rx_buf[1]) && is_digit(rx_buf[2]) && is_digit(rx_buf[3]) &&
            rx_buf[4] == ' ' && rx_buf[5] == 'A' && (uint8_t)rx_buf[6] == cs_bytes(rx_buf, 6)) {
          const uint16_t val = (uint16_t)((rx_buf[1] - '0') * 100 +
                                          (rx_buf[2] - '0') * 10 +
                                          (rx_buf[3] - '0'));
          local_trace_log(1, (uint8_t)rx_buf[0], 0, (const uint8_t*)rx_buf, 7);
          publish_weller_light_ack(val);
          consume_weller_rx(7);
          continue;
        }
        consume_weller_rx(1);
        continue;
      }

      if (rx_len < 4) break;
      if (!(rx_buf[0] >= 'A' && rx_buf[0] <= 'Z')) {
        consume_weller_rx(1);
        continue;
      }
      if (!is_digit(rx_buf[1]) || !is_digit(rx_buf[2]) || !is_digit(rx_buf[3])) {
        consume_weller_rx(1);
        continue;
      }

      if (rx_len < 5) break;
      if ((uint8_t)rx_buf[4] != cs4(rx_buf)) {
        local_trace_log(1, (uint8_t)rx_buf[0], 0xEE, (const uint8_t*)rx_buf, 5);
        consume_weller_rx(1);
        continue;
      }

      const char typ = rx_buf[0];
      const uint16_t val = (uint16_t)((rx_buf[1] - '0') * 100 +
                                      (rx_buf[2] - '0') * 10 +
                                      (rx_buf[3] - '0'));
      local_trace_log(1, (uint8_t)typ, 0, (const uint8_t*)rx_buf, 5);
      publish_weller_frame(typ, val);
      consume_weller_rx(5);
    }
  }
}

static void poll_weller_startup_probe() {
  if (!weller_probe_due_ms) return;
  if ((int32_t)(millis() - weller_probe_due_ms) < 0) return;
  weller_probe_due_ms = 0;
  weller_queue_startup_queries();
}

static void poll_weller_recovery() {
  const uint32_t now = millis();
  if (last_weller_rx_ms && (uint32_t)(now - last_weller_rx_ms) <= WELLER_RECONNECT_GRACE_MS) return;
  if ((uint32_t)(now - last_weller_recovery_ms) < WELLER_RECONNECT_RETRY_MS) return;
  weller_restart_local_bus(true);
}

static void poll_weller_queries() {
  const uint32_t now = millis();
  if ((uint32_t)(now - last_fast_poll_ms) >= 1000UL) {
    last_fast_poll_ms = now;
    weller_send_query('D');
    weller_send_query('S');
    weller_send_query('L');
  }
  if ((uint32_t)(now - last_medium_poll_ms) >= 2000UL) {
    last_medium_poll_ms = now;
    weller_send_query('A');
    weller_send_query('V');
    weller_send_query('F');
  }
  if ((uint32_t)(now - last_slow_poll_ms) >= 10000UL) {
    last_slow_poll_ms = now;
    weller_send_query('G');
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
  resp.payload[o++] = MODULE_WELLER_ZERO_SMOG;
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
  const char name[] = "Weller Zero Smog Bus";
  const char* shown_name = module_label[0] ? module_label : name;
  while (*shown_name && o < MAX_PAYLOAD) resp.payload[o++] = (uint8_t)*shown_name++;
  resp.len = (uint8_t)o;
  bus.send(resp);
}

static uint32_t module_caps() {
  return CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_WELLER_INTERFACE | CAP_FILTER_SENSOR | CAP_FAULT_REPORT | CAP_FW_UPDATE | CAP_DIGITAL_OUTPUT | CAP_LOCAL_TRACE;
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

static void update_weller_faults() {
  const uint32_t now = millis();
  if (!last_weller_rx_ms || (uint32_t)(now - last_weller_rx_ms) > 5000UL) fault_mask |= WELLER_FAULT_BUS;
  else fault_mask &= (uint16_t)~WELLER_FAULT_BUS;

  const bool fan_requested = output_enabled || (io_output_mask & 0x0001U);
  if (!fan_requested) {
    fan_on_since_ms = 0;
    fault_mask &= (uint16_t)~WELLER_FAULT_SPEED_FEEDBACK;
    return;
  }
  if (!fan_on_since_ms) fan_on_since_ms = now;
  if (fan_rpm > 0) {
    fault_mask &= (uint16_t)~WELLER_FAULT_SPEED_FEEDBACK;
  } else if (!(fault_mask & WELLER_FAULT_BUS) && (uint32_t)(now - fan_on_since_ms) > WELLER_RPM_GRACE_MS) {
    fault_mask |= WELLER_FAULT_SPEED_FEEDBACK;
  }
}
static void handle_status(const Frame& req) {
  update_weller_faults();

  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_STATUS | 0x80;
  resp.len = 8;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = output_enabled ? 1 : 0;
  put_u16_le(resp.payload + 2, output_power);
  put_u16_le(resp.payload + 4, fan_rpm);
  put_u16_le(resp.payload + 6, fault_mask);
  bus.send(resp);
}

static void record_loop_time(uint32_t busy_us) {
  if (busy_us > loop_max_us) loop_max_us = busy_us;
  const uint32_t now = millis();
  if ((uint32_t)(now - loop_window_ms) >= 1000UL) {
    uint32_t max_ms = (loop_max_us + 999UL) / 1000UL;
    if (max_ms > 65535UL) max_ms = 65535UL;
    loop_max_ms = (uint16_t)max_ms;
    if ((uint32_t)(now - last_cpu_sample_ms) >= 5000UL) {
      last_cpu_sample_ms = now;
      sample_cpu_load();
    }
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

static void handle_telemetry(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_TELEMETRY | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_WELLER_ZERO_SMOG;
  resp.payload[o++] = reported_speed_percent;
  resp.payload[o++] = filter_status_code;
  put_u16_le(resp.payload + 4, filter_runtime_minutes);
  put_u16_le(resp.payload + 6, programmed_filter_minutes);
  uint16_t age_sec = 0xFFFF;
  if (last_weller_rx_ms) {
    const uint32_t age = (millis() - last_weller_rx_ms) / 1000UL;
    age_sec = age > 0xFFFEUL ? 0xFFFE : (uint16_t)age;
  }
  put_u16_le(resp.payload + 8, age_sec);
  put_u16_le(resp.payload + 10, weller_version);
  resp.payload[12] = work_light_state;
  put_u16_le(resp.payload + 13, fan_rpm);
  o = 15;
  append_system_telemetry(resp.payload, o);
  resp.payload[o++] = (uint8_t)ofe_status_leds.busEvent();
  resp.payload[o++] = (uint8_t)ofe_status_leds.moduleEvent();
  resp.len = o;
  bus.send(resp);
}

static void handle_get_io(const Frame& req) {
  update_weller_faults();
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_IO | 0x80;
  resp.len = 7;
  resp.payload[0] = STATUS_OK;
  put_u16_le(resp.payload + 1, 0);
  put_u16_le(resp.payload + 3, io_output_mask);
  put_u16_le(resp.payload + 5, fault_mask);
  bus.send(resp);
}

static void handle_set_io(const Frame& req) {
  if (req.len != 4) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint16_t mask = get_u16_le(req.payload);
  const uint16_t value = get_u16_le(req.payload + 2);
  if (mask & 0x0001) {
    output_enabled = (value & 0x0001) != 0;
    weller_apply_output();
  }
  if (mask & 0x0002) {
    const bool light_on = (value & 0x0002) != 0;
    weller_send_light(light_on);
    if (light_on) io_output_mask |= 0x0002;
    else io_output_mask &= (uint16_t)~0x0002U;
  }
  send_status_response(req, STATUS_OK);
}

static void handle_set_output(const Frame& req) {
  if (req.len < 1) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  switch (req.payload[0]) {
    case 1:
      if (req.len < 2) {
        send_status_response(req, STATUS_BAD_LEN);
        return;
      }
      target_speed_percent = req.payload[1];
      if (target_speed_percent < 30) target_speed_percent = 30;
      if (target_speed_percent > 100) target_speed_percent = 100;
      manual_speed_percent = target_speed_percent;
      manual_speed_dirty = true;
      manual_speed_save_due_ms = millis() + 30000UL;
      output_power = (uint16_t)target_speed_percent * 10U;
      weller_clear_tx_queue();
      weller_send_speed(target_speed_percent);
      send_status_response(req, STATUS_OK);
      return;
    case 2:
      weller_reset_filter_runtime();
      send_status_response(req, STATUS_OK);
      return;
    case 3:
      if (req.len < 3) {
        send_status_response(req, STATUS_BAD_LEN);
        return;
      }
      weller_set_filter_runtime(get_u16_le(req.payload + 1));
      send_status_response(req, STATUS_OK);
      return;
    default:
      send_status_response(req, STATUS_BAD_VALUE);
      return;
  }
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
  resp.payload[o++] = MODULE_WELLER_ZERO_SMOG;
  put_u64_le(resp.payload + o, uid); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u32_le(resp.payload + o, module_caps()); o += 4;
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
  resp.payload[o++] = MODULE_WELLER_ZERO_SMOG;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u32_le(resp.payload + o, module_caps()); o += 4;
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
  if (get_u64_le(req.payload) != module_uid()) return;
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

  if (req.src == ADDR_MASTER) last_master_ms = millis();

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
    case CMD_GET_IO:
      handle_get_io(req);
      break;
    case CMD_SET_IO:
      handle_set_io(req);
      break;
    case CMD_SET_LABEL:
      handle_set_label(req);
      break;
    case CMD_SET_OUTPUT:
      handle_set_output(req);
      break;
    case CMD_TRACE_CONTROL:
      handle_trace_control(req);
      break;
    case CMD_TRACE_READ:
      handle_trace_read(req);
      break;
    case CMD_SET_ENABLE:
      if (req.len != 1) {
        send_status_response(req, STATUS_BAD_LEN);
        break;
      }
      output_enabled = req.payload[0] != 0;
      weller_apply_output();
      send_status_response(req, STATUS_OK);
      break;
    case CMD_SET_POWER:
      if (req.len != 2) {
        send_status_response(req, STATUS_BAD_LEN);
        break;
      }
      output_power = get_u16_le(req.payload);
      if (output_power > 1000) output_power = 1000;
      if (output_enabled) weller_apply_output();
      send_status_response(req, STATUS_OK);
      break;
    case CMD_SET_ADDRESS:
      if (req.len != 1 || !valid_module_addr(req.payload[0])) {
        send_status_response(req, STATUS_BAD_VALUE);
        break;
      }
      prefs.putUChar("addr", req.payload[0]);
      send_status_response(req, STATUS_OK);
      delay(20);
      module_addr = req.payload[0];
      break;
    case CMD_FACTORY_RESET:
      prefs.clear();
      module_addr = DEFAULT_MODULE_ADDR;
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
#if DEBUG_SERIAL_ENABLE
  Serial.begin(115200);
#endif
  delay(300);

  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  WELLER.begin(WELLER_BAUD, SERIAL_8N1, WELLER_RX_PIN, WELLER_TX_PIN);
  prefs.begin("weller-smog", false);
  module_addr = prefs.getUChar("addr", DEFAULT_MODULE_ADDR);
  prefs.getString("label", module_label, sizeof(module_label));
  manual_speed_percent = prefs.getUChar("speed", 30);
  if (manual_speed_percent < 30 || manual_speed_percent > 100) manual_speed_percent = 30;
  target_speed_percent = manual_speed_percent;
  if (!valid_module_addr(module_addr)) {
    module_addr = DEFAULT_MODULE_ADDR;
    prefs.putUChar("addr", module_addr);
  }

  weller_send_query('M');

#if DEBUG_SERIAL_ENABLE
  Serial.println("Weller Zero Smog RS485 module");
  Serial.print("addr=0x");
  if (module_addr < 0x10) Serial.print('0');
  Serial.println(module_addr, HEX);
#endif
  join_announce_left = 8;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
}

void loop() {
  const uint32_t led_now = millis();
  const bool bus_online = last_master_ms && (uint32_t)(led_now - last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS;
  const bool weller_online = last_weller_rx_ms && (uint32_t)(led_now - last_weller_rx_ms) <= 5000UL;
  ofe_status_leds.setBusOnline(bus_online);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  ofe_status_leds.setModuleEvent(fault_mask ? (((fault_mask & (uint16_t)~(WELLER_FAULT_BUS | 0x0002U)) != 0) ? OFE_LED_EVENT_CRITICAL : OFE_LED_EVENT_WARNING) : (output_enabled ? OFE_LED_EVENT_EXTRACTOR_ON : (weller_online ? OFE_LED_EVENT_DEVICE_ONLINE : OFE_LED_EVENT_WARNING)));
  ofe_status_leds.tick();
  const uint32_t loop_start_us = micros();
  poll_weller_rx();
  poll_rs485();
  poll_pending_discover_response();
  poll_join_announce();
  fw_update_check_timeout();
  poll_fan_keepalive();
  poll_weller_queries();
  poll_weller_tx();
  update_weller_faults();
  check_output_failsafe();
  poll_manual_speed_save();
  record_loop_time((uint32_t)(micros() - loop_start_us));
  // Prevent the Arduino loop task from busy-spinning on one CPU core.
  // Placed after runtime measurement so loop_max_ms reports only real work.
  delay(1);
}











