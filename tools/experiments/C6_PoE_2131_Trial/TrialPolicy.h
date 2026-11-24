#pragma once
#include <stdint.h>

namespace c6_trial {
struct Image {
  uint32_t version, size;
  const char *text, *url, *sha256;
};
static constexpr Image BASELINE{0x020C0D, 1226976, "2.12.13",
  "https://raw.githubusercontent.com/espressif/arduino-esp32/0c2733048bd001c87b0d6912843de5f259b2a13e/hosted/esp32c6-v2.12.13.bin",
  "f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9"};
static constexpr Image TEST{0x020D01, 1244160, "2.13.1",
  "https://raw.githubusercontent.com/espressif/arduino-esp32/0c2733048bd001c87b0d6912843de5f259b2a13e/hosted/esp32c6-v2.13.1.bin",
  "2d8a98801680f1feccdc8c20ace05617c425d4f8f5041fd5b17cb8dc802c643f"};
inline bool allowed(uint32_t version) { return version == BASELINE.version || version == TEST.version; }

// Only this isolated trial can switch between these two exact, verified images.
template<class IO> const char* transfer(IO& io, const Image& target, const uint8_t* data) {
  if (&target != &BASELINE && &target != &TEST) return "UNKNOWN_TARGET";
  if (!data || !io.verify(target, data)) return "IMAGE_NOT_VERIFIED";
  if (!io.board()) return "WRONG_BOARD_OR_SDK";
  if (!io.lan()) return "LAN_REQUIRED";
  uint32_t current = 0;
  if (!io.version(current) || !allowed(current)) return "UNKNOWN_INSTALLED_VERSION";
  if (current == target.version) return "ALREADY_CURRENT";
  if (!io.mark(target.version)) return "NVS_FAILED";
  if (!io.lan()) return "LAN_REQUIRED";
  const uint32_t start = io.now();
  if (!io.begin()) return "BEGIN_FAILED_RESTART_REQUIRED";
  for (uint32_t offset = 0; offset < target.size;) {
    if (!io.lan()) return "LAN_LOST_RESTART_REQUIRED";
    if (uint32_t(io.now() - start) >= 180000U) return "TIMEOUT_RESTART_REQUIRED";
    const uint32_t count = target.size - offset > 2048 ? 2048 : target.size - offset;
    if (!io.write(data + offset, count)) return "WRITE_FAILED_RESTART_REQUIRED";
    offset += count; io.progress(offset, target.size); io.yield();
  }
  if (!io.lan() || uint32_t(io.now() - start) >= 180000U) return "TRANSFER_EXPIRED_RESTART_REQUIRED";
  if (!io.end()) return "VERIFY_FAILED_RESTART_REQUIRED";
  if (!io.lan()) return "LAN_LOST_RESTART_REQUIRED";
  if (!io.activate()) return "ACTIVATION_UNCONFIRMED_RESTART_REQUIRED";
  return "OK";
}
}
