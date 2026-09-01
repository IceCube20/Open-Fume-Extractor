#pragma once
#include "bus/OfeDisplayTunnel.h"
#include "ModuleRegistry.h"
#include <atomic>

class MasterDisplayWifi {
public:
  using ConfigProvider = void (*)(ofe_wifi::Config&);
  bool begin(jbc_rs485::Link& link, ModuleRegistry& registry, ConfigProvider provider);
  bool active(uint8_t addr) const;
  bool keyText(uint64_t uid, char out[33]) const;
  bool rootText(char out[33]) const;
  bool restoreRoot(const char* text);
  bool provisioningDue(uint8_t addr);
  bool configuration(uint8_t addr, ofe_wifi::Config& config) const;
  bool probeDue(uint8_t addr);
  void probeResult(uint8_t addr, bool ok);
  void forcePhysical(bool enabled) { physical_ = enabled; }
  void beginFirmware(uint8_t addr);
  void endFirmware() { firmware_addr_.store(0); }
  bool firmwareWireless(uint8_t addr) const { return firmware_addr_==addr && firmware_wifi_; }
private:
  struct Peer {
    uint64_t uid=0, client=0, server=0;
    uint64_t pending_client=0, pending_server=0;
    sockaddr_in endpoint={}, pending_endpoint={};
    uint32_t rx=0, tx=0, seen=0, pending_ms=0, probe_ms=0;
    uint8_t addr=0, mode=0, wired_probes=0;
    bool connected=false;
  };
  Peer peers_[16];
  std::atomic<uint32_t> published_seen_[16]{};
  uint8_t root_[16]={};
  uint32_t provision_ms_[16]={};
  bool ready_=false, physical_=false;
  std::atomic<uint8_t> firmware_addr_{0};
  uint64_t firmware_uid_=0;
  bool firmware_wifi_=false;
  int fd_=-1;
  uint32_t socket_retry_ms_=0;
  ModuleRegistry* registry_=nullptr;
  ConfigProvider provider_=nullptr;
  bool derive(uint64_t uid, uint8_t key[16]) const;
  Peer* peer(uint8_t addr);
  void publish(const Peer& peer);
  bool receive(jbc_rs485::Frame& frame);
  bool route(const jbc_rs485::Frame& frame);
  bool send(Peer& peer, uint8_t kind, const jbc_rs485::Frame* frame=nullptr);
  static bool routeHook(void* self, const jbc_rs485::Frame& frame);
  static bool pollHook(void* self, jbc_rs485::Frame& frame);
};
extern MasterDisplayWifi master_display_wifi;
