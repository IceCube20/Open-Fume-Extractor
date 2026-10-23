// Temporary USB diagnostic firmware, NOT an OFE module or signed OTA image.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <../demos/lv_demos.h>
#include "src/OfeReferenceDefaults.h"
#include "src/OfeReferencePlan.h"
#include "src/OfeRgbTileCopy.h"
#include "src/OfeReferencePanel.h"

#if !defined(CONFIG_ESP32S3_DATA_CACHE_LINE_64B) || !defined(CONFIG_SPIRAM_XIP_FROM_PSRAM)
#error "Select the installed OFE 800x480 High Performance board/SDK"
#endif
#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 6
#error "This reference harness requires LVGL 9.6.x"
#endif
static_assert(DISPLAY_ROTATION == 2 && DISPLAY_RGB_WIDTH == 800 && DISPLAY_RGB_HEIGHT == 480,
              "Reference flush must match the production RGB geometry");

static ofe_reference::Selection selection;
static OfeEspLcdRgbDisplay panel(DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT, DISPLAY_ROTATION);
static bool ready = false;
static bool measuring = false;
static uint32_t started_ms = 0;
static uint32_t last_handler_ms = 0;
static uint64_t frames = 0, tiles = 0, pixels = 0, copy_us = 0;
static uint32_t copy_max_us = 0;

static uint32_t reference_tick() { return millis(); }

static ofe_reference::Selection reference_select_test() {
  Serial.println("REF SELECT: send 1=28 lines STA idle, 2=48 lines STA idle, 3=28 radio off, 4=48 radio off");
  Serial.println("REF WAITING: no automatic start; choose a test in the USB serial monitor");
  uint32_t reminder_ms = millis();
  for (;;) {
    ofe_reference::Selection next;
    if (Serial.available() && ofe_reference::command(static_cast<char>(Serial.read()), next)) {
      return next;
    }
    if (static_cast<uint32_t>(millis() - reminder_ms) >= 5000) {
      reminder_ms = millis();
      Serial.println("REF WAITING: send 1, 2, 3 or 4 to start; settings are not stored");
    }
    delay(5);
  }
}

static void print_memory(const char* phase) {
  constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  Serial.printf("REF MEM %s: internal=%u largest=%u min=%u psram=%u\n", phase,
    (unsigned)heap_caps_get_free_size(internal),
    (unsigned)heap_caps_get_largest_free_block(internal),
    (unsigned)heap_caps_get_minimum_free_size(internal),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void IRAM_ATTR reference_copy(uint16_t* framebuffer, const uint16_t* data,
                                     int32_t x, int32_t y, int32_t w, int32_t h) {
  ofe_rgb_tile::copy180Direct(framebuffer, DISPLAY_RGB_WIDTH, data, x, y, w, h,
                            DISPLAY_RGB_INVERT_COLORS ? 0xFFFFU : 0);
}

static void reference_flush(lv_display_t* display, const lv_area_t* area, uint8_t* data) {
  const uint32_t start = micros();
  const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
  if (w > 0 && h > 0) {
    reference_copy(panel.getFramebuffer(), reinterpret_cast<const uint16_t*>(data),
      DISPLAY_RGB_WIDTH - area->x2 - 1, DISPLAY_RGB_HEIGHT - area->y2 - 1, w, h);
    if (measuring) {
      const uint32_t elapsed = micros() - start;
      ++tiles;
      pixels += static_cast<uint64_t>(w) * h;
      copy_us += elapsed;
      if (elapsed > copy_max_us) copy_max_us = elapsed;
      if (lv_display_flush_is_last(display)) ++frames;
    }
  }
  lv_display_flush_ready(display);
}

static void reference_finished(const lv_demo_benchmark_summary_t* summary) {
  measuring = false;
  const uint32_t duration = millis() - started_ms;
  Serial.printf("REF RESULT: lines=%u radio=%s LVGL=%d.%d.%d PCLK=%u bounce=%u rotation=%u\n",
    (unsigned)selection.lines, selection.wifi ? "STA_IDLE" : "OFF",
    LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
    (unsigned)DISPLAY_RGB_PCLK_HZ, (unsigned)DISPLAY_RGB_BOUNCE_BUFFER_LINES,
    (unsigned)DISPLAY_ROTATION);
  Serial.println("REF SCENE: name | LVGL FPS | LVGL handler busy % | render ms | flush ms | samples");
  for (auto* scene = summary->scenes; scene && scene->create_cb; ++scene) {
    // LVGL summary_create() has already removed the first ignored sample.
    const uint32_t n = scene->measurement_cnt;
    if (n <= 1) {
      Serial.printf("REF SCENE: %s | INSUFFICIENT SAMPLES\n", scene->name);
      continue;
    }
    Serial.printf("REF SCENE: %s | %.2f | %.2f | %.2f | %.2f | %u\n", scene->name,
      scene->fps_avg / (double)n, scene->cpu_avg_usage / (double)n,
      scene->render_avg_time / (double)n, scene->flush_avg_time / (double)n, (unsigned)n);
  }
  if (summary->valid_scene_cnt > 0) {
    Serial.printf("REF OFFICIAL AVG: %.2f FPS, %d valid scenes\n",
      summary->total_avg_fps / (double)summary->valid_scene_cnt, (int)summary->valid_scene_cnt);
  }
  Serial.printf("REF FLUSH: suite=%u ms frames=%llu tiles/frame=%.2f copy/frame=%.2f ms copy-max=%.2f ms MPix/s=%.2f\n",
    (unsigned)duration, (unsigned long long)frames, frames ? tiles / (double)frames : 0,
    frames ? copy_us / (1000.0 * frames) : 0, copy_max_us / 1000.0,
    duration ? pixels / (1000.0 * duration) : 0);
  print_memory("finished");
  Serial.println("REF DONE. Press the hardware RESET/EN button, then choose the next test at REF SELECT. No automatic restart.");
  lv_demo_benchmark_summary_display(summary);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("OFE RGB REFERENCE v2: TEMPORARY USB TEST; no OFE bus, pairing, OTA or controls");
  selection = reference_select_test();
  Serial.printf("REF CONFIG: lines=%u bytes=%u radio=%s SDK=%s CPU=%u MHz\n",
    (unsigned)selection.lines, (unsigned)ofe_reference::drawBytes(selection.lines),
    selection.wifi ? "STA_IDLE_NOT_ASSOCIATED" : "OFF", ESP.getSdkVersion(), (unsigned)getCpuFrequencyMhz());
  if (getCpuFrequencyMhz() != 240 || !psramFound()) {
    Serial.println("REF ERROR: requires CPU 240 MHz and OPI PSRAM; no benchmark started");
    return;
  }
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  print_memory("before-panel");
  if (!panel.begin()) { Serial.println("REF ERROR: panel initialization failed"); return; }
  print_memory("after-panel");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.useStaticBuffers(false);
  if (selection.wifi) {
    if (!WiFi.mode(WIFI_STA)) { Serial.println("REF ERROR: WiFi driver init failed"); return; }
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
  } else {
    WiFi.mode(WIFI_OFF);
  }
  print_memory("after-radio");
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t bytes = ofe_reference::drawBytes(selection.lines);
  if (!ofe_reference::fits(heap_caps_get_free_size(caps), bytes)) {
    Serial.println("REF ERROR: requested SRAM tile would violate the reference-only 32 KiB reserve");
    return;
  }
  auto* draw = static_cast<uint8_t*>(heap_caps_aligned_alloc(64, bytes, caps));
  if (!draw || heap_caps_get_free_size(caps) < ofe_reference::TEST_RESERVE) {
    if (draw) heap_caps_free(draw);
    Serial.println("REF ERROR: exact requested SRAM tile unavailable; no silent fallback");
    return;
  }
  lv_init();
  for (unsigned i = 0; i < 5; ++i) {
    void* pool = heap_caps_aligned_alloc(64, LV_MEM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pool || !lv_mem_add_pool(pool, LV_MEM_SIZE)) {
      if (pool) heap_caps_free(pool);
      Serial.println("REF ERROR: PSRAM LVGL pool unavailable");
      return;
    }
  }
  lv_tick_set_cb(reference_tick);
  auto* display = lv_display_create(DISPLAY_RGB_WIDTH, DISPLAY_RGB_HEIGHT);
  if (!display) { Serial.println("REF ERROR: LVGL display creation failed"); return; }
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, reference_flush);
  lv_display_set_buffers(display, draw, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_timer_set_period(lv_display_get_refr_timer(display), 16);
  print_memory("before-benchmark");
  Serial.println("REF NOTE: official LVGL scenes; different UI/allocator/radio workload from OFE; not a 25 FPS promise");
  Serial.println("REF NOTE: LVGL busy percentage is NOT the OFE dual-core CPU measurement");
  lv_demo_benchmark_set_end_cb(reference_finished);
  started_ms = millis();
  measuring = true;
  ready = true;
  lv_demo_benchmark();
}

void loop() {
  if (ready && static_cast<uint32_t>(millis() - last_handler_ms) >= 5) {
    last_handler_ms = millis();
    lv_timer_handler();
  }
  delay(1);
}
