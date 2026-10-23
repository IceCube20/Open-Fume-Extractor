#pragma once

#include <atomic>
#include <stdint.h>
#include <string.h>
#include <esp_heap_caps.h>

#ifndef OFE_SMALL_DRAW_LINES
#define OFE_SMALL_DRAW_LINES 32
#endif
#if OFE_SMALL_DRAW_LINES != 16 && OFE_SMALL_DRAW_LINES != 32 && OFE_SMALL_DRAW_LINES != 48
#error "OFE_SMALL_DRAW_LINES must be 16, 32 or 48"
#endif

namespace ofe_small_display {
inline bool validLines(uint16_t lines) {
  return lines == 16 || lines == 32 || lines == 48;
}

inline uint32_t bufferBytes(uint32_t width, uint16_t lines) {
  if (!width || !validLines(lines)) return 0;
  const uint64_t bytes = (uint64_t)width * lines * 2U;
  return bytes <= UINT32_MAX ? (uint32_t)bytes : 0;
}

inline uint8_t* allocatePsram(uint32_t width, uint16_t lines) {
  const uint32_t bytes = bufferBytes(width, lines);
  if (!bytes) return nullptr;
  return static_cast<uint8_t*>(heap_caps_aligned_alloc(
    64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

inline uint8_t* allocateBootPsram(uint32_t width, uint16_t& selected_lines) {
  selected_lines = OFE_SMALL_DRAW_LINES;
  auto* buffer = allocatePsram(width, selected_lines);
  if (!buffer && selected_lines != 16) {
    selected_lines = 16;
    buffer = allocatePsram(width, selected_lines);
  }
  return buffer;
}

// The CLI only publishes requests. Buffer replacement belongs to the GUI task.
class Profiles {
 public:
  bool request(uint16_t lines) {
    if (!validLines(lines)) return false;
    uint8_t expected = 0;
    return pending_.compare_exchange_strong(expected, (uint8_t)lines);
  }
  uint8_t take() { return pending_.exchange(0); }
  uint8_t pending() const { return pending_.load(); }
  uint16_t active() const { return active_.load(); }
  void publish(uint16_t lines) { active_.store(lines); }

 private:
  std::atomic<uint8_t> pending_{0};
  std::atomic<uint16_t> active_{0};
};

inline uint8_t commandLines(const char* command) {
  if (!command) return 0;
  if (!strcmp(command, "render b16")) return 16;
  if (!strcmp(command, "render b32")) return 32;
  if (!strcmp(command, "render b48")) return 48;
  return 0;
}
}
