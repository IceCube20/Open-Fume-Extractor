#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "../../../OpenFumeExtractorMaster/src/MqttAvailabilityCache.h"

using String = std::string;
static MqttAvailabilityCache mqtt_availability_cache;
static String base = "ofe/";
static String mqtt_session_topic_root;
static String mqtt_topic_path(const char* leaf) { return base + leaf; }
static String mqtt_module_topic(uint8_t addr, const char* leaf) {
  char path[40];
  snprintf(path, sizeof(path), "module/%02X/%s", addr, leaf);
  return mqtt_topic_path(path);
}
struct Client {
  bool online = true;
  bool succeed = true;
  unsigned loops = 0;
  struct Message { String topic, payload; bool retained; };
  std::vector<Message> messages;
  bool connected() const { return online; }
} mqtt_client;
static bool mqtt_publish_measured(const char* topic, const char* payload, bool retained) {
  mqtt_client.messages.push_back({topic, payload, retained});
  return mqtt_client.succeed;
}
static void mqtt_client_loop_measured() { ++mqtt_client.loops; }
struct Registry {
  struct Record { uint8_t addr; } records[2] = {{0x20}, {0x40}};
  uint8_t count() const { return 2; }
  const Record& at(uint8_t i) const { return records[i]; }
} registry;
#include "generated_mqtt_availability.inc.h"

int main() {
  static_assert(sizeof(MqttAvailabilityCache) == 64, "Availability cache must stay bounded");
  MqttAvailabilityCache cache;
  for (unsigned addr = 0; addr < 256; ++addr) {
    assert(cache.needsPublish((uint8_t)addr, false));
    assert(cache.needsPublish((uint8_t)addr, true));
    cache.published((uint8_t)addr, (addr & 1U) != 0);
  }
  for (unsigned addr = 0; addr < 256; ++addr) {
    const bool online = (addr & 1U) != 0;
    assert(!cache.needsPublish((uint8_t)addr, online));
    assert(cache.needsPublish((uint8_t)addr, !online));
    cache.forget((uint8_t)addr);
    assert(cache.needsPublish((uint8_t)addr, online));
  }

  mqtt_publish_module_availability(0x20, true);
  assert(mqtt_client.messages.size() == 1);
  assert(mqtt_client.messages.back().topic == "ofe/module/20/status");
  assert(mqtt_client.messages.back().payload == "online" && mqtt_client.messages.back().retained);
  for (unsigned i = 0; i < 100; ++i) mqtt_publish_module_availability(0x20, true);
  assert(mqtt_client.messages.size() == 1);
  mqtt_publish_module_availability(0x20, false);
  assert(mqtt_client.messages.size() == 2 && mqtt_client.messages.back().payload == "offline");
  mqtt_publish_module_availability(0x20, true);
  assert(mqtt_client.messages.size() == 3);
  mqtt_client.succeed = false;
  mqtt_publish_module_availability(0x20, false);
  mqtt_publish_module_availability(0x20, false);
  assert(mqtt_client.messages.size() == 5);
  mqtt_client.succeed = true;
  mqtt_publish_module_availability(0x20, false);
  mqtt_publish_module_availability(0x20, false);
  assert(mqtt_client.messages.size() == 6);
  mqtt_client.online = false;
  mqtt_publish_module_availability(0x40, true);
  mqtt_publish_module_offline(0x40);
  assert(mqtt_client.messages.size() == 6);
  mqtt_client.online = true;
  mqtt_publish_module_availability(0x40, true);
  assert(mqtt_client.messages.size() == 7);

  mqtt_availability_cache.reset(); // Successful reconnect: all retained availability is republished.
  mqtt_publish_module_availability(0x20, false);
  mqtt_publish_module_availability(0x40, true);
  assert(mqtt_client.messages.size() == 9);
  base = "new/";
  mqtt_availability_cache.reset(); // New broker/topic namespace is a new session.
  mqtt_publish_module_availability(0x40, true);
  assert(mqtt_client.messages.back().topic == "new/module/40/status");
  mqtt_availability_cache.forget(0x40); // Retained topic removed; address may be reused.
  mqtt_publish_module_availability(0x40, true);
  assert(mqtt_client.messages.size() == 11);
  mqtt_publish_module_availability(0x20, false);
  const size_t before = mqtt_client.messages.size();
  mqtt_publish_module_offline(0x20);
  mqtt_publish_module_offline(0x20);
  assert(mqtt_client.messages.size() == before + 2); // Offline state snapshots still refresh.
  assert(mqtt_client.messages.back().payload == "{\"addr\":32,\"online\":false}");

  mqtt_session_topic_root = "old/";
  mqtt_publish_all_availability_offline(); // Explicit shutdown always bypasses deduplication.
  assert(mqtt_client.messages.size() == before + 7 && mqtt_client.loops == 1);
  for (size_t i = before + 2; i < mqtt_client.messages.size(); ++i)
    assert(mqtt_client.messages[i].topic.find("old/") == 0);
  mqtt_client.online = false;
  mqtt_publish_all_availability_offline();
  assert(mqtt_client.messages.size() == before + 7);
  std::cout << "MQTT availability: 256 addresses, deduplication, transitions, failed-send retry, reconnect, topic change, address reuse and forced shutdown passed\n";
}
