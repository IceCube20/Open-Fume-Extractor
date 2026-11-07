#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

enum { WIFI_AP=1,WIFI_STA=2,WIFI_AP_STA=3,WL_DISCONNECTED=0,WL_CONNECTED=1 };
static uint32_t now_ms=0;
static uint32_t millis() { return now_ms; }
struct FakeWifi {
  int mode_=0;
  bool connected=false,ap=false,auto_reconnect=false;
  unsigned begins=0,configs=0,ap_stops=0;
  bool mode(int value) { mode_=value; if(value==WIFI_AP) connected=false; return true; }
  bool setSleep(bool) { return true; }
  void setAutoReconnect(bool value) { auto_reconnect=value; }
  bool config(int ip,int gateway,int subnet,int dns1,int dns2) {
    assert(ip==1 && gateway==2 && subnet==3 && dns1==4 && dns2==5); ++configs; return true;
  }
  void begin(const char* ssid,const char*) { assert(*ssid); ++begins; }
  bool softAP(const char*,const char*) { ap=true; return true; }
  bool softAPdisconnect(bool) { ap=false; mode_=WIFI_STA; ++ap_stops; return true; }
  int softAPIP() { return 123; }
  int localIP() { return 456; }
  int status() { return connected ? WL_CONNECTED : WL_DISCONNECTED; }
} WiFi;
struct FakeDns {
  bool active=false;
  void start(int,const char*,int) { active=true; }
  void stop() { active=false; }
} dns;
struct FakeSerial {
  template<typename T> void print(const T&) {}
  template<typename T> void println(const T&) {}
} Serial;
static char wifi_ssid[33]="saved-network",wifi_password[65]="test-only";
static char master_ap_password[65]="test-only",master_ap_ssid[33]="setup",master_hostname[33]="ofe";
static bool captive_active=false,wifi_sta_pending=false,wifi_sta_was_connected=false,wifi_time_configured=false;
static bool wifi_static_enabled=false;
static int wifi_static_ip=1,wifi_static_gateway=2,wifi_static_subnet=3,wifi_static_dns1=4,wifi_static_dns2=5;
static uint32_t wifi_sta_start_ms=0,wifi_next_retry_ms=0,mqtt_next_connect_ms=0;
static unsigned mdns_starts=0,time_configs=0;
static void apply_ap_hostname() {}
static void apply_sta_hostname() {}
static void generate_setup_password(char* out,size_t) { std::strcpy(out,"test-only"); }
static void start_mdns_service() { ++mdns_starts; }
static void configTzTime(const char*,const char*,const char*,const char*) { ++time_configs; }
#include "generated_wifi.inc.h"

static void reset(bool saved=true) {
  WiFi=FakeWifi(); dns=FakeDns(); now_ms=0;
  std::strcpy(wifi_ssid,saved ? "saved-network" : "");
  captive_active=wifi_sta_pending=wifi_sta_was_connected=wifi_time_configured=false;
  wifi_static_enabled=false; wifi_sta_start_ms=wifi_next_retry_ms=mqtt_next_connect_ms=0;
  mdns_starts=time_configs=0;
}
int main() {
  reset(false); wifi_start_setup_ap();
  assert(captive_active && WiFi.mode_==WIFI_AP && dns.active);
  now_ms=100000; wifi_service_tick(); assert(WiFi.begins==0); // Unconfigured setup remains available.

  reset(); wifi_start_sta_connect();
  assert(WiFi.mode_==WIFI_STA && wifi_sta_pending && WiFi.auto_reconnect);
  now_ms=12000; wifi_service_tick();
  assert(captive_active && !wifi_sta_pending && WiFi.mode_==WIFI_AP_STA && dns.active);
  now_ms=29999; wifi_service_tick(); assert(WiFi.begins==1);
  now_ms=30000; wifi_service_tick();
  assert(WiFi.begins==2 && wifi_sta_pending && captive_active && WiFi.ap && dns.active);
  now_ms=42000; wifi_service_tick(); assert(!wifi_sta_pending && captive_active);
  now_ms=60000; wifi_service_tick(); assert(WiFi.begins==3 && captive_active);
  WiFi.connected=true; now_ms=60100; wifi_service_tick();
  assert(!captive_active && !wifi_sta_pending && wifi_sta_was_connected && !dns.active && !WiFi.ap);
  assert(WiFi.mode_==WIFI_STA && WiFi.ap_stops==1 && time_configs==1);
  const auto starts=mdns_starts;
  wifi_service_tick(); assert(mdns_starts==starts); // No repeated service restarts while online.

  WiFi.connected=false; now_ms=100000; wifi_service_tick();
  assert(!wifi_sta_was_connected && wifi_sta_pending && WiFi.begins==4);
  now_ms=112000; wifi_service_tick(); assert(captive_active && WiFi.ap);
  // AP returns between explicit retries; driver auto-reconnect also exits fallback.
  WiFi.connected=true; now_ms=112001; wifi_service_tick();
  assert(!captive_active && !wifi_sta_pending && wifi_sta_was_connected && !WiFi.ap);
  assert(time_configs==1 && mqtt_next_connect_ms==113001 && mdns_starts==starts+2);

  reset(); wifi_static_enabled=true; wifi_start_sta_connect();
  assert(WiFi.configs==1); now_ms=12000; wifi_service_tick();
  now_ms=30000; wifi_service_tick(); assert(WiFi.configs==2 && captive_active);

  reset(); now_ms=0xffffe000U; wifi_start_sta_connect();
  const auto started=now_ms;
  now_ms=started+12000U; wifi_service_tick(); assert(captive_active);
  now_ms=started+29999U; wifi_service_tick(); assert(WiFi.begins==1);
  ++now_ms; wifi_service_tick(); assert(WiFi.begins==2 && captive_active);
  puts("PASS: production Master WiFi lifecycle: first setup, AP outage/recovery, repeated retries with portal retained, auto-reconnect, static IP, services and millis rollover.");
}
