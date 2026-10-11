#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <map>
#include <string>

class String : public std::string {
public:
  using std::string::string;
  String(const std::string& text) : std::string(text) {}
  void toUpperCase() {
    std::transform(begin(), end(), begin(), [](unsigned char c) { return (char)std::toupper(c); });
  }
};

struct Web {
  std::map<std::string, String> args;
  int status = 0;
  std::string response;
  String arg(const char* key) { return args[key]; }
  void send(int code, const char*, const String& body) { status = code; response = body; }
} web;

struct ModuleRecord {
  uint64_t uid;
  bool online;
  uint8_t addr = 32;
  uint8_t type = 3;
  uint8_t filter_mode = 1;
};
struct Registry {
  ModuleRecord module{0x200000001234ABCDULL, true};
  const ModuleRecord* find(uint8_t addr) { return addr == 32 ? &module : nullptr; }
} registry;
struct Scheduler {
  bool ota = false;
  bool moduleFirmwareUpdateActive() { return ota; }
  uint8_t moduleFirmwareUpdateTarget() { return 32; }
} scheduler;
struct Esp {
  uint64_t getEfuseMac() { return 0x1234ABCDULL; }
} ESP;
static const char* web_csrf_token = "0123456789ABCDEF";
static void web_csrf_init() {}
static String uid_hex(uint64_t uid) {
  char result[17];
  std::snprintf(result, sizeof(result), "%016llX", (unsigned long long)uid);
  return result;
}
#define MASTER_FW_VERSION "2.0.31RC"
static std::string dispatched;
static bool status_led_enabled = true;
static uint8_t status_led_brightness_pct = 80;
static uint32_t mqtt_last_publish_ms = 100;
static bool save_ok = true, command_ok = true;
static bool save_status_led_config(bool enabled, uint8_t brightness) {
  if (!save_ok) return false;
  status_led_enabled = enabled; status_led_brightness_pct = brightness;
  dispatched = "status_led"; return true;
}
static String mqtt_main_input_label(uint8_t type, uint8_t addr, uint8_t bit) {
  return type == 3 && addr == 32 && bit == 40 ? "0x20 Valve IN3" : "No input";
}
static bool mqtt_find_main_input_by_label(const String& value, uint8_t& type, uint8_t& addr, uint8_t& bit) {
  if (value == "No input") { type = 0; addr = 0; bit = 0; return true; }
  if (value != "0x20 Valve IN3") return false;
  type = 3; addr = 32; bit = 40; return true;
}
static String mqtt_main_output_label(uint8_t addr) { return addr ? "0x20 Fan" : "Auto"; }
static bool mqtt_find_main_output_by_label(const String& value, uint8_t& addr) {
  if (value == "Auto") { addr = 0; return true; }
  if (value != "0x20 Fan") return false;
  addr = 32; return true;
}
static bool mqtt_module_provides_extractor_output(const ModuleRecord& module) { return module.online; }
static bool master_cmd_set_main_input(uint8_t type, uint8_t addr, uint8_t bit) {
  assert((type == 3 && addr == 32 && bit == 40) || (type == 0 && addr == 0 && bit == 0));
  if (command_ok) dispatched = "main_input";
  return command_ok;
}
static bool master_cmd_set_preferred_output(uint8_t addr) {
  assert(addr == 0 || addr == 32);
  if (command_ok) dispatched = "main_output";
  return command_ok;
}
#define HANDLER(name, action) static void name() { dispatched = action; web.send(200, "text/plain", "ok"); }
HANDLER(web_handle_jbc_settings, "extractor")
HANDLER(web_handle_universal_entity, "entity")
HANDLER(web_handle_display_set, "display")
HANDLER(web_handle_io_set, "io")
HANDLER(web_handle_module_output_set, "output")
HANDLER(web_handle_weller_set, "weller")
HANDLER(web_handle_module_reboot, "module_reboot")
HANDLER(web_handle_restart, "restart")
static constexpr uint8_t HA_JBC_CONTROL_COUNT = 69;
static bool ha_jbc_control_payload(const ModuleRecord&, unsigned long id, float value, uint8_t*, uint8_t& len) {
  len = 4; return id == 0 && std::isfinite(value) && value >= 50 && value <= 500;
}
static bool master_cmd_set_jbc_usb_config(uint8_t addr, uint8_t*, uint8_t len) {
  assert(addr == 32 && len == 4); dispatched = "jbc_usb"; return command_ok;
}
static constexpr uint8_t MODULE_FAN_IO_PRO = 3;
static uint8_t mqtt_fanio_filter_mode(const ModuleRecord& rec) { return rec.filter_mode; }
static bool master_cmd_calibrate_fanio_filter(uint8_t addr, uint8_t action, uint16_t warn, uint16_t full) {
  assert(addr == 32 && action == 7 && !warn && !full);
  dispatched = "fanio_reset_filter"; return command_ok;
}

#include "../../../OpenFumeExtractorMaster/src/WebHomeAssistant.inc.h"

static void reset(const char* action = "entity") {
  web.args = {{"action", action}, {"addr", "32"}, {"uid", "200000001234abcd"}};
  web.status = 0;
  web.response.clear();
  dispatched.clear();
  registry.module.online = true;
  scheduler.ota = false;
  save_ok = command_ok = true;
}

int main() {
  for (uint8_t type : {2, 3, 5}) for (uint8_t mode : {0, 1, 2, 3}) {
    reset("fanio_reset_filter"); registry.module.type = type; registry.module.filter_mode = mode;
    web_handle_ha_control();
    const bool allowed = type == 3 && (mode == 1 || mode == 3);
    assert(web.status == (allowed ? 200 : 409));
    assert(dispatched == (allowed ? "fanio_reset_filter" : ""));
  }
  registry.module.type = 3; registry.module.filter_mode = 1;
  reset("fanio_reset_filter"); command_ok = false;
  web_handle_ha_control(); assert(web.status == 503);
  reset("fanio_reset_filter"); scheduler.ota = true;
  web_handle_ha_control(); assert(web.status == 409 && dispatched.empty());
  reset("fanio_reset_filter"); registry.module.online = false;
  web_handle_ha_control(); assert(web.status == 409 && dispatched.empty());
  reset("jbc_usb"); web.args["control"] = "0"; web.args["value"] = "350";
  web_handle_ha_control();
  assert(web.status == 202 && dispatched == "jbc_usb");
  for (const char* id : {"", "-1", "69", "0junk"}) {
    reset("jbc_usb"); web.args["control"] = id; web.args["value"] = "350";
    web_handle_ha_control();
    assert(web.status == 400 && dispatched.empty());
  }
  for (const char* value : {"", "nan", "inf", "350junk", "49", "501"}) {
    reset("jbc_usb"); web.args["control"] = "0"; web.args["value"] = value;
    web_handle_ha_control();
    assert(web.status == 409 && dispatched.empty());
  }
  reset("jbc_usb"); web.args["control"] = "0"; web.args["value"] = "350"; scheduler.ota = true;
  web_handle_ha_control();
  assert(web.status == 409 && dispatched.empty());
  for (const char* action : {"extractor", "entity", "display", "io", "output", "weller", "module_reboot", "restart"}) {
    reset(action);
    web_handle_ha_control();
    assert(web.status == 200 && dispatched == action);
  }
  for (const char* address : {"", "-1", "256", "32garbage"}) {
    reset();
    web.args["addr"] = address;
    web_handle_ha_control();
    assert(web.status == 400 && dispatched.empty());
  }
  reset();
  web.args["uid"] = "20000000DEADBEEF";
  web_handle_ha_control();
  assert(web.status == 409 && dispatched.empty());
  reset();
  registry.module.online = false;
  web_handle_ha_control();
  assert(web.status == 409 && dispatched.empty());
  reset();
  web.args["addr"] = "33";
  web_handle_ha_control();
  assert(web.status == 409 && dispatched.empty());
  reset();
  scheduler.ota = true;
  web_handle_ha_control();
  assert(web.status == 409 && dispatched.empty());
  reset("unknown");
  web_handle_ha_control();
  assert(web.status == 400 && dispatched.empty());
  reset("status_led");
  web.args.erase("addr"); web.args.erase("uid");
  web.args["setting"] = "enabled"; web.args["value"] = "0";
  web_handle_ha_control();
  assert(web.status == 200 && !status_led_enabled && status_led_brightness_pct == 80);
  for (const char* value : {"", "9", "101", "-1", "20abc", "nan"}) {
    reset("status_led"); web.args["setting"] = "brightness"; web.args["value"] = value;
    web_handle_ha_control();
    assert(web.status == 400 && dispatched.empty());
  }
  reset("status_led"); web.args["setting"] = "brightness"; web.args["value"] = "22";
  web_handle_ha_control();
  assert(web.status == 200 && status_led_brightness_pct == 22 && !status_led_enabled);
  reset("status_led"); web.args["setting"] = "enabled"; web.args["value"] = "1"; save_ok = false;
  web_handle_ha_control();
  assert(web.status == 507 && dispatched.empty() && !status_led_enabled);
  for (const char* action : {"main_input", "main_output"}) {
    reset(action); web.args.erase("addr"); web.args.erase("uid");
    web.args["value"] = action == std::string("main_input") ? "0x20 Valve IN3" : "0x20 Fan";
    web_handle_ha_control();
    assert(web.status == 200 && dispatched == action);
    reset(action); web.args["value"] = "old alias";
    web_handle_ha_control();
    assert(web.status == 409 && dispatched.empty());
    reset(action); command_ok = false;
    web.args["value"] = action == std::string("main_input") ? "No input" : "Auto";
    web_handle_ha_control();
    assert(web.status == 503 && dispatched.empty());
  }
  reset("main_output"); registry.module.online = false; web.args["value"] = "0x20 Fan";
  web_handle_ha_control();
  assert(web.status == 409 && dispatched.empty());
  web_handle_ha_session();
  assert(web.status == 200);
  assert(web.response.find("\"master_uid\":\"000000001234ABCD\"") != std::string::npos);
  assert(web.response.find("\"api_version\":1") != std::string::npos);
  assert(web.response.find("\"csrf\":\"0123456789ABCDEF\"") != std::string::npos);
  std::cout << "Master HA API: identity/session/LED/routing/dispatch regressions passed\n";
}
