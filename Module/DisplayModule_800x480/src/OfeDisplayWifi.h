#pragma once
#include "OfeDisplayTunnel.h"
#include "OfeDisplayMemory.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class OfeDisplayWifi {
public:
  using Transition = void (*)();
  struct View {
    ofe_wifi::Config config;
    char networks[544]={};
    char ip[16]={};
    int rssi=0;
    uint32_t revision=0;
    uint32_t scan_revision=0, free_internal=0, largest_internal=0;
    uint8_t result=0; // 0 idle, 1 saving, 2 saved, 3 write failed, 4 task unavailable
    bool wifi=false, active=false, scanning=false;
  };
  // Setup-only: the LCD has its DMA buffers; WiFi precedes LVGL's optional tile.
  bool prepareRadio() {
    boot_radio_ready_=startRadio("startup");
    return boot_radio_ready_;
  }
  size_t drawBufferReserve() const {
    return boot_radio_ready_ ? ofe_display_memory::RUNTIME_RESERVE : ofe_display_memory::STARTUP_RESERVE;
  }
  bool begin(jbc_rs485::Link& link, uint64_t uid, uint8_t& address, Transition transition) {
    link_=&link; uid_=uid; address_=&address; transition_=transition;
    Preferences p;
    if (p.begin("ofe-display-net",false)) {
      ofe_wifi::Config saved;
      if (p.getBytesLength("config")==sizeof(saved) && p.getBytes("config",&saved,sizeof(saved))==sizeof(saved) &&
          ofe_wifi::valid(saved)) shared_.config=saved;
      p.end();
    }
    active_config_=shared_.config;
    last_transport_wireless_.store(active_config_.mode==ofe_wifi::WIRELESS, std::memory_order_relaxed);
    link.setTransportHooks(this,sendHook,pollHook,serialHook);
    bool ok=xTaskCreatePinnedToCore(workerHook,"display-wifi",6144,this,1,&worker_,0)==pdPASS;
    if (!ok) shared_.result=4;
    return ok;
  }
  View view() {
    portENTER_CRITICAL(&mux_); View v=shared_; portEXIT_CRITICAL(&mux_); return v;
  }
  bool beginUpdate() {
    portENTER_CRITICAL(&mux_);
    const bool ok=!save_pending_ && !shared_.scanning && !update_transport_.load();
    if (ok) update_transport_.store(network_origin_ ? 2 : 1);
    portEXIT_CRITICAL(&mux_);
    return ok;
  }
  void finishUpdate() { update_transport_.store(0); }
  bool updateWireless() const { return update_transport_.load()==2; }
  bool save(const ofe_wifi::Config& c) {
    if (!worker_ || !ofe_wifi::valid(c) ||
        (!c.from_master && c.mode!=ofe_wifi::WIRED && !ofe_wifi::configured(c))) return false;
    portENTER_CRITICAL(&mux_);
    bool ok=!save_pending_ && !update_transport_.load();
    if (ok) { pending_=c; save_pending_=true; shared_.result=1; }
    portEXIT_CRITICAL(&mux_); return ok;
  }
  void scan() {
    portENTER_CRITICAL(&mux_);
    if (!update_transport_.load()) { scan_requested_=true; shared_.scanning=true; }
    portEXIT_CRITICAL(&mux_);
  }
  bool wireless() {
    return last_transport_wireless_.load(std::memory_order_relaxed);
  }
  uint64_t uid() const { return uid_; }
  bool handleConfig(const jbc_rs485::Frame& request) {
    using namespace ofe_wifi;
    if (request.cmd!=CMD_DISPLAY_CONFIG) return false;
    // The bus parser also exposes traffic addressed to other displays.
    if (request.dst!=*address_) return true;
    Frame response; response.dst=request.src; response.src=*address_; response.seq=request.seq;
    response.cmd=CMD_DISPLAY_CONFIG|0x80; response.len=1; response.payload[0]=STATUS_BAD_VALUE;
    if (!network_origin_ && request.src==ADDR_MASTER && request.len==1 && request.payload[0]==0) {
      View v=view(); response.len=7; response.payload[0]=STATUS_OK; response.payload[1]=v.config.from_master;
      response.payload[2]=v.config.mode; put_u32_le(response.payload+3,configHash(v.config));
    } else if (!network_origin_ && request.src==ADDR_MASTER && request.len==1+sizeof(Config) && request.payload[0]==1) {
      Config c; memcpy(&c,request.payload+1,sizeof(c));
      if (c.from_master && configured(c) && save(c)) response.payload[0]=STATUS_OK;
    }
    link_->send(response); return true;
  }
  static uint32_t configHash(const ofe_wifi::Config& c) {
    const uint8_t key[16]={}; uint8_t digest[32]={};
    ofe_wifi::mac(key,reinterpret_cast<const uint8_t*>(&c),sizeof(c),digest);
    return jbc_rs485::get_u32_le(digest);
  }
private:
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  View shared_;
  ofe_wifi::Config pending_,active_config_;
  bool save_pending_=false,scan_requested_=false;
  bool boot_radio_ready_=false;
  std::atomic<uint8_t> update_transport_{0}; // 0 idle, 1 RS485, 2 authenticated WiFi
  std::atomic<bool> last_transport_wireless_{false};
  uint32_t resolved_ip_=0;
  uint32_t config_revision_=0;
  TaskHandle_t worker_=nullptr;
  jbc_rs485::Link* link_=nullptr;
  uint8_t* address_=nullptr;
  uint64_t uid_=0,client_=0,server_=0;
  uint32_t tx_=0,rx_=0,seen_=0,hello_ms_=0,last_wired_ms_=0;
  uint32_t heartbeat_ms_=0;
  uint32_t socket_retry_ms_=0;
  int fd_=-1;
  sockaddr_in endpoint_={};
  bool connected_=false,network_origin_=false;
  Transition transition_=nullptr;
  void setActive(bool active) {
    portENTER_CRITICAL(&mux_);
    bool changed=shared_.active!=active; shared_.active=active;
    portEXIT_CRITICAL(&mux_);
    if (changed && transition_) transition_();
  }
  void resetSession(bool leave) {
    if (leave && connected_) send(ofe_wifi::LEAVE);
    connected_=false; client_=0; server_=0; tx_=rx_=0; hello_ms_=0;
    setActive(false);
  }
  bool send(uint8_t kind,const jbc_rs485::Frame* frame=nullptr) {
    using namespace ofe_wifi;
    if (tx_==UINT32_MAX) { resetSession(false); return false; }
    Packet p; p.kind=kind; p.uid=uid_; p.mode=active_config_.mode; p.client=client_; p.server=server_;
    p.counter=(kind==DATA || kind==LEAVE || kind==KEEPALIVE) ? ++tx_ : 0;
    if (frame) setFrame(p,*frame);
    return sendPacket(fd_,endpoint_,p,active_config_.key);
  }
  static bool sendHook(void* self,const jbc_rs485::Frame& f) {
    auto* d=static_cast<OfeDisplayWifi*>(self);
    if (d->network_origin_ && d->connected_) { d->send(ofe_wifi::DATA,&f); return true; }
    if (!d->network_origin_ && (f.cmd==(jbc_rs485::CMD_PING|0x80) || f.cmd==(jbc_rs485::CMD_DISPLAY_CONFIG|0x80))) return false;
    if (d->connected_) return true;
    // Periodic joins must not leak from a wireless-only display onto RS485.
    return d->active_config_.mode==ofe_wifi::WIRELESS &&
      f.cmd!=(jbc_rs485::CMD_DISPLAY_CONFIG|0x80);
  }
  static bool pollHook(void* self,jbc_rs485::Frame& f) {
    return static_cast<OfeDisplayWifi*>(self)->poll(f);
  }
  static bool serialHook(void* self,const jbc_rs485::Frame& f) {
    using namespace ofe_wifi;
    auto* d=static_cast<OfeDisplayWifi*>(self);
    d->network_origin_=false;
    if (d->updateWireless()) return false;
    if (f.src!=ADDR_MASTER) return d->active_config_.mode!=WIRELESS && !d->connected_;
    // Provisioning remains available over a cable in every mode.
    if (f.dst==*d->address_ && f.cmd==CMD_DISPLAY_CONFIG) return true;
    if (d->active_config_.mode==WIRELESS) return false;
    if (f.dst==*d->address_) {
      // PING probes test cable stability without taking over from WiFi.
      if (d->connected_ && f.cmd==CMD_PING) return true;
      d->last_transport_wireless_.store(false, std::memory_order_relaxed);
      d->last_wired_ms_=millis();
      if (d->connected_) d->resetSession(true);
    }
    return !d->connected_;
  }
  bool poll(jbc_rs485::Frame& frame) {
    using namespace ofe_wifi;
    if (update_transport_.load()==1) return false;
    // This runs on every OFE poll, including idle polls. Do not copy the scan
    // list/UI view with interrupts disabled; only config changes need a copy.
    Config next_config;
    uint32_t ip, revision;
    bool wifi;
    portENTER_CRITICAL(&mux_);
    ip=resolved_ip_; wifi=shared_.wifi; revision=shared_.revision;
    if (config_revision_!=revision) next_config=shared_.config;
    portEXIT_CRITICAL(&mux_);
    if (config_revision_!=revision) {
      resetSession(true); active_config_=next_config; config_revision_=revision;
      if (active_config_.mode==WIRELESS) last_transport_wireless_.store(true, std::memory_order_relaxed);
      else if (active_config_.mode==WIRED) last_transport_wireless_.store(false, std::memory_order_relaxed);
    }
    if (active_config_.mode==WIRED || !configured(active_config_) || !wifi || !ip ||
        (active_config_.mode==AUTOMATIC && last_wired_ms_ && (uint32_t)(millis()-last_wired_ms_)<3500)) {
      if (connected_) resetSession(true);
      return false;
    }
    if (fd_<0) {
      if (socket_retry_ms_ && (uint32_t)(millis()-socket_retry_ms_)<3000) return false;
      socket_retry_ms_=millis(); fd_=socketOpen(0); if (fd_<0) return false;
    }
    if (endpoint_.sin_addr.s_addr!=ip) {
      resetSession(true); endpoint_={}; endpoint_.sin_family=AF_INET;
      endpoint_.sin_port=htons(PORT); endpoint_.sin_addr.s_addr=ip;
    }
    if (connected_ && (uint32_t)(millis()-seen_)>5000) resetSession(false);
    if (connected_ && (uint32_t)(millis()-heartbeat_ms_)>1000) {
      send(KEEPALIVE); heartbeat_ms_=millis();
    }
    if (!connected_ && (!hello_ms_ || (uint32_t)(millis()-hello_ms_)>1000+(uid_&255))) {
      if (!client_) client_=nonce();
      if (server_ && (uint32_t)(millis()-seen_)>5000) { server_=0; client_=nonce(); }
      send(server_ ? PROOF : HELLO); hello_ms_=millis();
    }
    uint8_t raw[PACKET_MAX+1]; sockaddr_in from={}; socklen_t size=sizeof(from);
    int n=recvfrom(fd_,raw,sizeof(raw),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&from),&size);
    if (n<=0 || !samePeer(from,endpoint_)) return false;
    Packet p;
    if (!decode(raw,n,active_config_.key,p) || p.uid!=uid_ || p.mode!=active_config_.mode || p.client!=client_) return false;
    if (p.kind==CHALLENGE && !connected_ && p.server && !p.counter && !p.length) {
      server_=p.server; seen_=millis(); send(PROOF); return false;
    }
    if (p.kind==READY && !connected_ && server_ && p.server==server_ && !p.counter &&
        p.length==1 && p.body[0]>=0x40 && p.body[0]<=0x4f) {
      *address_=p.body[0]; connected_=true; seen_=millis(); tx_=rx_=0;
      last_transport_wireless_.store(true, std::memory_order_relaxed);
      setActive(true); return false;
    }
    if (!connected_ || !inSession(p,uid_,client_,server_,active_config_.mode,rx_)) return false;
    if (p.kind==KEEPALIVE && !p.length) { rx_=p.counter; seen_=millis(); return false; }
    if (p.kind==LEAVE) {
      last_wired_ms_=millis(); resetSession(false); return false;
    }
    if (p.kind!=DATA || !getFrame(p,frame) || frame.src!=ADDR_MASTER ||
        (frame.dst!=*address_ && frame.dst!=ADDR_BROADCAST)) return false;
    rx_=p.counter; seen_=millis(); network_origin_=true;
    last_transport_wireless_.store(true, std::memory_order_relaxed);
    // OTA uses the same handlers after authentication/replay checks above.
    return true;
  }
  static void workerHook(void* arg) { static_cast<OfeDisplayWifi*>(arg)->worker(); }
  void sampleMemory() {
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    uint32_t free=heap_caps_get_free_size(caps), largest=heap_caps_get_largest_free_block(caps);
    portENTER_CRITICAL(&mux_);
    shared_.free_internal=free; shared_.largest_internal=largest;
    portEXIT_CRITICAL(&mux_);
  }
  bool startRadio(const char* phase) {
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t free_before=heap_caps_get_free_size(caps), block_before=heap_caps_get_largest_free_block(caps);
    WiFi.persistent(false);
    WiFi.useStaticBuffers(false);
    bool ok=WiFi.mode(WIFI_STA);
    if (ok) { WiFi.setAutoReconnect(true); WiFi.setSleep(false); }
    sampleMemory();
    Serial.printf("[Display WiFi] %s: %s; internal %lu -> %lu B, largest %lu -> %lu B\n",
      phase,ok ? "ready" : "FAILED",(unsigned long)free_before,(unsigned long)heap_caps_get_free_size(caps),
      (unsigned long)block_before,(unsigned long)heap_caps_get_largest_free_block(caps));
    portENTER_CRITICAL(&mux_);
    if (!ok) shared_.result=5;
    else if (shared_.result==5) shared_.result=0;
    portEXIT_CRITICAL(&mux_);
    return ok;
  }
  void finishScan(int count) {
    char names[544]={}; size_t used=0;
    for (int i=0;i<count && i<16;++i) {
      String ssid=WiFi.SSID(i); ssid.replace("\n"," "); ssid.replace("\r"," ");
      if (!ssid.length()) continue;
      int n=snprintf(names+used,sizeof(names)-used,"%s%s",used ? "\n" : "",ssid.c_str());
      if (n<0 || (size_t)n>=sizeof(names)-used) break;
      used+=n;
    }
    portENTER_CRITICAL(&mux_);
    memcpy(shared_.networks,names,sizeof(names)); shared_.scanning=false; ++shared_.scan_revision;
    if (count<0) shared_.result=6;
    else if (shared_.result==5 || shared_.result==6) shared_.result=0;
    portEXIT_CRITICAL(&mux_);
    WiFi.scanDelete();
  }
  void worker() {
    using namespace ofe_wifi;
    uint32_t connect_ms=0,resolve_ms=0,revision=UINT32_MAX,memory_ms=0;
    bool radio=WiFi.getMode()==WIFI_STA,scanning=false,mdns=false;
    Config c;
    for (;;) {
      // Driver keepalive/reconnect tasks continue; defer scans, config writes and DNS.
      if (update_transport_.load()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
      Config next; bool pending=false,scan=false;
      portENTER_CRITICAL(&mux_);
      if (save_pending_) { next=pending_; pending=true; }
      scan=scan_requested_; scan_requested_=false;
      portEXIT_CRITICAL(&mux_);
      if (pending) {
        Preferences prefs; bool ok=prefs.begin("ofe-display-net",false);
        if (ok) {
          ok=prefs.putBytes("config",&next,sizeof(next))==sizeof(next);
          Config verify;
          ok=ok && prefs.getBytes("config",&verify,sizeof(verify))==sizeof(verify) && memcmp(&verify,&next,sizeof(next))==0;
          prefs.end();
        }
        portENTER_CRITICAL(&mux_);
        if (ok) { shared_.config=next; ++shared_.revision; resolved_ip_=0; }
        shared_.result=ok ? 2 : 3; save_pending_=false;
        portEXIT_CRITICAL(&mux_);
      }
      portENTER_CRITICAL(&mux_);
      const bool config_changed=revision!=shared_.revision;
      if (config_changed) { c=shared_.config; revision=shared_.revision; }
      portEXIT_CRITICAL(&mux_);
      if (config_changed) {
        connect_ms=0; resolve_ms=0;
        if (radio) WiFi.disconnect(false,false);
      }
      bool wanted=c.mode!=WIRED && configured(c);
      if ((c.mode!=WIRED || scan || scanning) && !radio) {
        if (!startRadio("retry")) {
          portENTER_CRITICAL(&mux_); scan_requested_=scan_requested_ || scan; portEXIT_CRITICAL(&mux_);
          vTaskDelay(pdMS_TO_TICKS(3000)); continue;
        }
        radio=true;
      }
      if (radio && WiFi.status()==WL_CONNECTED) {
        if (!mdns) { char name[32]; snprintf(name,sizeof(name),"ofe-display-%08lx",(unsigned long)uid_); mdns=MDNS.begin(name); }
      }
      if (c.mode==WIRED && !scan && !scanning && radio) {
        if (mdns) { MDNS.end(); mdns=false; }
        if (WiFi.mode(WIFI_OFF)) radio=false;
      }
      if (wanted && !scan && !scanning && WiFi.status()!=WL_CONNECTED && (!connect_ms || (uint32_t)(millis()-connect_ms)>10000)) {
        WiFi.begin(c.ssid,c.password); connect_ms=millis();
      }
      if (scan && !scanning && radio) {
        // A pending association can reject scans; keep an established link intact.
        if (WiFi.status()!=WL_CONNECTED) {
          WiFi.setAutoReconnect(false);
          WiFi.disconnect(false,false);
          connect_ms=0;
        }
        WiFi.scanDelete();
        int result=WiFi.scanNetworks(true);
        scanning=result==WIFI_SCAN_RUNNING;
        if (!scanning) { finishScan(result); WiFi.setAutoReconnect(true); }
      }
      if (scanning) {
        int count=WiFi.scanComplete();
        if (count!=WIFI_SCAN_RUNNING) {
          finishScan(count); scanning=false; WiFi.setAutoReconnect(true);
        }
      }
      bool online=radio && WiFi.status()==WL_CONNECTED;
      if (online && (!resolve_ms || (uint32_t)(millis()-resolve_ms)>30000)) {
        IPAddress ip;
        // DNS can wait, but only this worker does so. LVGL and the OFE task do not.
        bool ok=ip.fromString(c.host) || WiFi.hostByName(c.host,ip)==1;
        if (ok) { portENTER_CRITICAL(&mux_); resolved_ip_=(uint32_t)ip; portEXIT_CRITICAL(&mux_); }
        resolve_ms=millis();
      }
      char ip[16]={}; int rssi=online ? WiFi.RSSI() : 0;
      if (online) WiFi.localIP().toString().toCharArray(ip,sizeof(ip));
      portENTER_CRITICAL(&mux_);
      shared_.wifi=online; shared_.rssi=rssi; memcpy(shared_.ip,ip,sizeof(ip));
      if (!online) { resolved_ip_=0; resolve_ms=0; }
      portEXIT_CRITICAL(&mux_);
      if ((uint32_t)(millis()-memory_ms)>=1000) { sampleMemory(); memory_ms=millis(); }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
};
