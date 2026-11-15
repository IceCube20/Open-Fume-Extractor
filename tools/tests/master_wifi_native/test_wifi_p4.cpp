#define main unused_s3_main
#include "test_wifi.cpp"
#undef main
static void p4reset(bool saved=true) {
  reset(saved); master_lan_wifi_policy=MasterLanWifiPolicy(); previous_network=0;
  eth_ready=master_update_active=master_display_ble.running=scheduler.running=false;
  mqtt_network_reconnect_requested=false;
  if(saved)wifi_start_sta_connect(); else wifi_start_setup_ap();
}
int main() {
  p4reset(); WiFi.connected=true; eth_ready=true;
  wifi_service_tick(); now_ms=1499; wifi_service_tick(); assert(WiFi.stops==0);
  now_ms=1500; wifi_service_tick();
  assert(WiFi.stops==1 && WiFi.mode_==WIFI_OFF && !WiFi.auto_reconnect);
  assert(!captive_active && !dns.active && !wifi_sta_pending);
  assert(!strcmp(wifi_ssid,"saved-network") && !strcmp(wifi_password,"test-only"));
  now_ms=10000; wifi_service_tick(); assert(WiFi.stops==1 && WiFi.begins==1);
  assert(mqtt_network_reconnect_requested.exchange(false));
  eth_ready=false; now_ms=10001; wifi_service_tick();
  assert(WiFi.begins==2 && wifi_sta_pending && WiFi.mode_==WIFI_STA && WiFi.auto_reconnect);
  assert(mqtt_network_reconnect_requested.exchange(false));
  WiFi.connected=true; ++now_ms; wifi_service_tick(); assert(!wifi_sta_pending && wifi_sta_was_connected);

  p4reset(false); eth_ready=true; wifi_service_tick(); assert(!captive_active && !dns.active);
  now_ms=1500; wifi_service_tick(); assert(WiFi.mode_==WIFI_OFF);
  eth_ready=false; now_ms=2000; wifi_service_tick(); assert(captive_active && dns.active && WiFi.ap);

  for (unsigned mode=0;mode<3;++mode) {
    p4reset(); eth_ready=true; wifi_service_tick();
    if(mode==0)master_display_ble.running=true;
    if(mode==1)master_update_active=true;
    if(mode==2)scheduler.running=true;
    now_ms=5000; wifi_service_tick(); assert(WiFi.stops==0);
    master_display_ble.running=master_update_active=scheduler.running=false;
    now_ms=6499; wifi_service_tick(); assert(WiFi.stops==0);
    now_ms=6500; wifi_service_tick(); assert(WiFi.stops==1);
  }

  p4reset(); eth_ready=true; wifi_service_tick(); now_ms=1500; wifi_service_tick();
  now_ms=3000; master_lan_wifi_policy.requestScan(now_ms); WiFi.mode(WIFI_STA); WiFi.scan=WIFI_SCAN_RUNNING;
  now_ms=9000; wifi_service_tick(); assert(WiFi.stops==1);
  WiFi.scan=2; master_lan_wifi_policy.releaseScan();
  now_ms=10499; wifi_service_tick(); assert(WiFi.stops==1);
  now_ms=10500; wifi_service_tick(); assert(WiFi.stops==2);

  p4reset(); eth_ready=true; wifi_service_tick(); WiFi.stop_ok=false;
  now_ms=1500; wifi_service_tick(); assert(WiFi.stops==1 && WiFi.mode_==WIFI_STA);
  now_ms=6499; wifi_service_tick(); assert(WiFi.stops==1);
  WiFi.stop_ok=true; now_ms=6500; wifi_service_tick(); assert(WiFi.stops==2 && WiFi.mode_==WIFI_OFF);

  MasterLanWifiPolicy policy;
  const uint32_t start=0xfffffe00U;
  assert(policy.tick(start,true,false,true,false)==MasterLanWifiPolicy::None);
  assert(policy.tick(start+1499U,true,false,true,false)==MasterLanWifiPolicy::None);
  assert(policy.tick(start+1500U,true,false,true,false)==MasterLanWifiPolicy::SuspendWifi);
  policy.requestScan(start+2000U);
  assert(policy.tick(start+31999U,true,false,true,false)==MasterLanWifiPolicy::None);
  assert(policy.tick(start+33499U,true,false,true,false)==MasterLanWifiPolicy::SuspendWifi);
  assert(policy.tick(start+34000U,false,true,false,true)==MasterLanWifiPolicy::ResumeWifi);
  puts("PASS: actual P4 LAN/WiFi lifecycle: stable LAN, WiFi OFF, credentials retained, WLAN/AP fallback, BLE/OTA guards, scan, failure backoff, reconnect flag, rollover.");
}
