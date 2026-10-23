#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "OfeSmallDisplayProfile.h"

namespace {
alignas(64) uint8_t memory[4][480 * 48 * 2];
bool used[4] = {}, fail_all = false, fail_preferred = false, gui_owner = false;
unsigned allocations = 0, frees = 0, attaches = 0, invalidations = 0, resets = 0;
void* installed = nullptr;
std::vector<std::string> logs;
}
void* heap_caps_aligned_alloc(size_t align, size_t bytes, uint32_t caps) {
  ++allocations;
  assert(align == 64 && bytes <= sizeof(memory[0]));
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (fail_all || (fail_preferred && bytes == 480U * OFE_SMALL_DRAW_LINES * 2U)) return nullptr;
  for (unsigned i = 0; i < 4; ++i) {
    if (!used[i]) { used[i] = true; return memory[i]; }
  }
  assert(false);
  return nullptr;
}
void heap_caps_free(void* p) {
  assert(p != installed);
  for (unsigned i = 0; i < 4; ++i) {
    if (p == memory[i]) { assert(used[i]); used[i] = false; ++frees; return; }
  }
  assert(false);
}
size_t heap_caps_get_free_size(uint32_t) { return 80000; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 32000; }
static void debug_printf(const char* format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  logs.emplace_back(message);
}
static void debug_println(const char* text) { logs.emplace_back(text); }
static uint32_t millis() { return 1234; }
struct FakeGfx { uint32_t width() { return 480; } } fake_gfx;
static FakeGfx* gfx = &fake_gfx;
static int fake_display;
static int* lvgl_disp = &fake_display;
static uint8_t* lvgl_draw_buf = nullptr;
static uint32_t lvgl_buf_pixels = 0;
static bool lvgl_ready = true, fw_update_active = false, screensaver_active = false;
static struct { bool update_active = false; } status;
static struct { bool active = false; } display_benchmark;
static struct { bool enabled = false; bool active() { return enabled; } } display_ble;
static ofe_small_display::Profiles small_display_profiles;
constexpr int LV_DISPLAY_RENDER_MODE_PARTIAL = 7;
static void lv_display_set_buffers(int* display, uint8_t* buf, void* second, uint32_t bytes, int mode) {
  assert(gui_owner && display == lvgl_disp && buf && !second);
  assert(mode == LV_DISPLAY_RENDER_MODE_PARTIAL && bytes >= 480 * 16 * 2);
  installed = buf;
  ++attaches;
}
static void display_benchmark_reset_counters(uint32_t now) {
  assert(gui_owner && now == 1234);
  ++resets;
}
static int* lv_screen_active() { assert(gui_owner); return &fake_display; }
static void lv_obj_invalidate(int* screen) {
  assert(gui_owner && screen == &fake_display);
  ++invalidations;
}
#include "OfeSmallDisplayProfileUi.inc.h"

static void gui_tick() {
  gui_owner = true;
  small_profile_tick();
  gui_owner = false;
}

int main() {
  using namespace ofe_small_display;
  for (auto lines : {16, 32, 48}) assert(bufferBytes(480, (uint16_t)lines) == 480U * lines * 2U);
  assert(bufferBytes(0, 32) == 0 && bufferBytes(UINT32_MAX, 48) == 0 && bufferBytes(480, 17) == 0);
  assert(commandLines("render b16") == 16 && commandLines("render b32") == 32 && commandLines("render b48") == 48);
  for (auto* command : {"render b32x", "render b32 extra", "render b999", "render -1", "render b32;reboot"})
    assert(commandLines(command) == 0);
  assert(commandLines(nullptr) == 0);

  uint16_t selected = 0;
  auto* boot = allocateBootPsram(480, selected);
  assert(boot && selected == OFE_SMALL_DRAW_LINES);
  heap_caps_free(boot);
  if constexpr (OFE_SMALL_DRAW_LINES != 16) {
    fail_preferred = true;
    boot = allocateBootPsram(480, selected);
    assert(boot && selected == 16);
    heap_caps_free(boot);
    fail_preferred = false;
  }
  fail_all = true;
  assert(!allocateBootPsram(480, selected) && selected == 16);
  fail_all = false;

  lvgl_draw_buf = allocatePsram(480, 16);
  installed = lvgl_draw_buf;
  lvgl_buf_pixels = 480 * 16;
  small_display_profiles.publish(16);
  assert(!small_display_profiles.request(17));
  assert(!small_profile_command("heap"));
  assert(small_profile_command("render") && logs.back().find("active=B16") != std::string::npos);
  assert(small_profile_command("render b32 extra") && !small_display_profiles.pending());
  lvgl_ready = false;
  assert(small_profile_command("render b32") && !small_display_profiles.pending());
  lvgl_ready = true;
  assert(small_profile_command("render b32") && small_display_profiles.pending() == 32);
  assert(small_profile_command("render b48") && small_display_profiles.pending() == 32);

  // Each busy state retains the request and does not touch the allocator/LVGL.
  bool* guards[] = {&fw_update_active, &status.update_active, &display_benchmark.active,
                   &display_ble.enabled, &screensaver_active};
  const auto allocation_before = allocations;
  for (auto guard : guards) {
    *guard = true;
    gui_tick();
    assert(small_display_profiles.pending() == 32 && allocations == allocation_before && attaches == 0);
    *guard = false;
  }
  lvgl_ready = false; gui_tick(); lvgl_ready = true;
  lvgl_disp = nullptr; gui_tick(); lvgl_disp = &fake_display;
  gfx = nullptr; gui_tick(); gfx = &fake_gfx;
  assert(small_display_profiles.pending() == 32 && allocations == allocation_before);
  gui_tick();
  assert(small_display_profiles.active() == 32 && !small_display_profiles.pending());
  assert(lvgl_buf_pixels == 480 * 32 && installed == lvgl_draw_buf);
  assert(attaches == 1 && resets == 1 && invalidations == 1);

  const auto* previous = lvgl_draw_buf;
  const auto freed_before = frees;
  fail_all = true;
  assert(small_profile_command("render b48"));
  gui_tick();
  assert(lvgl_draw_buf == previous && small_display_profiles.active() == 32);
  assert(frees == freed_before && attaches == 1 && !small_display_profiles.pending());
  fail_all = false;

  // Repeated switching must not leak buffers or free the currently installed one.
  for (unsigned i = 0; i < 20; ++i) {
    for (uint16_t lines : {uint16_t{16}, uint16_t{48}, uint16_t{32}}) {
      assert(small_display_profiles.request(lines));
      gui_tick();
      assert(small_display_profiles.active() == lines && lvgl_buf_pixels == 480U * lines);
      unsigned live = 0;
      for (auto allocated : used) if (allocated) ++live;
      assert(live == 1);
    }
  }
  installed = nullptr;
  heap_caps_free(lvgl_draw_buf);
  puts("PASS: B16/B32/B48 boot/fallback, strict CLI, deferred GUI ownership, busy guards, atomic replacement and leak-free switching");
}
