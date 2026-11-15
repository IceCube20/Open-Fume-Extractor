#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "../settings_store_native/stubs/Arduino.h"
#include "MasterIpv4Settings.h"
#define WL_CONNECTED 3
#define WIFI_OFF 0
#define WIFI_AP 2
#define ETH_PHY_IP101 1
#define EMAC_CLK_EXT_IN 1
#define ESP_OK 0
struct Interface { int id; };
struct {
  Interface STA{1}; int state=0,pins[7]{}; IPAddress ip{10},ap{4}; bool pins_ok=true; int mode_=WIFI_AP;
  int getMode() { return mode_; }
  int status() { return state; }
  IPAddress localIP() { return ip; }
  IPAddress softAPIP() { return ap; }
  bool setPins(int a,int b,int c,int d,int e,int f,int g) {
    const int values[]={a,b,c,d,e,f,g}; memcpy(pins,values,sizeof(pins)); return pins_ok;
  }
} WiFi;
struct Ethernet:Interface {
  bool online=false; IPAddress ip{20}; const char* hostname=nullptr;
  bool link=true,config_ok=true; unsigned configs=0,ends=0; MasterIpv4Settings configured;
  Ethernet():Interface{2} {}
  bool hasIP() { return online; }
  bool linkUp() { return link; }
  IPAddress localIP() { return ip; }
  bool begin(int phy,int addr,int mdc,int mdio,int reset,int clk) {
    assert(phy==ETH_PHY_IP101 && addr==1 && mdc==31 && mdio==52 && reset==51 && clk==EMAC_CLK_EXT_IN); return true;
  }
  void setHostname(const char* value) { hostname=value; }
  bool config(IPAddress ip,IPAddress gateway,IPAddress subnet,IPAddress dns1,IPAddress dns2) {
    ++configs;configured={true,ip,gateway,subnet,dns1,dns2};return config_ok;
  }
  void end() { ++ends;online=false; }
} ETH;
struct { int selected=0; void setDefaultInterface(Interface& net) { selected=net.id; } } Network;
struct { void println(const char*) {} } Serial;
struct { uint64_t getEfuseMac() { return 0x010203040506ULL; } } ESP;
inline int esp_efuse_mac_get_default(uint8_t* bytes) {
  const uint8_t mac[]={1,2,3,4,5,6}; memcpy(bytes,mac,sizeof(mac)); return 0;
}
#include "generated_master_network.h"
#include "generated_master_identity.h"
#include "MasterBoardConfig.h"
int main() {
  assert(!master_network_ready()); assert(uint32_t(master_network_ip())==4);
  assert(!strcmp(master_network_transport(),"AP")); assert(master_network_begin("ofe-test"));
  WiFi.state=WL_CONNECTED;
  assert(master_network_ready()); assert(uint32_t(master_network_ip())==10);
  assert(master_network_changed()); assert(!master_network_changed());
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  assert(RS485_RX_PIN==21 && RS485_TX_PIN==20 && OFE_STATUS_LED_ENABLE);
  const int pins[]={18,19,14,15,16,17,54}; assert(!memcmp(WiFi.pins,pins,sizeof(pins)));
  assert(!strcmp(ETH.hostname,"ofe-test")); assert(Network.selected==1);
  WiFi.pins_ok=false; ETH.hostname=nullptr;
  assert(!master_network_begin("ofe-recovery")); assert(!strcmp(ETH.hostname,"ofe-recovery"));
  ETH.online=true; assert(master_network_changed()); assert(Network.selected==2);
  assert(uint32_t(master_network_ip())==20 && master_ethernet_ready());
  assert(!strcmp(master_network_transport(),"Ethernet"));
  ETH.ip=IPAddress(21); assert(master_network_changed());
  ETH.online=false; assert(master_network_changed()); assert(Network.selected==1);
  WiFi.state=0; assert(master_network_changed()); assert(!master_network_ready());
  ETH.online=true; assert(master_network_changed()); assert(master_network_ready());
  assert(uint32_t(master_network_ip())==21); assert(Network.selected==2);
  assert(master_factory_uid()==0x060504030201ULL);
  assert(master_ethernet_supported());
  ETH.link=false; assert(!master_ethernet_ready()); ETH.link=true;
  auto& c=master_ethernet_settings();
  c={true,IPAddress(192,0,2,30),IPAddress(192,0,2,1),IPAddress(255,255,255,0),IPAddress(192,0,2,2),IPAddress(1,1,1,1)};
  WiFi.pins_ok=true;assert(master_network_begin("static-lan"));
  assert(ETH.configs==1 && ETH.configured.ip==c.ip && ETH.configured.gateway==c.gateway && ETH.configured.subnet==c.subnet && ETH.configured.dns1==c.dns1 && ETH.configured.dns2==c.dns2);
  ETH.config_ok=false;assert(master_network_begin("failed-static-lan"));assert(ETH.ends==1 && !master_ethernet_ready());
  std::cout<<"PASS: actual P4 network helpers: SDIO/PHY pins, LAN priority, DHCP address change, WiFi failover and P4 factory UID\n";
#else
  assert(!master_ethernet_ready()); assert(master_factory_uid()==ESP.getEfuseMac());
  assert(!master_ethernet_supported());
  assert(Network.selected==0);
  std::cout<<"PASS: actual S3 helpers preserve WiFi/AP behavior and factory UID\n";
#endif
}
