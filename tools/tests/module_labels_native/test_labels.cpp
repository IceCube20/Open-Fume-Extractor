#include "MasterSettingsStore.h"
#include "ModuleLabelPolicy.h"
#include <cassert>
#include <cstdio>
#include <cstring>

struct ModuleRecord {
  uint64_t uid = 0x2000F4D9961CB2B0ULL;
  char name[24]{}, label[24]{};
  bool info_name_valid = false;
  uint32_t timeout_count = 0, miss_count = 0, crc_error_count = 0, caps = 0;
  uint16_t hw_version = 0;
  uint8_t type = 0;
  const char* type_name = "Fan/IO Pro";
};
struct Registry {
  ModuleRecord record;
  uint8_t count() const { return 1; }
  ModuleRecord& at(uint8_t) { return record; }
} registry;
const char* module_type_name_for(const ModuleRecord& m) { return m.type_name; }
String module_label_key(uint64_t uid) { return MasterSettingsStore::moduleLabelKey(uid); }
#include "apply_labels.h"
struct ModuleRegistry {
  static void mergeHistory(ModuleRecord& dst, const ModuleRecord& src);
};
#include "merge_labels.h"

void info(const char* name) {
  std::snprintf(registry.record.name, sizeof(registry.record.name), "%s", name);
  registry.record.info_name_valid = true;
}
int main() {
  Preferences::reset();
  info("Testbank"); apply_module_labels();
  assert(std::strcmp(registry.record.label, "Testbank") == 0);  // A second/new MCU has no local alias.
  const auto writes = Preferences::writes;
  for (unsigned i = 0; i < 100; ++i) apply_module_labels();
  assert(Preferences::writes == writes);
  Preferences p;
  assert(p.begin(MasterSettingsStore::NS_CFG));
  const String key = module_label_key(registry.record.uid);
  assert(p.getString(key.c_str()) == String("Testbank"));
  p.putString(key.c_str(), "Stale local alias"); p.end();
  info("Renamed elsewhere"); apply_module_labels();
  assert(std::strcmp(registry.record.label, "Renamed elsewhere") == 0);
  assert(p.begin(MasterSettingsStore::NS_CFG));
  assert(p.getString(key.c_str()) == String("Renamed elsewhere")); p.end();
  registry.record = ModuleRecord{}; apply_module_labels();
  assert(std::strcmp(registry.record.label, "Renamed elsewhere") == 0);  // Offline boot cache.
  info("Fan/IO Pro"); apply_module_labels();
  assert(registry.record.label[0] == 0);
  assert(p.begin(MasterSettingsStore::NS_CFG)); assert(!p.isKey(key.c_str())); p.end();
  Preferences::fail_begin = true;
  info("Alias without NVS"); apply_module_labels();
  assert(std::strcmp(registry.record.label, "Alias without NVS") == 0);
  Preferences::fail_begin = false;
  char label[24];
  ofe_module_label::from_info("  \tCustom <test>  ", "Fan/IO Pro", label, sizeof(label));
  assert(std::strcmp(label, "Custom  test") == 0);
  ofe_module_label::from_info("fan/io pro", "Fan/IO Pro", label, sizeof(label)); assert(!label[0]);
  ofe_module_label::from_info("A very long alias beyond twenty three bytes", "Fan/IO Pro", label, sizeof(label));
  assert(std::strlen(label) == 23);
  ModuleRecord fresh, history;
  strcpy_s(history.name, "Old alias");
  strcpy_s(history.label, "Old alias");
  history.info_name_valid = true;
  history.timeout_count = 12;
  ModuleRegistry::mergeHistory(fresh, history);
  assert(fresh.info_name_valid && !strcmp(fresh.label, "Old alias"));
  fresh = ModuleRecord{};
  strcpy_s(fresh.name, "Fan/IO Pro");
  fresh.info_name_valid = true;
  ModuleRegistry::mergeHistory(fresh, history);
  assert(!fresh.label[0] && !strcmp(fresh.name, "Fan/IO Pro"));
  assert(fresh.timeout_count == 12);  // Counters still follow the physical module.
  fresh = ModuleRecord{};
  history.info_name_valid = false;
  ModuleRegistry::mergeHistory(fresh, history);
  assert(!fresh.info_name_valid && !strcmp(fresh.label, "Old alias"));
  std::puts("PASS: actual alias reconciliation: new MCU, module rename, stale cache, explicit clear, offline cache, no NVS, sanitization and deduplicated writes");
}
