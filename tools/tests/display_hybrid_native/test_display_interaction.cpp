#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>

#define DISPLAY_RGB_WIDTH 800
#define DISPLAY_RGB_HEIGHT 480
#define DISPLAY_ROTATION 2

static uint32_t now_ms = 1000;
static uint32_t millis() { return now_ms; }
static uint8_t report = 0, point[8] = {};
static bool status_ok = true, point_ok = true;
static unsigned acknowledgements = 0;
static bool gt911_read_reg(uint16_t reg, uint8_t* out, uint8_t len) {
  if (reg == 0x814E) {
    assert(len == 1);
    *out = report;
    return status_ok;
  }
  assert(reg == 0x8150 && len == sizeof(point));
  std::memcpy(out, point, len);
  return point_ok;
}
static bool gt911_write_reg(uint16_t reg, uint8_t* value, uint8_t len) {
  assert(reg == 0x814E && len == 1 && *value == 0);
  report = 0;
  ++acknowledgements;
  return true;
}
static void send_point(uint16_t x, uint16_t y) {
  report = 0x81;
  point[0] = uint8_t(x); point[1] = uint8_t(x >> 8);
  point[2] = uint8_t(y); point[3] = uint8_t(y >> 8);
}

static bool fw_update_active = false, lvgl_touch_pressed = false;
static uint32_t lvgl_last_touch_ms = 0;
static int* lvgl_touch_indev = nullptr;
static int* scroll_object = nullptr;
static int* lv_indev_get_scroll_obj(const int*) { return scroll_object; }

// Test the production ink-offset calculation independently of font size/baseline.
constexpr int LV_PART_MAIN = 0;
struct lv_font_t { int32_t line_height, base_line; };
struct lv_font_glyph_dsc_t { int32_t box_h, ofs_y; };
struct lv_obj_t { lv_font_t font; lv_font_glyph_dsc_t glyph; int32_t pad_top; };
static lv_obj_t* current_label = nullptr;
static uint32_t requested_symbol = 0;
static const lv_font_t* lv_obj_get_style_text_font(lv_obj_t* obj, int) {
  current_label = obj;
  return &obj->font;
}
static bool lv_font_get_glyph_dsc(const lv_font_t*, lv_font_glyph_dsc_t* g, uint32_t symbol, int) {
  requested_symbol = symbol;
  *g = current_label->glyph;
  return true;
}
static void lv_obj_set_style_pad_top(lv_obj_t* obj, int32_t top, int) { obj->pad_top = top; }

#include "generated_display_interaction.inc.h"

int main() {
  uint16_t x = 0, y = 0;
  assert(!get_touch_point(x, y));
  send_point(123, 45);
  assert(get_touch_point(x, y) && x == 676 && y == 434);
  assert(acknowledgements == 1 && report == 0);

  // A continuous two-second swipe with LVGL polling four times per report.
  for (unsigned i = 1; i <= 400; ++i) {
    now_ms += 5;
    if (i % 4 == 0) send_point(123 + i, 45);
    assert(get_touch_point(x, y));
    assert(x == 799 - (123 + (i / 4) * 4) && y == 434);
  }
  report = 0x80; // Explicit ready, zero contacts: release immediately.
  assert(!get_touch_point(x, y));
  assert(!get_touch_point(x, y));

  send_point(0, 479);
  assert(get_touch_point(x, y) && x == 799 && y == 0);
  now_ms += 10;
  status_ok = false;
  assert(get_touch_point(x, y));
  now_ms += 240;
  assert(!get_touch_point(x, y)); // A disconnected controller cannot latch press.
  status_ok = true;
  send_point(799, 0);
  assert(get_touch_point(x, y) && x == 0 && y == 479);
  now_ms += 5;
  send_point(12, 34); point_ok = false;
  assert(get_touch_point(x, y) && x == 0 && y == 479);
  point_ok = true;
  assert(get_touch_point(x, y) && x == 787 && y == 445);
  send_point(800, 480);
  assert(get_touch_point(x, y) && x == 787 && y == 445);
  report = 0x86; // Corrupt contact count is not a fresh point.
  now_ms += 250;
  assert(!get_touch_point(x, y));

  now_ms = 0xfffffff0U;
  send_point(10, 20);
  assert(get_touch_point(x, y));
  now_ms += 20;
  assert(get_touch_point(x, y));
  now_ms += 230;
  assert(!get_touch_point(x, y));

  int device = 0, scrolling = 0;
  now_ms = 5000;
  assert(!ui_should_hold_heavy_updates());
  lvgl_touch_pressed = true;
  assert(ui_should_hold_heavy_updates());
  lvgl_touch_pressed = false;
  lvgl_touch_indev = &device;
  scroll_object = &scrolling;
  assert(ui_should_hold_heavy_updates()); // Inertia outlives the 180ms grace.
  fw_update_active = true;
  assert(!ui_should_hold_heavy_updates()); // OTA must not wait for scrolling.
  fw_update_active = false;
  scroll_object = nullptr;
  lvgl_last_touch_ms = now_ms - 179;
  assert(ui_should_hold_heavy_updates());
  --lvgl_last_touch_ms;
  assert(!ui_should_hold_heavy_updates());

  for (int height = 8; height <= 19; ++height) {
    for (int offset = -3; offset <= 3; ++offset) {
      lv_obj_t label = {{20, 1}, {height, offset}, 0};
      center_icon(&label, true);
      assert(requested_symbol == 0xF1EB);
      int top = label.pad_top + 19 - height - offset;
      int bottom_gap = 20 - top - height;
      assert(top >= 0 && bottom_gap - top >= 0 && bottom_gap - top <= 1);
      label.glyph = {19, 0};
      center_icon(&label, false);
      assert(requested_symbol == 0xE000 && label.pad_top == 0);
    }
  }
  std::puts("Display interaction: continuous touch, release, I2C faults, rollover, inertia, icon centering OK");
}
