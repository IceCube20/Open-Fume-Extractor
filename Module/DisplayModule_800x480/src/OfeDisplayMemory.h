#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_heap_caps.h>

namespace ofe_display_memory {
// Leave room for WiFi RX/TX, task stacks, scans and controls after startup.
// Preserve runtime headroom after the large widget tree and WiFi task start.
// 104 KiB keeps a little more post-startup headroom while the smaller 32 KiB
// internal LVGL pool gives the draw-tile selector room to step up from B20.
// Expected balance on the measured board: roughly B24-B28 with ~80 KiB runtime heap.
constexpr size_t RUNTIME_RESERVE = 104U * 1024U;
constexpr size_t STARTUP_RESERVE = 112U * 1024U;
inline bool fits(size_t free_bytes, size_t bytes, size_t reserve) {
  return free_bytes >= reserve && bytes <= free_bytes - reserve;
}
inline uint8_t* allocateDraw(size_t bytes, size_t reserve) {
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  if (!fits(heap_caps_get_free_size(caps), bytes, reserve)) return nullptr;
  auto* p = static_cast<uint8_t*>(heap_caps_aligned_alloc(64, bytes, caps));
  if (p && heap_caps_get_free_size(caps) < reserve) {
    heap_caps_free(p);
    return nullptr;
  }
  return p;
}
}

