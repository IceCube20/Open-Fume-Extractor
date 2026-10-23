#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_heap_caps.h>
#include "OfeP4PpaConfig.h"

namespace ofe_display_memory {
// Leave room for WiFi RX/TX, task stacks, scans and controls after startup.
// Preserve runtime headroom after the large widget tree and WiFi task start.
// The optional PSRAM widget-pool trial must pass these same allocation guards;
// it must not obtain larger draw tiles by borrowing radio/runtime headroom.
constexpr size_t RUNTIME_RESERVE = 104U * 1024U;
constexpr size_t STARTUP_RESERVE = 112U * 1024U;
inline bool fits(size_t free_bytes, size_t bytes, size_t reserve) {
  return free_bytes >= reserve && bytes <= free_bytes - reserve;
}
inline uint8_t* allocateDraw(size_t bytes, size_t reserve) {
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  if (!fits(heap_caps_get_free_size(caps), bytes, reserve)) return nullptr;
  auto* p = static_cast<uint8_t*>(heap_caps_aligned_alloc(OFE_P4_DRAW_ALIGNMENT, bytes, caps));
  if (p && heap_caps_get_free_size(caps) < reserve) {
    heap_caps_free(p);
    return nullptr;
  }
  return p;
}
}

