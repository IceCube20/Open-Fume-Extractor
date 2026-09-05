#pragma once
#include <Arduino.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include "Rs485PeripheralBus.h"

// Authenticated LAN transport. HMAC authenticates packets, it does not encrypt
// them. Credentials are provisioned only over the physical OFE bus.
namespace ofe_wifi {
using namespace jbc_rs485;
constexpr uint16_t PORT = 42585;
constexpr uint8_t CONFIG_VERSION = 1;
enum Mode : uint8_t { AUTOMATIC = 0, WIRED = 1, WIRELESS = 2 };
enum Kind : uint8_t { HELLO = 1, CHALLENGE, PROOF, READY, DATA, LEAVE, KEEPALIVE };
struct Config {
  uint8_t version = CONFIG_VERSION;
  uint8_t mode = AUTOMATIC;
  uint8_t from_master = 1;
  char ssid[33] = {};
  char password[65] = {};
  char host[64] = {};
  uint8_t key[16] = {};
};
static_assert(sizeof(Config) + 1 <= MAX_PAYLOAD, "Display configuration must fit one OFE frame");
inline bool nonzero(const uint8_t* p, size_t n) {
  uint8_t v = 0; while (n--) v |= *p++; return v != 0;
}
inline bool valid(const Config& c) {
  return c.version == CONFIG_VERSION && c.mode <= WIRELESS && c.from_master <= 1 &&
    memchr(c.ssid, 0, sizeof(c.ssid)) && memchr(c.password, 0, sizeof(c.password)) &&
    memchr(c.host, 0, sizeof(c.host));
}
inline bool configured(const Config& c) {
  return valid(c) && c.ssid[0] && c.host[0] && nonzero(c.key, sizeof(c.key));
}
inline uint64_t nonce() {
  uint64_t n; do { esp_fill_random(&n, sizeof(n)); } while (!n); return n;
}
inline bool mac(const uint8_t key[16], const uint8_t* data, size_t len, uint8_t out[32]) {
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 16, data, len, out) == 0;
}
inline bool equal(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0; while (n--) d |= *a++ ^ *b++; return d == 0;
}
inline void hex(const uint8_t* data, size_t n, char* out) {
  static const char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; ++i) { out[2*i] = digits[data[i] >> 4]; out[2*i+1] = digits[data[i] & 15]; }
  out[2*n] = 0;
}
inline bool unhex(const char* text, uint8_t* out, size_t n) {
  if (strlen(text) != n * 2) return false;
  for (size_t i = 0; i < n; ++i) {
    uint8_t value = 0;
    for (uint8_t j = 0; j < 2; ++j) {
      char c = text[2*i+j]; uint8_t v;
      if (c >= '0' && c <= '9') v = c-'0';
      else if (c >= 'a' && c <= 'f') v = c-'a'+10;
      else if (c >= 'A' && c <= 'F') v = c-'A'+10;
      else return false;
      value = (value << 4) | v;
    }
    out[i] = value;
  }
  return true;
}
struct Packet {
  uint8_t kind = 0, mode = 0;
  uint64_t uid = 0, client = 0, server = 0;
  uint32_t counter = 0;
  uint8_t length = 0;
  uint8_t body[5 + MAX_PAYLOAD] = {};
};
constexpr size_t HEADER = 35, TAG = 16, PACKET_MAX = HEADER + 5 + MAX_PAYLOAD + TAG;
// Firmware data is much larger than an OFE frame. Keep normal control traffic
// byte-for-byte compatible and use a separate authenticated LAN-only envelope
// to avoid one UDP round trip for every 188 firmware bytes.
constexpr uint16_t BULK_DATA_MAX = 1024;
constexpr size_t BULK_HEADER = 40;
constexpr size_t BULK_PACKET_MAX = BULK_HEADER + BULK_DATA_MAX + TAG;
struct BulkPacketView {
  bool ack = false;
  uint8_t mode = 0, status = STATUS_OK;
  uint64_t uid = 0, client = 0, server = 0;
  uint32_t request = 0, offset = 0;
  uint16_t length = 0;
  const uint8_t* data = nullptr;
};
inline bool inSession(const Packet& p, uint64_t uid, uint64_t client, uint64_t server, uint8_t mode, uint32_t last) {
  return server && p.uid==uid && p.client==client && p.server==server && p.mode==mode && p.counter>last;
}
inline size_t encode(const Packet& p, const uint8_t key[16], uint8_t* out) {
  if (p.length > sizeof(p.body)) return 0;
  memcpy(out, "OFD1", 4); out[4] = p.kind; out[5] = p.mode;
  put_u64_le(out+6, p.uid); put_u64_le(out+14, p.client); put_u64_le(out+22, p.server);
  put_u32_le(out+30, p.counter); out[34] = p.length;
  memcpy(out+HEADER, p.body, p.length);
  uint8_t digest[32];
  if (!mac(key, out, HEADER+p.length, digest)) return 0;
  memcpy(out+HEADER+p.length, digest, TAG);
  return HEADER+p.length+TAG;
}
inline bool decode(const uint8_t* in, size_t size, const uint8_t key[16], Packet& p) {
  if (size < HEADER+TAG || memcmp(in, "OFD1", 4) || in[34] > sizeof(p.body) ||
      size != HEADER+in[34]+TAG || in[4] < HELLO || in[4] > KEEPALIVE || in[5] > WIRELESS) return false;
  uint8_t digest[32];
  if (!mac(key, in, size-TAG, digest) || !equal(in+size-TAG, digest, TAG)) return false;
  p.kind=in[4]; p.mode=in[5]; p.uid=get_u64_le(in+6); p.client=get_u64_le(in+14);
  p.server=get_u64_le(in+22); p.counter=get_u32_le(in+30); p.length=in[34];
  memcpy(p.body, in+HEADER, p.length); return true;
}
inline void setFrame(Packet& p, const Frame& f) {
  p.length = 5+f.len; p.body[0]=f.dst; p.body[1]=f.src; p.body[2]=f.seq; p.body[3]=f.cmd;
  p.body[4]=f.len; memcpy(p.body+5, f.payload, f.len);
}
inline bool getFrame(const Packet& p, Frame& f) {
  if (p.length < 5 || p.body[4] > MAX_PAYLOAD || p.length != 5+p.body[4]) return false;
  f.dst=p.body[0]; f.src=p.body[1]; f.seq=p.body[2]; f.cmd=p.body[3]; f.len=p.body[4];
  memcpy(f.payload,p.body+5,f.len); return true;
}
inline int socketOpen(uint16_t port) {
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) return -1;
  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return -1; }
  sockaddr_in local = {}; local.sin_family=AF_INET; local.sin_port=htons(port);
  local.sin_addr.s_addr=INADDR_ANY;
  if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) { close(fd); return -1; }
  return fd;
}
inline bool sendPacket(int fd, const sockaddr_in& to, const Packet& p, const uint8_t key[16]) {
  uint8_t raw[PACKET_MAX]; size_t n=encode(p,key,raw);
  return n && sendto(fd,raw,n,MSG_DONTWAIT,reinterpret_cast<const sockaddr*>(&to),sizeof(to)) == (int)n;
}
inline bool samePeer(const sockaddr_in& a, const sockaddr_in& b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}
inline size_t encodeBulk(bool ack, uint8_t mode, uint8_t status, uint64_t uid,
                         uint64_t client, uint64_t server, uint32_t request,
                         uint32_t offset, const uint8_t* data, uint16_t length,
                         const uint8_t key[16], uint8_t* out) {
  if (length > BULK_DATA_MAX || (length && !data)) return 0;
  memcpy(out, ack ? "OFA1" : "OFB1", 4);
  out[4] = mode; out[5] = status;
  put_u64_le(out + 6, uid); put_u64_le(out + 14, client); put_u64_le(out + 22, server);
  put_u32_le(out + 30, request); put_u32_le(out + 34, offset); put_u16_le(out + 38, length);
  if (length) memcpy(out + BULK_HEADER, data, length);
  uint8_t digest[32];
  if (!mac(key, out, BULK_HEADER + length, digest)) return 0;
  memcpy(out + BULK_HEADER + length, digest, TAG);
  return BULK_HEADER + length + TAG;
}
inline bool decodeBulk(const uint8_t* in, size_t size, const uint8_t key[16], BulkPacketView& p) {
  if (!in || size < BULK_HEADER + TAG) return false;
  const bool ack = memcmp(in, "OFA1", 4) == 0;
  if (!ack && memcmp(in, "OFB1", 4) != 0) return false;
  const uint16_t length = get_u16_le(in + 38);
  if (length > BULK_DATA_MAX || size != BULK_HEADER + length + TAG || in[4] > WIRELESS) return false;
  uint8_t digest[32];
  if (!mac(key, in, size - TAG, digest) || !equal(in + size - TAG, digest, TAG)) return false;
  p.ack = ack; p.mode = in[4]; p.status = in[5];
  p.uid = get_u64_le(in + 6); p.client = get_u64_le(in + 14); p.server = get_u64_le(in + 22);
  p.request = get_u32_le(in + 30); p.offset = get_u32_le(in + 34);
  p.length = length; p.data = length ? in + BULK_HEADER : nullptr;
  return true;
}
inline bool sendBulkPacket(int fd, const sockaddr_in& to, bool ack, uint8_t mode,
                           uint8_t status, uint64_t uid, uint64_t client,
                           uint64_t server, uint32_t request, uint32_t offset,
                           const uint8_t* data, uint16_t length, const uint8_t key[16]) {
  uint8_t raw[BULK_PACKET_MAX];
  const size_t n = encodeBulk(ack, mode, status, uid, client, server, request,
                              offset, data, length, key, raw);
  return n && sendto(fd, raw, n, MSG_DONTWAIT,
                     reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == (int)n;
}
} // namespace ofe_wifi
