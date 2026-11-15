#include <Arduino.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
struct __FlashStringHelper;
using PGM_P = const char*;
#define strlen_P strlen
#define memcpy_P memcpy
#define F(value) value
#define OFE_STR_INNER(value) #value
#define OFE_STR(value) OFE_STR_INNER(value)
#include "identity.inc.h"
#include "OfePsramTextBuffer.h"
#include "HaEntityIcons.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
static char master_device_id[] = "open-fume-extractor-e1a0ed30";
static uint64_t factory_uid = 0x0000ACFFE1A0ED30ULL;
#else
static char master_device_id[] = "open-fume-extractor-384aec24";
static uint64_t factory_uid = 0x0000ECF7384AEC24ULL;
#endif
static char mqtt_base_topic[65] = "open-fume-extractor";
static bool master_extmem_malloc_enabled = true, developer_mode_enabled = true;
static bool mqtt_ha_discovery = true, mqtt_discovery_publish_failed = false;
static constexpr size_t MASTER_MQTT_DISCOVERY_PAYLOAD_RESERVE_PSRAM = 16384;
static OfePsramTextBuffer mqtt_discovery_payload_buffer;
static bool german = true;
static const char* mqtt_txt(const char* de, const char* en) { return german ? de : en; }
static bool web_is_german() { return german; }
static uint64_t master_factory_uid() { return factory_uid; }
static String uid_hex(uint64_t value) {
  char text[17]; std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
  return String(text);
}
static String mqtt_config_url() { return String("http://192.0.2.1/"); }
static String mqtt_discovery_topic(const char* component, const char* id, const char* suffix) {
  return String("homeassistant/") + String(component) + String("/") + String(id) + String("/") + String(suffix) + String("/config");
}
static String mqtt_main_input_options_extra() { return String(",\"options\":[\"No input\"],\"icon\":\"mdi:source-branch\""); }
static String mqtt_main_output_options_extra() { return String(",\"options\":[\"Auto\"],\"icon\":\"mdi:fan\""); }
static struct Client { bool connected() const { return true; } } mqtt_client;
static struct Scheduler {
  unsigned activeOutputMinSelectFlow() const { return 100; }
  unsigned activeOutputMaxSelectFlow() const { return 1000; }
} scheduler;
static std::vector<std::string> messages;
static unsigned deletes = 0, mqtt_manifest_delete_failed = 0;
static unsigned mqtt_manifest_delete_total = 0, mqtt_manifest_last_delete_count = 0;
static unsigned master_commands = 0, module_commands = 0;
static bool mqtt_apply_module_command(const String& leaf, const String&) {
  if (!leaf.startsWith("module/")) return false;
  ++module_commands; return true;
}
static void mqtt_apply_command(const String&, const String&) { ++master_commands; }
struct MqttDiscoveryManifest { char base_topic[65]; };
static bool mqtt_publish_measured(const char*, const char* payload, bool retained) {
  assert(retained && mqtt_discovery_payload_buffer.ok());
  if (*payload) messages.emplace_back(payload);
  else ++deletes;
  return true;
}
#include "discovery.inc.h"

int main() {
  assert(mqtt_normalized_base_topic("custom/site/") == "custom/site");
  const uint64_t own = factory_uid;
  const String first = mqtt_normalized_base_topic(" open-fume-extractor/// ");
  const String first_client = mqtt_client_identity();
  factory_uid ^= 0x100000000ULL; // Same lower 32 bits must still be isolated.
  const String second = mqtt_normalized_base_topic("");
  assert(!(first == second));
  assert(!(first_client == mqtt_client_identity()));
  factory_uid = own;
  assert(first == mqtt_normalized_base_topic(""));
  assert(first == mqtt_normalized_base_topic("open-fume-extractor"));
  assert(first == mqtt_normalized_base_topic("open-fume-extractor/0000000000000001"));
  assert(mqtt_normalized_base_topic("open-fume-extractor/custom") == "open-fume-extractor/custom");
  assert(mqtt_normalized_base_topic("open-fume-extractor/000000000000000Z") == "open-fume-extractor/000000000000000Z");
  assert(mqtt_client_identity() == String("ofe-") + uid_hex(factory_uid));
  assert(mqtt_module_topic(0x20, "state") == first + "/module/20/state");
  assert(mqtt_topic_path(mqtt_module_command_leaf(0x20, "out2").c_str()) == first + "/cmd/module/20/out2");
  const auto receive = [](const String& topic) {
    uint8_t value[] = {'1'};
    mqtt_callback(const_cast<char*>(topic.c_str()), value, 1);
  };
  receive(second + "/cmd/continuous");
  receive(second + "/cmd/module/20/out2");
  receive("open-fume-extractor/cmd/continuous");
  assert(master_commands == 0 && module_commands == 0);
  receive(first + "/cmd/continuous");
  receive(first + "/cmd/module/20/out2");
  assert(master_commands == 1 && module_commands == 1);
  MqttDiscoveryManifest old{};
  std::snprintf(old.base_topic, sizeof(old.base_topic), "open-fume-extractor");
  assert(mqtt_manifest_clear_old_module_value(old, 0x20, "state") && deletes == 0);
  assert(mqtt_manifest_clear_old_module_value(old, 0x20, "status") && deletes == 0);
  std::snprintf(old.base_topic, sizeof(old.base_topic), "open-fume-extractor/0000000000000001");
  assert(mqtt_manifest_clear_old_module_value(old, 0x20, "state") && deletes == 0);
  std::snprintf(old.base_topic, sizeof(old.base_topic), "old-custom");
  assert(mqtt_manifest_clear_old_module_value(old, 0x20, "state") && deletes == 1);
  std::snprintf(old.base_topic, sizeof(old.base_topic), "%s", first.c_str());
  assert(mqtt_manifest_clear_old_module_value(old, 0x20, "state") && deletes == 2);
  deletes = 0;
  for (const bool psram : {false, true}) for (const bool developer : {false, true}) for (const bool de : {false, true}) {
    master_extmem_malloc_enabled = psram; developer_mode_enabled = developer; german = de;
    messages.clear(); mqtt_discovery_publish_failed = false;
    publish_all();
    assert(!mqtt_discovery_publish_failed && messages.size() > 35);
    for (const auto& message : messages) std::puts(message.c_str());
  }
}
