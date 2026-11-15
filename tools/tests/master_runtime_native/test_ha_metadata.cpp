#include "HaMetadataCache.h"
#include "EntityPresentation.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include "generated_json_escape.inc.h"

struct ModuleRecord {
  uint64_t uid = 1;
  uint8_t addr = 0x20, type = 3;
  uint32_t caps = 7, universal_descriptor_crc = 123, live_value = 0;
  bool online = true, universal_descriptor_valid = true;
  char label[64] = "Fan", io_in1_alias[24] = "Input 1", io_in2_alias[24] = "Input 2";
  char universal_descriptor[8192] = {};
};
struct ModuleRegistry {
  static constexpr uint8_t MAX_MODULES = 16;
  ModuleRecord modules[MAX_MODULES];
  uint8_t size = 2;
  uint8_t count() const { return size; }
  ModuleRecord& at(uint8_t index) { return modules[index]; }
} registry;
struct Scheduler {
  uint8_t input_type = 1, input_addr = 0x20, input_bit = 0, output = 0;
  uint8_t mainInputSourceType() const { return input_type; }
  uint8_t mainInputSourceAddr() const { return input_addr; }
  uint8_t mainInputSourceBit() const { return input_bit; }
  uint8_t preferredOutputAddr() const { return output; }
} scheduler;
static char web_lang[3] = "de";
static unsigned defs_builds = 0, route_builds = 0;
static void ha_append_route_options_uncached(OfePsramTextBuffer& out) {
  ++route_builds;
  out += "route build "; out += route_builds;
}
#include "generated_ha_metadata.inc.h"

static std::string defs(bool cached) {
  OfePsramTextBuffer out;
  assert(out.reservePsram(16384));
  if (cached) append_universal_entity_defs_json(out, registry.at(0));
  else append_universal_entity_defs_json_uncached(out, registry.at(0));
  assert(out.ok());
  return out.c_str();
}
static void test_payload_and_hits() {
  auto& m = registry.at(0);
  strcpy(m.universal_descriptor,
    "schema=1\n1 sensor cpu ro\n20 select filter rw source=profile de=Filterzeit en=Filter time options=Aus|8 h values=0|48 idx=2\n"
    "21 number speed rw source=hardware en=Speed\n22 text state ro de=L\xC3\xBC" "fter \\\"bereit\\\"\n");
  const std::string expected = defs(false);
  assert(expected.find("profile_index") != std::string::npos);
  assert(expected.find("Speed") == std::string::npos);
  assert(defs(true) == expected);
  const unsigned built = defs_builds, allocated = heap_calls;
  for (unsigned i = 0; i < 1000; ++i) {
    ++m.live_value;
    assert(defs(true) == expected);
  }
  assert(defs_builds == built);
  // Only the caller's fresh output buffer allocates on a cache hit.
  assert(heap_calls == allocated + 1000);

  strcpy(web_lang, "en"); assert(defs(true) == defs(false));
  assert(defs(true).find("Filter time") != std::string::npos);
  strcpy(m.universal_descriptor, "20 select filter rw options=Off|16 h values=0|96 de=Neu en=New\n");
  // Deliberately keep the CRC unchanged: content must still invalidate.
  assert(defs(true) == defs(false));
  assert(defs(true).find("New") != std::string::npos);
  m.universal_descriptor_valid = false;
  assert(defs(true) == ",\"universal_entity_defs\":[]");
  m.universal_descriptor_valid = true; ++m.uid; ++m.addr; ++m.caps; ++m.type;
  assert(defs(true) == defs(false));
  std::swap(registry.modules[0], registry.modules[1]);
  assert(defs(true) == defs(false));
}
static void test_route_dependencies() {
  OfePsramTextBuffer out;
  assert(out.reservePsram(4096));
  auto query = [&] { out.clear(); ha_append_route_options(out); assert(out.ok()); };
  query();
  unsigned built = route_builds;
  for (unsigned i = 0; i < 1000; ++i) { ++registry.at(0).live_value; query(); }
  assert(route_builds == built);
  auto changed = [&] { query(); assert(route_builds == ++built); query(); assert(route_builds == built); };
  strcpy(web_lang, "de"); changed();
  ++scheduler.input_type; changed(); ++scheduler.input_addr; changed();
  ++scheduler.input_bit; changed(); ++scheduler.output; changed();
  auto& m = registry.at(0);
  ++m.uid; changed(); ++m.addr; changed(); ++m.type; changed(); ++m.caps; changed();
  m.online = !m.online; changed();
  strcpy(m.label, "Alias with \"quotes\""); changed();
  strcpy(m.io_in1_alias, "First"); changed(); strcpy(m.io_in2_alias, "Second"); changed();
  strcpy(m.universal_descriptor, "20 switch input rw role=main_input"); changed();
  strcpy(m.universal_descriptor, "20 switch input rw role=output"); changed();
  m.universal_descriptor_valid = false; changed();
  strcpy(m.universal_descriptor, "irrelevant while invalid"); query(); assert(route_builds == built);
  m.universal_descriptor_valid = true; changed();
  std::swap(registry.modules[0], registry.modules[1]); changed();
  registry.size = 1; changed(); registry.size = 0; changed(); registry.size = 2; changed();
}
static void test_failure_and_key_boundaries() {
  HaMetadataCache cache;
  OfePsramTextBuffer out;
  assert(out.reservePsram(8192));
  std::string first = "ab", second = "c", payload = "original";
  unsigned built = 0;
  auto key = [&](HaMetadataKey& key) { key.text(first.c_str()); key.text(second.c_str()); };
  auto build = [&](OfePsramTextBuffer& target) { ++built; target += payload.c_str(); };
  auto query = [&] { out.clear(); cache.appendTo(out, key, build); assert(out.ok()); return std::string(out.c_str()); };
  assert(query() == "original"); assert(query() == "original"); assert(built == 1);
  first = "a"; second = "bc"; payload = "different fields";
  assert(query() == payload); assert(built == 2);
  first.assign(2048, 'x'); payload = "fresh despite failed key allocation";
  fail_external = true;
  assert(query() == payload);
  fail_external = false;
  assert(query() == payload);
  payload.assign(4096, 'y'); first = "new key";
  fail_external = true;
  assert(query() == payload); // failed fragment allocation, preallocated caller still works
  fail_external = false;
  assert(query() == payload);
  first = "mutating"; payload = "latest";
  out.clear();
  cache.appendTo(out, key, [&](OfePsramTextBuffer& target) {
    target += payload.c_str(); first = "changed during build";
  });
  assert(std::string(out.c_str()) == "latest");
  assert(query() == "latest");
  const unsigned before = built; assert(query() == "latest"); assert(built == before);
}
int main() {
  test_payload_and_hits();
  test_route_dependencies();
  test_failure_and_key_boundaries();
  std::cout << "PASS: actual HA metadata builders, exact invalidation, live-value isolation, PSRAM failure fallback\n";
}
