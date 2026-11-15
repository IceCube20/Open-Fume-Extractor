#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ofe_home_metrics {
constexpr uint8_t CAPABILITY = 0xB7; // 0xB0 is the legacy JBC detail request.
constexpr uint8_t VERSION = 1;
constexpr uint8_t KIND = 4;
constexpr size_t WIRE_SIZE = 23;
enum Flag : uint16_t {
  ONLINE = 1, HAS_OUTPUT = 2, OUTPUT_VALID = 4, OUTPUT_ON = 8,
  HAS_POWER = 16, HAS_RPM = 32, HAS_FILTER = 64, HAS_PRESSURE = 128, HAS_TIME = 256,
  HAS_INPUT = 512, INPUT_ON = 1024, LINK_KNOWN = 2048, LINKED = 4096
};
struct State {
  uint8_t addr = 0, type = 0;
  uint16_t flags = 0, power = 0, rpm = 0, faults = 0, saturation = 0;
  int16_t pressure = 0;
  uint32_t runtime = 0, lifetime = 0;
  uint8_t filter = 255;
};
inline void put16(uint8_t* p, uint16_t n) { p[0] = uint8_t(n); p[1] = uint8_t(n >> 8); }
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline void put32(uint8_t* p, uint32_t n) { put16(p, uint16_t(n)); put16(p + 2, uint16_t(n >> 16)); }
inline uint32_t get32(const uint8_t* p) { return uint32_t(get16(p)) | uint32_t(get16(p + 2)) << 16; }
inline bool encode(uint8_t* out, size_t size, const State& s) {
  if (!out || size < WIRE_SIZE) return false;
  out[0] = s.addr; out[1] = s.type; put16(out + 2, s.flags);
  put16(out + 4, s.power); put16(out + 6, s.rpm); put16(out + 8, s.faults);
  put16(out + 10, s.saturation); put16(out + 12, uint16_t(s.pressure));
  put32(out + 14, s.runtime); put32(out + 18, s.lifetime); out[22] = s.filter;
  return true;
}
inline bool decode(const uint8_t* in, size_t size, State& s) {
  if (!in || size < WIRE_SIZE) return false;
  State next;
  next.addr = in[0]; next.type = in[1]; next.flags = get16(in + 2);
  next.power = get16(in + 4); next.rpm = get16(in + 6); next.faults = get16(in + 8);
  next.saturation = get16(in + 10); next.pressure = int16_t(get16(in + 12));
  next.runtime = get32(in + 14); next.lifetime = get32(in + 18); next.filter = in[22];
  if (!next.addr || next.power > 1000 || next.saturation > 1000) return false;
  s = next;
  return true;
}
inline uint32_t remaining(const State& s) { return s.runtime >= s.lifetime ? 0 : s.lifetime - s.runtime; }
struct Entry { State state; uint32_t received = 0; bool valid = false; };
struct Cache {
  Entry entries[17];
  void clear() { for (auto& e : entries) e = {}; }
  bool put(const State& s, uint32_t now) {
    Entry* target = nullptr;
    for (auto& e : entries) if (e.valid && e.state.addr == s.addr) { target = &e; break; }
    if (!target) for (auto& e : entries) if (!e.valid) { target = &e; break; }
    if (!target) {
      target = entries;
      for (auto& e : entries) if (uint32_t(now - e.received) > uint32_t(now - target->received)) target = &e;
    }
    const bool changed = !target->valid || target->state.addr != s.addr || target->state.type != s.type ||
      uint32_t(now - target->received) > 10000U ||
      target->state.flags != s.flags || target->state.power != s.power || target->state.rpm != s.rpm ||
      target->state.faults != s.faults || target->state.filter != s.filter ||
      target->state.saturation != s.saturation || target->state.pressure != s.pressure ||
      target->state.runtime != s.runtime || target->state.lifetime != s.lifetime;
    target->state = s; target->received = now; target->valid = true;
    return changed;
  }
  bool get(uint8_t addr, uint8_t type, State& s, uint32_t& received) const {
    for (const auto& e : entries) if (e.valid && e.state.addr == addr && e.state.type == type) {
      s = e.state; received = e.received; return true;
    }
    return false;
  }
};
} // namespace ofe_home_metrics

