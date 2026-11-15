#include "ModuleRegistry.h"
#include <cassert>
#include <vector>
#include <initializer_list>
#include "manifest.h"

static void registryTest() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  static_assert(ModuleRegistry::MAX_MODULES == 32, "P4 capacity");
#else
  static_assert(ModuleRegistry::MAX_MODULES == 16, "S3 capacity");
#endif
  ModuleRegistry registry;
  assert(registry.begin());
  assert(registry.usesPsram());
  assert(registry.storageBytes() == sizeof(ModuleRecord) * ModuleRegistry::MAX_MODULES);
  for (uint8_t i = 0; i < ModuleRegistry::MAX_MODULES; ++i) {
    auto* item = registry.upsert(uint8_t(0x10 + i));
    assert(item);
    item->uid = uint64_t(i) + 100;
  }
  assert(registry.count() == ModuleRegistry::MAX_MODULES);
  assert(!registry.upsert(0x60));
  assert(registry.upsert(0x10));
  auto* moved = registry.bindUidToAddress(100, 0x61);
  assert(moved && moved->uid == 100 && moved->addr == 0x61);
  assert(registry.count() == ModuleRegistry::MAX_MODULES);
  assert(!registry.find(0x10));
  assert(registry.removeAddress(0x61));
  assert(registry.upsert(0x60));
  assert(registry.count() == ModuleRegistry::MAX_MODULES);
  registry.sortByAddress();
  for (uint8_t i = 1; i < registry.count(); ++i) assert(registry.at(i - 1).addr < registry.at(i).addr);
  registry.clear();
  assert(registry.count() == 0);
}

static std::vector<uint8_t> legacy(size_t slots, size_t entities) {
  const size_t header = offsetof(MqttDiscoveryManifest, modules), stride = 2 + entities * sizeof(MqttManifestEntity);
  std::vector<uint8_t> bytes((header + slots * stride + 3) & ~size_t(3), 0);
  MqttDiscoveryManifest value;
  value.struct_size = uint16_t(bytes.size());
  value.module_count = uint8_t(slots);
  std::strcpy(value.device_id, "preserved-device");
  std::memcpy(bytes.data(), &value, header);
  for (size_t i = 0; i < slots; ++i) {
    auto* module = bytes.data() + header + i * stride;
    module[0] = uint8_t(0x10 + i); module[1] = 1;
    MqttManifestEntity entity; entity.component = MQTT_MAN_COMP_SWITCH; entity.key = uint8_t(i);
    std::memcpy(module + 2, &entity, sizeof(entity));
  }
  return bytes;
}

int main() {
  registryTest();
  for (const size_t slots : {size_t(16), size_t(32)}) {
    for (const size_t entries : {size_t(72), size_t(MQTT_HA_MANIFEST_MAX_ENTITIES)}) {
      auto bytes = legacy(slots, entries);
      MqttDiscoveryManifest output;
      const bool fits = slots <= ModuleRegistry::MAX_MODULES;
      assert(mqtt_manifest_migrate(bytes.data(), bytes.size(), output) == fits);
      if (!fits) continue;
      assert(mqtt_manifest_valid(output));
      assert(output.module_count == slots && output.struct_size == sizeof(output));
      assert(!std::strcmp(output.device_id, "preserved-device"));
      for (size_t i = 0; i < slots; ++i) {
        assert(output.modules[i].addr == 0x10 + i);
        assert(output.modules[i].entity_count == 1);
        assert(output.modules[i].entities[0].key == i);
      }
      assert(!mqtt_manifest_migrate(bytes.data(), bytes.size() - 1, output));
      bytes[0] ^= 1;
      assert(!mqtt_manifest_migrate(bytes.data(), bytes.size(), output));
      bytes = legacy(slots, entries); bytes[offsetof(MqttDiscoveryManifest, modules) + 1] = uint8_t(entries + 1);
      assert(!mqtt_manifest_migrate(bytes.data(), bytes.size(), output));
    }
  }
  std::printf("PASS: actual registry capacity %u, full-table UID moves, limits and MQTT 16/32 x 72/current migration\n", ModuleRegistry::MAX_MODULES);
}
