#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <stdarg.h>

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
#include "src/OfeModuleEco.h"
#include "src/FanIoHardwareConfig.h"

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

#ifndef FAN_PWM_DEFAULT_FREQUENCY_HZ
#define FAN_PWM_DEFAULT_FREQUENCY_HZ 1000U
#endif

#ifndef FAN_PWM_RESOLUTION_BITS
#define FAN_PWM_RESOLUTION_BITS 8U
#endif

#ifndef FAN_PWM_MAX_RESOLUTION_BITS
#define FAN_PWM_MAX_RESOLUTION_BITS 16U
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

static const uint16_t FAN_PWM_MIN_FREQUENCY_HZ = 10U;
static const uint16_t FAN_PWM_MAX_FREQUENCY_HZ = 30000U;
#if ESP_ARDUINO_VERSION_MAJOR < 3
static const uint8_t FAN_PWM_LEDC_CHANNEL = 0;
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
#define OFE_MODULE_FW_PATCH 73
#define OFE_MODULE_FW_SUFFIX "beta"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=FAN_IO;version=" OFE_MODULE_FW_VERSION ";";
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

static HardwareSerial RS485(1);
static Link bus(RS485);
static Preferences prefs;
static OfeStatusLed ofe_status_leds;
static bool module_eco_mode = false;
static bool module_light_sleep_armed = false;

static uint8_t module_addr = 0x20;
static char module_label[24] = {0};
static char io_alias[5][19] = {{0}};
static ofe_fanio::HardwareConfig hw_config;
// Hardware-editor transaction buffer. While a transaction is active,
// CHANNEL/FAN/FILTER writes modify only this RAM copy. The live GPIO map and
// NVS stay untouched until IO_CONFIG_COMMIT succeeds.
static ofe_fanio::HardwareConfig io_config_pending;
static uint16_t io_config_pending_pwm_frequency_hz = FAN_PWM_DEFAULT_FREQUENCY_HZ;
static bool io_config_transaction_active = false;
static uint32_t io_config_transaction_last_ms = 0;
static const uint32_t IO_CONFIG_TRANSACTION_TIMEOUT_MS = 15000UL;
static uint16_t fan_pwm_frequency_hz = FAN_PWM_DEFAULT_FREQUENCY_HZ;
static int16_t fan_pwm_attached_pin = -1;
static bool fan_pwm_ready = false;
static uint8_t fan_pwm_resolution_bits = FAN_PWM_RESOLUTION_BITS;
static char dynamic_descriptor[3072] = {0};
static bool dynamic_descriptor_dirty = true;
static bool fw_update_active = false;
static uint32_t fw_update_last_ms = 0;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;

#line 106 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void fw_update_abort_local();
#line 111 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void fw_update_touch();
#line 115 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void fw_update_check_timeout();
#line 151 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void sample_cpu_load();
#line 175 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static uint64_t module_uid();
#line 178 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static bool valid_module_addr(uint8_t addr);
#line 182 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static uint16_t sanitize_output_power(uint16_t power, bool allow_zero);
#line 189 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void remember_output_power(uint16_t power);
#line 201 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void write_enable_pin(bool enabled);
#line 206 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void apply_output();
#line 224 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void apply_generic_outputs();
#line 231 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void generic_outputs_off();
#line 237 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void output_off();
#line 244 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void check_output_failsafe();
#line 251 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void update_rpm();
#line 289 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void update_generic_inputs();
#line 298 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void send_status_response(const Frame& req, Status status);
#line 309 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_fw_begin(const Frame& req);
#line 332 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_fw_chunk(const Frame& req);
#line 364 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_fw_end(const Frame& req);
#line 378 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_fw_status(const Frame& req);
#line 391 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_fw_abort(const Frame& req);
#line 396 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void copy_label_from_payload(const Frame& req);
#line 407 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_set_label(const Frame& req);
#line 414 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_info(const Frame& req);
#line 443 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_caps(const Frame& req);
#line 465 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_status(const Frame& req);
#line 480 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static uint32_t discover_delay_ms(const Frame& req);
#line 492 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void send_discover_response(uint8_t dst, uint8_t seq);
#line 522 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_discover(const Frame& req);
#line 530 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void poll_pending_discover_response();
#line 541 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static uint32_t join_delay_ms(uint8_t round);
#line 548 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void send_join_announce();
#line 577 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void poll_join_announce();
#line 584 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_set_address_uid(const Frame& req);
#line 599 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_get_io(const Frame& req);
#line 614 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_set_io(const Frame& req);
#line 627 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void record_loop_time(uint32_t busy_us);
#line 640 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void append_system_telemetry(uint8_t* payload, uint8_t& o);
#line 648 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_telemetry(const Frame& req);
#line 662 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void handle_frame(const Frame& req);
#line 805 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
static void poll_rs485();
#line 810 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
void setup();
#line 855 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
void loop();
#line 106 "C:\\Users\\User\\Documents\\Codex\\2026-06-13\\files-mentioned-by-the-user-jbc\\outputs\\OpenFumeExtractorMaster\\Module\\FanIoModule\\FanIoModule.ino"
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
static uint16_t io_input_raw_mask = 0;
static uint16_t io_input_event_mask = 0;
static uint32_t io_input_change_ms[ofe_fanio::MAX_INPUTS] = {};
static const uint32_t GENERIC_INPUT_DEBOUNCE_MS = 10UL;
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
  if (power < 10) power = 10;
  return power;
}

static bool fan_uses_relay() {
  return ofe_fanio::fanUsesRelay(hw_config);
}

static bool fan_uses_pwm() {
  return ofe_fanio::fanUsesPwm(hw_config);
}

static uint16_t clamp_output_power_to_config(uint16_t power) {
  if (!fan_uses_pwm()) return 1000U;
  power = sanitize_output_power(power, false);
  const uint16_t min_power = (uint16_t)hw_config.fan_min_power_percent * 10U;
  const uint16_t max_power = (uint16_t)hw_config.fan_max_power_percent * 10U;
  if (power < min_power) power = min_power;
  if (power > max_power) power = max_power;
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

static bool fan_pwm_frequency_valid(uint32_t hz) {
  return hz >= FAN_PWM_MIN_FREQUENCY_HZ && hz <= FAN_PWM_MAX_FREQUENCY_HZ;
}

static void fan_pwm_set_open_drain(uint8_t pin, bool enabled) {
  // Do not call gpio_set_direction()/pinMode() after LEDC has been attached.
  // Those GPIO helpers switch the pin back to the plain GPIO function on some
  // ESP32 Arduino/IDF versions and thereby disconnect the LEDC matrix output.
  // Open-drain itself is only the PAD_DRIVER bit, so change exactly that bit
  // while leaving the peripheral routing untouched.
  if (pin <= 39U) GPIO.pin[pin].pad_driver = enabled ? 1U : 0U;
}

static void fan_pwm_detach() {
  if (fan_pwm_attached_pin < 0) {
    fan_pwm_ready = false;
    return;
  }
  const uint8_t pin = (uint8_t)fan_pwm_attached_pin;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(pin);
#else
  ledcDetachPin(pin);
#endif
  // LEDC is detached now, so returning the pin to ordinary push-pull GPIO is
  // safe and avoids leaving an old open-drain setting behind.
  fan_pwm_set_open_drain(pin, false);
  pinMode(pin, OUTPUT);
  fan_pwm_attached_pin = -1;
  fan_pwm_ready = false;
}

static bool fan_pwm_attach(uint8_t pin) {
  fan_pwm_detach();
  if (pin == ofe_fanio::PIN_UNUSED) return false;
  if (!fan_pwm_frequency_valid(fan_pwm_frequency_hz)) fan_pwm_frequency_hz = FAN_PWM_DEFAULT_FREQUENCY_HZ;
  pinMode(pin, OUTPUT);

  // The LEDC timer cannot cover 10 Hz .. 30 kHz with one fixed resolution on
  // every ESP32 target. Try the highest useful resolution first and fall back
  // until the requested frequency is representable. This keeps low frequencies
  // such as 10 Hz valid while preserving good duty resolution at higher rates.
  fan_pwm_ready = false;
  fan_pwm_resolution_bits = FAN_PWM_RESOLUTION_BITS;
  for (int bits = (int)FAN_PWM_MAX_RESOLUTION_BITS; bits >= (int)FAN_PWM_RESOLUTION_BITS; --bits) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if (ledcAttach(pin, fan_pwm_frequency_hz, (uint8_t)bits)) {
      fan_pwm_ready = true;
      fan_pwm_resolution_bits = (uint8_t)bits;
      break;
    }
#else
    const double actual = ledcSetup(FAN_PWM_LEDC_CHANNEL, fan_pwm_frequency_hz, (uint8_t)bits);
    if (actual > 0.0) {
      ledcAttachPin(pin, FAN_PWM_LEDC_CHANNEL);
      fan_pwm_ready = true;
      fan_pwm_resolution_bits = (uint8_t)bits;
      break;
    }
#endif
  }
  fan_pwm_attached_pin = fan_pwm_ready ? (int16_t)pin : -1;
  if (fan_pwm_ready) {
    // Set only the open-drain pad flag AFTER LEDC routing exists. Changing the
    // whole GPIO direction here would break PWM output on affected cores.
    fan_pwm_set_open_drain(pin, (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_OPEN_DRAIN) != 0);
  }
  return fan_pwm_ready;
}

static void fan_pwm_write(uint8_t duty) {
  if (!fan_pwm_ready || fan_pwm_attached_pin < 0) return;
  const uint32_t max_duty = (1UL << fan_pwm_resolution_bits) - 1UL;
  const uint32_t scaled_duty = ((uint32_t)duty * max_duty + 127UL) / 255UL;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite((uint8_t)fan_pwm_attached_pin, scaled_duty);
#else
  ledcWrite(FAN_PWM_LEDC_CHANNEL, scaled_duty);
#endif
}


static void IRAM_ATTR tacho_isr() {
  ++tacho_edges;
}

static void write_enable_pin(bool enabled) {
  if (!(hw_config.fan_flags & ofe_fanio::CHANNEL_ENABLED) || !fan_uses_relay() || hw_config.fan_enable_pin == ofe_fanio::PIN_UNUSED) return;
  const bool active_low = (hw_config.fan_enable_flags & ofe_fanio::FAN_ENABLE_ACTIVE_LOW) != 0;
  digitalWrite(hw_config.fan_enable_pin, (enabled != active_low) ? HIGH : LOW);
}

static void apply_output() {
  const bool configured = (hw_config.fan_flags & ofe_fanio::CHANNEL_ENABLED) != 0;
  const uint16_t effective_power = clamp_output_power_to_config(output_power);
  const bool demand = configured && output_enabled && (!fan_uses_pwm() || effective_power >= FAN_TACHO_FAULT_MIN_POWER);
  if (demand && !fan_demand_active) {
    fan_demand_since_ms = millis();
    fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
  } else if (!demand) {
    fan_demand_since_ms = 0;
    fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
  }
  fan_demand_active = demand;

  write_enable_pin(configured && output_enabled);
  if (fan_uses_pwm() && hw_config.fan_pwm_pin != ofe_fanio::PIN_UNUSED) {
    int duty = configured && output_enabled ? map(effective_power, 0, 1000, 0, 255) : 0;
    if (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_ACTIVE_LOW) duty = 255 - duty;
    fan_pwm_write((uint8_t)constrain(duty, 0, 255));
  }
}

static void apply_generic_outputs() {
  for (uint8_t i = 0; i < ofe_fanio::MAX_OUTPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.outputs[i];
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED) || ch.pin == ofe_fanio::PIN_UNUSED) continue;
    const bool on = (io_output_mask & (uint16_t)(1U << i)) != 0;
    const bool active_low = (ch.flags & ofe_fanio::CHANNEL_ACTIVE_LOW) != 0;
    digitalWrite(ch.pin, (on != active_low) ? HIGH : LOW);
  }
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
  if (hw_config.fan_tacho_pin == ofe_fanio::PIN_UNUSED) {
    fan_rpm = 0;
    fault_mask &= (uint16_t)~(FAULT_NO_TACH | FAULT_LOW_RPM);
    return;
  }
  const uint32_t now = millis();
  if ((uint32_t)(now - last_rpm_ms) < 1000UL) return;
  const uint32_t edges = tacho_edges;
  const uint32_t delta = edges - last_tacho_edges;
  last_tacho_edges = edges;
  last_rpm_ms = now;

  uint32_t rpm = 0;
  if (hw_config.tacho_pulses_per_rev > 0) {
    rpm = (delta * 60UL) / hw_config.tacho_pulses_per_rev;
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
}

static bool generic_input_active(const ofe_fanio::ChannelConfig& ch) {
  bool active = digitalRead(ch.pin) == HIGH;
  if (ch.flags & ofe_fanio::CHANNEL_ACTIVE_LOW) active = !active;
  return active;
}

static void reset_generic_input_state() {
  const uint32_t now = millis();
  uint16_t initial = 0;
  for (uint8_t i = 0; i < ofe_fanio::MAX_INPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.inputs[i];
    io_input_change_ms[i] = now;
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED) || ch.pin == ofe_fanio::PIN_UNUSED) continue;
    if (generic_input_active(ch)) initial |= (uint16_t)(1U << i);
  }
  io_input_raw_mask = initial;
  io_input_mask = initial;
  io_input_event_mask = 0;
}

static void update_generic_inputs() {
  const uint32_t now = millis();
  uint16_t enabled_mask = 0;
  for (uint8_t i = 0; i < ofe_fanio::MAX_INPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.inputs[i];
    const uint16_t bit = (uint16_t)(1U << i);
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED) || ch.pin == ofe_fanio::PIN_UNUSED) {
      io_input_raw_mask &= (uint16_t)~bit;
      io_input_mask &= (uint16_t)~bit;
      io_input_event_mask &= (uint16_t)~bit;
      continue;
    }
    enabled_mask |= bit;
    const bool raw_active = generic_input_active(ch);
    const bool previous_raw = (io_input_raw_mask & bit) != 0;
    if (raw_active != previous_raw) {
      if (raw_active) io_input_raw_mask |= bit;
      else io_input_raw_mask &= (uint16_t)~bit;
      io_input_change_ms[i] = now;
    }

    const bool stable_active = (io_input_mask & bit) != 0;
    if (raw_active != stable_active &&
        (uint32_t)(now - io_input_change_ms[i]) >= GENERIC_INPUT_DEBOUNCE_MS) {
      if (raw_active) {
        io_input_mask |= bit;
        // Rising/activation events are held until a master live poll fetches
        // them. This makes short button presses reliable even when they start
        // and end completely between two RS485 status polls.
        io_input_event_mask |= bit;
      } else {
        io_input_mask &= (uint16_t)~bit;
      }
    }
  }
  io_input_raw_mask &= enabled_mask;
  io_input_mask &= enabled_mask;
  io_input_event_mask &= enabled_mask;
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

static bool fan_output_configured() {
  if (!(hw_config.fan_flags & ofe_fanio::CHANNEL_ENABLED)) return false;
  if (fan_uses_relay() && hw_config.fan_enable_pin == ofe_fanio::PIN_UNUSED) return false;
  if (fan_uses_pwm() && hw_config.fan_pwm_pin == ofe_fanio::PIN_UNUSED) return false;
  return fan_uses_relay() || fan_uses_pwm();
}

static uint32_t advertised_caps() {
  uint32_t caps = CAP_FAULT_REPORT | CAP_FW_UPDATE | CAP_POWER_SAVE | CAP_DESCRIPTOR | CAP_ENTITY_CONTROL;
#if GENERIC_IO_ENABLE
  caps |= CAP_INPUT_KEYS | CAP_DIGITAL_OUTPUT;
#endif
  if (fan_output_configured()) {
    if (fan_uses_relay()) caps |= CAP_RELAY_OUTPUT;
#if FAN_PWM_ENABLE
    if (fan_uses_pwm()) caps |= CAP_PWM_OUTPUT;
#endif
#if FAN_TACHO_ENABLE
    if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) caps |= CAP_TACHO_INPUT;
#endif
  }
  return caps;
}

static void handle_info(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_INFO | 0x80;

  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_FAN_IO;
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
  const char* shown_name = module_label[0] ? module_label : ofe_module_default_name(MODULE_FAN_IO);
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
  uint32_t caps = advertised_caps();
  put_u32_le(resp.payload + 1, caps);
  bus.send(resp);
}

static void handle_status(const Frame& req) {
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
  resp.payload[o++] = MODULE_FAN_IO;
  put_u64_le(resp.payload + o, uid); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  uint32_t caps = advertised_caps();
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
  resp.payload[o++] = MODULE_FAN_IO;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  uint32_t caps = advertised_caps();
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
  if (prefs.putUChar("addr", next_addr) != sizeof(uint8_t)) {
    send_status_response(req, STATUS_BUSY);
    return;
  }
  send_status_response(req, STATUS_OK);
  delay(20);
  module_addr = next_addr;
}

static bool configure_dynamic_hardware(bool detach_current) {
  if (detach_current && hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) detachInterrupt(digitalPinToInterrupt(hw_config.fan_tacho_pin));
  if ((hw_config.fan_flags & ofe_fanio::CHANNEL_ENABLED) && fan_uses_relay() && hw_config.fan_enable_pin != ofe_fanio::PIN_UNUSED) {
    pinMode(hw_config.fan_enable_pin, OUTPUT);
    write_enable_pin(false);
  }
  fan_pwm_detach();
  bool pwm_ok = true;
  if ((hw_config.fan_flags & ofe_fanio::CHANNEL_ENABLED) && fan_uses_pwm() && hw_config.fan_pwm_pin != ofe_fanio::PIN_UNUSED) {
    const uint8_t off_duty = (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_ACTIVE_LOW) ? 255U : 0U;
    pwm_ok = fan_pwm_attach(hw_config.fan_pwm_pin);
    if (pwm_ok) fan_pwm_write(off_duty);
    else {
      // Keep the pin in a safe inactive state, but report the LEDC failure to
      // the caller. A hardware-editor COMMIT must never say OK when PWM could
      // not actually be attached.
      pinMode(hw_config.fan_pwm_pin, (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_OPEN_DRAIN) ? OUTPUT_OPEN_DRAIN : OUTPUT);
      digitalWrite(hw_config.fan_pwm_pin, off_duty ? HIGH : LOW);
    }
  }
  for (uint8_t i = 0; i < ofe_fanio::MAX_INPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.inputs[i];
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED) || ch.pin == ofe_fanio::PIN_UNUSED) continue;
    uint8_t mode = INPUT;
    if (ch.flags & ofe_fanio::CHANNEL_PULL_UP) mode = INPUT_PULLUP;
#ifdef INPUT_PULLDOWN
    else if (ch.flags & ofe_fanio::CHANNEL_PULL_DOWN) mode = INPUT_PULLDOWN;
#endif
    pinMode(ch.pin, mode);
  }
  reset_generic_input_state();
  for (uint8_t i = 0; i < ofe_fanio::MAX_OUTPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.outputs[i];
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED) || ch.pin == ofe_fanio::PIN_UNUSED) continue;
    pinMode(ch.pin, OUTPUT);
  }
  io_output_mask &= ofe_fanio::enabledMask(hw_config.outputs, ofe_fanio::MAX_OUTPUTS);
  apply_generic_outputs();
  if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) {
    pinMode(hw_config.fan_tacho_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(hw_config.fan_tacho_pin), tacho_isr, FALLING);
  }
  dynamic_descriptor_dirty = true;
  return pwm_ok;
}

static void descriptor_append(const char* fmt, ...) {
  size_t used = strlen(dynamic_descriptor);
  if (used + 1 >= sizeof(dynamic_descriptor)) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dynamic_descriptor + used, sizeof(dynamic_descriptor) - used, fmt, ap);
  va_end(ap);
}

static const char* dynamic_descriptor_text() {
  if (!dynamic_descriptor_dirty && dynamic_descriptor[0]) return dynamic_descriptor;
  dynamic_descriptor[0] = 0;
  const bool fan_configured = fan_output_configured();
  uint8_t count = 0;
  for (uint8_t i = 0; i < ofe_fanio::MAX_INPUTS; ++i) if (hw_config.inputs[i].flags & ofe_fanio::CHANNEL_ENABLED) ++count;
  for (uint8_t i = 0; i < ofe_fanio::MAX_OUTPUTS; ++i) if (hw_config.outputs[i].flags & ofe_fanio::CHANNEL_ENABLED) ++count;
  if (fan_configured) {
    ++count; // main enable
    if (fan_uses_pwm()) ++count;
    if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) ++count;
  }
  descriptor_append("schema=1\nmodule=Fan/IO\nprofile=Hardware I/O\nstation=Local GPIO\nlocal_bus=GPIO\nprofile_entities=%u\nprofile_slots=32\nsystem_entities=master_builtin\nprofile_active=yes\nfan_enabled=%u\nfan_active_low=%u\nfan_output_mode=%u\nfan_enable_active_low=%u\nfan_pwm_active_low=%u\nfan_pwm_open_drain=%u\nfan_min_power=%u\nfan_max_power=%u\nfan_pwm_frequency_hz=%u\nentities:\n",
    count,
    fan_configured ? 1U : 0U,
    (hw_config.fan_enable_flags & ofe_fanio::FAN_ENABLE_ACTIVE_LOW) ? 1U : 0U,
    hw_config.fan_output_mode,
    (hw_config.fan_enable_flags & ofe_fanio::FAN_ENABLE_ACTIVE_LOW) ? 1U : 0U,
    (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_ACTIVE_LOW) ? 1U : 0U,
    (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_OPEN_DRAIN) ? 1U : 0U,
    hw_config.fan_min_power_percent, hw_config.fan_max_power_percent, fan_pwm_frequency_hz);
  for (uint8_t i = 0; i < ofe_fanio::MAX_INPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.inputs[i];
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED)) continue;
    descriptor_append("%u binary_sensor in%u ro source=profile access=ro role=main_input en=%s gpio=%u idx=%u active_low=%u pull=%s\n",
      20U + i, i + 1U, ch.name, ch.pin, i,
      (ch.flags & ofe_fanio::CHANNEL_ACTIVE_LOW) ? 1U : 0U,
      (ch.flags & ofe_fanio::CHANNEL_PULL_UP) ? "up" : ((ch.flags & ofe_fanio::CHANNEL_PULL_DOWN) ? "down" : "none"));
  }
  for (uint8_t i = 0; i < ofe_fanio::MAX_OUTPUTS; ++i) {
    const ofe_fanio::ChannelConfig& ch = hw_config.outputs[i];
    if (!(ch.flags & ofe_fanio::CHANNEL_ENABLED)) continue;
    descriptor_append("%u switch out%u rw source=profile access=rw role=output_enable en=%s gpio=%u idx=%u active_low=%u value_on=1 value_off=0\n",
      40U + i, i + 1U, ch.name, ch.pin, i, (ch.flags & ofe_fanio::CHANNEL_ACTIVE_LOW) ? 1U : 0U);
  }
  if (fan_configured) {
    descriptor_append("60 switch fan rw source=profile access=rw role=main_output_enable en=%s value_on=1 value_off=0 gpio=%u output_mode=%u active_low=%u\n",
      io_alias[4][0] ? io_alias[4] : "Luefter", fan_uses_relay() ? hw_config.fan_enable_pin : ofe_fanio::PIN_UNUSED,
      hw_config.fan_output_mode, (hw_config.fan_enable_flags & ofe_fanio::FAN_ENABLE_ACTIVE_LOW) ? 1U : 0U);
    if (fan_uses_pwm()) {
      descriptor_append("61 number power rw source=profile access=rw role=main_output_power en=Leistung unit=%% min=%u max=%u step=1 gpio=%u pwm_hz=%u active_low=%u drive=%s\n",
        hw_config.fan_min_power_percent, hw_config.fan_max_power_percent, hw_config.fan_pwm_pin, fan_pwm_frequency_hz,
        (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_ACTIVE_LOW) ? 1U : 0U,
        (hw_config.fan_pwm_flags & ofe_fanio::FAN_PWM_OPEN_DRAIN) ? "open_drain" : "push_pull");
    }
    if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) {
      descriptor_append("62 sensor rpm ro source=profile access=ro en=Drehzahl unit=rpm gpio=%u ppr=%u\n", hw_config.fan_tacho_pin, hw_config.tacho_pulses_per_rev);
    }
  }
  dynamic_descriptor_dirty = false;
  return dynamic_descriptor;
}

static uint32_t descriptor_hash(const char* text) {
  uint32_t hash = 2166136261UL;
  while (text && *text) { hash ^= (uint8_t)*text++; hash *= 16777619UL; }
  return hash;
}

static void handle_descriptor_get(const Frame& req) {
  const char* text = dynamic_descriptor_text();
  const size_t text_len = strlen(text);
  const uint8_t chunk_size = MAX_PAYLOAD - 8;
  const uint8_t chunks = (uint8_t)max((size_t)1, (text_len + chunk_size - 1) / chunk_size);
  const uint8_t chunk = req.len ? req.payload[0] : 0;
  if (chunk >= chunks) { send_status_response(req, STATUS_BAD_VALUE); return; }
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_DESCRIPTOR_GET | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK; resp.payload[o++] = 1;
  put_u32_le(resp.payload + o, descriptor_hash(text)); o += 4;
  resp.payload[o++] = chunk; resp.payload[o++] = chunks;
  const size_t offset = (size_t)chunk * chunk_size;
  const size_t n = min((size_t)chunk_size, text_len - offset);
  memcpy(resp.payload + o, text + offset, n); o += (uint8_t)n;
  resp.len = o; bus.send(resp);
}

static bool entity_append(uint8_t* payload, uint8_t& o, uint8_t id, uint32_t value) {
  if ((uint16_t)o + 8U > MAX_PAYLOAD) return false;
  payload[o++] = id; payload[o++] = 4; put_u16_le(payload + o, 0); o += 2; put_u32_le(payload + o, value); o += 4;
  return true;
}

static bool entity_append_bool(uint8_t* payload, uint8_t& o, uint8_t id, bool value) {
  if ((uint16_t)o + 5U > MAX_PAYLOAD) return false;
  payload[o++] = id;
  payload[o++] = 1;
  put_u16_le(payload + o, 0); o += 2;
  payload[o++] = value ? 1 : 0;
  return true;
}

static void handle_entity_get(const Frame& req) {
  update_generic_inputs();
  const uint8_t wanted = req.len ? req.payload[0] : 0;
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_ENTITY_GET | 0x80;
  uint8_t o = 0; resp.payload[o++] = STATUS_OK; const uint8_t cp = o++; uint8_t count = 0;
  for (uint8_t i = 0; i < ofe_fanio::MAX_INPUTS; ++i) if ((hw_config.inputs[i].flags & ofe_fanio::CHANNEL_ENABLED) && (!wanted || wanted == 20U + i)) count += entity_append_bool(resp.payload, o, 20U + i, ((io_input_mask >> i) & 1U) != 0);
  for (uint8_t i = 0; i < ofe_fanio::MAX_OUTPUTS; ++i) if ((hw_config.outputs[i].flags & ofe_fanio::CHANNEL_ENABLED) && (!wanted || wanted == 40U + i)) count += entity_append_bool(resp.payload, o, 40U + i, ((io_output_mask >> i) & 1U) != 0);
  if (fan_output_configured()) {
    if (!wanted || wanted == 60) count += entity_append_bool(resp.payload, o, 60, output_enabled);
    if (fan_uses_pwm() && (!wanted || wanted == 61)) count += entity_append(resp.payload, o, 61, output_power / 10U);
    if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED && (!wanted || wanted == 62)) count += entity_append(resp.payload, o, 62, fan_rpm);
  }
  resp.payload[cp] = count; resp.len = o; bus.send(resp);
}

static long entity_value(const Frame& req) {
  if (req.len < 3 || !req.payload[1]) return 0;
  const uint8_t value_len = req.payload[1];
  if (value_len == 1 && req.len >= 3) {
    const uint8_t raw = req.payload[2];
    // Entity commands can arrive as textual Web/Display values ("0"/"1")
    // or as the compact binary bool used by bus-side callers. Never interpret
    // ASCII '0' (0x30) as true simply because the byte itself is non-zero.
    if (raw == 0 || raw == '0') return 0;
    if (raw == 1 || raw == '1') return 1;
  }
  if (value_len == 4 && req.len >= 6) return (long)get_u32_le(req.payload + 2);
  char text[16];
  const uint8_t n = min((uint8_t)(sizeof(text) - 1), value_len);
  memcpy(text, req.payload + 2, n); text[n] = 0;
  return atol(text);
}

static void handle_entity_set(const Frame& req) {
  if (req.len < 2 || req.len != (uint8_t)(req.payload[1] + 2U)) { send_status_response(req, STATUS_BAD_LEN); return; }
  const uint8_t id = req.payload[0];
  const long value = entity_value(req);
  if (id >= 40 && id < 40 + ofe_fanio::MAX_OUTPUTS && (hw_config.outputs[id - 40].flags & ofe_fanio::CHANNEL_ENABLED)) {
    const uint16_t bit = (uint16_t)(1U << (id - 40));
    io_output_mask = value ? (io_output_mask | bit) : (io_output_mask & ~bit);
    apply_generic_outputs(); send_status_response(req, STATUS_OK); return;
  }
  if (id == 60 || id == 61) {
    if (!fan_output_configured()) { send_status_response(req, STATUS_NOT_SUPPORTED); return; }
    if (id == 60) {
      output_enabled = value != 0;
      apply_output();
      send_status_response(req, STATUS_OK);
      return;
    }
    if (!fan_uses_pwm()) { send_status_response(req, STATUS_NOT_SUPPORTED); return; }
    output_power = clamp_output_power_to_config((uint16_t)constrain(value, 1L, 100L) * 10U);
    remember_output_power(output_power);
    apply_output();
    send_status_response(req, STATUS_OK);
    return;
  }
  send_status_response(req, STATUS_NOT_SUPPORTED);
}

static Status commit_io_config_candidate(const ofe_fanio::HardwareConfig& candidate, uint16_t candidate_pwm_frequency_hz) {
  const bool pro_module = false;
  ofe_fanio::HardwareConfig next = candidate;
  next.checksum = ofe_fanio::checksum(next);
  if (!fan_pwm_frequency_valid(candidate_pwm_frequency_hz) || !ofe_fanio::validate(next, pro_module)) return STATUS_BAD_VALUE;

  // Preserve the complete live state so a failed LEDC attach or NVS write can
  // roll back without leaving a half-applied hardware map behind.
  const ofe_fanio::HardwareConfig old_config = hw_config;
  const uint16_t old_pwm_frequency_hz = fan_pwm_frequency_hz;
  const bool old_output_enabled = output_enabled;
  const uint16_t old_output_power = output_power;
  const uint16_t old_manual_output_power = manual_output_power;
  const uint16_t old_io_output_mask = io_output_mask;

  output_off();
  if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) detachInterrupt(digitalPinToInterrupt(hw_config.fan_tacho_pin));
  hw_config = next;
  fan_pwm_frequency_hz = candidate_pwm_frequency_hz;
  manual_output_power = clamp_output_power_to_config(manual_output_power);
  output_power = clamp_output_power_to_config(output_power);

  // This is a real hardware preflight. If LEDC cannot represent/attach the
  // requested PWM configuration, do not persist it and restore the old map.
  if (!configure_dynamic_hardware(false)) {
    if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) detachInterrupt(digitalPinToInterrupt(hw_config.fan_tacho_pin));
    fan_pwm_detach();
    hw_config = old_config;
    fan_pwm_frequency_hz = old_pwm_frequency_hz;
    manual_output_power = old_manual_output_power;
    output_power = old_output_power;
    io_output_mask = old_io_output_mask;
    output_enabled = old_output_enabled;
    configure_dynamic_hardware(false);
    apply_generic_outputs();
    apply_output();
    return STATUS_BAD_VALUE;
  }

  ofe_fanio::HardwareConfig stored = hw_config;
  const bool config_saved = ofe_fanio::save(prefs, stored, pro_module);
  const bool frequency_saved = config_saved && prefs.putUInt("pwm_freq", fan_pwm_frequency_hz) == sizeof(uint32_t);
  if (!frequency_saved) {
    // NVS is not transactional across keys. Restore both old values best-effort
    // before returning an error, while the active RAM/hardware state is also
    // rolled back below.
    ofe_fanio::HardwareConfig rollback_store = old_config;
    ofe_fanio::save(prefs, rollback_store, pro_module);
    prefs.putUInt("pwm_freq", old_pwm_frequency_hz);

    if (hw_config.fan_tacho_pin != ofe_fanio::PIN_UNUSED) detachInterrupt(digitalPinToInterrupt(hw_config.fan_tacho_pin));
    fan_pwm_detach();
    hw_config = old_config;
    fan_pwm_frequency_hz = old_pwm_frequency_hz;
    manual_output_power = old_manual_output_power;
    output_power = old_output_power;
    io_output_mask = old_io_output_mask;
    output_enabled = old_output_enabled;
    configure_dynamic_hardware(false);
    apply_generic_outputs();
    apply_output();
    return STATUS_BUSY;
  }

  // Deliberately leave outputs OFF after a hardware-map change, matching the
  // previous editor behaviour. The next master synchronization enables the
  // selected main output again if required.
  output_enabled = false;
  io_output_mask = 0;
  apply_output();
  apply_generic_outputs();
  dynamic_descriptor_dirty = true;
  return STATUS_OK;
}

static void handle_io_config(const Frame& req) {
  if (!req.len) { send_status_response(req, STATUS_BAD_LEN); return; }
  const bool pro_module = false;
  const uint8_t action = req.payload[0];

  if (action == IO_CONFIG_BEGIN) {
    if (req.len != 1) { send_status_response(req, STATUS_BAD_LEN); return; }
    io_config_pending = hw_config;
    io_config_pending_pwm_frequency_hz = fan_pwm_frequency_hz;
    io_config_transaction_active = true;
    io_config_transaction_last_ms = millis();
    send_status_response(req, STATUS_OK);
    return;
  }
  if (action == IO_CONFIG_ABORT) {
    if (req.len != 1) { send_status_response(req, STATUS_BAD_LEN); return; }
    io_config_transaction_active = false;
    send_status_response(req, STATUS_OK);
    return;
  }
  if (action == IO_CONFIG_COMMIT) {
    if (req.len != 1) { send_status_response(req, STATUS_BAD_LEN); return; }
    if (!io_config_transaction_active) { send_status_response(req, STATUS_BAD_VALUE); return; }
    const Status status = commit_io_config_candidate(io_config_pending, io_config_pending_pwm_frequency_hz);
    if (status == STATUS_OK) io_config_transaction_active = false;
    io_config_transaction_last_ms = millis();
    send_status_response(req, status);
    return;
  }

  ofe_fanio::HardwareConfig next = io_config_transaction_active ? io_config_pending : hw_config;
  uint16_t next_pwm_frequency_hz = io_config_transaction_active ? io_config_pending_pwm_frequency_hz : fan_pwm_frequency_hz;

  if (action == IO_CONFIG_CHANNEL) {
    if (req.len < 7) { send_status_response(req, STATUS_BAD_LEN); return; }
    const bool output = req.payload[1] != 0;
    const uint8_t index = req.payload[2];
    if (index >= (output ? ofe_fanio::MAX_OUTPUTS : ofe_fanio::MAX_INPUTS)) { send_status_response(req, STATUS_BAD_VALUE); return; }
    ofe_fanio::ChannelConfig& ch = output ? next.outputs[index] : next.inputs[index];
    ch.pin = req.payload[4];
    ch.flags = req.payload[3] ? ofe_fanio::CHANNEL_ENABLED : 0;
    if (req.payload[5]) ch.flags |= ofe_fanio::CHANNEL_ACTIVE_LOW;
    if (!output && req.payload[6] == 1) ch.flags |= ofe_fanio::CHANNEL_PULL_UP;
    if (!output && req.payload[6] == 2) ch.flags |= ofe_fanio::CHANNEL_PULL_DOWN;
    if (req.len > 7) { char label[20]; const uint8_t n = min((uint8_t)19, (uint8_t)(req.len - 7)); memcpy(label, req.payload + 7, n); label[n] = 0; ofe_fanio::copyName(ch.name, label); }
    if (!ch.name[0]) {
      char fallback[8];
      snprintf(fallback, sizeof(fallback), "%s%u", output ? "OUT" : "IN", (unsigned)index + 1U);
      ofe_fanio::copyName(ch.name, fallback);
    }
  } else if (action == IO_CONFIG_FAN) {
    if (req.len != 7 && req.len != 9 && req.len != 14) { send_status_response(req, STATUS_BAD_LEN); return; }
    next.fan_enable_pin = req.payload[1]; next.fan_pwm_pin = req.payload[2]; next.fan_tacho_pin = req.payload[3];
    next.fan_flags = req.payload[4] ? ofe_fanio::CHANNEL_ENABLED : 0;
    next.tacho_pulses_per_rev = req.payload[6];
    const bool legacy_active_low = req.payload[5] != 0;
    next.fan_output_mode = ofe_fanio::inferFanOutputMode(next.fan_enable_pin, next.fan_pwm_pin);
    next.fan_enable_flags = legacy_active_low ? ofe_fanio::FAN_ENABLE_ACTIVE_LOW : 0;
    next.fan_pwm_flags = (next.fan_pwm_flags & ofe_fanio::FAN_PWM_OPEN_DRAIN) |
      (legacy_active_low ? ofe_fanio::FAN_PWM_ACTIVE_LOW : 0);
    if (req.len >= 9) {
      const uint16_t requested_pwm_frequency_hz = get_u16_le(req.payload + 7);
      if (!fan_pwm_frequency_valid(requested_pwm_frequency_hz)) { send_status_response(req, STATUS_BAD_VALUE); return; }
      next_pwm_frequency_hz = requested_pwm_frequency_hz;
    }
    if (req.len == 14) {
      next.fan_output_mode = req.payload[9];
      next.fan_enable_flags = req.payload[10] ? ofe_fanio::FAN_ENABLE_ACTIVE_LOW : 0;
      next.fan_pwm_flags = req.payload[11] & (ofe_fanio::FAN_PWM_ACTIVE_LOW | ofe_fanio::FAN_PWM_OPEN_DRAIN);
      next.fan_min_power_percent = req.payload[12];
      next.fan_max_power_percent = req.payload[13];
    }
  } else if (action == IO_CONFIG_FILTER) {
    send_status_response(req, STATUS_NOT_SUPPORTED); return;
  } else if (action == IO_CONFIG_RESET) {
    ofe_fanio::defaults(next, pro_module);
    next_pwm_frequency_hz = FAN_PWM_DEFAULT_FREQUENCY_HZ;
  } else { send_status_response(req, STATUS_NOT_SUPPORTED); return; }

  next.checksum = ofe_fanio::checksum(next);
  if (io_config_transaction_active) {
    // Intermediate transaction states may temporarily reuse a GPIO that will
    // be moved by a later CHANNEL write. Full cross-channel validation belongs
    // to COMMIT, when the final map is complete.
    io_config_pending = next;
    io_config_pending_pwm_frequency_hz = next_pwm_frequency_hz;
    io_config_transaction_last_ms = millis();
    send_status_response(req, STATUS_OK);
    return;
  }

  const Status status = commit_io_config_candidate(next, next_pwm_frequency_hz);
  send_status_response(req, status);
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

static const char* current_io_alias(uint8_t ch) {
  if (ch < 2) return hw_config.inputs[ch].name;
  if (ch < 4) return hw_config.outputs[ch - 2].name;
  return io_alias[4];
}

static uint8_t io_alias_len(uint8_t ch) {
  const char* alias = current_io_alias(ch);
  uint8_t n = 0;
  while (n < 18 && alias[n]) ++n;
  return n;
}

static void append_io_aliases(uint8_t* payload, uint8_t& o) {
  for (uint8_t ch = 0; ch < 5; ++ch) {
    const uint8_t n = io_alias_len(ch);
    payload[o++] = n;
    if (n) {
      memcpy(payload + o, current_io_alias(ch), n);
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
  if (ch < 2) {
    ofe_fanio::copyName(hw_config.inputs[ch].name, io_alias[ch][0] ? io_alias[ch] : (ch ? "IN2" : "IN1"));
    ok = ofe_fanio::save(prefs, hw_config, false) && ok;
    dynamic_descriptor_dirty = true;
  } else if (ch < 4) {
    const uint8_t index = ch - 2;
    ofe_fanio::copyName(hw_config.outputs[index].name, io_alias[ch][0] ? io_alias[ch] : (index ? "OUT2" : "OUT1"));
    ok = ofe_fanio::save(prefs, hw_config, false) && ok;
    dynamic_descriptor_dirty = true;
  }
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
  // - compact live polls may request a rising-edge latch in bytes 7..8
  // - scan/config sends IO_QUERY_INCLUDE_ALIASES -> include all aliases
  const bool include_aliases =
    req.len == 0 || (req.payload[0] & IO_QUERY_INCLUDE_ALIASES) != 0;
  const bool include_events =
    req.len > 0 && (req.payload[0] & IO_QUERY_INCLUDE_EVENTS) != 0;
  uint16_t delivered_events = 0;
  if (include_aliases) {
    append_io_aliases(resp.payload, o);
  } else if (include_events) {
    delivered_events = io_input_event_mask;
    put_u16_le(resp.payload + o, delivered_events); o += 2;
  }

  resp.len = o;
  bus.send(resp);
  if (delivered_events) io_input_event_mask &= (uint16_t)~delivered_events;
}

static void handle_set_io(const Frame& req) {
  if (req.len != 4) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  const uint16_t mask = get_u16_le(req.payload);
  const uint16_t value = get_u16_le(req.payload + 2);
  io_output_mask = (io_output_mask & ~mask) | (value & mask);
  io_output_mask &= ofe_fanio::enabledMask(hw_config.outputs, ofe_fanio::MAX_OUTPUTS);
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
  resp.payload[o++] = MODULE_FAN_IO;
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

  if (ofe_handle_power_save_command(req, bus, module_addr, ofe_status_leds,
                                    false,
                                    module_eco_mode, module_light_sleep_armed)) return;

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

    case CMD_IO_CONFIG:
      handle_io_config(req);
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

    case CMD_SET_LABEL:
      handle_set_label(req);
      break;

    case CMD_SET_ENABLE:
      if (req.len != 1) {
        send_status_response(req, STATUS_BAD_LEN);
        break;
      }
      if (!fan_output_configured()) {
        send_status_response(req, STATUS_NOT_SUPPORTED);
        break;
      }
      output_enabled = req.payload[0] != 0;
      // Keep the last slider value visible while the relay/PWM output is off.
      // Only the enable flag decides whether the fan actually runs.
      output_power = clamp_output_power_to_config(output_power < 10 ? manual_output_power : output_power);
      apply_output();
      send_status_response(req, STATUS_OK);
      break;

    case CMD_SET_POWER:
      if (req.len != 2) {
        send_status_response(req, STATUS_BAD_LEN);
        break;
      }
      if (!fan_output_configured()) {
        send_status_response(req, STATUS_NOT_SUPPORTED);
        break;
      }
      {
        const uint16_t requested_power = get_u16_le(req.payload);
        if (requested_power == 0) {
          // Zero remains the legacy OFF/idle sync value.
          output_enabled = false;
          output_power = clamp_output_power_to_config(manual_output_power);
        } else if (fan_uses_pwm()) {
          output_power = clamp_output_power_to_config(requested_power);
          remember_output_power(output_power);
        } else {
          // Relay-only hardware has no variable power stage.
          output_power = 1000U;
        }
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
        if (prefs.putUChar("addr", next_addr) != sizeof(uint8_t)) {
          send_status_response(req, STATUS_BUSY);
          break;
        }
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
      io_config_transaction_active = false;
      ofe_fanio::defaults(hw_config, false);
      fan_pwm_frequency_hz = FAN_PWM_DEFAULT_FREQUENCY_HZ;
      ofe_fanio::save(prefs, hw_config, false);
      prefs.putUInt("pwm_freq", fan_pwm_frequency_hz);
      configure_dynamic_hardware(false);
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
  const bool loaded_hw_config = ofe_fanio::load(prefs, hw_config, false);
  if (!loaded_hw_config) {
    if (io_alias[0][0]) ofe_fanio::copyName(hw_config.inputs[0].name, io_alias[0]);
    if (io_alias[1][0]) ofe_fanio::copyName(hw_config.inputs[1].name, io_alias[1]);
    if (io_alias[2][0]) ofe_fanio::copyName(hw_config.outputs[0].name, io_alias[2]);
    if (io_alias[3][0]) ofe_fanio::copyName(hw_config.outputs[1].name, io_alias[3]);
    ofe_fanio::save(prefs, hw_config, false);
  }
  manual_output_power = clamp_output_power_to_config(manual_output_power);
  output_power = manual_output_power;
  const uint32_t stored_pwm_frequency_hz = prefs.getUInt("pwm_freq", FAN_PWM_DEFAULT_FREQUENCY_HZ);
  fan_pwm_frequency_hz = fan_pwm_frequency_valid(stored_pwm_frequency_hz)
    ? (uint16_t)stored_pwm_frequency_hz
    : (uint16_t)FAN_PWM_DEFAULT_FREQUENCY_HZ;
  if (stored_pwm_frequency_hz != fan_pwm_frequency_hz) prefs.putUInt("pwm_freq", fan_pwm_frequency_hz);
  configure_dynamic_hardware(false);

  Serial.println("Fan Output RS485 module");
  Serial.print("addr=0x");
  if (module_addr < 0x10) Serial.print('0');
  Serial.println(module_addr, HEX);
  join_announce_left = 8;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
}

void loop() {
  if (io_config_transaction_active &&
      (uint32_t)(millis() - io_config_transaction_last_ms) > IO_CONFIG_TRANSACTION_TIMEOUT_MS) {
    io_config_transaction_active = false;
  }
  ofe_status_leds.setBusOnline(last_master_ms && (uint32_t)(millis() - last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  ofe_status_leds.setModuleEvent(fault_mask ? OFE_LED_EVENT_CRITICAL : (output_enabled ? OFE_LED_EVENT_EXTRACTOR_ON : OFE_LED_EVENT_OFF));
  ofe_status_leds.tick();
  const uint32_t loop_start_us = micros();
  update_rpm();
  update_generic_inputs();
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
