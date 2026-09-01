#include "MasterDisplayWifi.h"
#include <Preferences.h>
#include <WiFi.h>
using namespace ofe_wifi;
MasterDisplayWifi master_display_wifi;

bool MasterDisplayWifi::begin(Link& link, ModuleRegistry& registry, ConfigProvider provider) {
  Preferences prefs;
  if (!prefs.begin("display-link", false)) return false;
  bool ok=prefs.getBytesLength("root") == sizeof(root_) && prefs.getBytes("root",root_,sizeof(root_)) == sizeof(root_);
  if (!ok && !prefs.isKey("root")) {
    esp_fill_random(root_,sizeof(root_));
    ok=prefs.putBytes("root",root_,sizeof(root_)) == sizeof(root_);
  }
  prefs.end();
  ready_=ok && nonzero(root_,sizeof(root_));
  if (!ready_) return false;
  registry_=&registry; provider_=provider;
  link.setTransportHooks(this,routeHook,pollHook,nullptr);
  return true;
}
bool MasterDisplayWifi::derive(uint64_t uid, uint8_t key[16]) const {
  uint8_t input[16]={ 'O','F','E','-','D','S','P','1' }, digest[32];
  put_u64_le(input+8,uid);
  if (!ready_ || (uid >> 60) != 4 || !mac(root_,input,sizeof(input),digest)) return false;
  memcpy(key,digest,16); return true;
}
bool MasterDisplayWifi::keyText(uint64_t uid, char out[33]) const {
  uint8_t key[16]; if (!derive(uid,key)) return false; hex(key,16,out); return true;
}
bool MasterDisplayWifi::rootText(char out[33]) const {
  if (!ready_) return false; hex(root_,sizeof(root_),out); return true;
}
bool MasterDisplayWifi::restoreRoot(const char* text) {
  uint8_t root[16]; if (!unhex(text,root,16) || !nonzero(root,16)) return false;
  Preferences p; if (!p.begin("display-link",false)) return false;
  bool ok=p.putBytes("root",root,16)==16; p.end();
  // Restore is applied by the existing restore/reboot flow, never mid-session.
  return ok;
}
MasterDisplayWifi::Peer* MasterDisplayWifi::peer(uint8_t addr) {
  for (auto& p:peers_) if (p.uid && p.addr==addr) return &p;
  return nullptr;
}
bool MasterDisplayWifi::active(uint8_t addr) const {
  if (addr<0x40 || addr>0x4f) return false;
  const uint32_t seen=published_seen_[addr-0x40].load(std::memory_order_relaxed);
  return seen && (uint32_t)(millis()-seen)<5000;
}
void MasterDisplayWifi::publish(const Peer& p) {
  if (p.addr>=0x40 && p.addr<=0x4f)
    published_seen_[p.addr-0x40].store(p.connected ? p.seen : 0,std::memory_order_relaxed);
}
bool MasterDisplayWifi::provisioningDue(uint8_t addr) {
  if (!ready_ || addr<0x40 || addr>0x4f || active(addr)) return false;
  uint32_t& last=provision_ms_[addr-0x40];
  if (last && (uint32_t)(millis()-last)<10000) return false;
  last=millis(); return true;
}
bool MasterDisplayWifi::configuration(uint8_t addr, Config& config) const {
  const auto* rec=registry_ ? registry_->find(addr) : nullptr;
  if (!rec || rec->type!=MODULE_DISPLAY || !provider_) return false;
  provider_(config);
  return derive(rec->uid,config.key) && configured(config);
}
bool MasterDisplayWifi::probeDue(uint8_t addr) {
  Peer* p=peer(addr);
  if (!p || !active(addr) || p->mode!=AUTOMATIC || (uint32_t)(millis()-p->probe_ms)<2000) return false;
  p->probe_ms=millis(); return true;
}
void MasterDisplayWifi::probeResult(uint8_t addr, bool ok) {
  Peer* p=peer(addr); if (!p) return;
  p->wired_probes=ok ? p->wired_probes+1 : 0;
  if (p->wired_probes>=2) {
    // LEAVE is only advisory. The first addressed serial status frame is the
    // authoritative switchover, so a lost datagram cannot strand the display.
    send(*p,LEAVE); p->connected=false; publish(*p);
  }
}
bool MasterDisplayWifi::send(Peer& p, uint8_t kind, const Frame* frame) {
  if (p.tx==UINT32_MAX) { p.connected=false; publish(p); return false; }
  Packet packet; packet.kind=kind; packet.mode=p.mode; packet.uid=p.uid;
  packet.client=p.client; packet.server=p.server; packet.counter=++p.tx;
  if (frame) setFrame(packet,*frame);
  uint8_t key[16]; return derive(p.uid,key) && sendPacket(fd_,p.endpoint,packet,key);
}
bool MasterDisplayWifi::routeHook(void* self,const Frame& f) {
  return static_cast<MasterDisplayWifi*>(self)->route(f);
}
void MasterDisplayWifi::beginFirmware(uint8_t addr) {
  firmware_wifi_=active(addr);
  const Peer* p=peer(addr);
  firmware_uid_=p ? p->uid : 0;
  firmware_addr_.store(addr);
}
bool MasterDisplayWifi::pollHook(void* self,Frame& f) {
  return static_cast<MasterDisplayWifi*>(self)->receive(f);
}
bool MasterDisplayWifi::route(const Frame& frame) {
  if (firmware_addr_ && frame.dst==firmware_addr_) {
    if (!firmware_wifi_) return false;
    Peer* p=peer(frame.dst);
    if (p && p->uid==firmware_uid_ && p->connected && active(frame.dst)) send(*p,DATA,&frame);
    // Lost WiFi during OTA must never leak a retry onto the physical bus.
    return true;
  }
  if (physical_) return false;
  if (frame.dst==ADDR_BROADCAST) {
    if (frame.cmd==CMD_LED_SYNC || frame.cmd==CMD_DISPLAY_UPDATE)
      for (auto& p:peers_) if (active(p.addr) && p.connected) send(p,DATA,&frame);
    return false;
  }
  Peer* p=peer(frame.dst);
  if (!p || !active(frame.dst)) return false;
  send(*p,DATA,&frame);
  // No serial fallback within a request: that could execute a command twice.
  return true;
}
bool MasterDisplayWifi::receive(Frame& frame) {
  if (!ready_ || WiFi.status()!=WL_CONNECTED) return false;
  if (fd_<0) {
    if (socket_retry_ms_ && (uint32_t)(millis()-socket_retry_ms_)<3000) return false;
    socket_retry_ms_=millis(); fd_=socketOpen(PORT);
    if (fd_<0) return false;
  }
  // One datagram per poll bounds authentication work under unsolicited traffic.
  uint8_t raw[PACKET_MAX+1]; sockaddr_in from={}; socklen_t from_len=sizeof(from);
  int n=recvfrom(fd_,raw,sizeof(raw),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&from),&from_len);
  if (n<(int)(HEADER+TAG) || n>(int)PACKET_MAX) return false;
  uint64_t uid=get_u64_le(raw+6); uint8_t key[16]; Packet msg;
  if (!derive(uid,key) || !decode(raw,n,key,msg)) return false;
  Peer* p=nullptr;
  for (auto& candidate:peers_) if (candidate.uid==uid) { p=&candidate; break; }
  if (!p) for (auto& candidate:peers_) if (!candidate.uid) { p=&candidate; p->uid=uid; break; }
  if (!p) return false;
  const uint32_t now=millis();
  if (msg.kind==HELLO && msg.client && !msg.server && !msg.counter && msg.length==0 && msg.mode!=WIRED) {
    if (p->connected && (uint32_t)(now-p->seen)<3000) return false;
    if (p->pending_client!=msg.client || !samePeer(p->pending_endpoint,from) || (uint32_t)(now-p->pending_ms)>5000) {
      p->pending_client=msg.client; p->pending_server=nonce(); p->pending_endpoint=from; p->pending_ms=now;
    }
    Packet challenge; challenge.kind=CHALLENGE; challenge.uid=uid; challenge.mode=msg.mode;
    challenge.client=msg.client; challenge.server=p->pending_server;
    sendPacket(fd_,from,challenge,key); return false;
  }
  if (msg.kind==PROOF && msg.length==0 && msg.counter==0 && msg.client==p->pending_client &&
      msg.server==p->pending_server && msg.server && samePeer(from,p->pending_endpoint) && (uint32_t)(now-p->pending_ms)<=5000) {
    uint8_t addr=0;
    for (uint8_t i=0;i<registry_->count();++i) {
      const auto& rec=registry_->at(i);
      if (rec.uid==uid) { if (rec.type!=MODULE_DISPLAY) return false; addr=rec.addr; break; }
    }
    if (!addr) for (uint8_t a=0x40;a<=0x4f;++a) if (!registry_->find(a)) { addr=a; break; }
    if (addr<0x40 || addr>0x4f) return false;
    ModuleRecord* rec=registry_->bindUidToAddress(uid,addr); if (!rec) return false;
    rec->type=MODULE_DISPLAY;
    // Retain identity/alias; INFO and CAPS are refreshed through normal discovery.
    if (!rec->caps) rec->caps=CAP_DISPLAY | CAP_DISPLAY_HYBRID;
    // Keep availability through a cable/WiFi handover. Normal discovery refreshes
    // identity, and only failed requests may turn an online module offline.
    rec->consecutive_timeouts=0;
    rec->last_seen_ms=now;
    p->connected=false; publish(*p);
    p->addr=addr; p->client=msg.client; p->server=msg.server; p->endpoint=from;
    p->mode=msg.mode; p->rx=0; p->tx=0; p->seen=now; p->connected=true; p->wired_probes=0;
    publish(*p);
    p->pending_client=0; p->pending_server=0;
    Packet ready; ready.kind=READY; ready.uid=uid; ready.mode=p->mode; ready.client=p->client; ready.server=p->server;
    ready.length=1; ready.body[0]=addr; sendPacket(fd_,from,ready,key);
    frame=Frame(); frame.dst=ADDR_MASTER; frame.src=addr; frame.cmd=CMD_DISCOVER_MODULES|0x80; frame.len=15;
    memset(frame.payload,0,15); frame.payload[0]=STATUS_OK; frame.payload[1]=MODULE_DISPLAY;
    put_u64_le(frame.payload+2,uid); frame.payload[10]=addr;
    return true;
  }
  // A lost READY may cause repeated PROOF; acknowledge without resetting counters.
  if (msg.kind==PROOF && p->connected && msg.client==p->client && msg.server==p->server &&
      samePeer(from,p->endpoint) && !msg.counter && !msg.length) {
    Packet ready; ready.kind=READY; ready.uid=uid; ready.mode=p->mode; ready.client=p->client; ready.server=p->server;
    ready.length=1; ready.body[0]=p->addr; sendPacket(fd_,from,ready,key); return false;
  }
  if (!p->connected || !samePeer(from,p->endpoint) ||
      !inSession(msg,p->uid,p->client,p->server,p->mode,p->rx)) return false;
  if (msg.kind==LEAVE) { p->rx=msg.counter; p->connected=false; publish(*p); return false; }
  if (msg.kind==KEEPALIVE && !msg.length) {
    p->rx=msg.counter; p->seen=now; publish(*p); send(*p,KEEPALIVE); return false;
  }
  if (msg.kind!=DATA || !getFrame(msg,frame) || frame.src!=p->addr || frame.dst!=ADDR_MASTER || !(frame.cmd&0x80)) return false;
  p->rx=msg.counter; p->seen=now;
  publish(*p);
  return true;
}
