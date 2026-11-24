#include <Arduino.h>
#include <ETH.h>
#include <Network.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp32-hal-hosted.h>
#include <esp_arduino_version.h>
#include <esp_hosted.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include "TrialPolicy.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4) || CONFIG_ESP32P4_SELECTS_REV_LESS_V3
#error "Only Waveshare P4 PoE, post-V3 / revision 3.2 is supported."
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "Use Arduino-ESP32 3.3.12. This is a C6-only trial with the existing host SDK."
#endif

static uint8_t* staged = nullptr;
static const c6_trial::Image* staged_image = nullptr;
static bool ready = false, restart_required = false;
static bool recovery_confirmed = false;
static char command[96];
static size_t command_size = 0;
static bool command_overflow = false;

static bool lan_ready() { return ETH.linkUp() && ETH.hasIP(); }
static bool board_ready() {
  uint32_t a = 0, b = 0, c = 0;
  hostedGetHostVersion(&a, &b, &c);
  return ESP.getChipRevision() == 302 && ESP.getFlashChipSize() == 32U * 1024 * 1024 &&
    ESP.getPsramSize() >= 32U * 1024 * 1024 && a == 2 && b == 12 && c == 13;
}
static bool query_version(uint32_t& value) {
  esp_hosted_coprocessor_fwver_t v{};
  const esp_err_t result = esp_hosted_get_coprocessor_fwversion(&v);
  if (result != ESP_OK || v.major1 > 255 || v.minor1 > 255 || v.patch1 > 255) {
    Serial.printf("C6 RPC failed: %s\n", esp_err_to_name(result)); return false;
  }
  value = (v.major1 << 16) | (v.minor1 << 8) | v.patch1;
  Serial.printf("C6=%lu.%lu.%lu\n", (unsigned long)v.major1, (unsigned long)v.minor1, (unsigned long)v.patch1);
  return value != 0;
}
static bool verify_image(const c6_trial::Image& image, const uint8_t* bytes) {
  if (!bytes || bytes[0] != 0xE9 || bytes[12] != 13 || bytes[13] != 0) return false;
  uint8_t digest[32]; char hex[65];
  if (mbedtls_sha256(bytes, image.size, digest, 0) != 0) return false;
  for (unsigned i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", unsigned(digest[i]));
  return strcmp(hex, image.sha256) == 0;
}
static void discard() {
  if (staged) heap_caps_free(staged);
  staged = nullptr; staged_image = nullptr;
}
static void info() {
  Serial.printf("TRIAL ONLY: P4 revision=%u, flash=%lu MiB; host=2.12.13 (unchanged)\n",
    ESP.getChipRevision(), (unsigned long)(ESP.getFlashChipSize() / 1048576));
  Serial.printf("LAN=%s IP=%s heap=%lu PSRAM=%lu\n", lan_ready() ? "ready" : "offline",
    ETH.localIP().toString().c_str(), (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());
  if (restart_required) { Serial.println("OTA attempted; only RESTART is allowed."); return; }
  uint32_t version = 0;
  if (!ready || !query_version(version)) return;
  Preferences p;
  if (p.begin("ofe-c6trial", false)) {
    const uint32_t expected = p.getUInt("expected", 0);
    if (expected) {
      Serial.println(expected == version ? "Previous C6 transfer confirmed after boot." : "Previous C6 transfer NOT confirmed.");
      if (expected == version) p.remove("expected");
    }
    p.end();
  }
  if (!c6_trial::allowed(version)) Serial.println("Unknown C6 version: this tool refuses to write.");
}
static void fetch_image(const c6_trial::Image& image) {
  discard();
  if (!ready || !board_ready() || !lan_ready()) { Serial.println("Board/SDK/LAN not ready."); return; }
  Network.setDefaultInterface(ETH);
  if (time(nullptr) < 1760000000) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    const uint32_t start = millis();
    while (time(nullptr) < 1760000000 && uint32_t(millis() - start) < 30000 && lan_ready()) delay(100);
  }
  if (time(nullptr) < 1760000000) { Serial.println("No valid time for TLS; download refused."); return; }
  staged = static_cast<uint8_t*>(heap_caps_malloc(image.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!staged) { Serial.println("Not enough PSRAM."); return; }
  esp_http_client_config_t cfg{};
  cfg.url = image.url; cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000; cfg.disable_auto_redirect = true;
  auto client = esp_http_client_init(&cfg);
  bool ok = client && esp_http_client_open(client, 0) == ESP_OK;
  if (ok) {
    const int64_t length = esp_http_client_fetch_headers(client);
    ok = esp_http_client_get_status_code(client) == 200 && length == image.size;
  }
  uint32_t received = 0, start = millis();
  while (ok && received < image.size) {
    if (!lan_ready() || uint32_t(millis() - start) >= 120000) { ok = false; break; }
    const uint32_t count = image.size - received > 4096 ? 4096 : image.size - received;
    const int n = esp_http_client_read(client, reinterpret_cast<char*>(staged + received), count);
    if (n <= 0) { ok = false; break; }
    received += n; delay(1);
  }
  if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
  if (!ok || received != image.size || !verify_image(image, staged)) {
    Serial.println("Download/header/size/SHA256 failed. Nothing written to C6."); discard(); return;
  }
  staged_image = &image;
  Serial.printf("Verified C6 %s: %lu bytes, SHA256 %s. NOT flashed.\n", image.text, (unsigned long)image.size, image.sha256);
}
struct TrialIO {
  bool verify(const c6_trial::Image& image, const uint8_t* bytes) { return staged_image == &image && verify_image(image, bytes); }
  bool board() { return board_ready(); }
  bool lan() { return lan_ready(); }
  bool version(uint32_t& v) { return query_version(v); }
  uint32_t now() { return millis(); }
  bool mark(uint32_t value) {
    Preferences p; if (!p.begin("ofe-c6trial", false)) return false;
    const bool ok = p.putUInt("expected", value) == 4 && p.getUInt("expected", 0) == value;
    p.end(); return ok;
  }
  bool begin() { restart_required = true; return esp_hosted_slave_ota_begin() == ESP_OK; }
  bool write(const uint8_t* data, uint32_t size) { return esp_hosted_slave_ota_write(const_cast<uint8_t*>(data), size) == ESP_OK; }
  bool end() { return esp_hosted_slave_ota_end() == ESP_OK; }
  bool activate() { return esp_hosted_slave_ota_activate() == ESP_OK; }
  void yield() { delay(1); }
  void progress(uint32_t done, uint32_t total) {
    if (done == total || done % 65536 == 0) Serial.printf("C6 OTA %lu/%lu\n", (unsigned long)done, (unsigned long)total);
  }
};
static void flash_image(const c6_trial::Image& image) {
  if (!recovery_confirmed) { Serial.println("STOP: verify direct C6 UART recovery first, then send RECOVERY CHECKED."); return; }
  if (!ready || staged_image != &image) { Serial.println("FETCH and verify this exact image first."); return; }
  Network.setDefaultInterface(ETH);
  TrialIO io;
  const char* result = c6_trial::transfer(io, image, staged);
  Serial.printf("C6 transfer: %s\n", result);
  discard();
  if (!strcmp(result, "OK")) { Serial.println("Restarting P4; check C6 version after boot."); delay(1000); ESP.restart(); }
}
static void run_command(const char* text) {
  if (!strcmp(text, "RESTART")) { discard(); ESP.restart(); return; }
  if (restart_required) { Serial.println("Restart required. Do not retry OTA in this boot."); return; }
  if (!strcmp(text, "INFO")) info();
  else if (!strcmp(text, "RECOVERY CHECKED")) {
    recovery_confirmed = true; Serial.println("Owner confirms direct C6 UART recovery was checked. Valid for this boot only.");
  }
  else if (!strcmp(text, "FETCH TEST")) fetch_image(c6_trial::TEST);
  else if (!strcmp(text, "FETCH BASELINE")) fetch_image(c6_trial::BASELINE);
  else if (!strcmp(text, "FLASH TEST 2.13.1")) flash_image(c6_trial::TEST);
  else if (!strcmp(text, "RESTORE BASELINE 2.12.13")) flash_image(c6_trial::BASELINE);
  else if (!strcmp(text, "DISCARD")) discard();
  else if (!strcmp(text, "SCAN") && ready) {
    const int count = WiFi.scanNetworks(); Serial.printf("WLAN scan: %d networks\n", count); WiFi.scanDelete();
  } else Serial.println("Commands: INFO | SCAN | FETCH TEST | RECOVERY CHECKED | FLASH TEST 2.13.1 | FETCH BASELINE | RESTORE BASELINE 2.12.13 | DISCARD | RESTART");
}
void setup() {
  Serial.begin(115200); delay(1500);
  Serial.println("\nC6 2.13.1 TRIAL ONLY: Waveshare PoE P4 rev 3.2 / 32 MiB.");
  Serial.println("Nothing is flashed at startup. Host stays 2.12.13. This is NOT a full 2.13.1 host-SDK test.");
  if (!board_ready()) { Serial.println("Wrong board/SDK. Writes disabled."); return; }
  if (!WiFi.setPins(18, 19, 14, 15, 16, 17, 54)) return;
  WiFi.STA.begin();
  if (!hostedIsInitialized()) { Serial.println("Hosted initialization failed."); return; }
  if (!ETH.begin(ETH_PHY_IP101, 1, 31, 52, 51, EMAC_CLK_EXT_IN)) return;
  const uint32_t start = millis();
  while (!lan_ready() && uint32_t(millis() - start) < 30000) delay(100);
  ready = true; info(); run_command("HELP");
}
void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (command_size && !command_overflow) { command[command_size] = 0; run_command(command); }
      command_size = 0; command_overflow = false;
    } else if (command_size < sizeof(command) - 1 && !command_overflow) command[command_size++] = c;
    else command_overflow = true;
  }
  delay(10);
}
