#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ofe_reference {
constexpr size_t TEST_RESERVE = 32U * 1024U;
struct Selection {
  uint32_t lines;
  uint32_t wifi;
};
constexpr Selection DEFAULT_SELECTION = {28, 1};
inline bool valid(const Selection& s) {
  return (s.lines == 28 || s.lines == 48) && s.wifi <= 1;
}
inline bool command(char c, Selection& selected) {
  if (c < '1' || c > '4') return false;
  selected = DEFAULT_SELECTION;
  selected.lines = ((c - '1') & 1) ? 48 : 28;
  selected.wifi = c <= '2' ? 1 : 0;
  return true;
}
constexpr size_t drawBytes(uint32_t lines) { return 800U * lines * sizeof(uint16_t); }
inline bool fits(size_t free_bytes, size_t bytes) {
  return free_bytes >= TEST_RESERVE && bytes <= free_bytes - TEST_RESERVE;
}
}  // namespace ofe_reference
