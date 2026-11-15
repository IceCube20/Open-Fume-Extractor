#include "MasterFirmwareHeader.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <initializer_list>

enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload { int status = 0; uint8_t* buf = nullptr; size_t currentSize = 0; } upload;
struct Web {
  std::map<std::string, std::string> args;
  HTTPUpload& upload() { return ::upload; }
  std::string arg(const char* key) { return args[key]; }
  bool hasArg(const char* key) { return args.count(key) != 0; }
} web;
static bool developer = false, metadata_ok = true, payload_ok = true, crypto_ok = true;
static bool developer_override_active() { return developer; }
static bool firmware_signature_allowed(const std::string& value, const char*) { return metadata_ok && value == "signed"; }
struct Auth {
  bool begin(const std::string& value, uint32_t, const char*) { return metadata_ok && value == "auth"; }
  bool update(const uint8_t*, size_t) { return crypto_ok; }
  bool finish() { return crypto_ok; }
  const char* error() { return "crypto failed"; }
} master_update_auth;
struct Updater {
  unsigned begins = 0, writes = 0, ends = 0, aborts = 0;
  bool begin(size_t) { ++begins; return true; }
  size_t write(const uint8_t*, size_t n) { ++writes; return n; }
  bool end(bool) { ++ends; return true; }
  void abort() { ++aborts; }
} Update;
struct Scheduler { void notifyDisplayUpdate(bool, uint8_t, uint8_t, uint32_t = 0) {} } scheduler;
static constexpr uint8_t ADDR_MASTER = 1;
static constexpr size_t UPDATE_SIZE_UNKNOWN = size_t(-1);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
static constexpr uint8_t CHIP = 18;
#else
static constexpr uint8_t CHIP = 9;
#endif
static MasterFirmwareHeader master_firmware_header(CHIP);
static bool master_update_header_checked = false, master_update_payload_signature_verified = false;
static bool master_update_active = false, master_update_ok = false, master_update_unsafe_fw_type = false;
static uint32_t master_update_offset = 0, master_update_size = 0, master_update_progress = 0;
static uint32_t master_update_speed_bps = 0, master_update_speed_sample_ms = 0, master_update_speed_sample_offset = 0;
static uint32_t master_update_signature_probe_bytes = 0, now = 0;
static uint8_t master_update_signature_overlap[96]{}, master_update_signature_overlap_len = 0;
static char update_status_msg[256]{};
static uint32_t millis() { return now += 500; }
static void master_firmware_signature_marker(char* value, size_t capacity) { std::snprintf(value, capacity, "marker"); }
static void firmware_signature_probe_feed(const uint8_t*, size_t n, const char*, bool& valid,
    uint32_t& bytes, uint8_t*, uint8_t&) { bytes += uint32_t(n); valid = payload_ok; }
static void master_update_signature_reset() {
  master_firmware_header.reset(); master_update_header_checked = false;
  master_update_payload_signature_verified = false;
}
static void master_update_abort_active() {
  Update.abort(); master_update_active = master_update_ok = false; master_update_signature_reset();
}
#include "multipart.h"

static void reset(bool unsafe = false) {
  Update = {}; web.args = {{"size", "32"}, {"sig", "signed"}, {"auth", "auth"}};
  developer = unsafe; if (unsafe) web.args["unsafe"] = "1";
  metadata_ok = payload_ok = crypto_ok = true;
  master_update_active = master_update_ok = master_update_unsafe_fw_type = false;
  update_status_msg[0] = 0; master_update_signature_reset();
}
static void send(int status, uint8_t* data = nullptr, size_t size = 0) {
  upload = {status, data, size}; web_handle_master_update_upload();
}
int main() {
  uint8_t image[32]{}; image[0] = 0xE9; image[1] = 4;
  for (bool unsafe : {false, true}) for (const uint8_t chip : {uint8_t(9), uint8_t(18)}) {
    for (size_t split = 0; split <= sizeof(image); ++split) {
      reset(unsafe); image[12] = chip;
      send(UPLOAD_FILE_START);
      send(UPLOAD_FILE_WRITE, image, split);
      send(UPLOAD_FILE_WRITE, image + split, sizeof(image) - split);
      send(UPLOAD_FILE_END);
      assert(master_update_ok == (chip == CHIP));
      assert(Update.ends == (chip == CHIP ? 1U : 0U));
      assert(!master_update_active);
    }
  }
  reset(); metadata_ok = false; send(UPLOAD_FILE_START); send(UPLOAD_FILE_END);
  assert(Update.begins == 0 && Update.ends == 0);
  reset(); image[12] = CHIP; send(UPLOAD_FILE_START); send(UPLOAD_FILE_WRITE, image, 23); send(UPLOAD_FILE_END);
  assert(!master_update_ok && Update.ends == 0);
  reset(); send(UPLOAD_FILE_START); payload_ok = false; send(UPLOAD_FILE_WRITE, image, 32); send(UPLOAD_FILE_END);
  assert(!master_update_ok && Update.ends == 0);
  reset(); send(UPLOAD_FILE_START); send(UPLOAD_FILE_WRITE, image, 32); crypto_ok = false; send(UPLOAD_FILE_END);
  assert(!master_update_ok && Update.ends == 0);
  reset(); master_update_active = true; send(UPLOAD_FILE_START); send(UPLOAD_FILE_WRITE, image, 32); send(UPLOAD_FILE_END);
  assert(master_update_active && Update.begins == 0 && Update.ends == 0 && Update.aborts == 0);
  std::printf("PASS: actual multipart MCU chip %u: split headers, wrong chip with/without developer override, metadata, payload, crypto, truncation and busy ownership\n", CHIP);
}
