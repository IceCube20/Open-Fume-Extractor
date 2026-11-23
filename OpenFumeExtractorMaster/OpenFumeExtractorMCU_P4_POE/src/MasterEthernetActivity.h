#pragma once
#include <Arduino.h>
#include <atomic>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <ETH.h>
#include <esp_event.h>
#include <esp_netif.h>
#endif

struct MasterEthernetActivitySnapshot {
  bool link=false, available=false;
  uint16_t speed_mbps=0;
  uint32_t sequence=0;
  uint32_t sample_ms=0;
  uint64_t recent=0;
};

class MasterEthernetActivity {
public:
  MasterEthernetActivitySnapshot sample(uint32_t now,bool observe=false) {
    MasterEthernetActivitySnapshot result;
    result.sample_ms=now;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    tick(now);
    result.link=ETH.linkUp();
    if (result.link) result.speed_mbps=ETH.linkSpeed();
#if CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
    if (observe && result.link && ETH.netif()) {
      last_sample_ms_=now;
      if (!enabled_ && (!attempted_ || uint32_t(now-last_attempt_ms_)>=5000U)) {
        attempted_=true;
        last_attempt_ms_=now;
        if (!registered_) registered_=esp_event_handler_instance_register(IP_EVENT,IP_EVENT_TX_RX,onTraffic,this,&handler_)==ESP_OK;
        if (registered_) {
          netif_.store(ETH.netif(),std::memory_order_relaxed);
          enabled_=esp_netif_tx_rx_event_enable(ETH.netif())==ESP_OK;
          if (!enabled_) netif_.store(nullptr,std::memory_order_relaxed);
        }
      }
    }
    result.available=enabled_;
    result.sequence=sequence_.load(std::memory_order_relaxed);
    if (enabled_) for (const auto& bin:bins_) {
      const uint32_t stamp=bin.load(std::memory_order_relaxed),age=uint32_t(now-stamp);
      if (stamp && age<1280U) result.recent|=uint64_t(1)<<(age/20U);
    }
#endif
#else
    (void)now;
    (void)observe;
#endif
    return result;
  }

  void tick(uint32_t now) {
#if defined(CONFIG_IDF_TARGET_ESP32P4) && CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
    if (enabled_ && (!ETH.linkUp() || ETH.netif()!=netif_.load(std::memory_order_relaxed) || uint32_t(now-last_sample_ms_)>=8000U)) {
      auto* previous=netif_.exchange(nullptr,std::memory_order_relaxed);
      enabled_=false;
      if (previous && previous==ETH.netif()) esp_netif_tx_rx_event_disable(previous);
    }
#else
    (void)now;
#endif
  }

private:
#if defined(CONFIG_IDF_TARGET_ESP32P4) && CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
  static void onTraffic(void* arg,esp_event_base_t base,int32_t id,void* data) {
    if (!arg || !data || base!=IP_EVENT || id!=IP_EVENT_TX_RX) return;
    auto& self=*static_cast<MasterEthernetActivity*>(arg);
    const auto& packet=*static_cast<ip_event_tx_rx_t*>(data);
    // Do not copy frames or touch the UI from the SDK event task.
    if (packet.esp_netif && packet.esp_netif==self.netif_.load(std::memory_order_relaxed) && packet.len &&
        (packet.dir==ESP_NETIF_TX || packet.dir==ESP_NETIF_RX)) {
      const uint32_t stamp=millis();
      self.bins_[(stamp/20U)%64U].store(stamp,std::memory_order_relaxed);
      self.sequence_.fetch_add(1,std::memory_order_relaxed);
    }
  }
  std::atomic<esp_netif_t*> netif_{nullptr};
  std::atomic<uint32_t> sequence_{0};
  std::atomic<uint32_t> bins_[64]{};
  esp_event_handler_instance_t handler_=nullptr;
  bool registered_=false,enabled_=false,attempted_=false;
  uint32_t last_sample_ms_=0,last_attempt_ms_=0;
#endif
};

inline MasterEthernetActivity& master_ethernet_activity() {
  static MasterEthernetActivity monitor;
  return monitor;
}
