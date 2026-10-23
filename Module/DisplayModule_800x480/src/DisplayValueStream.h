#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Opt-in values only. Descriptor labels/options stay in v1/v2 cache pages.
namespace ofe_display_values {
constexpr uint8_t CAPABILITY = 0xB8, REQUEST = 0xB9, CAP_VERSION = 1, PAGE_VERSION = 4;
constexpr uint8_t MAX_ENTITIES = 32;
constexpr size_t HEADER = 10;
struct Value {
  uint8_t id = 0, type = 0;
  int16_t number = 0;
  char text[32] = {};
};
struct Request {
  bool valid = false;
  uint8_t addr = 0, count = 0, priority = 0, cursor = 0;
  uint32_t crc = 0;
  uint16_t known[MAX_ENTITIES] = {};
};
inline uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { put16(p, uint16_t(v)); put16(p + 2, uint16_t(v >> 16)); }
inline uint16_t hash(const Value& v) {
  uint16_t crc = 0xFFFF;
  auto byte = [&](uint8_t b) {
    crc ^= uint16_t(b) << 8;
    for (unsigned i = 0; i < 8; ++i) crc = crc & 0x8000 ? uint16_t(crc << 1) ^ 0x1021 : uint16_t(crc << 1);
  };
  byte(v.id); byte(v.type); byte(uint8_t(v.number)); byte(uint8_t(uint16_t(v.number) >> 8));
  // Binary values also arrive in the native hot stream, which intentionally
  // clears textual readback. Their UI text is derived from the boolean itself.
  if (v.type != 2 && v.type != 4)
    for (size_t i = 0; i < sizeof(v.text) && v.text[i]; ++i) byte(uint8_t(v.text[i]));
  return crc ? crc : 1; // zero requests a forced refresh.
}
inline size_t encodeRequest(uint8_t* out, size_t size, const Request& r) {
  const size_t bytes = 9U + size_t(r.count) * 2U;
  if (!out || !r.valid || !r.addr || r.count > MAX_ENTITIES || r.cursor >= MAX_ENTITIES || size < bytes) return 0;
  out[0] = REQUEST; out[1] = r.addr; put32(out + 2, r.crc);
  out[6] = r.count; out[7] = r.priority; out[8] = r.cursor;
  for (unsigned i = 0; i < r.count; ++i) put16(out + 9 + i * 2, r.known[i]);
  return bytes;
}
// The dispatch loop has already consumed the REQUEST marker.
inline size_t decodeRequest(const uint8_t* in, size_t size, Request& r) {
  if (!in || size < 8 || !in[0] || in[5] > MAX_ENTITIES || in[7] >= MAX_ENTITIES) return 0;
  const size_t bytes = 8U + size_t(in[5]) * 2U;
  if (size < bytes) return 0;
  Request next;
  next.valid = true; next.addr = in[0]; next.crc = u32(in + 1);
  next.count = in[5]; next.priority = in[6]; next.cursor = in[7];
  for (unsigned i = 0; i < next.count; ++i) next.known[i] = u16(in + 8 + i * 2);
  r = next;
  return bytes;
}
inline size_t encodePage(uint8_t* out, size_t size, uint8_t addr, uint8_t type, uint8_t flags,
                         uint32_t crc, const Value* values, uint8_t count, const Request& request) {
  if (!out || size < HEADER || count > MAX_ENTITIES || request.count != count ||
      !request.valid || request.addr != addr || request.crc != crc || (count && !values)) return 0;
  out[0] = PAGE_VERSION; out[1] = addr; out[2] = type; out[3] = flags; put32(out + 4, crc);
  out[8] = 0; out[9] = count ? request.cursor % count : 0;
  size_t pos = HEADER;
  uint32_t sent = 0;
  auto emit = [&](uint8_t index) {
    const Value& v = values[index];
    size_t length = 0;
    while (length < sizeof(v.text) && v.text[length]) ++length;
    if (length >= sizeof(v.text) || pos + 5 + length > size) return;
    out[pos++] = v.id; out[pos++] = v.type; put16(out + pos, uint16_t(v.number)); pos += 2;
    out[pos++] = uint8_t(length); memcpy(out + pos, v.text, length); pos += length;
    ++out[8]; out[9] = uint8_t((index + 1U) % count); sent |= uint32_t(1) << index;
  };
  // Explicit write readback wins, but rotating changed values cannot starve later IDs.
  if (request.priority)
    for (uint8_t i = 0; i < count; ++i) if (values[i].id == request.priority) { emit(i); break; }
  for (uint8_t offset = 0; offset < count; ++offset) {
    const uint8_t i = uint8_t((request.cursor + offset) % count);
    if (!(sent & (uint32_t(1) << i)) && request.known[i] != hash(values[i])) emit(i);
  }
  return pos;
}
struct Page {
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint8_t addr = 0, type = 0, flags = 0, count = 0, cursor = 0;
  uint32_t crc = 0;
};
inline bool decodePage(const uint8_t* in, size_t size, Page& page) {
  if (!in || size < HEADER || in[0] != PAGE_VERSION || !in[1] ||
      in[8] > MAX_ENTITIES || in[9] >= MAX_ENTITIES) return false;
  size_t pos = HEADER;
  uint8_t seen[32] = {};
  for (unsigned i = 0; i < in[8]; ++i) {
    if (pos + 5 > size) return false;
    const uint8_t id = in[pos], type = in[pos + 1], length = in[pos + 4];
    if (id < 20 || type < 1 || type > 7 || length > 31 || pos + 5 + length > size ||
        (seen[id / 8] & (1U << (id % 8)))) return false;
    seen[id / 8] |= uint8_t(1U << (id % 8));
    for (unsigned j = 0; j < length; ++j) if (!in[pos + 5 + j]) return false;
    pos += 5 + length;
  }
  if (pos != size) return false;
  page = {in, size, in[1], in[2], in[3], in[8], in[9], u32(in + 4)};
  return true;
}
inline size_t read(const Page& page, size_t pos, Value& value) {
  value = {};
  value.id = page.data[pos]; value.type = page.data[pos + 1];
  value.number = int16_t(u16(page.data + pos + 2));
  const uint8_t length = page.data[pos + 4];
  memcpy(value.text, page.data + pos + 5, length);
  return pos + 5 + length;
}
inline uint32_t statusSlot(unsigned displays, uint32_t maximum) {
  uint32_t slot = 200U / (displays ? displays : 1U);
  if (slot > maximum) slot = maximum;
  return slot < 50U ? 50U : slot;
}
} // namespace ofe_display_values
