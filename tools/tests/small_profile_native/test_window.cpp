#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>

static uint32_t now_ms = 100;
static uint32_t millis() { return now_ms; }
static uint32_t perf_window_start_ms = 0, perf_lvgl_frames = 0, perf_lvgl_tiles = 0;
static uint64_t perf_lvgl_pixels = 0, perf_canvas_draw_us = 0, perf_handler_us = 0;
static uint32_t perf_canvas_draw_max_us = 0, perf_handler_calls = 0, perf_handler_max_us = 0;
static uint32_t perf_panel_frames = 0, perf_panel_flush_max_us = 0;
static uint64_t perf_panel_flush_us = 0, perf_panel_bytes = 0;
static struct { bool active = false; } display_benchmark;
static constexpr uint32_t PERF_REPORT_INTERVAL_MS = 5000, PERF_PANEL_FRAME_BYTES = 307200;
static std::string report;
static void debug_printf(const char* fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  report = buffer;
}
struct lv_event_t {};
static int display_view_mode = 0, ui_system_screen = 1;
static uint32_t last_user_activity_ms = 0;
static constexpr int DISPLAY_VIEW_SYSTEM = 4;
static void lv_update_display_settings_widgets(bool force) { assert(force); }
static void lv_screen_switch(int screen) { assert(screen == ui_system_screen); }
#include "generated_small_window.inc.h"

static void seed_benchmark() {
  display_benchmark.active = true;
  perf_window_start_ms = now_ms - 12000U;
  perf_lvgl_frames = perf_panel_frames = 180;
  perf_lvgl_tiles = 936;
  perf_lvgl_pixels = 180U * 76800U;
  perf_canvas_draw_us = 300000;
  perf_canvas_draw_max_us = 5800;
  perf_handler_us = 7596000;
  perf_handler_calls = 180;
  perf_handler_max_us = 60000;
  perf_panel_flush_us = 3330000;
  perf_panel_flush_max_us = 20000;
  perf_panel_bytes = 180ULL * 307200;
}
static void assert_reset() {
  assert(perf_window_start_ms == now_ms && !perf_lvgl_frames && !perf_panel_frames);
  assert(!perf_lvgl_tiles && !perf_lvgl_pixels && !perf_canvas_draw_us && !perf_canvas_draw_max_us);
  assert(!perf_handler_us && !perf_handler_calls && !perf_handler_max_us);
  assert(!perf_panel_flush_us && !perf_panel_flush_max_us);
#if DISPLAY_TEST_SPI
  assert(!perf_panel_bytes);
#endif
}
int main() {
  now_ms = 13000;
  seed_benchmark();
  perf_report_if_due();
  assert(report.empty()); // No periodic report during the benchmark.
  display_benchmark.active = false;
  display_benchmark_reset_counters(now_ms); // Verified production finish call site.
  assert_reset();

  // Ten NEW frames in five seconds must be 2 FPS, not (180+10)/5 = 38 FPS.
  perf_lvgl_frames = perf_panel_frames = 10;
  perf_lvgl_tiles = 20;
  perf_lvgl_pixels = 10U * 480U * 16U;
  perf_handler_us = 10000; perf_handler_calls = 10;
  perf_panel_flush_us = 185000; perf_panel_bytes = 10ULL * 307200;
  now_ms += PERF_REPORT_INTERVAL_MS - 1;
  perf_report_if_due(); assert(report.empty());
  ++now_ms; perf_report_if_due();
  assert(report.find("LVGL 2.0 fps | panel 2.0 fps") != std::string::npos);
  assert_reset();
  report.clear();

  // Back/cancel must reset the same window, including around millis rollover.
  for (uint32_t stamp : {uint32_t{24000}, UINT32_MAX - 10U, uint32_t{20}}) {
    now_ms = stamp;
    seed_benchmark();
    display_benchmark_back_event(nullptr);
    assert(!display_benchmark.active && display_view_mode == DISPLAY_VIEW_SYSTEM);
    assert(last_user_activity_ms == now_ms);
    assert_reset();
    now_ms += PERF_REPORT_INTERVAL_MS;
    perf_report_if_due();
    assert(report.find("LVGL 0.0 fps | panel 0.0 fps") != std::string::npos);
    assert_reset();
    report.clear();
  }
  puts("PASS: actual small-display reset/report/cancel helpers, benchmark boundary, fresh FPS window, SPI bytes and rollover");
}
