#pragma once
#include <Arduino.h>

struct MasterIpv4Settings {
  bool use_static=false;
  IPAddress ip{0,0,0,0}, gateway{0,0,0,0}, subnet{255,255,255,0};
  IPAddress dns1{0,0,0,0}, dns2{0,0,0,0};
};

inline uint32_t master_ipv4_bits(const IPAddress& ip) {
  return uint32_t(ip[0])<<24 | uint32_t(ip[1])<<16 | uint32_t(ip[2])<<8 | ip[3];
}
inline bool master_ipv4_unicast(const IPAddress& ip) {
  return ip[0]>0 && ip[0]<224 && ip[0]!=127;
}
inline bool master_ipv4_valid(const MasterIpv4Settings& c) {
  if (!c.use_static) return true;
  const uint32_t mask=master_ipv4_bits(c.subnet), host=~mask, address=master_ipv4_bits(c.ip);
  if (!master_ipv4_unicast(c.ip) || !mask || !host || (host & (host+1U)) ||
      !(address & host) || (address & host)==host) return false;
  const uint32_t gateway=master_ipv4_bits(c.gateway);
  if (gateway && (!master_ipv4_unicast(c.gateway) || gateway==address ||
      (gateway & mask)!=(address & mask) || !(gateway & host) || (gateway & host)==host)) return false;
  return (!master_ipv4_bits(c.dns1) || master_ipv4_unicast(c.dns1)) &&
         (!master_ipv4_bits(c.dns2) || master_ipv4_unicast(c.dns2));
}
inline bool master_ipv4_conflict(const MasterIpv4Settings& wifi,const MasterIpv4Settings& ethernet) {
  return wifi.use_static && ethernet.use_static && wifi.ip==ethernet.ip;
}
inline MasterIpv4Settings& master_ethernet_settings() {
  static MasterIpv4Settings settings;
  return settings;
}
