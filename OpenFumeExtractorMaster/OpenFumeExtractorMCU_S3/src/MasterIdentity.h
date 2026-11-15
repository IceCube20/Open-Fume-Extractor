#pragma once
#include <Arduino.h>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <esp_mac.h>
#endif

inline uint64_t master_factory_uid() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // The hosted C6 WiFi MAC is not the master's own factory identity.
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return 0;
  uint64_t uid = 0;
  for (unsigned i = 0; i < sizeof(mac); ++i) uid |= uint64_t(mac[i]) << (8 * i);
  return uid;
#else
  return ESP.getEfuseMac();
#endif
}
