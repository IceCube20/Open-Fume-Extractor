#pragma once
#include "ModuleRegistry.h"

namespace ofe_display_values {
struct SnapshotKey {
  uint64_t uid;
  uint32_t descriptor, input;
  uint8_t addr, type, language;
  bool operator==(const SnapshotKey& other) const {
    return uid == other.uid && descriptor == other.descriptor && input == other.input &&
      addr == other.addr && type == other.type && language == other.language;
  }
};
inline SnapshotKey snapshotKey(const ModuleRecord& rec, uint8_t language) {
  uint32_t input = 2166136261UL;
  auto byte = [&](uint8_t value) { input = (input ^ value) * 16777619UL; };
  byte(rec.online); byte(rec.universal_entities_valid);
  for (unsigned shift = 0; shift < 32; shift += 8) byte(uint8_t(rec.caps >> shift));
  byte(rec.universal_entity_count);
  for (unsigned i = 0; i < rec.universal_entity_count && i < ModuleRecord::UNIVERSAL_ENTITY_MAX; ++i) {
    const auto& entity = rec.universal_entities[i];
    byte(entity.id); byte(entity.len);
    for (unsigned j = 0; j < entity.len && j < sizeof(entity.data); ++j) byte(entity.data[j]);
  }
  // Poll age is not presentation input. An identical readback reuses the same
  // formatted values across displays; descriptor/language/identity never do.
  return {rec.uid, rec.universal_descriptor_crc, input, rec.addr, rec.type, language};
}
struct Snapshot {
  SnapshotKey key = {};
  Value values[MAX_ENTITIES];
  uint32_t built = 0, used = 0;
  uint8_t count = 0, flags = 0;
  bool valid = false;
};
class SnapshotCache {
 public:
  Snapshot* find(const SnapshotKey& key, uint32_t now) {
    for (auto& slot : slots_) {
      if (slot.valid && slot.key == key && uint32_t(now - slot.built) < 1000U) {
        slot.used = now;
        return &slot;
      }
    }
    return nullptr;
  }
  Snapshot& begin(const SnapshotKey& key, uint32_t now) {
    Snapshot* chosen = &slots_[0];
    for (auto& slot : slots_) {
      if (slot.key.addr == key.addr && slot.key.uid == key.uid && slot.key.language == key.language) {
        chosen = &slot; break;
      }
      if (!slot.valid) chosen = &slot;
      else if (chosen->valid && uint32_t(now - slot.used) > uint32_t(now - chosen->used)) chosen = &slot;
    }
    chosen->valid = false;
    chosen->key = key;
    chosen->built = chosen->used = now;
    return *chosen;
  }
  void commit(Snapshot& slot, uint8_t count, uint8_t flags) {
    slot.count = count; slot.flags = flags;
    slot.valid = count <= MAX_ENTITIES;
  }
 private:
  // Loop-owned, bounded storage, never a frame-sized object on loopTask's stack.
  Snapshot slots_[2];
};
} // namespace ofe_display_values
