#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
static uint32_t packet_ms=10002;
static uint32_t millis() {return packet_ms;}
struct esp_netif_t {} primary_netif,other_netif;
using esp_event_base_t=const char*;
using esp_event_handler_instance_t=void*;
static constexpr esp_event_base_t IP_EVENT="IP";
enum {IP_EVENT_TX_RX=7,ESP_NETIF_TX=0,ESP_NETIF_RX=1,ESP_OK=0};
struct ip_event_tx_rx_t {esp_netif_t* esp_netif;size_t len;int dir;};
static void (*callback)(void*,esp_event_base_t,int32_t,void*)=nullptr;
static void* callback_arg=nullptr;
static bool register_ok=true,enable_ok=true;
static unsigned registrations=0,enables=0,disables=0;
static int esp_event_handler_instance_register(esp_event_base_t,int32_t,decltype(callback) fn,void* arg,void** handle) {
  ++registrations;if(!register_ok)return -1;callback=fn;callback_arg=arg;*handle=arg;return ESP_OK;
}
static int esp_netif_tx_rx_event_enable(esp_netif_t* netif) {assert(netif==&primary_netif);++enables;return enable_ok ? ESP_OK : -1;}
static int esp_netif_tx_rx_event_disable(esp_netif_t* netif) {assert(netif==&primary_netif);++disables;return ESP_OK;}
struct {
  bool link=false;esp_netif_t* current=&primary_netif;
  bool linkUp() {return link;}
  uint16_t linkSpeed() {return 100;}
  esp_netif_t* netif() {return current;}
} ETH;
#include "generated_ethernet_activity.h"
static void packet(esp_netif_t* netif,size_t len=100,int dir=ESP_NETIF_RX) {
  ip_event_tx_rx_t event{netif,len,dir};if(callback)callback(callback_arg,IP_EVENT,IP_EVENT_TX_RX,&event);
}
int main() {
  MasterEthernetActivity monitor;
  auto s=monitor.sample(0);assert(!s.link && !s.available && s.sequence==0 && enables==0);
  ETH.link=true;s=monitor.sample(1);assert(enables==0);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  assert(s.link && s.speed_mbps==100);
#if CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
  register_ok=false;s=monitor.sample(2,true);assert(!s.available && registrations==1 && enables==0);
  register_ok=true;monitor.sample(5001,true);assert(registrations==1);
  enable_ok=false;s=monitor.sample(5002,true);assert(!s.available && registrations==2 && enables==1);
  enable_ok=true;s=monitor.sample(10002,true);assert(s.available && enables==2 && registrations==2);
  packet(&other_netif);packet(nullptr);packet(&primary_netif,0);packet(&primary_netif,100,17);
  assert(monitor.sample(10003).sequence==0);
  packet(&primary_netif);packet(&primary_netif,100,ESP_NETIF_TX);
  callback(nullptr,IP_EVENT,IP_EVENT_TX_RX,nullptr);
  callback(callback_arg,IP_EVENT,999,nullptr);
  assert(monitor.sample(10004).sequence==2);
  assert(monitor.sample(10004).recent==1 && monitor.sample(10044).recent==4);
  monitor.tick(18001);assert(disables==0);
  monitor.tick(18002);assert(disables==1 && !monitor.sample(18003).available);
  packet(&primary_netif);assert(monitor.sample(18004).sequence==2);
  s=monitor.sample(18005,true);assert(s.available && registrations==2 && enables==3);
  ETH.link=false;monitor.tick(18006);assert(disables==2 && !monitor.sample(18007).link);
  ETH.link=true;ETH.current=nullptr;s=monitor.sample(18008,true);assert(!s.available && enables==3);
  ETH.current=&primary_netif;monitor.sample(23005,true);assert(enables==4);
  ETH.current=&other_netif;monitor.tick(23006);assert(disables==2); // Never dereference an old deleted netif.
  ETH.current=&primary_netif;
  MasterEthernetActivity late;
  s=late.sample(0xFFFFFF00U,true);assert(s.available); // First request after long uptime.
  late.tick(0xFFFFFF00U+7999U);assert(disables==2);
  late.tick(0xFFFFFF00U+8000U);assert(disables==3);
  s=late.sample(0x90000000U,true);assert(s.available); // A long pause is not a future retry deadline.
  std::cout<<"PASS: actual Ethernet traffic monitor, real matching TX/RX only, lazy lease, failures/backoff, one handler, unplug/netif change and millis rollover\n";
#else
  s=monitor.sample(10,true);assert(s.link && !s.available && enables==0 && registrations==0);
  std::cout<<"PASS: unsupported SDK reports link without fabricating activity\n";
#endif
#else
  assert(!s.link && !s.available && enables==0 && registrations==0);
  std::cout<<"PASS: S3 Ethernet activity no-op\n";
#endif
}
