#include <cstdint>
#include <cassert>
#include <cstring>
#include <initializer_list>

constexpr uint32_t CAP_DISPLAY = 1U << 10;
constexpr uint8_t CMD_SET_STATE = 3;
constexpr uint8_t STATUS_OK = 0, STATUS_BAD_LEN = 1;
struct Frame { uint8_t cmd = 0, len = 0, payload[200] = {}; };
struct ModuleRecord {
  bool online = true, display_backlight_supported = true;
  uint32_t caps = CAP_DISPLAY;
  uint8_t display_brightness_pct = 85, display_language = 0, display_theme = 0;
  uint8_t display_screensaver_min = 2, display_backlight_mode = 0, display_backlight_timeout_min = 5;
};
class MasterScheduler {
 public:
  bool module_fw_active_ = false;
  uint8_t module_fw_target_ = 0;
  struct Registry {
    ModuleRecord rec;
    ModuleRecord* find(uint8_t addr) { return addr == 64 ? &rec : nullptr; }
  } registry_;
  unsigned calls = 0, telemetry_calls = 0;
  uint8_t lengths[3] = {}, sent[6] = {};
  uint8_t echo_size = 7;
  bool legacy = false, transport_ok = true;
  bool request(uint8_t, uint8_t cmd, const uint8_t* payload, uint8_t len, Frame& reply, unsigned) {
    lengths[calls++] = len;
    memcpy(sent, payload, len);
    reply.cmd = cmd | 0x80;
    reply.len = echo_size;
    reply.payload[0] = legacy && len != 3 ? STATUS_BAD_LEN : STATUS_OK;
    reply.payload[1] = 70; reply.payload[2] = 1; reply.payload[3] = 0;
    reply.payload[4] = 5; reply.payload[5] = 2; reply.payload[6] = 17;
    return transport_ok;
  }
  void readTelemetry(uint8_t) { ++telemetry_calls; }
  bool setDisplaySettings(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t = 0xFF, uint8_t = 0xFF, uint8_t = 0xFF);
};
#include "generated_backlight_settings.inc.h"

int main() {
  MasterScheduler current;
  assert(current.setDisplaySettings(64, 0xFF, 0xFF, 0xFF, 0xFF, 2, 17));
  assert(current.calls == 1 && current.lengths[0] == 6);
  assert(current.sent[0] == 0xFF && current.sent[4] == 2 && current.sent[5] == 17);
  assert(current.registry_.rec.display_backlight_mode == 2);
  assert(current.registry_.rec.display_backlight_timeout_min == 17);
  assert(current.telemetry_calls == 1);

  for (auto args : {0, 61, 254}) {
    MasterScheduler invalid;
    assert(!invalid.setDisplaySettings(64, 85, 0, 0, 2, 2, uint8_t(args)));
    assert(invalid.calls == 0);
  }
  MasterScheduler invalid_mode;
  assert(!invalid_mode.setDisplaySettings(64, 85, 0, 0, 2, 3, 5) && invalid_mode.calls == 0);
  MasterScheduler old;
  old.registry_.rec.display_backlight_supported = false;
  assert(!old.setDisplaySettings(64, 85, 0, 0, 2, 2, 5) && old.calls == 0);
  old.echo_size = 5;
  assert(old.setDisplaySettings(64, 70, 1, 0, 5));
  assert(old.lengths[0] == 4);

  MasterScheduler legacy;
  legacy.legacy = true; legacy.echo_size = 3;
  assert(legacy.setDisplaySettings(64, 70, 1, 0));
  assert(legacy.calls == 2 && legacy.lengths[0] == 4 && legacy.lengths[1] == 3);
  MasterScheduler no_false_success;
  no_false_success.echo_size = 5;
  assert(!no_false_success.setDisplaySettings(64, 70, 1, 0, 5, 2, 5));
  MasterScheduler no_hidden_saver_change;
  no_hidden_saver_change.legacy = true;
  assert(!no_hidden_saver_change.setDisplaySettings(64, 70, 1, 0, 5));
  assert(no_hidden_saver_change.calls == 1);
  MasterScheduler busy;
  busy.module_fw_active_ = true; busy.module_fw_target_ = 64;
  assert(!busy.setDisplaySettings(64, 85, 0, 0, 2, 2, 5) && busy.calls == 0);
  return 0;
}
