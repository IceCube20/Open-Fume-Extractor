#include "ModuleRegistry.h"
#include "ModuleAlarmPolicy.h"
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

static void moduleAlarmTest() {
  using namespace jbc_rs485;
  using ofe_module_alarm::evaluate;
  ModuleRecord rec;
  rec.addr = 0x30; rec.online = true; rec.type = MODULE_WELLER_ZERO_SMOG;
  rec.caps = CAP_WELLER_INTERFACE; rec.weller_uart_age_sec = 0xFFFF;
  assert(evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
  rec.connection_warnings_muted = true;
  assert(!evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
  assert(evaluate(rec, rec.addr, 0).error());
  assert(evaluate(rec, 0, rec.addr).error()); // Required but not active yet.
  rec.io_fault_mask = 1;
  assert(!evaluate(rec, 0, 0).error() && !evaluate(rec, 0, 0).warning());
  assert(evaluate(rec, rec.addr, 0).error());
  rec.connection_warnings_muted = false;
  assert(evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
  rec.connection_warnings_muted = true;
  rec.io_fault_mask = 2; rec.role_output = true; rec.weller_uart_age_sec = 0;
  assert(evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
  rec.io_fault_mask = 6;
  assert(evaluate(rec, 0, 0).warning() && evaluate(rec, 0, 0).error());
  rec.io_fault_mask = 0; rec.online = false;
  assert(evaluate(rec, 0, 0).error());
  rec.online = true; rec.type = MODULE_JBC_BUS; rec.caps = CAP_JBC_BUS; rec.role_output = false;
  assert(!evaluate(rec, 0, 0).error() && !evaluate(rec, 0, 0).warning());
  rec.connection_warnings_muted = false;
  assert(evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
  rec.jbc_link_flags = FAST_FLAG_CONNECTED | FAST_FLAG_ERROR_PENDING; rec.station_addr = 0x18;
  assert(!evaluate(rec, 0, 0).error() && !evaluate(rec, 0, 0).warning());
  rec.type = MODULE_JBC_USB; rec.caps = CAP_JBC_USB; rec.jbc_link_flags = 0;
  assert(evaluate(rec, 0, 0).warning());
  rec.connection_warnings_muted = true;
  assert(!evaluate(rec, 0, 0).warning());
  for (auto type : {MODULE_UNIVERSAL_RS232, MODULE_MODBUS_RTU}) {
    rec.type = type; rec.caps = 0; rec.io_fault_mask = 1;
    assert(ofe_module_alarm::supportsConnectionWarning(rec));
    for (bool role : {false, true}) for (bool enabled : {false, true}) {
      rec.role_output = role; rec.output_enabled = enabled;
      rec.connection_warnings_muted = false;
      assert(evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
      rec.connection_warnings_muted = true;
      assert(!evaluate(rec, 0, 0).warning() && !evaluate(rec, 0, 0).error());
      assert(evaluate(rec, rec.addr, 0).error());
      assert(evaluate(rec, 0, rec.addr).error());
    }
    rec.role_output = true; rec.io_fault_mask = 5;
    assert(evaluate(rec, 0, 0).error()); // Muting a link never mutes another fault.
  }
  ModuleRegistry registry;
  assert(registry.begin());
  auto* item = registry.upsert(0x10); item->uid = 123; item->connection_warnings_muted = true;
  item = registry.bindUidToAddress(123, 0x11);
  assert(item && item->connection_warnings_muted);
  assert(!registry.upsert(0x10)->connection_warnings_muted);
  const auto warning = ofe_module_health::summary(true, false);
  assert((warning & ofe_module_health::SUMMARY_WARNING) && !(warning & ofe_module_health::SUMMARY_ERROR));
}

int main() {
  moduleAlarmTest();
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
