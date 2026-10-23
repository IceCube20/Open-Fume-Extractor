#pragma once
#ifndef OFE_JBC_DETAIL_WIRE_H
#define OFE_JBC_DETAIL_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// DISPLAY_DETAIL_PAGE v3 is opt-in through a status-response extension.
// Old displays continue receiving B5/B6 only. Both transports use this codec.
namespace ofe_jbc_detail {
enum Section : uint8_t { STATION, PORT, COUNTERS, SETTINGS, DIAGNOSTICS, SECTION_COUNT };
enum Kind : uint8_t { TEXT, NUMBER, SWITCH, SELECT };
constexpr uint8_t LEGACY_REQUEST = 0xB0, COMMAND = 0xB1, REQUEST = 0xB2, VERSION = 3;
constexpr uint8_t WINDOW = 12, LEGACY_WINDOW = 6;
static_assert(WINDOW <= 16, "The display window uses a 16-bit receipt mask");
constexpr size_t HEADER_SIZE = 15;
struct Row {
  uint8_t id = 0, kind = TEXT;
  bool writable = false;
  int16_t value = 0, minimum = 0, maximum = 0, step = 10; // tenths of displayed units
  char label[49] = {}, text[65] = {};
};
inline void copy(char* dst, size_t size, const char* src) {
  if (!size) return;
  size_t n = src ? strlen(src) : 0;
  if (n >= size) {
    n = size - 1;
    while (n && (static_cast<uint8_t>(src[n]) & 0xC0U) == 0x80U) --n;
  }
  if (n) memcpy(dst, src, n);
  dst[n] = 0;
}
inline uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
inline uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }
inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { put16(p, uint16_t(v)); put16(p + 2, uint16_t(v >> 16)); }
inline size_t encode(const Row& row, uint8_t* data, size_t size) {
  const size_t label = strlen(row.label), text = strlen(row.text), needed = 13 + label + text;
  if (needed > size) return 0;
  data[0] = row.id; data[1] = row.kind; data[2] = row.writable;
  put16(data + 3, row.value); put16(data + 5, row.minimum);
  put16(data + 7, row.maximum); put16(data + 9, row.step);
  data[11] = uint8_t(label); data[12] = uint8_t(text);
  memcpy(data + 13, row.label, label); memcpy(data + 13 + label, row.text, text);
  return needed;
}
inline bool decode(const uint8_t* data, size_t size, Row& row, size_t& used) {
  if (size < 13 || data[1] > SELECT || data[2] > 1) return false;
  const size_t label = data[11], text = data[12];
  used = 13 + label + text;
  if (label >= sizeof(row.label) || text >= sizeof(row.text) || used > size) return false;
  row = Row(); row.id = data[0]; row.kind = data[1]; row.writable = data[2];
  row.value = int16_t(u16(data + 3)); row.minimum = int16_t(u16(data + 5));
  row.maximum = int16_t(u16(data + 7)); row.step = int16_t(u16(data + 9));
  if (row.writable && (row.kind == TEXT || row.minimum > row.maximum || row.step <= 0)) return false;
  memcpy(row.label, data + 13, label); memcpy(row.text, data + 13 + label, text);
  return true;
}
}
#endif
