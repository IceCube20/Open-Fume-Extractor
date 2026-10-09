#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <algorithm>

#define BACKLIGHT_PWM_BITS 12
#define ST7796_PERCEPTUAL_BACKLIGHT 1
static uint8_t display_brightness_pct = 85;
static bool screensaver_active = false;
static bool bus_task = false;
static uint32_t written_duty = 0, writes = 0, widget_updates = 0, deferred_flags = 0;
static const uint32_t UI_DEFER_DISPLAY_SETTINGS = 1U << 9;
static uint32_t screensaver_last_update_ms = 0;
static std::atomic<bool> screensaver_refresh_pending{true};
static void* screensaver_return_screen = nullptr;
static int screen;
static void* ui_screensaver_screen = &screen;
static uint32_t last_user_activity_ms = 123;
static uint32_t millis() { return 456; }
static long map(long x, long low, long high, long out_low, long out_high) {
  return (x - low) * (out_high - out_low) / (high - low) + out_low;
}
static bool running_in_rs485_task() { return bus_task; }
static void ui_defer_flags(uint32_t flags) { deferred_flags |= flags; }
static void write_backlight_duty(uint32_t duty) { written_duty = duty; ++writes; }
static void lv_update_display_settings_widgets(bool) { assert(!bus_task); ++widget_updates; }
static void display_resync_after_flash_write() {}
static bool screensaver_can_start() { return true; }
static void* lv_screen_active() { return &screen; }
static void lv_screen_load(void*) {}
static void screensaver_update_values() {}
struct PreferencesStub {
  uint8_t brightness = 85;
  unsigned saves = 0;
  void putUChar(const char*, uint8_t value) { brightness = value; ++saves; }
} prefs;

#include "generated_backlight.inc.h"

int main() {
  for (unsigned value = 0; value <= 255; ++value) {
    const uint8_t clamped = (uint8_t)std::clamp(value, 10U, 100U);
    for (bool saver : {false, true}) {
      screensaver_active = saver;
      display_brightness_pct = (uint8_t)value;
      apply_backlight();
      assert(display_brightness_pct == clamped);
      const uint8_t effective = saver && clamped > 30 ? 30 : clamped;
      assert(written_duty == backlight_duty_from_percent(effective));
    }
  }
  screensaver_active = false;
  set_brightness(85, false);
  screensaver_enter();
  assert(screensaver_active && display_brightness_pct == 85);
  assert(written_duty == backlight_duty_from_percent(30));
  assert(screensaver_last_update_ms == 456);
  assert(last_user_activity_ms == 123);

  for (uint8_t requested : {80, 20, 10, 100, 0, 255}) {
    bus_task = true; // Same receive handler is used by RS485 and WLAN.
    const auto previous_writes = writes;
    const auto previous_widgets = widget_updates;
    const auto previous_saves = prefs.saves;
    set_brightness(requested, true);
    assert(writes == previous_writes && widget_updates == previous_widgets);
    assert(deferred_flags & UI_DEFER_DISPLAY_SETTINGS);
    const uint8_t normal = (uint8_t)std::clamp((unsigned)requested, 10U, 100U);
    assert(prefs.brightness == normal && prefs.saves == previous_saves + 1);
    bus_task = false;
    process_display_settings();
    assert(writes == previous_writes + 1 && widget_updates == previous_widgets + 1);
    assert(screensaver_active && last_user_activity_ms == 123);
    assert(display_brightness_pct == normal);
    assert(written_duty == backlight_duty_from_percent(std::min(normal, (uint8_t)30)));
  }
  set_brightness(70, false); // Local controls use the same effective brightness.
  assert(screensaver_active && written_duty == backlight_duty_from_percent(30));
  const auto saves = prefs.saves;
  screensaver_wake();
  assert(!screensaver_active && display_brightness_pct == 70);
  assert(written_duty == backlight_duty_from_percent(70) && prefs.saves == saves);

  // Coalesced remote updates must apply the latest value without waking.
  screensaver_enter();
  bus_task = true;
  set_brightness(50, false);
  set_brightness(20, false);
  bus_task = false;
  process_display_settings();
  assert(screensaver_active && written_duty == backlight_duty_from_percent(20));
  screensaver_wake();
  assert(written_duty == backlight_duty_from_percent(20));

  // Waking between receive and UI processing must not reapply a stale dim value.
  screensaver_enter();
  bus_task = true;
  set_brightness(90, false);
  bus_task = false;
  screensaver_wake();
  process_display_settings();
  assert(!screensaver_active && written_duty == backlight_duty_from_percent(90));
  assert(prefs.saves == saves);
  std::puts("PASS: actual brightness setters/curves, saver cap, saved normal value, deferred bus updates, coalescing and wake ordering");
}
