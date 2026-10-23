#pragma once
#include "OfeJbcDetailWire.h"

namespace ofe_jbc_detail {
struct State {
  uint8_t addr = 0, section = STATION, port = 0, base = 0, total = 0, ports = 0, flags = 0;
  uint16_t loaded = 0, refreshed = 0;
  uint32_t revision = 0, request_ms = 0, command_ms = 0, started_ms = 0, command_revision = 0;
  Row rows[WINDOW];
  bool received = false, pending = false, acknowledged = false;
  uint8_t cookie = 0, result = 0xFF, control_id = 0;
  int16_t control_value = 0;
  void select(uint8_t target, uint8_t category, uint8_t selected_port, uint8_t first = 0) {
    if (pending || category >= SECTION_COUNT || selected_port >= 4) return;
    addr = target; section = category; port = selected_port; base = first;
    total = 0; loaded = 0; refreshed = 0; revision = 0; request_ms = 0; started_ms = 0; received = false; result = 0xFF;
    memset(rows, 0, sizeof(rows));
  }
  bool queue(uint8_t slot, int16_t value, uint32_t now) {
    if (pending || !revision || slot >= WINDOW || !(loaded & (1U << slot)) || (flags & 3) != 3) return false;
    const Row& row = rows[slot];
    if (!row.writable || value < row.minimum || value > row.maximum) return false;
    control_id = row.id; control_value = value; pending = true; acknowledged = false; command_ms = now; command_revision = revision;
    if (!++cookie) ++cookie;
    result = 0xFF; request_ms = 0;
    return true;
  }
  size_t request(uint8_t* out, size_t size, uint32_t now) {
    if (!addr || size < 5) return 0;
    if (!started_ms) started_ms = now ? now : 1;
    if (pending && uint32_t(now - command_ms) >= 15000) { pending = false; result = 2; }
    uint8_t count = !received ? WINDOW : total > base ? (total - base < WINDOW ? total - base : WINDOW) : 0;
    const uint16_t mask = uint16_t((1UL << count) - 1UL);
    const bool complete = received && (refreshed & mask) == mask;
    if (!pending && complete && request_ms && uint32_t(now - request_ms) < 1500) return 0;
    if (complete) refreshed = 0;
    uint8_t next = base;
    for (uint8_t i = 0; i < count; ++i) if (!(refreshed & (1U << i))) { next = base + i; break; }
    out[0] = REQUEST; out[1] = addr; out[2] = section; out[3] = port; out[4] = next;
    size_t length = 5;
    if (pending && !acknowledged && size >= 15) {
      out[5] = COMMAND; out[6] = addr; out[7] = control_id; put16(out + 8, control_value);
      put32(out + 10, command_revision); out[14] = cookie; length = 15;
    }
    request_ms = now;
    return length;
  }
  bool accept(const uint8_t* data, size_t size) {
    if (size < HEADER_SIZE || data[0] != VERSION || data[1] != addr || data[2] != section ||
        data[3] != port || data[12] > 4 || data[14] > WINDOW ||
        uint16_t(data[4]) + data[14] > data[5] || (!data[14] && data[5])) return false;
    const uint8_t start = data[4], count = data[14], new_total = data[5];
    const uint32_t generation = u32(data + 6);
    const bool reset = generation != revision || new_total != total;
    const uint8_t new_base = base >= new_total ? 0 : base;
    if (start < new_base || uint16_t(start) + count > uint16_t(new_base) + WINDOW) return false;
    Row scratch[WINDOW]; size_t cursor = HEADER_SIZE;
    for (uint8_t i = 0; i < count; ++i) {
      size_t used = 0;
      if (!decode(data + cursor, size - cursor, scratch[i], used)) return false;
      cursor += used;
    }
    if (cursor != size) return false;
    if (port && port >= data[12] && (section == PORT || section == COUNTERS || section == SETTINGS)) {
      // A four-port station may be replaced by a one-port cleaner/preheater.
      // Do not leave an invisible, unselectable old port as the request target.
      pending = false;
      select(addr, section, 0);
      ports = data[12];
      return true;
    }
    if (reset) { loaded = 0; refreshed = 0; memset(rows, 0, sizeof(rows)); }
    base = new_base; total = new_total; revision = generation; ports = data[12]; flags = data[13]; received = true;
    // A queued change belongs to the original station, never the next one.
    if (pending && generation != command_revision) { pending = false; result = 2; }
    if (pending && data[10] == cookie && data[11] != 0xFF) {
      acknowledged = true; result = data[11]; request_ms = 0;
      if (result != 0) pending = false;
    }
    for (uint8_t i = 0; i < count; ++i) { const uint8_t slot = start + i - base; rows[slot] = scratch[i]; loaded |= 1U << slot; refreshed |= 1U << slot; }
    if (pending && acknowledged) {
      for (uint8_t i = 0; i < WINDOW; ++i) if ((loaded & (1U << i)) && rows[i].id == control_id) {
        // FE's native 450..580 flow range quantizes public percent setpoints.
        const int tolerance = control_id == 68 ? 7 : 0;
        const int difference = int(rows[i].value) - control_value;
        if (rows[i].writable && difference >= -tolerance && difference <= tolerance) pending = false;
      }
    }
    return true;
  }
};
}
