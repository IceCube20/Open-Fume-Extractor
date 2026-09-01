// Forward declarations kept inside this .ino so Arduino's auto-prototype
// generator knows these custom types before helper prototypes are emitted.
struct DisplayModuleSummary;
struct DisplayModuleDetail;
struct DisplayJbcUsbCore;
struct DisplayUniversalEntity;
struct DisplayUniversalModuleCache;
struct DisplayUniversalControlPending;

#include <Arduino.h>
#if defined(OFE_REQUIRE_RGB_HIGH_PERF_SDK) && \
    (!defined(CONFIG_ESP32S3_DATA_CACHE_LINE_64B) || !defined(CONFIG_SPIRAM_XIP_FROM_PSRAM))
#error "The OFE RGB board requires the 64-byte DCache / PSRAM XIP high-performance SDK"
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <Update.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_idf_version.h>
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif
#define OFE_PSRAM_BSS_ATTR EXT_RAM_BSS_ATTR
#include <string.h>
#include <math.h>
#include <new>
#ifndef OFE_STATUS_LED_ENABLE
#define OFE_STATUS_LED_ENABLE 1
#endif

#ifndef OFE_STATUS_LED_PIN
#define OFE_STATUS_LED_PIN -1
#endif

#include "src/Rs485PeripheralBus.h"
#include "src/OfeDisplayWifi.h"
#ifndef OFE_STATUS_LED_MASTER_TIMEOUT_MS
#define OFE_STATUS_LED_MASTER_TIMEOUT_MS 8000UL
#endif

#include "src/OfeStatusLed.h"


#include <Arduino_GFX_Library.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp32s3/rom/cache.h>
#include <lvgl.h>
#include "ofe_lv_profiler.h"
#include "src/OfeRgbTileCopy.h"
static_assert(LV_MEM_SIZE == 64U * 1024U, "Use the bundled build_opt.h and ofe_lv_conf.h for the RGB display");
static_assert(LV_OBJ_STYLE_CACHE == 1, "The RGB display requires the bundled LVGL style-cache configuration");
#include "SolderIronIcon.h"
#include "src/OfeSerialPortFont.h"

// Eigene LVGL-Montserrat-Medium-Fonts mit deutschen Umlauten.
// LVGL built-in Montserrat fonts do not include German umlauts.
LV_FONT_DECLARE(openfume_montserrat_14_de);
LV_FONT_DECLARE(openfume_montserrat_18_de);
LV_FONT_DECLARE(openfume_montserrat_22_de);
LV_FONT_DECLARE(openfume_montserrat_28_de);

#define UI_FONT_14 (&openfume_montserrat_14_de)
#define UI_FONT_18 (&openfume_montserrat_18_de)
#define UI_FONT_22 (&openfume_montserrat_22_de)
#define UI_FONT_28 (&openfume_montserrat_28_de)
#define UI_FONT_DEFAULT UI_FONT_14
using namespace jbc_rs485;

static String module_summary_addr_name(uint8_t addr);
static const char* fan_io_alias_for(uint8_t addr, uint8_t slot, const char* fallback);

#include "OpenFumeBootLogo.h"
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 18
#endif

#ifndef RS485_TX_PIN
#define RS485_TX_PIN 17
#endif

#ifndef RS485_BAUD
#define RS485_BAUD 250000 // 230400 Standart
#endif

#ifndef BACKLIGHT_PIN
#define BACKLIGHT_PIN 2
#endif

#ifndef BACKLIGHT_PWM_FREQ
#define BACKLIGHT_PWM_FREQ 1220
#endif

#ifndef BACKLIGHT_PWM_ENABLE
#define BACKLIGHT_PWM_ENABLE 1
#endif

#ifndef BACKLIGHT_PWM_BITS
#define BACKLIGHT_PWM_BITS 12
#endif

#ifndef BACKLIGHT_PWM_CHANNEL
#define BACKLIGHT_PWM_CHANNEL 0
#endif

#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 2
#endif

#ifndef DISPLAY_RGB_WIDTH
#define DISPLAY_RGB_WIDTH 800
#endif

#ifndef DISPLAY_RGB_HEIGHT
#define DISPLAY_RGB_HEIGHT 480
#endif

#ifndef DISPLAY_RGB_BOUNCE_BUFFER_LINES
// RGB scanout liest den Framebuffer aus PSRAM. Ein Bounce-Buffer in internem RAM
// stabilisiert den DMA, wenn LVGL/RS485 parallel Speicherbandbreite brauchen.
// 20 Zeilen = 2 * 800 * 20 * 2 = 64.000 Bytes internes RAM für beide Bounce-Buffer.
// Espressif recommends increasing the bounce buffer when drift persists.
// The LCD is initialized before application caches/LVGL so this DMA-critical SRAM is reserved first.
#define DISPLAY_RGB_BOUNCE_BUFFER_LINES 20
#endif
#ifndef DISPLAY_RGB_INVERT_COLORS
#define DISPLAY_RGB_INVERT_COLORS 0
#endif

#ifndef DISPLAY_RGB_PCLK_HZ
// Exact pixel clock used by known JC8048W550 Arduino_GFX configurations.
// Keep porches/polarities unchanged; only use the board's documented 16 MHz PCLK.
#define DISPLAY_RGB_PCLK_HZ 16000000
#endif

#ifndef DISPLAY_FAST_UI
// 1 = weniger Schatten/Gradienten, dadurch deutlich weniger LVGL-Renderlast.
#define DISPLAY_FAST_UI 1
#endif

#ifndef LVGL_CANVAS_FLUSH_MIN_INTERVAL_MS
// JC8048W550 nutzt ein RGB565/DPI-Panel. Wir zeichnen LVGL direkt in das RGB-Display;
// der eigentliche RGB-Framebuffer liegt bereits im PSRAM. Der LVGL-Drawbuffer bleibt
// deshalb bevorzugt im internen RAM, damit Scrollen und RGB-DMA nicht um PSRAM-Bandbreite kaempfen.
// Kept for compatibility; the current direct RGB framebuffer path does not throttle here.
#define LVGL_CANVAS_FLUSH_MIN_INTERVAL_MS 16
#endif

#ifndef LVGL_DRAW_BUFFER_USE_PSRAM
// Beim 800x480-RGB-Panel braucht der RGB-Bounce schnellen internen RAM.
// PSRAM als primaerer LVGL-Drawbuffer ist langsam und kann RGB-DMA-Artefakte provozieren.
#define LVGL_DRAW_BUFFER_USE_PSRAM 0
#endif
#ifndef LVGL_DRAW_BUFFER_LINES_FAST
// Lossless performance target:
// one 48-line RGB565 LVGL tile = 76,800 bytes in INTERNAL SRAM.
// The old equal-sized rotation scratch buffer is gone; rotation is done in-place.
#define LVGL_DRAW_BUFFER_LINES_FAST 48
#endif
#ifndef DISPLAY_LVGL_LARGE_TILES
// 1.3.60 hardware comparison: PSRAM blending/copies outweighed fewer redraws.
// Keep SRAM rendering by default, including on the high-performance SDK.
// The larger-tile experiment is retained only for explicit A/B builds.
#define DISPLAY_LVGL_LARGE_TILES 0
#endif
#ifndef DISPLAY_LVGL_FULL_REFRESH
// Diagnostic/stability mode for JC8048W550: render a complete frame into a
// backbuffer and commit it in one flush. This avoids many small partial writes
// into the visible PSRAM framebuffer while the RGB engine scans it out.
#define DISPLAY_LVGL_FULL_REFRESH 0
#endif
#ifndef DISPLAY_LVGL_HANDLER_INTERVAL_MS
// lv_timer_handler() service cadence. The actual display/input timers are tuned
// separately below; calling the handler frequently does NOT by itself change
// LVGL's default refresh/input periods.
#define DISPLAY_LVGL_HANDLER_INTERVAL_MS 5
#endif

#ifndef DISPLAY_LVGL_REFRESH_PERIOD_MS
// Target UI refresh cadence. The JC8048W550 at 16 MHz RGB PCLK is ~39 Hz
// physically, so 16 ms lets LVGL present updates as soon as the next scan permits.
#define DISPLAY_LVGL_REFRESH_PERIOD_MS 16
#endif

#ifndef DISPLAY_TOUCH_READ_PERIOD_MS
// GT911 sampling. The old build inherited LVGL's default input timer period,
// making drag tracking feel much slower even though loop() called lv_timer_handler()
// every 5 ms.
#define DISPLAY_TOUCH_READ_PERIOD_MS 5
#endif

#ifndef DISPLAY_SCROLL_LIMIT_PX
// Start dragging after only a few pixels of finger travel.
#define DISPLAY_SCROLL_LIMIT_PX 3
#endif

#ifndef DISPLAY_SCROLL_THROW_PCT
// LVGL: larger = faster slow-down. 4% gives a useful inertial flick on this UI.
#define DISPLAY_SCROLL_THROW_PCT 4
#endif
#ifndef DISPLAY_PANEL_STATIC_TEST
// Temporary hardware diagnostic: draw one static screen and keep LVGL/RS485 off.
#define DISPLAY_PANEL_STATIC_TEST 0
#endif
#ifndef DISPLAY_BACKGROUND_CACHE_ENABLED
// The 800x480 RGB panel is sensitive to periodic PSRAM/bus bursts while scanning out.
// Visible module pages still request fresh data; background cache is disabled by default.
#define DISPLAY_BACKGROUND_CACHE_ENABLED 0
#endif

#ifndef LVGL_DRAW_BUFFER_ALLOW_PSRAM_FALLBACK
// 0 = LVGL-Drawbuffer bleibt im schnellen internen RAM.
// 1 = nur als Notfall-Fallback PSRAM verwenden, wenn interner RAM nicht reicht.
#define LVGL_DRAW_BUFFER_ALLOW_PSRAM_FALLBACK 0
#endif

#ifndef DISPLAY_RS485_DEDICATED_TASK
// 1 = RS485-Protokoll laeuft in einem eigenen FreeRTOS-Task auf Core 0.
// Dadurch bleibt der Master-Link auch waehrend langsamer Full-Frame-Display-Flushes erreichbar.
#define DISPLAY_RS485_DEDICATED_TASK 1
#endif

#ifndef DISPLAY_RS485_TASK_CORE
#define DISPLAY_RS485_TASK_CORE 0
#endif

#ifndef DISPLAY_RS485_TASK_PRIORITY
// Niedriger als v12: Der Bus bleibt separat, nimmt dem UI beim Scrollen aber weniger Zeit weg.
#define DISPLAY_RS485_TASK_PRIORITY 1
#endif

#ifndef DISPLAY_RS485_TASK_STACK
#define DISPLAY_RS485_TASK_STACK 8192
#endif

#ifndef DISPLAY_RS485_IDLE_DELAY_MS
// Wenn keine UART-Daten anliegen, darf der RS485-Task kurz schlafen.
// Das entlastet den zweiten Core und macht Touch/Scroll fluessiger.
#define DISPLAY_RS485_IDLE_DELAY_MS 3
#endif

#ifndef DISPLAY_STATUS_UI_MIN_INTERVAL_MS
// Live telemetry refresh. The old 500-ms value hard-limited normal status UI
// updates to ~2 FPS, which is exactly what the profiler showed.
// 100 ms gives up to 10 Hz while still coalescing fast RS485 traffic.
#define DISPLAY_STATUS_UI_MIN_INTERVAL_MS 100
#endif
#ifndef DISPLAY_RS485_ACTIVE_YIELD_MS
// Nach empfangenen Frames kurz schlafen, damit RGB-DMA/LVGL beim Scrollen Luft bekommt.
#define DISPLAY_RS485_ACTIVE_YIELD_MS 2
#endif

#ifndef DISPLAY_RS485_PERIODIC_MS
// Join/Discovery/Housekeeping muessen nicht in jedem 1-ms-Durchlauf laufen.
#define DISPLAY_RS485_PERIODIC_MS 5
#endif


#ifndef LV_GRAD_DIR_NONE
#define LV_GRAD_DIR_NONE ((lv_grad_dir_t)0)
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

#ifndef TOUCH_SDA_PIN
#define TOUCH_SDA_PIN 19
#endif

#ifndef TOUCH_SCL_PIN
#define TOUCH_SCL_PIN 20
#endif

#ifndef TOUCH_I2C_CLOCK
#define TOUCH_I2C_CLOCK 400000
#endif

#ifndef TOUCH_RST_PIN
#define TOUCH_RST_PIN 38
#endif

#ifndef TOUCH_INT_PIN
#define TOUCH_INT_PIN -1
#endif

#define GT911_ADDR_PRIMARY 0x5D
#define GT911_ADDR_SECONDARY 0x14
static uint8_t touch_addr = GT911_ADDR_PRIMARY;

static const uint16_t HW_VERSION = 0x0100;
#ifndef OFE_STR_HELPER
#define OFE_STR_HELPER(x) #x
#define OFE_STR(x) OFE_STR_HELPER(x)
#endif

#define OFE_MODULE_FW_MAJOR 1
#define OFE_MODULE_FW_MINOR 3
#define OFE_MODULE_FW_PATCH 64
#define OFE_MODULE_FW_SUFFIX "beta"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=DISPLAY_800X480;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}

static HardwareSerial RS485(1);
static Link bus(RS485);
static OfeDisplayWifi display_wifi;
static Preferences prefs;
static OfeStatusLed ofe_status_leds;

static const uint8_t DEFAULT_MODULE_ADDR = 0x40;
static uint8_t module_addr = DEFAULT_MODULE_ADDR;
static char module_label[24] = {0};
static bool fw_update_active = false;
static uint32_t fw_update_last_ms = 0;
static uint32_t fw_update_started_ms = 0;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;
static const uint32_t MASTER_LINK_TIMEOUT_MS = 5000UL;

// UI state for firmware updates of OTHER modules on the RS485 bus.
// Other module updates are now shown inline in the module list instead of
// taking over the whole display. The watchdog only removes stale row status.
static uint32_t bus_update_last_ms = 0;
static uint32_t bus_update_done_ms = 0;
// Only used for LOCAL display update recovery now. Foreign module updates are
// kept until an explicit inactive packet or a sniffed FW_END arrives.
static const uint32_t BUS_UPDATE_UI_TIMEOUT_MS = 12000UL;
static const uint32_t BUS_UPDATE_DONE_HOLD_MS = 5000UL;

// Passive progress tracker: the display can see FW_BEGIN/FW_CHUNK/FW_END frames
// for other modules on the RS485 bus. Some master DISPLAY_UPDATE packets only
// carry "busy / 0%", so we calculate the real percentage from chunk offsets.
static bool bus_update_snoop_active = false;
static uint8_t bus_update_snoop_target = 0;
static uint32_t bus_update_snoop_size = 0;
static uint32_t bus_update_snoop_offset = 0;
static uint32_t bus_update_speed_bps = 0;
static uint32_t bus_update_speed_sample_ms = 0;
static uint32_t bus_update_speed_sample_offset = 0;
static uint8_t bus_update_last_ui_progress = 255;
static uint32_t bus_update_last_ui_ms = 0;
static bool bus_update_synthetic_module_active = false;
static uint8_t bus_update_synthetic_module_addr = 0;
#ifndef BUS_UPDATE_AUTO_OPEN_MODULE_LIST
#define BUS_UPDATE_AUTO_OPEN_MODULE_LIST 1
#endif
#ifndef BUS_UPDATE_AUTO_SCROLL_TO_MODULE
#define BUS_UPDATE_AUTO_SCROLL_TO_MODULE 1
#endif
static bool bus_update_auto_nav_done = false;
static uint8_t bus_update_auto_nav_target = 0;
static bool bus_update_auto_return_pending = false;
static uint8_t bus_update_auto_return_view = 0;
static uint8_t bus_update_auto_return_detail_addr = 0;
static uint8_t bus_update_last_scrolled_target = 0;
static uint32_t bus_update_last_scroll_ms = 0;
static uint32_t module_list_cache_last_request_ms = 0;
static uint8_t module_list_cache_request_start = 0;
static const uint32_t MODULE_LIST_CACHE_REQUEST_MS = 1000UL;
static uint32_t module_detail_cache_last_request_ms = 0;
static uint8_t module_detail_cache_phase = 0;
static const uint32_t MODULE_DETAIL_CACHE_REQUEST_MS = 650UL;
static const uint32_t HOME_DETAIL_CACHE_VALID_MS = 10000UL;

// Lightweight Home-only cache maintenance.
// Module list refresh is event-driven (boot / presence / module-count change).
// Fan/IO detail is then refreshed slowly so its independent OUT1/OUT2 state is
// known even when another module is the selected main output.
static bool home_module_list_refresh_pending = true;
static uint32_t home_fan_detail_request_ms = 0;
static const uint32_t HOME_FAN_DETAIL_REQUEST_MS = 1500UL;

struct HomeWellerDetailCache {
  bool valid = false;
  uint8_t addr = 0;
  bool connected = false;
  uint8_t speed = 0;
  uint8_t filter = 0;
  uint16_t runtime = 0;
  uint16_t programmed = 0;
  uint16_t rpm = 0;
  uint16_t version = 0;
  uint8_t light = 0;
  uint16_t io_outputs = 0;
  uint16_t uart_age = 0xFFFF;
  uint32_t last_ms = 0;
};

struct HomeFanIoDetailCache {
  bool valid = false;
  uint8_t addr = 0;
  bool online = false;
  bool relay_style = false;
  uint16_t inputs = 0;
  uint16_t outputs = 0;
  uint16_t faults = 0;
  bool output_enabled = false;
  uint16_t output_power = 0;
  uint16_t output_rpm = 0;
  uint16_t output_fault = 0;
  uint16_t filter_saturation_permille = 0;
  int16_t filter_pressure_raw = 0;
  int16_t filter_zero_raw = 0;
  int16_t filter_clean_raw = 0;
  int16_t filter_full_raw = 0;
  char io_main_alias[19] = {0};
  char io_in1_alias[19] = {0};
  char io_in2_alias[19] = {0};
  char io_out1_alias[19] = {0};
  char io_out2_alias[19] = {0};
  uint32_t last_ms = 0;
};

static HomeWellerDetailCache home_weller_cache;
static HomeFanIoDetailCache home_fan_io_cache;

static std::atomic<uint32_t> fw_update_reboot_ms{0};
static void fw_update_abort_local() {
  if (fw_update_active) Update.abort();
  fw_update_active = false;
  display_wifi.finishUpdate();
  fw_update_started_ms = 0;
}

static void fw_update_touch() {
  fw_update_last_ms = millis();
}

static void fw_update_check_timeout() {
  const uint32_t committed_ms = fw_update_reboot_ms.load();
  if (committed_ms && (uint32_t)(millis() - committed_ms) >= 12000UL) ESP.restart();
  if (fw_update_active && (uint32_t)(millis() - fw_update_last_ms) > FW_UPDATE_TIMEOUT_MS) {
    fw_update_abort_local();
  }
}
static uint32_t fw_update_offset = 0;
static uint32_t fw_update_size = 0;
#ifndef FW_UPDATE_WRITE_BUFFER_SIZE
#define FW_UPDATE_WRITE_BUFFER_SIZE 4096
#endif
static uint8_t* fw_update_write_buffer = nullptr;
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
static uint8_t fw_update_last_draw_percent = 255;
static uint8_t fw_update_last_draw_target = 255;
static uint32_t fw_update_last_draw_ms = 0;
static uint32_t last_master_ms = 0;
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
// Lightweight CPU-load sampler.
// The old implementation called uxTaskGetSystemState() every second, which walks
// all tasks and can disturb the latency-sensitive RGB bounce-buffer refill path.
// We only inspect the two IDLE tasks and compare their run-time counters against
// the same FreeRTOS run-time counter source.
static configRUN_TIME_COUNTER_TYPE cpu_prev_total = 0;
static configRUN_TIME_COUNTER_TYPE cpu_prev_idle[configNUMBER_OF_CORES] = {};
static bool cpu_runtime_valid = false;

static TaskHandle_t cpu_idle_task_handle(BaseType_t core) {
#if defined(ESP_IDF_VERSION_MAJOR) && \
    ((ESP_IDF_VERSION_MAJOR > 5) || (ESP_IDF_VERSION_MAJOR == 5 && ESP_IDF_VERSION_MINOR >= 2))
  return xTaskGetIdleTaskHandleForCore(core);
#else
  return xTaskGetIdleTaskHandleForCPU(core);
#endif
}

static configRUN_TIME_COUNTER_TYPE cpu_runtime_counter_now() {
#if defined(portGET_RUN_TIME_COUNTER_VALUE)
  return (configRUN_TIME_COUNTER_TYPE)portGET_RUN_TIME_COUNTER_VALUE();
#else
  // Arduino-ESP32 normally uses the 1-MHz ESP timer for FreeRTOS runtime stats.
  // This fallback is lock-free and very cheap.
  return (configRUN_TIME_COUNTER_TYPE)esp_timer_get_time();
#endif
}

static void sample_cpu_load() {
#if defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1)
  configRUN_TIME_COUNTER_TYPE idle_now[configNUMBER_OF_CORES] = {};
  uint64_t idle_delta_sum = 0;

  for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core) {
    TaskHandle_t idle = cpu_idle_task_handle(core);
    if (!idle) return;

    TaskStatus_t info = {};
    // pdFALSE avoids the stack high-water scan; we only need ulRunTimeCounter.
    vTaskGetInfo(idle, &info, pdFALSE, eInvalid);
    idle_now[core] = info.ulRunTimeCounter;

    if (cpu_runtime_valid) {
      // Unsigned subtraction intentionally handles the 32-bit runtime-counter wrap.
      idle_delta_sum += (configRUN_TIME_COUNTER_TYPE)(idle_now[core] - cpu_prev_idle[core]);
    }
  }

  const configRUN_TIME_COUNTER_TYPE total_now = cpu_runtime_counter_now();
  if (cpu_runtime_valid) {
    const configRUN_TIME_COUNTER_TYPE elapsed =
      (configRUN_TIME_COUNTER_TYPE)(total_now - cpu_prev_total);
    const uint64_t capacity = (uint64_t)elapsed * (uint64_t)configNUMBER_OF_CORES;

    if (capacity) {
      if (idle_delta_sum > capacity) idle_delta_sum = capacity;
      cpu_load_pct = (uint8_t)(
        ((capacity - idle_delta_sum) * 100ULL + capacity / 2ULL) / capacity);
      if (cpu_load_pct > 100) cpu_load_pct = 100;
    }
  }

  cpu_prev_total = total_now;
  for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core) {
    cpu_prev_idle[core] = idle_now[core];
  }
  cpu_runtime_valid = true;
#else
  // Runtime stats are not compiled into this core. Keep the UI value defined.
  cpu_load_pct = 0;
#endif
}

struct DisplayStatus {
  bool valid = false;
  bool output_enabled = false;
  bool jbc_connected = false;
  bool weller_connected = false;
  bool jbc_present = false;
  bool weller_present = false;
  bool fan_present = false;
  bool output_present = false;
  bool update_active = false;
  uint16_t output_power = 0;
  uint16_t fan_rpm = 0;
  uint16_t afterrun_s = 0;
  uint16_t select_flow = 0;
  uint16_t delay_work_s = 0;
  uint16_t delay_stand_s = 0;
  bool afterrun_power_enabled = false;
  uint16_t afterrun_power = 300;
  uint16_t weller_filter_runtime_min = 0;
  uint16_t weller_filter_programmed_min = 0;
  uint16_t io_input_mask = 0;
  uint16_t io_output_mask = 0;
  uint16_t io_fault_mask = 0;
  uint16_t module_output_power = 0;
  uint16_t module_output_rpm = 0;
  uint16_t module_output_fault = 0;
  uint16_t weller_version = 0;
  uint16_t jbc_stat_error = 0;
  uint16_t clock_year = 0;
  uint8_t work_mask = 0;
  uint8_t modules_count = 0;
  uint8_t update_target = 0;
  uint8_t update_progress = 0;
  uint8_t output_addr = 0;
  uint8_t preferred_output_addr = 0;
  uint8_t auto_output_addr = 0;
  uint8_t main_input_source_type = 1;
  uint8_t main_input_source_addr = 0;
  uint8_t main_input_source_bit = 0;
  uint8_t jbc_inputs = 0;
  uint8_t continuous = 0;
  uint8_t suction_level = 0;
  uint8_t jbc_addr = 0;
  uint8_t station_addr = 0;
  uint8_t weller_speed = 0;
  uint8_t weller_filter_status = 0;
  uint8_t weller_light = 0;
  uint8_t external_input = 0;
  uint8_t module_output_enabled = 0;
  uint8_t jbc_link_flags = 0;
  uint8_t jbc_work_mask = 0;
  uint8_t jbc_stand_mask = 0;
  uint8_t route_jbc_output = 0;
  uint8_t stand_intakes = 0;
  bool clock_valid = false;
  uint8_t clock_hour = 0;
  uint8_t clock_minute = 0;
  uint8_t clock_day = 0;
  uint8_t clock_month = 0;
  char update_name[32] = {0};
  bool master_alarm_valid = false;
  uint8_t master_alarm_count = 0;
  uint8_t master_alarm_item_count = 0;
  uint8_t master_alarm_critical = 0;
  uint8_t master_alarm_critical_mask = 0;
  uint8_t master_alarm_addr[6] = {0};
  uint8_t master_alarm_type[6] = {0};
  uint8_t master_alarm_code[6] = {0};
  uint16_t master_alarm_value[6] = {0};
  char master_alarm_title[40] = {0};
  char master_alarm_detail[72] = {0};
  char master_alarm_titles[3][40] = {{0}};
  char master_alarm_details[3][72] = {{0}};
  struct JbcStation {
    uint8_t flags = 0; // bit0 Work, bit1 Stand, bit2 station error
    char model[5] = {0};
  } jbc_stations[16];
  uint8_t jbc_station_count = 0;
};

static DisplayStatus* status_psram = nullptr;
#define status (*status_psram)
static DisplayStatus* last_drawn_status_psram = nullptr;
#define last_drawn_status (*last_drawn_status_psram)
static bool have_drawn_status = false;
static uint8_t display_page = 0;
static uint16_t last_touch_x = 0;
static uint16_t last_touch_y = 0;
static uint8_t display_brightness_pct = 85;
static uint8_t display_language = 0; // 0 = English, 1 = Deutsch
static uint8_t display_theme = 0; // 0 = dark, 1 = light
static uint8_t screensaver_timeout_min = 2; // 0 = disabled
static uint32_t last_user_activity_ms = 0;
static const uint32_t DISPLAY_FIRST_SETUP_DELAY_MS = 8000UL;
static uint32_t display_boot_setup_started_ms = 0;
static bool display_boot_setup_prompted = false;
static bool screensaver_active = false;
static bool screensaver_wait_release = false;
static bool screensaver_wake_deferred = false;
static uint32_t screensaver_last_update_ms = 0;
static lv_obj_t* screensaver_return_screen = nullptr;
static bool backlight_pwm_ready = false;
static uint8_t pending_display_event = 0;
static int16_t pending_display_event_value = 0;
static uint8_t active_alarm_count = 0;

struct DisplayModuleSummary;
struct DisplayModuleDetail;


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

static const uint8_t DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX = 32;
static const uint8_t DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX = 32;
static const uint8_t DISPLAY_UNIVERSAL_MODULE_CACHE_MAX = 16;
static const uint32_t DISPLAY_UNIVERSAL_CACHE_REFRESH_MS = 1200UL;

enum DisplayUniversalEntityType : uint8_t {
  DISPLAY_UNI_SENSOR = 1,
  DISPLAY_UNI_BINARY_SENSOR = 2,
  DISPLAY_UNI_NUMBER = 3,
  DISPLAY_UNI_SWITCH = 4,
  DISPLAY_UNI_BUTTON = 5,
  DISPLAY_UNI_TEXT = 6,
  DISPLAY_UNI_SELECT = 7,
};

struct DisplayUniversalEntity {
  bool valid = false;
  uint8_t id = 0;
  uint8_t type = 0;
  uint8_t flags = 0; // bit0 readable, bit1 writable
  int16_t min_value = 0;
  int16_t max_value = 100;
  int16_t step_value = 1;
  int16_t value = 0;
  char label[32] = {0};
  char unit[6] = {0};
  char text[32] = {0};
  char options[160] = {0};
};

struct DisplayModuleSummary {
  bool valid = false;
  uint8_t addr = 0;
  uint8_t type = MODULE_UNKNOWN;
  uint8_t flags = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
  uint8_t fw_patch = 0;
  char fw_suffix[8] = {0};
  uint32_t caps = 0;
  uint32_t uptime_s = 0;
  char name[24] = {0};
};

// Compact JBC USB detail extensions (Master B5/v1 + friendly B6/v1). These mirror only the
// normal operational values used by MQTT/display; full DLL diagnostics stay
// on the Master web UI.
struct DisplayJbcUsbCorePort {
  bool valid = false;
  uint8_t state = 0;
  uint8_t tool = 0;
  uint8_t error = 0;
  uint16_t temp_c = 0xFFFF;
  uint16_t set_temp_c = 0xFFFF;
  uint8_t power_pct = 0;
  uint8_t flow_pct = 0;
  uint16_t time_to_stop_s = 0;
};

struct DisplayJbcUsbCore {
  bool valid = false;
  bool friendly_valid = false;
  uint8_t family = 0;
  uint8_t flags = 0;
  uint16_t station_error = 0;
  uint8_t port_count = 0;
  char model[9] = {0};
  DisplayJbcUsbCorePort ports[4];
  uint8_t cl_mode = 0;
  uint8_t cl_flags = 0;
  uint16_t ph_temp_c[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t ph_set_temp_c[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
  uint8_t ph_flags = 0;
  uint8_t ph_selected_power_pct = 0;
  uint8_t ph_heater_power_pct = 0;
  uint8_t ph_active_zones = 0;
  uint8_t ph_work_mode = 0;
  uint16_t ph_time_to_stop_s = 0;
  uint8_t fe_suction_level = 0;
  uint8_t fe_flags = 0;
  uint16_t fe_time_to_stop_work_raw = 0;
  uint16_t fe_time_to_stop_stand_raw = 0;
  uint8_t sf_flags = 0;
  uint8_t sf_program = 0;
  uint16_t sf_speed_tenth_mm_s = 0;
  uint16_t sf_length_tenth_mm = 0;
  uint8_t sf_state = 0;
};

struct DisplayModuleDetail : DisplayModuleSummary {
  uint32_t heap_free = 0;
  uint8_t cpu_load = 0;
  uint16_t loop_max_ms = 0;
  uint16_t io_inputs = 0;
  uint16_t io_outputs = 0;
  uint16_t io_faults = 0;
  bool output_enabled = false;
  uint16_t output_power = 0;
  uint16_t output_rpm = 0;
  uint16_t output_fault = 0;
  uint8_t jbc_addr = 0;
  uint8_t station_addr = 0;
  uint8_t jbc_flags = 0;
  uint8_t jbc_work = 0;
  uint8_t jbc_stand = 0;
  uint8_t suction = 0;
  uint16_t select_flow = 0;
  uint16_t delay_work = 0;
  uint16_t delay_stand = 0;
  uint8_t stand_intakes = 0;
  uint8_t continuous = 0;
  uint8_t weller_speed = 0;
  uint8_t weller_filter = 0;
  uint16_t weller_runtime = 0;
  uint16_t weller_programmed = 0;
  uint16_t weller_rpm = 0;
  uint16_t weller_version = 0;
  uint8_t weller_light = 0;
  uint16_t weller_uart_age = 0xFFFF;
  uint8_t jbc_device_id_len = 0;
  uint8_t jbc_device_id[64] = {0};
  char master_ip[16] = {0};
  uint16_t filter_saturation_permille = 0;
  int16_t filter_pressure_raw = 0;
  int16_t filter_zero_raw = 0;
  int16_t filter_clean_raw = 0;
  int16_t filter_full_raw = 0;
  char io_main_alias[19] = {0};
  char io_in1_alias[19] = {0};
  char io_in2_alias[19] = {0};
  char io_out1_alias[19] = {0};
  char io_out2_alias[19] = {0};
  DisplayJbcUsbCore jbc_usb_core;
  uint32_t universal_descriptor_crc = 0;
  uint8_t universal_entity_total = 0;
  uint8_t universal_entity_count = 0;
  DisplayUniversalEntity universal_entities[DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX];
};

struct DisplayUniversalModuleCache {
  bool valid = false;
  uint8_t addr = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t universal_descriptor_crc = 0;
  uint8_t universal_entity_total = 0;
  uint8_t universal_entity_count = 0;
  uint8_t request_cursor = 0;
  DisplayUniversalEntity universal_entities[DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX];
  uint32_t last_ms = 0;
};

// Arduino's .ino preprocessor can place auto-generated prototypes before local
// struct definitions. Explicit prototypes here keep the custom entity type in
// scope and avoid "DisplayUniversalEntity does not name a type" during compile.
static DisplayUniversalEntity* selected_universal_entity_by_id(uint8_t id);
static bool universal_entity_writable(const DisplayUniversalEntity& e);
static bool universal_entity_readable(const DisplayUniversalEntity& e);
static String universal_entity_value_text(const DisplayUniversalEntity& e);
static void universal_pending_set(uint8_t addr, uint8_t id, uint8_t type, int16_t value);
static bool universal_pending_value(uint8_t addr, uint8_t id, uint8_t type, int16_t& value);
static void universal_pending_observe(uint8_t addr, const DisplayUniversalEntity& e);
static DisplayUniversalModuleCache* universal_cache_find(uint8_t addr, bool create = false);
static const DisplayUniversalEntity* universal_cached_entity_by_id(uint8_t addr, uint8_t id);
static bool universal_entity_is_main_input_candidate(const DisplayUniversalEntity& e);
static String universal_input_label(uint8_t addr, const DisplayUniversalEntity& e);
static uint8_t universal_cache_request_start(uint8_t addr);
static uint8_t first_universal_module_addr(bool online_only = true);
static bool module_summary_is_universal_addr(uint8_t addr);
static bool parse_detail_io_aliases(const Frame& req, uint16_t& p, DisplayModuleDetail& m);
static bool parse_detail_jbc_usb_core(const Frame& req, uint16_t& p, DisplayModuleDetail& m);
static bool parse_detail_jbc_usb_friendly(const Frame& req, uint16_t& p, DisplayModuleDetail& m);
static uint8_t jbc_usb_core_generic_tool_id(const DisplayJbcUsbCore& c, uint8_t raw);
static String jbc_usb_core_tool_text(const DisplayJbcUsbCore& c, uint8_t raw);
static String jbc_usb_core_detail_text(const DisplayModuleDetail& m);

static uint8_t display_view_mode = DISPLAY_VIEW_HOME;
static uint8_t display_view_arg = 0;
static uint8_t module_total = 0;
static DisplayModuleSummary* module_summaries = nullptr;
static uint8_t expected_module_total = 0;
static DisplayModuleSummary* expected_modules = nullptr;
static bool master_session_reset_pending = false;
static bool master_uptime_valid = false;
static uint32_t last_master_uptime_s = 0;
static DisplayModuleDetail* selected_module_psram = nullptr;
#define selected_module (*selected_module_psram)
static DisplayModuleDetail* detail_parse_scratch_psram = nullptr;
#define detail_parse_scratch (*detail_parse_scratch_psram)
static DisplayUniversalModuleCache* universal_module_cache = nullptr;
static uint8_t detail_controls_addr = 0;
static uint8_t detail_controls_type = MODULE_UNKNOWN;
static uint32_t detail_controls_caps = 0;
static uint8_t detail_requested_addr = 0;
static uint32_t detail_open_ms = 0;
static uint32_t detail_last_rx_ms = 0;
static uint32_t detail_status_msg_ms = 0;
static uint8_t selected_universal_request_addr = 0;
static uint8_t selected_universal_request_cursor = 0;
static uint32_t selected_universal_request_ms = 0;
static uint8_t selected_universal_readback_id = 0;

// Keep frequently written/read ("hot") state in internal SRAM.
// The RGB engine continuously consumes the PSRAM framebuffer; moving hot state
// into PSRAM increases bus contention and can provoke ESP32-S3 RGB screen drift.
// Large/cold caches still live in PSRAM.
static void* alloc_cache(size_t item_size, size_t count, const char* label, bool prefer_internal) {
  const size_t bytes = item_size * count;
  void* ptr = nullptr;
  bool internal = false;

  if (prefer_internal) {
    ptr = heap_caps_calloc(count, item_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    internal = ptr != nullptr;
    if (!ptr) ptr = heap_caps_calloc(count, item_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  } else {
    ptr = heap_caps_calloc(count, item_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
      ptr = heap_caps_calloc(count, item_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      internal = ptr != nullptr;
    }
  }

  if (!ptr) {
    Serial.printf("Cache allocation failed: %s (%u bytes)\n", label, (unsigned)bytes);
    return nullptr;
  }
  Serial.printf("Cache %s: %u bytes in %s\n", label, (unsigned)bytes,
                internal ? "internal RAM" : "PSRAM");
  return ptr;
}

static void init_psram_caches() {
  // Firmware staging is large/sequential and only active during an update -> PSRAM.
  if (!fw_update_write_buffer)
    fw_update_write_buffer = (uint8_t*)heap_caps_calloc(FW_UPDATE_WRITE_BUFFER_SIZE, 1,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const bool fw_psram = fw_update_write_buffer != nullptr;
  if (!fw_update_write_buffer)
    fw_update_write_buffer = (uint8_t*)heap_caps_calloc(FW_UPDATE_WRITE_BUFFER_SIZE, 1,
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  // HOT: RS485 and LVGL touch these continuously.
  if (!status_psram)
    status_psram = (DisplayStatus*)alloc_cache(sizeof(DisplayStatus), 1, "status HOT", true);
  if (!last_drawn_status_psram)
    last_drawn_status_psram = (DisplayStatus*)alloc_cache(sizeof(DisplayStatus), 1,
                                                          "last_drawn_status HOT", true);
  if (!module_summaries)
    module_summaries = (DisplayModuleSummary*)alloc_cache(sizeof(DisplayModuleSummary), 17,
                                                          "module_summaries HOT", true);
  if (!expected_modules)
    expected_modules = (DisplayModuleSummary*)alloc_cache(sizeof(DisplayModuleSummary), 17,
                                                          "expected_modules HOT", true);
  // Detail records are consumed on data changes, not by the pixel renderer.
  // Leave this SRAM for LVGL tiles while preserving the radio/runtime reserve.
  if (!selected_module_psram)
    selected_module_psram = (DisplayModuleDetail*)alloc_cache(sizeof(DisplayModuleDetail), 1,
                                                               "selected_module COLD", false);
  if (!detail_parse_scratch_psram)
    detail_parse_scratch_psram = (DisplayModuleDetail*)alloc_cache(sizeof(DisplayModuleDetail), 1,
                                                                   "detail_parse_scratch COLD", false);

  // COLD/LARGE: descriptor cache can stay in PSRAM.
  if (!universal_module_cache)
    universal_module_cache = (DisplayUniversalModuleCache*)alloc_cache(
      sizeof(DisplayUniversalModuleCache), DISPLAY_UNIVERSAL_MODULE_CACHE_MAX,
      "universal_module_cache COLD", false);

  Serial.printf("Cache fw_update_write_buffer: %u bytes in %s\n",
                (unsigned)FW_UPDATE_WRITE_BUFFER_SIZE,
                fw_update_write_buffer ? (fw_psram ? "PSRAM" : "internal RAM") : "FAILED");
}
class OfeDpiRgbDisplay : public Arduino_GFX {
public:
  OfeDpiRgbDisplay(int16_t w, int16_t h, Arduino_ESP32RGBPanel* panel, bool auto_flush = true)
      : Arduino_GFX(w, h), panel_(panel), auto_flush_(auto_flush) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    if (!panel_) return false;
    panel_->begin(speed);
    framebuffer_ = panel_->getFrameBuffer(_width, _height);
    framebuffer_bytes_ = (size_t)_width * (size_t)_height * 2U;
    return framebuffer_ != nullptr;
  }

  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    if (!framebuffer_) return;
    mapPoint(x, y);
    uint16_t* dst = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    *dst = color;
    if (auto_flush_) Cache_WriteBack_Addr((uint32_t)dst, 2);
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!framebuffer_) return;
    if (_rotation == 2) {
      writeFillRectPreclipped(x, y, w, 1, color);
      return;
    }
    if (!_ordered_in_range(y, 0, _max_y) || w == 0) return;
    if (w < 0) { x += w + 1; w = -w; }
    if (x > _max_x || (x + w - 1) < 0) return;
    if (x < 0) { w += x; x = 0; }
    if ((x + w - 1) > _max_x) w = _max_x - x + 1;
    uint16_t* dst = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    for (int16_t i = 0; i < w; ++i) dst[i] = color;
    if (auto_flush_) Cache_WriteBack_Addr((uint32_t)dst, (uint32_t)w * 2U);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!framebuffer_) return;
    if (_rotation == 2) {
      writeFillRectPreclipped(x, y, 1, h, color);
      return;
    }
    if (!_ordered_in_range(x, 0, _max_x) || h == 0) return;
    if (h < 0) { y += h + 1; h = -h; }
    if (y > _max_y || (y + h - 1) < 0) return;
    if (y < 0) { h += y; y = 0; }
    if ((y + h - 1) > _max_y) h = _max_y - y + 1;
    uint16_t* dst = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    for (int16_t j = 0; j < h; ++j) {
      *dst = color;
      if (auto_flush_) Cache_WriteBack_Addr((uint32_t)dst, 2);
      dst += DISPLAY_RGB_WIDTH;
    }
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (!framebuffer_ || w <= 0 || h <= 0) return;
    if (_rotation == 2) {
      x = DISPLAY_RGB_WIDTH - x - w;
      y = DISPLAY_RGB_HEIGHT - y - h;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x > DISPLAY_RGB_WIDTH - 1 || y > DISPLAY_RGB_HEIGHT - 1 || w <= 0 || h <= 0) return;
    if (x + w > DISPLAY_RGB_WIDTH) w = DISPLAY_RGB_WIDTH - x;
    if (y + h > DISPLAY_RGB_HEIGHT) h = DISPLAY_RGB_HEIGHT - y;
    uint16_t* flush_start = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH);
    const uint32_t flush_bytes = (uint32_t)DISPLAY_RGB_WIDTH * (uint32_t)h * 2U;
    uint16_t* row = flush_start + x;
    for (int16_t j = 0; j < h; ++j) {
      for (int16_t i = 0; i < w; ++i) row[i] = color;
      row += DISPLAY_RGB_WIDTH;
    }
    if (auto_flush_) Cache_WriteBack_Addr((uint32_t)flush_start, flush_bytes);
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) override {
    draw16bitRGBBitmapCopy(x, y, bitmap, w, h);
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) override {
    draw16bitRGBBitmapCopy(x, y, bitmap, w, h);
  }

  void flush(bool force_flush = false) override {
    if (framebuffer_ && force_flush) Cache_WriteBack_Addr((uint32_t)framebuffer_, framebuffer_bytes_);
  }

  uint16_t* getFramebuffer() { return framebuffer_; }

private:
  void mapPoint(int16_t& x, int16_t& y) const {
    if (_rotation == 2) {
      x = DISPLAY_RGB_WIDTH - 1 - x;
      y = DISPLAY_RGB_HEIGHT - 1 - y;
    }
  }

  void draw16bitRGBBitmapCopy(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h) {
    const int16_t src_stride = w;
    if (!framebuffer_ || !bitmap || w <= 0 || h <= 0) return;
    if ((x + w - 1) < 0 || (y + h - 1) < 0 || x > _max_x || y > _max_y) return;
    int16_t src_x = 0;
    int16_t src_y = 0;
    if (x < 0) { src_x = -x; w += x; x = 0; }
    if (y < 0) { src_y = -y; h += y; y = 0; }
    if ((x + w - 1) > _max_x) w = _max_x - x + 1;
    if ((y + h - 1) > _max_y) h = _max_y - y + 1;
    const uint16_t* src = bitmap + ((int32_t)src_y * src_stride) + src_x;
    if (_rotation == 2) {
      const int16_t dst_x = DISPLAY_RGB_WIDTH - x - w;
      const int16_t dst_y = DISPLAY_RGB_HEIGHT - y - h;
      uint16_t* flush_start = framebuffer_ + ((int32_t)dst_y * DISPLAY_RGB_WIDTH);
      const uint32_t flush_bytes = (uint32_t)DISPLAY_RGB_WIDTH * (uint32_t)h * 2U;
      for (int16_t j = 0; j < h; ++j) {
        uint16_t* dst = framebuffer_ + ((int32_t)(dst_y + h - 1 - j) * DISPLAY_RGB_WIDTH) + dst_x;

        // Source is the small INTERNAL-RAM LVGL tile; framebuffer is PSRAM.
        // Read the source backwards but write PSRAM strictly forward so the
        // cache can combine stores into sequential bursts. The old code wrote
        // PSRAM backwards one pixel at a time and was noticeably slower while scrolling.
        const uint16_t* s = src + w;
        int16_t i = 0;
        for (; i + 7 < w; i += 8) {
          dst[i + 0] = *--s;
          dst[i + 1] = *--s;
          dst[i + 2] = *--s;
          dst[i + 3] = *--s;
          dst[i + 4] = *--s;
          dst[i + 5] = *--s;
          dst[i + 6] = *--s;
          dst[i + 7] = *--s;
        }
        for (; i < w; ++i) dst[i] = *--s;
        src += src_stride;
      }
      if (auto_flush_) Cache_WriteBack_Addr((uint32_t)flush_start, flush_bytes);
      return;
    }
    uint16_t* dst = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    uint16_t* flush_start = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH);
    const uint32_t flush_bytes = (uint32_t)DISPLAY_RGB_WIDTH * (uint32_t)h * 2U;
    for (int16_t j = 0; j < h; ++j) {
      memcpy(dst, src, (size_t)w * 2U);
      src += src_stride;
      dst += DISPLAY_RGB_WIDTH;
    }
    if (auto_flush_) Cache_WriteBack_Addr((uint32_t)flush_start, flush_bytes);
  }

  Arduino_ESP32RGBPanel* panel_ = nullptr;
  uint16_t* framebuffer_ = nullptr;
  size_t framebuffer_bytes_ = 0;
  bool auto_flush_ = true;
};
class OfeEspLcdRgbDisplay : public Arduino_GFX {
public:
  OfeEspLcdRgbDisplay(int16_t w, int16_t h, uint8_t rotation)
      : Arduino_GFX(w, h) {
    setRotation(rotation);
  }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    if (panel_) return framebuffer_ != nullptr;

    // Stable ESP32-S3 RGB topology:
    //   one full RGB565 framebuffer in PSRAM
    //   + two small DMA bounce buffers in internal SRAM
    // LVGL itself keeps its tile/draw buffer in internal SRAM as well.
    // A second PSRAM framebuffer is intentionally NOT allocated here because
    // LVGL is using PARTIAL rendering, so it would not provide true page flipping.
    esp_lcd_rgb_panel_config_t panel_config = {
      .clk_src = LCD_CLK_SRC_PLL160M,
      .timings = {
        .pclk_hz = DISPLAY_RGB_PCLK_HZ,
        .h_res = DISPLAY_RGB_WIDTH,
        .v_res = DISPLAY_RGB_HEIGHT,
        .hsync_pulse_width = 4,
        .hsync_back_porch = 8,
        .hsync_front_porch = 8,
        .vsync_pulse_width = 4,
        .vsync_back_porch = 8,
        .vsync_front_porch = 8,
        .flags = {
          // IMPORTANT: preserve the polarity used by the original working-mostly
          // JC8048W550 sketch. Arduino_GFX maps hsync/vsync_polarity=0 to
          // esp_lcd hsync_idle_low/vsync_idle_low=1.
          .hsync_idle_low = 1,
          .vsync_idle_low = 1,
          .de_idle_high = 0,
          .pclk_active_neg = 1,
          .pclk_idle_high = 0,
        },
      },
      .data_width = 16,
      .bits_per_pixel = 16,
      .num_fbs = 1,
      .bounce_buffer_size_px = DISPLAY_RGB_WIDTH * DISPLAY_RGB_BOUNCE_BUFFER_LINES,
      .sram_trans_align = 8,
      .psram_trans_align = 64,
      .hsync_gpio_num = 39,
      .vsync_gpio_num = 41,
      .de_gpio_num = 40,
      .pclk_gpio_num = 42,
      .disp_gpio_num = GPIO_NUM_NC,
      .data_gpio_nums = {0},
      .flags = {
        .disp_active_low = true,
        .refresh_on_demand = false,
        .fb_in_psram = true,
        .double_fb = false,
        .no_fb = false,
        // Keep false: LVGL/CPU writes into the PSRAM framebuffer while the
        // bounce-buffer ISR reads it. Invalidating those cache lines can lose
        // a concurrent write on the other core.
        .bb_invalidate_cache = false,
      },
    };

    // Match Arduino_ESP32RGBPanel's RGB565 bit order for this board.
    const int data_pins[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};
    for (uint8_t i = 0; i < 16; ++i) panel_config.data_gpio_nums[i] = data_pins[i];

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &panel_);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_new_rgb_panel failed: 0x%X (bounce=%u lines)\n",
                    (unsigned)err, (unsigned)DISPLAY_RGB_BOUNCE_BUFFER_LINES);
      panel_ = nullptr;
      return false;
    }

    err = esp_lcd_panel_reset(panel_);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_panel_reset failed: 0x%X\n", (unsigned)err);
      return false;
    }
    err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_panel_init failed: 0x%X\n", (unsigned)err);
      return false;
    }

    void* fb0 = nullptr;
    err = esp_lcd_rgb_panel_get_frame_buffer(panel_, 1, &fb0);
    if (err != ESP_OK || !fb0) {
      Serial.printf("RGB framebuffer lookup failed: 0x%X fb=%p\n", (unsigned)err, fb0);
      return false;
    }

    framebuffer_ = static_cast<uint16_t*>(fb0);
    framebuffer_bytes_ = (size_t)DISPLAY_RGB_WIDTH * (size_t)DISPLAY_RGB_HEIGHT * sizeof(uint16_t);
    Serial.printf("RGB panel: single PSRAM framebuffer=%p (%u bytes), bounce=%u lines (%u bytes internal total)\n",
                  framebuffer_, (unsigned)framebuffer_bytes_,
                  (unsigned)DISPLAY_RGB_BOUNCE_BUFFER_LINES,
                  (unsigned)(2U * DISPLAY_RGB_WIDTH * DISPLAY_RGB_BOUNCE_BUFFER_LINES * sizeof(uint16_t)));
    return true;
  }

  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    if (!framebuffer_) return;
    mapPoint(x, y);
    if ((uint16_t)x >= DISPLAY_RGB_WIDTH || (uint16_t)y >= DISPLAY_RGB_HEIGHT) return;
    uint16_t* dst = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    *dst = color;
    writeBack(dst, sizeof(uint16_t));
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    writeFillRectPreclipped(x, y, w, 1, color);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    writeFillRectPreclipped(x, y, 1, h, color);
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (!framebuffer_ || w <= 0 || h <= 0) return;

    if (_rotation == 2) {
      x = DISPLAY_RGB_WIDTH - x - w;
      y = DISPLAY_RGB_HEIGHT - y - h;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= DISPLAY_RGB_WIDTH || y >= DISPLAY_RGB_HEIGHT || w <= 0 || h <= 0) return;
    if (x + w > DISPLAY_RGB_WIDTH) w = DISPLAY_RGB_WIDTH - x;
    if (y + h > DISPLAY_RGB_HEIGHT) h = DISPLAY_RGB_HEIGHT - y;

    uint16_t* first = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    uint16_t* row = first;
    for (int16_t j = 0; j < h; ++j) {
      for (int16_t i = 0; i < w; ++i) row[i] = color;
      row += DISPLAY_RGB_WIDTH;
    }

    // One cache writeback per rectangle instead of one esp_lcd draw call per row.
    const size_t span_pixels = (size_t)(h - 1) * DISPLAY_RGB_WIDTH + (size_t)w;
    writeBack(first, span_pixels * sizeof(uint16_t));
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) override {
    draw16bitRGBBitmapCopy(x, y, bitmap, w, h);
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) override {
    draw16bitRGBBitmapCopy(x, y, bitmap, w, h);
  }

  void flush(bool force_flush = false) override {
    if (framebuffer_ && force_flush) writeBack(framebuffer_, framebuffer_bytes_);
  }

  void requestDmaResync() {
    if (!panel_) return;
    // esp_lcd schedules the actual DMA restart for the next VSYNC, so the
    // raster is repaired without an asynchronous mid-frame restart.
    const esp_err_t err = esp_lcd_rgb_panel_restart(panel_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      Serial.printf("RGB DMA resync request failed: 0x%X\n", (unsigned)err);
    }
  }

  uint16_t* getFramebuffer() const { return framebuffer_; }

private:
  static void writeBack(const void* addr, size_t bytes) {
    (void)addr;
    (void)bytes;
    // IMPORTANT: no explicit PSRAM framebuffer cache writeback in bounce-buffer mode.
    //
    // esp_lcd's bounce-buffer ISR refills the internal DMA buffers with a CPU memcpy()
    // from the PSRAM framebuffer. The official RGB driver intentionally skips the
    // framebuffer esp_cache_msync() path when a bounce buffer is active.
    //
    // Forcing Cache_WriteBack_Addr() here after every LVGL rectangle can stall the
    // external-memory/cache path long enough for the bounce refill ISR to miss its
    // deadline. On ESP32-S3 that produces exactly the visible whole-frame drift /
    // vertical shift which the driver then repairs at VSYNC.
  }

  void mapPoint(int16_t& x, int16_t& y) const {
    if (_rotation == 2) {
      x = DISPLAY_RGB_WIDTH - 1 - x;
      y = DISPLAY_RGB_HEIGHT - 1 - y;
    }
  }

  void draw16bitRGBBitmapCopy(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h) {
    if (!framebuffer_ || !bitmap || w <= 0 || h <= 0) return;

    const int16_t src_stride = w;
    if ((x + w - 1) < 0 || (y + h - 1) < 0 || x > _max_x || y > _max_y) return;

    int16_t src_x = 0;
    int16_t src_y = 0;
    if (x < 0) { src_x = -x; w += x; x = 0; }
    if (y < 0) { src_y = -y; h += y; y = 0; }
    if ((x + w - 1) > _max_x) w = _max_x - x + 1;
    if ((y + h - 1) > _max_y) h = _max_y - y + 1;
    if (w <= 0 || h <= 0) return;

    const uint16_t* src = bitmap + ((int32_t)src_y * src_stride) + src_x;

    if (_rotation == 2) {
      const int16_t dst_x = DISPLAY_RGB_WIDTH - x - w;
      const int16_t dst_y = DISPLAY_RGB_HEIGHT - y - h;
      uint16_t* first = framebuffer_ + ((int32_t)dst_y * DISPLAY_RGB_WIDTH) + dst_x;

      // LVGL's tile stays in internal SRAM. Rotate/copy it directly into the
      // single PSRAM framebuffer, then commit the whole affected span once.
      for (int16_t j = 0; j < h; ++j) {
        uint16_t* dst = framebuffer_ + ((int32_t)(dst_y + h - 1 - j) * DISPLAY_RGB_WIDTH) + dst_x;
        for (int16_t i = 0; i < w; ++i) dst[w - 1 - i] = src[i];
        src += src_stride;
      }

      const size_t span_pixels = (size_t)(h - 1) * DISPLAY_RGB_WIDTH + (size_t)w;
      writeBack(first, span_pixels * sizeof(uint16_t));
      return;
    }

    uint16_t* first = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    uint16_t* dst = first;
    for (int16_t j = 0; j < h; ++j) {
      memcpy(dst, src, (size_t)w * sizeof(uint16_t));
      src += src_stride;
      dst += DISPLAY_RGB_WIDTH;
    }

    const size_t span_pixels = (size_t)(h - 1) * DISPLAY_RGB_WIDTH + (size_t)w;
    writeBack(first, span_pixels * sizeof(uint16_t));
  }

  esp_lcd_panel_handle_t panel_ = nullptr;
  uint16_t* framebuffer_ = nullptr;
  size_t framebuffer_bytes_ = 0;
};
#if 0
static Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
  40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
  45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
  5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
  8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
  0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
  0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
  1 /* pclk_active_neg */, DISPLAY_RGB_PCLK_HZ /* prefer_speed */, false /* useBigEndian */,
  0 /* de_idle_high */, 0 /* pclk_idle_high */, DISPLAY_RGB_WIDTH * DISPLAY_RGB_BOUNCE_BUFFER_LINES);
static Arduino_RGB_Display* gfx_unused = new Arduino_RGB_Display(
  DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT, rgbpanel, DISPLAY_ROTATION, true /* auto_flush */);
#endif
static OfeEspLcdRgbDisplay* gfx = new OfeEspLcdRgbDisplay(DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT, DISPLAY_ROTATION);

// NVS/flash operations can temporarily block the external-memory cache used by
// the RGB bounce-buffer refill path. Request a clean DMA restart at the next
// VSYNC after such writes so a short stall cannot leave the raster shifted.
static inline void display_resync_after_flash_write() {
  // Deliberately no automatic raster restart here.
  // Preferences writes are infrequent; forcing esp_lcd_rgb_panel_restart()
  // after every write makes the restart itself visible as a vertical jump.
  // If a long OTA/flash operation is active, handle PCLK/restart around that
  // operation explicitly instead of restarting normal UI traffic.
}

static lv_display_t* lvgl_disp = nullptr;
static lv_indev_t* lvgl_touch_indev = nullptr;
static uint8_t* lvgl_draw_buf = nullptr;
static uint16_t* lvgl_transfer_scratch = nullptr;
static uint32_t lvgl_transfer_scratch_pixels = 0;
static bool lvgl_draw_buf_psram = false;
static uint32_t lvgl_buf_pixels = 0;
static uint32_t lvgl_buf_bytes = 0;
static bool system_psram_available = false;
static uint32_t system_psram_total = 0;
static uint32_t system_psram_free = 0;
static bool lvgl_ready = false;
static bool lvgl_canvas_dirty = false;
static uint32_t lvgl_last_canvas_flush_ms = 0;
static uint32_t lvgl_last_handler_ms = 0;

// --- LVGL performance profiler ------------------------------------------------
// Counts completed LVGL refresh cycles (last flush of a refresh), the amount of
// rendered pixels, number of partial tiles, and time spent inside the LVGL
// timer handler / framebuffer flush. This makes it possible to distinguish
// "low FPS" from "partial framebuffer wipe/tearing".
static uint32_t perf_window_ms = 0;
static uint32_t perf_frames = 0;
static uint32_t perf_flush_calls = 0;
static uint64_t perf_pixels = 0;
static uint64_t perf_flush_us = 0;
static uint32_t perf_flush_max_us = 0;
static uint64_t perf_handler_us = 0;
static uint32_t perf_handler_calls = 0;
static uint32_t perf_handler_max_us = 0;

static uint16_t perf_fps_x10 = 0;
static uint16_t perf_tiles_x10 = 0;
static uint16_t perf_mpixels_x100 = 0;
static uint16_t perf_handler_avg_x10_ms = 0;
static uint16_t perf_handler_max_x10_ms = 0;
static uint16_t perf_flush_avg_x10_ms = 0;
static uint16_t perf_flush_max_x10_ms = 0;

static volatile bool lvgl_touch_pressed = false;
static volatile uint32_t lvgl_last_touch_ms = 0;


static TaskHandle_t rs485_task_handle = nullptr;
static bool rs485_task_started = false;

static portMUX_TYPE ui_deferred_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t ui_deferred_flags = 0;

static uint8_t ui_deferred_update_target = 0;
static uint8_t ui_deferred_update_progress = 0;
static bool ui_deferred_update_force = false;
static char ui_deferred_update_msg[40] = {0};
static char ui_deferred_update_name[40] = {0};
static char ui_deferred_status_msg[64] = {0};
static uint32_t ui_home_values_last_defer_ms = 0;


static const uint32_t UI_DEFER_MASTER_LINK   = 1UL << 0;
static const uint32_t UI_DEFER_APP_VALUES    = 1UL << 1;
static const uint32_t UI_DEFER_DASHBOARD     = 1UL << 2;
static const uint32_t UI_DEFER_BUS_UPDATE    = 1UL << 3;
static const uint32_t UI_DEFER_UPDATE_SCREEN = 1UL << 4;
static const uint32_t UI_DEFER_BOOT_SCREEN   = 1UL << 5;
static const uint32_t UI_DEFER_STATUS_MSG    = 1UL << 6;
static const uint32_t UI_DEFER_MODULE_LIST   = 1UL << 7;
static const uint32_t UI_DEFER_MODULE_DETAIL = 1UL << 8;
static const uint32_t UI_DEFER_DISPLAY_SETTINGS = 1UL << 9;

static bool running_in_rs485_task() {
#if DISPLAY_RS485_DEDICATED_TASK
  return rs485_task_handle && xTaskGetCurrentTaskHandle() == rs485_task_handle;
#else
  return false;
#endif
}

static void ui_defer_flags(uint32_t flags) {
  portENTER_CRITICAL(&ui_deferred_mux);
  ui_deferred_flags |= flags;
  portEXIT_CRITICAL(&ui_deferred_mux);
}

static void ui_defer_app_values_throttled(uint16_t min_interval_ms = 250) {
  const uint32_t now = millis();
  if ((uint32_t)(now - ui_home_values_last_defer_ms) < min_interval_ms) return;
  ui_home_values_last_defer_ms = now;
  ui_defer_flags(UI_DEFER_APP_VALUES);
}

static void copy_deferred_text(char* dst, size_t dst_len, const char* src) {
  if (!dst || !dst_len) return;
  if (!src) src = "";
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = 0;
}

static void ui_defer_update_screen(uint8_t target, uint8_t progress, const char* msg, bool force, const char* target_name) {
  portENTER_CRITICAL(&ui_deferred_mux);
  ui_deferred_update_target = target;
  ui_deferred_update_progress = progress;
  ui_deferred_update_force = force;
  copy_deferred_text(ui_deferred_update_msg, sizeof(ui_deferred_update_msg), msg);
  copy_deferred_text(ui_deferred_update_name, sizeof(ui_deferred_update_name), target_name);
  ui_deferred_flags |= UI_DEFER_UPDATE_SCREEN;
  portEXIT_CRITICAL(&ui_deferred_mux);
}

static void ui_defer_status_message(const char* msg) {
  portENTER_CRITICAL(&ui_deferred_mux);
  copy_deferred_text(ui_deferred_status_msg, sizeof(ui_deferred_status_msg), msg);
  ui_deferred_flags |= UI_DEFER_STATUS_MSG;
  portEXIT_CRITICAL(&ui_deferred_mux);
}

static lv_obj_t* ui_boot_screen = nullptr;
static lv_obj_t* ui_update_screen = nullptr;
static lv_obj_t* ui_dashboard_screen = nullptr;
static lv_obj_t* ui_status_screen = nullptr;
static lv_obj_t* ui_module_list_screen = nullptr;
static lv_obj_t* ui_module_detail_screen = nullptr;
static lv_obj_t* ui_alarm_screen = nullptr;
static lv_obj_t* ui_alarm_list = nullptr;
static lv_obj_t* ui_alarm_rows[8] = {nullptr};
static lv_obj_t* ui_alarm_titles[8] = {nullptr};
static lv_obj_t* ui_alarm_details[8] = {nullptr};
static lv_obj_t* ui_system_screen = nullptr;

static lv_obj_t* ui_header_link_cards[10] = {nullptr};
static lv_obj_t* ui_header_link_labels[10] = {nullptr};
static lv_obj_t* ui_header_clock_labels[10] = {nullptr};
static lv_obj_t* ui_header_alarm_labels[10] = {nullptr};
static uint8_t ui_header_link_count = 0;
static uint8_t ui_header_link_draw_state = 255;

static lv_obj_t* ui_boot_fw = nullptr;
static lv_obj_t* ui_boot_addr = nullptr;
static lv_obj_t* ui_update_target = nullptr;
static lv_obj_t* ui_update_status = nullptr;
static lv_obj_t* ui_update_percent = nullptr;
static lv_obj_t* ui_update_bar = nullptr;
static lv_obj_t* ui_update_hint = nullptr;
static lv_obj_t* ui_update_phase = nullptr;
static lv_obj_t* ui_update_written = nullptr;
static lv_obj_t* ui_update_size = nullptr;
static lv_obj_t* ui_update_speed = nullptr;
static lv_obj_t* ui_update_detail = nullptr;
static lv_obj_t* ui_status_msg = nullptr;
static lv_obj_t* ui_home_output = nullptr;
static lv_obj_t* ui_home_power = nullptr;
static lv_obj_t* ui_home_rpm = nullptr;
static lv_obj_t* ui_home_afterrun = nullptr;
static lv_obj_t* ui_home_power_bar = nullptr;
static lv_obj_t* ui_home_work = nullptr;
static lv_obj_t* ui_home_jbc = nullptr;
static lv_obj_t* ui_home_suction = nullptr;
static lv_obj_t* ui_home_fan_detail = nullptr;
static lv_obj_t* ui_home_suction_title = nullptr;
static lv_obj_t* ui_home_fanio_relay_button = nullptr;
static lv_obj_t* ui_home_fanio_power_slider = nullptr;
static lv_obj_t* ui_home_fanio_power_value = nullptr;
static lv_obj_t* ui_home_weller = nullptr;
static lv_obj_t* ui_home_filter = nullptr;
static lv_obj_t* ui_home_modules = nullptr;
static lv_obj_t* ui_home_fault = nullptr;
static lv_obj_t* ui_home_content = nullptr;
static lv_obj_t* ui_home_output_card = nullptr;
static lv_obj_t* ui_home_settings_card = nullptr;
static lv_obj_t* ui_home_mode_dropdown = nullptr;
static lv_obj_t* ui_home_afterrun_power_button = nullptr;
static lv_obj_t* ui_home_afterrun_power_input = nullptr;
static lv_obj_t* ui_home_input_dropdown = nullptr;
static lv_obj_t* ui_home_output_dropdown = nullptr;
static uint16_t home_input_values[64] = {0};
static uint8_t home_input_count = 0;
static String home_input_options_cache;
static bool home_input_dropdown_refreshing = false;
static char home_input_selected_text[72] = {0};
static uint32_t home_input_pending_route_ms = 0;
static uint16_t home_input_pending_value = 0;
static uint8_t home_output_addrs[18] = {0};
static uint8_t home_output_count = 0;
static String home_output_options_cache;
static uint8_t home_output_user_route_addr = 0;
static bool home_output_manual_latch = false;
static uint8_t home_output_pending_route_addr = 0;
static uint32_t home_output_pending_route_ms = 0;
static const uint32_t HOME_OUTPUT_ROUTE_GRACE_MS = 3000UL;
static bool home_output_dropdown_refreshing = false;
static char home_output_selected_text[72] = {0};
static lv_obj_t* ui_home_power_input = nullptr;
static lv_obj_t* ui_home_delay_input = nullptr;
static lv_obj_t* ui_home_work_card = nullptr;
static lv_obj_t* ui_home_work_icon = nullptr;
static lv_obj_t* ui_home_jbc_card = nullptr;
static lv_obj_t* ui_home_suction_card = nullptr;
static lv_obj_t* ui_home_weller_card = nullptr;
static lv_obj_t* ui_home_filter_card = nullptr;
static lv_obj_t* ui_home_fault_card = nullptr;
static lv_obj_t* ui_home_jbc_stripe = nullptr;
static lv_obj_t* ui_home_suction_stripe = nullptr;
static lv_obj_t* ui_home_weller_stripe = nullptr;
static lv_obj_t* ui_home_fault_stripe = nullptr;
static lv_obj_t* ui_home_continuous_button = nullptr;
static lv_obj_t* ui_home_fan_button = nullptr;
static lv_obj_t* ui_home_light_button = nullptr;
static lv_obj_t* ui_home_io1_button = nullptr;
static lv_obj_t* ui_home_io2_button = nullptr;
static lv_obj_t* ui_module_list = nullptr;
static lv_obj_t* ui_module_rows[17] = {nullptr};
static lv_obj_t* ui_module_row_status_stripes[17] = {nullptr};
static lv_obj_t* ui_module_row_names[17] = {nullptr};
static lv_obj_t* ui_module_row_meta[17] = {nullptr};
static lv_obj_t* ui_module_update_notice = nullptr;
static lv_obj_t* ui_detail_title = nullptr;
static lv_obj_t* ui_detail_meta = nullptr;
static lv_obj_t* ui_detail_status = nullptr;
static lv_obj_t* ui_detail_values = nullptr;
static lv_obj_t* ui_detail_controls = nullptr;
static lv_obj_t* ui_detail_work_icon = nullptr;
static lv_obj_t* ui_detail_weller_filter_dropdown = nullptr;
static lv_obj_t* ui_system_brightness = nullptr;
static lv_obj_t* ui_system_language = nullptr;
static lv_obj_t* ui_system_theme = nullptr;
static lv_obj_t* ui_system_screensaver = nullptr;
static lv_obj_t* ui_screensaver_screen = nullptr;
static lv_obj_t* ui_screensaver_brand = nullptr;
static lv_obj_t* ui_screensaver_clock = nullptr;
static lv_obj_t* ui_screensaver_state = nullptr;
static lv_obj_t* ui_screensaver_power = nullptr;
static lv_obj_t* ui_screensaver_power_bar = nullptr;
static lv_obj_t* ui_screensaver_modules = nullptr;
static lv_obj_t* ui_screensaver_info = nullptr;
static lv_obj_t* ui_screensaver_alarm = nullptr;
static lv_obj_t* ui_screensaver_hint = nullptr;
static lv_obj_t* ui_detail_jbc_mode_dropdown = nullptr;
static lv_obj_t* ui_detail_jbc_power_input = nullptr;
static lv_obj_t* ui_detail_jbc_delay_work_input = nullptr;
static lv_obj_t* ui_detail_jbc_delay_stand_input = nullptr;
static lv_obj_t* ui_detail_weller_speed_slider = nullptr;
static lv_obj_t* ui_detail_display_brightness_slider = nullptr;
static lv_obj_t* ui_detail_display_brightness_value = nullptr;
static lv_obj_t* ui_detail_display_language = nullptr;
static lv_obj_t* ui_detail_display_theme = nullptr;
static lv_obj_t* ui_detail_stand_button = nullptr;
static lv_obj_t* ui_detail_continuous_button = nullptr;
static lv_obj_t* ui_detail_fan_button = nullptr;
static lv_obj_t* ui_detail_out1_button = nullptr;
static lv_obj_t* ui_detail_out2_button = nullptr;
static lv_obj_t* ui_detail_output_power_slider = nullptr;
static lv_obj_t* ui_detail_output_power_value = nullptr;
static lv_obj_t* ui_detail_weller_fan_button = nullptr;
static lv_obj_t* ui_detail_weller_light_button = nullptr;
static lv_obj_t* ui_detail_universal_sliders[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX] = {nullptr};
static lv_obj_t* ui_detail_universal_switches[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX] = {nullptr};
static lv_obj_t* ui_detail_universal_values[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX] = {nullptr};
static lv_obj_t* ui_detail_universal_buttons[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX] = {nullptr};
static lv_obj_t* ui_detail_universal_buttons_b[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX] = {nullptr};
static lv_obj_t* ui_detail_universal_selects[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX] = {nullptr};
static bool ui_detail_universal_select_refreshing = false;

struct DisplayUniversalControlPending {
  bool active = false;
  uint8_t addr = 0;
  uint8_t id = 0;
  uint8_t type = 0;
  int16_t value = 0;
  uint32_t ms = 0;
};
static DisplayUniversalControlPending universal_control_pending[DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX];
static uint32_t detail_controls_signature = 0;
static lv_obj_t* ui_numeric_keyboard = nullptr;
static lv_obj_t* ui_numeric_editor = nullptr;
static lv_obj_t* ui_numeric_source = nullptr;
static uint8_t ui_numeric_field = 0;
static bool language_rebuild_pending = false;
static bool weller_speed_pending = false;
static uint8_t weller_speed_pending_addr = 0;
static uint8_t weller_speed_pending_value = 0;
static uint32_t weller_speed_pending_ms = 0;
static bool output_power_pending = false;
static uint8_t output_power_pending_addr = 0;
static uint8_t output_power_pending_value = 0;
static uint32_t output_power_pending_ms = 0;

static lv_obj_t* ui_page_title = nullptr;
static lv_obj_t* ui_output_card = nullptr;
static lv_obj_t* ui_output_bar = nullptr;
static lv_obj_t* ui_jbc_card = nullptr;
static lv_obj_t* ui_weller_card = nullptr;
static lv_obj_t* ui_output_addr_card = nullptr;
static lv_obj_t* ui_fan_card = nullptr;
static lv_obj_t* ui_station_card = nullptr;
static lv_obj_t* ui_suction_card = nullptr;
static lv_obj_t* ui_custom_card = nullptr;
static lv_obj_t* ui_delay_card = nullptr;
static lv_obj_t* ui_continuous_card = nullptr;
static lv_obj_t* ui_work_card = nullptr;
static lv_obj_t* ui_jbc_detail_card = nullptr;
static lv_obj_t* ui_control_jbc_row = nullptr;
static lv_obj_t* ui_control_output_row = nullptr;
static lv_obj_t* ui_control_weller_row = nullptr;
static lv_obj_t* ui_brightness_bar = nullptr;
static lv_obj_t* ui_output_state = nullptr;
static lv_obj_t* ui_output_power = nullptr;
static lv_obj_t* ui_afterrun = nullptr;
static lv_obj_t* ui_jbc_state = nullptr;
static lv_obj_t* ui_weller_state = nullptr;
static lv_obj_t* ui_work_mask = nullptr;
static lv_obj_t* ui_fan_rpm = nullptr;
static lv_obj_t* ui_modules = nullptr;
static lv_obj_t* ui_addr = nullptr;
static lv_obj_t* ui_touch = nullptr;
static lv_obj_t* ui_touch_pos = nullptr;
static lv_obj_t* ui_heap = nullptr;
static lv_obj_t* ui_uptime = nullptr;
static lv_obj_t* ui_loop = nullptr;
static lv_obj_t* ui_brightness = nullptr;
static lv_obj_t* ui_suction = nullptr;
static lv_obj_t* ui_custom_power = nullptr;
static lv_obj_t* ui_delay = nullptr;
static lv_obj_t* ui_output_addr = nullptr;
static lv_obj_t* ui_station = nullptr;
static lv_obj_t* ui_continuous = nullptr;
static lv_obj_t* ui_jbc_detail = nullptr;
static lv_obj_t* ui_delay_stand = nullptr;
static lv_obj_t* ui_stand_intakes = nullptr;
static lv_obj_t* ui_fan_detail_output = nullptr;
static lv_obj_t* ui_fan_detail_power = nullptr;
static lv_obj_t* ui_fan_detail_rpm = nullptr;
static lv_obj_t* ui_fan_detail_inputs = nullptr;
static lv_obj_t* ui_fan_detail_outputs = nullptr;
static lv_obj_t* ui_fan_detail_fault = nullptr;
static lv_obj_t* ui_weller_detail_link = nullptr;
static lv_obj_t* ui_weller_detail_speed = nullptr;
static lv_obj_t* ui_weller_detail_rpm = nullptr;
static lv_obj_t* ui_weller_detail_fan = nullptr;
static lv_obj_t* ui_weller_detail_light = nullptr;
static lv_obj_t* ui_weller_detail_filter = nullptr;
static lv_obj_t* ui_weller_detail_runtime = nullptr;
static lv_obj_t* ui_weller_detail_sw = nullptr;
static lv_obj_t* ui_weller_filter_card = nullptr;
static lv_obj_t* ui_page_dots[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static lv_obj_t* ui_page_tabs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

static lv_obj_t* ui_pages[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

static const char* tr(const char* english, const char* german) {
  return display_language == 1 ? german : english;
}

static lv_color_t ui_theme_color(uint32_t dark, uint32_t light) {
  return lv_color_hex(display_theme == 1 ? light : dark);
}

static lv_color_t ui_theme_text(lv_color_t color) {
  if (display_theme != 1) return color;
  if (lv_color_eq(color, lv_color_hex(0xFFFFFF)) || lv_color_eq(color, lv_color_hex(0xF7FAFF)) ||
      lv_color_eq(color, lv_color_hex(0xDDE4EC))) return lv_color_hex(0x17212B);
  if (lv_color_eq(color, lv_color_hex(0x96A0AA)) || lv_color_eq(color, lv_color_hex(0x8F9BA8)) ||
      lv_color_eq(color, lv_color_hex(0x7F8C9B)) || lv_color_eq(color, lv_color_hex(0x718092))) return lv_color_hex(0x5E6D7C);
  return color;
}

static const char* on_off(bool value) {
  return value ? tr("on", "an") : tr("off", "aus");
}

enum NumericField : uint8_t {
  NUMERIC_FIELD_NONE = 0,
  NUMERIC_FIELD_JBC_POWER = 1,
  NUMERIC_FIELD_JBC_DELAY_WORK = 2,
  NUMERIC_FIELD_JBC_DELAY_STAND = 3,
  NUMERIC_FIELD_AFTER_POWER = 4,
};
static uint64_t module_uid() {
  return 0x4000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}
static bool valid_module_addr(uint8_t addr) {
  return addr >= 0x40 && addr <= 0x4F;
}


static void init_backlight_pwm() {
  if (backlight_pwm_ready) return;
  pinMode(BACKLIGHT_PIN, OUTPUT);
#if BACKLIGHT_PWM_ENABLE
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  backlight_pwm_ready = ledcAttach(BACKLIGHT_PIN, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
#else
  ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_PWM_CHANNEL);
  backlight_pwm_ready = true;
#endif
#else
  backlight_pwm_ready = true;
#endif
}

static void write_backlight_duty(uint32_t duty) {
  init_backlight_pwm();
  const uint32_t max_duty = (1UL << BACKLIGHT_PWM_BITS) - 1UL;
  if (duty > max_duty) duty = max_duty;
#if BACKLIGHT_PWM_ENABLE
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (backlight_pwm_ready) ledcWrite(BACKLIGHT_PIN, duty);
#else
  if (backlight_pwm_ready) ledcWrite(BACKLIGHT_PWM_CHANNEL, duty);
#endif
#else
  digitalWrite(BACKLIGHT_PIN, duty ? HIGH : LOW);
#endif
}

static void backlight_off() {
  write_backlight_duty(0);
}

static uint32_t backlight_duty_from_percent(uint8_t pct) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  const uint32_t max_duty = (1UL << BACKLIGHT_PWM_BITS) - 1UL;
  const uint32_t min_duty = (max_duty * 42U) / 100U; // This panel's useful range starts high; 10% must stay readable.
  const float user = ((float)pct - 10.0f) / 90.0f;
  const float shaped = powf(user < 0.0f ? 0.0f : user, 0.55f);
  return min_duty + (uint32_t)((float)(max_duty - min_duty) * shaped + 0.5f);
}

static void apply_backlight() {
  if (display_brightness_pct < 10) display_brightness_pct = 10;
  if (display_brightness_pct > 100) display_brightness_pct = 100;
  write_backlight_duty(backlight_duty_from_percent(display_brightness_pct));
}

static void backlight_on() {
  apply_backlight();
}

// Forward declaration: set_brightness() is defined before the System UI helpers.
static void lv_update_display_settings_widgets(bool force);

static void set_brightness(uint8_t pct, bool save) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  display_brightness_pct = pct;
  apply_backlight();
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_DISPLAY_SETTINGS);
  } else {
    lv_update_display_settings_widgets(true);
  }
  if (save) {
    prefs.putUChar("bright", display_brightness_pct);
    display_resync_after_flash_write();
  }
}

static bool gt911_write_reg(uint16_t reg, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(touch_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  for (uint8_t i = 0; i < len; ++i) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}

static bool gt911_read_reg(uint16_t reg, uint8_t* data, uint8_t len) {
  Wire.beginTransmission(touch_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(touch_addr, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
  return true;
}

static bool gt911_probe_addr(uint8_t addr) {
  touch_addr = addr;
  uint8_t product[4] = {0};
  return gt911_read_reg(0x8140, product, sizeof(product));
}

static void touch_reset_f1atb_sequence() {
  Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
  Wire.setClock(TOUCH_I2C_CLOCK);

  if (TOUCH_INT_PIN >= 0) {
    pinMode(TOUCH_INT_PIN, OUTPUT);
    digitalWrite(TOUCH_INT_PIN, HIGH);
  }
  pinMode(TOUCH_RST_PIN, OUTPUT);
  digitalWrite(TOUCH_RST_PIN, LOW);
  delay(20);
  digitalWrite(TOUCH_RST_PIN, HIGH);
  delay(120);
  if (TOUCH_INT_PIN >= 0) pinMode(TOUCH_INT_PIN, INPUT_PULLUP);

  if (gt911_probe_addr(GT911_ADDR_PRIMARY)) return;
  if (gt911_probe_addr(GT911_ADDR_SECONDARY)) return;
  touch_addr = GT911_ADDR_PRIMARY;
  Serial.println("GT911 not found on 0x5D/0x14 yet");
}

static bool get_touch_point(uint16_t& x, uint16_t& y) {
  static bool pressed = false;
  static uint16_t last_x = 0, last_y = 0;
  static uint32_t last_report_ms = 0;
  // GT911 reports more slowly than the LVGL read timer. No new report is not
  // a release; a bounded hold also prevents a stuck press after an I2C fault.
  auto previous_point = [&]() {
    if ((uint32_t)(millis() - last_report_ms) >= 250UL) pressed = false;
    x = last_x;
    y = last_y;
    return pressed;
  };
  uint8_t status_reg = 0;
  if (!gt911_read_reg(0x814E, &status_reg, 1)) return previous_point();
  if ((status_reg & 0x80) == 0) return previous_point();

  const uint8_t points = status_reg & 0x0F;
  if (points == 0) {
    uint8_t clear = 0;
    gt911_write_reg(0x814E, &clear, 1);
    pressed = false;
    return false;
  }
  if (points > 5) return previous_point();

  uint8_t point[8] = {0};
  if (!gt911_read_reg(0x8150, point, sizeof(point))) return previous_point();
  uint8_t clear = 0;
  gt911_write_reg(0x814E, &clear, 1);

  const uint16_t raw_x = (uint16_t)point[1] << 8 | point[0];
  const uint16_t raw_y = (uint16_t)point[3] << 8 | point[2];
  if (raw_x >= DISPLAY_RGB_WIDTH || raw_y >= DISPLAY_RGB_HEIGHT) return previous_point();

#if DISPLAY_ROTATION == 2
  x = DISPLAY_RGB_WIDTH - 1 - raw_x;
  y = DISPLAY_RGB_HEIGHT - 1 - raw_y;
#else
  x = raw_x;
  y = raw_y;
#endif
  last_x = x;
  last_y = y;
  last_report_ms = millis();
  pressed = true;
  return true;
}

static uint32_t lvgl_millis_cb() {
  return millis();
}

static void draw_static_panel_test() {
  if (!gfx) return;
  gfx->fillScreen(RGB565(8, 12, 18));
  const int16_t bar_h = DISPLAY_RGB_HEIGHT / 6;
  gfx->fillRect(0, 0 * bar_h, DISPLAY_RGB_WIDTH, bar_h, RGB565(220, 30, 30));
  gfx->fillRect(0, 1 * bar_h, DISPLAY_RGB_WIDTH, bar_h, RGB565(30, 180, 60));
  gfx->fillRect(0, 2 * bar_h, DISPLAY_RGB_WIDTH, bar_h, RGB565(40, 110, 230));
  gfx->fillRect(0, 3 * bar_h, DISPLAY_RGB_WIDTH, bar_h, RGB565(240, 220, 40));
  gfx->fillRect(0, 4 * bar_h, DISPLAY_RGB_WIDTH, bar_h, RGB565(220, 60, 220));
  gfx->fillRect(0, 5 * bar_h, DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT - 5 * bar_h, RGB565(235, 240, 245));
  for (int16_t x = 0; x < DISPLAY_RGB_WIDTH; x += 40) gfx->drawFastVLine(x, 0, DISPLAY_RGB_HEIGHT, RGB565(0, 0, 0));
  for (int16_t y = 0; y < DISPLAY_RGB_HEIGHT; y += 40) gfx->drawFastHLine(0, y, DISPLAY_RGB_WIDTH, RGB565(0, 0, 0));
  gfx->setTextColor(RGB565(0, 0, 0));
  gfx->setTextSize(2);
  gfx->setCursor(24, DISPLAY_RGB_HEIGHT - 58);
  gfx->print("OFE 800x480 STATIC RGB TEST 1.3.12beta");
  gfx->setCursor(24, DISPLAY_RGB_HEIGHT - 32);
  gfx->print("No LVGL, no RS485. Image must be rock solid.");
  gfx->flush(true);
}
static void lvgl_flush_canvas_if_dirty(bool force = false) {
  (void)force;
  // The RGB panel scans the visible PSRAM framebuffer continuously. Full cache
  // writebacks outside the LVGL flush callback can cause periodic tearing.
}

// Lossless 180-degree RGB565 rotation in-place.
//
// A tightly-packed W*H tile rotated by 180 degrees is exactly the same 1-D
// pixel array in reverse order. LVGL owns px_map until lv_display_flush_ready(),
// therefore the flush callback may safely modify the tile in-place.
//
// This removes the old equal-sized SRAM scratch buffer and leaves substantially
// more internal SRAM for the LVGL draw tile itself.
static inline void IRAM_ATTR rotate_rgb565_180_inplace(uint16_t* pixels,
                                                        uint32_t count,
                                                        uint16_t xor_mask) {
  if (!pixels || count == 0) return;

  uint32_t lo = 0;
  uint32_t hi = count - 1U;

  // Unroll four swaps per iteration. All accesses are INTERNAL SRAM.
  while (hi >= lo + 7U) {
    uint16_t a0 = pixels[lo + 0U];
    uint16_t a1 = pixels[lo + 1U];
    uint16_t a2 = pixels[lo + 2U];
    uint16_t a3 = pixels[lo + 3U];

    uint16_t b0 = pixels[hi - 0U];
    uint16_t b1 = pixels[hi - 1U];
    uint16_t b2 = pixels[hi - 2U];
    uint16_t b3 = pixels[hi - 3U];

    pixels[lo + 0U] = b0 ^ xor_mask;
    pixels[lo + 1U] = b1 ^ xor_mask;
    pixels[lo + 2U] = b2 ^ xor_mask;
    pixels[lo + 3U] = b3 ^ xor_mask;

    pixels[hi - 0U] = a0 ^ xor_mask;
    pixels[hi - 1U] = a1 ^ xor_mask;
    pixels[hi - 2U] = a2 ^ xor_mask;
    pixels[hi - 3U] = a3 ^ xor_mask;

    lo += 4U;
    hi -= 4U;
  }

  while (lo < hi) {
    const uint16_t a = pixels[lo];
    const uint16_t b = pixels[hi];
    pixels[lo] = b ^ xor_mask;
    pixels[hi] = a ^ xor_mask;
    ++lo;
    --hi;
  }

  if (lo == hi && xor_mask) pixels[lo] ^= xor_mask;
}

static void perf_finish_window_if_due() {
  const uint32_t now_ms = millis();
  if (!perf_window_ms) {
    perf_window_ms = now_ms;
    return;
  }
  const uint32_t elapsed_ms = now_ms - perf_window_ms;
  if (elapsed_ms < 1000) return;

  OfeLvProfileSample costs[OFE_LV_PROFILE_COUNT] = {};
  uint32_t profile_errors = 0;
  const bool costs_ready = ofe_lv_profile_take(costs, &profile_errors);

  perf_fps_x10 = elapsed_ms ? (uint16_t)((perf_frames * 10000ULL) / elapsed_ms) : 0;
  perf_tiles_x10 = perf_frames ? (uint16_t)((perf_flush_calls * 10ULL) / perf_frames) : 0;
  perf_mpixels_x100 = (uint16_t)((perf_pixels * 100ULL) / 1000000ULL);

  const uint32_t handler_avg_us =
    perf_handler_calls ? (uint32_t)(perf_handler_us / perf_handler_calls) : 0;
  const uint32_t flush_avg_us =
    perf_flush_calls ? (uint32_t)(perf_flush_us / perf_flush_calls) : 0;

  perf_handler_avg_x10_ms = (uint16_t)((handler_avg_us + 50U) / 100U);
  perf_handler_max_x10_ms = (uint16_t)((perf_handler_max_us + 50U) / 100U);
  perf_flush_avg_x10_ms = (uint16_t)((flush_avg_us + 50U) / 100U);
  perf_flush_max_x10_ms = (uint16_t)((perf_flush_max_us + 50U) / 100U);

  Serial.printf(
    "LVGL PERF: %u.%u fps | %u.%u tiles/frame | %.2f MPix/s | "
    "handler avg %u.%ums max %u.%ums | flush avg %u.%ums max %u.%ums | "
    "panel theoretical %.1f Hz\n",
    perf_fps_x10 / 10, perf_fps_x10 % 10,
    perf_tiles_x10 / 10, perf_tiles_x10 % 10,
    (double)perf_pixels * 1000.0 / (double)elapsed_ms / 1000000.0,
    perf_handler_avg_x10_ms / 10, perf_handler_avg_x10_ms % 10,
    perf_handler_max_x10_ms / 10, perf_handler_max_x10_ms % 10,
    perf_flush_avg_x10_ms / 10, perf_flush_avg_x10_ms % 10,
    perf_flush_max_x10_ms / 10, perf_flush_max_x10_ms % 10,
    (double)DISPLAY_RGB_PCLK_HZ /
      (double)((DISPLAY_RGB_WIDTH + 8 + 4 + 8) *
               (DISPLAY_RGB_HEIGHT + 8 + 4 + 8)));

  if (costs_ready && perf_frames) {
    const double frame_ms = 1000.0 * perf_frames;
    const uint64_t render_us = costs[OFE_LV_RENDER].us;
    const uint64_t paint_us = render_us > perf_flush_us ? render_us - perf_flush_us : 0;
    Serial.printf(
      "LVGL FRAME ms/frame: refresh=%.1f layout=%.1f paint=%.1f copy=%.1f | "
      "DRAW inclusive ms/frame: tasks=%.1f text=%.1f blend=%.1f objects=%.1f | "
      "INPUT avg/max=%.2f/%.2f ms | profile errors=%lu\n",
      costs[OFE_LV_REFRESH].us / frame_ms, costs[OFE_LV_LAYOUT].us / frame_ms,
      paint_us / frame_ms, perf_flush_us / frame_ms,
      costs[OFE_LV_DRAW].us / frame_ms, costs[OFE_LV_TEXT].us / frame_ms,
      costs[OFE_LV_BLEND].us / frame_ms, costs[OFE_LV_OBJECTS].us / frame_ms,
      costs[OFE_LV_INPUT].calls ? costs[OFE_LV_INPUT].us / (1000.0 * costs[OFE_LV_INPUT].calls) : 0.0,
      costs[OFE_LV_INPUT].max_us / 1000.0, (unsigned long)profile_errors);
  } else if (!costs_ready || profile_errors) {
    Serial.printf("LVGL PROFILE: incomplete hooks, errors=%lu\n", (unsigned long)profile_errors);
  }

  perf_window_ms = now_ms;
  perf_frames = 0;
  perf_flush_calls = 0;
  perf_pixels = 0;
  perf_flush_us = 0;
  perf_flush_max_us = 0;
  perf_handler_us = 0;
  perf_handler_calls = 0;
  perf_handler_max_us = 0;
}

static uint32_t lvgl_timer_handler_profiled() {
  const uint32_t start_us = micros();
  const uint32_t next = lv_timer_handler();
  const uint32_t elapsed_us = micros() - start_us;

  perf_handler_us += elapsed_us;
  perf_handler_calls++;
  if (elapsed_us > perf_handler_max_us) perf_handler_max_us = elapsed_us;
  perf_finish_window_if_due();
  return next;
}

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint32_t flush_start_us = micros();
  const int32_t w = lv_area_get_width(area);
  const int32_t h = lv_area_get_height(area);
  if (!gfx || w <= 0 || h <= 0) {
    lv_display_flush_ready(disp);
    return;
  }

  const uint16_t* pixels = reinterpret_cast<const uint16_t*>(px_map);

#if DISPLAY_ROTATION == 2
  {
  // Lossless fast path for the physically upside-down JC8048W550:
  // rotate in INTERNAL SRAM, then write completed rows sequentially. Large
  // PSRAM render tiles pass through the retained SRAM allocation in chunks.
  const uint32_t pixel_count = (uint32_t)w * (uint32_t)h;
  uint16_t* mutable_pixels = reinterpret_cast<uint16_t*>(px_map);
  const uint16_t xor_mask = DISPLAY_RGB_INVERT_COLORS ? 0xFFFFU : 0x0000U;

  uint16_t* fb = gfx->getFramebuffer();
  const bool staged = lvgl_transfer_scratch && ofe_rgb_tile::copy180(
    fb, DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT, area->x1, area->y1, w, h,
    pixels, lvgl_transfer_scratch, lvgl_transfer_scratch_pixels, xor_mask,
    rotate_rgb565_180_inplace);

  if (!staged) {
    rotate_rgb565_180_inplace(mutable_pixels, pixel_count, xor_mask);
    const int32_t dst_x = DISPLAY_RGB_WIDTH - area->x2 - 1;
    const int32_t dst_y = DISPLAY_RGB_HEIGHT - area->y2 - 1;
    const size_t row_bytes = (size_t)w * sizeof(uint16_t);

    const uint16_t* src_row = mutable_pixels;
    uint16_t* dst_row = fb + dst_y * DISPLAY_RGB_WIDTH + dst_x;
    for (int32_t row = 0; row < h; ++row) {
      memcpy(dst_row, src_row, row_bytes);
      src_row += w;
      dst_row += DISPLAY_RGB_WIDTH;
    }
  }

  const uint32_t flush_elapsed_us = micros() - flush_start_us;
  perf_flush_calls++;
  perf_pixels += (uint64_t)w * (uint64_t)h;
  perf_flush_us += flush_elapsed_us;
  if (flush_elapsed_us > perf_flush_max_us) perf_flush_max_us = flush_elapsed_us;
  if (lv_display_flush_is_last(disp)) perf_frames++;
  lv_display_flush_ready(disp);
  return;
  }
#endif

  // Fallback path (also used for non-180-degree builds).
#if DISPLAY_RGB_INVERT_COLORS
  uint16_t* mutable_pixels = reinterpret_cast<uint16_t*>(px_map);
  const uint32_t count = (uint32_t)w * (uint32_t)h;
  for (uint32_t i = 0; i < count; ++i) mutable_pixels[i] ^= 0xFFFF;
#endif
  gfx->draw16bitRGBBitmap(area->x1, area->y1, pixels, w, h);
  const uint32_t flush_elapsed_us = micros() - flush_start_us;
  perf_flush_calls++;
  perf_pixels += (uint64_t)w * (uint64_t)h;
  perf_flush_us += flush_elapsed_us;
  if (flush_elapsed_us > perf_flush_max_us) perf_flush_max_us = flush_elapsed_us;
  if (lv_display_flush_is_last(disp)) perf_frames++;
  lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  if (fw_update_active) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  uint16_t x = 0, y = 0;
  if (get_touch_point(x, y)) {
    last_user_activity_ms = millis();
    last_touch_x = x;
    last_touch_y = y;
    if (screensaver_active) {
      screensaver_wait_release = true;
      screensaver_wake();
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    if (screensaver_wait_release) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
    lvgl_touch_pressed = true;
    lvgl_last_touch_ms = millis();
  } else {
    screensaver_wait_release = false;
    lvgl_touch_pressed = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static lv_obj_t* lv_label(lv_obj_t* parent, const char* text, int16_t x, int16_t y, lv_color_t color, const lv_font_t* font = UI_FONT_DEFAULT, int16_t width = 0) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, ui_theme_text(color), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_pad_all(label, 0, 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(label, x, y);
  if (width > 0) {
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  }
  lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  return label;
}

static lv_obj_t* lv_card(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h, lv_color_t bg) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_bg_color(card, bg, 0);
#if DISPLAY_FAST_UI
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
#else
  lv_obj_set_style_bg_grad_color(card, ui_theme_color(0x101821, 0xEAF1F7), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
#endif
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, ui_theme_color(0x2C3B4A, 0xCCD8E3), 0);
#if DISPLAY_FAST_UI
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
#else
  lv_obj_set_style_shadow_width(card, 8, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_shadow_ofs_y(card, 3, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
#endif
  // Absolute child positioning is used throughout the UI.
  // Padding would shrink the content area and clip labels in the small cards.
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  return card;
}

static lv_obj_t* lv_value_card(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h, const char* label, lv_obj_t** value, lv_color_t accent) {
  lv_obj_t* card = lv_card(parent, x, y, w, h, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_obj_t* stripe = lv_obj_create(card);
  const int16_t stripe_h = h > 20 ? h - 16 : h - 4;
  const int16_t label_y = h <= 48 ? 5 : 7;
  const int16_t value_y = h <= 48 ? 24 : 30;
  lv_obj_set_pos(stripe, 8, 8);
  lv_obj_set_size(stripe, 5, stripe_h);
  lv_obj_set_style_radius(stripe, 3, 0);
  lv_obj_set_style_bg_color(stripe, accent, 0);
  lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(stripe, 0, 0);
  lv_obj_set_style_pad_all(stripe, 0, 0);
  lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* dot = lv_obj_create(card);
  lv_obj_set_pos(dot, w - 22, 10);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, accent, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_70, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

  lv_label(card, label, 20, label_y, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, w - 44);
  *value = lv_label(card, "-", 20, value_y, lv_color_hex(0xF7FAFF), UI_FONT_DEFAULT, w - 30);
  return card;
}

static lv_obj_t* lv_home_status_card(lv_obj_t* parent, int16_t y, int16_t h,
                                     const char* title, lv_color_t accent,
                                     lv_obj_t** primary, lv_obj_t** secondary,
                                     lv_obj_t** title_label_out = nullptr, lv_obj_t** stripe_out = nullptr,
                                     int16_t x = 4, int16_t w = 440) {
  lv_obj_t* card = lv_card(parent, x, y, w, h, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_obj_t* stripe = lv_obj_create(card);
  lv_obj_set_pos(stripe, 10, 10);
  lv_obj_set_size(stripe, 5, h - 20);
  lv_obj_set_style_radius(stripe, 3, 0);
  lv_obj_set_style_bg_color(stripe, accent, 0);
  lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(stripe, 0, 0);
  lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
  if (stripe_out) *stripe_out = stripe;
  const int16_t right_x = (w > 300) ? (w / 2 + 18) : 150;
  const int16_t title_w = (w > 300) ? (w / 2 - 52) : 120;
  const int16_t right_w = w - right_x - 20;
  const int16_t second_w = w - 50;
  lv_obj_t* title_label = lv_label(card, title, 26, 9, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, title_w);
  if (title_label_out) *title_label_out = title_label;
  *primary = lv_label(card, "-", right_x, 9, lv_color_hex(0xF7FAFF), UI_FONT_DEFAULT, right_w);
  lv_obj_set_style_text_align(*primary, LV_TEXT_ALIGN_RIGHT, 0);
  *secondary = lv_label(card, "-", 26, 35, lv_color_hex(0xDDE4EC), UI_FONT_DEFAULT, second_w);
  lv_label_set_long_mode(*secondary, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(*secondary, 2, 0);
  return card;
}
static void lv_stabilize_button(lv_obj_t* button) {
  if (!button) return;
  lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_x(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(button, 0, LV_STATE_PRESSED);
}

static lv_obj_t* lv_small_button(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, const char* text, lv_event_cb_t cb) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, 38);
  lv_stabilize_button(btn);
  lv_obj_set_style_radius(btn, 19, 0);
  lv_obj_set_style_bg_color(btn, ui_theme_color(0x223247, 0xEAF1F7), 0);
#if DISPLAY_FAST_UI
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
#else
  lv_obj_set_style_bg_grad_color(btn, ui_theme_color(0x172333, 0xDDE7F1), 0);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_shadow_width(btn, 6, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
  lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
#endif
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, ui_theme_color(0x3B5570, 0xB5C5D5), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, ui_theme_color(0xFFFFFF, 0x17212B), 0);
  lv_obj_set_style_text_font(lbl, UI_FONT_DEFAULT, 0);
  lv_obj_center(lbl);
  lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
  return btn;
}

// Forward declarations for lossless conditional-update helpers.
// Definitions are below with the common LVGL text/update helper section.
// Do NOT repeat default arguments here; C++ allows a default only once.
static inline void lv_set_bg_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector);
static inline void lv_set_bg_grad_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector);
static inline void lv_set_border_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector);
static inline void lv_set_text_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector);
static inline void lv_set_image_recolor_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector);
static void lv_set_visible(lv_obj_t* obj, bool visible);

static void lv_set_toggle_style(lv_obj_t* button, bool active) {
  if (!button) return;
  lv_set_bg_color_if_changed(button, active ? lv_color_hex(0x167A4A) : ui_theme_color(0x223247, 0xEAF1F7), 0);
  lv_set_bg_grad_color_if_changed(button, active ? lv_color_hex(0x0F5936) : ui_theme_color(0x172333, 0xDDE7F1), 0);
  lv_set_border_color_if_changed(button, active ? lv_color_hex(0x42D88A) : ui_theme_color(0x3B5570, 0xB5C5D5), 0);
}

static void lv_style_dropdown_popup(lv_obj_t* dropdown) {
  if (!dropdown) return;
  lv_obj_t* list = lv_dropdown_get_list(dropdown);
  if (!list) return;
  lv_obj_set_style_radius(list, 12, 0);
  lv_obj_set_style_bg_color(list, ui_theme_color(0x101821, 0xF8FAFC), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_border_color(list, ui_theme_color(0x3A5068, 0xB5C5D5), 0);
  lv_obj_set_style_text_color(list, ui_theme_color(0xF7FAFF, 0x17212B), 0);
  lv_obj_set_style_bg_color(list, ui_theme_color(0x246BFF, 0xD9ECFF), LV_PART_SELECTED);
  lv_obj_set_style_text_color(list, ui_theme_color(0xFFFFFF, 0x0F3A6D), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(list, ui_theme_color(0x223247, 0xEAF1F7), LV_STATE_PRESSED);
}

static void lv_dropdown_theme_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CLICKED || code == LV_EVENT_VALUE_CHANGED) {
    lv_style_dropdown_popup(lv_event_get_target_obj(e));
  }
}

static void lv_apply_dropdown_theme(lv_obj_t* dropdown, bool open_up = false) {
  if (!dropdown) return;
  lv_obj_set_style_radius(dropdown, 10, 0);
  lv_obj_set_style_bg_color(dropdown, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
  lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dropdown, 1, 0);
  lv_obj_set_style_border_color(dropdown, ui_theme_color(0x3A5068, 0xB5C5D5), 0);
  lv_obj_set_style_text_color(dropdown, ui_theme_color(0xFFFFFF, 0x17212B), 0);
  lv_obj_set_style_text_color(dropdown, ui_theme_color(0xB9C6D3, 0x718092), LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(dropdown, ui_theme_color(0x111820, 0xEAF1F7), LV_STATE_DISABLED);
  lv_obj_set_style_border_color(dropdown, ui_theme_color(0x303A46, 0xCCD8E3), LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(dropdown, ui_theme_color(0x223247, 0xE8F0F7), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(dropdown, lv_color_hex(0x58B8FF), LV_STATE_FOCUSED);
  if (open_up) lv_dropdown_set_dir(dropdown, LV_DIR_TOP);
  lv_style_dropdown_popup(dropdown);
  lv_obj_add_event_cb(dropdown, lv_dropdown_theme_event, LV_EVENT_ALL, NULL);
}

static lv_obj_t* lv_mini_bar(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h, lv_color_t accent) {
  lv_obj_t* bar = lv_bar_create(parent);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, w, h);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_radius(bar, h / 2, 0);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, ui_theme_color(0x283241, 0xD7E2ED), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  return bar;
}

static String fmt_bytes(uint32_t bytes) {
  if (bytes >= 1024UL * 1024UL) return String(bytes / (1024UL * 1024UL)) + "." + String((bytes / (1024UL * 102UL)) % 10) + " MB";
  return String(bytes / 1024UL) + " KB";
}

static String fmt_uptime(uint32_t seconds) {
  const uint32_t d = seconds / 86400UL;
  seconds %= 86400UL;
  const uint32_t h = seconds / 3600UL;
  seconds %= 3600UL;
  const uint32_t m = seconds / 60UL;
  if (d) return String(d) + "d " + h + "h";
  if (h) return String(h) + "h " + m + "m";
  return String(m) + "m";
}

static String fmt_dhm(uint16_t minutes) {
  const uint16_t d = minutes / 1440U;
  minutes %= 1440U;
  const uint8_t h = minutes / 60U;
  const uint8_t m = minutes % 60U;
  if (d) return String(d) + "d " + h + "h" + (m ? (String(" ") + m + "m") : String());
  if (h) return String(h) + "h " + m + "m";
  return String(m) + "m";
}

static const char WELLER_FILTER_OPTIONS[] =
  "1 hour\n2 hours\n3 hours\n4 hours\n5 hours\n6 hours\n7 hours\n8 hours\n9 hours\n10 hours\n"
  "11 hours\n12 hours\n13 hours\n14 hours\n15 hours\n16 hours\n17 hours\n18 hours\n19 hours\n20 hours\n"
  "21 hours\n22 hours\n23 hours\n1 day\n2 days\n3 days\n4 days\n5 days\n6 days\n6 days 16 hours";

static uint8_t weller_filter_preset_index(uint16_t minutes) {
  if (minutes == 9600U) return 29;
  if (minutes >= 60U && minutes <= 1380U && (minutes % 60U) == 0) return (minutes / 60U) - 1U;
  if (minutes >= 1440U && minutes <= 8640U && (minutes % 1440U) == 0) return 22U + (minutes / 1440U);
  return 0;
}
static const char* suction_name(uint8_t level) {
  switch (level) {
    case 0: return tr("High", "Hoch");
    case 1: return tr("Medium", "Mittel");
    case 2: return tr("Low", "Niedrig");
    default: return tr("Custom", "Benutzer");
  }
}

static const char* station_name(uint8_t addr) {
  if (addr >= 0x18 && addr <= 0x21) return "DDE";
  if (addr >= 0x12 && addr <= 0x15) return "JTSE";
  return addr ? "JBC" : "-";
}

static portMUX_TYPE jbc_station_mux = portMUX_INITIALIZER_UNLOCKED;

static bool update_jbc_station_list(const Frame& frame, uint16_t pos) {
  if (pos >= frame.len) return false;
  const uint8_t count = frame.payload[pos++];
  if (count > 16 || pos + (uint16_t)count * 5U > frame.len) return false;
  DisplayStatus::JbcStation next[16] = {};
  for (uint8_t i = 0; i < count; ++i) {
    next[i].flags = frame.payload[pos++];
    memcpy(next[i].model, frame.payload + pos, 4);
    pos += 4;
  }
  // Publish a complete list; the LVGL core must never see a cleared half-list.
  portENTER_CRITICAL(&jbc_station_mux);
  memcpy(status.jbc_stations, next, sizeof(next));
  status.jbc_station_count = count;
  portEXIT_CRITICAL(&jbc_station_mux);
  return true;
}

static String home_jbc_station_models() {
  DisplayStatus::JbcStation stations[16];
  portENTER_CRITICAL(&jbc_station_mux);
  const uint8_t count = status.jbc_station_count;
  memcpy(stations, status.jbc_stations, sizeof(stations));
  portEXIT_CRITICAL(&jbc_station_mux);
  // USB model names cannot be decoded from the legacy FAE station address.
  if (!count) return status.station_addr ? String(station_name(status.station_addr)) : String("JBC");
  String models;
  for (uint8_t i = 0; i < count && i < 2; ++i) {
    if (i) models += ", ";
    models += stations[i].model[0] ? stations[i].model : "JBC";
  }
  if (count > 2) models += String(" +") + String(count - 2);
  return models;
}

static const char* filter_name(uint8_t value) {
  if (value == 1) return tr("very good", "sehr gut");
  if (value == 10) return tr("change soon", "bald wechseln");
  if (value == 100) return tr("change filter", "Filter wechseln");
  return "-";
}

static String fault_name(uint16_t value, uint8_t module_type = MODULE_UNKNOWN) {
  if (!value) return "OK";
  String out;
  auto add = [&](const char* text) {
    if (out.length()) out += ", ";
    out += text;
  };
  auto add_tr = [&](const char* en, const char* de) {
    if (out.length()) out += ", ";
    out += tr(en, de);
  };
  if (value & 0x0001) add_tr(module_type == MODULE_WELLER_ZERO_SMOG ? "Weller device bus error" : "No speed feedback", module_type == MODULE_WELLER_ZERO_SMOG ? "Weller Ger\303\244tebus Fehler" : "Drehzahlr\303\274ckmeldung fehlt");
  if (value & 0x0100) add_tr("No speed feedback", "Drehzahlr\303\274ckmeldung fehlt");
  if (value & 0x0002) add_tr("Filter warn", "Filterwarnung");
  if (value & 0x0004) add_tr("Filter full", "Filter voll");
  if (value & 0x0008) add_tr("Filter missing", "Filter fehlt");
  if (value & 0x0010) add_tr("Sensor fault", "Sensorfehler");
  if (value & 0x0200) add("Timeout");
  if (value & 0x0400) add_tr("Low RPM", "Drehzahl zu gering");
  if (!out.length()) out = String("0x") + String(value, HEX);
  return out;
}

static String jbc_error_name(uint16_t value) {
  if (!value) return "OK";
  String out;
  auto add = [&](const char* en, const char* de) {
    if (out.length()) out += ", ";
    out += tr(en, de);
  };
  if (value & 0x0001) add("Filter lifetime expired", "Filterlaufzeit abgelaufen");
  if (value & 0x0002) add("Filter lifetime ending", "Filterlaufzeit endet bald");
  if (value & 0x0004) add("Filter clogged", "Filter verstopft");
  if (value & 0x0008) add("Filter almost clogged", "Filter fast verstopft");
  if (value & 0x0010) add("No filter", "Kein Filter");
  if (value & 0x0020) add("Cover open", "Abdeckung offen");
  if (value & 0x0040) add("Blower damaged", "L\303\274fter defekt");
  if (value & 0x0100) add("Valve error", "Ventilfehler");
  if (value & 0x0200) add("Aux overcurrent", "Hilfsausgang \303\234berstrom");
  if (value & 0x0400) add("Pedal error", "Pedalfehler");
  if (value & 0x0800) add("FAE system error", "FAE Systemfehler");
  if (value & 0x1000) add("FAE system error 2", "FAE Systemfehler 2");
  const uint16_t rest = value & (uint16_t)~0x1F7FU;
  if (rest) {
    if (out.length()) out += ", ";
    out += "0x" + String(rest, HEX);
  }
  out += " (0x" + String(value, HEX) + ")";
  return out;
}

static String weller_sw_name(uint16_t value) {
  if (!value) return "-";
  return String("V0.") + String(value);
}

static bool update_is_local_display_target(uint8_t target) {
  return target == module_addr || target == 0;
}

static bool bus_update_is_for_addr(uint8_t addr) {
  return status.update_active && !update_is_local_display_target(status.update_target) && status.update_target == addr;
}

static String bus_update_progress_text(uint8_t progress) {
  String text = progress >= 100 ? String(tr("done 100%", "fertig 100%")) :
    (progress == 0 ? String(tr("updating", "Update l\303\244uft")) : String(tr("updating ", "Update ")) + String(progress) + "%");
  if (bus_update_speed_bps) {
    text += "  ";
    if (bus_update_speed_bps < 1000UL) {
      text += String(bus_update_speed_bps) + " B/s";
    } else {
      text += String(bus_update_speed_bps / 1000UL) + "." +
        String((bus_update_speed_bps % 1000UL) / 100UL) + " kB/s";
    }
  }
  return text;
}

static String lv_addr_text(uint8_t addr);

static String update_target_name(uint8_t target) {
  if (target == ADDR_MASTER) return "Master";
  if (target == module_addr || target == 0) return lv_addr_text(module_addr) + " Display";
  String name = String(tr("Module", "Modul"));
  if (target >= 0x10 && target <= 0x1F) name = "JBC Bus";
  else if (target >= 0x20 && target <= 0x2F) name = "Fan/IO";
  else if (target >= 0x30 && target <= 0x3F) name = "Weller";
  else if (target >= 0x40 && target <= 0x4F) name = "Display";
  else if (target >= 0x50 && target <= 0x5F) name = "Universal RS232";
  else if (target >= 0x60 && target <= 0x6F) name = "Modbus RTU";
  return lv_addr_text(target) + " " + name;
}

static bool update_has_live_percent(uint8_t target) {
  // Full-screen update UI is only used for this display itself.
  // Other modules are displayed inline in the module list and always show the
  // received progress value.
  return update_is_local_display_target(target);
}

static const char* update_message_text(const char* msg) {
  if (!msg || !msg[0]) return "";
  if (strcmp(msg, "Preparing display firmware") == 0) return tr("Preparing display firmware", "Display-Firmware vorbereiten");
  if (strcmp(msg, "Image too large") == 0) return tr("Image too large", "Firmware-Datei zu gro\303\237");
  if (strcmp(msg, "Begin failed") == 0) return tr("Begin failed", "Start fehlgeschlagen");
  if (strcmp(msg, "Receiving chunks") == 0) return tr("Receiving chunks", "Daten werden empfangen");
  if (strcmp(msg, "Writing firmware") == 0) return tr("Writing firmware", "Firmware wird geschrieben");
  if (strcmp(msg, "Update complete - rebooting") == 0) return tr("Update complete - rebooting", "Update fertig - Neustart");
  if (strcmp(msg, "Update failed") == 0) return tr("Update failed", "Update fehlgeschlagen");
  if (strcmp(msg, "Update aborted") == 0) return tr("Update aborted", "Update abgebrochen");
  if (strcmp(msg, "Bus update") == 0) return tr("Bus update", "Bus-Update");
  return msg;
}

static void show_dashboard();
static void lv_update_module_list();
static void lv_update_module_detail();
static void lv_update_app_values();
static void lv_update_display_settings_widgets(bool force);
static uint8_t lv_update_alarm_center();
static String master_alarm_title_text(uint8_t addr, uint8_t type, uint8_t code);
static String master_alarm_detail_text(uint8_t code, uint16_t value, uint8_t type);
static void bus_update_ensure_module_row();
static void bus_update_remove_synthetic_module_row();
static void bus_update_auto_show_once();
static void bus_update_auto_return_to_previous(uint8_t view, uint8_t detail_addr);
static void bus_update_scroll_to_target(bool force);
static void lv_open_module_summary(uint8_t index);

static void refresh_bus_update_inline_ui() {
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_BUS_UPDATE);
    return;
  }
  if (!lvgl_ready) return;
  bus_update_ensure_module_row();
  lv_update_module_list();
#if BUS_UPDATE_AUTO_OPEN_MODULE_LIST
  // Auto navigation is one-shot per update target. After that the user may
  // leave the module list and will not be pulled back on every progress packet.
  bus_update_auto_show_once();
#endif
  if (selected_module.valid && selected_module.addr == status.update_target) {
    lv_update_module_detail();
  }
}

static void bus_update_touch();
static void bus_update_clear_state();

static uint8_t module_type_guess_from_addr(uint8_t addr) {
  if (addr >= 0x10 && addr <= 0x1F) return MODULE_JBC_BUS;
  if (addr >= 0x20 && addr <= 0x2F) return MODULE_FAN_IO;
  if (addr >= 0x30 && addr <= 0x3F) return MODULE_WELLER_ZERO_SMOG;
  if (addr >= 0x40 && addr <= 0x4F) return MODULE_DISPLAY;
  if (addr >= 0x50 && addr <= 0x5F) return MODULE_UNIVERSAL_RS232;
  if (addr >= 0x60 && addr <= 0x6F) return MODULE_MODBUS_RTU;
  return MODULE_UNKNOWN;
}

static int8_t module_summary_index_by_addr(uint8_t addr) {
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    if (module_summaries[i].valid && module_summaries[i].addr == addr) return (int8_t)i;
  }
  return -1;
}

static void reset_expected_modules() {
  expected_module_total = 0;
  if (expected_modules) memset(expected_modules, 0, sizeof(DisplayModuleSummary) * 17);
}

static int8_t expected_module_index_by_addr(uint8_t addr) {
  for (uint8_t i = 0; i < expected_module_total && i < 17; ++i) {
    if (expected_modules[i].valid && expected_modules[i].addr == addr) return (int8_t)i;
  }
  return -1;
}

static void learn_expected_modules_from_current_list() {
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    const DisplayModuleSummary& current = module_summaries[i];
    if (!current.valid || !(current.flags & 0x01) || current.addr == ADDR_MASTER || current.addr == module_addr) continue;
    int8_t expected_index = expected_module_index_by_addr(current.addr);
    if (expected_index < 0) {
      if (expected_module_total >= 17) continue;
      expected_index = expected_module_total++;
    }
    expected_modules[(uint8_t)expected_index] = current;
  }
}

static void bus_update_remove_synthetic_module_row() {
  if (!bus_update_synthetic_module_active) return;
  const uint8_t addr = bus_update_synthetic_module_addr;
  int8_t idx = module_summary_index_by_addr(addr);
  if (idx >= 0 && (uint8_t)idx < module_total) {
    const char* n = module_summaries[idx].name;
    const bool looks_synthetic = !n[0] || strcmp(n, "Updating module") == 0 || strcmp(n, "Module update") == 0;
    // Only remove the temporary row that we created locally. If the master has
    // meanwhile supplied real module data/name, keep it in the list.
    if (looks_synthetic) {
      for (uint8_t j = (uint8_t)idx; j + 1 < module_total && j + 1 < 17; ++j) {
        module_summaries[j] = module_summaries[j + 1];
      }
      if (module_total > 0) {
        memset(&module_summaries[module_total - 1], 0, sizeof(module_summaries[module_total - 1]));
        module_total--;
      }
    }
  }
  bus_update_synthetic_module_active = false;
  bus_update_synthetic_module_addr = 0;
}

static void bus_update_ensure_module_row() {
  if (!status.update_active || update_is_local_display_target(status.update_target)) return;
  const uint8_t target = status.update_target;
  if (module_summary_index_by_addr(target) >= 0) return;

  uint8_t slot = 17;
  for (uint8_t i = 0; i < 17; ++i) {
    if (!module_summaries[i].valid) {
      slot = i;
      break;
    }
  }
  if (slot >= 17) return;
  if (slot >= module_total) module_total = slot + 1;

  DisplayModuleSummary& m = module_summaries[slot];
  memset(&m, 0, sizeof(m));
  m.valid = true;
  m.addr = target;
  m.type = module_type_guess_from_addr(target);
  m.flags = 1;  // show online while the update is running
  const char* name = status.update_name[0] ? status.update_name : "Updating module";
  strncpy(m.name, name, sizeof(m.name) - 1);
  m.name[sizeof(m.name) - 1] = 0;
  bus_update_synthetic_module_active = true;
  bus_update_synthetic_module_addr = target;
}

static void bus_update_copy_known_module_name(uint8_t target) {
  status.update_name[0] = 0;
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    if (module_summaries[i].valid && module_summaries[i].addr == target && module_summaries[i].name[0]) {
      strncpy(status.update_name, module_summaries[i].name, sizeof(status.update_name) - 1);
      status.update_name[sizeof(status.update_name) - 1] = 0;
      return;
    }
  }
}

static void bus_update_scroll_to_target(bool force) {
  if (!lvgl_ready || !ui_module_list || !status.update_active || update_is_local_display_target(status.update_target)) return;
  if (lv_screen_active() != ui_module_list_screen) return;

  const int8_t idx = module_summary_index_by_addr(status.update_target);
  if (idx < 0 || idx >= 17 || !ui_module_rows[(uint8_t)idx]) return;

  const uint32_t now = millis();
  if (!force && bus_update_last_scrolled_target == status.update_target &&
      (uint32_t)(now - bus_update_last_scroll_ms) < 1200UL) {
    return;
  }

  bus_update_last_scrolled_target = status.update_target;
  bus_update_last_scroll_ms = now;

  lv_obj_clear_flag(ui_module_rows[(uint8_t)idx], LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(ui_module_list);

  // Put the updating row roughly in the middle when possible, so neighbouring
  // modules stay visible instead of showing only the target row.
  int32_t y = (int32_t)idx * 54L - 54L;
  if (y < 0) y = 0;
  lv_obj_scroll_to_y(ui_module_list, y, LV_ANIM_OFF);
}

static void bus_update_auto_show_once() {
  if (!lvgl_ready || !status.update_active || update_is_local_display_target(status.update_target)) return;
  if (!ui_module_list_screen) return;

  if (bus_update_auto_nav_done && bus_update_auto_nav_target == status.update_target) return;

  bus_update_auto_nav_done = true;
  bus_update_auto_nav_target = status.update_target;
  bus_update_last_scrolled_target = 0;
  bus_update_last_scroll_ms = 0;

  if (!bus_update_auto_return_pending) {
    bus_update_auto_return_view = display_view_mode;
    bus_update_auto_return_detail_addr = selected_module.valid ? selected_module.addr : display_view_arg;
    bus_update_auto_return_pending = display_view_mode != DISPLAY_VIEW_MODULE_LIST;
  }

  screensaver_wake();
  display_view_mode = DISPLAY_VIEW_MODULE_LIST;
  display_view_arg = 0;
  lv_screen_switch(ui_module_list_screen);
  lv_update_module_list();
#if BUS_UPDATE_AUTO_SCROLL_TO_MODULE
  bus_update_scroll_to_target(true);
#endif
}

static void bus_update_auto_return_to_previous(uint8_t view, uint8_t detail_addr) {
  if (!lvgl_ready) return;
  screensaver_wake();

  switch (view) {
    case DISPLAY_VIEW_MODULE_LIST:
      display_view_mode = DISPLAY_VIEW_MODULE_LIST;
      display_view_arg = 0;
      lv_update_module_list();
      lv_screen_switch(ui_module_list_screen);
      break;
    case DISPLAY_VIEW_MODULE_DETAIL: {
      int8_t idx = module_summary_index_by_addr(detail_addr);
      if (idx >= 0) {
        lv_open_module_summary((uint8_t)idx);
      } else if (selected_module.valid && selected_module.addr == detail_addr) {
        display_view_mode = DISPLAY_VIEW_MODULE_DETAIL;
        display_view_arg = detail_addr;
        lv_update_module_detail();
        lv_screen_switch(ui_module_detail_screen);
      } else {
        show_dashboard();
      }
      break;
    }
    case DISPLAY_VIEW_ALARMS:
      display_view_mode = DISPLAY_VIEW_ALARMS;
      display_view_arg = 0;
      lv_update_alarm_center();
      lv_screen_switch(ui_alarm_screen);
      break;
    case DISPLAY_VIEW_SYSTEM:
      display_view_mode = DISPLAY_VIEW_SYSTEM;
      display_view_arg = 0;
      lv_update_display_settings_widgets(true);
      lv_screen_switch(ui_system_screen);
      break;
    case DISPLAY_VIEW_HOME:
    default:
      display_view_mode = DISPLAY_VIEW_HOME;
      display_view_arg = 0;
      show_dashboard();
      break;
  }
}

static void refresh_bus_update_inline_ui_throttled(bool force = false) {
  if (running_in_rs485_task()) {
    const uint32_t now = millis();
    if (!force &&
        status.update_progress == bus_update_last_ui_progress &&
        (uint32_t)(now - bus_update_last_ui_ms) < 250UL) {
      return;
    }
    bus_update_last_ui_progress = status.update_progress;
    bus_update_last_ui_ms = now;
    ui_defer_flags(UI_DEFER_BUS_UPDATE);
    return;
  }
  if (!lvgl_ready) return;
  const uint32_t now = millis();
  if (!force &&
      status.update_progress == bus_update_last_ui_progress &&
      (uint32_t)(now - bus_update_last_ui_ms) < 250UL) {
    return;
  }
  bus_update_last_ui_progress = status.update_progress;
  bus_update_last_ui_ms = now;
  refresh_bus_update_inline_ui();
}

static void bus_update_set_foreign_progress(uint8_t target, uint8_t progress, bool done = false, bool force_ui = false) {
  if (target == ADDR_BROADCAST || target == ADDR_MASTER || update_is_local_display_target(target)) return;
  if (progress > 100) progress = 100;

  const bool same_target = status.update_active && status.update_target == target;
  if (same_target && progress == 0 && status.update_progress > 0 && status.update_progress < 100) {
    // Do not let "busy/0%" status overwrite a real percentage from chunks.
    progress = status.update_progress;
  }
  if (same_target && progress > 0 && progress < 100 &&
      status.update_progress > progress && status.update_progress < 100) {
    // Chunk/status packets can arrive out of cadence; never move backwards.
    progress = status.update_progress;
  }

  const bool target_changed = !same_target;
  status.update_active = true;
  status.update_target = target;
  status.update_progress = progress;
  if (target_changed) {
    bus_update_auto_nav_done = false;
    bus_update_auto_nav_target = 0;
    bus_update_last_scrolled_target = 0;
    bus_update_last_scroll_ms = 0;
  }
  bus_update_copy_known_module_name(target);
  bus_update_touch();
  bus_update_ensure_module_row();

  if (done || progress >= 100) {
    status.update_progress = 100;
    bus_update_done_ms = millis();
    bus_update_snoop_active = false;
  }

  refresh_bus_update_inline_ui_throttled(force_ui || done);
}

static void bus_update_monitor_foreign_frame(const Frame& req) {
  // Passive sniffer for firmware updates of OTHER modules. We do not answer
  // these frames; we only mirror the progress into the module list UI.
  if (req.src != ADDR_MASTER) return;
  if (req.dst == module_addr || req.dst == ADDR_BROADCAST || req.dst == ADDR_MASTER) return;

  const uint8_t target = req.dst;
  switch (req.cmd) {
    case CMD_FW_BEGIN:
      if (req.len >= 4) {
        bus_update_snoop_active = true;
        bus_update_snoop_target = target;
        bus_update_snoop_size = get_u32_le(req.payload);
        bus_update_snoop_offset = 0;
        bus_update_speed_bps = 0;
        bus_update_speed_sample_ms = millis();
        bus_update_speed_sample_offset = 0;
        bus_update_set_foreign_progress(target, 0, false, true);
      }
      break;

    case CMD_FW_CHUNK:
      if (req.len >= 5) {
        const uint32_t offset = get_u32_le(req.payload);
        const uint32_t chunk_len = (uint32_t)req.len - 4UL;
        uint32_t end_offset = offset + chunk_len;
        if (!bus_update_snoop_active || bus_update_snoop_target != target) {
          bus_update_snoop_active = true;
          bus_update_snoop_target = target;
          bus_update_snoop_size = 0;
          bus_update_snoop_offset = 0;
          bus_update_speed_bps = 0;
          bus_update_speed_sample_ms = millis();
          bus_update_speed_sample_offset = 0;
        }
        if (end_offset > bus_update_snoop_offset) bus_update_snoop_offset = end_offset;

        const uint32_t speed_now = millis();
        const uint32_t speed_elapsed = speed_now - bus_update_speed_sample_ms;
        if (speed_elapsed >= 250UL && bus_update_snoop_offset >= bus_update_speed_sample_offset) {
          const uint32_t bytes = bus_update_snoop_offset - bus_update_speed_sample_offset;
          const uint32_t sample_bps = bytes * 1000UL / speed_elapsed;
          bus_update_speed_bps = bus_update_speed_bps ?
            (bus_update_speed_bps * 3UL + sample_bps) / 4UL : sample_bps;
          bus_update_speed_sample_ms = speed_now;
          bus_update_speed_sample_offset = bus_update_snoop_offset;
        }

        uint8_t progress = status.update_progress;
        if (bus_update_snoop_size > 0) {
          uint32_t pct = (bus_update_snoop_offset * 100UL) / bus_update_snoop_size;
          if (pct > 99UL) pct = 99UL;
          progress = (uint8_t)pct;
        } else if (progress == 0) {
          progress = 1;  // Unknown total size: at least show that data is moving.
        }
        bus_update_set_foreign_progress(target, progress, false, false);
      }
      break;

    case CMD_FW_END:
      bus_update_set_foreign_progress(target, 100, true, true);
      break;

    case CMD_FW_ABORT:
      if (status.update_active && status.update_target == target) {
        bus_update_clear_state();
        refresh_bus_update_inline_ui();
      }
      break;

    default:
      break;
  }
}

static void bus_update_touch() {
  bus_update_last_ms = millis();
  if (status.update_progress >= 100) bus_update_done_ms = bus_update_last_ms;
  else bus_update_done_ms = 0;
}

static void bus_update_clear_state() {
  bus_update_remove_synthetic_module_row();
  status.update_active = false;
  status.update_target = 0;
  status.update_progress = 0;
  status.update_name[0] = 0;
  bus_update_last_ms = 0;
  bus_update_done_ms = 0;
  bus_update_snoop_active = false;
  bus_update_snoop_target = 0;
  bus_update_snoop_size = 0;
  bus_update_snoop_offset = 0;
  bus_update_speed_bps = 0;
  bus_update_speed_sample_ms = 0;
  bus_update_speed_sample_offset = 0;
  bus_update_last_ui_progress = 255;
  bus_update_last_ui_ms = 0;
  bus_update_auto_nav_done = false;
  bus_update_auto_nav_target = 0;
  bus_update_auto_return_pending = false;
  bus_update_auto_return_view = DISPLAY_VIEW_HOME;
  bus_update_auto_return_detail_addr = 0;
  bus_update_last_scrolled_target = 0;
  bus_update_last_scroll_ms = 0;
  fw_update_last_draw_target = 255;
  fw_update_last_draw_percent = 255;
  fw_update_last_draw_ms = 0;
}

static void bus_update_check_ui_timeout() {
  if (fw_update_active || !status.update_active) return;
  const uint32_t now = millis();
  const bool was_local_display_update = update_is_local_display_target(status.update_target);

  // If a 100%/done state is shown, keep it visible for 5 seconds, then remove the
  // inline module-list status. Only local display updates may leave the user
  // on the full-screen update page.
  if (status.update_progress >= 100) {
    if (!bus_update_done_ms) bus_update_done_ms = now;
    if ((uint32_t)(now - bus_update_done_ms) >= BUS_UPDATE_DONE_HOLD_MS) {
      const bool should_auto_return = !was_local_display_update && bus_update_auto_return_pending;
      const uint8_t return_view = bus_update_auto_return_view;
      const uint8_t return_detail_addr = bus_update_auto_return_detail_addr;
      bus_update_clear_state();
      refresh_bus_update_inline_ui();
      if (was_local_display_update && lvgl_ready) show_dashboard();
      else if (should_auto_return) bus_update_auto_return_to_previous(return_view, return_detail_addr);
    }
    return;
  }

  // Do NOT time out foreign module updates here. Some targets only report
  // "busy" while receiving chunks and can be silent for longer than a few
  // seconds. Foreign updates are cleared by explicit inactive status, FW_END,
  // FW_ABORT, or after the done hold above.
  if (!was_local_display_update) return;

  if (bus_update_last_ms && (uint32_t)(now - bus_update_last_ms) >= BUS_UPDATE_UI_TIMEOUT_MS) {
    bus_update_clear_state();
    refresh_bus_update_inline_ui();
    if (lvgl_ready) show_dashboard();
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

static void queue_display_event(uint8_t type, int16_t value = 0) {
  pending_display_event = type;
  pending_display_event_value = value;
}

static bool active_output_is_weller();
static bool active_output_is_fan_io();
static uint8_t extractor_power_min_percent();

static void lv_numeric_hide() {
  if (ui_numeric_keyboard) lv_obj_add_flag(ui_numeric_keyboard, LV_OBJ_FLAG_HIDDEN);
  if (ui_numeric_editor) lv_obj_add_flag(ui_numeric_editor, LV_OBJ_FLAG_HIDDEN);
  ui_numeric_source = nullptr;
  ui_numeric_field = NUMERIC_FIELD_NONE;
}

static void lv_numeric_keyboard_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CANCEL) {
    lv_numeric_hide();
    return;
  }
  if (code != LV_EVENT_READY || !ui_numeric_editor) return;
  int32_t value = atoi(lv_textarea_get_text(ui_numeric_editor));
  uint8_t event_type = DISPLAY_EVENT_NONE;
  if (ui_numeric_field == NUMERIC_FIELD_JBC_POWER) {
    value = constrain(value, extractor_power_min_percent(), 100);
    event_type = DISPLAY_EVENT_JBC_POWER_SET;
  } else if (ui_numeric_field == NUMERIC_FIELD_JBC_DELAY_WORK) {
    value = constrain(value, 0, 600);
    event_type = DISPLAY_EVENT_JBC_DELAY_WORK_SET;
  } else if (ui_numeric_field == NUMERIC_FIELD_JBC_DELAY_STAND) {
    value = constrain(value, 0, 600);
    event_type = DISPLAY_EVENT_JBC_DELAY_STAND_SET;
  } else if (ui_numeric_field == NUMERIC_FIELD_AFTER_POWER) {
    value = constrain(value, extractor_power_min_percent(), 100);
    event_type = DISPLAY_EVENT_AFTER_POWER_SET;
  }
  if (event_type != DISPLAY_EVENT_NONE) {
    queue_display_event(event_type, (int16_t)value);
    if (ui_numeric_source) lv_textarea_set_text(ui_numeric_source, String(value).c_str());
  }
  lv_numeric_hide();
}

static void lv_numeric_field_event(lv_event_t* e) {
  ui_numeric_source = lv_event_get_target_obj(e);
  ui_numeric_field = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (!ui_numeric_editor) {
    ui_numeric_editor = lv_textarea_create(lv_layer_top());
    lv_obj_set_pos(ui_numeric_editor, 8, 0);
    lv_obj_set_size(ui_numeric_editor, 784, 40);
    lv_textarea_set_one_line(ui_numeric_editor, true);
    lv_textarea_set_accepted_chars(ui_numeric_editor, "0123456789");
    lv_textarea_set_max_length(ui_numeric_editor, 4);
    lv_obj_set_style_text_font(ui_numeric_editor, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_bg_color(ui_numeric_editor, ui_theme_color(0x172331, 0xFFFFFF), 0);
    lv_obj_set_style_text_color(ui_numeric_editor, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    ui_numeric_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_numeric_keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_keyboard_set_mode(ui_numeric_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(ui_numeric_keyboard, DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT - 42);
    lv_obj_align(ui_numeric_keyboard, LV_ALIGN_TOP_LEFT, 0, 42);
    lv_obj_add_event_cb(ui_numeric_keyboard, lv_numeric_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ui_numeric_keyboard, lv_numeric_keyboard_event, LV_EVENT_CANCEL, NULL);
  }
  lv_textarea_set_text(ui_numeric_editor, lv_textarea_get_text(ui_numeric_source));
  lv_keyboard_set_textarea(ui_numeric_keyboard, ui_numeric_editor);
  lv_obj_clear_flag(ui_numeric_editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_numeric_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_numeric_editor, -1);
  lv_obj_move_to_index(ui_numeric_keyboard, -1);
  lv_obj_send_event(ui_numeric_editor, LV_EVENT_FOCUSED, NULL);
}

static lv_obj_t* lv_numeric_field(lv_obj_t* parent, int16_t x, int16_t y, int16_t w,
                                  uint16_t value, uint8_t field) {
  lv_obj_t* input = lv_textarea_create(parent);
  lv_obj_set_pos(input, x, y);
  lv_obj_set_size(input, w, 38);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_text(input, String(value).c_str());
  lv_textarea_set_accepted_chars(input, "0123456789");
  lv_textarea_set_max_length(input, 4);
  lv_obj_set_style_radius(input, 8, 0);
  lv_obj_set_style_bg_color(input, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
  lv_obj_set_style_border_color(input, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
  lv_obj_set_style_text_color(input, ui_theme_color(0xFFFFFF, 0x17212B), 0);
  lv_obj_add_event_cb(input, lv_numeric_field_event, LV_EVENT_CLICKED, (void*)(uintptr_t)field);
  return input;
}
static void lv_set_text(lv_obj_t* obj, const String& text) {
  if (!obj) return;
  const char* old = lv_label_get_text(obj);
  if (!old || strcmp(old, text.c_str()) != 0) lv_label_set_text(obj, text.c_str());
}

static void lv_set_text_c(lv_obj_t* obj, const char* text) {
  if (!obj) return;
  const char* old = lv_label_get_text(obj);
  if (!old || strcmp(old, text) != 0) lv_label_set_text(obj, text);
}

static void lv_button_set_text(lv_obj_t* btn, const String& text) {
  if (!btn) return;
  lv_obj_t* label = lv_obj_get_child(btn, 0);
  if (label) lv_set_text(label, text);
}

// Lossless update helpers: avoid invalidating an LVGL object if the effective
// value is already identical. This preserves the exact appearance/behavior,
// but prevents one changing telemetry value from needlessly dirtying dozens of
// otherwise unchanged cards every status packet.
static inline void lv_set_bg_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector = 0) {
  if (!obj) return;
  const lv_part_t part = (lv_part_t)(selector & 0xFFFFU);
  if (!lv_color_eq(lv_obj_get_style_bg_color(obj, part), color))
    lv_obj_set_style_bg_color(obj, color, selector);
}

static inline void lv_set_bg_grad_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector = 0) {
  if (!obj) return;
  const lv_part_t part = (lv_part_t)(selector & 0xFFFFU);
  if (!lv_color_eq(lv_obj_get_style_bg_grad_color(obj, part), color))
    lv_obj_set_style_bg_grad_color(obj, color, selector);
}

static inline void lv_set_border_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector = 0) {
  if (!obj) return;
  const lv_part_t part = (lv_part_t)(selector & 0xFFFFU);
  if (!lv_color_eq(lv_obj_get_style_border_color(obj, part), color))
    lv_obj_set_style_border_color(obj, color, selector);
}

static inline void lv_set_text_color_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector = 0) {
  if (!obj) return;
  const lv_part_t part = (lv_part_t)(selector & 0xFFFFU);
  if (!lv_color_eq(lv_obj_get_style_text_color(obj, part), color))
    lv_obj_set_style_text_color(obj, color, selector);
}

static inline void lv_set_image_recolor_if_changed(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector = 0) {
  if (!obj) return;
  const lv_part_t part = (lv_part_t)(selector & 0xFFFFU);
  if (!lv_color_eq(lv_obj_get_style_image_recolor(obj, part), color))
    lv_obj_set_style_image_recolor(obj, color, selector);
}

static inline void lv_bar_set_value_if_changed(lv_obj_t* obj, int32_t value) {
  if (!obj) return;
  if (lv_bar_get_value(obj) != value) lv_bar_set_value(obj, value, LV_ANIM_OFF);
}

static inline void lv_slider_set_value_if_changed(lv_obj_t* obj, int32_t value) {
  if (!obj) return;
  if (lv_slider_get_value(obj) != value) lv_slider_set_value(obj, value, LV_ANIM_OFF);
}

static inline void lv_dropdown_set_selected_if_changed(lv_obj_t* obj, uint32_t selected) {
  if (!obj) return;
  if (lv_dropdown_get_selected(obj) != selected) lv_dropdown_set_selected(obj, selected);
}

static inline void lv_textarea_set_text_if_changed(lv_obj_t* obj, const char* text) {
  if (!obj) return;
  if (!text) text = "";
  const char* old = lv_textarea_get_text(obj);
  if (!old || strcmp(old, text) != 0) lv_textarea_set_text(obj, text);
}

static inline void lv_set_disabled_if_changed(lv_obj_t* obj, bool disabled) {
  if (!obj) return;
  const bool old = lv_obj_has_state(obj, LV_STATE_DISABLED);
  if (old == disabled) return;
  if (disabled) lv_obj_add_state(obj, LV_STATE_DISABLED);
  else lv_obj_remove_state(obj, LV_STATE_DISABLED);
}

static void lv_set_detail_work_icon(bool visible, bool active) {
  if (!ui_detail_work_icon) return;
  const bool hidden = lv_obj_has_flag(ui_detail_work_icon, LV_OBJ_FLAG_HIDDEN);
  if (visible && hidden) lv_obj_clear_flag(ui_detail_work_icon, LV_OBJ_FLAG_HIDDEN);
  else if (!visible && !hidden) lv_obj_add_flag(ui_detail_work_icon, LV_OBJ_FLAG_HIDDEN);
  lv_set_image_recolor_if_changed(ui_detail_work_icon,
    active ? lv_color_hex(0x2DFF88) : lv_color_hex(0x66717D), 0);
  if (lv_obj_get_style_image_recolor_opa(ui_detail_work_icon, LV_PART_MAIN) != LV_OPA_COVER)
    lv_obj_set_style_image_recolor_opa(ui_detail_work_icon, LV_OPA_COVER, 0);
}

static void lv_set_home_work_icon(bool visible, bool active) {
  if (!ui_home_work_icon) return;
  const bool hidden = lv_obj_has_flag(ui_home_work_icon, LV_OBJ_FLAG_HIDDEN);
  if (visible && hidden) lv_obj_clear_flag(ui_home_work_icon, LV_OBJ_FLAG_HIDDEN);
  else if (!visible && !hidden) lv_obj_add_flag(ui_home_work_icon, LV_OBJ_FLAG_HIDDEN);
  lv_set_image_recolor_if_changed(ui_home_work_icon,
    active ? lv_color_hex(0x2DFF88) : lv_color_hex(0x66717D), 0);
  if (lv_obj_get_style_image_recolor_opa(ui_home_work_icon, LV_PART_MAIN) != LV_OPA_COVER)
    lv_obj_set_style_image_recolor_opa(ui_home_work_icon, LV_OPA_COVER, 0);
}
static void lv_style_screen(lv_obj_t* screen) {
  lv_obj_set_style_bg_color(screen, ui_theme_color(0x090D12, 0xE9EFF6), 0);
#if DISPLAY_FAST_UI
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
#else
  lv_obj_set_style_bg_grad_color(screen, ui_theme_color(0x111A26, 0xDCE6F0), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
#endif
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static uint8_t master_link_state_now() {
  if (!last_master_ms) return 0;  // still waiting for the first master frame
  return ((uint32_t)(millis() - last_master_ms) <= MASTER_LINK_TIMEOUT_MS) ? 1 : 2;
}

static bool master_link_online() {
  return master_link_state_now() == 1;
}

static bool master_link_lost() {
  return master_link_state_now() == 2;
}

#include "src/OfeDisplayWifiUi.inc.h"

static const char* master_link_text(uint8_t state) {
  (void)state;
  return display_wifi.wireless() ? LV_SYMBOL_WIFI : OFE_SYMBOL_SERIAL_PORT;
}

static lv_color_t master_link_bg(uint8_t state) {
  if (state == 1) return lv_color_hex(0x14241E);
  if (state == 2) return lv_color_hex(0x25191C);
  return lv_color_hex(0x242117);
}

static lv_color_t master_link_border(uint8_t state) {
  if (state == 1) return lv_color_hex(0x315845);
  if (state == 2) return lv_color_hex(0x5A3036);
  return lv_color_hex(0x51452B);
}

static lv_color_t master_link_text_color(uint8_t state) {
  if (state == 1) return lv_color_hex(0x55D98A);
  if (state == 2) return lv_color_hex(0xFF6B78);
  return lv_color_hex(0xE5B85C);
}

static bool lv_update_master_link_ui(bool force = false) {
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_MASTER_LINK | UI_DEFER_APP_VALUES);
    return false;
  }
  if (!lvgl_ready && ui_header_link_count == 0) return false;
  static bool previous_wifi = false;
  const bool wifi = display_wifi.wireless();
  if (wifi != previous_wifi) { previous_wifi = wifi; force = true; }
  const uint8_t state = master_link_state_now();
  if (!force && state == ui_header_link_draw_state) return false;
  const uint8_t old_state = ui_header_link_draw_state;
  if (state == 2) master_session_reset_pending = true;
  if (state == 1 && master_session_reset_pending) {
    reset_expected_modules();
    master_uptime_valid = false;
    master_session_reset_pending = false;
  }
  for (uint8_t i = 0; i < ui_header_link_count; ++i) {
    if (ui_header_link_cards[i]) {
      lv_obj_set_style_bg_color(ui_header_link_cards[i], master_link_bg(state), 0);
      lv_obj_set_style_border_color(ui_header_link_cards[i], master_link_border(state), 0);
    }
    if (ui_header_link_labels[i]) {
      lv_label_set_text(ui_header_link_labels[i], master_link_text(state));
      ofe_serial_port::center_icon(ui_header_link_labels[i], wifi);
      lv_obj_set_style_text_color(ui_header_link_labels[i], master_link_text_color(state), 0);
    }
  }
  ui_header_link_draw_state = state;

  // The module list is a cache from the master. If the master link changes,
  // redraw it so cached modules are no longer shown as online while the master
  // is lost, and switch back automatically when the master returns.
  if (force || old_state != state) {
    if (ui_module_list_screen && lv_screen_active() == ui_module_list_screen) lv_update_module_list();
    if (ui_module_detail_screen && lv_screen_active() == ui_module_detail_screen) lv_update_module_detail();
  }
  return true;
}

static void lv_update_clock_ui() {
  char text[24] = "--:--";
  if (status.clock_valid) {
    if (status.clock_day && status.clock_month && status.clock_year) snprintf(text, sizeof(text), "%02u:%02u\n%02u.%02u.%04u", status.clock_hour, status.clock_minute, status.clock_day, status.clock_month, status.clock_year);
    else if (status.clock_day && status.clock_month) snprintf(text, sizeof(text), "%02u:%02u\n%02u.%02u", status.clock_hour, status.clock_minute, status.clock_day, status.clock_month);
    else snprintf(text, sizeof(text), "%02u:%02u", status.clock_hour, status.clock_minute);
  }
  for (uint8_t i = 0; i < ui_header_link_count; ++i) {
    if (ui_header_clock_labels[i] && lv_obj_is_valid(ui_header_clock_labels[i])) lv_set_text_c(ui_header_clock_labels[i], text);
  }
}
static void lv_show_alarms_event(lv_event_t* e);

static void lv_update_alarm_header_ui() {
  char text[16];
  snprintf(text, sizeof(text), LV_SYMBOL_BELL " %u", active_alarm_count);
  const bool has_critical_alarm = active_alarm_count && status.master_alarm_critical_mask;
  const lv_color_t alarm_color = has_critical_alarm ? lv_color_hex(0xFF4D5E) : lv_color_hex(0xFFB020);
  for (uint8_t i = 0; i < ui_header_link_count; ++i) {
    lv_obj_t* alarm = ui_header_alarm_labels[i];
    if (!alarm || !lv_obj_is_valid(alarm)) continue;
    if (active_alarm_count) {
      lv_set_text_c(alarm, text);
      lv_set_text_color_if_changed(alarm, alarm_color, 0);
      lv_set_visible(alarm, true);
    } else {
      lv_set_visible(alarm, false);
    }
  }
}
static void lv_add_header(lv_obj_t* screen, const char* subtitle) {
  lv_obj_t* header = lv_obj_create(screen);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, DISPLAY_RGB_WIDTH, 42);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_bg_color(header, ui_theme_color(0x0D141D, 0xF8FAFC), 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, ui_theme_color(0x253241, 0xCCD8E3), 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);

  const char* localized_subtitle = subtitle;
  if (strcmp(subtitle, "extractor") == 0) localized_subtitle = tr("extractor", "Absaugung");
  else if (strcmp(subtitle, "bus modules") == 0) localized_subtitle = tr("bus modules", "Busmodule");
  else if (strcmp(subtitle, "module details") == 0) localized_subtitle = tr("module details", "Moduldetails");
  else if (strcmp(subtitle, "display settings") == 0) localized_subtitle = tr("display settings", "Einstellungen");
  else if (strcmp(subtitle, "alarms") == 0) localized_subtitle = tr("alarms", "Alarme");
  else if (strcmp(subtitle, "display update") == 0) localized_subtitle = tr("FW Update", "FW-Update");
  else if (strcmp(subtitle, "display module") == 0) localized_subtitle = tr("display module", "Displaymodul");
  lv_obj_t* sub = lv_label(header, localized_subtitle, 12, 11, lv_color_hex(0x7F8C9B), UI_FONT_DEFAULT, 170);
  lv_obj_set_size(sub, 170, 20);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_LEFT, 0);

  lv_obj_t* title = lv_label(header, "Open Fume Extractor", 220, 3, lv_color_hex(0xF7FAFF), UI_FONT_DEFAULT, 360);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* author = lv_label(header, "by IceCube20", 220, 22, lv_color_hex(0x718092), UI_FONT_DEFAULT, 360);
  lv_obj_set_style_text_align(author, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* alarm_lbl = lv_label(header, LV_SYMBOL_BELL, 686, 11, lv_color_hex(0xFF5A67), UI_FONT_DEFAULT, 48);
  lv_obj_set_style_text_align(alarm_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(alarm_lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(alarm_lbl, lv_show_alarms_event, LV_EVENT_CLICKED, NULL);
  if (!active_alarm_count) lv_obj_add_flag(alarm_lbl, LV_OBJ_FLAG_HIDDEN);

  char clock_text[24] = "--:--";
  if (status.clock_valid) {
    if (status.clock_day && status.clock_month && status.clock_year) snprintf(clock_text, sizeof(clock_text), "%02u:%02u\n%02u.%02u.%04u", status.clock_hour, status.clock_minute, status.clock_day, status.clock_month, status.clock_year);
    else if (status.clock_day && status.clock_month) snprintf(clock_text, sizeof(clock_text), "%02u:%02u\n%02u.%02u", status.clock_hour, status.clock_minute, status.clock_day, status.clock_month);
    else snprintf(clock_text, sizeof(clock_text), "%02u:%02u", status.clock_hour, status.clock_minute);
  }
  lv_obj_t* clock_lbl = lv_label(header, clock_text, 590, 4, ui_theme_color(0xDDE4EC, 0x33404D), UI_FONT_DEFAULT, 92);
  lv_obj_set_style_text_align(clock_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(clock_lbl, -2, 0);
  lv_label_set_long_mode(clock_lbl, LV_LABEL_LONG_CLIP);

  const uint8_t link_state = master_link_state_now();
  // Serial/WiFi status shares one small glyph and the existing touch target.
  lv_obj_t* link_lbl = lv_label(header, master_link_text(link_state), 748, 11, master_link_text_color(link_state), UI_FONT_DEFAULT, 38);
  lv_obj_set_size(link_lbl, 38, 20);
  lv_obj_set_style_text_font(link_lbl, ofe_serial_port::font(UI_FONT_DEFAULT), 0);
  lv_obj_set_style_text_align(link_lbl, LV_TEXT_ALIGN_CENTER, 0);
  ofe_serial_port::center_icon(link_lbl, display_wifi.wireless());
  lv_obj_add_flag(link_lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(link_lbl, OfeDisplayWifiUi::openEvent, LV_EVENT_CLICKED, nullptr);

  if (ui_header_link_count < 10) {
    ui_header_link_cards[ui_header_link_count] = nullptr;
    ui_header_link_labels[ui_header_link_count] = link_lbl;
    ui_header_clock_labels[ui_header_link_count] = clock_lbl;
    ui_header_alarm_labels[ui_header_link_count] = alarm_lbl;
    ui_header_link_count++;
  }
  lv_update_master_link_ui(true);
  lv_update_alarm_header_ui();
}
static void lv_screen_switch(lv_obj_t* screen) {
  if (lvgl_ready && screen) {
    if (screensaver_active && screen != ui_screensaver_screen) {
      screensaver_active = false;
      screensaver_return_screen = nullptr;
      apply_backlight();
    }

    // Do not recurse into lv_timer_handler() from an LVGL event callback.
    // lv_screen_load() invalidates the target; the normal 5-ms service loop /
    // 16-ms refresh timer renders it immediately after the event returns.
    lv_screen_load(screen);
  }
}

static void lv_update_page_dots() {
  for (uint8_t i = 0; i < 5; i++) {
    if (!ui_page_dots[i]) continue;
    lv_obj_set_style_bg_color(ui_page_dots[i], display_page == i ? lv_color_hex(0x58B8FF) : ui_theme_color(0x47515D, 0xB9C6D3), 0);
    lv_obj_set_style_bg_opa(ui_page_dots[i], display_page == i ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_size(ui_page_dots[i], display_page == i ? 18 : 7, display_page == i ? 7 : 7);
  }
  for (uint8_t i = 0; i < 5; i++) {
    if (!ui_page_tabs[i]) continue;
    const bool active = display_page == i;
    lv_obj_set_style_bg_color(ui_page_tabs[i], active ? lv_color_hex(0x246BFF) : ui_theme_color(0x17202B, 0xEAF1F7), 0);
    lv_obj_set_style_bg_grad_color(ui_page_tabs[i], active ? lv_color_hex(0x58B8FF) : ui_theme_color(0x111820, 0xDDE7F1), 0);
    lv_obj_set_style_border_color(ui_page_tabs[i], active ? lv_color_hex(0x8AD7FF) : ui_theme_color(0x303A46, 0xB5C5D5), 0);
  }
}

static void lv_dashboard_set_page(uint8_t page) {
  if (page > 4) page = 0;
  display_page = page;
  for (uint8_t i = 0; i < 5; i++) {
    if (ui_pages[i]) {
      if (i == display_page) lv_obj_clear_flag(ui_pages[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(ui_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  lv_update_page_dots();
  const char* titles[5] = {"Overview", "JBC Bus", "Fan / IO", "Weller", "System"};
  lv_set_text_c(ui_page_title, titles[display_page]);
}

static void lv_dashboard_tab_event(lv_event_t* e) {
  const uintptr_t page = (uintptr_t)lv_event_get_user_data(e);
  lv_dashboard_set_page((uint8_t)page);
}

static void lv_brightness_down_event(lv_event_t* e) {
  (void)e;
  set_brightness(display_brightness_pct <= 20 ? 10 : display_brightness_pct - 10, true);
  lv_set_text(ui_brightness, String(display_brightness_pct) + "%");
  if (ui_brightness_bar) lv_bar_set_value_if_changed(ui_brightness_bar, display_brightness_pct);
}

static void lv_brightness_up_event(lv_event_t* e) {
  (void)e;
  set_brightness(display_brightness_pct >= 90 ? 100 : display_brightness_pct + 10, true);
  lv_set_text(ui_brightness, String(display_brightness_pct) + "%");
  if (ui_brightness_bar) lv_bar_set_value_if_changed(ui_brightness_bar, display_brightness_pct);
}

static uint8_t screensaver_timeout_from_selection(uint8_t selected) {
  const uint8_t minutes[] = {0, 1, 2, 5, 10};
  return selected < 5 ? minutes[selected] : 2;
}

static uint8_t screensaver_selection_from_timeout(uint8_t minutes) {
  const uint8_t values[] = {0, 1, 2, 5, 10};
  for (uint8_t i = 0; i < 5; ++i) if (values[i] == minutes) return i;
  return 2;
}

static void lv_update_display_settings_widgets(bool force) {
  if (!lvgl_ready) return;

  if (ui_system_brightness) {
    lv_set_text(ui_system_brightness, String(display_brightness_pct) + "%");
  }
  if (ui_brightness) {
    lv_set_text(ui_brightness, String(display_brightness_pct) + "%");
  }
  if (ui_brightness_bar) {
    lv_bar_set_value_if_changed(ui_brightness_bar, display_brightness_pct);
  }

  if (ui_system_language && (force || !lv_dropdown_is_open(ui_system_language))) {
    lv_dropdown_set_selected(ui_system_language, display_language > 1 ? 0 : display_language);
  }
  if (ui_system_theme && (force || !lv_dropdown_is_open(ui_system_theme))) {
    lv_dropdown_set_selected(ui_system_theme, display_theme > 1 ? 0 : display_theme);
  }
  if (ui_system_screensaver && (force || !lv_dropdown_is_open(ui_system_screensaver))) {
    lv_dropdown_set_selected(ui_system_screensaver, screensaver_selection_from_timeout(screensaver_timeout_min));
  }

  if (ui_detail_display_brightness_value) {
    lv_set_text(ui_detail_display_brightness_value, String(display_brightness_pct) + "%");
  }
  if (ui_detail_display_brightness_slider && !lv_obj_has_state(ui_detail_display_brightness_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(ui_detail_display_brightness_slider, display_brightness_pct, LV_ANIM_OFF);
  }
  if (ui_detail_display_language && (force || !lv_dropdown_is_open(ui_detail_display_language))) {
    lv_dropdown_set_selected(ui_detail_display_language, display_language > 1 ? 0 : display_language);
  }
  if (ui_detail_display_theme && (force || !lv_dropdown_is_open(ui_detail_display_theme))) {
    lv_dropdown_set_selected(ui_detail_display_theme, display_theme > 1 ? 0 : display_theme);
  }
}

static void apply_screensaver_timeout(uint8_t minutes) {
  screensaver_timeout_min = minutes;
  prefs.putUChar("saver", screensaver_timeout_min);
  display_resync_after_flash_write();
  last_user_activity_ms = millis();
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_DISPLAY_SETTINGS);
  } else {
    lv_update_display_settings_widgets(true);
  }
  if (!screensaver_timeout_min && screensaver_active) {
    // This can be called from the RS485 task when the Master changes the
    // Display settings. Never touch LVGL/backlight from that task; wake in
    // the normal LVGL loop instead.
    if (running_in_rs485_task()) screensaver_wake_deferred = true;
    else screensaver_wake();
  }
}

static void lv_screensaver_timeout_event(lv_event_t* e) {
  apply_screensaver_timeout(screensaver_timeout_from_selection(
    (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e))));
}
static void lv_suction_next_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_SUCTION_NEXT);
}

static void lv_power_down_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_CUSTOM_POWER_DELTA, -10);
}

static void lv_power_up_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_CUSTOM_POWER_DELTA, 10);
}

static void lv_delay_down_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_DELAY_WORK_DELTA, -1);
}

static void lv_delay_up_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_DELAY_WORK_DELTA, 1);
}

static void lv_delay_stand_down_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_DELAY_STAND_DELTA, -1);
}

static void lv_delay_stand_up_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_DELAY_STAND_DELTA, 1);
}

static void lv_stand_intakes_toggle_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_STAND_INTAKES_TOGGLE);
}

static void lv_continuous_toggle_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_CONTINUOUS_TOGGLE);
}

static void lv_weller_speed_down_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_WELLER_SPEED_DELTA, -10);
}

static void lv_weller_speed_up_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_WELLER_SPEED_DELTA, 10);
}

static void lv_weller_fan_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_WELLER_FAN_TOGGLE);
}

static void lv_weller_light_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_WELLER_LIGHT_TOGGLE);
}

static void lv_weller_reset_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_WELLER_RESET_FILTER);
}

static void lv_weller_filter_time_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_WELLER_FILTER_TIME_NEXT);
}

static void lv_io_out1_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_IO_OUT_TOGGLE, 0);
}

static void lv_io_out2_event(lv_event_t* e) {
  (void)e;
  queue_display_event(DISPLAY_EVENT_IO_OUT_TOGGLE, 1);
}

static void lv_open_boot_connection_setup(lv_event_t*) {
  display_boot_setup_prompted = true;
  last_user_activity_ms = millis();
  OfeDisplayWifiUi::openPanel();
  display_wifi.scan();
}

static void lv_create_boot_screen() {
  ui_boot_screen = lv_obj_create(NULL);
  lv_style_screen(ui_boot_screen);
  lv_add_header(ui_boot_screen, "display module");
  lv_obj_t* card = lv_card(ui_boot_screen, 120, 78, 560, 166, ui_theme_color(0x121C28, 0xF7FAFC));
  lv_obj_set_style_border_color(card, ui_theme_color(0x34516B, 0xC4D0DB), 0);

  lv_obj_t* logo = lv_image_create(card);
  lv_image_set_src(logo, &openfume_boot_logo);
  lv_obj_set_pos(logo, 20, 24);

  lv_label(card, "Open Fume Extractor", 112, 18, lv_color_hex(0xFFFFFF), UI_FONT_22, 420);
  lv_label(card, "by IceCube20", 112, 47, lv_color_hex(0x58B8FF), UI_FONT_18, 420);
  lv_label(card, tr("Touch control", "Touch-Bedienung"), 112, 70, lv_color_hex(0x2DFF88), UI_FONT_DEFAULT, 420);
  ui_boot_fw = lv_label(card, "", 112, 104, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 190);
  ui_boot_addr = lv_label(card, "", 330, 104, lv_color_hex(0xDDE4EC), UI_FONT_DEFAULT, 190);
  lv_obj_t* wait_card = lv_card(ui_boot_screen, 120, 256, 560, 38, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_label(wait_card, tr("Connecting to master... Tap for setup", "Verbinde mit Master... Antippen zum Einrichten"), 16, 10, lv_color_hex(0x96A0AA), UI_FONT_DEFAULT, 528);
  lv_obj_add_flag(wait_card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wait_card, lv_open_boot_connection_setup, LV_EVENT_CLICKED, nullptr);
}

static void lv_create_update_screen() {
  ui_update_screen = lv_obj_create(NULL);
  lv_style_screen(ui_update_screen);
  lv_add_header(ui_update_screen, "display update");

  lv_obj_t* hero = lv_card(ui_update_screen, 20, 62, 760, 146, ui_theme_color(0x101D2C, 0xF7FAFC));
  lv_obj_set_style_border_color(hero, ui_theme_color(0x1F7AFF, 0x9CCBFF), 0);

  lv_obj_t* chip = lv_obj_create(hero);
  lv_obj_set_pos(chip, 16, 14);
  lv_obj_set_size(chip, 112, 24);
  lv_obj_set_style_radius(chip, 12, 0);
  lv_obj_set_style_bg_color(chip, ui_theme_color(0x17375C, 0xE1F0FF), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, ui_theme_color(0x2997FF, 0x9CCBFF), 0);
  lv_obj_set_style_pad_all(chip, 0, 0);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* chip_label = lv_label(chip, tr("LOCAL OTA", "OTA LOKAL"), 0, 4, ui_theme_color(0x8CC8FF, 0x175CD3), UI_FONT_DEFAULT, 112);
  lv_obj_set_size(chip_label, 112, 18);
  lv_label_set_long_mode(chip_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(chip_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_label(hero, tr("Display firmware", "Display-Firmware"), 16, 44, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_18, 380);
  ui_update_target = lv_label(hero, "", 16, 74, ui_theme_color(0xB9C6D3, 0x455466), UI_FONT_DEFAULT, 420);
  ui_update_status = lv_label(hero, "", 16, 101, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 440);
  ui_update_percent = lv_label(hero, "0%", 594, 28, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_28, 140);
  ui_update_phase = lv_label(hero, tr("Starting", "Start"), 584, 94, lv_color_hex(0x2DFF88), UI_FONT_DEFAULT, 150);

  ui_update_bar = lv_bar_create(hero);
  lv_obj_set_pos(ui_update_bar, 16, 126);
  lv_obj_set_size(ui_update_bar, 728, 12);
  lv_bar_set_range(ui_update_bar, 0, 100);
  lv_obj_set_style_radius(ui_update_bar, 6, 0);
  lv_obj_set_style_radius(ui_update_bar, 6, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui_update_bar, ui_theme_color(0x283241, 0xD7E2ED), 0);
  lv_obj_set_style_bg_opa(ui_update_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ui_update_bar, lv_color_hex(0x2DFF88), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(ui_update_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_clear_flag(ui_update_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ui_update_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* written = lv_card(ui_update_screen, 20, 218, 230, 54, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_label(written, tr("Written", "Empfangen"), 12, 8, ui_theme_color(0x8F9BA8, 0x5E6D7C), UI_FONT_DEFAULT, 100);
  ui_update_written = lv_label(written, "0 KB", 12, 29, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 200);

  lv_obj_t* size = lv_card(ui_update_screen, 284, 218, 230, 54, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_label(size, tr("Image", "Datei"), 12, 8, ui_theme_color(0x8F9BA8, 0x5E6D7C), UI_FONT_DEFAULT, 100);
  ui_update_size = lv_label(size, "-", 12, 29, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 200);

  lv_obj_t* speed = lv_card(ui_update_screen, 548, 218, 232, 54, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_label(speed, tr("Speed", "Tempo"), 12, 8, ui_theme_color(0x8F9BA8, 0x5E6D7C), UI_FONT_DEFAULT, 100);
  ui_update_speed = lv_label(speed, "-", 12, 29, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 202);

  ui_update_detail = lv_label(ui_update_screen, tr("RS485 firmware transfer", "RS485-Firmware wird empfangen"), 32, 286, ui_theme_color(0xB9C6D3, 0x5E6D7C), UI_FONT_DEFAULT, 520);
  ui_update_hint = lv_label(ui_update_screen, tr("Do not unplug", "Nicht trennen!"), 600, 286, lv_color_hex(0xFFB020), UI_FONT_DEFAULT, 170);
  lv_obj_set_style_text_align(ui_update_hint, LV_TEXT_ALIGN_RIGHT, 0);
}

static void lv_create_dashboard_screen() {
  ui_dashboard_screen = lv_obj_create(NULL);
  lv_style_screen(ui_dashboard_screen);
  lv_add_header(ui_dashboard_screen, "status");
  ui_page_title = lv_label(ui_dashboard_screen, "Status", 18, 52, lv_color_hex(0xFFFFFF), UI_FONT_18, 180);

  for (uint8_t i = 0; i < 5; i++) {
    ui_pages[i] = lv_obj_create(ui_dashboard_screen);
    lv_obj_set_pos(ui_pages[i], 12, 78);
    lv_obj_set_size(ui_pages[i], 456, 190);
    lv_obj_set_style_bg_opa(ui_pages[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_pages[i], 0, 0);
    lv_obj_set_style_pad_all(ui_pages[i], 0, 0);
    lv_obj_clear_flag(ui_pages[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui_pages[i], LV_OBJ_FLAG_CLICKABLE);
  }

  ui_output_card = lv_card(ui_pages[0], 0, 0, 456, 84, ui_theme_color(0x102C24, 0xEAF7F0));
  lv_obj_set_style_border_color(ui_output_card, lv_color_hex(0x2E6D55), 0);
  lv_label(ui_output_card, "Extraction", 16, 9, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 160);
  ui_output_state = lv_label(ui_output_card, "Idle", 16, 32, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 150);
  ui_output_power = lv_label(ui_output_card, "Power -", 254, 18, lv_color_hex(0x2DFF88), UI_FONT_DEFAULT, 180);
  ui_afterrun = lv_label(ui_output_card, "After 0s", 254, 45, lv_color_hex(0xDDE4EC), UI_FONT_DEFAULT, 180);
  ui_output_bar = lv_mini_bar(ui_output_card, 16, 66, 424, 8, lv_color_hex(0x2DFF88));

  ui_jbc_card = lv_value_card(ui_pages[0], 0, 96, 142, 42, "JBC", &ui_jbc_state, lv_color_hex(0x2DFF88));
  ui_weller_card = lv_value_card(ui_pages[0], 156, 96, 142, 42, "Weller", &ui_weller_state, lv_color_hex(0x2997FF));
  ui_output_addr_card = lv_value_card(ui_pages[0], 312, 96, 144, 42, "Output", &ui_output_addr, lv_color_hex(0xFFB020));
  ui_fan_card = lv_value_card(ui_pages[0], 0, 148, 142, 42, "Fan", &ui_fan_rpm, lv_color_hex(0x2997FF));
  lv_value_card(ui_pages[0], 156, 148, 142, 42, "Modules", &ui_modules, lv_color_hex(0xB98CFF));
  ui_work_card = lv_value_card(ui_pages[0], 312, 148, 144, 42, "Work", &ui_work_mask, lv_color_hex(0xFFB020));

  ui_station_card = lv_value_card(ui_pages[1], 0, 0, 108, 42, "Station", &ui_station, lv_color_hex(0x2DFF88));
  ui_suction_card = lv_value_card(ui_pages[1], 116, 0, 108, 42, "Mode", &ui_suction, lv_color_hex(0x2997FF));
  ui_custom_card = lv_value_card(ui_pages[1], 232, 0, 108, 42, "Power", &ui_custom_power, lv_color_hex(0xB98CFF));
  ui_jbc_detail_card = lv_value_card(ui_pages[1], 348, 0, 108, 42, "Work / Stand", &ui_jbc_detail, lv_color_hex(0x2DFF88));
  ui_delay_card = lv_value_card(ui_pages[1], 0, 50, 108, 42, "Delay Work", &ui_delay, lv_color_hex(0xFFB020));
  lv_value_card(ui_pages[1], 116, 50, 108, 42, "Delay Stand", &ui_delay_stand, lv_color_hex(0xFFB020));
  lv_value_card(ui_pages[1], 232, 50, 108, 42, "Stand Intakes", &ui_stand_intakes, lv_color_hex(0x2997FF));
  ui_continuous_card = lv_value_card(ui_pages[1], 348, 50, 108, 42, "Continuous", &ui_continuous, lv_color_hex(0x2997FF));

  ui_control_jbc_row = lv_obj_create(ui_pages[1]);
  lv_obj_set_pos(ui_control_jbc_row, 0, 100);
  lv_obj_set_size(ui_control_jbc_row, 456, 40);
  lv_obj_set_style_bg_opa(ui_control_jbc_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_control_jbc_row, 0, 0);
  lv_obj_set_style_pad_all(ui_control_jbc_row, 0, 0);
  lv_obj_clear_flag(ui_control_jbc_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_small_button(ui_control_jbc_row, 0, 0, 80, "Mode", lv_suction_next_event);
  lv_small_button(ui_control_jbc_row, 88, 0, 64, "P -", lv_power_down_event);
  lv_small_button(ui_control_jbc_row, 160, 0, 64, "P +", lv_power_up_event);
  lv_small_button(ui_control_jbc_row, 232, 0, 104, "Stand", lv_stand_intakes_toggle_event);
  lv_small_button(ui_control_jbc_row, 344, 0, 112, "Continuous", lv_continuous_toggle_event);

  lv_obj_t* jbc_delay_row = lv_obj_create(ui_pages[1]);
  lv_obj_set_pos(jbc_delay_row, 0, 148);
  lv_obj_set_size(jbc_delay_row, 456, 40);
  lv_obj_set_style_bg_opa(jbc_delay_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(jbc_delay_row, 0, 0);
  lv_obj_set_style_pad_all(jbc_delay_row, 0, 0);
  lv_obj_clear_flag(jbc_delay_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_small_button(jbc_delay_row, 0, 0, 104, "Work -", lv_delay_down_event);
  lv_small_button(jbc_delay_row, 112, 0, 104, "Work +", lv_delay_up_event);
  lv_small_button(jbc_delay_row, 240, 0, 104, "Stand -", lv_delay_stand_down_event);
  lv_small_button(jbc_delay_row, 352, 0, 104, "Stand +", lv_delay_stand_up_event);

  lv_value_card(ui_pages[2], 0, 0, 142, 42, "Output", &ui_fan_detail_output, lv_color_hex(0x2DFF88));
  lv_value_card(ui_pages[2], 156, 0, 142, 42, "Power", &ui_fan_detail_power, lv_color_hex(0xB98CFF));
  lv_value_card(ui_pages[2], 312, 0, 144, 42, "RPM", &ui_fan_detail_rpm, lv_color_hex(0x2997FF));
  lv_value_card(ui_pages[2], 0, 52, 142, 42, "Inputs", &ui_fan_detail_inputs, lv_color_hex(0x2997FF));
  lv_value_card(ui_pages[2], 156, 52, 142, 42, "Outputs", &ui_fan_detail_outputs, lv_color_hex(0xFFB020));
  lv_value_card(ui_pages[2], 312, 52, 144, 42, "Fault", &ui_fan_detail_fault, lv_color_hex(0xFF5B5B));
  ui_control_output_row = lv_obj_create(ui_pages[2]);
  lv_obj_set_pos(ui_control_output_row, 0, 148);
  lv_obj_set_size(ui_control_output_row, 456, 40);
  lv_obj_set_style_bg_opa(ui_control_output_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_control_output_row, 0, 0);
  lv_obj_set_style_pad_all(ui_control_output_row, 0, 0);
  lv_obj_clear_flag(ui_control_output_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_small_button(ui_control_output_row, 0, 0, 220, "Toggle OUT1", lv_io_out1_event);
  lv_small_button(ui_control_output_row, 236, 0, 220, "Toggle OUT2", lv_io_out2_event);

  lv_value_card(ui_pages[3], 0, 0, 108, 42, "Link", &ui_weller_detail_link, lv_color_hex(0x2DFF88));
  lv_value_card(ui_pages[3], 116, 0, 108, 42, "Speed", &ui_weller_detail_speed, lv_color_hex(0x2997FF));
  lv_value_card(ui_pages[3], 232, 0, 108, 42, "RPM", &ui_weller_detail_rpm, lv_color_hex(0x2997FF));
  lv_value_card(ui_pages[3], 348, 0, 108, 42, "SW", &ui_weller_detail_sw, lv_color_hex(0xB98CFF));
  lv_value_card(ui_pages[3], 0, 52, 108, 42, "Fan", &ui_weller_detail_fan, lv_color_hex(0x2DFF88));
  lv_value_card(ui_pages[3], 116, 52, 108, 42, "Light", &ui_weller_detail_light, lv_color_hex(0xFFB020));
  ui_weller_filter_card = lv_value_card(ui_pages[3], 232, 52, 108, 42, "Filter", &ui_weller_detail_filter, lv_color_hex(0x2DFF88));
  lv_value_card(ui_pages[3], 348, 52, 108, 42, "Filter time", &ui_weller_detail_runtime, lv_color_hex(0x2997FF));
  ui_control_weller_row = lv_obj_create(ui_pages[3]);
  lv_obj_set_pos(ui_control_weller_row, 0, 104);
  lv_obj_set_size(ui_control_weller_row, 456, 40);
  lv_obj_set_style_bg_opa(ui_control_weller_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_control_weller_row, 0, 0);
  lv_obj_set_style_pad_all(ui_control_weller_row, 0, 0);
  lv_obj_clear_flag(ui_control_weller_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_small_button(ui_control_weller_row, 0, 0, 104, "Fan", lv_weller_fan_event);
  lv_small_button(ui_control_weller_row, 112, 0, 104, "Light", lv_weller_light_event);
  lv_small_button(ui_control_weller_row, 240, 0, 104, "Speed -", lv_weller_speed_down_event);
  lv_small_button(ui_control_weller_row, 352, 0, 104, "Speed +", lv_weller_speed_up_event);
  lv_small_button(ui_pages[3], 0, 150, 220, "Next filter time", lv_weller_filter_time_event);
  lv_small_button(ui_pages[3], 236, 150, 220, "Reset filter", lv_weller_reset_event);

  lv_obj_t* bright_card = lv_value_card(ui_pages[4], 0, 0, 220, 82, "Brightness", &ui_brightness, lv_color_hex(0xFFB020));
  ui_brightness_bar = lv_mini_bar(bright_card, 18, 62, 184, 7, lv_color_hex(0xFFB020));
  // Bigger hit areas make the brightness control usable on the small touch panel.
  lv_small_button(ui_pages[4], 236, 0, 100, "-", lv_brightness_down_event);
  lv_small_button(ui_pages[4], 356, 0, 100, "+", lv_brightness_up_event);
  lv_value_card(ui_pages[4], 236, 48, 220, 42, "Display FW", &ui_touch, lv_color_hex(0xB98CFF));
  lv_value_card(ui_pages[4], 0, 100, 142, 42, "Bus addr", &ui_addr, lv_color_hex(0x2997FF));
  lv_value_card(ui_pages[4], 156, 100, 142, 42, "Free heap", &ui_heap, lv_color_hex(0x2DFF88));
  lv_value_card(ui_pages[4], 312, 100, 144, 42, "Uptime", &ui_uptime, lv_color_hex(0xFFB020));
  lv_value_card(ui_pages[4], 0, 150, 220, 40, "CPU / Loop max", &ui_loop, lv_color_hex(0xB98CFF));
  lv_value_card(ui_pages[4], 236, 150, 220, 40, "Touch", &ui_touch_pos, lv_color_hex(0x2997FF));

  for (uint8_t i = 0; i < 5; i++) {
    ui_page_dots[i] = lv_obj_create(ui_dashboard_screen);
    lv_obj_set_pos(ui_page_dots[i], 198 + i * 17, 308);
    lv_obj_set_size(ui_page_dots[i], 7, 7);
    lv_obj_set_style_radius(ui_page_dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui_page_dots[i], 0, 0);
    lv_obj_set_style_pad_all(ui_page_dots[i], 0, 0);
    lv_obj_clear_flag(ui_page_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui_page_dots[i], LV_OBJ_FLAG_CLICKABLE);
  }

  const char* tab_labels[5] = {"Home", "JBC", "Fan", "Weller", "System"};
  for (uint8_t i = 0; i < 5; i++) {
    ui_page_tabs[i] = lv_button_create(ui_dashboard_screen);
    lv_obj_set_pos(ui_page_tabs[i], 8 + i * 94, 274);
    lv_obj_set_size(ui_page_tabs[i], 88, 30);
    lv_obj_set_style_radius(ui_page_tabs[i], 15, 0);
    lv_obj_set_style_bg_color(ui_page_tabs[i], lv_color_hex(0x17202B), 0);
    lv_obj_set_style_bg_grad_color(ui_page_tabs[i], lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_grad_dir(ui_page_tabs[i], LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(ui_page_tabs[i], 1, 0);
    lv_obj_set_style_border_color(ui_page_tabs[i], lv_color_hex(0x303A46), 0);
    lv_obj_set_style_shadow_width(ui_page_tabs[i], 4, 0);
    lv_obj_set_style_shadow_opa(ui_page_tabs[i], LV_OPA_20, 0);
    lv_obj_add_event_cb(ui_page_tabs[i], lv_dashboard_tab_event, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_set_style_pad_all(ui_page_tabs[i], 0, 0);
    lv_obj_clear_flag(ui_page_tabs[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* tab_lbl = lv_label_create(ui_page_tabs[i]);
    lv_label_set_text(tab_lbl, tab_labels[i]);
    lv_obj_set_style_text_color(tab_lbl, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_obj_set_style_text_font(tab_lbl, UI_FONT_DEFAULT, 0);
    lv_obj_center(tab_lbl);
    lv_obj_remove_flag(tab_lbl, LV_OBJ_FLAG_CLICKABLE);
  }

  // Do not attach a full-screen click handler here. On LVGL touch panels it can
  // steal or immediately follow button clicks, which made +/- brightness taps
  // feel like they did not work. Page changes are done with the tabs below.
  lv_dashboard_set_page(0);
}

static void lv_create_status_screen() {
  ui_status_screen = lv_obj_create(NULL);
  lv_style_screen(ui_status_screen);
  lv_add_header(ui_status_screen, "display");
  lv_obj_t* card = lv_card(ui_status_screen, 120, 110, 560, 132, ui_theme_color(0x171C22, 0xF7FAFC));
  ui_status_msg = lv_label(card, "", 18, 40, lv_color_hex(0xDDE4EC), UI_FONT_DEFAULT, 510);
}

static void lv_create_screensaver_screen() {
  ui_screensaver_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(ui_screensaver_screen, lv_color_hex(0x030609), 0);
  lv_obj_set_style_bg_opa(ui_screensaver_screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(ui_screensaver_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_screensaver_brand = lv_label(ui_screensaver_screen, "Open Fume Extractor", 0, 22,
    lv_color_hex(0xB5C2D1), UI_FONT_DEFAULT, DISPLAY_RGB_WIDTH);
  lv_obj_set_style_text_align(ui_screensaver_brand, LV_TEXT_ALIGN_CENTER, 0);
  ui_screensaver_clock = lv_label(ui_screensaver_screen, "--:--", 0, 54,
    lv_color_hex(0xFFFFFF), UI_FONT_28, DISPLAY_RGB_WIDTH);
  lv_obj_set_style_text_align(ui_screensaver_clock, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(ui_screensaver_clock, 4, 0);
  ui_screensaver_state = lv_label(ui_screensaver_screen, "", 0, 142,
    lv_color_hex(0x55D98A), UI_FONT_DEFAULT, DISPLAY_RGB_WIDTH);
  lv_obj_set_style_text_align(ui_screensaver_state, LV_TEXT_ALIGN_CENTER, 0);
  ui_screensaver_power = lv_label(ui_screensaver_screen, "", 102, 196,
    lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 260);
  ui_screensaver_modules = lv_label(ui_screensaver_screen, "", 438, 196,
    lv_color_hex(0xD8E2EE), UI_FONT_DEFAULT, 260);
  lv_obj_set_style_text_align(ui_screensaver_modules, LV_TEXT_ALIGN_RIGHT, 0);
  ui_screensaver_alarm = lv_label(ui_screensaver_screen, "", 60, 164,
    lv_color_hex(0xFF6B78), UI_FONT_DEFAULT, 680);
  lv_obj_set_style_text_align(ui_screensaver_alarm, LV_TEXT_ALIGN_CENTER, 0);
  ui_screensaver_power_bar = lv_mini_bar(ui_screensaver_screen, 102, 224, 596, 7, lv_color_hex(0x2DFF88));
  ui_screensaver_info = lv_label(ui_screensaver_screen, "", 60, 238,
    lv_color_hex(0xB5C2D1), UI_FONT_DEFAULT, 680);
  lv_obj_set_style_text_align(ui_screensaver_info, LV_TEXT_ALIGN_CENTER, 0);
  ui_screensaver_hint = lv_label(ui_screensaver_screen,
    tr("Touch to wake", "Zum Aufwecken ber\303\274hren"), 0, 284,
    lv_color_hex(0x91A0B2), UI_FONT_DEFAULT, DISPLAY_RGB_WIDTH);
  lv_obj_set_style_text_align(ui_screensaver_hint, LV_TEXT_ALIGN_CENTER, 0);
}

static String screensaver_addr_text(uint8_t addr) {
  char buf[6];
  snprintf(buf, sizeof(buf), "0x%02X", addr);
  return String(buf);
}

static String screensaver_output_text() {
  if (!status.output_addr) return String();
  String text = String("OUT ") + module_summary_addr_name(status.output_addr);
  if (active_output_is_fan_io()) {
    text += " - ";
    text += fan_io_alias_for(status.output_addr, 0, tr("Relay/Fan", "Relais/L\303\274fter"));
  }
  return text;
}
static String screensaver_input_text() {
  if (status.main_input_source_type == 0) return tr("No input", "Kein Eingang");
  if (status.main_input_source_type == 1) {
    if (!status.main_input_source_addr) return tr("All JBC modules", "Alle JBC-Module");
    const int8_t idx = module_summary_index_by_addr(status.main_input_source_addr);
    if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) {
      return String(tr("No input", "Kein Eingang")) + " (" + module_summary_addr_name(status.main_input_source_addr) + ")";
    }
    return module_summary_addr_name(status.main_input_source_addr);
  }
  if (status.main_input_source_type == 2) {
    String suffix = String(" - ") + fan_io_alias_for(status.main_input_source_addr, status.main_input_source_bit == 0 ? 1 : 2, status.main_input_source_bit == 0 ? "IN1" : "IN2");
    const int8_t idx = module_summary_index_by_addr(status.main_input_source_addr);
    if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) {
      return String(tr("No input", "Kein Eingang")) + " (" + module_summary_addr_name(status.main_input_source_addr) + ")" + suffix;
    }
    return module_summary_addr_name(status.main_input_source_addr) + suffix;
  }
  if (status.main_input_source_type == 3) {
    const DisplayUniversalEntity* e = universal_cached_entity_by_id(status.main_input_source_addr, status.main_input_source_bit);
    String suffix = e ? String(" - ") + (e->label[0] ? String(e->label) : (String("Entity ") + String(e->id))) : String(" - Entity ") + String(status.main_input_source_bit);
    const int8_t idx = module_summary_index_by_addr(status.main_input_source_addr);
    if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) {
      return String(tr("No input", "Kein Eingang")) + " (" + module_summary_addr_name(status.main_input_source_addr) + ")" + suffix;
    }
    return module_summary_addr_name(status.main_input_source_addr) + suffix;
  }
  return tr("Input", "Eingang");
}

static bool screensaver_has_alarm_code(uint8_t code) {
  if (!status.master_alarm_valid) return false;
  for (uint8_t i = 0; i < status.master_alarm_item_count; ++i) {
    if (status.master_alarm_code[i] == code) return true;
  }
  return false;
}

static bool screensaver_alarm_hidden(uint8_t) {
  return false;
}

static int8_t screensaver_first_alarm_index() {
  if (!status.master_alarm_valid) return -1;
  for (uint8_t i = 0; i < status.master_alarm_item_count; ++i) {
    if (!screensaver_alarm_hidden(i)) return (int8_t)i;
  }
  return -1;
}

static uint8_t screensaver_alarm_count() {
  if (!master_link_online()) return 1;
  if (!status.master_alarm_valid) return 0;
  uint8_t visible = 0;
  for (uint8_t i = 0; i < status.master_alarm_item_count; ++i) {
    if (!screensaver_alarm_hidden(i)) ++visible;
  }
  if (!visible && status.master_alarm_count) return status.master_alarm_count;
  return visible;
}

static bool screensaver_alarm_is_critical() {
  if (!master_link_online()) return true;
  if (!status.master_alarm_valid) return false;
  for (uint8_t i = 0; i < status.master_alarm_item_count; ++i) {
    if (!screensaver_alarm_hidden(i) && (status.master_alarm_critical_mask & (1U << i))) return true;
  }
  return false;
}

static String screensaver_alarm_title() {
  if (!master_link_online()) return tr("Master connection lost", "Master-Verbindung verloren");
  const int8_t idx = screensaver_first_alarm_index();
  if (idx < 0) return String();
  return master_alarm_title_text(status.master_alarm_addr[idx], status.master_alarm_type[idx], status.master_alarm_code[idx]);
}

static String screensaver_alarm_detail(uint8_t alarm_count) {
  if (!master_link_online()) return tr("No current bus data", "Keine aktuellen Busdaten");
  const int8_t idx = screensaver_first_alarm_index();
  if (idx < 0) return String();
  String detail = master_alarm_detail_text(status.master_alarm_code[idx], status.master_alarm_value[idx], status.master_alarm_type[idx]);
  if (alarm_count > 1) {
    detail += "  +";
    detail += String(alarm_count - 1);
    detail += tr(" more", " weitere");
  }
  return detail;
}

static uint16_t main_output_rpm_for_ui() {
  uint16_t rpm = status.fan_rpm;
  const uint32_t now = millis();
  if (active_output_is_weller() && home_weller_cache.valid &&
      home_weller_cache.addr == status.output_addr &&
      (uint32_t)(now - home_weller_cache.last_ms) <= HOME_DETAIL_CACHE_VALID_MS &&
      home_weller_cache.rpm) {
    rpm = home_weller_cache.rpm;
  } else if (active_output_is_fan_io() && home_fan_io_cache.valid &&
             home_fan_io_cache.addr == status.output_addr &&
             (uint32_t)(now - home_fan_io_cache.last_ms) <= HOME_DETAIL_CACHE_VALID_MS &&
             home_fan_io_cache.output_rpm) {
    rpm = home_fan_io_cache.output_rpm;
  }
  if (!rpm && status.module_output_rpm) rpm = status.module_output_rpm;
  return rpm;
}

static String screensaver_jbc_text() {
  if (!status.jbc_present && !status.jbc_inputs) return String();
  DisplayStatus::JbcStation station = {};
  const uint32_t rotation_step = millis() / 5000UL;
  portENTER_CRITICAL(&jbc_station_mux);
  const uint8_t count = status.jbc_station_count;
  const uint8_t index = count ? (uint8_t)(rotation_step % count) : 0;
  if (count) station = status.jbc_stations[index];
  portEXIT_CRITICAL(&jbc_station_mux);
  if (count) {
    String text = String("JBC Station ") + String(index + 1) + ": ";
    text += station.model[0] ? station.model : "JBC";
    if (station.flags & 0x01) text += " Work";
    else if (station.flags & 0x02) text += " Stand";
    else text += String(" ") + tr("Ready", "Bereit");
    text += (station.flags & 0x04) ? String(" ") + tr("Error", "Fehler") : String(" OK");
    return text;
  }
  String text = String("JBC ");
  const bool jbc_station_alarm = screensaver_has_alarm_code(DISPLAY_ALARM_JBC_STATION);
  if (status.jbc_connected && !jbc_station_alarm) {
    text += station_name(status.station_addr);
    text += status.work_mask ? String(" Work") : String(" Ready");
    if (status.jbc_stat_error) text += String(" ") + jbc_error_name(status.jbc_stat_error);
    else text += " OK";
  } else {
    text += tr("station offline", "Station offline");
  }
  return text;
}

static String screensaver_runtime_info() {
  String info = String("IN ") + screensaver_input_text();
  const String out_text = screensaver_output_text();
  if (out_text.length()) info += "  >  " + out_text;
  info += "   |   " + String(main_output_rpm_for_ui()) + " rpm";
  const String jbc_text = screensaver_jbc_text();
  if (jbc_text.length()) info += "   |   " + jbc_text;
  return info;
}

static void screensaver_update_values() {
  if (!ui_screensaver_screen) return;
  char clock_text[28] = "--:--";
  if (status.clock_valid) {
    if (status.clock_day && status.clock_month && status.clock_year) snprintf(clock_text, sizeof(clock_text), "%02u:%02u\n%02u.%02u.%04u", status.clock_hour, status.clock_minute, status.clock_day, status.clock_month, status.clock_year);
    else if (status.clock_day && status.clock_month) snprintf(clock_text, sizeof(clock_text), "%02u:%02u\n%02u.%02u", status.clock_hour, status.clock_minute, status.clock_day, status.clock_month);
    else snprintf(clock_text, sizeof(clock_text), "%02u:%02u", status.clock_hour, status.clock_minute);
  }
  lv_set_text_c(ui_screensaver_clock, clock_text);
  lv_set_text_c(ui_screensaver_hint, tr("Touch to wake", "Zum Aufwecken ber\303\274hren"));
  const bool running = status.output_enabled || status.afterrun_s > 0;
  const bool module_offline_alarm = screensaver_has_alarm_code(DISPLAY_ALARM_MODULE_OFFLINE);
  const bool no_main_input = status.main_input_source_type == 0 || screensaver_has_alarm_code(DISPLAY_ALARM_NO_MAIN_INPUT);
  const bool not_ready = module_offline_alarm || no_main_input;
  lv_set_text(ui_screensaver_state, running
    ? (status.afterrun_s ? String(tr("Afterrun ", "Nachlauf ")) + status.afterrun_s + "s" : String(tr("Extraction active", "Absaugung aktiv")))
    : (not_ready ? String(tr("Not ready", "Nicht bereit")) : String(tr("Ready", "Bereit"))));
  lv_obj_set_style_text_color(ui_screensaver_state,
    running ? lv_color_hex(0x55D98A) : (not_ready ? lv_color_hex(0xFFB020) : lv_color_hex(0xD8E2EE)), 0);
  uint8_t screensaver_power_pct = 0;
  if (status.output_enabled) {
    // Use the Master-owned extractor output value on the global screensaver.
    // Universal/Modbus entity values are asynchronous module controls/readbacks
    // and must not override the main output display.
    screensaver_power_pct = constrain(status.output_power / 10, 0, 100);
  }
  lv_set_text(ui_screensaver_power, String(tr("Power ", "Leistung ")) + screensaver_power_pct + "%");
  if (ui_screensaver_power_bar) lv_bar_set_value(ui_screensaver_power_bar, screensaver_power_pct, LV_ANIM_OFF);
  lv_obj_set_style_text_color(ui_screensaver_clock, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_color(ui_screensaver_power, lv_color_hex(0xFFFFFF), 0);
  lv_set_text(ui_screensaver_modules, String(status.modules_count) + " " + tr("modules", "Module"));
  lv_obj_set_style_text_color(ui_screensaver_modules, lv_color_hex(0xD8E2EE), 0);
  const uint8_t alarms = screensaver_alarm_count();
  const bool critical_alarm = screensaver_alarm_is_critical();
  String alarm_text = screensaver_alarm_title();
  if (alarms) {
    if (!alarm_text.length()) alarm_text = tr("Active alarm", "Aktiver Alarm");
    const String detail = screensaver_alarm_detail(alarms);
    alarm_text = String(LV_SYMBOL_BELL) + " " + alarm_text;
    if (detail.length()) alarm_text += ": " + detail;
  }
  lv_set_text(ui_screensaver_alarm, alarms
    ? alarm_text
    : String(LV_SYMBOL_OK) + " " + tr("No alarms", "Keine Alarme"));
  lv_obj_set_style_text_color(ui_screensaver_alarm,
    alarms ? (critical_alarm ? lv_color_hex(0xFF4D5E) : lv_color_hex(0xFFB020)) : lv_color_hex(0x55D98A), 0);
  lv_set_text(ui_screensaver_info, screensaver_runtime_info());
  lv_obj_set_style_text_color(ui_screensaver_info, lv_color_hex(0xB5C2D1), 0);

  // Shift the central block slightly each minute to avoid a permanent panel pattern.
  const int16_t shift_x = status.clock_valid ? ((int16_t)(status.clock_minute % 3) - 1) * 8 : 0;
  const int16_t shift_y = status.clock_valid ? ((int16_t)((status.clock_minute / 3) % 3) - 1) * 4 : 0;
  if (ui_screensaver_brand) lv_obj_set_pos(ui_screensaver_brand, shift_x, 22 + shift_y);
  lv_obj_set_pos(ui_screensaver_clock, shift_x, 54 + shift_y);
  lv_obj_set_pos(ui_screensaver_state, shift_x, 142 + shift_y);
  lv_obj_set_pos(ui_screensaver_alarm, 60 + shift_x, 164 + shift_y);
  lv_obj_set_pos(ui_screensaver_power, 102 + shift_x, 196 + shift_y);
  lv_obj_set_pos(ui_screensaver_modules, 438 + shift_x, 196 + shift_y);
  if (ui_screensaver_power_bar) lv_obj_set_pos(ui_screensaver_power_bar, 102 + shift_x, 224 + shift_y);
  lv_obj_set_pos(ui_screensaver_info, 60 + shift_x, 238 + shift_y);
  lv_obj_set_pos(ui_screensaver_hint, shift_x, 284 + shift_y);
}

static bool screensaver_can_start() {
  if (!lvgl_ready || !ui_screensaver_screen || fw_update_active || status.update_active) return false;
  lv_obj_t* active = lv_screen_active();
  return active == ui_dashboard_screen || active == ui_module_list_screen ||
    active == ui_module_detail_screen || active == ui_alarm_screen || active == ui_system_screen;
}

static void screensaver_enter() {
  if (screensaver_active || !screensaver_can_start()) return;
  screensaver_return_screen = lv_screen_active();
  screensaver_active = true;
  screensaver_update_values();
  screensaver_last_update_ms = millis();
  const uint8_t dim_pct = display_brightness_pct < 18 ? display_brightness_pct : 18;
  write_backlight_duty(backlight_duty_from_percent(dim_pct));
  lv_screen_load(ui_screensaver_screen);
}

static void screensaver_wake() {
  if (!screensaver_active) return;
  screensaver_active = false;
  apply_backlight();
  screensaver_return_screen = nullptr;
  last_user_activity_ms = millis();

  // Do not restore a potentially stale LVGL screen pointer. Rebuild the active
  // view from display_view_mode so waking from sleep always returns to a valid
  // fully populated page.
  switch (display_view_mode) {
    case DISPLAY_VIEW_MODULE_LIST:
      lv_update_module_list();
      lv_screen_switch(ui_module_list_screen);
      break;
    case DISPLAY_VIEW_MODULE_DETAIL:
      if (selected_module.valid) {
        lv_update_module_detail();
        lv_screen_switch(ui_module_detail_screen);
      } else {
        display_view_mode = DISPLAY_VIEW_HOME;
        display_view_arg = 0;
        show_dashboard();
      }
      break;
    case DISPLAY_VIEW_ALARMS:
      lv_update_alarm_center();
      lv_screen_switch(ui_alarm_screen);
      break;
    case DISPLAY_VIEW_SYSTEM:
      lv_update_display_settings_widgets(true);
      lv_screen_switch(ui_system_screen);
      break;
    case DISPLAY_VIEW_HOME:
    default:
      display_view_mode = DISPLAY_VIEW_HOME;
      display_view_arg = 0;
      show_dashboard();
      break;
  }
}

static void screensaver_tick() {
  // Never hide a boot/recovery problem behind the screensaver. Home must have
  // rendered at least once after a valid Master status before sleep is allowed.
  if (!have_drawn_status && !screensaver_active) return;
  if (screensaver_active) {
    const uint32_t now = millis();
    if ((uint32_t)(now - screensaver_last_update_ms) >= 1000UL) {
      screensaver_last_update_ms = now;
      screensaver_update_values();
    }
    return;
  }
  if (OfeDisplayWifiUi::isOpen() || !screensaver_timeout_min || !screensaver_can_start()) return;
  if ((uint32_t)(millis() - last_user_activity_ms) >= (uint32_t)screensaver_timeout_min * 60000UL) {
    screensaver_enter();
  }
}
static const char* display_module_type_name(uint8_t type) {
  switch (type) {
    case MODULE_JBC_BUS: return "JBC FAE Bus";
    case MODULE_JBC_USB: return "JBC USB";
    case MODULE_FAN_IO: return "Fan/IO";
    case MODULE_FAN_IO_PRO: return "Fan/IO Pro";
    case MODULE_SENSOR_RESERVED: return tr("Sensor", "Sensor");
    case MODULE_WELLER_ZERO_SMOG: return "Weller Zero Smog Bus";
    case MODULE_DISPLAY: return "Display";
    case MODULE_UNIVERSAL_RS232: return tr("Universal RS232 Bridge", "Universal RS232 Bridge");
    case MODULE_MODBUS_RTU: return tr("Modbus RTU Bridge", "Modbus RTU Bridge");
    default: return tr("Module", "Modul");
  }
}

static int16_t targeted_event_value(uint8_t addr, int8_t value = 0) {
  return (int16_t)(((uint16_t)addr << 8) | (uint8_t)value);
}

static void lv_show_home_event(lv_event_t*) {
  display_view_mode = DISPLAY_VIEW_HOME;
  display_view_arg = 0;

  // Rebuild Home widgets from the latest status/module caches BEFORE the screen
  // becomes visible. No module data is lost while Home is hidden.
  lv_update_app_values();
  lv_screen_switch(ui_dashboard_screen);
}

static void lv_show_modules_event(lv_event_t*) {
  display_view_mode = DISPLAY_VIEW_MODULE_LIST;
  display_view_arg = 0;
  // Do NOT clear the cached module list here. During another module's OTA the
  // master may pause list pages or temporarily report no modules; clearing here
  // would make the list disappear until the update is finished.
  lv_update_module_list();
  lv_screen_switch(ui_module_list_screen);
}

static void lv_back_to_modules_event(lv_event_t*) {
  display_view_mode = DISPLAY_VIEW_MODULE_LIST;
  display_view_arg = 0;
  lv_screen_switch(ui_module_list_screen);
}

static void lv_language_event(lv_event_t* e) {
  const uint8_t selected = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
  if (selected == display_language) return;
  display_language = selected > 1 ? 0 : selected;
  prefs.putUChar("lang", display_language);
  display_resync_after_flash_write();
  language_rebuild_pending = true;
}
static void lv_detail_display_brightness_event(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target_obj(e);
  const uint8_t value = (uint8_t)lv_slider_get_value(slider);
  set_brightness(value, lv_event_get_code(e) == LV_EVENT_RELEASED);
  lv_set_text(ui_detail_display_brightness_value, String(value) + "%");
}
static void lv_theme_event(lv_event_t* e) {
  const uint8_t selected = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
  if (selected == display_theme) return;
  display_theme = selected > 1 ? 0 : selected;
  prefs.putUChar("theme", display_theme);
  display_resync_after_flash_write();
  language_rebuild_pending = true;
}
static void lv_show_alarms_event(lv_event_t*) {
  display_view_mode = DISPLAY_VIEW_ALARMS;
  display_view_arg = 0;
  lv_update_alarm_center();
  lv_screen_switch(ui_alarm_screen);
}

static void lv_show_system_event(lv_event_t*) {
  display_view_mode = DISPLAY_VIEW_SYSTEM;
  display_view_arg = 0;
  lv_screen_switch(ui_system_screen);
}

static void lv_open_module_summary(uint8_t index) {
  if (index >= 17 || !module_summaries[index].valid) return;
  memset(&selected_module, 0, sizeof(selected_module));
  selected_module.addr = module_summaries[index].addr;
  selected_module.type = module_summaries[index].type;
  strncpy(selected_module.name, module_summaries[index].name, sizeof(selected_module.name) - 1);
  display_view_mode = DISPLAY_VIEW_MODULE_DETAIL;
  display_view_arg = selected_module.addr;
  detail_requested_addr = selected_module.addr;
  detail_open_ms = millis();
  detail_last_rx_ms = 0;
  detail_status_msg_ms = 0;
  lv_set_text(ui_detail_title, lv_addr_text(selected_module.addr) + "  " + String(selected_module.name[0] ? selected_module.name : display_module_type_name(selected_module.type)));
  lv_set_text_c(ui_detail_status, tr("Loading module data...", "Moduldaten laden..."));
  lv_set_text_c(ui_detail_values, "");
  lv_obj_add_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
  // Force controls to be rebuilt when the detail packet arrives.
  // Without this, reopening the same addr/type can keep the old control-card cache
  // and leave the hidden control box invisible.
  detail_controls_addr = 0;
  detail_controls_type = MODULE_UNKNOWN;
  detail_controls_caps = 0;
  lv_screen_switch(ui_module_detail_screen);
}

static void lv_module_row_event(lv_event_t* e) {
  lv_obj_t* row = lv_event_get_target_obj(e);
  lv_open_module_summary((uint8_t)(uintptr_t)lv_obj_get_user_data(row));
}

static bool module_summary_matches_group(const struct DisplayModuleSummary& m, uint8_t group) {
  if (!m.valid) return false;
  if (group == MODULE_FAN_IO) {
    if (m.type == MODULE_FAN_IO || m.type == MODULE_FAN_IO_PRO) return true;
    return (m.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_TACHO_INPUT)) && !(m.caps & CAP_WELLER_INTERFACE);
  }
  if (group == MODULE_WELLER_ZERO_SMOG) return m.type == MODULE_WELLER_ZERO_SMOG || (m.caps & CAP_WELLER_INTERFACE);
  if (group == MODULE_JBC_BUS) return m.type == MODULE_JBC_BUS || m.type == MODULE_JBC_USB || (m.caps & CAP_JBC_ACTIVITY);
  return m.type == group;
}

static uint8_t module_count_by_group(uint8_t group, bool online_only) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    if (!module_summary_matches_group(module_summaries[i], group)) continue;
    if (online_only && !(module_summaries[i].flags & 0x01)) continue;
    count++;
  }
  return count;
}

static uint8_t online_module_count_by_type(uint8_t type) {
  return module_count_by_group(type, true);
}

static bool active_output_is_weller() {
  const int8_t index = module_summary_index_by_addr(status.output_addr);
  if (index >= 0) return (module_summaries[index].caps & CAP_WELLER_INTERFACE) != 0;
  return status.output_addr >= 0x30 && status.output_addr <= 0x3F;
}

static uint8_t extractor_power_min_percent() {
  return active_output_is_weller() ? 30 : 10;
}

static bool active_output_is_fan_io() {
  const int8_t index = module_summary_index_by_addr(status.output_addr);
  if (index >= 0) {
    const DisplayModuleSummary& m = module_summaries[(uint8_t)index];
    const uint32_t caps = m.caps;
    if (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) return false;
    return (caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT)) && !(caps & CAP_WELLER_INTERFACE);
  }
  return status.output_addr != 0 && !active_output_is_weller() &&
    !(status.output_addr >= 0x50 && status.output_addr <= 0x6F);
}

static bool module_summary_is_fan_io_pro(const DisplayModuleSummary& m) {
  return m.valid && (m.type == MODULE_FAN_IO_PRO || (m.caps & (CAP_FILTER_SENSOR | CAP_PRESSURE_SENSOR | CAP_ANALOG_INPUT)));
}

static int8_t first_module_index_by_group(uint8_t group, bool online_only) {
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    if (!module_summary_matches_group(module_summaries[i], group)) continue;
    if (online_only && !(module_summaries[i].flags & 0x01)) continue;
    return (int8_t)i;
  }
  return -1;
}

static uint8_t first_module_addr_by_group(uint8_t group, bool online_only = true) {
  const int8_t idx = first_module_index_by_group(group, online_only);
  return idx >= 0 ? module_summaries[(uint8_t)idx].addr : 0;
}

static uint8_t home_fan_io_control_addr() {
  if (status.output_addr && active_output_is_fan_io()) return status.output_addr;
  uint8_t addr = first_module_addr_by_group(MODULE_FAN_IO, true);
  if (!addr) addr = first_module_addr_by_group(MODULE_FAN_IO, false);
  return addr;
}

static String home_fan_io_title() {
  const int8_t idx = first_module_index_by_group(MODULE_FAN_IO, false);
  if (idx < 0) return String("Fan / IO");
  const DisplayModuleSummary& m = module_summaries[(uint8_t)idx];
  return module_summary_is_fan_io_pro(m) ? String("Fan / IO Pro") : String("Fan / IO");
}

static bool home_weller_cache_fresh() {
  // DISPLAY_STATUS presence is authoritative. A cached detail packet must never
  // keep an unplugged/offline Weller module looking online.
  return status.weller_present && home_weller_cache.valid &&
    (uint32_t)(millis() - home_weller_cache.last_ms) <= HOME_DETAIL_CACHE_VALID_MS;
}

static bool module_addr_online(uint8_t addr) {
  const int8_t idx = module_summary_index_by_addr(addr);
  return idx >= 0 && (module_summaries[(uint8_t)idx].flags & 0x01);
}

static bool home_fan_io_cache_fresh() {
  // DISPLAY_STATUS presence is authoritative. Never let a stale module-list
  // online bit keep an unplugged Fan/IO cache alive.
  if (!status.fan_present) return false;
  if (!home_fan_io_cache.valid || !home_fan_io_cache.online) return false;
  if ((uint32_t)(millis() - home_fan_io_cache.last_ms) > HOME_DETAIL_CACHE_VALID_MS) return false;

  const int8_t idx = module_summary_index_by_addr(home_fan_io_cache.addr);
  if (idx >= 0 && !(module_summaries[(uint8_t)idx].flags & 0x01)) return false;
  return true;
}

static const char* detail_alias_or(const char* alias, const char* fallback) {
  return (alias && alias[0]) ? alias : fallback;
}

static void copy_cstr_alias(char* dst, size_t dst_size, const char* src) {
  if (!dst || !dst_size) return;
  dst[0] = 0;
  if (!src) return;
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = 0;
}

static bool detail_fields_is_weller(uint8_t type, uint32_t caps) {
  return type == MODULE_WELLER_ZERO_SMOG || (caps & CAP_WELLER_INTERFACE);
}

static bool detail_fields_is_jbc_usb(uint8_t type, uint32_t caps) {
  return type == MODULE_JBC_USB || (caps & CAP_JBC_USB);
}

static bool detail_fields_is_jbc_fae(uint8_t type, uint32_t caps) {
  return !detail_fields_is_jbc_usb(type, caps) && (type == MODULE_JBC_BUS || (caps & CAP_JBC_BUS));
}

static bool detail_fields_is_jbc(uint8_t type, uint32_t caps) {
  return detail_fields_is_jbc_usb(type, caps) || detail_fields_is_jbc_fae(type, caps);
}

static bool detail_fields_is_universal(uint8_t type, uint32_t) {
  return type == MODULE_UNIVERSAL_RS232 || type == MODULE_MODBUS_RTU;
}

static bool detail_fields_is_universal_output(uint8_t type, uint32_t caps) {
  return detail_fields_is_universal(type, caps) && (caps & CAP_ENTITY_CONTROL);
}

static DisplayUniversalModuleCache* universal_cache_find(uint8_t addr, bool create) {
  int8_t free_slot = -1;
  uint32_t oldest_ms = 0xFFFFFFFFUL;
  int8_t oldest_slot = 0;
  for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_MODULE_CACHE_MAX; ++i) {
    DisplayUniversalModuleCache& c = universal_module_cache[i];
    if (c.valid && c.addr == addr) return &c;
    if (!c.valid && free_slot < 0) free_slot = (int8_t)i;
    if (c.valid && c.last_ms < oldest_ms) { oldest_ms = c.last_ms; oldest_slot = (int8_t)i; }
  }
  if (!create || !addr) return nullptr;
  DisplayUniversalModuleCache& c = universal_module_cache[(uint8_t)(free_slot >= 0 ? free_slot : oldest_slot)];
  memset(&c, 0, sizeof(c));
  c.valid = true;
  c.addr = addr;
  c.last_ms = millis();
  return &c;
}

static const DisplayUniversalEntity* universal_cached_entity_by_id(uint8_t addr, uint8_t id) {
  const DisplayUniversalModuleCache* c = universal_cache_find(addr, false);
  if (!c) return nullptr;
  for (uint8_t i = 0; i < c->universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
    if (c->universal_entities[i].valid && c->universal_entities[i].id == id) return &c->universal_entities[i];
  }
  return nullptr;
}

static bool universal_entity_is_main_input_candidate(const DisplayUniversalEntity& e) {
  return e.valid && e.id >= 20 && (e.flags & 0x01) &&
    (e.type == DISPLAY_UNI_BINARY_SENSOR || e.type == DISPLAY_UNI_SWITCH);
}

static String universal_input_label(uint8_t addr, const DisplayUniversalEntity& e) {
  String text = module_summary_addr_name(addr);
  text += " - ";
  text += e.label[0] ? String(e.label) : (String("Entity ") + String(e.id));
  return text;
}

static uint8_t universal_cache_request_start(uint8_t addr) {
  DisplayUniversalModuleCache* c = universal_cache_find(addr, false);
  if (!c || !c->universal_entity_total) return 0;
  const uint8_t want = c->universal_entity_total > DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX ? DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX : c->universal_entity_total;
  if (c->universal_entity_count < want) return c->universal_entity_count;
  uint8_t start = c->request_cursor;
  if (start >= want) start = 0;
  return start;
}

static bool module_summary_is_universal_addr(uint8_t addr) {
  const int8_t idx = module_summary_index_by_addr(addr);
  if (idx < 0) return addr >= 0x50 && addr <= 0x6F;
  return module_summaries[(uint8_t)idx].type == MODULE_UNIVERSAL_RS232 ||
         module_summaries[(uint8_t)idx].type == MODULE_MODBUS_RTU;
}

static uint8_t first_universal_module_addr(bool online_only) {
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    const DisplayModuleSummary& m = module_summaries[i];
    if (!m.valid) continue;
    if (online_only && !(m.flags & 0x01)) continue;
    if (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU) return m.addr;
  }
  return 0;
}

static uint8_t first_universal_module_addr_needing_cache() {
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    const DisplayModuleSummary& m = module_summaries[i];
    if (!m.valid || !(m.flags & 0x01)) continue;
    if (m.type != MODULE_UNIVERSAL_RS232 && m.type != MODULE_MODBUS_RTU) continue;
    const DisplayUniversalModuleCache* c = universal_cache_find(m.addr, false);
    if (!c || !c->universal_entity_total ||
        c->universal_entity_count < (c->universal_entity_total > DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX ? DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX : c->universal_entity_total)) {
      return m.addr;
    }
    if ((uint32_t)(millis() - c->last_ms) >= DISPLAY_UNIVERSAL_CACHE_REFRESH_MS) return m.addr;
  }
  return 0;
}

static bool detail_fields_is_fan_io(uint8_t type, uint32_t caps) {
  if (type == MODULE_UNIVERSAL_RS232 || type == MODULE_MODBUS_RTU) return false;
  if (type == MODULE_FAN_IO || type == MODULE_FAN_IO_PRO) return true;
  const uint32_t fan_caps = CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_TACHO_INPUT | CAP_CLOSED_LOOP_RPM | CAP_DIGITAL_OUTPUT;
  return (caps & fan_caps) && !(caps & (CAP_WELLER_INTERFACE | CAP_JBC_ACTIVITY | CAP_DISPLAY));
}

static bool selected_detail_is_weller() {
  return detail_fields_is_weller(selected_module.type, selected_module.caps);
}

static bool selected_detail_is_jbc() {
  // JBC USB is intentionally read-only on the OFE display.  Only the FAE bus
  // keeps the existing suction control card.
  return detail_fields_is_jbc_fae(selected_module.type, selected_module.caps);
}

static bool selected_detail_is_fan_io() {
  return detail_fields_is_fan_io(selected_module.type, selected_module.caps);
}

static bool selected_detail_is_universal() {
  return detail_fields_is_universal(selected_module.type, selected_module.caps);
}

static bool selected_detail_is_universal_output() {
  return detail_fields_is_universal_output(selected_module.type, selected_module.caps);
}

static bool selected_detail_has_output_controls() {
  return selected_detail_is_fan_io() || selected_detail_is_universal_output();
}


static DisplayUniversalEntity* selected_universal_entity_by_id(uint8_t id) {
  for (uint8_t i = 0; i < selected_module.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
    if (selected_module.universal_entities[i].valid && selected_module.universal_entities[i].id == id) return &selected_module.universal_entities[i];
  }
  return nullptr;
}

static bool universal_entity_writable(const DisplayUniversalEntity& e) {
  return (e.flags & 0x02) != 0;
}

static bool universal_entity_readable(const DisplayUniversalEntity& e) {
  return (e.flags & 0x01) != 0;
}

static String universal_entity_value_text(const DisplayUniversalEntity& e) {
  if (e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BINARY_SENSOR) return on_off(e.value != 0);
  if (e.text[0]) {
    String out = String(e.text);
    if (e.unit[0] && out.indexOf(e.unit) < 0) out += String(" ") + e.unit;
    return out;
  }
  String out = String(e.value);
  if (e.unit[0]) out += String(" ") + e.unit;
  return out;
}

static int8_t universal_pending_find(uint8_t addr, uint8_t id) {
  for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
    if (universal_control_pending[i].active &&
        universal_control_pending[i].addr == addr &&
        universal_control_pending[i].id == id) return (int8_t)i;
  }
  return -1;
}

static void universal_pending_set(uint8_t addr, uint8_t id, uint8_t type, int16_t value) {
  int8_t slot = universal_pending_find(addr, id);
  if (slot < 0) {
    slot = 0;
    for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      if (!universal_control_pending[i].active) { slot = (int8_t)i; break; }
    }
  }
  DisplayUniversalControlPending& p = universal_control_pending[(uint8_t)slot];
  p.active = true;
  p.addr = addr;
  p.id = id;
  p.type = type;
  p.value = value;
  p.ms = millis();
}

static bool universal_pending_value(uint8_t addr, uint8_t id, uint8_t type, int16_t& value) {
  int8_t slot = universal_pending_find(addr, id);
  if (slot < 0) return false;
  DisplayUniversalControlPending& p = universal_control_pending[(uint8_t)slot];
  if (p.type != type || (uint32_t)(millis() - p.ms) >= 4000UL) {
    p.active = false;
    return false;
  }
  value = p.value;
  return true;
}

static void universal_pending_observe(uint8_t addr, const DisplayUniversalEntity& e) {
  int8_t slot = universal_pending_find(addr, e.id);
  if (slot < 0) return;
  DisplayUniversalControlPending& p = universal_control_pending[(uint8_t)slot];
  const int16_t actual = (e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BINARY_SENSOR) ? (e.value ? 1 : 0) : e.value;
  if (p.type != e.type || actual == p.value || (uint32_t)(millis() - p.ms) >= 4000UL) p.active = false;
}

static int8_t universal_control_slot_for_id(lv_obj_t* const* controls, uint8_t id) {
  for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
    if (controls[i] && (uint8_t)(uintptr_t)lv_obj_get_user_data(controls[i]) == id) return (int8_t)i;
  }
  return -1;
}

static bool universal_entity_label_has(const DisplayUniversalEntity& e, const char* needle) {
  String label = String(e.label);
  label.toLowerCase();
  return label.indexOf(needle) >= 0;
}

static bool universal_entity_is_output_power_candidate(const DisplayUniversalEntity& e) {
  if (!e.valid || e.type != DISPLAY_UNI_NUMBER || !(e.flags & 0x01)) return false;
  String unit = String(e.unit);
  unit.toLowerCase();
  if (unit.indexOf("rpm") >= 0) return false;
  return universal_entity_label_has(e, "power") ||
         universal_entity_label_has(e, "leistung") ||
         universal_entity_label_has(e, "speed") ||
         universal_entity_label_has(e, "drehzahl");
}

static const DisplayUniversalEntity* universal_cached_output_power_entity(uint8_t addr) {
  const DisplayUniversalModuleCache* c = universal_cache_find(addr, false);
  if (!c) return nullptr;
  for (uint8_t i = 0; i < c->universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
    const DisplayUniversalEntity& e = c->universal_entities[i];
    if (universal_entity_is_output_power_candidate(e)) return &e;
  }
  return nullptr;
}

static uint8_t universal_output_power_percent_for_ui(uint8_t addr, uint8_t fallback_pct) {
  const DisplayUniversalEntity* e = universal_cached_output_power_entity(addr);
  if (!e) return fallback_pct;
  int16_t shown = e->value;
  universal_pending_value(addr, e->id, e->type, shown);
  if (shown < 0) shown = 0;
  if (shown > 100) shown = shown / 10;
  return constrain(shown, 0, 100);
}

static uint8_t selected_universal_control_count() {
  uint8_t n = 0;
  if (!selected_detail_is_universal()) return 0;
  for (uint8_t i = 0; i < selected_module.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
    const DisplayUniversalEntity& e = selected_module.universal_entities[i];
    if (!e.valid || !universal_entity_writable(e)) continue;
    if (e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BUTTON || e.type == DISPLAY_UNI_NUMBER || e.type == DISPLAY_UNI_SELECT) {
      ++n;
      if (n >= DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX) break;
    }
  }
  return n;
}

static uint32_t selected_universal_controls_signature() {
  if (selected_detail_is_fan_io()) {
    const char* aliases[3] = {selected_module.io_main_alias, selected_module.io_out1_alias, selected_module.io_out2_alias};
    uint32_t sig = 0xF10A0000UL;
    for (uint8_t i = 0; i < 3; ++i) {
      const char* s = aliases[i];
      while (s && *s) sig = (sig * 33UL) ^ (uint8_t)(*s++);
      sig ^= (uint32_t)i << 24;
    }
    return sig;
  }
  if (!selected_detail_is_universal()) return 0;
  uint32_t sig = selected_module.universal_descriptor_crc ^ ((uint32_t)selected_module.universal_entity_total << 24) ^ selected_module.universal_entity_count;
  for (uint8_t i = 0; i < selected_module.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
    const DisplayUniversalEntity& e = selected_module.universal_entities[i];
    sig ^= ((uint32_t)e.id << ((i & 3) * 8));
    sig ^= ((uint32_t)e.type << (((i + 1) & 3) * 8));
    sig ^= ((uint32_t)e.flags << (((i + 2) & 3) * 8));
  }
  return sig;
}

static uint8_t selected_universal_request_start() {
  if (!selected_detail_is_universal()) return 0;
  if (!selected_module.universal_entity_total) return 0;
  if (selected_universal_request_addr != selected_module.addr) {
    selected_universal_request_addr = selected_module.addr;
    selected_universal_request_cursor = 0;
    selected_universal_readback_id = 0;
  }
  const uint8_t display_total = selected_module.universal_entity_total > DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX ? DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX : selected_module.universal_entity_total;
  if (selected_module.universal_entity_count < display_total) return selected_module.universal_entity_count;

  // Keep an RW control on its own entity page until the actual module value
  // confirms the write. This prevents a 32-entity page rotation from delaying
  // the visible readback for several seconds.
  if (selected_universal_readback_id) {
    if (universal_pending_find(selected_module.addr, selected_universal_readback_id) < 0) {
      selected_universal_readback_id = 0;
    } else {
      for (uint8_t i = 0; i < selected_module.universal_entity_count && i < display_total; ++i) {
        const DisplayUniversalEntity& e = selected_module.universal_entities[i];
        if (e.valid && e.id == selected_universal_readback_id) return i;
      }
    }
  }

  uint8_t start = selected_universal_request_cursor;
  if (start >= display_total) start = 0;
  return start;
}

static void selected_universal_expect_readback(uint8_t id) {
  DisplayUniversalEntity* e = selected_universal_entity_by_id(id);
  if (!e || !universal_entity_readable(*e)) return;
  selected_universal_readback_id = id;
  selected_universal_request_addr = selected_module.addr;
  selected_universal_request_ms = 0;
}

static String universal_select_option_text(const DisplayUniversalEntity& e, int16_t index) {
  if (index < 0 || !e.options[0]) return String("-");
  String opts = String(e.options);
  uint16_t pos = 0;
  int16_t current = 0;
  while (pos <= opts.length()) {
    int pipe = opts.indexOf('|', pos);
    int semi = opts.indexOf(';', pos);
    int next = pipe < 0 ? semi : (semi < 0 ? pipe : (pipe < semi ? pipe : semi));
    String item = next >= 0 ? opts.substring(pos, next) : opts.substring(pos);
    item.trim();
    if (current == index) return item.length() ? item : String("-");
    if (next < 0) break;
    pos = (uint16_t)(next + 1);
    ++current;
  }
  return String("-");
}

static void lv_detail_universal_switch_event(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* obj = lv_event_get_target_obj(e);
  const uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(obj);
  if (!id) return;
  const bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
  DisplayUniversalEntity* ent = selected_universal_entity_by_id(id);
  const bool readable = ent && universal_entity_readable(*ent);
  universal_pending_set(selected_module.addr, id, DISPLAY_UNI_SWITCH, checked ? 1 : 0);
  if (!readable) {
    for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      if (ui_detail_universal_switches[i] == obj && ui_detail_universal_values[i]) {
        lv_set_text(ui_detail_universal_values[i], on_off(checked));
        break;
      }
    }
  }
  queue_display_event(DISPLAY_EVENT_UNIVERSAL_ENTITY_VALUE_SET,
    (int16_t)(((uint16_t)id << 8) | (checked ? 1U : 0U)));
  if (readable) selected_universal_expect_readback(id);
}

static void lv_detail_universal_select_event(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  if (ui_detail_universal_select_refreshing) return;
  lv_obj_t* obj = lv_event_get_target_obj(e);
  const uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(obj);
  if (!id) return;
  const uint8_t selected = (uint8_t)lv_dropdown_get_selected(obj);
  if (selected == 0) return; // neutral command placeholder
  char label[32] = {0};
  lv_dropdown_get_selected_str(obj, label, sizeof(label));
  DisplayUniversalEntity* ent = selected_universal_entity_by_id(id);
  const bool readable = ent && universal_entity_readable(*ent);
  if (!readable) {
    for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      if (ui_detail_universal_selects[i] == obj && ui_detail_universal_values[i]) {
        lv_set_text(ui_detail_universal_values[i], label[0] ? label : "-");
        break;
      }
    }
  }
  const uint8_t profile_index = (uint8_t)(selected - 1U);
  universal_pending_set(selected_module.addr, id, DISPLAY_UNI_SELECT, profile_index);
  queue_display_event(DISPLAY_EVENT_UNIVERSAL_ENTITY_SELECT_SET,
    (int16_t)(((uint16_t)id << 8) | profile_index));
  if (readable) selected_universal_expect_readback(id);

  // Controls send commands; the separate value column is the actual RW state.
  ui_detail_universal_select_refreshing = true;
  lv_dropdown_set_selected(obj, 0);
  ui_detail_universal_select_refreshing = false;
}

static void lv_detail_universal_button_event(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_target_obj(e);
  const uintptr_t raw = (uintptr_t)lv_obj_get_user_data(obj);
  const uint8_t id = (uint8_t)(raw & 0xFFU);
  const uint8_t action = (uint8_t)((raw >> 8) & 0xFFU);
  const uint8_t set_value = (uint8_t)((raw >> 16) & 0xFFU);
  if (!id) return;
  // Write-only switches have no reliable readback state. Render them as two
  // momentary buttons and send the requested value directly instead of using a
  // LVGL switch that would snap back to OFF.
  if (action == 1) {
    universal_pending_set(selected_module.addr, id, DISPLAY_UNI_SWITCH, set_value ? 1 : 0);
    queue_display_event(DISPLAY_EVENT_UNIVERSAL_ENTITY_VALUE_SET,
      (int16_t)(((uint16_t)id << 8) | (set_value ? 1U : 0U)));
    selected_universal_expect_readback(id);
    return;
  }
  queue_display_event(DISPLAY_EVENT_UNIVERSAL_ENTITY_BUTTON, id);
  selected_universal_expect_readback(id);
}

static void lv_detail_universal_slider_event(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_target_obj(e);
  const uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(obj);
  if (!id) return;
  uint8_t value = (uint8_t)lv_slider_get_value(obj);
  DisplayUniversalEntity* ent = selected_universal_entity_by_id(id);
  const bool readable = ent && universal_entity_readable(*ent);
  if (!readable) {
    for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      if (ui_detail_universal_sliders[i] == obj && ui_detail_universal_values[i]) {
        String txt = String(value);
        if (ent && ent->unit[0]) txt += String(" ") + ent->unit;
        lv_set_text(ui_detail_universal_values[i], txt);
        break;
      }
    }
  }
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    universal_pending_set(selected_module.addr, id, DISPLAY_UNI_NUMBER, value);
    queue_display_event(DISPLAY_EVENT_UNIVERSAL_ENTITY_VALUE_SET, (int16_t)(((uint16_t)id << 8) | value));
    if (readable) selected_universal_expect_readback(id);
  }
}

static String detail_type_name(uint8_t type, uint32_t caps) {
  if (type == MODULE_UNIVERSAL_RS232) return String(display_module_type_name(type));
  if (type == MODULE_MODBUS_RTU) return String(display_module_type_name(type));
  if (detail_fields_is_fan_io(type, caps)) return (type == MODULE_FAN_IO_PRO || (caps & (CAP_FILTER_SENSOR | CAP_PRESSURE_SENSOR | CAP_ANALOG_INPUT))) ? String("Fan/IO Pro") : String("Fan/IO");
  if (detail_fields_is_weller(type, caps)) return String("Weller Zero Smog Bus");
  if (detail_fields_is_jbc_usb(type, caps)) return String("JBC USB");
  if (detail_fields_is_jbc_fae(type, caps)) return String("JBC FAE Bus");
  return String(display_module_type_name(type));
}

static String lv_addr_text(uint8_t addr) {
  if (!addr) return String("-");
  char buf[5];
  snprintf(buf, sizeof(buf), "0x%02X", addr);
  return String(buf);
}

static String module_fw_text(uint8_t major, uint8_t minor, uint8_t patch, const char* suffix) {
  String fw = String(major) + "." + String(minor) + "." + String(patch);
  if (suffix && suffix[0]) fw += suffix;
  return fw;
}

static String module_summary_addr_name(uint8_t addr) {
  const int8_t idx = module_summary_index_by_addr(addr);
  String text = lv_addr_text(addr);
  text += " ";
  if (idx >= 0) {
    const DisplayModuleSummary& m = module_summaries[(uint8_t)idx];
    text += m.name[0] ? String(m.name) : String(display_module_type_name(m.type));
  } else {
    text += tr("Module", "Modul");
  }
  return text;
}

static String lv_unavailable_route_label(const char* en, const char* de, uint8_t addr) {
  String text = tr(en, de);
  if (addr) {
    text += " (";
    text += module_summary_addr_name(addr);
    text += ")";
  }
  return text;
}


static const char* fan_io_alias_for(uint8_t addr, uint8_t slot, const char* fallback) {
  const char* alias = nullptr;
  if (selected_module.valid && selected_module.addr == addr) {
    switch (slot) {
      case 0: alias = selected_module.io_main_alias; break;
      case 1: alias = selected_module.io_in1_alias; break;
      case 2: alias = selected_module.io_in2_alias; break;
      case 3: alias = selected_module.io_out1_alias; break;
      case 4: alias = selected_module.io_out2_alias; break;
      default: break;
    }
  }
  if ((!alias || !alias[0]) && home_fan_io_cache.valid && home_fan_io_cache.addr == addr) {
    switch (slot) {
      case 0: alias = home_fan_io_cache.io_main_alias; break;
      case 1: alias = home_fan_io_cache.io_in1_alias; break;
      case 2: alias = home_fan_io_cache.io_in2_alias; break;
      case 3: alias = home_fan_io_cache.io_out1_alias; break;
      case 4: alias = home_fan_io_cache.io_out2_alias; break;
      default: break;
    }
  }
  return detail_alias_or(alias, fallback);
}

static String lv_home_output_option_label(uint8_t addr, uint8_t auto_effective_addr = 0) {
  if (!addr) {
    String text = String(tr("Auto", "Auto"));
    if (auto_effective_addr) text += " (" + module_summary_addr_name(auto_effective_addr) + ")";
    else text += " (" + String(tr("No output", "Kein Ausgang")) + ")";
    return text;
  }
  const int8_t idx = module_summary_index_by_addr(addr);
  if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) {
    return lv_unavailable_route_label("No output", "Kein Ausgang", addr);
  }
  return module_summary_addr_name(addr);
}

static void lv_home_output_set_dropdown_text(lv_obj_t* dropdown, const String& text) {
  if (!dropdown) return;
  snprintf(home_output_selected_text, sizeof(home_output_selected_text), "%s", text.c_str());
#if LVGL_VERSION_MAJOR >= 8
  lv_dropdown_set_text(dropdown, home_output_selected_text);
#endif
}

static uint8_t lv_home_output_index_for_addr(uint8_t addr) {
  if (!addr) return 0;
  for (uint8_t i = 1; i < home_output_count; ++i) {
    if (home_output_addrs[i] == addr) return i;
  }
  return 0;
}

static void lv_home_output_select_event(lv_event_t* e) {
  if (home_output_dropdown_refreshing) return;
  lv_obj_t* dropdown = lv_event_get_target_obj(e);
  const uint16_t selected = lv_dropdown_get_selected(dropdown);
  if (selected >= home_output_count) return;

  home_output_user_route_addr = home_output_addrs[selected];
  home_output_manual_latch = home_output_user_route_addr != 0;
  home_output_pending_route_addr = home_output_user_route_addr;
  home_output_pending_route_ms = millis();

  // Keep the visible selection immediately. The next DISPLAY_STATUS packet can
  // still contain the old effective output for one poll cycle; without this
  // short pending latch the dropdown jumped back to Auto (0xXX).
  home_output_dropdown_refreshing = true;
  lv_dropdown_set_selected(dropdown, selected);
  lv_home_output_set_dropdown_text(dropdown, lv_home_output_option_label(home_output_user_route_addr, status.output_addr));
  home_output_dropdown_refreshing = false;

  queue_display_event(DISPLAY_EVENT_OUTPUT_SELECT, home_output_user_route_addr);
}

static uint16_t main_input_encode(uint8_t source_type, uint8_t source_addr, uint8_t source_bit) {
  return ((uint16_t)(source_type & 0x03) << 14) |
         ((uint16_t)(source_bit & 0x3F) << 8) |
         source_addr;
}

static uint8_t main_input_encoded_type(uint16_t encoded) { return (uint8_t)((encoded >> 14) & 0x03); }
static uint8_t main_input_encoded_bit(uint16_t encoded) { return (uint8_t)((encoded >> 8) & 0x3F); }
static uint8_t main_input_encoded_addr(uint16_t encoded) { return (uint8_t)(encoded & 0xFF); }

static String lv_home_input_option_label(uint16_t encoded) {
  const uint8_t source_type = main_input_encoded_type(encoded);
  const uint8_t source_bit = main_input_encoded_bit(encoded);
  const uint8_t source_addr = main_input_encoded_addr(encoded);
  if (source_type == 0) return tr("No input", "Kein Eingang");
  if (source_type == 1) {
    if (!source_addr) return tr("All JBC modules", "Alle JBC-Module");
    const int8_t idx = module_summary_index_by_addr(source_addr);
    if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) return lv_unavailable_route_label("No input", "Kein Eingang", source_addr);
    return module_summary_addr_name(source_addr);
  }
  if (source_type == 2) {
    String suffix = String(" - ") + fan_io_alias_for(source_addr, source_bit == 0 ? 1 : 2, source_bit == 0 ? "IN1" : "IN2");
    const int8_t idx = module_summary_index_by_addr(source_addr);
    if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) return lv_unavailable_route_label("No input", "Kein Eingang", source_addr) + suffix;
    return module_summary_addr_name(source_addr) + suffix;
  }
  if (source_type == 3) {
    const DisplayUniversalEntity* e = universal_cached_entity_by_id(source_addr, source_bit);
    String suffix = e ? String(" - ") + universal_input_label(source_addr, *e) : String(" - Entity ") + String(source_bit);
    const int8_t idx = module_summary_index_by_addr(source_addr);
    if (idx < 0 || !(module_summaries[(uint8_t)idx].flags & 0x01)) return lv_unavailable_route_label("No input", "Kein Eingang", source_addr) + suffix;
    return e ? universal_input_label(source_addr, *e) : (module_summary_addr_name(source_addr) + suffix);
  }
  return tr("Input", "Eingang");
}

static uint16_t current_main_input_encoded() {
  return main_input_encode(status.main_input_source_type, status.main_input_source_addr, status.main_input_source_bit);
}

static void lv_home_input_set_dropdown_text(lv_obj_t* dropdown, const String& text) {
  if (!dropdown) return;
  snprintf(home_input_selected_text, sizeof(home_input_selected_text), "%s", text.c_str());
#if LVGL_VERSION_MAJOR >= 8
  lv_dropdown_set_text(dropdown, home_input_selected_text);
#endif
}

static uint8_t lv_home_input_index_for_value(uint16_t value) {
  for (uint8_t i = 0; i < home_input_count; ++i) {
    if (home_input_values[i] == value) return i;
  }
  return 0;
}

static void lv_home_input_select_event(lv_event_t* e) {
  if (home_input_dropdown_refreshing) return;
  lv_obj_t* dropdown = lv_event_get_target_obj(e);
  const uint16_t selected = lv_dropdown_get_selected(dropdown);
  if (selected >= home_input_count) return;
  const uint16_t encoded = home_input_values[selected];
  home_input_pending_value = encoded;
  home_input_pending_route_ms = millis();
  home_input_dropdown_refreshing = true;
  lv_dropdown_set_selected(dropdown, selected);
  lv_home_input_set_dropdown_text(dropdown, lv_home_input_option_label(encoded));
  home_input_dropdown_refreshing = false;
  queue_display_event(DISPLAY_EVENT_MAIN_INPUT_SELECT, (int16_t)encoded);
}

static void lv_refresh_input_dropdown() {
  if (!ui_home_input_dropdown || lv_dropdown_is_open(ui_home_input_dropdown)) return;
  uint16_t values[64] = {0};
  uint8_t count = 0;
  values[count++] = main_input_encode(0, 0, 0);
  if (status.jbc_inputs && count < (sizeof(values) / sizeof(values[0]))) values[count++] = main_input_encode(1, 0, 0);
  for (uint8_t i = 0; i < module_total && i < 17 && count < (sizeof(values) / sizeof(values[0])); ++i) {
    const DisplayModuleSummary& m = module_summaries[i];
    if (!m.valid || !(m.flags & 0x01)) continue;
    if ((m.caps & CAP_JBC_ACTIVITY) && count < (sizeof(values) / sizeof(values[0]))) values[count++] = main_input_encode(1, m.addr, 0);
    if ((m.caps & CAP_INPUT_KEYS) && count + 1 < (sizeof(values) / sizeof(values[0]))) {
      values[count++] = main_input_encode(2, m.addr, 0);
      values[count++] = main_input_encode(2, m.addr, 1);
    }
    const DisplayUniversalModuleCache* uc = universal_cache_find(m.addr, false);
    if (uc && (m.type == MODULE_UNIVERSAL_RS232 || m.type == MODULE_MODBUS_RTU)) {
      for (uint8_t u = 0; u < uc->universal_entity_count && u < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX && count < (sizeof(values) / sizeof(values[0])); ++u) {
        const DisplayUniversalEntity& ent = uc->universal_entities[u];
        if (universal_entity_is_main_input_candidate(ent)) values[count++] = main_input_encode(3, m.addr, ent.id);
      }
    }
  }

  const uint16_t current_value_for_options = current_main_input_encoded();
  bool current_value_present = false;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] == current_value_for_options) { current_value_present = true; break; }
  }
  if (!current_value_present && current_value_for_options && count < (sizeof(values) / sizeof(values[0]))) {
    values[count++] = current_value_for_options;
  }

  String options = lv_home_input_option_label(values[0]);
  for (uint8_t i = 1; i < count; ++i) {
    options += '\n';
    options += lv_home_input_option_label(values[i]);
  }
  home_input_count = count;
  memcpy(home_input_values, values, sizeof(home_input_values));

  home_input_dropdown_refreshing = true;
  if (options != home_input_options_cache) {
    home_input_options_cache = options;
    lv_dropdown_set_options(ui_home_input_dropdown, options.c_str());
  }

  const uint32_t now = millis();
  uint16_t selected_value = current_main_input_encoded();
  if (home_input_pending_route_ms && (uint32_t)(now - home_input_pending_route_ms) <= HOME_OUTPUT_ROUTE_GRACE_MS) {
    selected_value = home_input_pending_value;
    if (current_main_input_encoded() == selected_value) home_input_pending_route_ms = 0;
  }
  const uint8_t selected = lv_home_input_index_for_value(selected_value);
  lv_dropdown_set_selected_if_changed(ui_home_input_dropdown, selected);
  lv_home_input_set_dropdown_text(ui_home_input_dropdown, lv_home_input_option_label(home_input_values[selected]));
  home_input_dropdown_refreshing = false;
}
static void lv_refresh_output_dropdown() {
  if (!ui_home_output_dropdown || lv_dropdown_is_open(ui_home_output_dropdown)) return;
  uint8_t addrs[18] = {0};
  uint8_t count = 1;
  uint8_t first_output_addr = 0;
  addrs[0] = 0;  // 0 means master-side automatic main-output selection.
  for (uint8_t i = 0; i < module_total && i < 17 && count < (sizeof(addrs) / sizeof(addrs[0])); ++i) {
    const DisplayModuleSummary& m = module_summaries[i];
    const bool entity_output = detail_fields_is_universal_output(m.type, m.caps);
    if (!m.valid || !(m.flags & 0x01) ||
        (!(m.caps & (CAP_RELAY_OUTPUT | CAP_PWM_OUTPUT | CAP_WELLER_INTERFACE)) && !entity_output)) continue;
    if (!first_output_addr) first_output_addr = m.addr;
    addrs[count++] = m.addr;
  }

  if (status.preferred_output_addr) {
    bool preferred_present = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (addrs[i] == status.preferred_output_addr) { preferred_present = true; break; }
    }
    if (!preferred_present && count < (sizeof(addrs) / sizeof(addrs[0]))) addrs[count++] = status.preferred_output_addr;
  }

  const uint8_t auto_addr = status.auto_output_addr ? status.auto_output_addr : (status.preferred_output_addr ? 0 : (status.output_addr ? status.output_addr : first_output_addr));
  String options = lv_home_output_option_label(0, auto_addr);
  for (uint8_t i = 1; i < count; ++i) {
    options += '\n';
    options += lv_home_output_option_label(addrs[i]);
  }

  home_output_count = count;
  memcpy(home_output_addrs, addrs, sizeof(home_output_addrs));
  // Updating the Auto label from "Auto" to e.g. "Auto (0x31)" can reset
  // the LVGL dropdown selection internally. Guard the event handler so this
  // programmatic refresh does not look like the user selected Auto again.
  home_output_dropdown_refreshing = true;
  if (options != home_output_options_cache) {
    home_output_options_cache = options;
    lv_dropdown_set_options(ui_home_output_dropdown, options.c_str());
  }

  const uint32_t now = millis();
  const bool pending_route_active =
    home_output_pending_route_ms &&
    (uint32_t)(now - home_output_pending_route_ms) <= HOME_OUTPUT_ROUTE_GRACE_MS;

  uint8_t selected = 0;
  uint8_t selected_addr = 0;

  if (pending_route_active) {
    // A user action on the display wins briefly, so one stale status packet
    // from the master cannot immediately flip the dropdown back.
    selected_addr = home_output_pending_route_addr;
    selected = lv_home_output_index_for_addr(selected_addr);
    if (status.preferred_output_addr == selected_addr) {
      home_output_pending_route_ms = 0;
    }
  } else if (status.preferred_output_addr) {
    // Master firmware v0.3.71+ reports the configured output route separately.
    // This is the reliable difference between Auto and a manually selected
    // Weller/Fan-IO module, even when both resolve to the same effective addr.
    selected_addr = status.preferred_output_addr;
    selected = lv_home_output_index_for_addr(selected_addr);
    if (selected) {
      home_output_user_route_addr = selected_addr;
      home_output_manual_latch = true;
    } else {
      home_output_user_route_addr = 0;
      home_output_manual_latch = false;
    }
  } else {
    // preferred_output_addr == 0 means the Master is in Auto mode. Show the
    // Auto row and use output_addr only as the address hint in "Auto (0xXX)".
    selected_addr = 0;
    selected = 0;
    home_output_user_route_addr = 0;
    home_output_manual_latch = false;
  }

  lv_dropdown_set_selected_if_changed(ui_home_output_dropdown, selected);
  if (selected) lv_home_output_set_dropdown_text(ui_home_output_dropdown, lv_home_output_option_label(home_output_addrs[selected]));
  else lv_home_output_set_dropdown_text(ui_home_output_dropdown, lv_home_output_option_label(0, auto_addr));
  home_output_dropdown_refreshing = false;
}
static void lv_home_module_card_event(lv_event_t* e) {
  const uint8_t group = (uint8_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
  uint8_t match_count = 0;
  uint8_t match_index = 0;
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    if (module_summary_matches_group(module_summaries[i], group)) {
      match_index = i;
      match_count++;
    }
  }
  if (match_count == 1) lv_open_module_summary(match_index);
  else lv_show_modules_event(nullptr);
}

static void lv_enable_home_module_card(lv_obj_t* card, uint8_t type) {
  if (!card) return;
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(card, (void*)(uintptr_t)type);
  lv_obj_add_event_cb(card, lv_home_module_card_event, LV_EVENT_CLICKED, NULL);
}

static void lv_detail_fan_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_MODULE_OUTPUT_TOGGLE, targeted_event_value(selected_module.addr));
}

static void lv_detail_io1_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_IO_OUT_TOGGLE, targeted_event_value(selected_module.addr, 0));
}

static void lv_detail_io2_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_IO_OUT_TOGGLE, targeted_event_value(selected_module.addr, 1));
}

static void lv_detail_output_power_slider_event(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target_obj(e);
  const uint8_t value = (uint8_t)lv_slider_get_value(slider);
  if (ui_detail_output_power_value) lv_set_text(ui_detail_output_power_value, String(value) + "%");
  if (lv_event_get_code(e) == LV_EVENT_RELEASED && selected_module.addr) {
    output_power_pending = true;
    output_power_pending_addr = selected_module.addr;
    output_power_pending_value = value;
    output_power_pending_ms = millis();
    queue_display_event(DISPLAY_EVENT_MODULE_OUTPUT_POWER_SET,
      targeted_event_value(selected_module.addr, (int8_t)(value < 10 ? 10 : value)));
  }
}

static void lv_detail_jbc_mode_event(lv_event_t* e) {
  const uint16_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(e));
  queue_display_event(DISPLAY_EVENT_JBC_MODE_SET, (int16_t)selected);
}

static void hold_current_weller_speed(uint8_t addr) {
  if (!addr) return;
  uint8_t value = 30;
  if (ui_detail_weller_speed_slider && lv_obj_is_valid(ui_detail_weller_speed_slider)) {
    value = (uint8_t)lv_slider_get_value(ui_detail_weller_speed_slider);
  } else if (home_weller_cache.valid && home_weller_cache.addr == addr && home_weller_cache.speed) {
    value = home_weller_cache.speed;
  } else if (status.weller_speed) {
    value = status.weller_speed;
  }
  if (value < 30) value = 30;
  if (value > 100) value = 100;
  weller_speed_pending = true;
  weller_speed_pending_addr = addr;
  weller_speed_pending_value = value;
  weller_speed_pending_ms = millis();
}
static void lv_detail_weller_speed_slider_event(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target_obj(e);
  weller_speed_pending = true;
  weller_speed_pending_addr = selected_module.addr;
  weller_speed_pending_value = (uint8_t)lv_slider_get_value(slider);
  weller_speed_pending_ms = millis();
  queue_display_event(DISPLAY_EVENT_WELLER_SPEED_SET,
    targeted_event_value(selected_module.addr, (int8_t)weller_speed_pending_value));
}
static void lv_detail_weller_fan_event(lv_event_t*) {
  hold_current_weller_speed(selected_module.addr);
  queue_display_event(DISPLAY_EVENT_WELLER_FAN_TOGGLE, targeted_event_value(selected_module.addr));
}

static void lv_detail_weller_light_event(lv_event_t*) {
  hold_current_weller_speed(selected_module.addr);
  queue_display_event(DISPLAY_EVENT_WELLER_LIGHT_TOGGLE, targeted_event_value(selected_module.addr));
}

static void lv_detail_weller_speed_down_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_WELLER_SPEED_DELTA, targeted_event_value(selected_module.addr, -10));
}

static void lv_detail_weller_speed_up_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_WELLER_SPEED_DELTA, targeted_event_value(selected_module.addr, 10));
}

static void lv_detail_weller_filter_event(lv_event_t* e) {
  lv_obj_t* dropdown = lv_event_get_target_obj(e);
  const uint16_t selected = lv_dropdown_get_selected(dropdown);
  queue_display_event(DISPLAY_EVENT_WELLER_FILTER_TIME_SET,
    targeted_event_value(selected_module.addr, (int8_t)selected));
}

static void lv_detail_weller_reset_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_WELLER_RESET_FILTER, targeted_event_value(selected_module.addr));
}

static void lv_home_continuous_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_CONTINUOUS_TOGGLE);
}

static void lv_home_fan_event(lv_event_t*) {
  hold_current_weller_speed(status.output_addr);
  queue_display_event(DISPLAY_EVENT_WELLER_FAN_TOGGLE);
}

static void lv_home_light_event(lv_event_t*) {
  hold_current_weller_speed(status.output_addr);
  queue_display_event(DISPLAY_EVENT_WELLER_LIGHT_TOGGLE);
}

static void lv_home_io1_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_IO_OUT_TOGGLE, status.output_addr ? targeted_event_value(status.output_addr, 0) : 0);
}

static void lv_home_io2_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_IO_OUT_TOGGLE, status.output_addr ? targeted_event_value(status.output_addr, 1) : 1);
}

static void lv_home_afterrun_power_event(lv_event_t*) {
  queue_display_event(DISPLAY_EVENT_AFTER_POWER_TOGGLE);
}

static void lv_home_fanio_relay_event(lv_event_t*) {
  const uint8_t addr = home_fan_io_control_addr();
  queue_display_event(DISPLAY_EVENT_MODULE_OUTPUT_TOGGLE, addr ? targeted_event_value(addr) : 0);
}

static void lv_home_fanio_power_slider_event(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target_obj(e);
  const uint8_t value = (uint8_t)lv_slider_get_value(slider);
  const uint8_t send_value = value < 10 ? 10 : value;
  if (ui_home_fanio_power_value) lv_set_text(ui_home_fanio_power_value, String(send_value) + "%");
  const lv_event_code_t code = lv_event_get_code(e);
  const uint8_t addr = home_fan_io_control_addr();
  if (code == LV_EVENT_PRESSED && addr && addr != status.output_addr) {
    queue_display_event(DISPLAY_EVENT_OUTPUT_SELECT, addr);
  } else if (code == LV_EVENT_RELEASED) {
    queue_display_event(DISPLAY_EVENT_JBC_POWER_SET, (int16_t)send_value);
  }
}

static void lv_add_main_nav(lv_obj_t* screen, uint8_t active) {
  const int16_t y = DISPLAY_RGB_HEIGHT - 48;
  constexpr int16_t margin = 8;
  constexpr int16_t gap = 8;
  constexpr int16_t button_w = (DISPLAY_RGB_WIDTH - 2 * margin - 3 * gap) / 4; // 190 px

  lv_obj_t* home = lv_small_button(screen, margin, y, button_w,
    tr("Home", "Start"), lv_show_home_event);
  lv_obj_t* modules = lv_small_button(screen, margin + (button_w + gap), y, button_w,
    tr("Modules", "Module"), lv_show_modules_event);
  lv_obj_t* alarms = lv_small_button(screen, margin + 2 * (button_w + gap), y, button_w,
    tr("Alarms", "Alarme"), lv_show_alarms_event);
  lv_obj_t* system = lv_small_button(screen, margin + 3 * (button_w + gap), y, button_w,
    tr("System", "System"), lv_show_system_event);

  lv_obj_t* active_btn = active == 0 ? home : (active == 1 ? modules : (active == 2 ? alarms : system));
  lv_obj_set_style_bg_color(active_btn, lv_color_hex(0x246BFF), 0);
}

static void lv_create_app_screens() {
  ui_dashboard_screen = lv_obj_create(NULL);
  lv_style_screen(ui_dashboard_screen);
  lv_add_header(ui_dashboard_screen, "extractor");

  ui_home_content = lv_obj_create(ui_dashboard_screen);
  lv_obj_set_pos(ui_home_content, 8, 52);
  // Bottom navigation starts at y=432. End Home content at y=420 so card
  // borders/scrollbar cannot visually overlap the navigation buttons.
  lv_obj_set_size(ui_home_content, 784, 368);
  lv_obj_set_style_bg_opa(ui_home_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_home_content, 0, 0);
  lv_obj_set_style_pad_all(ui_home_content, 0, 0);
  lv_obj_set_scroll_dir(ui_home_content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ui_home_content, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_add_flag(ui_home_content, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_anim_duration(ui_home_content, 0, 0);

  ui_home_output_card = lv_card(ui_home_content, 4, 4, 376, 88, ui_theme_color(0x102C24, 0xEAF7F0));
  lv_obj_set_style_border_color(ui_home_output_card, lv_color_hex(0x2E6D55), 0);
  lv_label(ui_home_output_card, tr("Extraction", "Absaugung"), 16, 9, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 160);
  ui_home_output = lv_label(ui_home_output_card, "Idle", 16, 32, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 170);
  ui_home_power = lv_label(ui_home_output_card, "0%", 310, 3, lv_color_hex(0x2DFF88), UI_FONT_18, 54);
  ui_home_rpm = lv_label(ui_home_output_card, "0 rpm", 194, 5, lv_color_hex(0xDDE4EC), UI_FONT_18, 106);
  lv_obj_set_height(ui_home_rpm, 24);
  lv_label_set_long_mode(ui_home_rpm, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(ui_home_rpm, LV_TEXT_ALIGN_RIGHT, 0);
  ui_home_afterrun = lv_label(ui_home_output_card, "Afterrun 0s", 16, 54, lv_color_hex(0x96A0AA), UI_FONT_DEFAULT, 116);
  ui_home_continuous_button = lv_small_button(ui_home_output_card, 120, 32, 94, tr("Continuous", "Dauerlauf"), lv_home_continuous_event);
  ui_home_fan_button = lv_small_button(ui_home_output_card, 220, 32, 68, tr("Fan", "L\303\274fter"), lv_home_fan_event);
  ui_home_light_button = lv_small_button(ui_home_output_card, 296, 32, 68, tr("Light", "Licht"), lv_home_light_event);
  ui_home_io1_button = lv_small_button(ui_home_output_card, 220, 32, 68, "OUT 1", lv_home_io1_event);
  ui_home_io2_button = lv_small_button(ui_home_output_card, 296, 32, 68, "OUT 2", lv_home_io2_event);
  lv_obj_add_flag(ui_home_io1_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_home_io2_button, LV_OBJ_FLAG_HIDDEN);
  ui_home_power_bar = lv_mini_bar(ui_home_output_card, 16, 74, 344, 6, lv_color_hex(0x2DFF88));

  ui_home_settings_card = lv_card(ui_home_content, 4, 100, 376, 282, ui_theme_color(0x151B23, 0xF7FAFC));

  // Row 1: three clean columns. Labels sit above the controls so German text
  // can never intrude into a neighbouring dropdown/text field.
  lv_label(ui_home_settings_card, tr("Mode", "Stufe"), 12, 8,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 108);
  ui_home_mode_dropdown = lv_dropdown_create(ui_home_settings_card);
  lv_obj_set_pos(ui_home_mode_dropdown, 12, 30);
  lv_obj_set_size(ui_home_mode_dropdown, 108, 40);
  lv_dropdown_set_options(ui_home_mode_dropdown,
    display_language == 1 ? "Hoch 100%\nMittel 60%\nNiedrig 30%\nBenutzer" :
                            "High 100%\nMedium 60%\nLow 30%\nCustom");
  lv_apply_dropdown_theme(ui_home_mode_dropdown);
  lv_obj_set_style_pad_left(ui_home_mode_dropdown, 10, 0);
  lv_obj_set_style_pad_right(ui_home_mode_dropdown, 28, 0);
  lv_obj_add_event_cb(ui_home_mode_dropdown, lv_detail_jbc_mode_event, LV_EVENT_VALUE_CHANGED, NULL);

  lv_label(ui_home_settings_card, tr("Custom power %", "Leistung %"), 128, 8,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 108);
  ui_home_power_input = lv_numeric_field(ui_home_settings_card, 128, 30, 108,
    status.select_flow / 10U, NUMERIC_FIELD_JBC_POWER);

  lv_label(ui_home_settings_card, tr("Work afterrun s", "Work-Nachlauf s"), 244, 8,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 120);
  ui_home_delay_input = lv_numeric_field(ui_home_settings_card, 244, 30, 120,
    status.delay_work_s, NUMERIC_FIELD_JBC_DELAY_WORK);

  // Row 2: Afterrun controls.
  lv_label(ui_home_settings_card, tr("Afterrun", "Nachlauf"), 12, 84,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 108);
  ui_home_afterrun_power_button = lv_small_button(ui_home_settings_card, 12, 106, 108,
    tr("Enabled", "Aktiv"), lv_home_afterrun_power_event);

  lv_label(ui_home_settings_card, tr("Afterrun power %", "Nachlauf %"), 128, 84,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 108);
  ui_home_afterrun_power_input = lv_numeric_field(ui_home_settings_card, 128, 106, 108,
    status.afterrun_power / 10U, NUMERIC_FIELD_AFTER_POWER);

  // Full-width selectors: labels are above, never horizontally overlapping the box.
  lv_label(ui_home_settings_card, tr("Main input", "Haupteingang"), 12, 154,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 352);
  ui_home_input_dropdown = lv_dropdown_create(ui_home_settings_card);
  lv_obj_set_pos(ui_home_input_dropdown, 12, 174);
  lv_obj_set_size(ui_home_input_dropdown, 352, 40);
  lv_dropdown_set_options(ui_home_input_dropdown, "-");
  lv_apply_dropdown_theme(ui_home_input_dropdown, true);
  lv_obj_set_style_pad_left(ui_home_input_dropdown, 12, 0);
  lv_obj_set_style_pad_right(ui_home_input_dropdown, 30, 0);
  lv_obj_add_event_cb(ui_home_input_dropdown, lv_home_input_select_event, LV_EVENT_VALUE_CHANGED, NULL);

  lv_label(ui_home_settings_card, tr("Main output", "Hauptausgang"), 12, 220,
    lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 352);
  ui_home_output_dropdown = lv_dropdown_create(ui_home_settings_card);
  lv_obj_set_pos(ui_home_output_dropdown, 12, 240);
  lv_obj_set_size(ui_home_output_dropdown, 352, 40);
  lv_dropdown_set_options(ui_home_output_dropdown, "-");
  lv_apply_dropdown_theme(ui_home_output_dropdown, true);
  lv_obj_set_style_pad_left(ui_home_output_dropdown, 12, 0);
  lv_obj_set_style_pad_right(ui_home_output_dropdown, 30, 0);
  lv_obj_add_event_cb(ui_home_output_dropdown, lv_home_output_select_event, LV_EVENT_VALUE_CHANGED, NULL);

  ui_home_work_card = nullptr;
  ui_home_filter_card = nullptr;
  ui_home_jbc_card = lv_home_status_card(ui_home_content, 4, 72, "JBC Bus",
    lv_color_hex(0x2DFF88), &ui_home_jbc, &ui_home_work, nullptr, &ui_home_jbc_stripe, 392, 376);
  ui_home_work_icon = lv_image_create(ui_home_jbc_card);
  lv_image_set_src(ui_home_work_icon, &solder_iron_icon);
  lv_obj_set_pos(ui_home_work_icon, 26, 38);
  lv_obj_set_size(ui_home_work_icon, 20, 20);
  lv_obj_remove_flag(ui_home_work_icon, LV_OBJ_FLAG_CLICKABLE);
  if (ui_home_work) {
    lv_obj_set_x(ui_home_work, 54);
    lv_obj_set_width(ui_home_work, 300);
  }
  lv_set_home_work_icon(true, false);
  ui_home_suction_card = lv_home_status_card(ui_home_content, 84, 72, "Fan / IO",
    lv_color_hex(0x2997FF), &ui_home_suction, &ui_home_fan_detail, &ui_home_suction_title, &ui_home_suction_stripe, 392, 376);
  // Fan/IO controls intentionally stay out of the home card.
  // The home page already exposes output-dependent buttons in the Extraction card
  // and the power value in the Extraction settings. Module-specific controls are
  // built only on the module detail page.
  ui_home_weller_card = lv_home_status_card(ui_home_content, 164, 84, "Weller Zero Smog",
    lv_color_hex(0x58B8FF), &ui_home_weller, &ui_home_filter, nullptr, &ui_home_weller_stripe, 392, 376);
  ui_home_fault_card = lv_home_status_card(ui_home_content, 260, 72, tr("Alarm center", "Alarmzentrale"),
    lv_color_hex(0xB98CFF), &ui_home_fault, &ui_home_modules, nullptr, &ui_home_fault_stripe, 392, 376);
  lv_enable_home_module_card(ui_home_jbc_card, MODULE_JBC_BUS);
  lv_enable_home_module_card(ui_home_suction_card, MODULE_FAN_IO);
  lv_enable_home_module_card(ui_home_weller_card, MODULE_WELLER_ZERO_SMOG);
  lv_obj_add_flag(ui_home_fault_card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ui_home_fault_card, lv_show_alarms_event, LV_EVENT_CLICKED, NULL);

  lv_add_main_nav(ui_dashboard_screen, 0);

  ui_module_list_screen = lv_obj_create(NULL);
  lv_style_screen(ui_module_list_screen);
  lv_add_header(ui_module_list_screen, "bus modules");
  ui_module_list = lv_obj_create(ui_module_list_screen);
  lv_obj_set_pos(ui_module_list, 10, 58);
  lv_obj_set_size(ui_module_list, 780, 330);
  lv_obj_set_style_bg_opa(ui_module_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_module_list, 0, 0);
  lv_obj_set_style_pad_all(ui_module_list, 0, 0);
  lv_obj_set_scroll_dir(ui_module_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ui_module_list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_add_flag(ui_module_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_anim_duration(ui_module_list, 0, 0);
  for (uint8_t i = 0; i < 17; ++i) {
    lv_obj_t* row = lv_button_create(ui_module_list);
    ui_module_rows[i] = row;
    lv_obj_set_pos(row, 0, i * 54);
    lv_obj_set_size(row, 758, 48);
    lv_stabilize_button(row);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, ui_theme_color(0x151D27, 0xF7FAFC), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, ui_theme_color(0x2C3B4A, 0xC4D0DB), 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_user_data(row, (void*)(uintptr_t)i);
    lv_obj_add_event_cb(row, lv_module_row_event, LV_EVENT_CLICKED, NULL);
    ui_module_row_status_stripes[i] = lv_obj_create(row);
    lv_obj_set_pos(ui_module_row_status_stripes[i], 4, 8);
    lv_obj_set_size(ui_module_row_status_stripes[i], 6, 32);
    lv_obj_set_style_radius(ui_module_row_status_stripes[i], 3, 0);
    lv_obj_set_style_bg_color(ui_module_row_status_stripes[i], ui_theme_color(0x47515D, 0x9AA8B6), 0);
    lv_obj_set_style_bg_opa(ui_module_row_status_stripes[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_module_row_status_stripes[i], 0, 0);
    lv_obj_set_style_pad_all(ui_module_row_status_stripes[i], 0, 0);
    lv_obj_clear_flag(ui_module_row_status_stripes[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui_module_row_status_stripes[i], LV_OBJ_FLAG_CLICKABLE);
    ui_module_row_names[i] = lv_label(row, "", 18, 5, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 726);
    ui_module_row_meta[i] = lv_label(row, "", 18, 27, lv_color_hex(0x96A0AA), UI_FONT_DEFAULT, 726);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
  }
  ui_module_update_notice = lv_label(ui_module_list_screen, "", 16, 400, lv_color_hex(0x2997FF), UI_FONT_DEFAULT, 760);
  lv_obj_add_flag(ui_module_update_notice, LV_OBJ_FLAG_HIDDEN);
  lv_add_main_nav(ui_module_list_screen, 1);

  ui_alarm_screen = lv_obj_create(NULL);
  lv_style_screen(ui_alarm_screen);
  lv_add_header(ui_alarm_screen, "alarms");
  ui_alarm_list = lv_obj_create(ui_alarm_screen);
  lv_obj_set_pos(ui_alarm_list, 10, 58);
  lv_obj_set_size(ui_alarm_list, 780, 330);
  lv_obj_set_style_bg_opa(ui_alarm_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_alarm_list, 0, 0);
  lv_obj_set_style_pad_all(ui_alarm_list, 0, 0);
  lv_obj_set_scroll_dir(ui_alarm_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ui_alarm_list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_add_flag(ui_alarm_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  // Alarm UI has exactly eight backing pointer slots. Keep this loop
  // independent from the Universal/Modbus control capacity.
  for (uint8_t i = 0; i < 8; ++i) {
    ui_alarm_rows[i] = lv_card(ui_alarm_list, 0, i * 58, 758, 52, ui_theme_color(0x151B23, 0xF7FAFC));
    ui_alarm_titles[i] = lv_label(ui_alarm_rows[i], "", 14, 7, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 725);
    ui_alarm_details[i] = lv_label(ui_alarm_rows[i], "", 14, 29, lv_color_hex(0x96A0AA), UI_FONT_DEFAULT, 725);
    lv_obj_add_flag(ui_alarm_rows[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_add_main_nav(ui_alarm_screen, 2);

  ui_module_detail_screen = lv_obj_create(NULL);
  lv_style_screen(ui_module_detail_screen);
  lv_add_header(ui_module_detail_screen, "module details");
  lv_small_button(ui_module_detail_screen, 10, 46, 92, tr("Back", "Zur\303\274ck"), lv_back_to_modules_event);
  ui_detail_title = lv_label(ui_module_detail_screen, "Module", 116, 50, lv_color_hex(0xFFFFFF), UI_FONT_18, 662);
  lv_obj_t* detail = lv_obj_create(ui_module_detail_screen);
  lv_obj_set_pos(detail, 8, 90);
  lv_obj_set_size(detail, 784, 332);
  lv_obj_set_style_bg_color(detail, ui_theme_color(0x121A24, 0xF7FAFC), 0);
#if DISPLAY_FAST_UI
  lv_obj_set_style_bg_grad_dir(detail, LV_GRAD_DIR_NONE, 0);
#else
  lv_obj_set_style_bg_grad_color(detail, ui_theme_color(0x0E141D, 0xEDF4FA), 0);
  lv_obj_set_style_bg_grad_dir(detail, LV_GRAD_DIR_VER, 0);
#endif
  lv_obj_set_style_bg_opa(detail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(detail, 1, 0);
  lv_obj_set_style_border_color(detail, ui_theme_color(0x2C3B4A, 0xC4D0DB), 0);
  lv_obj_set_style_radius(detail, 18, 0);
  lv_obj_set_style_pad_all(detail, 0, 0);
  lv_obj_set_scroll_dir(detail, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(detail, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_add_flag(detail, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_anim_duration(detail, 0, 0);
  ui_detail_meta = lv_label(detail, "", 16, 14, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 716);
  ui_detail_status = lv_label(detail, tr("Loading module data...", "Moduldaten laden..."), 16, 40, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 300);
  lv_obj_set_style_bg_color(ui_detail_status, ui_theme_color(0x1C2A3A, 0xE8F1FA), 0);
  lv_obj_set_style_bg_opa(ui_detail_status, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ui_detail_status, 12, 0);
  lv_obj_set_style_pad_left(ui_detail_status, 10, 0);
  lv_obj_set_style_pad_right(ui_detail_status, 10, 0);
  lv_obj_set_style_pad_top(ui_detail_status, 4, 0);
  lv_obj_set_style_pad_bottom(ui_detail_status, 4, 0);
  ui_detail_values = lv_label(detail, "", 16, 72, ui_theme_color(0xDDE4EC, 0x263442), UI_FONT_DEFAULT, 730);
  lv_label_set_long_mode(ui_detail_values, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(ui_detail_values, 3, 0);
  ui_detail_work_icon = lv_image_create(detail);
  lv_image_set_src(ui_detail_work_icon, &solder_iron_icon);
  lv_obj_set_pos(ui_detail_work_icon, 736, 40);
  lv_obj_set_size(ui_detail_work_icon, 20, 20);
  lv_obj_remove_flag(ui_detail_work_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_set_detail_work_icon(false, false);
  ui_detail_controls = lv_obj_create(detail);
  lv_obj_set_pos(ui_detail_controls, 14, 166);
  lv_obj_set_size(ui_detail_controls, 740, 126);
  lv_obj_set_style_radius(ui_detail_controls, 14, 0);
  lv_obj_set_style_bg_color(ui_detail_controls, ui_theme_color(0x151F2B, 0xF3F7FB), 0);
  lv_obj_set_style_bg_opa(ui_detail_controls, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ui_detail_controls, 1, 0);
  lv_obj_set_style_border_color(ui_detail_controls, ui_theme_color(0x2A3A4B, 0xD5E0EA), 0);
  lv_obj_set_style_pad_all(ui_detail_controls, 0, 0);
  lv_obj_clear_flag(ui_detail_controls, LV_OBJ_FLAG_SCROLLABLE);

  ui_system_screen = lv_obj_create(NULL);
  lv_style_screen(ui_system_screen);
  lv_add_header(ui_system_screen, "display settings");
  lv_obj_t* system_body = lv_obj_create(ui_system_screen);
  lv_obj_set_pos(system_body, 12, 50);
  lv_obj_set_size(system_body, 776, 374);
  lv_obj_set_style_pad_all(system_body, 0, 0);
  lv_obj_set_style_border_width(system_body, 0, 0);
  lv_obj_set_style_bg_opa(system_body, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(system_body, LV_DIR_VER);
  lv_small_button(system_body, 0, 0, 776, tr("Connection: RS485 / WiFi", "Verbindung: RS485 / WLAN"), OfeDisplayWifiUi::openEvent);
  lv_obj_t* brightness = lv_card(system_body, 0, 54, 776, 112, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_label(brightness, tr("Brightness", "Helligkeit"), 16, 12, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 120);
  ui_system_brightness = lv_label(brightness, (String(display_brightness_pct) + "%").c_str(), 142, 12, lv_color_hex(0xFFFFFF), UI_FONT_DEFAULT, 70);
  lv_small_button(brightness, 16, 54, 92, tr("Darker", "Dunkler"), lv_brightness_down_event);
  lv_small_button(brightness, 116, 54, 92, tr("Brighter", "Heller"), lv_brightness_up_event);
  lv_label(brightness, tr("Screensaver", "Ruhemodus"), 232, 12, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 112);
  ui_system_screensaver = lv_dropdown_create(brightness);
  lv_obj_set_pos(ui_system_screensaver, 344, 7);
  lv_obj_set_size(ui_system_screensaver, 96, 44);
  lv_dropdown_set_options(ui_system_screensaver, display_language == 1 ? "Aus\n1 min\n2 min\n5 min\n10 min" : "Off\n1 min\n2 min\n5 min\n10 min");
  lv_dropdown_set_selected(ui_system_screensaver, screensaver_selection_from_timeout(screensaver_timeout_min));
  lv_obj_set_style_radius(ui_system_screensaver, 8, 0);
  lv_obj_set_style_bg_color(ui_system_screensaver, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
  lv_obj_set_style_border_color(ui_system_screensaver, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
  lv_obj_set_style_text_color(ui_system_screensaver, ui_theme_color(0xFFFFFF, 0x17212B), 0);
  lv_apply_dropdown_theme(ui_system_screensaver);
  lv_obj_add_event_cb(ui_system_screensaver, lv_screensaver_timeout_event, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t* language = lv_card(system_body, 0, 176, 776, 76, ui_theme_color(0x151B23, 0xF7FAFC));
  lv_label(language, tr("Language", "Sprache"), 12, 14, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 76);
  ui_system_language = lv_dropdown_create(language);
  lv_obj_set_pos(ui_system_language, 88, 9);
  lv_obj_set_size(ui_system_language, 130, 44);
  lv_dropdown_set_options(ui_system_language, "English\nDeutsch");
  lv_dropdown_set_selected(ui_system_language, display_language);
  lv_obj_set_style_radius(ui_system_language, 8, 0);
  lv_obj_set_style_bg_color(ui_system_language, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
  lv_obj_set_style_border_color(ui_system_language, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
  lv_obj_set_style_text_color(ui_system_language, ui_theme_color(0xFFFFFF, 0x17212B), 0);
  lv_apply_dropdown_theme(ui_system_language);
  lv_obj_add_event_cb(ui_system_language, lv_language_event, LV_EVENT_VALUE_CHANGED, NULL);

  lv_label(language, tr("Theme", "Farbschema"), 232, 14, lv_color_hex(0x8F9BA8), UI_FONT_DEFAULT, 86);
  ui_system_theme = lv_dropdown_create(language);
  lv_obj_set_pos(ui_system_theme, 320, 9);
  lv_obj_set_size(ui_system_theme, 124, 44);
  lv_dropdown_set_options(ui_system_theme, display_language == 1 ? "Dunkel\nHell" : "Dark\nLight");
  lv_dropdown_set_selected(ui_system_theme, display_theme);
  lv_obj_set_style_radius(ui_system_theme, 8, 0);
  lv_obj_set_style_bg_color(ui_system_theme, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
  lv_obj_set_style_border_color(ui_system_theme, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
  lv_obj_set_style_text_color(ui_system_theme, ui_theme_color(0xFFFFFF, 0x17212B), 0);
  lv_apply_dropdown_theme(ui_system_theme);
  lv_obj_add_event_cb(ui_system_theme, lv_theme_event, LV_EVENT_VALUE_CHANGED, NULL);

  lv_update_display_settings_widgets(true);
  lv_add_main_nav(ui_system_screen, 3);
}

static void lv_rebuild_app_ui() {
  OfeDisplayWifiUi::closePanel();
  const uint8_t return_view = display_view_mode;
  lv_obj_t* old_screens[] = {
    ui_dashboard_screen,
    ui_module_list_screen,
    ui_module_detail_screen,
    ui_alarm_screen,
    ui_system_screen,
    ui_boot_screen,
    ui_update_screen,
    ui_status_screen,
    ui_screensaver_screen
  };

  lv_numeric_hide();

  lv_obj_t* temp_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(temp_screen, ui_theme_color(0x090D12, 0xE9EFF6), 0);
  lv_obj_set_style_bg_opa(temp_screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(temp_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_screen_load(temp_screen);
  lvgl_timer_handler_profiled();

  for (uint8_t i = 0; i < sizeof(old_screens) / sizeof(old_screens[0]); ++i) {
    if (old_screens[i] && lv_obj_is_valid(old_screens[i])) lv_obj_delete(old_screens[i]);
  }
  ui_dashboard_screen = nullptr;
  ui_module_list_screen = nullptr;
  ui_module_detail_screen = nullptr;
  ui_alarm_screen = nullptr;
  ui_system_screen = nullptr;
  ui_boot_screen = nullptr;
  ui_update_screen = nullptr;
  ui_status_screen = nullptr;
  ui_screensaver_screen = nullptr;
  ui_screensaver_brand = nullptr;
  ui_screensaver_clock = nullptr;
  ui_screensaver_state = nullptr;
  ui_screensaver_power = nullptr;
  ui_screensaver_power_bar = nullptr;
  ui_screensaver_modules = nullptr;
  ui_screensaver_info = nullptr;
  ui_screensaver_alarm = nullptr;
  ui_screensaver_hint = nullptr;
  ui_status_msg = nullptr;
  ui_home_io1_button = nullptr;
  ui_home_io2_button = nullptr;
  ui_home_work_icon = nullptr;
  ui_header_link_count = 0;
  for (uint8_t i = 0; i < 10; ++i) {
    ui_header_link_cards[i] = nullptr;
    ui_header_link_labels[i] = nullptr;
    ui_header_clock_labels[i] = nullptr;
    ui_header_alarm_labels[i] = nullptr;
  }
  detail_controls_addr = 0;
  detail_controls_type = MODULE_UNKNOWN;
  detail_controls_caps = 0;
  detail_controls_signature = 0;
  // Recreated dropdowns start with the placeholder option. The option cache must
  // be cleared, otherwise lv_refresh_output_dropdown() may think the main-output
  // list is already loaded and leave the new dropdown empty after a theme change.
  home_output_options_cache = String();
  home_output_count = 0;
  memset(home_output_addrs, 0, sizeof(home_output_addrs));
  home_output_pending_route_addr = 0;
  home_output_pending_route_ms = 0;
  home_output_dropdown_refreshing = false;
  home_input_options_cache = String();
  home_input_count = 0;
  memset(home_input_values, 0, sizeof(home_input_values));
  home_input_pending_route_ms = 0;
  home_input_pending_value = 0;
  home_input_dropdown_refreshing = false;
  lv_display_set_theme(lvgl_disp, lv_theme_default_init(lvgl_disp, lv_color_hex(0x246BFF), lv_color_hex(0x2DCC88), display_theme == 0, UI_FONT_DEFAULT));
  if (ui_numeric_keyboard && lv_obj_is_valid(ui_numeric_keyboard)) lv_obj_delete(ui_numeric_keyboard);
  if (ui_numeric_editor && lv_obj_is_valid(ui_numeric_editor)) lv_obj_delete(ui_numeric_editor);
  ui_numeric_keyboard = nullptr;
  ui_numeric_editor = nullptr;
  lv_create_boot_screen();
  lv_create_update_screen();
  lv_create_app_screens();
  lv_create_status_screen();
  lv_create_screensaver_screen();
  lv_update_app_values();
  if (return_view == DISPLAY_VIEW_MODULE_LIST) {
    lv_update_module_list();
    lv_screen_switch(ui_module_list_screen);
  } else if (return_view == DISPLAY_VIEW_MODULE_DETAIL && selected_module.valid) {
    lv_update_module_detail();
    lv_screen_switch(ui_module_detail_screen);
  } else if (return_view == DISPLAY_VIEW_ALARMS) {
    lv_update_alarm_center();
    lv_screen_switch(ui_alarm_screen);
  } else if (return_view == DISPLAY_VIEW_SYSTEM) {
    lv_screen_switch(ui_system_screen);
  } else {
    display_view_mode = DISPLAY_VIEW_HOME;
    lv_screen_switch(ui_dashboard_screen);
  }
  if (temp_screen && lv_obj_is_valid(temp_screen)) lv_obj_delete(temp_screen);
}

static void lv_update_module_list() {
  // DATA is updated by the RS485/parser path regardless of the current view.
  // Only skip writing LVGL widgets when this screen is hidden.
  if (display_view_mode != DISPLAY_VIEW_MODULE_LIST &&
      (!ui_module_list_screen || lv_screen_active() != ui_module_list_screen)) {
    return;
  }

  const uint8_t master_state = master_link_state_now();
  const bool master_lost_now = master_state == 2;
  const bool master_waiting = master_state == 0;
  bool update_target_found = false;
  for (uint8_t i = 0; i < 17; ++i) {
    if (!ui_module_rows[i]) continue;
    if (i >= module_total || !module_summaries[i].valid) {
      lv_obj_add_flag(ui_module_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const DisplayModuleSummary& m = module_summaries[i];
    lv_obj_clear_flag(ui_module_rows[i], LV_OBJ_FLAG_HIDDEN);
    const bool row_is_updating = !master_lost_now && bus_update_is_for_addr(m.addr);
    if (row_is_updating) update_target_found = true;

    String title = lv_addr_text(m.addr) + "  " + String(m.name[0] ? m.name : display_module_type_name(m.type));
    String meta;
    lv_color_t border = ui_theme_color(0x4A3030, 0xB42318);
    lv_color_t accent = border;
    lv_color_t meta_color = ui_theme_color(0x96A0AA, 0x5E6D7C);
    lv_color_t bg = ui_theme_color(0x151D27, 0xF7FAFC);
    if (master_lost_now) {
      // The list entries are cached from the last master response. Do not show
      // them as online while the master is missing.
      meta = String(tr("cached - master lost  FW ", "Cache - Master fehlt  FW ")) + module_fw_text(m.fw_major, m.fw_minor, m.fw_patch, m.fw_suffix);
      title += String("  ") + tr("(stale)", "(Cache)");
      border = ui_theme_color(0xFF4D5E, 0xD92D20);
      accent = border;
      meta_color = ui_theme_color(0xFF8A98, 0xB42318);
      bg = ui_theme_color(0x261219, 0xFFE8EB);
    } else if (master_waiting) {
      meta = String(tr("cached - waiting  FW ", "Cache - warten  FW ")) + module_fw_text(m.fw_major, m.fw_minor, m.fw_patch, m.fw_suffix);
      border = ui_theme_color(0xFFB020, 0xB54708);
      accent = border;
      meta_color = ui_theme_color(0xFFD166, 0xB54708);
      bg = ui_theme_color(0x221C12, 0xFFF1C2);
    } else if (row_is_updating) {
      meta = bus_update_progress_text(status.update_progress);
      border = ui_theme_color(0x2997FF, 0x1570EF);
      accent = border;
      meta_color = ui_theme_color(0x58B8FF, 0x175CD3);
      bg = ui_theme_color(0x14243A, 0xE1F0FF);
    } else {
      meta = String((m.flags & 1) ? tr("online", "Online") : tr("offline", "Offline")) + "  " + module_fw_text(m.fw_major, m.fw_minor, m.fw_patch, m.fw_suffix);
      border = (m.flags & 1) ? ui_theme_color(0x2DCC88, 0x047857) : ui_theme_color(0xFF6B6B, 0xB42318);
      accent = border;
      meta_color = (m.flags & 1) ? ui_theme_color(0x2DFF88, 0x047857) : ui_theme_color(0xFF8A98, 0xB42318);
      bg = (m.flags & 1) ? ui_theme_color(0x151D27, 0xECFDF3) : ui_theme_color(0x151D27, 0xFFF1F3);
    }
    lv_set_text(ui_module_row_names[i], title);
    lv_set_text(ui_module_row_meta[i], meta);
    lv_obj_set_style_border_color(ui_module_rows[i], border, 0);
    lv_obj_set_style_bg_color(ui_module_rows[i], bg, 0);
    lv_obj_set_style_text_color(ui_module_row_meta[i], meta_color, 0);
    if (ui_module_row_status_stripes[i]) lv_obj_set_style_bg_color(ui_module_row_status_stripes[i], accent, 0);
  }

  if (ui_module_update_notice) {
    if (master_lost_now) {
      // The cached/master-lost state is already visible in every module row.
      // Keep the full list height and do not show an extra footer label that
      // can overlap the bottom navigation buttons.
      lv_obj_add_flag(ui_module_update_notice, LV_OBJ_FLAG_HIDDEN);
    } else if (status.update_active && !update_is_local_display_target(status.update_target) && !update_target_found) {
      const String name = status.update_name[0] ? String(status.update_name) : update_target_name(status.update_target);
      lv_set_text(ui_module_update_notice, String(tr("Update: ", "Update: ")) + name + "  " + bus_update_progress_text(status.update_progress));
      lv_obj_clear_flag(ui_module_update_notice, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui_module_update_notice, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ui_module_list) lv_obj_update_layout(ui_module_list);
}
static uint16_t lv_detail_controls_y() {
  uint16_t y = 0;
  if (selected_detail_is_jbc()) y = 232;
  else if (selected_detail_is_fan_io()) y = (selected_module.caps & CAP_FILTER_SENSOR) ? 268 : 220;
  else if (selected_detail_is_universal()) y = 220;
  else if (selected_detail_is_weller()) y = 214;
  else if (selected_module.type == MODULE_DISPLAY) y = 174;

  // Fan/IO Pro can show extra filter/calibration lines. Keep the control-card
  // below the actual text height so its border/buttons never overlap the
  // telemetry field when labels wrap or the German strings are longer.
  if (y && ui_detail_values) {
    lv_obj_update_layout(ui_detail_values);
    const uint16_t text_bottom = (uint16_t)(lv_obj_get_y(ui_detail_values) + lv_obj_get_height(ui_detail_values));
    const uint16_t dynamic_y = text_bottom + 16;
    if (dynamic_y > y) y = dynamic_y;
  }
  return y;
}

static uint16_t lv_detail_controls_h() {
  if (selected_detail_is_jbc()) return 144;
  if (selected_detail_is_weller()) return 132;
  if (selected_module.type == MODULE_DISPLAY) return 76;
  if (selected_detail_is_fan_io()) return 126;
  if (selected_detail_is_universal()) {
    const uint8_t n = selected_universal_control_count();
    return n ? (uint16_t)(28 + n * 68U) : 58;
  }
  return 0;
}

static void lv_build_detail_controls() {
  if (!ui_detail_controls || !selected_module.valid) return;
  const uint16_t controls_y = lv_detail_controls_y();
  const uint16_t controls_h = lv_detail_controls_h();
  lv_obj_set_y(ui_detail_controls, controls_y);
  lv_obj_set_height(ui_detail_controls, controls_h ? controls_h : 58);
  const bool controls_hidden = lv_obj_has_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
  const uint32_t next_controls_signature = selected_universal_controls_signature();
  if (!controls_hidden && detail_controls_addr == selected_module.addr &&
      detail_controls_type == selected_module.type && detail_controls_caps == selected_module.caps &&
      detail_controls_signature == next_controls_signature) return;
  detail_controls_addr = selected_module.addr;
  detail_controls_type = selected_module.type;
  detail_controls_caps = selected_module.caps;
  detail_controls_signature = next_controls_signature;
  lv_obj_clean(ui_detail_controls);
  ui_detail_weller_filter_dropdown = nullptr;
  ui_detail_jbc_mode_dropdown = nullptr;
  ui_detail_jbc_power_input = nullptr;
  ui_detail_jbc_delay_work_input = nullptr;
  ui_detail_jbc_delay_stand_input = nullptr;
  ui_detail_weller_speed_slider = nullptr;
  ui_detail_display_brightness_slider = nullptr;
  ui_detail_display_brightness_value = nullptr;
  ui_detail_display_language = nullptr;
  ui_detail_display_theme = nullptr;
  ui_detail_stand_button = nullptr;
  ui_detail_continuous_button = nullptr;
  ui_detail_fan_button = nullptr;
  ui_detail_out1_button = nullptr;
  ui_detail_out2_button = nullptr;
  ui_detail_output_power_slider = nullptr;
  ui_detail_output_power_value = nullptr;
  ui_detail_weller_fan_button = nullptr;
  ui_detail_weller_light_button = nullptr;
  for (uint8_t i = 0; i < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
    ui_detail_universal_sliders[i] = nullptr;
    ui_detail_universal_switches[i] = nullptr;
    ui_detail_universal_values[i] = nullptr;
    ui_detail_universal_buttons[i] = nullptr;
    ui_detail_universal_buttons_b[i] = nullptr;
    ui_detail_universal_selects[i] = nullptr;
  }
  lv_obj_clear_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);

  if (selected_detail_is_jbc()) {
    lv_label(ui_detail_controls, tr("Mode", "Stufe"), 10, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 94);
    ui_detail_jbc_mode_dropdown = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_jbc_mode_dropdown, 10, 28);
    lv_obj_set_size(ui_detail_jbc_mode_dropdown, 96, 38);
    lv_dropdown_set_options(ui_detail_jbc_mode_dropdown,
      display_language == 1 ? "Hoch 100%\nMittel 60%\nNiedrig 30%\nBenutzer" : "High 100%\nMedium 60%\nLow 30%\nCustom");
    lv_dropdown_set_selected(ui_detail_jbc_mode_dropdown, selected_module.suction > 3 ? 3 : selected_module.suction);
    lv_obj_set_style_radius(ui_detail_jbc_mode_dropdown, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_jbc_mode_dropdown, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_border_color(ui_detail_jbc_mode_dropdown, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
    lv_obj_set_style_text_color(ui_detail_jbc_mode_dropdown, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_jbc_mode_dropdown);
    lv_obj_add_event_cb(ui_detail_jbc_mode_dropdown, lv_detail_jbc_mode_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_label(ui_detail_controls, tr("Power %", "Leistung %"), 116, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 94);
    ui_detail_jbc_power_input = lv_numeric_field(ui_detail_controls, 116, 28, 86, selected_module.select_flow / 10U, NUMERIC_FIELD_JBC_POWER);
    ui_detail_stand_button = lv_small_button(ui_detail_controls, 212, 28, 98, tr("Stand", "Stand"), lv_stand_intakes_toggle_event);
    ui_detail_continuous_button = lv_small_button(ui_detail_controls, 318, 28, 108, tr("Continuous", "Dauerlauf"), lv_continuous_toggle_event);

    lv_label(ui_detail_controls, tr("Work delay (s)", "Work Nachlauf (s)"), 10, 76, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 202);
    ui_detail_jbc_delay_work_input = lv_numeric_field(ui_detail_controls, 10, 96, 202, selected_module.delay_work, NUMERIC_FIELD_JBC_DELAY_WORK);
    lv_label(ui_detail_controls, tr("Stand delay (s)", "Stand Nachlauf (s)"), 224, 76, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 202);
    ui_detail_jbc_delay_stand_input = lv_numeric_field(ui_detail_controls, 224, 96, 202, selected_module.delay_stand, NUMERIC_FIELD_JBC_DELAY_STAND);
  } else if (selected_detail_is_fan_io()) {
    String main_label = detail_alias_or(selected_module.io_main_alias, tr("Relay/Fan", "Relais/L\303\274fter"));
    String out1_label = detail_alias_or(selected_module.io_out1_alias, "OUT1");
    String out2_label = detail_alias_or(selected_module.io_out2_alias, "OUT2");
    ui_detail_fan_button = lv_small_button(ui_detail_controls, 10, 10, 112, main_label.c_str(), lv_detail_fan_event);
    ui_detail_out1_button = lv_small_button(ui_detail_controls, 132, 10, 90, out1_label.c_str(), lv_detail_io1_event);
    ui_detail_out2_button = lv_small_button(ui_detail_controls, 232, 10, 90, out2_label.c_str(), lv_detail_io2_event);
    lv_label(ui_detail_controls, tr("Power", "Leistung"), 10, 62, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 96);
    ui_detail_output_power_value = lv_label(ui_detail_controls, "-", 360, 62, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 64);
    ui_detail_output_power_slider = lv_slider_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_output_power_slider, 116, 86);
    lv_obj_set_size(ui_detail_output_power_slider, 240, 12);
    lv_slider_set_range(ui_detail_output_power_slider, 10, 100);
    lv_slider_set_value(ui_detail_output_power_slider, constrain(selected_module.output_power / 10, 10, 100), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui_detail_output_power_slider, ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_detail_output_power_slider, lv_color_hex(0x2997FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_detail_output_power_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(ui_detail_output_power_slider, lv_detail_output_power_slider_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_detail_output_power_slider, lv_detail_output_power_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_detail_output_power_slider, lv_detail_output_power_slider_event, LV_EVENT_RELEASED, NULL);
  } else if (selected_detail_is_universal()) {
    uint8_t row = 0;
    for (uint8_t i = 0; i < selected_module.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX && row < DISPLAY_UNIVERSAL_DETAIL_CONTROL_MAX; ++i) {
      const DisplayUniversalEntity& e = selected_module.universal_entities[i];
      if (!e.valid || !universal_entity_writable(e)) continue;
      if (!(e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BUTTON || e.type == DISPLAY_UNI_NUMBER || e.type == DISPLAY_UNI_SELECT)) continue;
      const uint8_t slot = row;
      const uint16_t y = 12 + row * 68U;
      lv_label(ui_detail_controls, e.label[0] ? e.label : "Entity", 10, y, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 190);
      if (e.type == DISPLAY_UNI_NUMBER) {
        const bool readback_available = universal_entity_readable(e);
        const int16_t actual_value = e.value;
        int16_t control_value = actual_value;
        const bool has_pending = universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_NUMBER, control_value);
        String value_text = (!readback_available && !has_pending) ? String("-") : String(readback_available ? actual_value : control_value);
        if (value_text != "-" && e.unit[0]) value_text += String(" ") + e.unit;
        ui_detail_universal_values[slot] = lv_label(ui_detail_controls, value_text.c_str(), 354, y, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 70);
        ui_detail_universal_sliders[slot] = lv_slider_create(ui_detail_controls);
        lv_obj_set_pos(ui_detail_universal_sliders[slot], 154, y + 30);
        lv_obj_set_size(ui_detail_universal_sliders[slot], 188, 12);
        int16_t min_v = e.min_value;
        int16_t max_v = e.max_value;
        if (max_v <= min_v) { min_v = 0; max_v = 100; }
        if (min_v < 0) min_v = 0;
        if (max_v > 255) max_v = 255;
        lv_slider_set_range(ui_detail_universal_sliders[slot], min_v, max_v);
        lv_slider_set_value(ui_detail_universal_sliders[slot], constrain(control_value, min_v, max_v), LV_ANIM_OFF);
        lv_obj_set_user_data(ui_detail_universal_sliders[slot], (void*)(uintptr_t)e.id);
        lv_obj_set_style_bg_color(ui_detail_universal_sliders[slot], ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_detail_universal_sliders[slot], lv_color_hex(0x2997FF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_detail_universal_sliders[slot], lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_add_event_cb(ui_detail_universal_sliders[slot], lv_detail_universal_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(ui_detail_universal_sliders[slot], lv_detail_universal_slider_event, LV_EVENT_RELEASED, NULL);
      } else if (e.type == DISPLAY_UNI_SWITCH) {
        const bool readback_available = universal_entity_readable(e);
        const int16_t actual_value = e.value ? 1 : 0;
        int16_t control_value = actual_value;
        const bool has_pending = universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_SWITCH, control_value);

        lv_label(ui_detail_controls,
          readback_available ? tr("State", "Status") : tr("Target", "Soll"),
          254, y, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 58);
        const char* shown_text = (!readback_available && !has_pending) ? "-" : on_off((readback_available ? actual_value : control_value) != 0);
        ui_detail_universal_values[slot] = lv_label(
          ui_detail_controls, shown_text,
          350, y, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 76);
        ui_detail_universal_switches[slot] = lv_switch_create(ui_detail_controls);
        lv_obj_set_pos(ui_detail_universal_switches[slot], 254, y + 22);
        lv_obj_set_size(ui_detail_universal_switches[slot], 74, 34);
        lv_obj_set_user_data(ui_detail_universal_switches[slot], (void*)(uintptr_t)e.id);
        lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], lv_color_hex(0x167A4A), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_detail_universal_switches[slot], lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_border_width(ui_detail_universal_switches[slot], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(ui_detail_universal_switches[slot], ui_theme_color(0x3B5570, 0xB5C5D5), LV_PART_MAIN);
        if (control_value) lv_obj_add_state(ui_detail_universal_switches[slot], LV_STATE_CHECKED);
        else lv_obj_clear_state(ui_detail_universal_switches[slot], LV_STATE_CHECKED);
        lv_obj_add_event_cb(ui_detail_universal_switches[slot], lv_detail_universal_switch_event, LV_EVENT_VALUE_CHANGED, NULL);
      } else if (e.type == DISPLAY_UNI_SELECT) {
        int16_t shown_index = e.value;
        const bool readback_available = universal_entity_readable(e);
        const bool has_pending = universal_pending_value(selected_module.addr, e.id, DISPLAY_UNI_SELECT, shown_index);
        String current_text = readback_available
          ? universal_entity_value_text(e)
          : (has_pending ? universal_select_option_text(e, shown_index) : String("-"));
        ui_detail_universal_values[slot] = lv_label(ui_detail_controls, current_text.c_str(), 354, y, ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 70);
        ui_detail_universal_selects[slot] = lv_dropdown_create(ui_detail_controls);
        lv_obj_set_pos(ui_detail_universal_selects[slot], 154, y + 24);
        lv_obj_set_size(ui_detail_universal_selects[slot], 188, 38);
        String profile_opts = e.options[0] ? String(e.options) : current_text;
        profile_opts.replace("|", "\n");
        profile_opts.replace(";", "\n");
        String opts = String(tr("Please choose option", "Bitte Option w\303\244hlen"));
        if (profile_opts.length()) opts += "\n" + profile_opts;
        lv_dropdown_set_options(ui_detail_universal_selects[slot], opts.c_str());
        // Index 0 stays a neutral command placeholder. RW readback is shown
        // in the separate value column beside the control.
        ui_detail_universal_select_refreshing = true;
        lv_dropdown_set_selected(ui_detail_universal_selects[slot], 0);
        ui_detail_universal_select_refreshing = false;
        lv_obj_set_user_data(ui_detail_universal_selects[slot], (void*)(uintptr_t)e.id);
        lv_obj_set_style_radius(ui_detail_universal_selects[slot], 8, 0);
        lv_obj_set_style_bg_color(ui_detail_universal_selects[slot], ui_theme_color(0x1A2633, 0xF8FAFC), 0);
        lv_obj_set_style_border_color(ui_detail_universal_selects[slot], ui_theme_color(0x3A5068, 0xB1C1D0), 0);
        lv_obj_set_style_text_color(ui_detail_universal_selects[slot], ui_theme_color(0xFFFFFF, 0x17212B), 0);
        lv_apply_dropdown_theme(ui_detail_universal_selects[slot]);
        lv_obj_add_event_cb(ui_detail_universal_selects[slot], lv_detail_universal_select_event, LV_EVENT_VALUE_CHANGED, NULL);
      } else {
        ui_detail_universal_buttons[slot] = lv_small_button(ui_detail_controls, 254, y + 8, 172, tr("Send", "Senden"), lv_detail_universal_button_event);
        lv_obj_set_user_data(ui_detail_universal_buttons[slot], (void*)(uintptr_t)e.id);
      }
      ++row;
    }
    if (!row) {
      const bool descriptor_known = selected_module.universal_descriptor_crc != 0 ||
        selected_module.universal_entity_total != 0 || selected_module.universal_entity_count != 0;
      const bool descriptor_incomplete = selected_module.universal_entity_total != 0 &&
        selected_module.universal_entity_count < selected_module.universal_entity_total;
      const char* msg = (!descriptor_known || descriptor_incomplete)
        ? tr("Loading profile controls...", "Profil-Controls werden geladen...")
        : tr("Profile has no writable display controls", "Profil hat keine schreibbaren Display-Controls");
      lv_label(ui_detail_controls, msg, 14, 18,
        ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 398);
    }
  } else if (selected_detail_is_weller()) {
    ui_detail_weller_fan_button = lv_small_button(ui_detail_controls, 10, 10, 96, tr("Fan", "L\303\274fter"), lv_detail_weller_fan_event);
    ui_detail_weller_light_button = lv_small_button(ui_detail_controls, 114, 10, 96, tr("Light", "Licht"), lv_detail_weller_light_event);
    lv_label(ui_detail_controls, tr("Speed", "Drehzahl"), 230, 8, ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 196);
    ui_detail_weller_speed_slider = lv_slider_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_weller_speed_slider, 230, 34);
    lv_obj_set_size(ui_detail_weller_speed_slider, 196, 12);
    lv_slider_set_range(ui_detail_weller_speed_slider, 30, 100);
    lv_slider_set_value(ui_detail_weller_speed_slider,
      constrain(selected_module.weller_speed, 30, 100), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui_detail_weller_speed_slider, ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_detail_weller_speed_slider, lv_color_hex(0x2997FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_detail_weller_speed_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(ui_detail_weller_speed_slider,
      lv_detail_weller_speed_slider_event, LV_EVENT_RELEASED, NULL);
    ui_detail_weller_filter_dropdown = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_weller_filter_dropdown, 10, 76);
    lv_obj_set_size(ui_detail_weller_filter_dropdown, 202, 40);
    lv_dropdown_set_options(ui_detail_weller_filter_dropdown, WELLER_FILTER_OPTIONS);
    lv_dropdown_set_selected(ui_detail_weller_filter_dropdown,
      weller_filter_preset_index(selected_module.weller_programmed));
    lv_obj_set_style_radius(ui_detail_weller_filter_dropdown, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_weller_filter_dropdown, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_border_color(ui_detail_weller_filter_dropdown, ui_theme_color(0x3A5068, 0xB1C1D0), 0);
    lv_obj_set_style_text_color(ui_detail_weller_filter_dropdown, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_weller_filter_dropdown);
    lv_obj_add_event_cb(ui_detail_weller_filter_dropdown,
      lv_detail_weller_filter_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_small_button(ui_detail_controls, 224, 76, 202, tr("Reset filter", "Filter zur\303\274cksetzen"), lv_detail_weller_reset_event);
  } else if (selected_module.type == MODULE_DISPLAY && selected_module.addr == module_addr) {
    lv_label(ui_detail_controls, tr("Brightness", "Helligkeit"), 10, 8,
      ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 118);
    ui_detail_display_brightness_value = lv_label(ui_detail_controls, "", 142, 8,
      ui_theme_color(0xFFFFFF, 0x17212B), UI_FONT_DEFAULT, 54);
    lv_set_text(ui_detail_display_brightness_value, String(display_brightness_pct) + "%");
    ui_detail_display_brightness_slider = lv_slider_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_display_brightness_slider, 10, 38);
    lv_obj_set_size(ui_detail_display_brightness_slider, 188, 12);
    lv_slider_set_range(ui_detail_display_brightness_slider, 10, 100);
    lv_slider_set_value(ui_detail_display_brightness_slider, display_brightness_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui_detail_display_brightness_slider, ui_theme_color(0x263442, 0xD5DEE7), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_detail_display_brightness_slider, lv_color_hex(0x2997FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_detail_display_brightness_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(ui_detail_display_brightness_slider, lv_detail_display_brightness_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_detail_display_brightness_slider, lv_detail_display_brightness_event, LV_EVENT_RELEASED, NULL);

    lv_label(ui_detail_controls, tr("Language", "Sprache"), 220, 8,
      ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 94);
    ui_detail_display_language = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_display_language, 220, 28);
    lv_obj_set_size(ui_detail_display_language, 98, 40);
    lv_dropdown_set_options(ui_detail_display_language, "English\nDeutsch");
    lv_dropdown_set_selected(ui_detail_display_language, display_language);
    lv_obj_set_style_radius(ui_detail_display_language, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_display_language, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_text_color(ui_detail_display_language, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_display_language);
    lv_obj_add_event_cb(ui_detail_display_language, lv_language_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_label(ui_detail_controls, tr("Theme", "Farben"), 330, 8,
      ui_theme_color(0x96A0AA, 0x5E6D7C), UI_FONT_DEFAULT, 96);
    ui_detail_display_theme = lv_dropdown_create(ui_detail_controls);
    lv_obj_set_pos(ui_detail_display_theme, 330, 28);
    lv_obj_set_size(ui_detail_display_theme, 96, 40);
    lv_dropdown_set_options(ui_detail_display_theme,
      display_language == 1 ? "Dunkel\nHell" : "Dark\nLight");
    lv_dropdown_set_selected(ui_detail_display_theme, display_theme);
    lv_obj_set_style_radius(ui_detail_display_theme, 8, 0);
    lv_obj_set_style_bg_color(ui_detail_display_theme, ui_theme_color(0x1A2633, 0xF8FAFC), 0);
    lv_obj_set_style_text_color(ui_detail_display_theme, ui_theme_color(0xFFFFFF, 0x17212B), 0);
    lv_apply_dropdown_theme(ui_detail_display_theme);
    lv_obj_add_event_cb(ui_detail_display_theme, lv_theme_event, LV_EVENT_VALUE_CHANGED, NULL);
  } else {
    lv_obj_add_flag(ui_detail_controls, LV_OBJ_FLAG_HIDDEN);
  }
}
static String pro_filter_text(uint16_t saturation_permille, int16_t pressure_raw) {
  String text = String((saturation_permille + 5) / 10) + "%";
  text += "   ";
  text += tr("Pressure ", "Druck ");
  text += String(pressure_raw);
  return text;
}

static String pro_calibration_text(int16_t zero_raw, int16_t clean_raw, int16_t full_raw) {
  return String(tr("Cal: zero ", "Kal: Null ")) + String(zero_raw) +
    tr("  clean ", "  sauber ") + String(clean_raw) +
    tr("  full ", "  voll ") + String(full_raw);
}
static String lv_detail_system_text() {
  return String("\n") + tr("Uptime: ", "Laufzeit: ") + fmt_uptime(selected_module.uptime_s) + "   Heap: " + fmt_bytes(selected_module.heap_free) +
    "\nCPU: " + String(selected_module.cpu_load) + "%   Loop max: " + String(selected_module.loop_max_ms) + "ms";
}
static String jbc_device_id_text() {
  const DisplayModuleDetail& module = selected_module;
  String text;
  for (uint8_t i = 0; i < module.jbc_device_id_len; ++i) {
    const uint8_t c = module.jbc_device_id[i];
    if (!c) break;
    text += (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
  }
  text.trim();
  return text.length() ? text : String("-");
}
static void sync_selected_module_detail_to_home_cache() {
  const DisplayModuleDetail& m = selected_module;
  if (!m.valid) return;
  bool changed = false;
  auto set_u8 = [&](uint8_t& dst, uint8_t value) { if (dst != value) { dst = value; changed = true; } };
  auto set_u16 = [&](uint16_t& dst, uint16_t value) { if (dst != value) { dst = value; changed = true; } };
  auto set_bool = [&](bool& dst, bool value) { if (dst != value) { dst = value; changed = true; } };

  const uint32_t now = millis();
  if (detail_fields_is_weller(m.type, m.caps)) {
    home_weller_cache.valid = true;
    home_weller_cache.addr = m.addr;
    home_weller_cache.connected = ((m.flags & 0x01) != 0) && m.weller_uart_age <= 10;
    home_weller_cache.speed = m.weller_speed;
    home_weller_cache.filter = m.weller_filter;
    home_weller_cache.runtime = m.weller_runtime;
    home_weller_cache.programmed = m.weller_programmed;
    home_weller_cache.rpm = m.weller_rpm;
    home_weller_cache.version = m.weller_version;
    home_weller_cache.light = m.weller_light;
    home_weller_cache.io_outputs = m.io_outputs;
    home_weller_cache.uart_age = m.weller_uart_age;
    home_weller_cache.last_ms = now;

    set_bool(status.weller_present, true);
    set_bool(status.weller_connected, home_weller_cache.connected);
    set_u8(status.weller_speed, m.weller_speed);
    set_u8(status.weller_filter_status, m.weller_filter);
    set_u16(status.weller_filter_runtime_min, m.weller_runtime);
    set_u16(status.weller_filter_programmed_min, m.weller_programmed);
    set_u16(status.weller_version, m.weller_version);
    set_u8(status.weller_light, m.weller_light);
    if (m.weller_rpm && active_output_is_weller()) set_u16(status.fan_rpm, m.weller_rpm);
    // Do not globally overwrite io_output_mask here when another Fan/IO module is
    // the main output. Home now keeps separate Weller/FanIO caches for tiles.
    changed = true;
  } else if (detail_fields_is_fan_io(m.type, m.caps)) {
    home_fan_io_cache.valid = true;
    home_fan_io_cache.addr = m.addr;
    home_fan_io_cache.online = (m.flags & 0x01) != 0;
    home_fan_io_cache.relay_style = (m.type == MODULE_FAN_IO_PRO) || (m.caps & CAP_RELAY_OUTPUT);
    home_fan_io_cache.inputs = m.io_inputs;
    home_fan_io_cache.outputs = m.io_outputs;
    home_fan_io_cache.faults = m.io_faults;
    home_fan_io_cache.output_enabled = m.output_enabled;
    home_fan_io_cache.output_power = m.output_power;
    home_fan_io_cache.output_rpm = m.output_rpm;
    home_fan_io_cache.output_fault = m.output_fault;
    home_fan_io_cache.filter_saturation_permille = m.filter_saturation_permille;
    home_fan_io_cache.filter_pressure_raw = m.filter_pressure_raw;
    home_fan_io_cache.filter_zero_raw = m.filter_zero_raw;
    home_fan_io_cache.filter_clean_raw = m.filter_clean_raw;
    home_fan_io_cache.filter_full_raw = m.filter_full_raw;
    copy_cstr_alias(home_fan_io_cache.io_main_alias, sizeof(home_fan_io_cache.io_main_alias), m.io_main_alias);
    copy_cstr_alias(home_fan_io_cache.io_in1_alias, sizeof(home_fan_io_cache.io_in1_alias), m.io_in1_alias);
    copy_cstr_alias(home_fan_io_cache.io_in2_alias, sizeof(home_fan_io_cache.io_in2_alias), m.io_in2_alias);
    copy_cstr_alias(home_fan_io_cache.io_out1_alias, sizeof(home_fan_io_cache.io_out1_alias), m.io_out1_alias);
    copy_cstr_alias(home_fan_io_cache.io_out2_alias, sizeof(home_fan_io_cache.io_out2_alias), m.io_out2_alias);
    home_fan_io_cache.last_ms = now;

    set_bool(status.fan_present, true);
    if (active_output_is_fan_io() || status.output_addr == m.addr) {
      set_u16(status.io_input_mask, m.io_inputs);
      set_u16(status.io_output_mask, m.io_outputs);
      set_u16(status.io_fault_mask, m.io_faults);
      set_u16(status.module_output_power, m.output_power);
      // Keep Master-owned configuration fields stable. select_flow is the
      // configured custom suction level from DISPLAY_STATUS; a Fan/IO detail
      // frame carries module feedback/setpoint and must not overwrite it.
      set_u16(status.module_output_rpm, m.output_rpm);
      set_u16(status.module_output_fault, m.output_fault);
      if (m.output_rpm) set_u16(status.fan_rpm, m.output_rpm);
    }
    changed = true;
  } else if (detail_fields_is_jbc(m.type, m.caps)) {
    set_bool(status.jbc_present, true);
    // Do not derive station-link state from the module-online flag. DISPLAY_STATUS
    // owns jbc_connected and already uses the JBC fast flag / station address.
    // Overwriting it here made the Home JBC card jump between "station offline"
    // and "Station -- | Work off" whenever a background detail frame arrived.
    set_u8(status.jbc_addr, m.jbc_addr);
    set_u8(status.station_addr, m.station_addr);
    set_u8(status.jbc_link_flags, m.jbc_flags);
    set_u8(status.jbc_work_mask, m.jbc_work);
    set_u8(status.jbc_stand_mask, m.jbc_stand);
    if (m.jbc_work) set_u8(status.work_mask, m.jbc_work);
  }

  if (changed) {
    if (running_in_rs485_task()) {
      ui_defer_flags(UI_DEFER_APP_VALUES);
    } else if (lvgl_ready && display_view_mode == DISPLAY_VIEW_HOME) {
      lv_update_app_values();
    }
  }
}

static const char* jbc_usb_core_family_text(uint8_t family) {
  switch (family) {
    case 1: return "SOLD";
    case 2: return "HA";
    case 3: return "CL";
    case 4: return "PH";
    case 5: return "FE";
    case 6: return "SF";
    default: return "UNKNOWN";
  }
}

static const char* jbc_usb_core_state_text(uint8_t state) {
  switch (state) {
    case 1: return "WORK";
    case 2: return "STAND";
    case 3: return "SLEEP";
    case 4: return "HIBERNATION";
    case 5: return "COOLING";
    case 6: return "SUCTION";
    case 7: return "CLEANING";
    case 8: return "FEEDING";
    case 9: return tr("NO TOOL", "KEIN TOOL");
    case 10: return "EXTRACTOR";
    default: return "IDLE";
  }
}

static const char* jbc_usb_core_cleaner_mode_text(uint8_t mode) {
  switch (mode) {
    case 1: return "DETECTION";
    case 2: return "CONTINUOUS";
    case 4: return "CALIBRATING";
    default: return "UNKNOWN";
  }
}

static const char* jbc_usb_core_suction_text(uint8_t level) {
  switch (level) {
    case 0: return "HIGH";
    case 1: return "MEDIUM";
    case 2: return "LOW";
    case 3: return "CUSTOM";
    default: return "UNKNOWN";
  }
}

static String jbc_usb_core_temp_pair(uint16_t actual_c, uint16_t selected_c) {
  String out = actual_c == 0xFFFFU ? String("-") : String(actual_c);
  out += " / ";
  out += selected_c == 0xFFFFU ? String("-") : String(selected_c);
  out += " \302\260C";
  return out;
}

static void jbc_usb_core_model_key(const char* model, char* out, size_t out_len) {
  if (!out || !out_len) return;
  out[0] = 0;
  if (!model) return;
  size_t n = 0;
  for (const char* q = model; *q && n + 1 < out_len; ++q) {
    char ch = *q;
    if (ch == '/' || ch == '-' || ch == '_' || ch == ' ' || ch == '\t') continue;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - ('a' - 'A'));
    out[n++] = ch;
  }
  out[n] = 0;
}

static uint8_t jbc_usb_core_generic_tool_id(const DisplayJbcUsbCore& c, uint8_t raw) {
  char key[16];
  jbc_usb_core_model_key(c.model, key, sizeof(key));
  if (c.family == 1) {
    if (!strcmp(key, "HD") || !strcmp(key, "HDE")) return 9;
    if (!strcmp(key, "NA")) {
      if (raw == 0) return 0;
      if (raw == 1) return 7;
      if (raw == 3) return 8;
      return raw;
    }
    if (!strcmp(key, "ALE")) return 10;
    return raw;
  }
  if (c.family == 2 && raw > 0) return (uint8_t)(raw + 30U);
  return raw;
}

static String jbc_usb_core_tool_text(const DisplayJbcUsbCore& c, uint8_t raw) {
  const uint8_t id = jbc_usb_core_generic_tool_id(c, raw);
  switch (id) {
    case 0: return String(tr("No tool", "Kein Werkzeug"));
    case 1: return String("T210");
    case 2: return String("T245");
    case 3: return String("PA");
    case 4: return String("HT");
    case 5: return String("DS");
    case 6: return String("DR");
    case 7: return String("NT105");
    case 8: return String("NP105");
    case 9: return String("T470");
    case 10: return String("ALE250");
    case 31: return String("JT");
    case 32: return String("TE");
    case 33: return String("PHS");
    case 34: return String("PHB");
    default: { char buf[8]; snprintf(buf, sizeof(buf), "0x%02X", (unsigned)id); return String(buf); }
  }
}

static uint8_t jbc_usb_core_error_code(uint8_t family, uint8_t raw) {
  if (!raw) return 0;
  if (family == 2) return (uint8_t)(raw + 20U);
  if (family == 4) return (uint8_t)(raw + 40U);
  return raw;
}

static String jbc_usb_core_error_text(uint8_t family, uint8_t raw) {
  const uint8_t code = jbc_usb_core_error_code(family, raw);
  if (!code) return String("OK");
  const char* name = nullptr;
  switch (code) {
    case 1: name = "SHORTCIRCUIT"; break;
    case 2: name = "SHORTCIRCUIT_NR"; break;
    case 3: name = "OPENCIRCUIT"; break;
    case 4: name = "NO_TOOL"; break;
    case 5: name = "WRONGTOOL"; break;
    case 6: name = "DETECTIONTOOL"; break;
    case 7: name = "MAXPOWER"; break;
    case 8: name = "STOPOVERLOAD_MOS"; break;
    case 9: name = "TIN_FEEDER_CLOGGING"; break;
    case 21: name = "AIR_PUMP_ERROR"; break;
    case 22: name = "PROTECION_TC_HIGH"; break;
    case 23: name = "REGULATION_TC_HIGH"; break;
    case 24: name = "EXTERNAL_TC_MISSING"; break;
    case 25: name = "SELECTED_TEMP_NOT_REACHED"; break;
    case 26: name = "HIGH_HEATER_INTENSITY"; break;
    case 27: name = "LOW_HEATER_RESISTANCE"; break;
    case 28: name = "WRONG_HEATER"; break;
    case 29: name = "NOTOOL_HA"; break;
    case 30: name = "DETECTIONTOOL_HA"; break;
    case 41: name = "SELECTED_TEMP_NOT_REACHED_PH"; break;
    case 42: name = "LOW_HEATER_INTENSITY"; break;
    case 43: name = "TC1_NOT_CONNECTED"; break;
    case 44: name = "TC2_NOT_CONNECTED"; break;
    case 45: name = "TC3_NOT_CONNECTED"; break;
    case 46: name = "TC4_NOT_CONNECTED"; break;
    case 47: name = "TC1_LIMIT_REACHED"; break;
    case 48: name = "TC2_LIMIT_REACHED"; break;
    case 49: name = "TC3_LIMIT_REACHED"; break;
    case 50: name = "TC4_LIMIT_REACHED"; break;
  }
  if (name) return String(name);
  char buf[16]; snprintf(buf, sizeof(buf), "ERROR_0x%02X", (unsigned)code); return String(buf);
}

static String jbc_usb_core_station_error_text(uint16_t value) {
  if (!value) return String("OK");
  const char* name = nullptr;
  switch (value) {
    case 1: name = "STOPOVERLOAD_TRAFO"; break;
    case 2: name = "WRONGSENSOR_TRAFO"; break;
    case 3: name = "MEMORY"; break;
    case 4: name = "MAINSFREQUENCY"; break;
    case 5: name = "STATION_MODEL"; break;
    case 6: name = "NOT_MCU_TOOLS"; break;
  }
  if (name) return String(name);
  char buf[18]; snprintf(buf, sizeof(buf), "ERROR_0x%04X", (unsigned)value); return String(buf);
}

static String jbc_usb_core_tts_text(uint16_t seconds) {
  const uint16_t min = (uint16_t)(seconds / 60U);
  const uint16_t sec = (uint16_t)(seconds % 60U);
  char buf[24]; snprintf(buf, sizeof(buf), "%u min %02u s", (unsigned)min, (unsigned)sec);
  return String(buf);
}

static String jbc_usb_core_detail_text(const DisplayModuleDetail& m) {
  const DisplayJbcUsbCore& c = m.jbc_usb_core;
  if (!c.valid) return String(tr("JBC USB core telemetry is loading", "JBC-USB-Core-Telemetrie wird geladen"));
  String values = String(tr("Station model: ", "Stationsmodell: ")) + (c.model[0] ? c.model : "-") + "  " + jbc_usb_core_family_text(c.family);
  values += "\nJBC: "; values += on_off((c.flags & 0x01U) != 0);
  values += "   WORK: "; values += on_off((c.flags & 0x02U) != 0);
  values += "   STAND: "; values += on_off((c.flags & 0x04U) != 0);
  values += "\n" + String(tr("Connect: ", "Modus: "));
  values += (c.flags & 0x10U) ? ((c.flags & 0x20U) ? "CONTROL" : "MONITOR") : "-";
  values += "   " + String(tr("Error: ", "Fehler: "));
  values += (c.flags & 0x08U) ? jbc_usb_core_station_error_text(c.station_error) : String("-");

  if (c.family == 1) { // SOLD
    for (uint8_t i = 0; i < c.port_count && i < 4; ++i) {
      const DisplayJbcUsbCorePort& port = c.ports[i];
      values += "\nP" + String((uint8_t)(i + 1U)) + "  " + (port.valid ? jbc_usb_core_state_text(port.state) : "OFFLINE") + "  " + jbc_usb_core_temp_pair(port.temp_c, port.set_temp_c);
      if (port.valid) {
        values += "\n  " + String(tr("Tool: ", "Werkzeug: ")) + jbc_usb_core_tool_text(c, port.tool);
        values += "   " + String(tr("Power: ", "Leistung: ")) + String(port.power_pct) + "%";
        values += "   " + String(tr("Error: ", "Fehler: ")) + jbc_usb_core_error_text(c.family, port.error);
      }
    }
  } else if (c.family == 2) { // HA
    for (uint8_t i = 0; i < c.port_count && i < 4; ++i) {
      const DisplayJbcUsbCorePort& port = c.ports[i];
      values += "\nP" + String((uint8_t)(i + 1U)) + "  " + (port.valid ? jbc_usb_core_state_text(port.state) : "OFFLINE") + "  " + jbc_usb_core_temp_pair(port.temp_c, port.set_temp_c);
      if (port.valid) {
        values += "\n  " + String(tr("Tool: ", "Werkzeug: ")) + (c.friendly_valid ? jbc_usb_core_tool_text(c, port.tool) : String("-"));
        values += "   " + String(tr("Flow: ", "Luftstrom: ")) + String(port.flow_pct) + "%";
        values += "   " + String(tr("Power: ", "Leistung: ")) + String(port.power_pct) + "%";
        values += "\n  " + String(tr("Error: ", "Fehler: ")) + jbc_usb_core_error_text(c.family, port.error);
        values += "   Time to stop: " + jbc_usb_core_tts_text(port.time_to_stop_s);
      }
    }
  } else if (c.family == 3) { // CL
    values += "\n" + String(tr("Cleaner: ", "Reiniger: ")) + jbc_usb_core_cleaner_mode_text(c.cl_mode);
    values += "   " + String(tr("Motors: ", "Motoren: ")) + ((c.cl_flags & 0x01U) ? on_off((c.cl_flags & 0x02U) != 0) : "-");
    values += "\n" + String(tr("Door: ", "T\303\274r: ")) + ((c.cl_flags & 0x04U) ? ((c.cl_flags & 0x08U) ? tr("open", "offen") : tr("closed", "zu")) : "-");
    values += "   " + String(tr("Error: ", "Fehler: ")) + (c.friendly_valid && c.ports[0].valid ? jbc_usb_core_error_text(c.family, c.ports[0].error) : String("-"));
  } else if (c.family == 4) { // PH
    for (uint8_t i = 0; i < 4; ++i) {
      if (c.ph_temp_c[i] == 0xFFFFU && c.ph_set_temp_c[i] == 0xFFFFU) continue;
      values += "\nTC" + String((uint8_t)(i + 1U)) + "  " + jbc_usb_core_temp_pair(c.ph_temp_c[i], c.ph_set_temp_c[i]);
    }
    values += "\n" + String(tr("Heater: ", "Heizung: ")) + ((c.ph_flags & 0x01U) ? on_off((c.ph_flags & 0x02U) != 0) : "-");
    values += "  P" + String(c.ph_heater_power_pct) + "%";
    if (c.ph_flags & 0x04U) values += "  Soll " + String(c.ph_selected_power_pct) + "%";
    if (c.ph_flags & 0x08U) values += "  Z" + String(c.ph_active_zones);
    if (c.ph_time_to_stop_s) values += "  Time to stop: " + jbc_usb_core_tts_text(c.ph_time_to_stop_s);
    values += "\n" + String(tr("Error: ", "Fehler: ")) + (c.friendly_valid && c.ports[0].valid ? jbc_usb_core_error_text(c.family, c.ports[0].error) : String("-"));
  } else if (c.family == 5) { // FE
    values += "\n" + String(tr("Suction: ", "Absaugung: ")) + jbc_usb_core_suction_text(c.fe_suction_level);
    values += "   " + String(tr("Continuous: ", "Dauer: ")) + ((c.flags & 0x40U) ? on_off((c.flags & 0x80U) != 0) : "-");
    values += "\nWORK: "; values += (c.fe_flags & 0x01U) ? on_off((c.fe_flags & 0x02U) != 0) : "-";
    values += "   STAND: "; values += (c.fe_flags & 0x04U) ? on_off((c.fe_flags & 0x08U) != 0) : "-";
    values += "   TTS raw " + String(c.fe_time_to_stop_work_raw) + "/" + String(c.fe_time_to_stop_stand_raw);
    values += "\n" + String(tr("Error: ", "Fehler: ")) + (c.friendly_valid && c.ports[0].valid ? jbc_usb_core_error_text(c.family, c.ports[0].error) : String("-"));
  } else if (c.family == 6) { // SF
    values += "\n" + String(tr("Feeding: ", "Zinnzufuhr: ")) + ((c.sf_flags & 0x01U) ? on_off((c.sf_flags & 0x02U) != 0) : "-");
    values += "   " + String(tr("Program: ", "Programm: ")) + String(c.sf_program);
    values += "\n" + String(tr("Speed: ", "Geschwindigkeit: "));
    values += (c.sf_flags & 0x10U) ? String(c.sf_speed_tenth_mm_s / 10.0f, 1) + " mm/s" : String("-");
    values += "   " + String(tr("Length: ", "L\303\244nge: "));
    values += (c.sf_flags & 0x20U) ? String(c.sf_length_tenth_mm / 10.0f, 1) + " mm" : String("-");
    values += "\n" + String(tr("Tool enabled: ", "Werkzeug aktiviert: ")); values += (c.sf_flags & 0x04U) ? on_off((c.sf_flags & 0x08U) != 0) : "-";
    values += "   " + String(tr("Error: ", "Fehler: ")) + (c.friendly_valid && c.ports[0].valid ? jbc_usb_core_error_text(c.family, c.ports[0].error) : String("-"));
  }
  return values;
}

static void lv_update_module_detail() {
  // selected_module/home caches are updated before this function is called.
  // Rendering the hidden detail widget tree is unnecessary.
  if (display_view_mode != DISPLAY_VIEW_MODULE_DETAIL &&
      (!ui_module_detail_screen || lv_screen_active() != ui_module_detail_screen)) {
    return;
  }
  if (!selected_module.valid) return;
  const DisplayModuleDetail& m = selected_module;
  const bool master_lost_now = master_link_lost();
  const String detail_type = m.addr == ADDR_MASTER ? String("Master") : detail_type_name(m.type, m.caps);
  lv_set_text(ui_detail_title, lv_addr_text(m.addr) + "  " + (m.name[0] ? String(m.name) : detail_type));
  String detail_meta = lv_addr_text(m.addr) + "  " + detail_type + "  FW " + module_fw_text(m.fw_major, m.fw_minor, m.fw_patch, m.fw_suffix);
  if (master_lost_now) detail_meta += String("  | ") + tr("cached", "Cache");
  if (bus_update_is_for_addr(m.addr)) detail_meta += String("  | ") + bus_update_progress_text(status.update_progress);
  lv_set_text(ui_detail_meta, detail_meta);

  const bool module_online = !master_lost_now && ((m.flags & 1) != 0);
  lv_set_text_c(ui_detail_status, master_lost_now ? tr("Master lost - cached data", "Master fehlt - Cache") : (module_online ? tr("Online", "Online") : tr("Offline", "Offline")));
  lv_obj_set_style_bg_color(ui_detail_status, master_lost_now
    ? ui_theme_color(0x332410, 0xFFF2D6)
    : (module_online ? ui_theme_color(0x123824, 0xE2F8EB) : ui_theme_color(0x3A2026, 0xFFE8EB)), 0);
  lv_obj_set_style_text_color(ui_detail_status, master_lost_now
    ? ui_theme_color(0xFFB020, 0x7A4B00)
    : (module_online ? ui_theme_color(0x2DFF88, 0x146B3A) : ui_theme_color(0xFF5B5B, 0x9D1D2F)), 0);

  lv_set_detail_work_icon(detail_fields_is_jbc(m.type, m.caps), m.jbc_work != 0);

  String values;
  if (m.addr == ADDR_MASTER) {
    values = String("IP: ") + (m.master_ip[0] ? m.master_ip : "-") +
      "\n" + tr("Uptime: ", "Laufzeit: ") + fmt_dhm((uint16_t)(m.uptime_s / 60UL)) +
      "\nHeap: " + String((m.heap_free + 512UL) / 1024UL) + " KB" +
      "\nCPU: " + String(m.cpu_load) + "%  Loop max: " + String(m.loop_max_ms) + " ms";
  } else if (detail_fields_is_jbc_usb(m.type, m.caps)) {
    values = jbc_usb_core_detail_text(m);
  } else if (detail_fields_is_jbc_fae(m.type, m.caps)) {
    values = String(tr("Station: ", "Station: ")) + station_name(m.station_addr) + "  " + lv_addr_text(m.jbc_addr) +
      "\nWork: " + on_off(m.jbc_work != 0) + "    Stand: " + on_off(m.jbc_stand != 0) +
      "\n" + tr("Mode: ", "Stufe: ") + suction_name(m.suction) + tr("    Power: ", "    Leistung: ") + String(m.select_flow / 10) + "%" +
      "\n" + tr("Afterrun: Work ", "Nachlauf: Work ") + String(m.delay_work) + "s   Stand " + String(m.delay_stand) + "s" +
      "\nDevice ID: " + jbc_device_id_text();
  } else if (detail_fields_is_universal(m.type, m.caps)) {
    const bool local_bus_online = ((m.output_fault | m.io_faults) & 0x0001U) == 0;
    values = String(tr("Bridge: ", "Bridge: ")) + (m.type == MODULE_MODBUS_RTU ? "Modbus RTU" : "RS232") +
      "\n" + tr("Device bus: ", "Ger\303\244tebus: ") + on_off(local_bus_online) +
      "\n" + tr("Profile entities: ", "Profil-Entities: ") + String(m.universal_entity_total ? m.universal_entity_total : m.universal_entity_count);
    if (!m.universal_entity_count) {
      values += "\n" + String((m.universal_entity_total || m.universal_descriptor_crc)
        ? tr("Profile entities are loading", "Profil-Entities werden geladen")
        : tr("No profile entities loaded", "Keine Profil-Entities geladen"));
    } else {
      for (uint8_t i = 0; i < m.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
        const DisplayUniversalEntity& e = m.universal_entities[i];
        if (!e.valid || !universal_entity_readable(e)) continue;
        values += "\n" + String(e.label[0] ? e.label : "Entity") + ": " + universal_entity_value_text(e);
      }
    }
    values += "\n" + String(tr("Fault: ", "Fehler: ")) + fault_name(m.output_fault | m.io_faults, m.type);
  } else if (detail_fields_is_fan_io(m.type, m.caps)) {
    const bool fan_active = m.output_enabled;
    const char* main_label = detail_alias_or(m.io_main_alias, tr("Relay/Fan", "Relais/L\303\274fter"));
    const char* in1_label = detail_alias_or(m.io_in1_alias, "IN1");
    const char* in2_label = detail_alias_or(m.io_in2_alias, "IN2");
    const char* out1_label = detail_alias_or(m.io_out1_alias, "OUT1");
    const char* out2_label = detail_alias_or(m.io_out2_alias, "OUT2");
    values = String(main_label) + ": " + on_off(fan_active) + "  " + String(m.output_power / 10) + "%" +
      "\nRPM: " + String(m.output_rpm) +
      "\n" + String(in1_label) + ": " + on_off((m.io_inputs & 0x01) != 0) + "   " + String(in2_label) + ": " + on_off((m.io_inputs & 0x02) != 0) +
      "\n" + String(out1_label) + ": " + on_off((m.io_outputs & 0x01) != 0) + "   " + String(out2_label) + ": " + on_off((m.io_outputs & 0x02) != 0) +
      "\n" + tr("Fault: ", "Fehler: ") + fault_name(m.output_fault | m.io_faults, m.type);
    if (m.caps & CAP_FILTER_SENSOR) {
      values += "\n" + String(tr("Filter: ", "Filter: ")) + pro_filter_text(m.filter_saturation_permille, m.filter_pressure_raw);
      values += "\n" + pro_calibration_text(m.filter_zero_raw, m.filter_clean_raw, m.filter_full_raw);
    }
  } else if (detail_fields_is_weller(m.type, m.caps)) {
    values = String(tr("Link: ", "Verbindung: ")) + on_off(m.weller_uart_age <= 10) + tr("   Fan: ", "   L\303\274fter: ") + on_off((m.io_outputs & 1) != 0) + tr("   Light: ", "   Licht: ") + on_off(m.weller_light != 0) +
      "\n" + tr("Speed: ", "Drehzahl: ") + String(m.weller_speed) + "%   RPM: " + String(m.weller_rpm) +
      "\n" + tr("Filter: ", "Filter: ") + filter_name(m.weller_filter) + "   SW: " + weller_sw_name(m.weller_version) +
      "\n" + tr("Filter runtime: ", "Filterlaufzeit: ") + fmt_dhm(m.weller_runtime) + " / " + fmt_dhm(m.weller_programmed) +
      "\n" + tr("Fault: ", "Fehler: ") + fault_name(m.output_fault | m.io_faults, m.type);
  } else if (m.type == MODULE_DISPLAY) {
    const uint8_t screen_mode = m.addr == module_addr ? display_view_mode : 0xFF;
    const char* screen = screen_mode == DISPLAY_VIEW_HOME ? "Home" :
      (screen_mode == DISPLAY_VIEW_MODULE_LIST ? tr("Modules", "Module") :
      (screen_mode == DISPLAY_VIEW_MODULE_DETAIL ? tr("Module details", "Moduldetails") :
      (screen_mode == DISPLAY_VIEW_ALARMS ? tr("Alarms", "Alarme") :
      (screen_mode == DISPLAY_VIEW_SYSTEM ? tr("Settings", "Einstellungen") : "-"))));
    values = String(tr("Screen: ", "Ansicht: ")) + screen;
    if (m.addr == module_addr) values += "\n" + String(tr("Brightness: ", "Helligkeit: ")) + String(display_brightness_pct) + "%";
  } else {
    values = String(tr("No module-specific telemetry", "Keine modulspezifische Telemetrie"));
  }

  values += lv_detail_system_text();
  lv_set_text(ui_detail_values, values);
  lv_build_detail_controls();

  if (ui_detail_jbc_mode_dropdown && !lv_dropdown_is_open(ui_detail_jbc_mode_dropdown)) {
    lv_dropdown_set_selected(ui_detail_jbc_mode_dropdown, m.suction > 3 ? 3 : m.suction);
  }
  if (ui_detail_jbc_power_input && ui_numeric_source != ui_detail_jbc_power_input) {
    lv_textarea_set_text(ui_detail_jbc_power_input, String(m.select_flow / 10U).c_str());
  }
  if (ui_detail_jbc_delay_work_input && ui_numeric_source != ui_detail_jbc_delay_work_input) {
    lv_textarea_set_text(ui_detail_jbc_delay_work_input, String(m.delay_work).c_str());
  }
  if (ui_detail_jbc_delay_stand_input && ui_numeric_source != ui_detail_jbc_delay_stand_input) {
    lv_textarea_set_text(ui_detail_jbc_delay_stand_input, String(m.delay_stand).c_str());
  }

  lv_set_toggle_style(ui_detail_stand_button, m.stand_intakes != 0);
  lv_set_toggle_style(ui_detail_continuous_button, m.continuous != 0);
  lv_set_toggle_style(ui_detail_fan_button, m.output_enabled);
  lv_set_toggle_style(ui_detail_out1_button, (m.io_outputs & 0x01) != 0);
  lv_set_toggle_style(ui_detail_out2_button, (m.io_outputs & 0x02) != 0);
  lv_set_toggle_style(ui_detail_weller_fan_button, (m.io_outputs & 0x01) != 0);
  lv_set_toggle_style(ui_detail_weller_light_button, m.weller_light != 0);

  if (weller_speed_pending && weller_speed_pending_addr == m.addr) {
    if (m.weller_speed == weller_speed_pending_value ||
        (uint32_t)(millis() - weller_speed_pending_ms) >= 4000UL) weller_speed_pending = false;
  }
  if (ui_detail_weller_speed_slider && !lv_obj_has_state(ui_detail_weller_speed_slider, LV_STATE_PRESSED)) {
    const uint8_t shown_speed = (weller_speed_pending && weller_speed_pending_addr == m.addr)
      ? weller_speed_pending_value : constrain(m.weller_speed, 30, 100);
    lv_slider_set_value(ui_detail_weller_speed_slider, shown_speed, LV_ANIM_OFF);
  }
  if (output_power_pending && output_power_pending_addr == m.addr) {
    const uint8_t reported_power = constrain(m.output_power / 10, 10, 100);
    if (reported_power == output_power_pending_value ||
        (uint32_t)(millis() - output_power_pending_ms) >= 4000UL) output_power_pending = false;
  }
  if (ui_detail_output_power_slider && !lv_obj_has_state(ui_detail_output_power_slider, LV_STATE_PRESSED)) {
    uint8_t shown_power = (output_power_pending && output_power_pending_addr == m.addr)
      ? output_power_pending_value : constrain(m.output_power / 10, 10, 100);
    lv_slider_set_value(ui_detail_output_power_slider, shown_power, LV_ANIM_OFF);
    if (ui_detail_output_power_value) lv_set_text(ui_detail_output_power_value, String(shown_power) + "%");
  }
  if (ui_detail_weller_filter_dropdown && !lv_dropdown_is_open(ui_detail_weller_filter_dropdown)) {
    lv_dropdown_set_selected(ui_detail_weller_filter_dropdown, weller_filter_preset_index(m.weller_programmed));
  }
  if (detail_fields_is_universal(m.type, m.caps)) {
    for (uint8_t i = 0; i < m.universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
      const DisplayUniversalEntity& e = m.universal_entities[i];
      if (!e.valid) continue;
      const bool readback_available = universal_entity_readable(e);
      if (readback_available) universal_pending_observe(m.addr, e);
      const int16_t actual_value = (e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BINARY_SENSOR) ? (e.value ? 1 : 0) : e.value;
      int16_t control_value = actual_value;
      const bool has_pending = universal_pending_value(m.addr, e.id, e.type, control_value);
      const int8_t slider_slot = universal_control_slot_for_id(ui_detail_universal_sliders, e.id);
      const int8_t switch_slot = universal_control_slot_for_id(ui_detail_universal_switches, e.id);
      const int8_t select_slot = universal_control_slot_for_id(ui_detail_universal_selects, e.id);
      const int8_t value_slot = slider_slot >= 0 ? slider_slot : (switch_slot >= 0 ? switch_slot : select_slot);
      if (slider_slot >= 0 && e.type == DISPLAY_UNI_NUMBER && !lv_obj_has_state(ui_detail_universal_sliders[(uint8_t)slider_slot], LV_STATE_PRESSED)) {
        int16_t min_v = e.min_value;
        int16_t max_v = e.max_value;
        if (max_v <= min_v) { min_v = 0; max_v = 100; }
        if (min_v < 0) min_v = 0;
        if (max_v > 255) max_v = 255;
        lv_slider_set_range(ui_detail_universal_sliders[(uint8_t)slider_slot], min_v, max_v);
        lv_slider_set_value(ui_detail_universal_sliders[(uint8_t)slider_slot], constrain(control_value, min_v, max_v), LV_ANIM_OFF);
      }
      if (switch_slot >= 0 && e.type == DISPLAY_UNI_SWITCH) {
        if (control_value) lv_obj_add_state(ui_detail_universal_switches[(uint8_t)switch_slot], LV_STATE_CHECKED);
        else lv_obj_clear_state(ui_detail_universal_switches[(uint8_t)switch_slot], LV_STATE_CHECKED);
      }
      if (select_slot >= 0 && e.type == DISPLAY_UNI_SELECT && !lv_dropdown_is_open(ui_detail_universal_selects[(uint8_t)select_slot])) {
        ui_detail_universal_select_refreshing = true;
        lv_dropdown_set_selected_if_changed(ui_detail_universal_selects[(uint8_t)select_slot], 0);
        ui_detail_universal_select_refreshing = false;
      }
      if (value_slot >= 0 && ui_detail_universal_values[(uint8_t)value_slot]) {
        if (e.type == DISPLAY_UNI_NUMBER) {
          const int16_t value_to_show = readback_available ? actual_value : control_value;
          String txt = (!readback_available && !has_pending) ? String("-") : String(value_to_show);
          if (txt != "-" && e.unit[0]) txt += String(" ") + e.unit;
          lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], txt);
        } else if (e.type == DISPLAY_UNI_SWITCH || e.type == DISPLAY_UNI_BINARY_SENSOR) {
          if (!readback_available && !has_pending) lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], "-");
          else lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], on_off((readback_available ? actual_value : control_value) != 0));
        } else if (e.type == DISPLAY_UNI_SELECT) {
          if (readback_available) lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], universal_entity_value_text(e));
          else if (has_pending) {
            String target = universal_select_option_text(e, control_value);
            lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], target.c_str());
          } else lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], "-");
        } else if (!has_pending) {
          lv_set_text(ui_detail_universal_values[(uint8_t)value_slot], universal_entity_value_text(e));
        }
      }
    }
  }
}

static void detail_request_watchdog_tick() {
  if (!lvgl_ready || fw_update_active || screensaver_active) return;
  if (lv_screen_active() != ui_module_detail_screen || !detail_requested_addr) return;
  display_view_mode = DISPLAY_VIEW_MODULE_DETAIL;
  display_view_arg = detail_requested_addr;
  const bool detail_loaded = selected_module.valid && selected_module.addr == detail_requested_addr;
  if (detail_loaded) return;
  const uint32_t now = millis();
  if ((uint32_t)(now - detail_open_ms) >= 1200UL &&
      (detail_status_msg_ms == 0 || (uint32_t)(now - detail_status_msg_ms) >= 1000UL)) {
    detail_status_msg_ms = now;
    lv_set_text_c(ui_detail_status, tr("Loading module data... retrying", "Moduldaten laden... erneut"));
  }
}

static void lv_set_visible(lv_obj_t* obj, bool visible);

static void lv_layout_home_cards(bool master_ok) {
  const bool show_jbc = master_ok && (status.jbc_present || module_count_by_group(MODULE_JBC_BUS, false));
  const bool show_fan = master_ok && (status.fan_present || module_count_by_group(MODULE_FAN_IO, false));
  const bool show_weller = master_ok && (status.weller_present || module_count_by_group(MODULE_WELLER_ZERO_SMOG, false));
  struct HomeCardVisibility {
    lv_obj_t* card;
    lv_obj_t* stripe;
    bool visible;
    int16_t height;
  } cards[] = {
    {ui_home_jbc_card, ui_home_jbc_stripe, show_jbc, 64},
    {ui_home_suction_card, ui_home_suction_stripe, show_fan, 112},
    {ui_home_weller_card, ui_home_weller_stripe, show_weller, 92},
    {ui_home_fault_card, ui_home_fault_stripe, true, 72},
  };
  int16_t y = 4;
  for (uint8_t i = 0; i < sizeof(cards) / sizeof(cards[0]); ++i) {
    lv_set_visible(cards[i].card, cards[i].visible);
    if (!cards[i].visible) continue;
    if (lv_obj_get_x(cards[i].card) != 392 || lv_obj_get_y(cards[i].card) != y) {
      lv_obj_set_pos(cards[i].card, 392, y);
    }
    if (lv_obj_get_width(cards[i].card) != 376 || lv_obj_get_height(cards[i].card) != cards[i].height) {
      lv_obj_set_size(cards[i].card, 376, cards[i].height);
    }
    if (cards[i].stripe && lv_obj_get_height(cards[i].stripe) != cards[i].height - 20) {
      lv_obj_set_size(cards[i].stripe, 5, cards[i].height - 20);
    }
    y += cards[i].height + 8;
  }
}
static void lv_alarm_add(uint8_t& index, const String& title, const String& detail, bool critical) {
  if (index >= 8 || !ui_alarm_rows[index]) return;
  lv_set_text(ui_alarm_titles[index], title);
  lv_set_text(ui_alarm_details[index], detail);
  lv_set_bg_color_if_changed(ui_alarm_rows[index], critical
    ? ui_theme_color(0x35161A, 0xFFE8EB)
    : ui_theme_color(0x332410, 0xFFF5D6), 0);
  lv_set_border_color_if_changed(ui_alarm_rows[index], critical
    ? lv_color_hex(0xFF4D5E) : lv_color_hex(0xFFB020), 0);
  lv_set_visible(ui_alarm_rows[index], true);
  index++;
}

static String master_alarm_source_text(uint8_t addr, uint8_t type) {
  if (addr == ADDR_MASTER) return tr("Master", "Master");
  const int8_t idx = module_summary_index_by_addr(addr);
  if (idx >= 0) {
    const DisplayModuleSummary& module = module_summaries[(uint8_t)idx];
    String name = module.name[0] ? String(module.name) : String(display_module_type_name(module.type));
    return lv_addr_text(module.addr) + " " + name;
  }
  return lv_addr_text(addr) + " " + String(display_module_type_name(type));
}

static String master_alarm_title_text(uint8_t addr, uint8_t type, uint8_t code) {
  String source = master_alarm_source_text(addr, type);
  switch (code) {
    case DISPLAY_ALARM_MODULE_OFFLINE:
    case DISPLAY_ALARM_OUTPUT_FAULT:
    case DISPLAY_ALARM_JBC_STATION:
    case DISPLAY_ALARM_WELLER_LINK:
      return source;
    case DISPLAY_ALARM_JBC_STATUS: return tr("JBC error sent", "JBC-Fehler gesendet");
    case DISPLAY_ALARM_NO_MAIN_INPUT: return tr("Main extractor input", "Haupteingang Absaugung");
    case DISPLAY_ALARM_NO_MAIN_OUTPUT: return tr("Main extractor output", "Hauptausgang Absaugung");
    default: return source;
  }
}

static String master_alarm_detail_text(uint8_t code, uint16_t value, uint8_t type) {
  switch (code) {
    case DISPLAY_ALARM_MODULE_OFFLINE: return tr("Module offline", "Modul offline");
    case DISPLAY_ALARM_OUTPUT_FAULT: return fault_name(value, type);
    case DISPLAY_ALARM_JBC_STATION: return tr("JBC station disconnected", "JBC-Station getrennt");
    case DISPLAY_ALARM_WELLER_LINK: return tr("Weller link disconnected", "Weller-Verbindung getrennt");
    case DISPLAY_ALARM_JBC_STATUS: return jbc_error_name(value);
    case DISPLAY_ALARM_NO_MAIN_INPUT: return tr("No main extractor input selected", "Kein Haupteingang Absaugung gew\303\244hlt");
    case DISPLAY_ALARM_NO_MAIN_OUTPUT: return tr("No main extractor output selected", "Kein Hauptausgang Absaugung gew\303\244hlt");
    default: return tr("Alarm reported by master", "Alarm vom Master gemeldet");
  }
}

static uint8_t lv_alarm_count_now() {
  if (!master_link_online()) return 1;
  if (status.master_alarm_valid) return status.master_alarm_count;
  return 0;
}

static uint8_t lv_update_alarm_center() {
  uint8_t count = 0;
  uint8_t reported_count = 0;
  if (!master_link_online()) {
    reported_count = 1;
    lv_alarm_add(count, tr("Master connection lost", "Master-Verbindung verloren"),
      tr("No current bus data", "Keine aktuellen Busdaten"), true);
  } else if (status.master_alarm_valid) {
    reported_count = status.master_alarm_count;
    for (uint8_t i = 0; i < status.master_alarm_item_count && i < 6 && count < 8; ++i) {
      lv_alarm_add(count,
        master_alarm_title_text(status.master_alarm_addr[i], status.master_alarm_type[i], status.master_alarm_code[i]),
        master_alarm_detail_text(status.master_alarm_code[i], status.master_alarm_value[i], status.master_alarm_type[i]),
        (status.master_alarm_critical_mask & (1U << i)) != 0);
    }
    if (reported_count > count && count < 8) {
      const uint8_t missing = reported_count - count;
      String detail = missing == 1
        ? String(tr("Waiting for alarm details", "Alarmdetails werden aktualisiert"))
        : (String("+") + String(missing) + tr(" more alarms", " weitere Alarme"));
      lv_alarm_add(count,
        tr("Alarms reported by master", "Alarme vom Master gemeldet"),
        detail,
        status.master_alarm_critical_mask != 0);
    }
  }
  if (reported_count == 0 && count == 0 && ui_alarm_rows[0]) {
    lv_set_text_c(ui_alarm_titles[0], tr("No active alarms", "Keine aktiven Alarme"));
    lv_set_text_c(ui_alarm_details[0], tr("All connected systems are operating normally", "Alle verbundenen Systeme arbeiten normal"));
    lv_set_bg_color_if_changed(ui_alarm_rows[0], ui_theme_color(0x102B22, 0xE5F7EC), 0);
    lv_set_border_color_if_changed(ui_alarm_rows[0], lv_color_hex(0x2DFF88), 0);
    lv_set_visible(ui_alarm_rows[0], true);
  }
  const uint8_t first_hidden = (reported_count == 0 && count == 0) ? 1 : count;
  for (uint8_t i = first_hidden; i < 8; ++i) lv_set_visible(ui_alarm_rows[i], false);
  active_alarm_count = reported_count ? reported_count : count;
  lv_update_alarm_header_ui();
  return active_alarm_count;
}

static void lv_update_app_values() {
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_APP_VALUES);
    return;
  }

  // IMPORTANT:
  // RS485 status/module data and Home module caches are always updated elsewhere.
  // Here we only decide which LVGL widget tree is worth touching.
  if (display_view_mode != DISPLAY_VIEW_HOME) {
    lv_update_clock_ui();

    if (display_view_mode == DISPLAY_VIEW_ALARMS) {
      // The Alarm page is visible, so keep its rows live.
      lv_update_alarm_center();
    } else {
      // Keep the shared alarm/header indication current without rebuilding the
      // hidden 8-row Alarm page or the hidden Home cards.
      active_alarm_count = lv_alarm_count_now();
      lv_update_alarm_header_ui();
    }
    return;
  }

  const bool master_ok = master_link_online();
  const bool master_lost_now = master_link_lost();
  uint8_t output_pct = 0;
  if (master_ok && status.output_enabled) {
    // Home/output cards display the Master-owned extractor output value.
    // Do not override this with Universal/Modbus entity-cache values because
    // those are asynchronous control/readback values and may lag or hold the
    // profile minimum/default.
    output_pct = constrain(status.output_power / 10, 0, 100);
  }
  const uint16_t fault_mask = status.module_output_fault | status.io_fault_mask;
  const uint8_t jbc_list_count = online_module_count_by_type(MODULE_JBC_BUS);
  const uint8_t fan_list_count = online_module_count_by_type(MODULE_FAN_IO);
  const uint8_t weller_list_count = online_module_count_by_type(MODULE_WELLER_ZERO_SMOG);

  // The Home page must be correct immediately after boot, even before the user
  // has ever opened the module-list page. DISPLAY_STATUS already tells us which
  // module families are present; module_summaries only refines the actual count.
  const uint8_t jbc_module_count =
    status.jbc_present ? (jbc_list_count ? jbc_list_count : 1U) : 0U;
  const uint8_t fan_module_count =
    status.fan_present ? (fan_list_count ? fan_list_count : 1U) : 0U;
  const uint8_t weller_module_count =
    status.weller_present ? (weller_list_count ? weller_list_count : 1U) : 0U;

  const bool fan_cache_ok = home_fan_io_cache_fresh();

  // DISPLAY_STATUS io_* belongs to the currently selected/main output. It is
  // valid for the Fan/IO Home card only when Fan/IO itself is that output.
  // Otherwise OUT1/OUT2 must come from the independent Fan/IO detail cache.
  const bool fan_main_output_live =
    status.fan_present && active_output_is_fan_io();

  const bool fan_io_state_known = fan_cache_ok || fan_main_output_live;
  const uint16_t home_fan_inputs = fan_cache_ok ? home_fan_io_cache.inputs :
    (fan_main_output_live ? status.io_input_mask : 0U);
  const uint16_t home_fan_outputs = fan_cache_ok ? home_fan_io_cache.outputs :
    (fan_main_output_live ? status.io_output_mask : 0U);
  const uint16_t home_fan_faults = fan_cache_ok ? home_fan_io_cache.faults :
    (fan_main_output_live ? status.io_fault_mask : 0U);
  const bool home_fan_enabled = fan_cache_ok ? home_fan_io_cache.output_enabled :
    (fan_main_output_live ? status.module_output_enabled : false);
  const uint16_t home_fan_power = fan_cache_ok ? home_fan_io_cache.output_power :
    (fan_main_output_live ? status.module_output_power : 0U);
  const uint16_t home_fan_rpm = fan_cache_ok ? home_fan_io_cache.output_rpm :
    (fan_main_output_live ? status.module_output_rpm : 0U);
  const uint16_t home_fan_fault = fan_cache_ok ? home_fan_io_cache.output_fault :
    (fan_main_output_live ? status.module_output_fault : 0U);

  const bool weller_cache_ok = home_weller_cache_fresh();
  const bool home_weller_connected = weller_cache_ok ? home_weller_cache.connected : status.weller_connected;
  const uint8_t home_weller_speed = weller_cache_ok ? home_weller_cache.speed : status.weller_speed;
  const uint8_t home_weller_filter_status = weller_cache_ok ? home_weller_cache.filter : status.weller_filter_status;
  const uint16_t home_weller_runtime_min = weller_cache_ok ? home_weller_cache.runtime : status.weller_filter_runtime_min;
  const uint16_t home_weller_programmed_min = weller_cache_ok ? home_weller_cache.programmed : status.weller_filter_programmed_min;
  const uint16_t home_weller_version = weller_cache_ok ? home_weller_cache.version : status.weller_version;
  const uint8_t home_weller_light = weller_cache_ok ? home_weller_cache.light : status.weller_light;
  const uint16_t home_weller_outputs = weller_cache_ok ? home_weller_cache.io_outputs : status.io_output_mask;

  lv_layout_home_cards(master_ok);
  if (ui_home_suction_title) lv_set_text(ui_home_suction_title, home_fan_io_title());

  if (!master_ok) {
    lv_set_text_c(ui_home_output, master_lost_now ? tr("Offline", "Offline") : tr("Connecting", "Verbinden"));
    lv_set_text(ui_home_power, "--");
    lv_set_text(ui_home_rpm, master_lost_now ? String("--") : tr("waiting", "warten"));
    lv_set_text(ui_home_afterrun, master_lost_now ? tr("Master missing", "Master fehlt") : tr("Waiting", "Warten"));
    if (ui_home_power_bar) lv_bar_set_value_if_changed(ui_home_power_bar, 0);

    lv_set_home_work_icon(status.jbc_present || jbc_module_count > 0, false);
    lv_set_text(ui_home_work, "-");
    lv_set_text(ui_home_jbc, master_lost_now ? String("Cache") : String("waiting"));
    lv_set_text(ui_home_suction, "-");
    lv_set_text(ui_home_fan_detail, tr("No current data", "Keine aktuellen Daten"));
    if (ui_home_mode_dropdown && !lv_dropdown_is_open(ui_home_mode_dropdown)) lv_dropdown_set_selected_if_changed(ui_home_mode_dropdown, 3);
    if (ui_home_power_input && ui_numeric_source != ui_home_power_input) lv_textarea_set_text_if_changed(ui_home_power_input, "-");
    if (ui_home_delay_input && ui_numeric_source != ui_home_delay_input) lv_textarea_set_text_if_changed(ui_home_delay_input, "-");
    lv_set_text(ui_home_weller, master_lost_now ? String("Cache") : String("waiting"));
    lv_set_text(ui_home_filter, "-");
    lv_set_text(ui_home_fault, master_lost_now ? tr("Master lost", "Master fehlt") : String("connecting"));
  } else {
    const bool module_offline_alarm = screensaver_has_alarm_code(DISPLAY_ALARM_MODULE_OFFLINE);
    const bool no_main_input = status.main_input_source_type == 0 || screensaver_has_alarm_code(DISPLAY_ALARM_NO_MAIN_INPUT);
    const bool not_ready = module_offline_alarm || no_main_input;
    lv_set_text_c(ui_home_output, status.output_enabled ? tr("Running", "L\303\244uft") : (not_ready ? tr("Not ready", "Nicht bereit") : tr("Idle", "Bereit")));
    lv_set_text(ui_home_power, String(output_pct) + "%");
    lv_set_text(ui_home_rpm, String(main_output_rpm_for_ui()) + " rpm");
    lv_set_text(ui_home_afterrun, status.afterrun_s ? (String(tr("Afterrun ", "Nachlauf ")) + String(status.afterrun_s) + "s") : (not_ready ? String(tr("Not ready", "Nicht bereit")) : String(tr("Ready", "Bereit"))));
    if (ui_home_power_bar) lv_bar_set_value_if_changed(ui_home_power_bar, output_pct);

    lv_set_home_work_icon(status.jbc_present || jbc_module_count > 0, status.work_mask != 0);
    String jbc_status_line = jbc_module_count
      ? (String(jbc_module_count) + tr(" RS485 online", " RS485 online"))
      : (status.jbc_present ? String(tr("JBC present", "JBC vorhanden")) : String("RS485 offline"));
    if (status.jbc_stat_error) jbc_status_line += String(" | ") + jbc_error_name(status.jbc_stat_error);
    else if (status.jbc_connected) jbc_status_line += " | OK";
    lv_set_text(ui_home_jbc, jbc_status_line);
    lv_set_text(ui_home_work, status.jbc_connected
      ? (String(tr("Station ", "Station ")) + home_jbc_station_models() + tr(" | Work ", " | Work ") + on_off(status.work_mask != 0))
      : (jbc_module_count ? String(tr("Station offline", "Station offline")) : String("-")));
    if (fan_module_count) lv_set_text(ui_home_suction, String(fan_module_count) + tr(" RS485 online", " RS485 online"));
    else lv_set_text(ui_home_suction, String("RS485 offline"));
    String fan_detail;
    if (!fan_module_count) {
      fan_detail = "-";
    } else {
      const char* main_label = detail_alias_or(home_fan_io_cache.io_main_alias, tr("Relay/Fan", "Relais/L\303\274fter"));
      const char* in1_label = detail_alias_or(home_fan_io_cache.io_in1_alias, "IN1");
      const char* in2_label = detail_alias_or(home_fan_io_cache.io_in2_alias, "IN2");
      const char* out1_label = detail_alias_or(home_fan_io_cache.io_out1_alias, "OUT1");
      const char* out2_label = detail_alias_or(home_fan_io_cache.io_out2_alias, "OUT2");
      const String main_state = fan_io_state_known ? String(on_off(home_fan_enabled)) : String("--");
      const String power_text = fan_io_state_known ? (String(home_fan_power / 10) + "%") : String("--%");
      const String rpm_text = fan_io_state_known ? String(home_fan_rpm) : String("--");
      const String out1_state = fan_io_state_known ? String(on_off((home_fan_outputs & 0x01) != 0)) : String("--");
      const String out2_state = fan_io_state_known ? String(on_off((home_fan_outputs & 0x02) != 0)) : String("--");
      const String in1_state = fan_io_state_known ? String(on_off((home_fan_inputs & 0x01) != 0)) : String("--");
      const String in2_state = fan_io_state_known ? String(on_off((home_fan_inputs & 0x02) != 0)) : String("--");

      fan_detail = (active_output_is_fan_io() ? String(tr("Main output | ", "Hauptausgang | ")) : String("")) +
        String(main_label) + " " + main_state +
        "   " + power_text + "   RPM " + rpm_text +
        "   " + String(out1_label) + ": " + out1_state +
        "   " + String(out2_label) + ": " + out2_state +
        "   " + String(in1_label) + ": " + in1_state +
        "   " + String(in2_label) + ": " + in2_state;
    }
    if (fan_module_count && fan_cache_ok && home_fan_io_cache.relay_style && (home_fan_io_cache.filter_saturation_permille || home_fan_io_cache.filter_pressure_raw)) {
      fan_detail += "   |   " + String(tr("Filter ", "Filter ")) + String((home_fan_io_cache.filter_saturation_permille + 5) / 10) + "%";
    }
    lv_set_text(ui_home_fan_detail, fan_detail);
    if (ui_home_mode_dropdown && !lv_dropdown_is_open(ui_home_mode_dropdown)) {
      lv_dropdown_set_selected_if_changed(ui_home_mode_dropdown, status.suction_level > 3 ? 3 : status.suction_level);
    }
    if (ui_home_power_input && ui_numeric_source != ui_home_power_input) {
      lv_textarea_set_text_if_changed(ui_home_power_input, String(status.select_flow / 10).c_str());
    }
    if (ui_home_delay_input && ui_numeric_source != ui_home_delay_input) {
      lv_textarea_set_text_if_changed(ui_home_delay_input, String(status.delay_work_s).c_str());
    }
    if (ui_home_afterrun_power_input && ui_numeric_source != ui_home_afterrun_power_input) {
      lv_textarea_set_text_if_changed(ui_home_afterrun_power_input, String(status.afterrun_power / 10U).c_str());
    }
    if (ui_home_afterrun_power_button) lv_set_toggle_style(ui_home_afterrun_power_button, status.afterrun_power_enabled);
    const bool weller_seen = status.weller_present;
    const bool weller_has_values = status.weller_present &&
      (home_weller_speed || home_weller_filter_status ||
       home_weller_runtime_min || home_weller_programmed_min ||
       home_weller_light || home_weller_version || weller_cache_ok);
    if (!status.weller_present) {
      lv_set_text(ui_home_weller, String("RS485 offline"));
      lv_set_text(ui_home_filter, "-");
    } else if (home_weller_connected || weller_has_values) {
      lv_set_text(ui_home_weller, String(weller_module_count ? weller_module_count : 1) +
        (home_weller_connected ? tr(" RS485 online", " RS485 online") : tr(" cached", " Cache")));
      lv_set_text(ui_home_filter, (active_output_is_weller() ? String(tr("Main output | ", "Hauptausgang | ")) : String("")) +
        String(tr("Fan ", "L\303\274fter ")) + on_off((home_weller_outputs & 0x01) != 0) +
        "   " + String(home_weller_speed) + "%   RPM " + String(home_weller_cache.rpm) +
        "\n" + String(tr("Filter ", "Filter ")) +
        fmt_dhm(home_weller_runtime_min) + " / " + fmt_dhm(home_weller_programmed_min) +
        tr("   Light ", "   Licht ") + on_off(home_weller_light != 0));
    } else if (weller_seen) {
      lv_set_text(ui_home_weller, String(weller_module_count ? weller_module_count : 1) + tr(" module online | UART offline", " Modul online | UART offline"));
      lv_set_text(ui_home_filter, tr("No Weller values yet", "Noch keine Weller-Werte"));
    } else {
      // Module exists on RS485, but no usable UART/value packet has arrived yet.
      lv_set_text(ui_home_weller, String(weller_module_count ? weller_module_count : 1) +
        tr(" module online | UART offline", " Modul online | UART offline"));
      lv_set_text(ui_home_filter, tr("No Weller values yet", "Noch keine Weller-Werte"));
    }
  }

  active_alarm_count = lv_alarm_count_now();
  lv_update_alarm_header_ui();
  lv_set_text(ui_home_fault, active_alarm_count
    ? (String(active_alarm_count) + tr(" active", " aktiv"))
    : String(tr("No alarms", "Keine Alarme")));
  lv_set_text(ui_home_modules, active_alarm_count
    ? String(tr("Tap for alarm details", "F\303\274r Alarmdetails antippen"))
    : (String(status.modules_count) + tr(" modules online", " Module online")));
  lv_set_disabled_if_changed(ui_home_power_input, !(master_ok && status.suction_level == 3));
  lv_set_disabled_if_changed(ui_home_delay_input, !master_ok);
  lv_set_disabled_if_changed(ui_home_mode_dropdown, !master_ok);
  lv_refresh_input_dropdown();
  lv_set_disabled_if_changed(ui_home_input_dropdown, !(master_ok && home_input_count > 0));
  lv_refresh_output_dropdown();
  lv_set_disabled_if_changed(ui_home_output_dropdown, !(master_ok && home_output_count > 0));
  lv_update_clock_ui();
  lv_set_toggle_style(ui_home_continuous_button, master_ok && status.continuous != 0);
  const bool weller_is_output = master_ok && active_output_is_weller();
  const bool fan_io_is_output = master_ok && active_output_is_fan_io();
  lv_set_toggle_style(ui_home_fan_button, weller_is_output && home_weller_connected && (home_weller_outputs & 0x01) != 0);
  lv_set_toggle_style(ui_home_light_button, weller_is_output && home_weller_light != 0);
  lv_set_toggle_style(ui_home_io1_button, fan_io_is_output && (home_fan_outputs & 0x01) != 0);
  lv_set_toggle_style(ui_home_io2_button, fan_io_is_output && (home_fan_outputs & 0x02) != 0);
  if (ui_home_fan_button) lv_set_visible(ui_home_fan_button, weller_is_output);
  if (ui_home_light_button) lv_set_visible(ui_home_light_button, weller_is_output);
  if (ui_home_io1_button) lv_set_visible(ui_home_io1_button, fan_io_is_output);
  if (ui_home_io2_button) lv_set_visible(ui_home_io2_button, fan_io_is_output);
  // No extra Fan/IO controls on the start page.
  // Buttons are selected by the configured main output above; power remains in Extraction settings.

  if (ui_home_output) lv_set_text_color_if_changed(ui_home_output, !master_ok ? lv_color_hex(0xFF8A98) : (status.output_enabled ? lv_color_hex(0x2DFF88) : ui_theme_color(0xFFFFFF, 0x17212B)), 0);
  if (ui_home_power) lv_set_text_color_if_changed(ui_home_power, !master_ok ? lv_color_hex(0xFF8A98) : (status.output_enabled ? lv_color_hex(0x2DFF88) : ui_theme_color(0x8FA1B3, 0x5B6875)), 0);
  if (ui_home_output_card) {
    lv_set_bg_color_if_changed(ui_home_output_card, !master_ok ? ui_theme_color(0x35161A, 0xFFE8EB) : (status.output_enabled ? ui_theme_color(0x103125, 0xE5F7EC) : ui_theme_color(0x151B23, 0xF7FAFC)), 0);
    lv_set_border_color_if_changed(ui_home_output_card, !master_ok ? lv_color_hex(0xFF4D5E) : (status.output_enabled ? lv_color_hex(0x2DFF88) : ui_theme_color(0x2C3B4A, 0xC4D0DB)), 0);
  }
  if (ui_home_work_card) lv_set_bg_color_if_changed(ui_home_work_card, master_ok && status.work_mask ? ui_theme_color(0x332410, 0xFFF5D6) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_home_jbc_card) lv_set_bg_color_if_changed(ui_home_jbc_card, master_ok && (status.jbc_connected || jbc_module_count) ? ui_theme_color(0x102B22, 0xE5F7EC) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_home_suction_card) lv_set_bg_color_if_changed(ui_home_suction_card, master_ok && fan_module_count ? ui_theme_color(0x102338, 0xE7F1FF) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_home_weller_card) lv_set_bg_color_if_changed(ui_home_weller_card, master_ok && (status.weller_connected || weller_module_count || status.weller_present) ? ui_theme_color(0x102338, 0xE7F1FF) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_home_filter_card) lv_set_bg_color_if_changed(ui_home_filter_card, master_ok && status.weller_filter_status == 100 ? ui_theme_color(0x331818, 0xFFE8EB) : (master_ok && status.weller_filter_status == 10 ? ui_theme_color(0x332410, 0xFFF5D6) : ui_theme_color(0x151B23, 0xF7FAFC)), 0);
  if (ui_home_fault_card) {
    lv_set_bg_color_if_changed(ui_home_fault_card, active_alarm_count
      ? ui_theme_color(0x331818, 0xFFE8EB) : ui_theme_color(0x102B22, 0xE5F7EC), 0);
    lv_set_border_color_if_changed(ui_home_fault_card, active_alarm_count
      ? lv_color_hex(0xFF4D5E) : lv_color_hex(0x2DFF88), 0);
  }
  if (display_view_mode == DISPLAY_VIEW_HOME) {
    last_drawn_status = status;
    have_drawn_status = true;
  }
}

static void lv_set_visible(lv_obj_t* obj, bool visible) {
  if (!obj) return;
  const bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  if (visible) {
    if (hidden) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (!hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static void lv_update_dashboard_values() {
  lv_set_visible(ui_jbc_card, status.jbc_present);
  lv_set_visible(ui_weller_card, status.weller_present);
  lv_set_visible(ui_fan_card, status.fan_present || status.weller_present);
  lv_set_visible(ui_output_addr_card, status.output_present);
  lv_set_visible(ui_page_tabs[1], status.jbc_present);
  lv_set_visible(ui_page_tabs[2], status.fan_present);
  lv_set_visible(ui_page_tabs[3], status.weller_present);
  lv_set_visible(ui_page_dots[1], status.jbc_present);
  lv_set_visible(ui_page_dots[2], status.fan_present);
  lv_set_visible(ui_page_dots[3], status.weller_present);
  if ((display_page == 1 && !status.jbc_present) ||
      (display_page == 2 && !status.fan_present) ||
      (display_page == 3 && !status.weller_present)) {
    lv_dashboard_set_page(0);
  }
  const bool tab_visible[5] = {true, status.jbc_present, status.fan_present, status.weller_present, true};
  uint8_t tab_count = 0;
  for (uint8_t i = 0; i < 5; ++i) if (tab_visible[i]) tab_count++;
  const int16_t tab_gap = 6;
  const int16_t tab_width = (464 - (tab_count - 1) * tab_gap) / tab_count;
  int16_t tab_x = 8;
  for (uint8_t i = 0; i < 5; ++i) {
    if (!tab_visible[i]) continue;
    if (lv_obj_get_x(ui_page_tabs[i]) != tab_x || lv_obj_get_width(ui_page_tabs[i]) != tab_width) {
      lv_obj_set_pos(ui_page_tabs[i], tab_x, 274);
      lv_obj_set_width(ui_page_tabs[i], tab_width);
    }
    tab_x += tab_width + tab_gap;
  }

  const bool fan_cache_ok = home_fan_io_cache_fresh();
  const uint16_t home_fan_inputs = fan_cache_ok ? home_fan_io_cache.inputs : status.io_input_mask;
  const uint16_t home_fan_outputs = fan_cache_ok ? home_fan_io_cache.outputs : status.io_output_mask;
  const bool home_fan_enabled = fan_cache_ok ?
    home_fan_io_cache.output_enabled :
    status.module_output_enabled;
  const uint16_t home_fan_power = fan_cache_ok ? home_fan_io_cache.output_power : status.module_output_power;
  const uint16_t home_fan_rpm = fan_cache_ok ? home_fan_io_cache.output_rpm : status.module_output_rpm;
  const uint16_t home_fan_fault = fan_cache_ok ? home_fan_io_cache.output_fault : status.module_output_fault;

  const bool weller_cache_ok = home_weller_cache_fresh();
  const bool home_weller_connected = weller_cache_ok ? home_weller_cache.connected : status.weller_connected;
  const uint8_t home_weller_speed = weller_cache_ok ? home_weller_cache.speed : status.weller_speed;
  const uint8_t home_weller_filter_status = weller_cache_ok ? home_weller_cache.filter : status.weller_filter_status;
  const uint16_t home_weller_runtime_min = weller_cache_ok ? home_weller_cache.runtime : status.weller_filter_runtime_min;
  const uint16_t home_weller_programmed_min = weller_cache_ok ? home_weller_cache.programmed : status.weller_filter_programmed_min;
  const uint16_t home_weller_version = weller_cache_ok ? home_weller_cache.version : status.weller_version;
  const uint8_t home_weller_light = weller_cache_ok ? home_weller_cache.light : status.weller_light;
  const uint16_t home_weller_outputs = weller_cache_ok ? home_weller_cache.io_outputs : status.io_output_mask;

  uint8_t output_pct = 0;
  if (master_link_online() && status.output_enabled) {
    // Dashboard uses the same Master-owned value as the Home card.
    // Entity-cache values remain reserved for Universal/Modbus detail controls.
    output_pct = constrain(status.output_power / 10, 0, 100);
  }

  lv_set_text_c(ui_output_state, status.output_enabled ? "Running" : "Idle");
  if (ui_output_state) lv_obj_set_style_text_color(ui_output_state, status.output_enabled ? lv_color_hex(0x2DFF88) : ui_theme_color(0xF7FAFF, 0x17212B), 0);
  if (ui_output_card) {
    lv_obj_set_style_bg_color(ui_output_card, status.output_enabled ? ui_theme_color(0x103125, 0xE5F7EC) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
    lv_obj_set_style_border_color(ui_output_card, status.output_enabled ? lv_color_hex(0x2DFF88) : ui_theme_color(0x2C3B4A, 0xC4D0DB), 0);
  }
  if (ui_jbc_card) lv_obj_set_style_bg_color(ui_jbc_card, status.jbc_connected ? ui_theme_color(0x102B22, 0xE5F7EC) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_weller_card) lv_obj_set_style_bg_color(ui_weller_card, status.weller_connected ? ui_theme_color(0x102338, 0xE7F1FF) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_work_card) lv_obj_set_style_bg_color(ui_work_card, status.work_mask ? ui_theme_color(0x332410, 0xFFF5D6) : ui_theme_color(0x151B23, 0xF7FAFC), 0);
  if (ui_weller_filter_card) lv_obj_set_style_bg_color(ui_weller_filter_card, status.weller_filter_status == 100 ? ui_theme_color(0x331818, 0xFFE8EB) : (status.weller_filter_status == 10 ? ui_theme_color(0x332410, 0xFFF5D6) : ui_theme_color(0x151B23, 0xF7FAFC)), 0);
  if (ui_output_bar) lv_bar_set_value_if_changed(ui_output_bar, output_pct);
  lv_set_text(ui_output_power, String("Power ") + String(output_pct) + "%");
  lv_set_text(ui_afterrun, String("After ") + String(status.afterrun_s) + "s");
  lv_set_text(ui_jbc_state, status.jbc_connected ? (String(status.jbc_inputs) + " linked") : String("Offline"));
  lv_set_text_c(ui_weller_state, home_weller_connected ? "Linked" : "Offline");
  lv_set_text(ui_work_mask, status.work_mask ? (String("0x") + String(status.work_mask, HEX)) : "idle");
  lv_set_text(ui_fan_rpm, String(status.fan_rpm) + " rpm");
  lv_set_text(ui_modules, String(status.modules_count));
  lv_set_text(ui_addr, lv_addr_text(module_addr));
  lv_set_text(ui_touch, String(FW_MAJOR) + "." + String(FW_MINOR) + "." + String(FW_PATCH) + String(FW_SUFFIX));
  lv_set_text(ui_touch_pos, String(last_touch_x) + "," + String(last_touch_y));
  lv_set_text(ui_brightness, String(display_brightness_pct) + "%");
  if (ui_brightness_bar) lv_bar_set_value_if_changed(ui_brightness_bar, display_brightness_pct);
  lv_set_text(ui_suction, suction_name(status.suction_level));
  lv_set_text(ui_custom_power, String(status.select_flow / 10) + "%");
  lv_set_text(ui_delay, String(status.delay_work_s) + "s");
  lv_set_text(ui_delay_stand, String(status.delay_stand_s) + "s");
  lv_set_text_c(ui_stand_intakes, status.stand_intakes ? "on" : "off");
  lv_set_text(ui_output_addr, status.output_addr ? lv_addr_text(status.output_addr) : String("-"));
  lv_set_text(ui_station, status.jbc_addr ? (String(station_name(status.station_addr)) + " " + lv_addr_text(status.jbc_addr)) : String("-"));
  lv_set_text_c(ui_continuous, status.continuous ? "on" : "off");
  lv_set_text(ui_jbc_detail, String("W") + String(status.jbc_work_mask, HEX) + " S" + String(status.jbc_stand_mask, HEX) + (status.continuous ? " C" : ""));
  lv_set_text_c(ui_fan_detail_output, home_fan_enabled ? "on" : "off");
  lv_set_text(ui_fan_detail_power, String(home_fan_power / 10) + "%");
  lv_set_text(ui_fan_detail_rpm, String(home_fan_rpm) + " rpm");
  lv_set_text(ui_fan_detail_inputs, String(detail_alias_or(home_fan_io_cache.io_in1_alias, "IN1")) + ": " + ((home_fan_inputs & 1) ? "on" : "off") + "   " + String(detail_alias_or(home_fan_io_cache.io_in2_alias, "IN2")) + ": " + ((home_fan_inputs & 2) ? "on" : "off"));
  lv_set_text(ui_fan_detail_outputs, String(detail_alias_or(home_fan_io_cache.io_out1_alias, "OUT1")) + ": " + ((home_fan_outputs & 0x01) ? "on" : "off") + "   " + String(detail_alias_or(home_fan_io_cache.io_out2_alias, "OUT2")) + ": " + ((home_fan_outputs & 0x02) ? "on" : "off"));
  lv_set_text(ui_fan_detail_fault, fault_name(home_fan_fault));
  lv_set_text_c(ui_weller_detail_link, home_weller_connected ? "online" : "offline");
  lv_set_text(ui_weller_detail_speed, String(home_weller_speed) + "%");
  lv_set_text(ui_weller_detail_rpm, String(weller_cache_ok ? home_weller_cache.rpm : status.fan_rpm) + " rpm");
  lv_set_text_c(ui_weller_detail_fan, (home_weller_outputs & 1) ? "on" : "off");
  lv_set_text_c(ui_weller_detail_light, home_weller_light ? "on" : "off");
  lv_set_text_c(ui_weller_detail_filter, filter_name(home_weller_filter_status));
  lv_set_text(ui_weller_detail_runtime, fmt_dhm(home_weller_runtime_min) + "/" + fmt_dhm(home_weller_programmed_min));
  lv_set_text(ui_weller_detail_sw, weller_sw_name(home_weller_version));
  lv_set_text(ui_heap, fmt_bytes(ESP.getFreeHeap()));
  lv_set_text(ui_uptime, fmt_uptime(millis() / 1000UL));
  lv_set_text(ui_loop,
    String("CPU ") + String(cpu_load_pct) + "%  " +
    String(perf_fps_x10 / 10) + "." + String(perf_fps_x10 % 10) + " fps  " +
    String("LV ") + String(perf_handler_max_x10_ms / 10) + "." +
    String(perf_handler_max_x10_ms % 10) + "ms");
}

static void log_internal_ram(const char* label) {
  Serial.printf("%s internal RAM: free %u bytes, largest %u bytes\n",
    label,
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}
static const char* psram_mode_text() {
#if defined(CONFIG_SPIRAM_MODE_OCT)
  return "OPI/octal";
#elif defined(CONFIG_SPIRAM_MODE_QUAD)
  return "QPI/quad";
#else
  return "unknown";
#endif
}

static const char* psram_speed_text() {
#if defined(CONFIG_SPIRAM_SPEED_120M)
  return "120 MHz";
#elif defined(CONFIG_SPIRAM_SPEED_80M)
  return "80 MHz";
#elif defined(CONFIG_SPIRAM_SPEED_40M)
  return "40 MHz";
#else
  return "unknown";
#endif
}
static void log_memory_setup() {
  system_psram_available = psramFound();
  system_psram_total = ESP.getPsramSize();
  system_psram_free = ESP.getFreePsram();
  Serial.printf("Internal RAM free: %u bytes, largest block: %u bytes\n",
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (system_psram_available && system_psram_total > 0) {
    Serial.printf("PSRAM available: %u bytes total, %u bytes free, mode %s, speed %s\n",
      (unsigned)system_psram_total, (unsigned)system_psram_free, psram_mode_text(), psram_speed_text());
  } else {
    Serial.println("PSRAM not available - enable OPI PSRAM in the board settings if this is an N8R16 module");
  }
}


static void reserve_lvgl_draw_buffer_early(uint32_t screen_w) {
#if DISPLAY_LVGL_FULL_REFRESH
  (void)screen_w;
  return;
#else
  if (lvgl_draw_buf || !screen_w) return;

  const uint16_t lines_try[] = {
    LVGL_DRAW_BUFFER_LINES_FAST, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8
  };

  for (uint8_t i = 0; i < sizeof(lines_try) / sizeof(lines_try[0]); ++i) {
    const uint32_t pixels_try = screen_w * lines_try[i];
    const uint32_t bytes_try = pixels_try * (uint32_t)sizeof(uint16_t);
    uint8_t* draw_try = ofe_display_memory::allocateDraw(bytes_try, display_wifi.drawBufferReserve());
    if (!draw_try) continue;

    lvgl_draw_buf = draw_try;
    lvgl_buf_pixels = pixels_try;
    lvgl_buf_bytes = bytes_try;
    lvgl_draw_buf_psram = false;
    Serial.printf("Early LVGL SRAM reservation: %u lines / %u bytes, largest internal block now %u bytes\n",
                  (unsigned)lines_try[i], (unsigned)bytes_try,
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    break;
  }
#endif
}

static void prepare_large_lvgl_tiles(uint32_t screen_w) {
#if DISPLAY_LVGL_LARGE_TILES && !DISPLAY_LVGL_FULL_REFRESH && DISPLAY_ROTATION == 2
  if (!screen_w || !lvgl_draw_buf || lvgl_draw_buf_psram || lvgl_transfer_scratch) return;
  constexpr uint32_t render_lines = 96;
  const uint32_t render_pixels = screen_w * render_lines;
  const uint32_t render_bytes = render_pixels * sizeof(uint16_t);
  uint8_t* render = (uint8_t*)heap_caps_aligned_alloc(
    64, render_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (render) {
    // Reuse the SRAM allocation for transfer; no extra internal RAM needed.
    lvgl_transfer_scratch = reinterpret_cast<uint16_t*>(lvgl_draw_buf);
    lvgl_transfer_scratch_pixels = lvgl_buf_pixels;
    lvgl_draw_buf = render;
    lvgl_buf_pixels = render_pixels;
    lvgl_buf_bytes = render_bytes;
    lvgl_draw_buf_psram = true;
  } else {
    Serial.println("Large LVGL tile unavailable; retaining internal render buffer");
  }
#else
  (void)screen_w;
#endif
}

static void lvgl_init_ui() {
  lv_init();
  // Keep the hot LVGL pool in SRAM and add capacity without taking RAM from RGB/WiFi.
  // TLSF limits each added pool to LV_MEM_SIZE with our no-expansion config.
  static void* widget_pools[2] = {};
  constexpr size_t pool_bytes = LV_MEM_SIZE;
  for (size_t i = 0; i < 2; ++i) {
    if (widget_pools[i]) continue;
    void* candidate = heap_caps_aligned_alloc(64, pool_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!candidate) {
      Serial.printf("LVGL PSRAM allocation failed: pool %u, %u bytes\n", (unsigned)i, (unsigned)pool_bytes);
      return;
    }
    if (!lv_mem_add_pool(candidate, pool_bytes)) {
      heap_caps_free(candidate);
      Serial.printf("LVGL TLSF rejected pool %u, %u bytes\n", (unsigned)i, (unsigned)pool_bytes);
      return;
    }
    widget_pools[i] = candidate;
  }
  Serial.printf("LVGL widget pools: %u bytes internal + 2 x %u bytes PSRAM\n", (unsigned)LV_MEM_SIZE, (unsigned)pool_bytes);
  lv_tick_set_cb(lvgl_millis_cb);
  const uint32_t screen_w = gfx->width();
  const uint32_t screen_h = gfx->height();
  const uint16_t draw_buf_lines_try[] = {
#if DISPLAY_LVGL_FULL_REFRESH
    (uint16_t)screen_h
#else
    LVGL_DRAW_BUFFER_LINES_FAST, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8
#endif
  };

#if DISPLAY_LVGL_FULL_REFRESH
  // Diagnostic mode only.
  lvgl_buf_pixels = screen_w * screen_h;
  lvgl_buf_bytes = lvgl_buf_pixels * (uint32_t)sizeof(uint16_t);
  lvgl_draw_buf = (uint8_t*)heap_caps_malloc(lvgl_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lvgl_draw_buf_psram = lvgl_draw_buf != nullptr;
#else
  // Prefer the early reservation made immediately after gfx->begin(). If that
  // was not possible, fall back to allocating here.
  if (!lvgl_draw_buf) {
    for (uint8_t i = 0; i < sizeof(draw_buf_lines_try) / sizeof(draw_buf_lines_try[0]); ++i) {
      const uint32_t pixels_try = screen_w * draw_buf_lines_try[i];
      const uint32_t bytes_try = pixels_try * (uint32_t)sizeof(uint16_t);

      uint8_t* draw_try = ofe_display_memory::allocateDraw(bytes_try, display_wifi.drawBufferReserve());

      if (!draw_try) continue;

      lvgl_draw_buf = draw_try;
      lvgl_buf_pixels = pixels_try;
      lvgl_buf_bytes = bytes_try;
      lvgl_draw_buf_psram = false;
      break;
    }
  }
#endif

#if LVGL_DRAW_BUFFER_ALLOW_PSRAM_FALLBACK && !DISPLAY_LVGL_FULL_REFRESH
  if (!lvgl_draw_buf) {
    // LVGL tiles are CPU-read, unlike the LCD bounce buffers which remain in SRAM.
    lvgl_buf_pixels = screen_w * 16U;
    lvgl_buf_bytes = lvgl_buf_pixels * sizeof(uint16_t);
    lvgl_draw_buf = (uint8_t*)heap_caps_malloc(lvgl_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lvgl_draw_buf_psram = lvgl_draw_buf != nullptr;
  }
#endif
  if (!lvgl_draw_buf) {
    Serial.println("LVGL draw buffer allocation failed");
    return;
  }

  prepare_large_lvgl_tiles(screen_w);

  const uint32_t selected_lines = screen_w ? (lvgl_buf_pixels / screen_w) : 0;
  Serial.printf("LVGL draw buffer: %u lines, %u pixels / %u bytes in %s, 64B aligned\n",
                (unsigned)selected_lines,
                (unsigned)lvgl_buf_pixels,
                (unsigned)lvgl_buf_bytes,
                lvgl_draw_buf_psram ? "PSRAM" : "internal RAM");
  if (lvgl_transfer_scratch) {
    Serial.printf("LVGL 180deg rotation: STAGED via %u internal bytes; RGB buffers unchanged\n",
                  (unsigned)(lvgl_transfer_scratch_pixels * sizeof(uint16_t)));
  } else {
    Serial.println("LVGL 180deg rotation: IN-PLACE (no SRAM scratch buffer)");
  }

  lvgl_disp = lv_display_create(screen_w, screen_h);
  lv_display_set_color_format(lvgl_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
  lv_display_set_buffers(lvgl_disp, lvgl_draw_buf, NULL, lvgl_buf_bytes, DISPLAY_LVGL_FULL_REFRESH ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL);

  // IMPORTANT: lv_timer_handler() can run every 5 ms while LVGL still keeps its
  // own display refresh timer at the library default (typically much slower).
  // Tune the real refresh timer explicitly.
  lv_timer_t* refr_timer = lv_display_get_refr_timer(lvgl_disp);
  if (refr_timer) lv_timer_set_period(refr_timer, DISPLAY_LVGL_REFRESH_PERIOD_MS);

  // User requirement: no visual quality loss. Keep LVGL anti-aliasing enabled.
  lv_display_set_antialiasing(lvgl_disp, true);

  lv_display_set_theme(lvgl_disp, lv_theme_default_init(lvgl_disp, lv_color_hex(0x246BFF), lv_color_hex(0x2DCC88), display_theme == 0, UI_FONT_DEFAULT));

  lv_indev_t* indev = lv_indev_create();
  lvgl_touch_indev = indev;
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_touch_cb);
  lv_indev_set_display(indev, lvgl_disp);

  // The input device owns a separate timer. Without this, touch/drag sampling
  // stays at LVGL's default period regardless of our 5-ms loop handler cadence.
  lv_timer_t* indev_timer = lv_indev_get_read_timer(indev);
  if (indev_timer) lv_timer_set_period(indev_timer, DISPLAY_TOUCH_READ_PERIOD_MS);
  lv_indev_set_scroll_limit(indev, DISPLAY_SCROLL_LIMIT_PX);
  lv_indev_set_scroll_throw(indev, DISPLAY_SCROLL_THROW_PCT);

  Serial.printf("LVGL timing: refresh=%u ms, touch=%u ms, scroll_limit=%u px, throw=%u%%, momentum=ON\n",
                (unsigned)DISPLAY_LVGL_REFRESH_PERIOD_MS,
                (unsigned)DISPLAY_TOUCH_READ_PERIOD_MS,
                (unsigned)DISPLAY_SCROLL_LIMIT_PX,
                (unsigned)DISPLAY_SCROLL_THROW_PCT);
  Serial.printf("Live status UI cadence: max %u Hz (%u ms coalescing)\n",
                (unsigned)(1000U / DISPLAY_STATUS_UI_MIN_INTERVAL_MS),
                (unsigned)DISPLAY_STATUS_UI_MIN_INTERVAL_MS);
  Serial.println("LVGL optimization: data caches always live; hidden screen widgets are not redrawn");
  Serial.println("Home UI: status fallback before module-list cache; full-width nav; non-overlapping settings controls");
  Serial.println("Home modules: DISPLAY_STATUS authoritative; event-driven list refresh; Fan/IO detail refresh for OUT1/OUT2");
  Serial.printf("Panel scan ceiling: %.2f Hz at %u Hz PCLK (%ux%u total timing)\n",
                (double)DISPLAY_RGB_PCLK_HZ /
                  (double)((DISPLAY_RGB_WIDTH + 8 + 4 + 8) *
                           (DISPLAY_RGB_HEIGHT + 8 + 4 + 8)),
                (unsigned)DISPLAY_RGB_PCLK_HZ,
                (unsigned)(DISPLAY_RGB_WIDTH + 8 + 4 + 8),
                (unsigned)(DISPLAY_RGB_HEIGHT + 8 + 4 + 8));

  lv_create_boot_screen();
  lv_create_update_screen();
  lvgl_ready = true;
}

static void show_boot_screen() {
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_BOOT_SCREEN);
    return;
  }
  if (!lvgl_ready) return;
  lv_set_text(ui_boot_fw, String(tr("Firmware ", "Firmware ")) + String(FW_MAJOR) + "." + String(FW_MINOR) + "." + String(FW_PATCH) + String(FW_SUFFIX));
  lv_set_text(ui_boot_addr, String("RS485 ") + lv_addr_text(module_addr));
  lv_screen_switch(ui_boot_screen);
}

static void lvgl_force_refresh_once() {
  if (!lvgl_ready) return;
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(lvgl_disp);
  lvgl_flush_canvas_if_dirty(true);
  delay(3);
  lv_refr_now(lvgl_disp);
  lvgl_flush_canvas_if_dirty(true);
}

static String fw_update_speed_text() {
  if (!fw_update_started_ms || !fw_update_offset) return String("-");
  const uint32_t elapsed_ms = millis() - fw_update_started_ms;
  if (elapsed_ms < 250) return String("-");
  const uint32_t bytes_per_s = (uint32_t)(((uint64_t)fw_update_offset * 1000ULL) / elapsed_ms);
  if (bytes_per_s >= 1024UL * 1024UL) {
    return String(bytes_per_s / (1024UL * 1024UL)) + "." + String((bytes_per_s / (1024UL * 102UL)) % 10) + " MB/s";
  }
  return String(bytes_per_s / 1024UL) + " KB/s";
}

static uint8_t fw_update_local_percent(uint8_t fallback_percent) {
  if (fallback_percent >= 100) return 100;
  if (!fw_update_size) return fallback_percent;
  uint32_t pct = ((uint64_t)fw_update_offset * 100ULL) / fw_update_size;
  if (fw_update_active && pct > 99) pct = 99;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

static void show_update_screen(uint8_t target, uint8_t percent, const char* msg, const char* target_name = nullptr) {
  const bool local_update = update_has_live_percent(target);
  const uint8_t shown_percent = local_update ? fw_update_local_percent(percent) : (percent >= 100 ? 100 : 35);
  const String target_text = local_update
    ? (lv_addr_text(module_addr) + " Display 800x480  v" + String(FW_MAJOR) + "." + String(FW_MINOR) + "." + String(FW_PATCH) + String(FW_SUFFIX))
    : (String(tr("Updating ", "Update: ")) + ((target_name && target_name[0]) ? String(target_name) : update_target_name(target)));
  const String percent_text = local_update ? (String(shown_percent) + "%") : (percent >= 100 ? String(tr("Done", "Fertig")) : String(tr("BUSY", "AKTIV")));
  const char* status_text = local_update ? update_message_text(msg) : tr("Module update running", "Modul-Update l\303\244uft");
  const char* phase_text = local_update ? (shown_percent >= 100 ? tr("Done", "Fertig") : (shown_percent == 0 ? tr("Start", "Start") : tr("Writing", "Schreiben"))) : tr("Busy", "Aktiv");
  const char* hint_text = local_update ? tr("Do not unplug", "Nicht trennen!") : tr("Target paused", "Ziel pausiert");
  if (!lvgl_ready) return;
  lv_set_text(ui_update_target, target_text);
  lv_set_text_c(ui_update_status, status_text);
  lv_set_text(ui_update_percent, percent_text);
  lv_set_text_c(ui_update_phase, phase_text);
  if (ui_update_bar) lv_bar_set_value(ui_update_bar, shown_percent, LV_ANIM_OFF);
  lv_set_text(ui_update_written, fmt_bytes(fw_update_offset));
  lv_set_text(ui_update_size, fw_update_size ? fmt_bytes(fw_update_size) : String(tr("unknown", "unbekannt")));
  lv_set_text(ui_update_speed, local_update ? fw_update_speed_text() : String("-"));
  lv_set_text_c(ui_update_detail, local_update ? tr("Receiving firmware over RS485", "Firmware per RS485") : tr("External module update", "Externes Modul-Update"));
  lv_set_text_c(ui_update_hint, hint_text);
  lv_screen_switch(ui_update_screen);
}

static void show_dashboard() {
  if (running_in_rs485_task()) {
    ui_defer_flags(UI_DEFER_DASHBOARD);
    return;
  }
  if (!lvgl_ready) return;
  if (screensaver_active) {
    // Keep the screensaver as the active screen. Status frames during sleep
    // must only refresh the screensaver widgets, not the hidden Home UI.
    screensaver_update_values();
    return;
  }
  const bool boot_active = ui_boot_screen && lv_screen_active() == ui_boot_screen;
  // After the display-frame split the boot screen can already receive header data
  // (clock/link) before the visible Home view is selected. Treat the first valid
  // master status as permission to leave Boot and normalize the visible view to Home.
  if (boot_active) {
    display_view_mode = DISPLAY_VIEW_HOME;
    display_view_arg = 0;
  }
  last_drawn_status = status;
  have_drawn_status = true;
  lv_update_app_values();
  if (!screensaver_active && (display_view_mode == DISPLAY_VIEW_HOME || boot_active) && lv_screen_active() != ui_dashboard_screen) {
    lv_screen_switch(ui_dashboard_screen);
  }
}

static void show_status_message(const char* msg) {
  if (running_in_rs485_task()) {
    ui_defer_status_message(msg);
    return;
  }
  if (!lvgl_ready) return;
  lv_set_text_c(ui_status_msg, msg);
  lv_screen_switch(ui_status_screen);
}

static bool status_changed_for_draw() {
  if (!have_drawn_status) return true;
  return status.output_enabled != last_drawn_status.output_enabled ||
    status.jbc_connected != last_drawn_status.jbc_connected ||
    status.weller_connected != last_drawn_status.weller_connected ||
    status.jbc_present != last_drawn_status.jbc_present ||
    status.weller_present != last_drawn_status.weller_present ||
    status.fan_present != last_drawn_status.fan_present ||
    status.output_present != last_drawn_status.output_present ||
    status.output_power != last_drawn_status.output_power ||
    status.fan_rpm != last_drawn_status.fan_rpm ||
    status.afterrun_s != last_drawn_status.afterrun_s ||
    status.work_mask != last_drawn_status.work_mask ||
    status.modules_count != last_drawn_status.modules_count ||
    status.output_addr != last_drawn_status.output_addr ||
    status.preferred_output_addr != last_drawn_status.preferred_output_addr ||
    status.auto_output_addr != last_drawn_status.auto_output_addr ||
    status.main_input_source_type != last_drawn_status.main_input_source_type ||
    status.main_input_source_addr != last_drawn_status.main_input_source_addr ||
    status.main_input_source_bit != last_drawn_status.main_input_source_bit ||
    status.jbc_inputs != last_drawn_status.jbc_inputs ||
    status.continuous != last_drawn_status.continuous ||
    status.suction_level != last_drawn_status.suction_level ||
    status.select_flow != last_drawn_status.select_flow ||
    status.delay_work_s != last_drawn_status.delay_work_s ||
    status.afterrun_power_enabled != last_drawn_status.afterrun_power_enabled ||
    status.afterrun_power != last_drawn_status.afterrun_power ||
    status.delay_stand_s != last_drawn_status.delay_stand_s ||
    status.stand_intakes != last_drawn_status.stand_intakes ||
    status.jbc_addr != last_drawn_status.jbc_addr ||
    status.station_addr != last_drawn_status.station_addr ||
    status.jbc_station_count != last_drawn_status.jbc_station_count ||
    memcmp(status.jbc_stations, last_drawn_status.jbc_stations, sizeof(status.jbc_stations)) != 0 ||
    status.weller_speed != last_drawn_status.weller_speed ||
    status.weller_filter_status != last_drawn_status.weller_filter_status ||
    status.weller_filter_runtime_min != last_drawn_status.weller_filter_runtime_min ||
    status.weller_filter_programmed_min != last_drawn_status.weller_filter_programmed_min ||
    status.weller_light != last_drawn_status.weller_light ||
    status.io_input_mask != last_drawn_status.io_input_mask ||
    status.io_output_mask != last_drawn_status.io_output_mask ||
    status.io_fault_mask != last_drawn_status.io_fault_mask ||
    status.module_output_enabled != last_drawn_status.module_output_enabled ||
    status.module_output_power != last_drawn_status.module_output_power ||
    status.module_output_rpm != last_drawn_status.module_output_rpm ||
    status.module_output_fault != last_drawn_status.module_output_fault ||
    status.weller_version != last_drawn_status.weller_version ||
    status.jbc_link_flags != last_drawn_status.jbc_link_flags ||
    status.jbc_work_mask != last_drawn_status.jbc_work_mask ||
    status.jbc_stand_mask != last_drawn_status.jbc_stand_mask ||
    status.route_jbc_output != last_drawn_status.route_jbc_output ||
    status.jbc_stat_error != last_drawn_status.jbc_stat_error ||
    status.master_alarm_valid != last_drawn_status.master_alarm_valid ||
    status.master_alarm_count != last_drawn_status.master_alarm_count ||
    status.master_alarm_item_count != last_drawn_status.master_alarm_item_count ||
    status.master_alarm_critical_mask != last_drawn_status.master_alarm_critical_mask ||
    memcmp(status.master_alarm_addr, last_drawn_status.master_alarm_addr, sizeof(status.master_alarm_addr)) != 0 ||
    memcmp(status.master_alarm_type, last_drawn_status.master_alarm_type, sizeof(status.master_alarm_type)) != 0 ||
    memcmp(status.master_alarm_code, last_drawn_status.master_alarm_code, sizeof(status.master_alarm_code)) != 0 ||
    memcmp(status.master_alarm_value, last_drawn_status.master_alarm_value, sizeof(status.master_alarm_value)) != 0 ||
    status.update_active != last_drawn_status.update_active ||
    status.update_target != last_drawn_status.update_target ||
    status.update_progress != last_drawn_status.update_progress;
}

static void fw_update_render_screen_now(bool force_flush = false) {
  // During a local firmware update the main loop intentionally skips normal
  // LVGL handling to keep touch/navigation disabled. Without this explicit
  // pump the update page changes its labels internally, but the canvas is not
  // rendered/flushed to the panel until the update is finished.
  if (!lvgl_ready) return;
  lvgl_timer_handler_profiled();
  lvgl_flush_canvas_if_dirty(force_flush);
}

static void draw_update_progress_throttled(uint8_t target, uint8_t progress, const char* msg, bool force = false, const char* target_name = nullptr) {
  if (running_in_rs485_task()) {
    const uint32_t now = millis();
    if (!force && progress < 100 &&
        (uint32_t)(now - fw_update_last_draw_ms) < 200UL &&
        target == fw_update_last_draw_target) {
      return;
    }
    fw_update_last_draw_target = target;
    fw_update_last_draw_percent = progress;
    fw_update_last_draw_ms = now;
    ui_defer_update_screen(target, progress, msg, force || progress >= 100, target_name);
    return;
  }
  const uint32_t now = millis();
  const bool local_update = update_is_local_display_target(target);
  if (!force && progress < 100 &&
      (uint32_t)(now - fw_update_last_draw_ms) < 200UL &&
      target == fw_update_last_draw_target) {
    return;
  }
  fw_update_last_draw_target = target;
  fw_update_last_draw_percent = progress;
  fw_update_last_draw_ms = now;
  show_update_screen(target, progress, msg, target_name);
  if (local_update) {
    fw_update_render_screen_now(force || progress >= 100);
  }
}


static bool ui_should_hold_heavy_updates() {
  if (fw_update_active) return false;
  // Keep live data cached, but do not relayout cards during dragging or inertia.
  const uint32_t now = millis();
  return lvgl_touch_pressed ||
    (lvgl_touch_indev && lv_indev_get_scroll_obj(lvgl_touch_indev)) ||
    (uint32_t)(now - lvgl_last_touch_ms) < 180UL;
}

static void apply_deferred_ui_updates() {
  if (!lvgl_ready) return;

  uint32_t flags = 0;
  uint8_t update_target = 0;
  uint8_t update_progress = 0;
  bool update_force = false;
  char update_msg[40] = {0};
  char update_name[40] = {0};
  char status_msg[64] = {0};

  portENTER_CRITICAL(&ui_deferred_mux);
  flags = ui_deferred_flags;
  ui_deferred_flags = 0;
  if (flags & UI_DEFER_UPDATE_SCREEN) {
    update_target = ui_deferred_update_target;
    update_progress = ui_deferred_update_progress;
    update_force = ui_deferred_update_force;
    copy_deferred_text(update_msg, sizeof(update_msg), ui_deferred_update_msg);
    copy_deferred_text(update_name, sizeof(update_name), ui_deferred_update_name);
  }
  if (flags & UI_DEFER_STATUS_MSG) {
    copy_deferred_text(status_msg, sizeof(status_msg), ui_deferred_status_msg);
  }
  portEXIT_CRITICAL(&ui_deferred_mux);

  if (!flags) return;

  const uint32_t heavy_flags = UI_DEFER_MASTER_LINK | UI_DEFER_APP_VALUES | UI_DEFER_DASHBOARD |
                               UI_DEFER_BUS_UPDATE | UI_DEFER_MODULE_LIST | UI_DEFER_MODULE_DETAIL;
  if ((flags & heavy_flags) && ui_should_hold_heavy_updates()) {
    const uint32_t keep_flags = flags & heavy_flags;
    flags &= ~heavy_flags;
    portENTER_CRITICAL(&ui_deferred_mux);
    ui_deferred_flags |= keep_flags;
    portEXIT_CRITICAL(&ui_deferred_mux);
    if (!flags) return;
  }

  if (flags & UI_DEFER_BOOT_SCREEN) show_boot_screen();
  if (flags & UI_DEFER_STATUS_MSG) show_status_message(status_msg[0] ? status_msg : "Status");
  if (flags & UI_DEFER_UPDATE_SCREEN) {
    show_update_screen(update_target, update_progress, update_msg[0] ? update_msg : "Bus update", update_name[0] ? update_name : nullptr);
  }
  if (flags & UI_DEFER_MASTER_LINK) {
    if (lv_update_master_link_ui()) flags |= UI_DEFER_APP_VALUES;
  }
  if (flags & UI_DEFER_BUS_UPDATE) refresh_bus_update_inline_ui();
  if (flags & UI_DEFER_MODULE_LIST) lv_update_module_list();
  if (flags & UI_DEFER_MODULE_DETAIL) lv_update_module_detail();
  if (flags & UI_DEFER_DISPLAY_SETTINGS) lv_update_display_settings_widgets(true);
  if (flags & UI_DEFER_APP_VALUES) {
    if (screensaver_active) screensaver_update_values();
    else lv_update_app_values();
  }
  if (flags & UI_DEFER_DASHBOARD) show_dashboard();

  if (flags & UI_DEFER_UPDATE_SCREEN) {
    lvgl_timer_handler_profiled();
    lvgl_flush_canvas_if_dirty(update_force);
  }
}

static void send_status_response(const Frame& req, Status s) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = req.cmd | 0x80;
  resp.len = 1;
  resp.payload[0] = s;
  bus.send(resp);
}

static void send_display_status_response(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_DISPLAY_STATUS | 0x80;
  resp.len = 6;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = pending_display_event;
  put_u16_le(resp.payload + 2, (uint16_t)pending_display_event_value);
  // Report the real UI view only. Background cache requests are appended as
  // markers below so the Master does not mistake them for a visible page change.
  resp.payload[4] = display_view_mode;
  resp.payload[5] = display_view_arg;

  const uint32_t now = millis();
  const bool user_needs_detail = display_view_mode == DISPLAY_VIEW_MODULE_DETAIL;
  const bool user_on_module_list = display_view_mode == DISPLAY_VIEW_MODULE_LIST;

  if (user_needs_detail && module_summary_is_universal_addr(display_view_arg) && resp.len + 2 <= MAX_PAYLOAD) {
    const bool descriptor_incomplete = selected_module.valid && selected_module.addr == display_view_arg &&
      selected_module.universal_entity_total != 0 &&
      selected_module.universal_entity_count < (selected_module.universal_entity_total > DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX
        ? DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX : selected_module.universal_entity_total);
    const uint32_t entity_request_interval_ms = selected_universal_readback_id ? 350UL : 1000UL;
    if (descriptor_incomplete || !selected_universal_request_ms ||
        (uint32_t)(now - selected_universal_request_ms) >= entity_request_interval_ms) {
      resp.payload[resp.len++] = 0xAC; // visible detail-page entity request
      resp.payload[resp.len++] = selected_module.addr == display_view_arg ? selected_universal_request_start() : universal_cache_request_start(display_view_arg);
      selected_universal_request_ms = now;
    }
  }

  // Home-essential cache maintenance: event-driven module-list refresh.
  if (!fw_update_active && !status.update_active && !user_needs_detail && !user_on_module_list &&
      home_module_list_refresh_pending && resp.len + 2 <= MAX_PAYLOAD) {
    resp.payload[resp.len++] = 0xAE;
    resp.payload[resp.len++] = module_list_cache_request_start < module_total ?
      module_list_cache_request_start : 0;
    module_list_cache_last_request_ms = now;
  }

  // Fan/IO OUT1/OUT2 are module-local values. If Fan/IO is not the main output,
  // DISPLAY_STATUS cannot provide them, so refresh that single detail record.
  if (!fw_update_active && !status.update_active && !user_needs_detail && !user_on_module_list &&
      status.fan_present && !home_module_list_refresh_pending &&
      (!home_fan_detail_request_ms ||
       (uint32_t)(now - home_fan_detail_request_ms) >= HOME_FAN_DETAIL_REQUEST_MS) &&
      resp.len + 2 <= MAX_PAYLOAD) {
    const uint8_t fan_addr = first_module_addr_by_group(MODULE_FAN_IO, false);
    if (fan_addr) {
      resp.payload[resp.len++] = 0xAF;
      resp.payload[resp.len++] = fan_addr;
      home_fan_detail_request_ms = now;
    }
  }

#if DISPLAY_BACKGROUND_CACHE_ENABLED
  if (!fw_update_active && !status.update_active && !user_needs_detail && !user_on_module_list && module_total > 0 &&
      (!module_detail_cache_last_request_ms ||
       (uint32_t)(now - module_detail_cache_last_request_ms) >= MODULE_DETAIL_CACHE_REQUEST_MS)) {
    uint8_t detail_addr = 0;
    switch (module_detail_cache_phase++ % 4U) {
      case 0:
        detail_addr = first_module_addr_by_group(MODULE_WELLER_ZERO_SMOG, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_FAN_IO, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_JBC_BUS, true);
        break;
      case 1:
        detail_addr = first_module_addr_by_group(MODULE_FAN_IO, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_JBC_BUS, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_WELLER_ZERO_SMOG, true);
        break;
      case 2:
        detail_addr = first_module_addr_by_group(MODULE_JBC_BUS, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_WELLER_ZERO_SMOG, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_FAN_IO, true);
        break;
      default:
        detail_addr = first_module_addr_by_group(MODULE_WELLER_ZERO_SMOG, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_FAN_IO, true);
        if (!detail_addr) detail_addr = first_module_addr_by_group(MODULE_JBC_BUS, true);
        break;
    }
    if (detail_addr && resp.len + 2 <= MAX_PAYLOAD) {
      resp.payload[resp.len++] = 0xAF; // background module detail request
      resp.payload[resp.len++] = detail_addr;
      module_detail_cache_last_request_ms = now;
    }
  }

  if (!fw_update_active && !status.update_active && !user_needs_detail && !user_on_module_list &&
      (!module_list_cache_last_request_ms ||
       (uint32_t)(now - module_list_cache_last_request_ms) >= MODULE_LIST_CACHE_REQUEST_MS) && resp.len + 2 <= MAX_PAYLOAD) {
    resp.payload[resp.len++] = 0xAE; // background module list request
    resp.payload[resp.len++] = module_list_cache_request_start < module_total ? module_list_cache_request_start : 0;
    module_list_cache_last_request_ms = now;
  }

  if (!fw_update_active && !status.update_active && !user_needs_detail && !user_on_module_list && resp.len + 3 <= MAX_PAYLOAD) {
    const uint8_t uni_addr = first_universal_module_addr_needing_cache();
    if (uni_addr) {
      resp.payload[resp.len++] = 0xAD;
      resp.payload[resp.len++] = uni_addr;
      resp.payload[resp.len++] = universal_cache_request_start(uni_addr);
    }
  }

#endif

  bus.send(resp);
  pending_display_event = DISPLAY_EVENT_NONE;
  pending_display_event_value = 0;
}

static void parse_display_extension(const Frame& req) {
  if (req.len < 57) return;
  const uint8_t ext_version = req.payload[55];
  uint16_t p = 56;
  if (ext_version == 1) {
    status.preferred_output_addr = 0;
    status.auto_output_addr = 0;
    status.jbc_stat_error = 0;
  } else if (ext_version >= 2) {
    if (req.len < 58) return;
    status.preferred_output_addr = req.payload[p++];
    status.auto_output_addr = 0;
    if (ext_version >= 3) {
      if (p + 2 > req.len) return;
      status.jbc_stat_error = get_u16_le(req.payload + p); p += 2;
    } else {
      status.jbc_stat_error = 0;
    }
  } else {
    return;
  }

  if (ext_version >= 4 && p + 4 <= req.len && req.payload[p] == 0xA9) {
    p++; // structured alarm list marker, parsed earlier by handle_display_status
    uint8_t item_count = req.payload[p + 1];
    p += 3;
    if (item_count > 6) item_count = 6;
    const uint16_t skip = (uint16_t)item_count * 6U;
    if (p + skip > req.len) return;
    p += skip;
  }

  if (ext_version >= 4 && p + 4 <= req.len && req.payload[p] == 0xAA) {
    p++; // main input route marker
    status.main_input_source_type = req.payload[p++];
    status.main_input_source_addr = req.payload[p++];
    status.main_input_source_bit = req.payload[p++];
  } else if (status.route_jbc_output) {
    status.main_input_source_type = 1;
    status.main_input_source_addr = 0;
    status.main_input_source_bit = 0;
  }

  if (ext_version >= 5 && p + 4 <= req.len && req.payload[p] == 0xAB) {
    p++; // afterrun power profile marker
    status.afterrun_power_enabled = req.payload[p++] != 0;
    status.afterrun_power = get_u16_le(req.payload + p); p += 2;
  }

  if (ext_version >= 5 && p + 2 <= req.len && req.payload[p] == 0xA8) {
    p++; // auto output candidate marker
    status.auto_output_addr = req.payload[p++];
  }

  if (ext_version >= 5 && p + 2 <= req.len && req.payload[p] == 0xA7) {
    p++;
    update_jbc_station_list(req, p);
  } else if (!status.jbc_connected) {
    portENTER_CRITICAL(&jbc_station_mux);
    status.jbc_station_count = 0;
    memset(status.jbc_stations, 0, sizeof(status.jbc_stations));
    portEXIT_CRITICAL(&jbc_station_mux);
  }

  // Frame-split protocol: module list, module details and Universal/Modbus
  // entities are no longer embedded in DISPLAY_STATUS. Stop here so stale
  // legacy blocks (or the clock marker 0xA6) can never update Home/detail caches.
  return;

  if (p >= req.len) return;
  const uint8_t mode = req.payload[p++];
  if (mode == DISPLAY_VIEW_MODULE_LIST && p + 3 <= req.len) {
    const uint8_t old_total = module_total;
    const uint8_t raw_total = req.payload[p++];
    uint8_t wire_total = raw_total > 17 ? 17 : raw_total;
    const bool other_module_update = status.update_active && !update_is_local_display_target(status.update_target);
    const uint8_t start = req.payload[p++];
    const uint8_t count = req.payload[p++];
    // While a slave OTA is running, the master can temporarily stop sending the
    // list chunks or report zero/less modules. Never let those temporary empty
    // packets clear the list UI.
    if (other_module_update && (wire_total == 0 || count == 0)) {
      bus_update_ensure_module_row();
      if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_LIST | UI_DEFER_BUS_UPDATE);
      else lv_update_module_list();
      return;
    }
    if (other_module_update && wire_total < module_total) wire_total = module_total;
    module_total = wire_total;
    uint8_t parsed = 0;
    bool list_changed = old_total != module_total;
    for (; parsed < count && (uint8_t)(start + parsed) < 17; ++parsed) {
      if (p + 16 > req.len) break;
      DisplayModuleSummary next_summary;
      memset(&next_summary, 0, sizeof(next_summary));
      DisplayModuleSummary& m = next_summary;
      m.valid = true;
      m.addr = req.payload[p++];
      m.type = req.payload[p++];
      m.flags = req.payload[p++];
      m.fw_major = req.payload[p++];
      m.fw_minor = req.payload[p++];
      m.fw_patch = req.payload[p++];
      uint8_t suffix_len = req.payload[p++];
      if (p + suffix_len > req.len) break;
      uint8_t suffix_copy = suffix_len;
      if (suffix_copy > sizeof(m.fw_suffix) - 1) suffix_copy = sizeof(m.fw_suffix) - 1;
      if (suffix_copy) memcpy(m.fw_suffix, req.payload + p, suffix_copy);
      m.fw_suffix[suffix_copy] = 0;
      p += suffix_len;
      if (p + 8 > req.len) break;
      m.caps = get_u32_le(req.payload + p); p += 4;
      m.uptime_s = get_u32_le(req.payload + p); p += 4;
      const uint8_t wire_name_len = req.payload[p++];
      if (p + wire_name_len > req.len) break;
      uint8_t copy_len = wire_name_len;
      if (copy_len > sizeof(m.name) - 1) copy_len = sizeof(m.name) - 1;
      memcpy(m.name, req.payload + p, copy_len);
      m.name[copy_len] = 0;
      p += wire_name_len;
      if (m.addr == ADDR_MASTER) {
        if (master_uptime_valid && m.uptime_s + 2 < last_master_uptime_s) reset_expected_modules();
        last_master_uptime_s = m.uptime_s;
        master_uptime_valid = true;
      }
      DisplayModuleSummary& current = module_summaries[start + parsed];
      if (memcmp(&current, &next_summary, sizeof(next_summary)) != 0) {
        current = next_summary;
        list_changed = true;
      }
    }
    const uint8_t next = start + parsed;
    if (next < module_total) display_view_arg = next;
    else {
      display_view_arg = 0;
      for (uint8_t i = module_total; i < 17; ++i) memset(&module_summaries[i], 0, sizeof(module_summaries[i]));
      learn_expected_modules_from_current_list();
    }
    if (list_changed) {
      if (running_in_rs485_task()) {
        ui_defer_flags(UI_DEFER_MODULE_LIST | UI_DEFER_BUS_UPDATE);
      } else {
        lv_update_module_list();
#if BUS_UPDATE_AUTO_SCROLL_TO_MODULE
        if (status.update_active && !update_is_local_display_target(status.update_target) &&
            lv_screen_active() == ui_module_list_screen &&
            bus_update_auto_nav_done && bus_update_auto_nav_target == status.update_target &&
            bus_update_last_scrolled_target != status.update_target) {
          bus_update_scroll_to_target(true);
        }
#endif
      }
    }
    if (next >= module_total) {
      if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_DASHBOARD);
      else if (lvgl_ready) show_dashboard();
    }
  } else if (mode == DISPLAY_VIEW_MODULE_DETAIL) {
    if (p >= req.len || !req.payload[p++]) return;
    if (p + 63 > req.len) return;
    DisplayModuleDetail& m = detail_parse_scratch;
    memset(&m, 0, sizeof(m));
    m.weller_uart_age = 0xFFFF;
    m.valid = true;
    m.addr = req.payload[p++];
    m.type = req.payload[p++];
    m.flags = req.payload[p++];
    m.fw_major = req.payload[p++];
    m.fw_minor = req.payload[p++];
    m.fw_patch = req.payload[p++];
    uint8_t suffix_len = req.payload[p++];
    if (p + suffix_len > req.len) return;
    uint8_t suffix_copy = suffix_len;
    if (suffix_copy > sizeof(m.fw_suffix) - 1) suffix_copy = sizeof(m.fw_suffix) - 1;
    if (suffix_copy) memcpy(m.fw_suffix, req.payload + p, suffix_copy);
    m.fw_suffix[suffix_copy] = 0;
    p += suffix_len;
    m.caps = get_u32_le(req.payload + p); p += 4;
    m.uptime_s = get_u32_le(req.payload + p); p += 4;
    m.heap_free = get_u32_le(req.payload + p); p += 4;
    m.cpu_load = req.payload[p++];
    m.loop_max_ms = get_u16_le(req.payload + p); p += 2;
    m.io_inputs = get_u16_le(req.payload + p); p += 2;
    m.io_outputs = get_u16_le(req.payload + p); p += 2;
    m.io_faults = get_u16_le(req.payload + p); p += 2;
    m.output_enabled = req.payload[p++] != 0;
    m.output_power = get_u16_le(req.payload + p); p += 2;
    m.output_rpm = get_u16_le(req.payload + p); p += 2;
    m.output_fault = get_u16_le(req.payload + p); p += 2;
    m.jbc_addr = req.payload[p++];
    m.station_addr = req.payload[p++];
    m.jbc_flags = req.payload[p++];
    m.jbc_work = req.payload[p++];
    m.jbc_stand = req.payload[p++];
    m.suction = req.payload[p++];
    m.select_flow = get_u16_le(req.payload + p); p += 2;
    m.delay_work = get_u16_le(req.payload + p); p += 2;
    m.delay_stand = get_u16_le(req.payload + p); p += 2;
    m.stand_intakes = req.payload[p++];
    m.continuous = req.payload[p++];
    m.weller_speed = req.payload[p++];
    m.weller_filter = req.payload[p++];
    m.weller_runtime = get_u16_le(req.payload + p); p += 2;
    m.weller_programmed = get_u16_le(req.payload + p); p += 2;
    m.weller_rpm = get_u16_le(req.payload + p); p += 2;
    m.weller_version = get_u16_le(req.payload + p); p += 2;
    m.weller_light = req.payload[p++];
    m.weller_uart_age = get_u16_le(req.payload + p); p += 2;
    detail_last_rx_ms = millis();
    const uint8_t wire_name_len = req.payload[p++];
    if (p + wire_name_len > req.len) return;
    uint8_t copy_name_len = wire_name_len;
    if (copy_name_len > sizeof(m.name) - 1) copy_name_len = sizeof(m.name) - 1;
    memcpy(m.name, req.payload + p, copy_name_len);
    m.name[copy_name_len] = 0;
    p += wire_name_len;
    if (p < req.len) {
      const uint8_t wire_device_id_len = req.payload[p++];
      if (wire_device_id_len <= sizeof(m.jbc_device_id) && p + wire_device_id_len <= req.len) {
        m.jbc_device_id_len = wire_device_id_len;
        if (wire_device_id_len) memcpy(m.jbc_device_id, req.payload + p, wire_device_id_len);
        p += wire_device_id_len;
      }
    }
    if (selected_module.valid && selected_module.addr == m.addr && detail_fields_is_universal(m.type, m.caps)) {
      m.universal_descriptor_crc = selected_module.universal_descriptor_crc;
      m.universal_entity_total = selected_module.universal_entity_total;
      m.universal_entity_count = selected_module.universal_entity_count;
      memcpy(m.universal_entities, selected_module.universal_entities, sizeof(m.universal_entities));
    }
    while (p < req.len) {
      const uint8_t marker = req.payload[p++];
      if (marker == 0xB1 && p + 10 <= req.len) {
        m.filter_saturation_permille = get_u16_le(req.payload + p); p += 2;
        m.filter_pressure_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
        m.filter_zero_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
        m.filter_clean_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
        m.filter_full_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
      } else if (marker == 0xB4) {
        if (!parse_detail_io_aliases(req, p, m)) return;
      } else if (marker == 0xB2 && p + 7 <= req.len) {
        const uint32_t crc = get_u32_le(req.payload + p); p += 4;
        const uint8_t total = req.payload[p++];
        const uint8_t start = req.payload[p++];
        uint8_t count = req.payload[p++];
        const DisplayUniversalModuleCache* old_uc = universal_cache_find(m.addr, false);
        if (selected_module.valid && selected_module.addr == m.addr && selected_module.universal_descriptor_crc == crc) {
          m.universal_descriptor_crc = selected_module.universal_descriptor_crc;
          m.universal_entity_total = selected_module.universal_entity_total;
          m.universal_entity_count = selected_module.universal_entity_count;
          memcpy(m.universal_entities, selected_module.universal_entities, sizeof(m.universal_entities));
        } else if (old_uc && old_uc->universal_descriptor_crc == crc) {
          m.universal_descriptor_crc = old_uc->universal_descriptor_crc;
          m.universal_entity_total = old_uc->universal_entity_total;
          m.universal_entity_count = old_uc->universal_entity_count;
          memcpy(m.universal_entities, old_uc->universal_entities, sizeof(m.universal_entities));
        } else {
          m.universal_descriptor_crc = crc;
          m.universal_entity_total = total;
          m.universal_entity_count = 0;
          memset(m.universal_entities, 0, sizeof(m.universal_entities));
        }
        m.universal_descriptor_crc = crc;
        m.universal_entity_total = total;
        if (m.universal_entity_count > total) m.universal_entity_count = total;
        for (uint8_t clear_i = total; clear_i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++clear_i) memset(&m.universal_entities[clear_i], 0, sizeof(m.universal_entities[clear_i]));
        for (uint8_t i = 0; i < count; ++i) {
          if (p + 12 > req.len) { p = req.len; break; }
          DisplayUniversalEntity e;
          memset(&e, 0, sizeof(e));
          e.valid = true;
          e.id = req.payload[p++];
          e.type = req.payload[p++];
          e.flags = req.payload[p++];
          e.min_value = (int16_t)get_u16_le(req.payload + p); p += 2;
          e.max_value = (int16_t)get_u16_le(req.payload + p); p += 2;
          e.step_value = (int16_t)get_u16_le(req.payload + p); p += 2;
          e.value = (int16_t)get_u16_le(req.payload + p); p += 2;
          uint8_t label_len = req.payload[p++];
          if (p + label_len > req.len) { p = req.len; break; }
          uint8_t copy_label = label_len;
          if (copy_label > sizeof(e.label) - 1) copy_label = sizeof(e.label) - 1;
          if (copy_label) memcpy(e.label, req.payload + p, copy_label);
          e.label[copy_label] = 0;
          p += label_len;
          if (p >= req.len) break;
          uint8_t unit_len = req.payload[p++];
          if (p + unit_len > req.len) { p = req.len; break; }
          uint8_t copy_unit = unit_len;
          if (copy_unit > sizeof(e.unit) - 1) copy_unit = sizeof(e.unit) - 1;
          if (copy_unit) memcpy(e.unit, req.payload + p, copy_unit);
          e.unit[copy_unit] = 0;
          p += unit_len;
          if (p >= req.len) break;
          uint8_t text_len = req.payload[p++];
          if (p + text_len > req.len) { p = req.len; break; }
          uint8_t copy_text = text_len;
          if (copy_text > sizeof(e.text) - 1) copy_text = sizeof(e.text) - 1;
          if (copy_text) memcpy(e.text, req.payload + p, copy_text);
          e.text[copy_text] = 0;
          p += text_len;
          if (p < req.len) {
            uint8_t options_len = req.payload[p++];
            if (p + options_len > req.len) { p = req.len; break; }
            uint8_t copy_options = options_len;
            if (copy_options > sizeof(e.options) - 1) copy_options = sizeof(e.options) - 1;
            if (copy_options) memcpy(e.options, req.payload + p, copy_options);
            e.options[copy_options] = 0;
            p += options_len;
          }
          const uint8_t slot = (uint8_t)(start + i);
          if (slot < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX) {
            m.universal_entities[slot] = e;
            if (m.universal_entity_count <= slot) m.universal_entity_count = slot + 1;
          }
        }
      } else {
        break;
      }
    }
    // Every detail packet refreshes the Home cache, even when the payload is
    // byte-identical to the last selected_module. The normal DISPLAY_STATUS
    // packet can contain only the current main-output telemetry and would
    // otherwise overwrite Weller/FanIO tiles until that module becomes main output.
    if (detail_fields_is_universal(m.type, m.caps)) {
      DisplayUniversalModuleCache* uc = universal_cache_find(m.addr, true);
      if (uc) {
        uc->type = m.type;
        uc->flags = m.flags;
        uc->universal_descriptor_crc = m.universal_descriptor_crc;
        uc->universal_entity_total = m.universal_entity_total;
        uc->universal_entity_count = m.universal_entity_count > DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX ? DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX : m.universal_entity_count;
        memcpy(uc->universal_entities, m.universal_entities, sizeof(uc->universal_entities));
        uc->last_ms = millis();
        if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_APP_VALUES);
        else lv_update_app_values();
      }
    }
    const bool detail_changed = memcmp(&selected_module, &m, sizeof(m)) != 0;
    selected_module = m;
    sync_selected_module_detail_to_home_cache();
    if (detail_changed) {
      const int8_t summary_index = module_summary_index_by_addr(m.addr);
      if (summary_index >= 0) {
        DisplayModuleSummary& summary = module_summaries[(uint8_t)summary_index];
        summary.type = m.type;
        summary.flags = m.flags;
        summary.fw_major = m.fw_major;
        summary.fw_minor = m.fw_minor;
        summary.fw_patch = m.fw_patch;
        strncpy(summary.fw_suffix, m.fw_suffix, sizeof(summary.fw_suffix) - 1);
        summary.fw_suffix[sizeof(summary.fw_suffix) - 1] = 0;
        summary.caps = m.caps;
        summary.uptime_s = m.uptime_s;
        strncpy(summary.name, m.name, sizeof(summary.name) - 1);
        summary.name[sizeof(summary.name) - 1] = 0;
        if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_LIST);
        else lv_update_module_list();
      }
    }
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_DETAIL);
    else lv_update_module_detail();
  }
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
  display_resync_after_flash_write();
  send_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
}

static void send_info(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_INFO | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_DISPLAY;
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
  const char name[24] = "Display";
  const char* shown_name = module_label[0] ? module_label : name;
  while (*shown_name && o < MAX_PAYLOAD) resp.payload[o++] = (uint8_t)*shown_name++;
  resp.len = o;
  bus.send(resp);
}

static void send_caps(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_CAPS | 0x80;
  resp.len = 5;
  resp.payload[0] = STATUS_OK;
  put_u32_le(resp.payload + 1, CAP_DISPLAY | CAP_DISPLAY_800X480 | CAP_FW_UPDATE | CAP_DISPLAY_HYBRID);
  bus.send(resp);
}

static void send_telemetry(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_GET_TELEMETRY | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_DISPLAY;
  put_u32_le(resp.payload + o, ESP.getFreeHeap()); o += 4;
  put_u32_le(resp.payload + o, ESP.getMinFreeHeap()); o += 4;
  put_u32_le(resp.payload + o, millis() / 1000UL); o += 4;
  resp.payload[o++] = cpu_load_pct;
  put_u16_le(resp.payload + o, loop_max_ms); o += 2;
  resp.payload[o++] = status.valid ? 1 : 0;
  resp.payload[o++] = display_brightness_pct;
  resp.payload[o++] = display_language;
  resp.payload[o++] = display_theme;
  resp.payload[o++] = screensaver_timeout_min;
  resp.payload[o++] = (uint8_t)ofe_status_leds.busEvent();
  resp.payload[o++] = (uint8_t)ofe_status_leds.moduleEvent();
  resp.len = o;
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
  if (fw_update_active || status.update_active) return;
  const uint64_t uid = module_uid();
  Frame resp;
  resp.dst = dst;
  resp.src = module_addr;
  resp.seq = seq;
  resp.cmd = CMD_DISCOVER_MODULES | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_DISPLAY;
  put_u64_le(resp.payload + o, uid); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = FW_MAJOR;
  resp.payload[o++] = FW_MINOR;
  resp.payload[o++] = FW_PATCH;
  put_u32_le(resp.payload + o, CAP_DISPLAY | CAP_DISPLAY_800X480 | CAP_FW_UPDATE | CAP_DISPLAY_HYBRID); o += 4;
  resp.len = o;
  bus.send(resp);
}

static void schedule_discover_response(const Frame& req) {
  if (fw_update_active || status.update_active) return;
  discover_response_dst = req.src;
  discover_response_seq = req.seq;
  discover_response_due_ms = millis() + discover_delay_ms(req);
  discover_response_pending = true;
}

static void poll_pending_discover_response() {
  if (!discover_response_pending) return;
  if ((int32_t)(millis() - discover_response_due_ms) < 0) return;
  discover_response_pending = false;
  send_discover_response(discover_response_dst, discover_response_seq);
}

static void announce_join() {
  if (fw_update_active || status.update_active) return;
  if (!join_announce_left || (int32_t)(millis() - next_join_announce_ms) < 0) return;
  send_discover_response(ADDR_MASTER, 0);
  join_announce_left--;
  next_join_announce_ms = millis() + 180UL;
}

static uint32_t join_delay_ms(uint8_t round) {
  return 80UL + (uint32_t)((module_uid() >> (round * 5U)) & 0x1FU) * 12UL;
}

static void handle_set_address_uid(const Frame& req) {
  if (req.len < 9) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  if (get_u64_le(req.payload) != module_uid()) return;
  const uint8_t next_addr = req.payload[8];
  if (!valid_module_addr(next_addr)) {
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  prefs.putUChar("addr", next_addr);
  display_resync_after_flash_write();
  send_status_response(req, STATUS_OK);
  delay(20);
  module_addr = next_addr;
}

static void handle_fw_begin(const Frame& req) {
  if (req.len < 4) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  if (fw_update_reboot_ms.load()) { send_status_response(req, STATUS_BUSY); return; }
  const uint32_t requested_size = get_u32_le(req.payload);
  if (fw_update_active) {
    if (fw_update_offset || requested_size != fw_update_size) { send_status_response(req, STATUS_BUSY); return; }
    fw_update_touch();
    send_status_response(req, STATUS_OK);
    return;
  }
  fw_update_size = requested_size;
  fw_update_offset = 0;
  fw_update_buffer_reset();
  fw_update_started_ms = millis();
  fw_update_last_draw_percent = 255;
  fw_update_last_draw_target = 255;
  fw_update_last_draw_ms = 0;

  draw_update_progress_throttled(module_addr, 0, "Preparing display firmware", true);
  if (!running_in_rs485_task()) {
    lvgl_timer_handler_profiled();
    lvgl_flush_canvas_if_dirty(true);
    delay(25);
  }

  const uint32_t free_sketch = ESP.getFreeSketchSpace();
  if (fw_update_size && fw_update_size > free_sketch) {
    fw_update_active = false;
    send_status_response(req, STATUS_BAD_VALUE);
    draw_update_progress_throttled(module_addr, 0, "Image too large", true);
    fw_update_started_ms = 0;
    return;
  }

  if (!display_wifi.beginUpdate()) {
    send_status_response(req, STATUS_BUSY);
    fw_update_started_ms = 0;
    return;
  }
  if (!Update.begin(fw_update_size ? fw_update_size : UPDATE_SIZE_UNKNOWN)) {
    display_wifi.finishUpdate();
    fw_update_active = false;
    send_status_response(req, STATUS_BUSY);
    draw_update_progress_throttled(module_addr, 0, "Begin failed", true);
    fw_update_started_ms = 0;
    return;
  }
  fw_update_active = true;
  fw_update_touch();
  fw_update_offset = 0;
  fw_update_buffer_reset();
  fw_update_started_ms = millis();
  draw_update_progress_throttled(module_addr, 0, "Receiving chunks", true);
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
  uint32_t pct = fw_update_size ? (fw_update_offset * 100UL) / fw_update_size : 0;
  if (pct > 99) pct = 99;
  const uint8_t progress = (uint8_t)pct;
  send_status_response(req, STATUS_OK);
  draw_update_progress_throttled(module_addr, progress, "Writing firmware");
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

static void handle_fw_end(const Frame& req) {
  if (fw_update_reboot_ms.load()) {
    send_status_response(req, STATUS_OK);
    return;
  }
  if (!fw_update_active) {
    send_status_response(req, STATUS_BUSY);
    return;
  }
  if (fw_update_size && fw_update_offset != fw_update_size) {
    fw_update_abort_local();
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  const bool ok = fw_update_buffer_flush() && Update.end(true);
  fw_update_active = false;
  const bool wifi_update = display_wifi.updateWireless();
  if (ok && wifi_update) fw_update_reboot_ms.store(millis());
  else display_wifi.finishUpdate();
  if (!ok) Update.abort();
  if (ok && fw_update_size) fw_update_offset = fw_update_size;
  send_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
  draw_update_progress_throttled(module_addr, ok ? 100 : 0, ok ? "Update complete - rebooting" : "Update failed", true);
  if (ok && !wifi_update) {
    delay(700);
    ESP.restart();
  }
}

static uint8_t normalized_bus_update_progress(uint8_t target, uint8_t incoming_progress) {
  uint8_t progress = incoming_progress > 100 ? 100 : incoming_progress;
  if (!update_is_local_display_target(target) && status.update_active && status.update_target == target) {
    // Some master status packets only signal "busy" and keep the progress byte
    // at 0 while the real update is still running. Do not let those packets
    // overwrite a better progress value that was already received.
    if (progress == 0 && status.update_progress > 0 && status.update_progress < 100) {
      progress = status.update_progress;
    } else if (progress > 0 && progress < 100 && status.update_progress > progress && status.update_progress < 100) {
      progress = status.update_progress;
    }
  }
  return progress;
}

static bool parse_detail_io_aliases(const Frame& req, uint16_t& p, DisplayModuleDetail& m) {
  char* aliases[5] = {m.io_main_alias, m.io_in1_alias, m.io_in2_alias, m.io_out1_alias, m.io_out2_alias};
  for (uint8_t i = 0; i < 5; ++i) {
    if (p >= req.len) return false;
    const uint8_t wire_len = req.payload[p++];
    if (p + wire_len > req.len) return false;
    uint8_t copy_len = wire_len;
    if (copy_len > 18) copy_len = 18;
    if (copy_len) memcpy(aliases[i], req.payload + p, copy_len);
    aliases[i][copy_len] = 0;
    p += wire_len;
  }
  return true;
}

static void mark_module_group_offline_from_status(uint8_t group) {
  for (uint8_t i = 0; i < module_total && i < 17; ++i) {
    if (!module_summaries[i].valid) continue;
    if (!module_summary_matches_group(module_summaries[i], group)) continue;
    module_summaries[i].flags &= (uint8_t)~0x01U;
  }
}

static void handle_display_status(const Frame& req) {
  if (req.len < 11) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  if (fw_update_active) {
    send_status_response(req, STATUS_OK);
    return;
  }

  const bool had_status = status.valid;
  const bool old_jbc_present = status.jbc_present;
  const bool old_weller_present = status.weller_present;
  const bool old_fan_present = status.fan_present;
  const uint8_t old_modules_count = status.modules_count;

  status.valid = true;
  status.output_enabled = req.payload[0] != 0;
  status.output_power = get_u16_le(req.payload + 1);
  status.work_mask = req.payload[3];
  status.afterrun_s = get_u16_le(req.payload + 4);
  status.modules_count = req.payload[6];
  status.jbc_connected = req.payload[7] != 0;
  status.weller_connected = req.payload[8] != 0;
  status.fan_rpm = get_u16_le(req.payload + 9);
  if (req.len >= 14) {
    const bool incoming_update_active = req.payload[11] != 0;
    const uint8_t incoming_update_target = req.payload[12];
    const uint8_t incoming_update_progress = normalized_bus_update_progress(incoming_update_target, req.payload[13]);
    const bool keep_snooped_foreign_update =
      !incoming_update_active && status.update_active &&
      !update_is_local_display_target(status.update_target) &&
      (bus_update_snoop_active || (status.update_progress >= 100 && bus_update_done_ms));

    if (incoming_update_active) {
      const bool target_changed = !status.update_active || status.update_target != incoming_update_target;
      status.update_active = true;
      status.update_target = incoming_update_target;
      status.update_progress = incoming_update_progress;
      if (target_changed && !update_is_local_display_target(status.update_target)) {
        bus_update_auto_nav_done = false;
        bus_update_auto_nav_target = 0;
        bus_update_last_scrolled_target = 0;
        bus_update_last_scroll_ms = 0;
      }
      if (!update_is_local_display_target(status.update_target)) {
        bus_update_copy_known_module_name(status.update_target);
        bus_update_ensure_module_row();
      }
      bus_update_touch();
    } else if (!keep_snooped_foreign_update) {
      bus_update_clear_state();
    }
  }
  if (req.len >= 32) {
    status.output_addr = req.payload[14];
    status.jbc_inputs = req.payload[15];
    status.continuous = req.payload[16];
    status.suction_level = req.payload[17];
    status.select_flow = get_u16_le(req.payload + 18);
    status.delay_work_s = get_u16_le(req.payload + 20);
    status.jbc_addr = req.payload[22];
    status.station_addr = req.payload[23];
    status.weller_speed = req.payload[24];
    status.weller_filter_status = req.payload[25];
    status.weller_filter_runtime_min = get_u16_le(req.payload + 26);
    status.weller_filter_programmed_min = get_u16_le(req.payload + 28);
    status.weller_light = req.payload[30];
    status.external_input = req.payload[31];
  }
  if (req.len >= 33) {
    const uint8_t presence = req.payload[32];
    status.jbc_present = (presence & 0x01) != 0;
    status.weller_present = (presence & 0x02) != 0;
    status.fan_present = (presence & 0x04) != 0;
    status.output_present = (presence & 0x08) != 0;
  } else {
    status.jbc_present = status.jbc_connected || status.jbc_inputs > 0;
    status.weller_present = status.weller_connected || status.weller_speed || status.weller_filter_status || status.weller_filter_programmed_min;
    status.fan_present = status.fan_rpm > 0;
    status.output_present = status.output_addr != 0;
  }
  const bool presence_changed =
    !had_status ||
    old_jbc_present != status.jbc_present ||
    old_weller_present != status.weller_present ||
    old_fan_present != status.fan_present ||
    old_modules_count != status.modules_count;

  if (presence_changed) {
    // Request a fresh list once; do not poll it continuously.
    home_module_list_refresh_pending = true;
    module_list_cache_request_start = 0;

    // DISPLAY_STATUS is authoritative right now. Invalidate stale family data
    // immediately so Home does not wait for the module-list page to be opened.
    if (!status.jbc_present) mark_module_group_offline_from_status(MODULE_JBC_BUS);
    if (!status.fan_present) {
      mark_module_group_offline_from_status(MODULE_FAN_IO);
      home_fan_io_cache.online = false;
    }
    if (!status.weller_present) {
      mark_module_group_offline_from_status(MODULE_WELLER_ZERO_SMOG);
      home_weller_cache.connected = false;
    }
  }

  if (req.len >= 52) {
    status.io_input_mask = get_u16_le(req.payload + 33);
    status.io_output_mask = get_u16_le(req.payload + 35);
    status.io_fault_mask = get_u16_le(req.payload + 37);
    status.module_output_enabled = req.payload[39];
    status.module_output_power = get_u16_le(req.payload + 40);
    status.module_output_rpm = get_u16_le(req.payload + 42);
    status.module_output_fault = get_u16_le(req.payload + 44);
    status.weller_version = get_u16_le(req.payload + 46);
    status.jbc_link_flags = req.payload[48];
    status.jbc_work_mask = req.payload[49];
    status.jbc_stand_mask = req.payload[50];
    status.route_jbc_output = req.payload[51];
    if (status.module_output_rpm) status.fan_rpm = status.module_output_rpm;
  }
  if (req.len >= 55) {
    status.delay_stand_s = get_u16_le(req.payload + 52);
    status.stand_intakes = req.payload[54];
  }

  bool selected_jbc_detail_changed = false;
  if (selected_module.valid && detail_fields_is_jbc(selected_module.type, selected_module.caps)) {
    // Identity, link and Work/Stand are module-specific detail values. The
    // aggregate status may describe another JBC module and must not replace them.
    if (selected_module.suction != status.suction_level) { selected_module.suction = status.suction_level; selected_jbc_detail_changed = true; }
    if (selected_module.select_flow != status.select_flow) { selected_module.select_flow = status.select_flow; selected_jbc_detail_changed = true; }
    if (selected_module.delay_work != status.delay_work_s) { selected_module.delay_work = status.delay_work_s; selected_jbc_detail_changed = true; }
    if (selected_module.delay_stand != status.delay_stand_s) { selected_module.delay_stand = status.delay_stand_s; selected_jbc_detail_changed = true; }
    if (selected_module.stand_intakes != status.stand_intakes) { selected_module.stand_intakes = status.stand_intakes; selected_jbc_detail_changed = true; }
    const uint8_t continuous_value = status.continuous ? 1 : 0;
    if (selected_module.continuous != continuous_value) { selected_module.continuous = continuous_value; selected_jbc_detail_changed = true; }
    if (selected_jbc_detail_changed) sync_selected_module_detail_to_home_cache();
  }

  int16_t time_pos = -1;
  if (req.len >= 7 && req.payload[req.len - 7] == 0xA6) time_pos = req.len - 7;
  const int16_t ext_end = time_pos >= 0 ? time_pos : req.len;
  bool alarm_block_seen = false;
  uint8_t parsed_alarm_count = 0;
  uint8_t parsed_item_count = 0;
  uint8_t parsed_critical_mask = 0;
  uint8_t parsed_addr[6] = {0};
  uint8_t parsed_type[6] = {0};
  uint8_t parsed_code[6] = {0};
  uint16_t parsed_value[6] = {0};
  for (int16_t i = 55; i + 4 < ext_end; ++i) {
    if (req.payload[i] == 0xA9) {
      uint8_t p = i + 1;
      parsed_alarm_count = req.payload[p++];
      parsed_item_count = req.payload[p++];
      parsed_critical_mask = req.payload[p++];
      if (parsed_item_count > 6) parsed_item_count = 6;
      if ((int16_t)p + (int16_t)parsed_item_count * 6 > ext_end) continue;
      for (uint8_t item = 0; item < parsed_item_count; ++item) {
        parsed_addr[item] = req.payload[p++];
        parsed_type[item] = req.payload[p++];
        parsed_code[item] = req.payload[p++];
        p++; // reserved
        parsed_value[item] = get_u16_le(req.payload + p); p += 2;
      }
      alarm_block_seen = true;
      break;
    }
  }
  if (alarm_block_seen) {
    status.master_alarm_valid = true;
    status.master_alarm_count = parsed_alarm_count;
    if (parsed_alarm_count == 0 || parsed_item_count > 0 || status.master_alarm_item_count == 0) {
      status.master_alarm_item_count = parsed_item_count;
      status.master_alarm_critical_mask = parsed_critical_mask;
      status.master_alarm_critical = parsed_critical_mask ? 1 : 0;
      memset(status.master_alarm_addr, 0, sizeof(status.master_alarm_addr));
      memset(status.master_alarm_type, 0, sizeof(status.master_alarm_type));
      memset(status.master_alarm_code, 0, sizeof(status.master_alarm_code));
      memset(status.master_alarm_value, 0, sizeof(status.master_alarm_value));
      memcpy(status.master_alarm_addr, parsed_addr, sizeof(parsed_addr));
      memcpy(status.master_alarm_type, parsed_type, sizeof(parsed_type));
      memcpy(status.master_alarm_code, parsed_code, sizeof(parsed_code));
      memcpy(status.master_alarm_value, parsed_value, sizeof(parsed_value));
      if (parsed_alarm_count == 0) {
        status.master_alarm_title[0] = 0;
        status.master_alarm_detail[0] = 0;
        memset(status.master_alarm_titles, 0, sizeof(status.master_alarm_titles));
        memset(status.master_alarm_details, 0, sizeof(status.master_alarm_details));
      }
    }
  }
  if (req.len >= 7 && req.payload[req.len - 7] == 0xA6) {
    const uint8_t hour = req.payload[req.len - 6];
    const uint8_t minute = req.payload[req.len - 5];
    const uint8_t day = req.payload[req.len - 4];
    const uint8_t month = req.payload[req.len - 3];
    status.clock_valid = hour < 24 && minute < 60 && day >= 1 && day <= 31 && month >= 1 && month <= 12;
    if (status.clock_valid) {
      status.clock_hour = hour;
      status.clock_minute = minute;
      status.clock_day = day;
      status.clock_month = month;
      status.clock_year = get_u16_le(req.payload + req.len - 2);
    }
  } else if (req.len >= 3 && req.payload[req.len - 3] == 0xA5) {
    const uint8_t hour = req.payload[req.len - 2];
    const uint8_t minute = req.payload[req.len - 1];
    status.clock_valid = hour < 24 && minute < 60;
    if (status.clock_valid) {
      status.clock_hour = hour;
      status.clock_minute = minute;
      status.clock_day = 0;
      status.clock_month = 0;
      status.clock_year = 0;
    }
  }

  // Important for RS485 firmware updates of OTHER modules:
  // ACK first, draw afterwards. LVGL/full-screen redraws can take long enough
  // to disturb the master's update timing if the ACK is delayed.
  send_display_status_response(req);
  parse_display_extension(req);

  // Do not touch LVGL from the RS485 task. Fresh Home data (especially Weller/FanIO)
  // is applied by the UI loop via deferred flags.
  if (running_in_rs485_task()) {
    // While sleeping, incoming Master frames must never schedule a Dashboard
    // redraw. Only the screensaver labels are refreshed in the LVGL loop.
    if (screensaver_active) {
      ui_defer_flags(UI_DEFER_APP_VALUES);
      if (status.update_active && update_is_local_display_target(status.update_target)) {
        ui_defer_update_screen(status.update_target, status.update_progress, "Bus update", false, status.update_name);
      }
      return;
    }
    // Leave the boot screen on the first real Master status.  After the frame split
    // the clock/header can be updated before Home was ever loaded, and
    // lv_update_app_values() may set have_drawn_status while the boot screen is
    // still active.  Therefore do not rely on have_drawn_status alone here.
    if (!had_status) {
      // First real Master frame leaves the boot page and builds Home once.
      // parse_display_extension() can update caches before this point, so do
      // not use have_drawn_status as the first-frame detector here.
      ui_defer_flags(UI_DEFER_DASHBOARD);
    }
    else if (display_view_mode == DISPLAY_VIEW_HOME) ui_defer_app_values_throttled(DISPLAY_STATUS_UI_MIN_INTERVAL_MS);
    if (display_view_mode == DISPLAY_VIEW_MODULE_DETAIL && selected_jbc_detail_changed) ui_defer_flags(UI_DEFER_MODULE_DETAIL);
    if (display_view_mode == DISPLAY_VIEW_ALARMS) ui_defer_app_values_throttled(DISPLAY_STATUS_UI_MIN_INTERVAL_MS);
    if (status.update_active && update_is_local_display_target(status.update_target)) {
      ui_defer_update_screen(status.update_target, status.update_progress, "Bus update", false, status.update_name);
    } else {
      if (status.update_active) refresh_bus_update_inline_ui_throttled(false);
      if (status_changed_for_draw()) ui_defer_app_values_throttled(DISPLAY_STATUS_UI_MIN_INTERVAL_MS);
    }
    return;
  }

  if (screensaver_active) {
    screensaver_update_values();
    return;
  }
  if (selected_jbc_detail_changed && display_view_mode == DISPLAY_VIEW_MODULE_DETAIL) lv_update_module_detail();
  if (status.update_active) {
    if (update_is_local_display_target(status.update_target)) {
      draw_update_progress_throttled(status.update_target, status.update_progress, "Bus update");
    } else {
      if (status.update_active) refresh_bus_update_inline_ui_throttled(false);
      if (status_changed_for_draw()) show_dashboard();
    }
  } else {
    if (status.update_active) refresh_bus_update_inline_ui_throttled(false);
    if (status_changed_for_draw()) show_dashboard();
  }
}

static void handle_display_event(const Frame& req) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_DISPLAY_EVENT | 0x80;
  resp.len = 4;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = pending_display_event;
  put_u16_le(resp.payload + 2, (uint16_t)pending_display_event_value);
  bus.send(resp);
  pending_display_event = DISPLAY_EVENT_NONE;
  pending_display_event_value = 0;
}



static void apply_display_alarm_payload(uint8_t parsed_alarm_count, uint8_t parsed_item_count, uint8_t parsed_critical_mask,
    const uint8_t* parsed_addr, const uint8_t* parsed_type, const uint8_t* parsed_code, const uint16_t* parsed_value) {
  status.master_alarm_valid = true;
  status.master_alarm_count = parsed_alarm_count;
  status.master_alarm_item_count = parsed_item_count;
  status.master_alarm_critical_mask = parsed_critical_mask;
  status.master_alarm_critical = parsed_critical_mask ? 1 : 0;
  memset(status.master_alarm_addr, 0, sizeof(status.master_alarm_addr));
  memset(status.master_alarm_type, 0, sizeof(status.master_alarm_type));
  memset(status.master_alarm_code, 0, sizeof(status.master_alarm_code));
  memset(status.master_alarm_value, 0, sizeof(status.master_alarm_value));
  if (parsed_addr) memcpy(status.master_alarm_addr, parsed_addr, sizeof(status.master_alarm_addr));
  if (parsed_type) memcpy(status.master_alarm_type, parsed_type, sizeof(status.master_alarm_type));
  if (parsed_code) memcpy(status.master_alarm_code, parsed_code, sizeof(status.master_alarm_code));
  if (parsed_value) memcpy(status.master_alarm_value, parsed_value, sizeof(status.master_alarm_value));
  if (parsed_alarm_count == 0) {
    status.master_alarm_title[0] = 0;
    status.master_alarm_detail[0] = 0;
    memset(status.master_alarm_titles, 0, sizeof(status.master_alarm_titles));
    memset(status.master_alarm_details, 0, sizeof(status.master_alarm_details));
  }
}

static void handle_display_alarms(const Frame& req) {
  if (req.len < 4 || req.payload[0] != 1) {
    send_status_response(req, req.len < 4 ? STATUS_BAD_LEN : STATUS_BAD_VALUE);
    return;
  }
  uint16_t p = 1;
  const uint8_t parsed_alarm_count = req.payload[p++];
  uint8_t parsed_item_count = req.payload[p++];
  const uint8_t parsed_critical_mask = req.payload[p++];
  if (parsed_item_count > 6) parsed_item_count = 6;
  if (p + (uint16_t)parsed_item_count * 6U > req.len) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  uint8_t parsed_addr[6] = {0};
  uint8_t parsed_type[6] = {0};
  uint8_t parsed_code[6] = {0};
  uint16_t parsed_value[6] = {0};
  for (uint8_t item = 0; item < parsed_item_count; ++item) {
    parsed_addr[item] = req.payload[p++];
    parsed_type[item] = req.payload[p++];
    parsed_code[item] = req.payload[p++];
    p++; // reserved
    parsed_value[item] = get_u16_le(req.payload + p); p += 2;
  }
  apply_display_alarm_payload(parsed_alarm_count, parsed_item_count, parsed_critical_mask,
    parsed_addr, parsed_type, parsed_code, parsed_value);
  send_status_response(req, STATUS_OK);
  if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_APP_VALUES);
  else lv_update_app_values();
}

static void handle_display_module_list(const Frame& req) {
  if (req.len < 4 || req.payload[0] != 1) {
    send_status_response(req, req.len < 4 ? STATUS_BAD_LEN : STATUS_BAD_VALUE);
    return;
  }
  uint16_t p = 1;
  const uint8_t old_total = module_total;
  const uint8_t raw_total = req.payload[p++];
  uint8_t wire_total = raw_total > 17 ? 17 : raw_total;
  const uint8_t start = req.payload[p++];
  const uint8_t count = req.payload[p++];
  if (wire_total != old_total && display_view_mode != DISPLAY_VIEW_MODULE_LIST) {
    module_list_cache_request_start = 0;
  }
  const bool other_module_update = status.update_active && !update_is_local_display_target(status.update_target);
  if (other_module_update && (wire_total == 0 || count == 0)) {
    bus_update_ensure_module_row();
    send_status_response(req, STATUS_OK);
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_LIST | UI_DEFER_BUS_UPDATE);
    else lv_update_module_list();
    return;
  }
  if (other_module_update && wire_total < module_total) wire_total = module_total;
  module_total = wire_total;
  uint8_t parsed = 0;
  bool list_changed = old_total != module_total;
  for (; parsed < count && (uint8_t)(start + parsed) < 17; ++parsed) {
    if (p + 16 > req.len) break;
    DisplayModuleSummary next_summary;
    memset(&next_summary, 0, sizeof(next_summary));
    DisplayModuleSummary& m = next_summary;
    m.valid = true;
    m.addr = req.payload[p++];
    m.type = req.payload[p++];
    m.flags = req.payload[p++];
    m.fw_major = req.payload[p++];
    m.fw_minor = req.payload[p++];
    m.fw_patch = req.payload[p++];
    uint8_t suffix_len = req.payload[p++];
    if (p + suffix_len > req.len) break;
    uint8_t suffix_copy = suffix_len;
    if (suffix_copy > sizeof(m.fw_suffix) - 1) suffix_copy = sizeof(m.fw_suffix) - 1;
    if (suffix_copy) memcpy(m.fw_suffix, req.payload + p, suffix_copy);
    m.fw_suffix[suffix_copy] = 0;
    p += suffix_len;
    if (p + 8 > req.len) break;
    m.caps = get_u32_le(req.payload + p); p += 4;
    m.uptime_s = get_u32_le(req.payload + p); p += 4;
    const uint8_t wire_name_len = req.payload[p++];
    if (p + wire_name_len > req.len) break;
    uint8_t copy_len = wire_name_len;
    if (copy_len > sizeof(m.name) - 1) copy_len = sizeof(m.name) - 1;
    memcpy(m.name, req.payload + p, copy_len);
    m.name[copy_len] = 0;
    p += wire_name_len;
    if (m.addr == ADDR_MASTER) {
      if (master_uptime_valid && m.uptime_s + 2 < last_master_uptime_s) reset_expected_modules();
      last_master_uptime_s = m.uptime_s;
      master_uptime_valid = true;
    }
    DisplayModuleSummary& current = module_summaries[start + parsed];
    if (memcmp(&current, &next_summary, sizeof(next_summary)) != 0) {
      current = next_summary;
      list_changed = true;
    }
  }
  const uint8_t next = start + parsed;
  if (display_view_mode == DISPLAY_VIEW_MODULE_LIST) {
    if (next < module_total) display_view_arg = next;
    else display_view_arg = 0;
  } else {
    module_list_cache_request_start = next < module_total ? next : 0;
  }
  if (next >= module_total) {
    for (uint8_t i = module_total; i < 17; ++i) memset(&module_summaries[i], 0, sizeof(module_summaries[i]));
    learn_expected_modules_from_current_list();
    home_module_list_refresh_pending = false;
  }
  send_status_response(req, STATUS_OK);
  if (list_changed) {
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_LIST | UI_DEFER_BUS_UPDATE | UI_DEFER_APP_VALUES);
    else {
      lv_update_module_list();
      lv_update_app_values();
    }
  }
}

static bool parse_detail_jbc_usb_core(const Frame& req, uint16_t& p, DisplayModuleDetail& m) {
  if (p + 7 > req.len) return false;
  const uint8_t version = req.payload[p++];
  if (version != 1) return false;
  DisplayJbcUsbCore& c = m.jbc_usb_core;
  memset(&c, 0, sizeof(c));
  for (uint8_t i = 0; i < 4; ++i) { c.ph_temp_c[i] = 0xFFFFU; c.ph_set_temp_c[i] = 0xFFFFU; }
  c.family = req.payload[p++];
  c.flags = req.payload[p++];
  c.station_error = get_u16_le(req.payload + p); p += 2;
  c.port_count = req.payload[p++];
  if (c.port_count > 4) return false;
  const uint8_t model_len = req.payload[p++];
  if (model_len > 8 || p + model_len > req.len) return false;
  if (model_len) memcpy(c.model, req.payload + p, model_len);
  c.model[model_len] = 0;
  p += model_len;

  if (c.family == 1) {
    if (p + (uint16_t)c.port_count * 9U > req.len) return false;
    for (uint8_t i = 0; i < c.port_count; ++i) {
      DisplayJbcUsbCorePort& port = c.ports[i];
      port.valid = (req.payload[p++] & 0x01U) != 0;
      port.state = req.payload[p++];
      port.tool = req.payload[p++];
      port.error = req.payload[p++];
      port.temp_c = get_u16_le(req.payload + p); p += 2;
      port.set_temp_c = get_u16_le(req.payload + p); p += 2;
      port.power_pct = req.payload[p++];
    }
  } else if (c.family == 2) {
    if (p + (uint16_t)c.port_count * 11U > req.len) return false;
    for (uint8_t i = 0; i < c.port_count; ++i) {
      DisplayJbcUsbCorePort& port = c.ports[i];
      port.valid = (req.payload[p++] & 0x01U) != 0;
      port.state = req.payload[p++];
      port.error = req.payload[p++];
      port.temp_c = get_u16_le(req.payload + p); p += 2;
      port.set_temp_c = get_u16_le(req.payload + p); p += 2;
      port.power_pct = req.payload[p++];
      port.flow_pct = req.payload[p++];
      port.time_to_stop_s = get_u16_le(req.payload + p); p += 2;
    }
  } else if (c.family == 3) {
    if (p + 2 > req.len) return false;
    c.cl_mode = req.payload[p++];
    c.cl_flags = req.payload[p++];
  } else if (c.family == 4) {
    if (p + 23 > req.len) return false;
    for (uint8_t i = 0; i < 4; ++i) {
      c.ph_temp_c[i] = get_u16_le(req.payload + p); p += 2;
      c.ph_set_temp_c[i] = get_u16_le(req.payload + p); p += 2;
    }
    c.ph_flags = req.payload[p++];
    c.ph_selected_power_pct = req.payload[p++];
    c.ph_heater_power_pct = req.payload[p++];
    c.ph_active_zones = req.payload[p++];
    c.ph_work_mode = req.payload[p++];
    c.ph_time_to_stop_s = get_u16_le(req.payload + p); p += 2;
  } else if (c.family == 5) {
    if (p + 6 > req.len) return false;
    c.fe_suction_level = req.payload[p++];
    c.fe_flags = req.payload[p++];
    c.fe_time_to_stop_work_raw = get_u16_le(req.payload + p); p += 2;
    c.fe_time_to_stop_stand_raw = get_u16_le(req.payload + p); p += 2;
  } else if (c.family == 6) {
    if (p + 7 > req.len) return false;
    c.sf_flags = req.payload[p++];
    c.sf_program = req.payload[p++];
    c.sf_speed_tenth_mm_s = get_u16_le(req.payload + p); p += 2;
    c.sf_length_tenth_mm = get_u16_le(req.payload + p); p += 2;
    c.sf_state = req.payload[p++];
  }
  c.valid = true;
  return true;
}

static bool parse_detail_jbc_usb_friendly(const Frame& req, uint16_t& p, DisplayModuleDetail& m) {
  if (p + 3 > req.len) return false;
  const uint8_t version = req.payload[p++];
  if (version != 1) return false;
  const uint8_t family = req.payload[p++];
  const uint8_t port_count = req.payload[p++];
  if (port_count > 4 || p + (uint16_t)port_count * 3U > req.len) return false;
  DisplayJbcUsbCore& c = m.jbc_usb_core;
  if (!c.valid || c.family != family) return false;
  for (uint8_t i = 0; i < port_count; ++i) {
    c.ports[i].valid = req.payload[p++] != 0;
    c.ports[i].tool = req.payload[p++];
    c.ports[i].error = req.payload[p++];
  }
  c.friendly_valid = true;
  return true;
}

static bool parse_display_module_detail_record(const Frame& req, uint16_t p, DisplayModuleDetail& m) {
  if (p >= req.len || !req.payload[p++]) return false;
  if (p + 63 > req.len) return false;
  memset(&m, 0, sizeof(m));
  m.weller_uart_age = 0xFFFF;
  m.valid = true;
  m.addr = req.payload[p++];
  m.type = req.payload[p++];
  m.flags = req.payload[p++];
  m.fw_major = req.payload[p++];
  m.fw_minor = req.payload[p++];
  m.fw_patch = req.payload[p++];
  uint8_t suffix_len = req.payload[p++];
  if (p + suffix_len > req.len) return false;
  uint8_t suffix_copy = suffix_len;
  if (suffix_copy > sizeof(m.fw_suffix) - 1) suffix_copy = sizeof(m.fw_suffix) - 1;
  if (suffix_copy) memcpy(m.fw_suffix, req.payload + p, suffix_copy);
  m.fw_suffix[suffix_copy] = 0;
  p += suffix_len;
  m.caps = get_u32_le(req.payload + p); p += 4;
  m.uptime_s = get_u32_le(req.payload + p); p += 4;
  m.heap_free = get_u32_le(req.payload + p); p += 4;
  m.cpu_load = req.payload[p++];
  m.loop_max_ms = get_u16_le(req.payload + p); p += 2;
  m.io_inputs = get_u16_le(req.payload + p); p += 2;
  m.io_outputs = get_u16_le(req.payload + p); p += 2;
  m.io_faults = get_u16_le(req.payload + p); p += 2;
  m.output_enabled = req.payload[p++] != 0;
  m.output_power = get_u16_le(req.payload + p); p += 2;
  m.output_rpm = get_u16_le(req.payload + p); p += 2;
  m.output_fault = get_u16_le(req.payload + p); p += 2;
  m.jbc_addr = req.payload[p++];
  m.station_addr = req.payload[p++];
  m.jbc_flags = req.payload[p++];
  m.jbc_work = req.payload[p++];
  m.jbc_stand = req.payload[p++];
  m.suction = req.payload[p++];
  m.select_flow = get_u16_le(req.payload + p); p += 2;
  m.delay_work = get_u16_le(req.payload + p); p += 2;
  m.delay_stand = get_u16_le(req.payload + p); p += 2;
  m.stand_intakes = req.payload[p++];
  m.continuous = req.payload[p++];
  m.weller_speed = req.payload[p++];
  m.weller_filter = req.payload[p++];
  m.weller_runtime = get_u16_le(req.payload + p); p += 2;
  m.weller_programmed = get_u16_le(req.payload + p); p += 2;
  m.weller_rpm = get_u16_le(req.payload + p); p += 2;
  m.weller_version = get_u16_le(req.payload + p); p += 2;
  m.weller_light = req.payload[p++];
  m.weller_uart_age = get_u16_le(req.payload + p); p += 2;
  detail_last_rx_ms = millis();
  const uint8_t wire_name_len = req.payload[p++];
  if (p + wire_name_len > req.len) return false;
  uint8_t copy_name_len = wire_name_len;
  if (copy_name_len > sizeof(m.name) - 1) copy_name_len = sizeof(m.name) - 1;
  memcpy(m.name, req.payload + p, copy_name_len);
  m.name[copy_name_len] = 0;
  p += wire_name_len;
  if (p < req.len) {
    const uint8_t wire_device_id_len = req.payload[p++];
    if (wire_device_id_len <= sizeof(m.jbc_device_id) && p + wire_device_id_len <= req.len) {
      m.jbc_device_id_len = wire_device_id_len;
      if (wire_device_id_len) memcpy(m.jbc_device_id, req.payload + p, wire_device_id_len);
      p += wire_device_id_len;
    }
  }
  if (selected_module.valid && selected_module.addr == m.addr && detail_fields_is_universal(m.type, m.caps)) {
    m.universal_descriptor_crc = selected_module.universal_descriptor_crc;
    m.universal_entity_total = selected_module.universal_entity_total;
    m.universal_entity_count = selected_module.universal_entity_count;
    memcpy(m.universal_entities, selected_module.universal_entities, sizeof(m.universal_entities));
  }
  while (p < req.len) {
    const uint8_t marker = req.payload[p++];
    if (marker == 0xB1 && p + 10 <= req.len) {
      m.filter_saturation_permille = get_u16_le(req.payload + p); p += 2;
      m.filter_pressure_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
      m.filter_zero_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
      m.filter_clean_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
      m.filter_full_raw = (int16_t)get_u16_le(req.payload + p); p += 2;
    } else if (marker == 0xB5) {
      if (!parse_detail_jbc_usb_core(req, p, m)) return false;
    } else if (marker == 0xB6) {
      if (!parse_detail_jbc_usb_friendly(req, p, m)) return false;
    } else if (marker == 0xB4) {
      if (!parse_detail_io_aliases(req, p, m)) return false;
    } else if (marker == 0xB3 && p < req.len) {
      uint8_t ip_len = req.payload[p++];
      if (p + ip_len > req.len) return false;
      uint8_t copy_len = ip_len;
      if (copy_len > sizeof(m.master_ip) - 1) copy_len = sizeof(m.master_ip) - 1;
      if (copy_len) memcpy(m.master_ip, req.payload + p, copy_len);
      m.master_ip[copy_len] = 0;
      p += ip_len;
    } else {
      break;
    }
  }
  return true;
}

static void apply_display_module_detail(DisplayModuleDetail& m) {
  if (detail_fields_is_universal(m.type, m.caps)) {
    const DisplayUniversalModuleCache* old_uc = universal_cache_find(m.addr, false);
    if (old_uc) {
      m.universal_descriptor_crc = old_uc->universal_descriptor_crc;
      m.universal_entity_total = old_uc->universal_entity_total;
      m.universal_entity_count = old_uc->universal_entity_count;
      memcpy(m.universal_entities, old_uc->universal_entities, sizeof(m.universal_entities));
    }
  }
  if (detail_fields_is_universal(m.type, m.caps)) {
    DisplayUniversalModuleCache* uc = universal_cache_find(m.addr, true);
    if (uc) {
      uc->type = m.type;
      uc->flags = m.flags;
      uc->last_ms = millis();
    }
  }
  const bool detail_changed = memcmp(&selected_module, &m, sizeof(m)) != 0;
  selected_module = m;
  sync_selected_module_detail_to_home_cache();
  if (detail_changed) {
    const int8_t summary_index = module_summary_index_by_addr(m.addr);
    if (summary_index >= 0) {
      DisplayModuleSummary& summary = module_summaries[(uint8_t)summary_index];
      summary.type = m.type;
      summary.flags = m.flags;
      summary.fw_major = m.fw_major;
      summary.fw_minor = m.fw_minor;
      summary.fw_patch = m.fw_patch;
      strncpy(summary.fw_suffix, m.fw_suffix, sizeof(summary.fw_suffix) - 1);
      summary.fw_suffix[sizeof(summary.fw_suffix) - 1] = 0;
      summary.caps = m.caps;
      summary.uptime_s = m.uptime_s;
      strncpy(summary.name, m.name, sizeof(summary.name) - 1);
      summary.name[sizeof(summary.name) - 1] = 0;
      if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_LIST);
      else lv_update_module_list();
    }
  }
  if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_DETAIL | UI_DEFER_APP_VALUES);
  else {
    lv_update_module_detail();
    lv_update_app_values();
  }
}

static void handle_display_module_detail(const Frame& req) {
  if (req.len < 2 || req.payload[0] != 1) {
    send_status_response(req, req.len < 2 ? STATUS_BAD_LEN : STATUS_BAD_VALUE);
    return;
  }
  if (req.len >= 2 && req.payload[1] == 0) {
    send_status_response(req, STATUS_OK);
    return;
  }
  DisplayModuleDetail& m = detail_parse_scratch;
  memset(&m, 0, sizeof(m));
  if (!parse_display_module_detail_record(req, 1, m)) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  send_status_response(req, STATUS_OK);
  apply_display_module_detail(m);
}

static void handle_display_detail_page(const Frame& req) {
  if (req.len < 11 || (req.payload[0] != 1 && req.payload[0] != 2)) {
    send_status_response(req, req.len < 11 ? STATUS_BAD_LEN : STATUS_BAD_VALUE);
    return;
  }
  const uint8_t detail_page_version = req.payload[0];
  uint16_t p = 1;
  const uint8_t addr = req.payload[p++];
  const uint8_t type = req.payload[p++];
  const uint8_t flags = req.payload[p++];
  const uint32_t crc = get_u32_le(req.payload + p); p += 4;
  const uint8_t total = req.payload[p++];
  const uint8_t start = req.payload[p++];
  uint8_t count = req.payload[p++];

  DisplayUniversalModuleCache* uc = universal_cache_find(addr, true);
  if (!uc) {
    send_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  if (uc->universal_descriptor_crc != crc || uc->universal_entity_total != total) {
    memset(uc->universal_entities, 0, sizeof(uc->universal_entities));
    uc->universal_entity_count = 0;
    uc->request_cursor = 0;
  }
  uc->type = type;
  uc->flags = flags;
  uc->universal_descriptor_crc = crc;
  uc->universal_entity_total = total;
  if (uc->universal_entity_count > total) uc->universal_entity_count = total;
  for (uint8_t clear_i = total; clear_i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++clear_i) {
    memset(&uc->universal_entities[clear_i], 0, sizeof(uc->universal_entities[clear_i]));
  }

  for (uint8_t i = 0; i < count; ++i) {
    if (p + 12 > req.len) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    DisplayUniversalEntity e;
    memset(&e, 0, sizeof(e));
    e.valid = true;
    e.id = req.payload[p++];
    e.type = req.payload[p++];
    e.flags = req.payload[p++];
    e.min_value = (int16_t)get_u16_le(req.payload + p); p += 2;
    e.max_value = (int16_t)get_u16_le(req.payload + p); p += 2;
    e.step_value = (int16_t)get_u16_le(req.payload + p); p += 2;
    e.value = (int16_t)get_u16_le(req.payload + p); p += 2;
    uint8_t label_len = req.payload[p++];
    if (p + label_len > req.len) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    uint8_t copy_label = label_len;
    if (copy_label > sizeof(e.label) - 1) copy_label = sizeof(e.label) - 1;
    if (copy_label) memcpy(e.label, req.payload + p, copy_label);
    e.label[copy_label] = 0;
    p += label_len;
    if (p >= req.len) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    uint8_t unit_len = req.payload[p++];
    if (p + unit_len > req.len) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    uint8_t copy_unit = unit_len;
    if (copy_unit > sizeof(e.unit) - 1) copy_unit = sizeof(e.unit) - 1;
    if (copy_unit) memcpy(e.unit, req.payload + p, copy_unit);
    e.unit[copy_unit] = 0;
    p += unit_len;
    if (p >= req.len) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    uint8_t text_len = req.payload[p++];
    if (p + text_len > req.len) {
      send_status_response(req, STATUS_BAD_LEN);
      return;
    }
    uint8_t copy_text = text_len;
    if (copy_text > sizeof(e.text) - 1) copy_text = sizeof(e.text) - 1;
    if (copy_text) memcpy(e.text, req.payload + p, copy_text);
    e.text[copy_text] = 0;
    p += text_len;
    if (detail_page_version >= 2) {
      if (p >= req.len) {
        send_status_response(req, STATUS_BAD_LEN);
        return;
      }
      uint8_t options_len = req.payload[p++];
      if (p + options_len > req.len) {
        send_status_response(req, STATUS_BAD_LEN);
        return;
      }
      uint8_t copy_options = options_len;
      if (copy_options > sizeof(e.options) - 1) copy_options = sizeof(e.options) - 1;
      if (copy_options) memcpy(e.options, req.payload + p, copy_options);
      e.options[copy_options] = 0;
      p += options_len;
    }

    const uint8_t slot = (uint8_t)(start + i);
    if (slot < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX) {
      uc->universal_entities[slot] = e;
      if (uc->universal_entity_count <= slot) uc->universal_entity_count = slot + 1;
    }
  }
  if (uc->universal_entity_count > total) uc->universal_entity_count = total;

  // The Master packs as many entities as fit in one RS485 frame. Long text/select
  // fields therefore make page sizes variable (often 1..3, not always 4). Advance
  // by the count that was actually received, otherwise entities are skipped.
  const uint8_t display_total = total > DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX ? DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX : total;
  uint8_t next_start = (uint8_t)(start + count);
  if (!count || next_start >= display_total) next_start = 0;
  uc->request_cursor = next_start;
  if (selected_module.valid && selected_module.addr == addr) {
    selected_universal_request_addr = addr;
    selected_universal_request_cursor = next_start;
  }
  uc->last_ms = millis();

  if (selected_module.valid && selected_module.addr == addr) {
    selected_module.type = type;
    selected_module.flags = flags;
    selected_module.universal_descriptor_crc = uc->universal_descriptor_crc;
    selected_module.universal_entity_total = uc->universal_entity_total;
    selected_module.universal_entity_count = uc->universal_entity_count;
    memcpy(selected_module.universal_entities, uc->universal_entities, sizeof(selected_module.universal_entities));
    sync_selected_module_detail_to_home_cache();
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MODULE_DETAIL | UI_DEFER_APP_VALUES);
    else {
      lv_update_module_detail();
      lv_update_app_values();
    }
  } else {
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_APP_VALUES);
    else lv_update_app_values();
  }

  send_status_response(req, STATUS_OK);
}

static void handle_display_update(const Frame& req) {
  if (req.len < 3) {
    send_status_response(req, STATUS_BAD_LEN);
    return;
  }
  if (fw_update_active) {
    send_status_response(req, STATUS_OK);
    return;
  }
  const bool incoming_update_active = req.payload[0] != 0;
  const uint8_t incoming_update_target = req.payload[1];
  const uint8_t incoming_update_progress = normalized_bus_update_progress(incoming_update_target, req.payload[2]);
  const bool incoming_update_done = !incoming_update_active && incoming_update_progress >= 100;
  const bool keep_snooped_foreign_update =
    !incoming_update_active && status.update_active &&
    !update_is_local_display_target(status.update_target) &&
    (bus_update_snoop_active || (status.update_progress >= 100 && bus_update_done_ms));

  if (incoming_update_active || incoming_update_done) {
    const bool target_changed = !status.update_active || status.update_target != incoming_update_target;
    status.update_active = true;
    status.update_target = incoming_update_target;
    status.update_progress = incoming_update_progress;
    if (target_changed && !update_is_local_display_target(status.update_target)) {
      bus_update_auto_nav_done = false;
      bus_update_auto_nav_target = 0;
      bus_update_last_scrolled_target = 0;
      bus_update_last_scroll_ms = 0;
    }
    status.update_name[0] = 0;
    uint8_t name_offset = 3;
    if (req.len >= 8 && req.payload[3] == 0xFF) {
      bus_update_speed_bps = get_u32_le(req.payload + 4);
      name_offset = 8;
    } else if (status.update_target == ADDR_MASTER) {
      bus_update_speed_bps = 0;
    }
    if (req.len > name_offset) {
      uint8_t name_len = req.len - name_offset;
      if (name_len > sizeof(status.update_name) - 1) name_len = sizeof(status.update_name) - 1;
      memcpy(status.update_name, req.payload + name_offset, name_len);
      status.update_name[name_len] = 0;
    } else if (!update_is_local_display_target(status.update_target)) {
      bus_update_copy_known_module_name(status.update_target);
    }
    if (!update_is_local_display_target(status.update_target)) bus_update_ensure_module_row();
    bus_update_touch();
  } else if (!keep_snooped_foreign_update) {
    bus_update_clear_state();
  }

  // Same rule as DISPLAY_STATUS: keep the bus timing clean first,
  // update the local UI only after the master has received the ACK.
  send_status_response(req, STATUS_OK);

  if (status.update_active) {
    if (update_is_local_display_target(status.update_target)) {
      draw_update_progress_throttled(status.update_target, status.update_progress, "Bus update", false, status.update_name);
    } else {
      refresh_bus_update_inline_ui();
      if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_DASHBOARD);
      else if (lvgl_ready && lv_screen_active() == ui_update_screen) show_dashboard();
    }
  } else {
    refresh_bus_update_inline_ui();
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_DASHBOARD);
    else if (lvgl_ready && lv_screen_active() == ui_update_screen) show_dashboard();
  }
}


static bool valid_screensaver_timeout(uint8_t minutes) {
  return minutes == 0 || minutes == 1 || minutes == 2 || minutes == 5 || minutes == 10;
}

static void send_display_settings_response(const Frame& req, Status s) {
  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = req.cmd | 0x80;
  resp.len = 5;
  resp.payload[0] = s;
  resp.payload[1] = display_brightness_pct;
  resp.payload[2] = display_language;
  resp.payload[3] = display_theme;
  resp.payload[4] = screensaver_timeout_min;
  bus.send(resp);
}

static void handle_display_settings(const Frame& req) {
  if (req.len != 3 && req.len != 4) {
    send_display_settings_response(req, STATUS_BAD_LEN);
    return;
  }
  if (req.payload[0] != 0xFF) set_brightness(constrain(req.payload[0], 10, 100), true);
  bool rebuild = false;
  if (req.payload[1] <= 1 && req.payload[1] != display_language) {
    display_language = req.payload[1];
    prefs.putUChar("lang", display_language);
    display_resync_after_flash_write();
    rebuild = true;
  }
  if (req.payload[2] <= 1 && req.payload[2] != display_theme) {
    display_theme = req.payload[2];
    prefs.putUChar("theme", display_theme);
    display_resync_after_flash_write();
    rebuild = true;
  }
  if (req.len >= 4 && req.payload[3] != 0xFF) {
    if (!valid_screensaver_timeout(req.payload[3])) {
      send_display_settings_response(req, STATUS_BAD_VALUE);
      return;
    }
    apply_screensaver_timeout(req.payload[3]);
  }
  if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_DISPLAY_SETTINGS);
  else lv_update_display_settings_widgets(true);
  send_display_settings_response(req, STATUS_OK);
  if (rebuild) language_rebuild_pending = true;
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
  if (display_wifi.handleConfig(req)) return;
  if (req.src == ADDR_MASTER) {
    last_master_ms = millis();
    if (running_in_rs485_task()) ui_defer_flags(UI_DEFER_MASTER_LINK);
    else if (lv_update_master_link_ui()) lv_update_app_values();
  }
  bus_update_monitor_foreign_frame(req);
  if (req.dst != module_addr && req.dst != ADDR_BROADCAST) return;
  if (req.dst == ADDR_BROADCAST && req.cmd == CMD_LED_SYNC) {
    handle_led_sync(req);
    return;
  }
  if (req.dst == ADDR_BROADCAST) {
    switch (req.cmd) {
      case CMD_DISCOVER_MODULES: schedule_discover_response(req); break;
      case CMD_SET_ADDRESS_UID: handle_set_address_uid(req); break;
      default: break;
    }
    return;
  }
  if (req.src == ADDR_MASTER) last_master_ms = millis();

  switch (req.cmd) {
    case CMD_PING: send_status_response(req, STATUS_OK); break;
    case CMD_INFO: send_info(req); break;
    case CMD_GET_CAPS: send_caps(req); break;
    case CMD_GET_TELEMETRY: send_telemetry(req); break;
    case CMD_SET_STATE: handle_display_settings(req); break;
    case CMD_SET_LABEL: handle_set_label(req); break;
    case CMD_DISPLAY_STATUS: handle_display_status(req); break;
    case CMD_DISPLAY_EVENT: handle_display_event(req); break;
    case CMD_DISPLAY_UPDATE: handle_display_update(req); break;
    case CMD_DISPLAY_DETAIL_PAGE: handle_display_detail_page(req); break;
    case CMD_DISPLAY_ALARMS: handle_display_alarms(req); break;
    case CMD_DISPLAY_MODULE_LIST: handle_display_module_list(req); break;
    case CMD_DISPLAY_MODULE_DETAIL: handle_display_module_detail(req); break;
    case CMD_SET_ADDRESS:
      if (req.len != 1 || !valid_module_addr(req.payload[0])) {
        send_status_response(req, STATUS_BAD_VALUE);
      } else {
        prefs.putUChar("addr", req.payload[0]);
        display_resync_after_flash_write();
        send_status_response(req, STATUS_OK);
        delay(20);
        module_addr = req.payload[0];
      }
      break;
    case CMD_FACTORY_RESET:
      display_wifi.save(ofe_wifi::Config());
      prefs.clear();
      display_resync_after_flash_write();
      module_addr = DEFAULT_MODULE_ADDR;
      module_label[0] = 0;
      send_status_response(req, STATUS_OK);
      show_boot_screen();
      break;
    case CMD_FW_BEGIN: handle_fw_begin(req); break;
    case CMD_FW_CHUNK: handle_fw_chunk(req); break;
    case CMD_FW_END: handle_fw_end(req); break;
    case CMD_FW_STATUS:
      handle_fw_status(req);
      break;
    case CMD_FW_ABORT:
      if (fw_update_reboot_ms.load()) { send_status_response(req, STATUS_BUSY); break; }
      fw_update_abort_local();
      send_status_response(req, STATUS_OK);
      show_status_message(update_message_text("Update aborted"));
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

static bool poll_rs485() {
  bool processed = false;
  Frame req;
  uint8_t frames = 0;
  while (frames < 6 && bus.poll(req)) {
    processed = true;
    handle_frame(req);
    ++frames;
  }
  // Keep LVGL/touch responsive even when the master is sending bursts.
  // Remaining frames are consumed on the next RS485 task/loop pass.
  if (frames >= 6) vTaskDelay(pdMS_TO_TICKS(1));
  return processed;
}


static void rs485_task(void* pv) {
  (void)pv;
  rs485_task_handle = xTaskGetCurrentTaskHandle();
  Serial.printf("RS485 task running on core %u\n", (unsigned)xPortGetCoreID());
  uint32_t last_periodic_ms = 0;
  for (;;) {
    const bool had_frames = poll_rs485();
    const uint32_t now = millis();
    if (had_frames || (uint32_t)(now - last_periodic_ms) >= DISPLAY_RS485_PERIODIC_MS) {
      poll_pending_discover_response();
      announce_join();
      last_periodic_ms = now;
    }
    vTaskDelay(pdMS_TO_TICKS(had_frames ? DISPLAY_RS485_ACTIVE_YIELD_MS : DISPLAY_RS485_IDLE_DELAY_MS));
  }
}

static void start_rs485_task_if_enabled() {
#if DISPLAY_RS485_DEDICATED_TASK
  if (rs485_task_started) return;
  BaseType_t ok = xTaskCreatePinnedToCore(
    rs485_task,
    "rs485_bus",
    DISPLAY_RS485_TASK_STACK,
    nullptr,
    DISPLAY_RS485_TASK_PRIORITY,
    &rs485_task_handle,
    DISPLAY_RS485_TASK_CORE
  );
  if (ok == pdPASS) {
    rs485_task_started = true;
  } else {
    rs485_task_handle = nullptr;
    rs485_task_started = false;
    Serial.println("RS485 task start failed; falling back to Arduino loop polling");
  }
#endif
}

static void record_loop_time(uint32_t busy_us) {
  if (busy_us > loop_max_us) loop_max_us = busy_us;
  const uint32_t now = millis();
  if ((uint32_t)(now - loop_window_ms) >= 1000UL) {
    uint32_t max_ms = (loop_max_us + 999UL) / 1000UL;
    if (max_ms > 65535UL) max_ms = 65535UL;
    loop_max_ms = (uint16_t)max_ms;
    // Cheap CPU measurement: only read the two IDLE task runtime counters.
    // No global uxTaskGetSystemState() snapshot and no task-array writes.
    sample_cpu_load();
    loop_window_ms = now;
    loop_max_us = 0;
  }
}

void setup() {
  ofe_keep_module_fw_signature();
  ofe_status_leds.begin();
  bus.setActivityCallback([]() { ofe_status_leds.pulseBusActivity(); });
  Serial.begin(115200);
  delay(300);
  backlight_off();

  Serial.printf("Display mode: RGB 800x480 SYNCFIX-v16 JC8048W550 LIVE-HOME-MODULES, %u-line internal bounce, %s LVGL flush, PCLK %u Hz, H/V idle-low=1, lv_color_t %u B\n",
                (unsigned)DISPLAY_RGB_BOUNCE_BUFFER_LINES,
                DISPLAY_LVGL_FULL_REFRESH ? "full" : "partial",
                (unsigned)DISPLAY_RGB_PCLK_HZ,
                (unsigned)sizeof(lv_color_t));

  // Reserve the DMA-critical LCD framebuffer/bounce resources before allocating
  // application caches. This guarantees that application state cannot starve
  // the RGB driver of contiguous internal SRAM.
  const bool display_ok = gfx->begin();
  display_wifi.prepareRadio();

  init_psram_caches();
  // Budget after persistent caches as well as the radio, not before them.
  if (display_ok) reserve_lvgl_draw_buffer_early(gfx->width());
  log_memory_setup();
  Serial.printf("ESP-IDF: %s\n", ESP.getSdkVersion());
  Serial.printf("CPU clock: %u MHz\n", (unsigned)getCpuFrequencyMhz());
#ifdef __OPTIMIZE__
  Serial.println("Compiler optimization: enabled (use bundled build_opt.h for -O3)");
#else
  Serial.println("Compiler optimization WARNING: optimization macro not set");
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR)
  Serial.printf("Arduino-ESP32 core: %d.%d.%d\n",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
#endif
#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_64B)
  Serial.println("RGB diagnostic: ESP32-S3 data cache line = 64 B (good for bounce-buffer mode)");
#else
  Serial.println("RGB diagnostic WARNING: build is not reporting 64-B S3 data-cache lines");
#endif
#if defined(CONFIG_SPIRAM_XIP_FROM_PSRAM)
  Serial.println("RGB diagnostic: PSRAM XIP enabled (instructions and rodata in PSRAM)");
#else
  Serial.println("RGB diagnostic WARNING: PSRAM XIP is disabled");
#endif
  Serial.printf("LVGL renderer: style cache=%u, circle cache=%u, SDK string ops=%u\n",
    (unsigned)LV_OBJ_STYLE_CACHE, (unsigned)LV_DRAW_SW_CIRCLE_CACHE_SIZE,
    (unsigned)(LV_USE_STDLIB_STRING == LV_STDLIB_CLIB));
  log_internal_ram("After gfx->begin");
  if (!display_ok) {
    Serial.println("RGB display begin failed");
    gfx->fillScreen(RGB565(120, 0, 0));
  }
  backlight_on();

  touch_reset_f1atb_sequence();
#if DISPLAY_PANEL_STATIC_TEST
  if (display_ok) {
    draw_static_panel_test();
    Serial.println("Static RGB panel test active; LVGL and RS485 are not started.");
  }
  while (true) delay(1000);
#endif

  // Hardware-visible startup marker. This uses the RGB display directly but
  // no LVGL objects, so panel/RAM failures remain distinguishable from UI failures.
  if (display_ok) {
    gfx->fillScreen(DISPLAY_RGB_INVERT_COLORS ? (uint16_t)(RGB565(12, 38, 66) ^ 0xFFFF) : RGB565(12, 38, 66));
    gfx->setTextColor(DISPLAY_RGB_INVERT_COLORS ? (uint16_t)(RGB565(235, 245, 255) ^ 0xFFFF) : RGB565(235, 245, 255));
    gfx->setTextSize(2);
    gfx->setCursor(334, 226);
    gfx->print("Booting...");
    gfx->flush(true);
  }

  prefs.begin("display", false);
  module_addr = prefs.getUChar("addr", DEFAULT_MODULE_ADDR);
  if (!valid_module_addr(module_addr)) {
    module_addr = DEFAULT_MODULE_ADDR;
    prefs.putUChar("addr", module_addr);
    display_resync_after_flash_write();
  }
  prefs.getString("label", module_label, sizeof(module_label));
  display_brightness_pct = prefs.getUChar("bright", display_brightness_pct);
  display_language = prefs.getUChar("lang", 0);
  if (display_language > 1) display_language = 0;
  display_theme = prefs.getUChar("theme", 0);
  if (display_theme > 1) display_theme = 0;
  screensaver_timeout_min = prefs.getUChar("saver", 2);
  if (screensaver_timeout_min != 0 && screensaver_timeout_min != 1 && screensaver_timeout_min != 2 &&
      screensaver_timeout_min != 5 && screensaver_timeout_min != 10) screensaver_timeout_min = 2;
  if (display_ok) lvgl_init_ui();
  log_internal_ram("After LVGL init");
  if (!lvgl_ready) Serial.println("LVGL init failed; RS485 remains active");
  else show_boot_screen();
  lvgl_force_refresh_once();
  if (lvgl_ready) {
    lv_create_app_screens();
    lv_create_status_screen();
    lv_create_screensaver_screen();
  }
  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  apply_backlight();
  last_user_activity_ms = millis();
  join_announce_left = 8;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
  display_wifi.begin(bus, module_uid(), module_addr, []() {
    pending_display_event = DISPLAY_EVENT_NONE;
    pending_display_event_value = 0;
    ui_defer_flags(UI_DEFER_MASTER_LINK | UI_DEFER_APP_VALUES);
  });
  display_boot_setup_started_ms = millis();
  start_rs485_task_if_enabled();
}

void loop() {
  const bool bus_online = last_master_ms && (uint32_t)(millis() - last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS;
  ofe_status_leds.setBusOnline(bus_online);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  ofe_status_leds.setModuleEvent(bus_online ? OFE_LED_EVENT_OFF : OFE_LED_EVENT_WARNING);
  ofe_status_leds.tick();
  const uint32_t loop_start_us = micros();
  if (!rs485_task_started) {
    poll_rs485();
    poll_pending_discover_response();
    announce_join();
  }
  fw_update_check_timeout();
  bus_update_check_ui_timeout();
  if (language_rebuild_pending && lvgl_ready && !fw_update_active) {
    language_rebuild_pending = false;
    lv_rebuild_app_ui();
  }
  if (!fw_update_active && !ui_should_hold_heavy_updates()) {
    if (lv_update_master_link_ui()) lv_update_app_values();
  }
  if (lvgl_ready) {
    if (screensaver_wake_deferred) {
      screensaver_wake_deferred = false;
      screensaver_wake();
    }
    apply_deferred_ui_updates();
    if (!fw_update_active) {
      detail_request_watchdog_tick();
      // Safety net: if the Master is online and valid Home data exists, never stay
      // on the boot screen. This runs in the LVGL loop task, so lv_screen_active()
      // and show_dashboard() are safe here.
      if (status.valid && master_link_online() && ui_boot_screen && lv_screen_active() == ui_boot_screen) {
        OfeDisplayWifiUi::closePanel();
        display_view_mode = DISPLAY_VIEW_HOME;
        display_view_arg = 0;
        show_dashboard();
      } else if (!display_boot_setup_prompted && display_boot_setup_started_ms &&
                 ui_boot_screen && lv_screen_active() == ui_boot_screen &&
                 !master_link_online() && !OfeDisplayWifiUi::isOpen() &&
                 (uint32_t)(millis() - display_boot_setup_started_ms) >= DISPLAY_FIRST_SETUP_DELAY_MS &&
                 !ofe_wifi::configured(display_wifi.view().config)) {
        lv_open_boot_connection_setup(nullptr);
      }
      screensaver_tick();
    }
    const uint32_t lvgl_now = millis();
    if ((uint32_t)(lvgl_now - lvgl_last_handler_ms) >= DISPLAY_LVGL_HANDLER_INTERVAL_MS) {
      lvgl_last_handler_ms = lvgl_now;
      lvgl_timer_handler_profiled();
      lvgl_flush_canvas_if_dirty(fw_update_active);
    }
  }
  record_loop_time((uint32_t)(micros() - loop_start_us));
  // Prevent the Arduino loop task from busy-spinning on one CPU core.
  // Placed after runtime measurement so loop_max_ms reports only real work.
  delay(1);
}
