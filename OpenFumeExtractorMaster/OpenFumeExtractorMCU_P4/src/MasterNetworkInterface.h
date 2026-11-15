#pragma once
#include "MasterBuildConfig.h"
#include <WiFi.h>
#include "MasterIpv4Settings.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <ETH.h>
#include <Network.h>
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "The P4 MCU preparation is pinned to Arduino-ESP32 3.3.12."
#endif
#if !CONFIG_ESP_HOSTED_ENABLED || !CONFIG_SPIRAM
#error "P4 MCU requires ESP-Hosted and enabled PSRAM."
#endif
#if CONFIG_ESP32P4_REV_MAX_FULL >= 300
#error "This P4 MCU preparation requires the Before v3.00 SDK variant."
#endif
#endif

inline bool master_ethernet_ready() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return ETH.linkUp() && ETH.hasIP();
#else
  return false;
#endif
}
inline bool master_ethernet_supported() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return true;
#else
  return false;
#endif
}
inline bool master_network_ready() {
  return master_ethernet_ready() || WiFi.status() == WL_CONNECTED;
}
inline bool master_wifi_enabled() { return WiFi.getMode()!=WIFI_OFF; }
inline const char* master_network_transport_key() {
  return master_ethernet_ready() ? "ethernet" : WiFi.status()==WL_CONNECTED ? "wifi" :
    (WiFi.getMode() & WIFI_AP) ? "ap" : "offline";
}
inline IPAddress master_network_ip() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (master_ethernet_ready()) return ETH.localIP();
#endif
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
}
inline const char* master_network_transport() {
  return master_ethernet_ready() ? "Ethernet" : WiFi.status() == WL_CONNECTED ? "WiFi" :
    (WiFi.getMode() & WIFI_AP) ? "AP" : "Offline";
}
inline bool master_network_begin(const char* hostname) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // Waveshare ESP32-P4-Module: internal C6 SDIO, not the Elecrow display pinout.
  const bool hosted_pins_ready=WiFi.setPins(18, 19, 14, 15, 16, 17, 54);
  // RMII data pins come from the stock esp32p4 variant; IP101 is TLK110-compatible.
  if (ETH.begin(ETH_PHY_IP101, 1, 31, 52, 51, EMAC_CLK_EXT_IN)) {
    ETH.setHostname(hostname);
    const auto& c=master_ethernet_settings();
    if (c.use_static && (!master_ipv4_valid(c) || !ETH.config(c.ip,c.gateway,c.subnet,c.dns1,c.dns2))) {
      Serial.println("[Network] Ethernet static IP failed; disabling Ethernet, WiFi recovery available");
      ETH.end();
    }
  }
  else Serial.println("[Network] Ethernet initialization failed; WiFi recovery remains available");
  return hosted_pins_ready;
#else
  (void)hostname;
#endif
  return true;
}
inline bool master_network_changed() {
  static uint32_t previous_ip = 0;
  static bool previous_eth = false;
  const uint32_t ip = master_network_ready() ? uint32_t(master_network_ip()) : 0;
  const bool ethernet = master_ethernet_ready();
  if (ip == previous_ip && ethernet == previous_eth) return false;
  previous_ip = ip; previous_eth = ethernet;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (ethernet) Network.setDefaultInterface(ETH);
  else if (WiFi.status() == WL_CONNECTED) Network.setDefaultInterface(WiFi.STA);
#endif
  return true;
}
