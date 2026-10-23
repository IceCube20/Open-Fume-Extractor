#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
static uint32_t now_ms=100,perf_window_ms=1;
static uint32_t perf_frames=100,perf_flush_calls=1100,perf_flush_max_us=2500;
static uint64_t perf_pixels=24000000,perf_flush_us=1500000,perf_handler_us=6000000;
static uint32_t perf_handler_calls=120,perf_handler_max_us=70000;
static uint32_t fw_update_last_draw_ms=0;
static uint8_t fw_update_last_draw_target=255,fw_update_last_draw_percent=255;
static bool bus_task=true;
static unsigned deferred=0,immediate=0,pumps=0;
static bool forced=false;
static uint32_t millis() { return now_ms; }
static bool ui_needs_defer() { return bus_task; }
static bool update_is_local_display_target(uint8_t addr) { return addr==0x41; }
static void ui_defer_update_screen(uint8_t,uint8_t,const char*,bool force,const char*) { ++deferred; forced=force; }
static void show_update_screen(uint8_t,uint8_t,const char*,const char*) { ++immediate; }
static void fw_update_render_screen_now(bool force) { ++pumps; forced=force; }
#include "generated_rgb_performance.inc.h"
int main() {
  display_benchmark_reset_counters(12000);
  assert(perf_window_ms==12000 && !perf_frames && !perf_flush_calls && !perf_pixels);
  assert(!perf_flush_us && !perf_flush_max_us && !perf_handler_us && !perf_handler_calls && !perf_handler_max_us);
  for(bool task:{true,false}) {
    bus_task=task; deferred=immediate=pumps=0;
    now_ms=100; fw_update_last_draw_target=255;
    draw_update_progress_throttled(0x41,0,"begin",true);
    now_ms=599; draw_update_progress_throttled(0x41,1,"writing");
    assert(deferred+immediate==1);
    now_ms=600; draw_update_progress_throttled(0x41,2,"writing");
    assert(deferred+immediate==2);
    now_ms=601; draw_update_progress_throttled(0x41,100,"done");
    assert(deferred+immediate==3 && forced);
    now_ms=602; draw_update_progress_throttled(0x20,10,"remote");
    assert(deferred+immediate==4);  // Target changes are never held.
    now_ms=801; draw_update_progress_throttled(0x20,11,"remote");
    assert(deferred+immediate==4);
    now_ms=802; draw_update_progress_throttled(0x20,12,"remote");
    assert(deferred+immediate==5);
    fw_update_last_draw_target=0x41; fw_update_last_draw_ms=UINT32_MAX-199;
    now_ms=299; draw_update_progress_throttled(0x41,40,"writing");
    assert(deferred+immediate==5);
    now_ms=300; draw_update_progress_throttled(0x41,41,"writing");
    assert(deferred+immediate==6);
    assert(task ? !immediate && !pumps : !deferred && pumps==4);
  }
  puts("PASS: real RGB progress scheduling, local 2Hz/remote 5Hz, final/forced/target changes, millis rollover and benchmark counter reset");
}
