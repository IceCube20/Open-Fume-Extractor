#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "FaeIdentity.h"

struct Store {
  std::vector<uint8_t> bytes;
  bool fail_write = false, fail_read = false;
  unsigned writes = 0;
  size_t getBytesLength(const char*) { return bytes.size(); }
  size_t getBytes(const char*, void* out, size_t capacity) {
    if (fail_read || capacity < bytes.size()) return 0;
    std::memcpy(out, bytes.data(), bytes.size()); return bytes.size();
  }
  size_t putBytes(const char*, const void* data, size_t len) {
    ++writes;
    if (fail_write) return 0;
    const auto* p = static_cast<const uint8_t*>(data);
    bytes.assign(p, p + len); return len;
  }
} prefs;
static uint8_t device_id[ofe_fae_identity::LEGACY_ID_MAX];
static uint8_t device_id_len = 0;
static uint64_t board_uid = 0x0010203040506070ULL;
static uint64_t esp_uid64() { return board_uid; }
static std::vector<uint8_t> captured_payload;
static void local_trace_log(uint8_t, uint8_t, uint8_t, const uint8_t* p, size_t n) {
  captured_payload.assign(p, p + n);
}
struct Serial {
  std::vector<uint8_t> wire;
  void write(const uint8_t* p, size_t n) { wire.assign(p, p + n); }
  void flush() {}
} JBC;
#include "generated_identity.inc.h"

static void expect(uint8_t command, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> expected = {0x10, 0x00, 0x10, command, uint8_t(data.size())};
  expected.insert(expected.end(), data.begin(), data.end());
  assert(captured_payload == expected);
  std::vector<uint8_t> wire = {0x10, 0x02};
  uint8_t checksum = 0x01;
  for (auto b : expected) {
    checksum ^= b; wire.push_back(b); if (b == 0x10) wire.push_back(b);
  }
  wire.push_back(checksum); if (checksum == 0x10) wire.push_back(checksum);
  wire.push_back(0x10); wire.push_back(0x03);
  assert(JBC.wire == wire);
}
static void command(uint8_t ctrl, const uint8_t* data = nullptr, uint8_t len = 0) {
  assert(handle_identity_command(0x10, 0x00, 0x10, ctrl, data, len));
}
int main() {
  using namespace ofe_fae_identity;
  assert(load(prefs, device_id, device_id_len, board_uid, 0x12345678, 0x90ABCDEF));
  assert(device_id_len == 32 && prefs.bytes.size() == 32 && prefs.writes == 1);
  const std::vector<uint8_t> initial = prefs.bytes;
  assert(std::all_of(initial.begin(), initial.end(), [](uint8_t b) {
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'F');
  }));
  assert(load(prefs, device_id, device_id_len, board_uid, 0, 0));
  assert(prefs.writes == 1 && prefs.bytes == initial);
  uint8_t another[LEGACY_ID_MAX], another_len = 0; Store second;
  assert(load(second, another, another_len, board_uid + 1, 0x12345678, 0x90ABCDEF));
  assert(second.bytes != initial);

  command(jbc_fe::M_FIRMWARE);
  const char expected_firmware[] = "02:F2:8886791:0023324:F1_DIS_02";
  assert(std::strcmp(FIRMWARE, expected_firmware) == 0);
  const std::vector<uint8_t> firmware(expected_firmware, expected_firmware + sizeof(expected_firmware) - 1);
  assert(firmware.size() == 31 && std::find(firmware.begin(), firmware.end(), 0) == firmware.end());
  expect(0x21, firmware);
  command(jbc_fe::M_R_DEVICEIDORIGINAL);
  const std::vector<uint8_t> hardware = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
  expect(0x1C, hardware);
  command(jbc_fe::M_R_DEVICEID); expect(0x1E, initial);
  const uint8_t escape_bcc = 0x0E;
  send_read_reply(0x10, 0, 0x10, 0x1E, &escape_bcc, 1);
  expect(0x1E, {escape_bcc}); // The BCC itself is 0x10 and must be doubled.

  const char replacement[] = "849158E467A340159646170D6B1595EF";
  command(jbc_fe::M_W_DEVICEID, reinterpret_cast<const uint8_t*>(replacement), 32);
  expect(0x1F, {0x06});
  const std::vector<uint8_t> changed(replacement, replacement + 32);
  assert(prefs.bytes == changed && device_id_len == 32);
  command(jbc_fe::M_R_DEVICEID); expect(0x1E, changed);
  command(jbc_fe::M_R_DEVICEIDORIGINAL); expect(0x1C, hardware);
  assert(load(prefs, device_id, device_id_len, board_uid, 0, 0));
  assert(std::equal(changed.begin(), changed.end(), device_id));

  const unsigned saved_writes = prefs.writes;
  const uint8_t binary[] = {0x84, 0, 0x91, 0x58};
  command(0x1F, binary, sizeof(binary)); expect(0x15, {0x15});
  command(0x1F); expect(0x15, {0x15});
  uint8_t too_long[33]; std::memset(too_long, 'A', sizeof(too_long));
  command(0x1F, too_long, sizeof(too_long)); expect(0x15, {0x15});
  assert(prefs.writes == saved_writes && prefs.bytes == changed);
  prefs.fail_write = true;
  command(0x1F, reinterpret_cast<const uint8_t*>("new"), 3); expect(0x15, {0x15});
  assert(prefs.bytes == changed && device_id_len == 32);
  command(0x1E); expect(0x1E, changed);

  Store legacy; legacy.bytes.assign(64, 'L');
  assert(load(legacy, another, another_len, board_uid, 0, 0));
  assert(another_len == 64 && legacy.writes == 0);
  legacy.fail_read = true;
  assert(!load(legacy, another, another_len, board_uid, 0, 0));
  assert(another_len == 0 && legacy.writes == 0 && legacy.bytes.size() == 64);
  legacy.fail_read = false; legacy.bytes.assign(65, 'X');
  assert(!load(legacy, another, another_len, board_uid, 0, 0) && legacy.writes == 0);
  Store unavailable; unavailable.fail_write = true;
  assert(!load(unavailable, another, another_len, board_uid, 0, 0));
  assert(another_len == 32 && unavailable.bytes.empty());
  assert(!handle_identity_command(0x10, 0x00, 0x10, 0x99, nullptr, 0));
  std::puts("PASS: actual FAE identity replies, 31/8/32-byte payloads, FID/address/BCC/DLE framing, NVS persistence, legacy retention and failed/invalid writes");
}
