#pragma once
#include <stdint.h>

// Called by the HTTP service task, also the owner of explicit WiFi scans.
class MasterLanWifiPolicy {
public:
  enum Action { None, SuspendWifi, ResumeWifi };
  Action tick(uint32_t now, bool ethernet, bool busy, bool wifi_enabled, bool scanning) {
    if (!ethernet) {
      const bool resume=ethernet_seen_;
      ethernet_seen_=scan_hold_=attempted_=suspended_=false;
      return resume ? ResumeWifi : None;
    }
    if (!ethernet_seen_) { ethernet_seen_=true; ethernet_since_=now; }
    if (scan_hold_ && uint32_t(now-scan_started_)>=30000UL) scan_hold_=false;
    if (busy || scanning || scan_hold_) { ethernet_since_=now; return None; }
    if (!wifi_enabled || uint32_t(now-ethernet_since_)<1500UL) return None;
    if (attempted_ && uint32_t(now-last_attempt_)<5000UL) return None;
    attempted_=true; last_attempt_=now;
    return SuspendWifi;
  }
  void requestScan(uint32_t now) { scan_hold_=true; scan_started_=now; attempted_=suspended_=false; }
  void releaseScan() { scan_hold_=false; attempted_=false; }
  void confirmSuspended() { suspended_=true; }
  bool suspended() const { return suspended_; }
private:
  bool ethernet_seen_=false, scan_hold_=false, attempted_=false, suspended_=false;
  uint32_t ethernet_since_=0, scan_started_=0, last_attempt_=0;
};
