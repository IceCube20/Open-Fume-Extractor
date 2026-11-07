#include "../../../Module/DisplayModule_320x480/src/OfeDisplayUpdateExit.h"
#include <cassert>
#include <iostream>

struct lv_event_t {};
struct { bool update_active = false; uint8_t update_target = 0; } status;
static std::atomic<bool> fw_update_active{false};
static std::atomic<uint32_t> fw_update_started_ms{0}, fw_update_reboot_ms{0};
static OfeDisplayUpdateExit display_update_exit;
static bool lvgl_ready = true, back_hidden = true;
static int home = 0, result = 1, update = 2, back = 3;
static int *ui_status_screen = &result, *ui_update_screen = &update, *ui_status_back = &back;
static int* active_screen = &home;
static uint32_t now_ms = 0;
static constexpr int LV_OBJ_FLAG_HIDDEN = 1;
static uint32_t millis() { return now_ms; }
static int* lv_screen_active() { return active_screen; }
static void lv_obj_add_flag(int*, int) { back_hidden = true; }
static void lv_obj_clear_flag(int*, int) { back_hidden = false; }
static bool update_is_local_display_target(uint8_t target) { return target == 0x40; }
static void lv_show_home_event(lv_event_t*) { active_screen = &home; }
static void screensaver_wake() {}
static const char* update_message_text(const char* text) { return text; }
static void show_status_message(const char*) { active_screen = &result; }
#include "../../../Module/DisplayModule_320x480/src/OfeDisplayUpdateExitUi.inc.h"

int main() {
  using Action = OfeDisplayUpdateExit::Action;
  OfeDisplayUpdateExit exit;
  assert(exit.poll(0, false, false) == Action::None);
  assert(!exit.canReturn(false));
  exit.requestAbort();
  // Start the hold when the GUI actually displays the result, not at transport time.
  assert(exit.poll(500, false, false) == Action::ShowAborted);
  assert(exit.canReturn(false));
  assert(!exit.canReturn(true));
  assert(exit.poll(10499, false, true) == Action::None);
  assert(exit.poll(10500, false, true) == Action::Home);
  assert(exit.poll(10501, false, true) == Action::None);
  assert(!exit.canReturn(false));

  exit.requestAbort();
  assert(exit.poll(0, false, false) == Action::ShowAborted);
  assert(exit.poll(9999, false, true) == Action::None);
  assert(exit.poll(10000, false, true) == Action::Home);

  exit.requestAbort();
  assert(exit.poll(0xfffffff0U, false, false) == Action::ShowAborted);
  assert(exit.poll(0xfffffff0U + 9999U, false, true) == Action::None);
  assert(exit.poll(0xfffffff0U + 10000U, false, true) == Action::Home);

  exit.requestAbort();
  assert(exit.poll(100, true, false) == Action::None);
  assert(exit.poll(200, false, true) == Action::None); // stale abort discarded
  exit.requestAbort();
  assert(exit.poll(300, false, false) == Action::ShowAborted);
  assert(exit.poll(400, true, true) == Action::None); // retry/reboot takes priority
  assert(exit.poll(20000, false, true) == Action::None);
  exit.requestAbort();
  exit.poll(30000, false, false);
  exit.cancel(); // Back button
  assert(exit.poll(50000, false, true) == Action::None);
  exit.requestAbort();
  exit.poll(60000, false, false);
  assert(exit.poll(60001, false, false) == Action::None); // navigated away
  assert(!exit.canReturn(false));
  // Exercise the actual GUI glue with stale local bus-status and live OTA guards.
  status.update_active = true;
  status.update_target = 0x40;
  display_update_exit.requestAbort();
  now_ms = 100;
  update_exit_tick();
  assert(active_screen == &result && !back_hidden);
  now_ms = 10099;
  update_exit_tick();
  assert(active_screen == &result);
  now_ms = 10100;
  update_exit_tick();
  assert(active_screen == &home && back_hidden);
  display_update_exit.requestAbort();
  update_exit_tick();
  lv_update_abort_back_event(nullptr);
  assert(active_screen == &home && back_hidden);
  display_update_exit.requestAbort();
  update_exit_tick();
  fw_update_started_ms = 10200; // new update is preparing, before writer.begin
  lv_update_abort_back_event(nullptr);
  assert(active_screen == &result);
  update_exit_tick();
  assert(back_hidden);
  fw_update_started_ms = 0;
  display_update_exit.requestAbort();
  fw_update_active = true;
  active_screen = &update;
  update_exit_tick();
  assert(active_screen == &update && back_hidden);
  fw_update_active = false;
  fw_update_reboot_ms = 10201;
  display_update_exit.requestAbort();
  update_exit_tick();
  assert(active_screen == &update && back_hidden);
  fw_update_reboot_ms = 0;
  status.update_target = 0x20; // foreign update must retain its UI
  display_update_exit.requestAbort();
  update_exit_tick();
  assert(active_screen == &update && back_hidden);
  std::cout << "PASS: abort hold, GUI timing, manual Back, millis wrap, retry/reboot guard and navigation\n";
}
