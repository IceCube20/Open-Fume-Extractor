#include <atomic>
#include <string>
#include "MasterLanWifiPolicy.h"
static MasterLanWifiPolicy master_lan_wifi_policy;
static int previous_network=0;
static std::atomic<bool> mqtt_network_reconnect_requested{false};
static bool master_wifi_enabled() { return WiFi.getMode()!=WIFI_OFF; }
static bool master_network_changed() {
  const int value=eth_ready ? 2 : WiFi.connected ? 1 : 0;
  const bool changed=value!=previous_network; previous_network=value; return changed;
}
