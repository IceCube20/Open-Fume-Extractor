#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using lv_color_t = uint32_t;
struct lv_obj_t {
  char text[24]{};
  uint32_t color = 0;
  uint32_t glyph = 0;
  bool visible = false;
};
static lv_obj_t labels[2];
static lv_obj_t* ui_header_alarm_labels[] = {&labels[0], &labels[1]};
static uint8_t ui_header_link_count = 2, active_alarm_count = 0;
static struct { uint8_t master_alarm_critical_mask = 0; } status;
static bool online = true;
static bool master_link_online() { return online; }
static uint32_t lv_color_hex(uint32_t value) { return value; }
static bool lv_obj_is_valid(lv_obj_t* obj) { return obj != nullptr; }
static void lv_set_text_c(lv_obj_t* obj, const char* value) { strcpy_s(obj->text, value); }
static void lv_set_text_color_if_changed(lv_obj_t* obj, uint32_t value, int) { obj->color = value; }
static void lv_set_visible(lv_obj_t* obj, bool value) { obj->visible = value; }
#define LV_SYMBOL_BELL "bell"
#define LV_SYMBOL_OK "check"
namespace ofe_display_header {
static void center_glyph(lv_obj_t* obj, uint32_t value) { obj->glyph = value; }
}
#include "alarm_headers.h"

int main() {
  for (auto update : {display0::lv_update_alarm_header_ui, display1::lv_update_alarm_header_ui,
                      display2::lv_update_alarm_header_ui, display3::lv_update_alarm_header_ui}) {
    const auto check = [&](bool connected, uint8_t count, uint8_t mask, uint32_t color) {
      online = connected;
      active_alarm_count = count;
      status.master_alarm_critical_mask = mask;
      update();
      for (const auto& label : labels) {
        assert(label.visible);
        assert(label.color == color);
        assert(label.glyph == (count ? 0xF0F3U : 0xF00CU));
      }
    };
    check(true, 1, 0, 0xFFB020);  // Ordinary warning.
    check(false, 1, 0, 0xFF4D5E); // Lost link, even without a remote critical bit.
    check(true, 0, 0, 0x55D98A);  // Recovery clears the local alarm.
    check(true, 2, 2, 0xFF4D5E);  // Remote critical alarms remain red.
    check(true, 0, 2, 0x55D98A);  // An old mask alone must not raise an alarm.
    check(true, 1, 0, 0xFFB020);  // Recovery to a noncritical warning.
  }
  puts("PASS: four actual display alarm headers: warning, link loss, recovery and critical alarms");
}
