#pragma once
#include <stdint.h>

// Owned by the HTTP/network service task; SNTP itself remains asynchronous.
class MasterTimeSyncPolicy {
public:
  bool poll(uint32_t now, bool ready, uint32_t ip, bool ethernet, bool time_valid, bool blocked) {
    if (!ready) { online_=false; return false; }
    if (!online_ || ip!=ip_ || ethernet!=ethernet_) {
      online_=true; ip_=ip; ethernet_=ethernet;
      changed_ms_=now; attempted_=false;
    }
    // Let DHCP and the Ethernet/WiFi handover settle before starting DNS/NTP.
    if (blocked || uint32_t(now-changed_ms_)<2000U) return false;
    if (attempted_ && (time_valid || uint32_t(now-attempt_ms_)<60000U)) return false;
    attempted_=true; attempt_ms_=now;
    return true;
  }
private:
  bool online_=false, ethernet_=false, attempted_=false;
  uint32_t ip_=0, changed_ms_=0, attempt_ms_=0;
};
