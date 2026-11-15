#include <atomic>
#include <string>
#include "MasterLanWifiPolicy.h"
static MasterLanWifiPolicy master_lan_wifi_policy;
static bool eth_ready=false,master_update_active=false;
static int previous_network=0;
static std::atomic<bool> mqtt_network_reconnect_requested{false};
static struct { bool running=false; bool active() { return running; } } master_display_ble;
static struct { bool running=false; bool firmwareUpdateActive() { return running; } } scheduler;
static bool master_ethernet_ready() { return eth_ready; }
static bool master_wifi_enabled() { return WiFi.getMode()!=WIFI_OFF; }
static bool master_network_ready() { return eth_ready || WiFi.connected; }
static const char* master_network_transport() { return eth_ready ? "Ethernet" : "WiFi"; }
static struct FakeIp { std::string toString() { return "192.0.2.1"; } } network_ip;
static FakeIp master_network_ip() { return network_ip; }
static bool master_network_changed() {
  const int value=eth_ready ? 2 : WiFi.connected ? 1 : 0;
  const bool changed=value!=previous_network; previous_network=value; return changed;
}
