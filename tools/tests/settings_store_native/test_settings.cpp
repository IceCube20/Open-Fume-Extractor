#include "MasterSettingsStore.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace MasterSettingsStore;
static void check(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static void controlEqual(const ControlSettings& a, const ControlSettings& b) {
  check(a.suction == b.suction && a.select_flow == b.select_flow &&
        a.delay_work_sec == b.delay_work_sec && a.delay_stand_sec == b.delay_stand_sec &&
        a.stand_intakes == b.stand_intakes && a.afterrun_power_enabled == b.afterrun_power_enabled &&
        a.afterrun_power == b.afterrun_power && !a.continuous, "persistent control roundtrip");
}
static void checksum(std::vector<uint8_t>& b) {
  b.back() = 0; for (size_t i = 0; i + 1 < b.size(); ++i) b.back() ^= b[i];
}

int main() {
  Preferences p;
  Preferences::reset();
  controlEqual(loadControl(p), ControlSettings{});
  check(!p.active() && Preferences::writes == 0, "fresh load does not write settings values");
  ControlSettings s;
  s.suction = 2; s.select_flow = 680; s.delay_work_sec = 12; s.delay_stand_sec = 20;
  s.stand_intakes = false; s.continuous = true; s.afterrun_power_enabled = true; s.afterrun_power = 400;
  check(saveControl(p, s), "save control");
  std::vector<uint8_t> golden{0xC6, 1, 2, 0xA8, 2, 12, 0, 20, 0, 0, 1, 0x90, 1, 0};
  checksum(golden);
  check(Preferences::db[NS_CFG].at(KEY_CFG_BLOB).blob == golden, "original v1 control blob unchanged");
  controlEqual(loadControl(p), s);
  const size_t before = Preferences::writes;
  s.continuous = false;
  for (int i = 0; i < 1000; ++i) check(saveControl(p, s), "identical save succeeds");
  check(Preferences::writes == before, "unchanged/Continuous-only settings do not write flash");
  s.select_flow = 550;
  Preferences::reject_write = Preferences::writes + 1;
  check(!saveControl(p, s), "failed write reported");
  check(loadControl(p).select_flow == 680 && !p.active(), "old control retained on failed write");
  Preferences::drop_write = Preferences::writes + 1;
  check(!saveControl(p, s), "apparently successful but lost write detected by readback");
  Preferences::drop_write = 0;
  check(saveControl(p, s), "retry succeeds");

  auto& blob = Preferences::db[NS_CFG].at(KEY_CFG_BLOB).blob;
  blob[3] ^= 1;
  controlEqual(loadControl(p), ControlSettings{});
  blob = golden; blob[9] = 2; checksum(blob);
  controlEqual(loadControl(p), ControlSettings{});
  blob = {0xC6, 1};
  controlEqual(loadControl(p), ControlSettings{});
  Preferences::reset();
  check(p.begin(NS_CFG), "open legacy");
  p.putUChar(KEY_CFG_SUCTION, 1); p.putUShort(KEY_CFG_FLOW, 430); p.putUShort(KEY_CFG_DELAY_WORK, 23);
  p.putUChar("cfg_cont", 1); p.end();
  const auto legacy = loadControl(p);
  check(legacy.suction == 1 && legacy.select_flow == 430 && legacy.delay_work_sec == 23 &&
        !legacy.continuous, "legacy keys compatible, legacy Continuous ignored");
  check(loadMainInput(p, 1, 0).type == 1, "legacy all JBC default preserved");
  check(saveMainInput(p, 2, 0x20, 7), "IN8 supported");
  auto input = loadMainInput(p, 1, 0);
  check(input.type == 2 && input.addr == 0x20 && input.bit == 7, "IN8 roundtrip");
  check(saveMainInput(p, 3, 0x60, 51), "universal entity IDs not limited to I/O bits");
  check(loadMainInput(p, 1, 0).bit == 51, "universal ID roundtrip");
  check(!saveMainInput(p, 2, 0x20, 16) && !saveMainInput(p, 3, 0x09, 20) &&
        !saveMainInput(p, 4, 0, 1), "invalid main input rejected before write");
  check(saveMainInput(p, 0, 0x20, 7), "none normalizes irrelevant fields");
  input = loadMainInput(p, 1, 0);
  check(input.type == 0 && input.addr == 0 && input.bit == 0, "normalized none");
  auto& main_blob = Preferences::db[NS_CFG].at(KEY_MAIN_IN_BLOB).blob;
  main_blob[2] = 4; checksum(main_blob);
  check(loadMainInput(p, 1, 0).type == 0, "invalid checksummed input fails closed");
  check(savePreferredOutput(p, 0x30) && loadPreferredOutput(p) == 0x30, "output roundtrip");
  check(!savePreferredOutput(p, 0xFF) && loadPreferredOutput(p) == 0x30, "invalid output rejected");
  check(savePreferredOutput(p, 0), "auto output preserved");
  check(routeKey(0x20, 0) == String("r1_20") && routeKey(0x20, 7) == String("r8_20"), "all route keys compatible");
  check(p.begin(NS_CFG), "open rule namespace");
  InputActionRuleSettings rule{true, 2, 0x20, 7, 2, 0x20, 6};
  check(saveRuleOpen(p, 1, rule), "rule save");
  const size_t rule_writes = Preferences::writes;
  check(saveRuleOpen(p, 1, rule) && Preferences::writes == rule_writes, "rule write deduplication");
  auto loaded_rule = loadRuleOpen(p, 1, 0, 0);
  check(loaded_rule.enabled && loaded_rule.source_bit == 7 && loaded_rule.target_bit == 6, "rule roundtrip");
  Preferences::db[NS_CFG].at("ar1_v1").blob[5] ^= 1;
  check(!loadRuleOpen(p, 1, 0, 0).enabled, "corrupt rule disabled");
  p.end();

  Preferences::reset();
  check(p.begin(NS_NET), "open network");
  check(putStringVerified(p, KEY_MQTT_USER, String("")), "empty string saves despite zero byte return");
  check(putStringVerified(p, KEY_MQTT_CA, String("-----BEGIN CERTIFICATE-----\nCA\n-----END CERTIFICATE-----")), "certificate exact save");
  const size_t empty_writes = Preferences::writes;
  check(putStringVerified(p, KEY_MQTT_USER, String("")) && Preferences::writes == empty_writes, "empty string no-op");
  Preferences::drop_write = Preferences::writes + 1;
  check(!putStringVerified(p, KEY_WIFI_PASS, String("new-pass")), "dropped nonempty string write rejected");
  Preferences::drop_write = Preferences::writes + 1;
  check(!putStringVerified(p, KEY_WIFI_SSID, String("")), "dropped empty string write rejected");
  Preferences::drop_write = 0;
  check(putBoolVerified(p, KEY_MQTT_ENABLED, false), "missing false key really written");
  check(putUCharVerified(p, KEY_LED_BRIGHTNESS, 0), "missing zero byte really written");
  check(putUShortVerified(p, KEY_MQTT_PORT, 0), "missing zero short really written");
  p.end();
  check(saveStatusLed(p, true, 18), "LED save");
  check(savePowerSave(p, false, 30), "eco save");
  const size_t config_writes = Preferences::writes;
  check(saveStatusLed(p, true, 18) && savePowerSave(p, false, 30) &&
        Preferences::writes == config_writes, "unchanged LED/eco no writes");
  Preferences::reject_write = Preferences::writes + 2;
  check(!saveStatusLed(p, false, 50), "second LED key failure reported");
  check(Preferences::db[NS_NET].at(KEY_LED_ENABLED).number == 1 &&
        Preferences::db[NS_NET].at(KEY_LED_BRIGHTNESS).number == 18 && !p.active(), "LED pair rollback on failure");
  Preferences::reject_write = Preferences::writes + 2;
  check(!savePowerSave(p, true, 40), "second eco key failure reported");
  check(Preferences::db[NS_NET].at(KEY_POWER_SAVE_ENABLED).number == 0 &&
        Preferences::db[NS_NET].at(KEY_POWER_SAVE_IDLE_MIN).number == 30, "eco pair rollback on failure");
  Preferences::reject_write = 0;
  check(saveStatusLed(p, true, 1) && Preferences::db[NS_NET].at(KEY_LED_BRIGHTNESS).number == 10, "LED min clamp");
  check(saveStatusLed(p, true, 255) && Preferences::db[NS_NET].at(KEY_LED_BRIGHTNESS).number == 100, "LED max clamp");
  check(savePowerSave(p, true, 0) && Preferences::db[NS_NET].at(KEY_POWER_SAVE_IDLE_MIN).number == 1, "eco min clamp");
  check(savePowerSave(p, true, 65535) && Preferences::db[NS_NET].at(KEY_POWER_SAVE_IDLE_MIN).number == 1440, "eco max clamp");
  Preferences::fail_begin = true;
  check(!saveControl(p, s) && !saveMainInput(p, 1, 0, 0) && !savePreferredOutput(p, 0x30) &&
        !saveStatusLed(p, true, 20) && !savePowerSave(p, true, 30), "open failures reported");
  Preferences::fail_begin = false;
  Preferences::fail_reads = true;
  check(!saveControl(p, s), "failed readback rejected");
  Preferences::fail_reads = false;
  check(!p.active(), "no leaked settings handle");
  Preferences::reset();
  Preferences::reject_write = 2;
  check(!saveStatusLed(p, false, 50), "fresh LED pair failure reported");
  check(Preferences::db[NS_NET].empty(), "fresh LED failure does not leave partial keys");
  Preferences::reject_write = Preferences::writes + 2;
  check(!savePowerSave(p, true, 40), "fresh eco pair failure reported");
  check(Preferences::db[NS_NET].empty(), "fresh eco failure does not leave partial keys");
  Preferences::reset();
  uint32_t seed = 0x4F464531;
  for (unsigned i = 0; i < 1000; ++i) {
    seed = seed * 1664525U + 1013904223U;
    ControlSettings sample;
    sample.suction = (uint8_t)(seed % 4);
    sample.select_flow = (uint16_t)(100 + seed % 901);
    sample.delay_work_sec = (uint16_t)(seed % 3601);
    sample.delay_stand_sec = (uint16_t)((seed >> 10) % 3601);
    sample.stand_intakes = (seed & 1) != 0;
    sample.continuous = true;
    sample.afterrun_power_enabled = (seed & 2) != 0;
    sample.afterrun_power = (uint16_t)(100 + (seed >> 16) % 901);
    check(saveControl(p, sample), "generated control save");
    controlEqual(loadControl(p), sample);
    check(!p.active(), "generated control closes handles");
  }
  std::cout << "Settings store native regressions passed (production .cpp, fault-injected Preferences)\n";
}
