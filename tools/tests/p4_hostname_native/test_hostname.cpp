#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static uint8_t factory_mac[6] = {0xE8, 0xF6, 0x0A, 0xE2, 0x51, 0xC1};
static int reads = 0;
static int result = 0;
constexpr int ESP_OK = 0;
static int esp_efuse_mac_get_default(uint8_t* mac) {
  ++reads;
  if (result == ESP_OK) std::memcpy(mac, factory_mac, sizeof(factory_mac));
  return result;
}
#include "hostname.inc.h"

int main() {
  Hostname first;
  first.prepareHostname();
  assert(std::strcmp(first.device_hostname_, "OFE-Display-P4-800x480-E251C1") == 0);
  assert(reads == 1);
  first.prepareHostname();
  assert(reads == 1);
  assert(std::strlen(first.device_hostname_) < sizeof(first.device_hostname_));

  factory_mac[3] = 0; factory_mac[4] = 1; factory_mac[5] = 0xAF;
  Hostname second;
  second.prepareHostname();
  assert(std::strcmp(second.device_hostname_, "OFE-Display-P4-800x480-0001AF") == 0);
  assert(std::strcmp(first.device_hostname_, second.device_hostname_) != 0);

  result = -1;
  Hostname failed;
  failed.prepareHostname();
  assert(std::strcmp(failed.device_hostname_, "OFE-Display-P4-800x480-000000") == 0);
  assert(std::strlen(failed.device_hostname_) < sizeof(failed.device_hostname_));
  std::puts("PASS: real P4 hostname method: factory eFuse MAC, byte order, leading zeros, stable cache and read failure");
}
