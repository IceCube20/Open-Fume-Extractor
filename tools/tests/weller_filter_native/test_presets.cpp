#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

struct lv_obj_t { uint16_t selected; };
struct lv_event_t { lv_obj_t* target; };
static lv_obj_t* lv_event_get_target_obj(lv_event_t* event) { return event->target; }
static uint16_t lv_dropdown_get_selected(lv_obj_t* dropdown) { return dropdown->selected; }
static struct { uint8_t addr = 0x30; } selected_module;
static constexpr uint8_t DISPLAY_EVENT_WELLER_FILTER_TIME_SET = 13;
static unsigned queued = 0;
static int16_t queued_value = 0;
static void queue_display_event(uint8_t type, int16_t value) {
  assert(type == DISPLAY_EVENT_WELLER_FILTER_TIME_SET);
  queued_value = value;
  ++queued;
}
#include "generated_presets.inc.h"

static void test(uint8_t& language, const char* (*options)(), uint8_t (*index)(uint16_t),
                 void (*event)(lv_event_t*)) {
  for (uint8_t lang = 0; lang <= 1; ++lang) {
    language = lang;
    std::istringstream input(options());
    std::vector<std::string> labels;
    for (std::string label; std::getline(input, label);) labels.push_back(label);
    assert(labels.size() == 31);
    assert(labels[0] == (lang ? "Aus" : "Off"));
    for (unsigned h = 1; h <= 23; ++h) {
      assert(labels[h] == std::to_string(h) + (lang ? (h == 1 ? " Stunde" : " Stunden") : (h == 1 ? " hour" : " hours")));
    }
    for (unsigned d = 1; d <= 6; ++d) {
      assert(labels[23 + d] == std::to_string(d) + (lang ? (d == 1 ? " Tag" : " Tage") : (d == 1 ? " day" : " days")));
    }
    assert(labels[30] == (lang ? "6 Tage 16 Stunden" : "6 days 16 hours"));
    assert(index(0) == 0);
    for (uint16_t h = 1; h <= 23; ++h) assert(index(h * 60) == h);
    for (uint16_t d = 1; d <= 6; ++d) assert(index(d * 1440) == 23 + d);
    assert(index(9600) == 30);
    assert(index(65) == 1 && index(65535) == 1);

    lv_obj_t dropdown{};
    lv_event_t selected{&dropdown};
    for (uint16_t i = 0; i <= 30; ++i) {
      dropdown.selected = i;
      queued = 0;
      event(&selected);
      assert(queued == 1);
      assert((uint16_t)queued_value >> 8 == selected_module.addr);
      const uint8_t wire = (uint8_t)queued_value;
      assert(wire == (i == 0 ? 30 : i - 1));
      const uint16_t minutes = wire == 30 ? 0 : wire == 29 ? 9600 : wire < 23 ? (wire + 1) * 60 : (wire - 22) * 1440;
      assert(index(minutes) == i);
    }
    for (uint16_t invalid : {uint16_t(31), uint16_t(255), uint16_t(65535)}) {
      dropdown.selected = invalid;
      queued = 0;
      event(&selected);
      assert(queued == 0);
    }
  }
}
int main() {
  test(DisplayModule_320x480::display_language, DisplayModule_320x480::weller_filter_options, DisplayModule_320x480::weller_filter_preset_index, DisplayModule_320x480::lv_detail_weller_filter_event);
  test(DisplayModule_800x480::display_language, DisplayModule_800x480::weller_filter_options, DisplayModule_800x480::weller_filter_preset_index, DisplayModule_800x480::lv_detail_weller_filter_event);
  test(DisplayModule_ST7796_320x480::display_language, DisplayModule_ST7796_320x480::weller_filter_options, DisplayModule_ST7796_320x480::weller_filter_preset_index, DisplayModule_ST7796_320x480::lv_detail_weller_filter_event);
  std::puts("PASS: all three production display selectors, DE/EN labels, Off first, language changes and compatible wire presets");
}
