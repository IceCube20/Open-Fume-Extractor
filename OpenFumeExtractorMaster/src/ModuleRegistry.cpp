#include "ModuleRegistry.h"
#include <esp_heap_caps.h>

static void swap_module_records_small_stack(ModuleRecord& a, ModuleRecord& b) {
  // Do not put a complete ModuleRecord (~5 KiB with descriptor cache) on the
  // Arduino loopTask stack. Swap in small fixed chunks instead.
  uint8_t tmp[64];
  uint8_t* pa = reinterpret_cast<uint8_t*>(&a);
  uint8_t* pb = reinterpret_cast<uint8_t*>(&b);
  for (size_t off = 0; off < sizeof(ModuleRecord); off += sizeof(tmp)) {
    size_t n = sizeof(ModuleRecord) - off;
    if (n > sizeof(tmp)) n = sizeof(tmp);
    memcpy(tmp, pa + off, n);
    memcpy(pa + off, pb + off, n);
    memcpy(pb + off, tmp, n);
  }
}

bool ModuleRegistry::begin() {
  if (records_) return true;
  records_ = static_cast<ModuleRecord*>(
      heap_caps_calloc(MAX_MODULES, sizeof(ModuleRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  records_psram_ = records_ != nullptr;

  // Keep a safe fallback for boards/builds without usable PSRAM. This preserves
  // functionality, but PSRAM is strongly preferred for the 4 KiB descriptors.
  if (!records_) {
    records_ = static_cast<ModuleRecord*>(
        heap_caps_calloc(MAX_MODULES, sizeof(ModuleRecord), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    records_psram_ = false;
  }
  count_ = 0;
  return records_ != nullptr;
}

void ModuleRegistry::clear() {
  count_ = 0;
  if (records_) memset(records_, 0, sizeof(ModuleRecord) * MAX_MODULES);
}

ModuleRecord* ModuleRegistry::find(uint8_t addr) {
  if (!records_) return nullptr;
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].addr == addr) return &records_[i];
  }
  return nullptr;
}

const ModuleRecord* ModuleRegistry::find(uint8_t addr) const {
  if (!records_) return nullptr;
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].addr == addr) return &records_[i];
  }
  return nullptr;
}

ModuleRecord* ModuleRegistry::upsert(uint8_t addr) {
  if (!records_) return nullptr;
  ModuleRecord* existing = find(addr);
  if (existing) return existing;
  if (count_ >= MAX_MODULES) return nullptr;

  uint8_t pos = count_;
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].addr > addr) {
      pos = i;
      break;
    }
  }

  for (uint8_t i = count_; i > pos; --i) records_[i] = records_[i - 1];
  ModuleRecord& rec = records_[pos];
  memset(&rec, 0, sizeof(rec));
  rec.addr = addr;
  ++count_;
  return &rec;
}

void ModuleRegistry::removeAt(uint8_t index) {
  if (!records_ || index >= count_) return;
  for (uint8_t i = index + 1; i < count_; ++i) {
    records_[i - 1] = records_[i];
  }
  if (count_) {
    --count_;
    memset(&records_[count_], 0, sizeof(records_[count_]));
  }
}

void ModuleRegistry::mergeHistory(ModuleRecord& dst, const ModuleRecord& src) {
  // These counters belong to the physical module, not to a particular RS485
  // address. Preserve the larger value when healing an address-move duplicate.
  if (src.timeout_count > dst.timeout_count) dst.timeout_count = src.timeout_count;
  if (src.miss_count > dst.miss_count) dst.miss_count = src.miss_count;
  if (src.crc_error_count > dst.crc_error_count) dst.crc_error_count = src.crc_error_count;

  // Preserve useful cached identity/UI metadata if the destination was freshly
  // created by an earlier scan at the new address.
  if (!dst.type && src.type) dst.type = src.type;
  if (!dst.hw_version && src.hw_version) dst.hw_version = src.hw_version;
  if (!dst.caps && src.caps) dst.caps = src.caps;
  if (!dst.name[0] && src.name[0]) {
    strncpy(dst.name, src.name, sizeof(dst.name) - 1);
    dst.name[sizeof(dst.name) - 1] = 0;
  }
  if (!dst.label[0] && src.label[0]) {
    strncpy(dst.label, src.label, sizeof(dst.label) - 1);
    dst.label[sizeof(dst.label) - 1] = 0;
  }
}

ModuleRecord* ModuleRegistry::bindUidToAddress(uint64_t uid, uint8_t addr) {
  if (!records_ || !uid) return nullptr;

  int8_t target_index = -1;
  int8_t uid_index = -1;

  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].addr == addr) target_index = (int8_t)i;
    if (records_[i].uid == uid && uid_index < 0) uid_index = (int8_t)i;
  }

  // A different remembered physical module already owns this address. Do not
  // silently destroy it; address-assignment code is supposed to avoid this.
  if (target_index >= 0 &&
      records_[(uint8_t)target_index].uid != 0 &&
      records_[(uint8_t)target_index].uid != uid) {
    return nullptr;
  }

  // No existing record for this UID: use/create the address record.
  if (uid_index < 0) {
    ModuleRecord* rec = target_index >= 0
      ? &records_[(uint8_t)target_index]
      : upsert(addr);
    if (rec) rec->uid = uid;
    return rec;
  }

  // UID exists, but there is no record at the new address: migrate the existing
  // record in-place. This preserves labels, miss history and cached metadata.
  if (target_index < 0) {
    records_[(uint8_t)uid_index].addr = addr;
    sortByAddress();
    return find(addr);
  }

  // Both the old and new address records exist for the same UID (for example
  // after older firmware re-addressed a module and then upserted the new
  // address). Keep the new-address record and merge historical information.
  if (target_index != uid_index) {
    mergeHistory(records_[(uint8_t)target_index], records_[(uint8_t)uid_index]);

    // removeAt() can shift target_index when the old duplicate is before it,
    // so remember the physical address and look it up again afterwards.
    removeAt((uint8_t)uid_index);
    ModuleRecord* target = find(addr);
    if (!target) return nullptr;
    target->uid = uid;

    // Remove any additional legacy duplicates of the same UID as a defensive
    // self-heal. Iterate backwards because removeAt compacts the array.
    for (int i = (int)count_ - 1; i >= 0; --i) {
      if (records_[i].addr == addr) continue;
      if (records_[i].uid != uid) continue;
      mergeHistory(*target, records_[i]);
      removeAt((uint8_t)i);
      target = find(addr);
      if (!target) return nullptr;
    }
    return target;
  }

  // Already canonical at this address. Still remove any extra legacy copies.
  ModuleRecord* target = &records_[(uint8_t)target_index];
  for (int i = (int)count_ - 1; i >= 0; --i) {
    if ((uint8_t)i == (uint8_t)target_index) continue;
    if (records_[i].uid != uid) continue;
    mergeHistory(*target, records_[i]);
    removeAt((uint8_t)i);
    target = find(addr);
    if (!target) return nullptr;
    // target_index may have shifted; use address lookup from here on.
  }
  target->uid = uid;
  return target;
}

ModuleRecord* ModuleRegistry::firstWithCaps(uint32_t caps) {
  if (!records_) return nullptr;
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].online && (records_[i].caps & caps) == caps) return &records_[i];
  }
  return nullptr;
}

const ModuleRecord* ModuleRegistry::firstWithCaps(uint32_t caps) const {
  if (!records_) return nullptr;
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].online && (records_[i].caps & caps) == caps) return &records_[i];
  }
  return nullptr;
}

void ModuleRegistry::clearRoles() {
  if (!records_) return;
  for (uint8_t i = 0; i < count_; ++i) {
    records_[i].role_jbc = false;
    records_[i].role_output = false;
  }
}

void ModuleRegistry::markAllScanUnseen() {
  if (!records_) return;
  for (uint8_t i = 0; i < count_; ++i) records_[i].seen_in_scan = false;
}

uint8_t ModuleRegistry::removeScanUnseen() {
  if (!records_) return 0;
  uint8_t removed = 0;
  uint8_t write = 0;
  for (uint8_t read = 0; read < count_; ++read) {
    if (!records_[read].seen_in_scan) {
      ++removed;
      continue;
    }
    if (write != read) records_[write] = records_[read];
    ++write;
  }
  for (uint8_t i = write; i < count_; ++i) memset(&records_[i], 0, sizeof(records_[i]));
  count_ = write;
  sortByAddress();
  return removed;
}

void ModuleRegistry::sortByAddress() {
  if (!records_) return;
  // Bubble sort is perfectly adequate for <=16 records and, unlike the old
  // insertion sort, never creates a full ModuleRecord temporary on loopTask's
  // stack. A ModuleRecord now contains a 4 KiB descriptor cache.
  for (uint8_t pass = 0; pass < count_; ++pass) {
    bool changed = false;
    for (uint8_t i = 1; i < (uint8_t)(count_ - pass); ++i) {
      if (records_[i - 1].addr <= records_[i].addr) continue;
      swap_module_records_small_stack(records_[i - 1], records_[i]);
      changed = true;
    }
    if (!changed) break;
  }
}
