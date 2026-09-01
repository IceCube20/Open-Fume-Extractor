#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_heap_caps.h>

namespace ofe_display_memory {
// Leave room for WiFi RX/TX, task stacks, scans and controls after startup.
constexpr size_t RUNTIME_RESERVE = 64U * 1024U;
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

