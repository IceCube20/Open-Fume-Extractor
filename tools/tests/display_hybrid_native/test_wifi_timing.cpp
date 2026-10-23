#include "OfeDisplayTunnel.h"
#include "../../../OpenFumeExtractorMaster/src/WifiTiming.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace ofe_wifi;
static uint32_t clock_us = 1000;
static uint32_t micros() { return clock_us; }
static uint32_t millis() { return clock_us / 1000; }
static uint8_t test_key[16] = {1,2,3,4};
static std::vector<uint8_t> sent;
static unsigned datagrams=0;
static bool capturePacket(int, const sockaddr_in&, const Packet& packet, const uint8_t* key) {
  ++datagrams;
  sent.resize(PACKET_MAX);
  const auto size = encode(packet, key, sent.data()); assert(size);
  sent.resize(size); return true;
}
static Packet captured() { Packet p; assert(decode(sent.data(),sent.size(),test_key,p)); return p; }
static void portENTER_CRITICAL(int* lock) { assert(!*lock); *lock = 1; }
static void portEXIT_CRITICAL(int* lock) { assert(*lock); *lock = 0; }
class MasterDisplayWifi {
 public:
  struct Peer {
    uint64_t uid=0x4000000012345678ULL,client=42,server=99;
    sockaddr_in endpoint={};
    uint32_t tx=0,rx=0,seen=0;
    uint8_t addr=0x40,mode=AUTOMATIC;
    bool connected=true,timing_capable=false;
    bool timing_probe_v2=false;
    uint8_t timing_version=0;
    uint32_t timing_cap_generation=0;
    ofe_wifi_timing::Tracker timing;
  } peers_[1];
  int fd_=1,timing_mux_=0;
  uint32_t timing_token_=0;
  std::atomic<uint32_t> timing_generation_{1};
  std::atomic<uint8_t> firmware_addr_{0};
  bool timing_enabled_=false;
  ofe_wifi_timing::Snapshot timing_stats_[16];
  bool timingEnabled() const { return timing_enabled_; }
  bool derive(uint64_t, uint8_t* key) { memcpy(key,test_key,16); return true; }
  void publish(const Peer&) {}
  Peer* peer(uint8_t addr) { return addr == 0x40 ? &peers_[0] : nullptr; }
  bool send(Peer&,uint8_t,const Frame* = nullptr);
  bool deliver(const Packet&,Frame&,uint32_t);
  void responseProcessed(const Frame&);
};
class OfeDisplayWifi {
 public:
  Config active_config_;
  int fd_=2;
  sockaddr_in endpoint_={};
  uint64_t uid_=0x4000000012345678ULL,client_=42,server_=99;
  uint32_t tx_=0,rx_=0,seen_=0,last_wired_ms_=0;
  uint8_t addr_=0x40,*address_=&addr_;
  bool connected_=true,network_origin_=false,timing_enabled_=false;
  uint32_t timing_token_=0,timing_received_us_=0;
  uint8_t timing_sequence_=0,timing_command_=0;
  uint8_t timing_version_=1,timing_reply_version_=1;
  uint32_t timing_request_poll_gap_us_=UINT32_MAX;
  RxPollGap timing_rx_polls_;
  std::atomic<bool> last_transport_wireless_{false};
  OfeDisplayWifi() { memcpy(active_config_.key,test_key,16); }
  void resetSession(bool) { connected_=false; timing_enabled_=false; timing_token_=0; }
  bool persistAssignedAddress(uint8_t) { return true; }
  void setActive(bool) {}
  bool send(uint8_t,const Frame* = nullptr);
  bool deliver(const uint8_t*,size_t,uint32_t,Frame&);
};
#include "generated_wifi_timing.inc.h"

static void wire_codec_tests() {
  for (uint8_t version=1;version<=2;++version) for (unsigned length=0;length<=MAX_PAYLOAD;++length) {
    Frame f; f.dst=0x40;f.src=ADDR_MASTER;f.cmd=CMD_DISPLAY_STATUS;f.seq=23;f.len=uint8_t(length);
    for(unsigned i=0;i<length;++i)f.payload[i]=uint8_t(i);
    Packet p;p.counter=7;p.uid=123;p.client=42;p.server=99;
    setTimedFrame(p,f,0x12345678,7654,version,4321);
    uint8_t raw[PACKET_MAX+1];const auto size=encode(p,test_key,raw);assert(size);
    Packet decoded;assert(decode(raw,size,test_key,decoded));
    uint32_t token=0,processing=0,gap=0;uint8_t parsed_version=0;Frame out;
    assert(getTimedFrame(decoded,out,token,processing,&gap,&parsed_version));
    assert(parsed_version==version&&gap==(version==2 ? 4321U : UINT32_MAX));
    assert(token==0x12345678&&processing==7654&&out.len==f.len&&!memcmp(out.payload,f.payload,f.len));
    assert(!getFrame(decoded,out)); // a timed trailer can never leak into legacy frame parsing
    for(size_t cut=0;cut<size;++cut)assert(!decode(raw,cut,test_key,decoded));
    for(size_t i=0;i<size;++i){raw[i]^=1;assert(!decode(raw,size,test_key,decoded));raw[i]^=1;}
    assert(!decode(raw,size+1,test_key,decoded));
    --p.length;assert(!getTimedFrame(p,out,token,processing));++p.length;
    put_u32_le(p.body+5+f.len,0);assert(!getTimedFrame(p,out,token,processing));
    Packet legacy;legacy.kind=DATA;setFrame(legacy,f);
    assert(getFrame(legacy,out)&&!getTimedFrame(legacy,out,token,processing));
  }
}
static void tracker_tests() {
  using namespace ofe_wifi_timing;
  static_assert(sizeof(Snapshot)<=208 && sizeof(Tracker)<=32, "Bounded per-display diagnostic storage");
  Tracker tracker;Snapshot s;
  tracker.begin(1,255,CMD_DISPLAY_STATUS,UINT32_MAX-999,3);
  tracker.polled(UINT32_MAX-499);
  assert(!tracker.finish(2,255,CMD_DISPLAY_STATUS|0x80,100,500,1,3,s));
  assert(!tracker.finish(1,254,CMD_DISPLAY_STATUS|0x80,100,500,1,3,s));
  assert(!tracker.finish(1,255,CMD_GET_TELEMETRY|0x80,100,500,1,3,s));
  assert(!tracker.finish(1,255,CMD_DISPLAY_STATUS|0x80,100,500,1,4,s));
  assert(tracker.finish(1,255,CMD_DISPLAY_STATUS|0x80,100,500,1,3,s));
  assert(s.rtt.last==1500&&s.transport.last==1400&&s.master_poll.last==1000);
  assert(s.display_poll.count==0&&s.peak.display_poll_us==UINT32_MAX);
  tracker.dispatched(255,CMD_DISPLAY_STATUS|0x80,600,3,s);assert(s.master_queue.last==100);
  assert(s.peak.master_queue_us==100);
  tracker.dispatched(255,CMD_DISPLAY_STATUS|0x80,700,3,s);assert(s.master_queue.count==1);
  assert(!tracker.finish(1,255,CMD_DISPLAY_STATUS|0x80,100,500,1,3,s));
  tracker.begin(2,0,CMD_DISPLAY_STATUS,100,3);
  assert(!tracker.finish(2,0,CMD_DISPLAY_STATUS|0x80,501,600,1,3,s)&&s.invalid==1);
  tracker.begin(3,0,CMD_DISPLAY_STATUS,100,3);tracker.reset();
  assert(!tracker.finish(3,0,CMD_DISPLAY_STATUS|0x80,100,600,1,3,s));
  tracker.begin(4,255,CMD_DISPLAY_STATUS,2000,3);
  assert(tracker.finish(4,255,CMD_DISPLAY_STATUS|0x80,50,2500,2,3,s,123));
  tracker.dispatched(255,CMD_DISPLAY_STATUS|0x80,2800,3,s);
  assert(s.master_queue.last==300&&s.peak.token==1&&s.peak.master_queue_us==100);
  tracker.begin(5,0,CMD_DISPLAY_STATUS,3000,3);
  tracker.polled(3200);tracker.polled(3400);
  assert(tracker.finish(5,0,CMD_DISPLAY_STATUS|0x80,200,5000,5,3,s,456));
  assert(s.peak.token==5&&s.peak.rtt_us==2000&&s.peak.display_poll_us==456&&s.peak.master_poll_us==1600);
  assert(s.peak.master_queue_us==UINT32_MAX);
  tracker.dispatched(0,CMD_DISPLAY_STATUS|0x80,5100,3,s);assert(s.peak.master_queue_us==100);
  RxPollGap polls;polls.sample(UINT32_MAX-99);assert(polls.gap==UINT32_MAX);
  polls.sample(200);assert(polls.gap==300);polls.reset();polls.sample(1000);assert(polls.gap==UINT32_MAX);
}
static void legacy_negotiation_tests() {
  MasterDisplayWifi master;auto& peer=master.peers_[0];master.timing_enabled_=true;
  Frame request;request.dst=0x40;request.src=ADDR_MASTER;request.seq=19;request.cmd=CMD_DISPLAY_STATUS;
  Frame out;bool old_display_enabled=false;unsigned timed_requests=0;
  // V1 firmware accepts only WT1 and disables timing after an unknown WT2.
  // Exercise live data between every heartbeat, including the transition.
  for(unsigned i=0;i<8;++i) {
    Packet heartbeat;heartbeat.kind=KEEPALIVE;heartbeat.uid=peer.uid;heartbeat.client=peer.client;
    heartbeat.server=peer.server;heartbeat.counter=peer.rx+1;
    if(old_display_enabled) { heartbeat.length=3;heartbeat.body[0]='W';heartbeat.body[1]='T';heartbeat.body[2]=1; }
    assert(!master.deliver(heartbeat,out,clock_us));
    const auto offer=captured();old_display_enabled=offer.length==3&&offer.body[2]==1;
    assert(master.send(peer,DATA,&request));
    const auto data=captured();
    assert(data.kind==DATA||old_display_enabled);
    if(data.kind==TIMED_DATA) { ++timed_requests;uint32_t token,processing,gap;uint8_t version;
      assert(getTimedFrame(data,out,token,processing,&gap,&version)&&version==1);
    }
  }
  assert(timed_requests>0);
  // An old Master never offers V2; a new display must still return an 8-B trailer.
  OfeDisplayWifi display;
  Packet offer;offer.kind=KEEPALIVE;offer.uid=display.uid_;offer.client=display.client_;offer.server=display.server_;
  offer.counter=1;offer.length=3;offer.body[0]='W';offer.body[1]='T';offer.body[2]=1;
  capturePacket(0,peer.endpoint,offer,test_key);assert(!display.deliver(sent.data(),sent.size(),clock_us,out));
  Packet data=offer;data.counter=2;setTimedFrame(data,request,99);
  capturePacket(0,peer.endpoint,data,test_key);assert(display.deliver(sent.data(),sent.size(),clock_us,out));
  Frame response;response.dst=ADDR_MASTER;response.src=0x40;response.seq=request.seq;response.cmd=request.cmd|0x80;
  clock_us+=100;assert(display.send(DATA,&response));
  uint32_t token,processing,gap;uint8_t version;
  assert(getTimedFrame(captured(),out,token,processing,&gap,&version)&&version==1&&gap==UINT32_MAX);
  master.timing_generation_.fetch_add(1);peer.timing_capable=true;
  assert(master.send(peer,DATA,&request)&&captured().kind==DATA); // fresh negotiation after reset
}
int main() {
  wire_codec_tests();tracker_tests();legacy_negotiation_tests();
  MasterDisplayWifi master;OfeDisplayWifi display;Frame received;
  auto& peer=master.peers_[0];auto& stats=master.timing_stats_[0];
  // No extra handshake packets: negotiate through existing keepalive/ACK.
  assert(display.send(KEEPALIVE));assert(!master.deliver(captured(),received,clock_us));
  assert(!display.deliver(sent.data(),sent.size(),clock_us,received)&&!display.timing_enabled_);
  master.timing_enabled_=true;
  assert(display.send(KEEPALIVE));assert(!master.deliver(captured(),received,clock_us));
  assert(!display.deliver(sent.data(),sent.size(),clock_us,received)&&display.timing_enabled_);
  assert(!peer.timing_capable);
  assert(display.send(KEEPALIVE));assert(!master.deliver(captured(),received,clock_us));
  assert(peer.timing_capable&&stats.supported);
  assert(!display.deliver(sent.data(),sent.size(),clock_us,received));
  assert(display.timing_version_==1);
  assert(display.send(KEEPALIVE));assert(!master.deliver(captured(),received,clock_us));
  assert(!display.deliver(sent.data(),sent.size(),clock_us,received));
  assert(display.timing_version_==2);
  assert(display.send(KEEPALIVE));assert(!master.deliver(captured(),received,clock_us));
  assert(peer.timing_version==2&&stats.version==2);
  assert(!display.deliver(sent.data(),sent.size(),clock_us,received));
  Frame request;request.dst=0x40;request.src=ADDR_MASTER;request.seq=15;request.cmd=CMD_DISPLAY_STATUS;
  const unsigned before=datagrams;
  clock_us=1000;assert(master.send(peer,DATA,&request));assert(captured().kind==TIMED_DATA);
  const auto request_wire=sent;
  for(uint32_t us=2000;us<=10000;us+=2000)peer.timing.polled(us);
  // Display clock deliberately differs by almost a full uint32_t range.
  display.timing_rx_polls_.sample(UINT32_MAX-3099);
  clock_us=UINT32_MAX-99;
  assert(display.deliver(request_wire.data(),request_wire.size(),clock_us,received));
  Frame response;response.dst=ADDR_MASTER;response.src=0x40;response.seq=15;response.cmd=CMD_DISPLAY_STATUS|0x80;
  response.len=1;response.payload[0]=STATUS_OK;
  clock_us+=300;assert(display.send(DATA,&response));const Packet reply=captured();
  assert(datagrams==before+2); // measurement adds bytes, not probe packets
  Frame unpacked;uint32_t token,processing,gap;uint8_t version;
  assert(getTimedFrame(reply,unpacked,token,processing,&gap,&version)&&processing==300&&version==2&&gap==3000);
  clock_us=12000;assert(master.deliver(reply,received,clock_us));
  assert(stats.rtt.last==11000&&stats.display.last==300&&stats.transport.last==10700);
  assert(stats.master_poll.last==2000&&stats.display_poll.last==3000);
  assert(stats.peak.rtt_us==11000&&stats.peak.master_poll_us==2000&&stats.peak.display_poll_us==3000);
  clock_us=14500;master.responseProcessed(received);assert(stats.master_queue.last==2500);
  assert(stats.peak.master_queue_us==2500);
  assert(!master.deliver(reply,received,clock_us)&&stats.rtt.count==1); // replay
  // Disabling timing leaves ordinary live-data traffic working unchanged.
  master.timing_enabled_=false;assert(master.send(peer,DATA,&request));assert(captured().kind==DATA);
  assert(display.deliver(sent.data(),sent.size(),clock_us,received));
  assert(display.send(DATA,&response)&&captured().kind==DATA);
  assert(master.deliver(captured(),received,clock_us));
  assert(stats.rtt.count==1);
  master.timing_enabled_=true;peer.timing_capable=false;
  assert(master.send(peer,DATA,&request)&&captured().kind==DATA); // legacy display
  peer.timing_capable=true;request.cmd=CMD_FW_CHUNK;
  assert(master.send(peer,DATA,&request)&&captured().kind==DATA);
  request.cmd=CMD_DISPLAY_STATUS;master.firmware_addr_.store(0x40);
  assert(master.send(peer,DATA,&request)&&captured().kind==DATA);
  master.firmware_addr_.store(0);request.dst=ADDR_BROADCAST;
  assert(master.send(peer,DATA,&request)&&captured().kind==DATA);
  puts("PASS: actual Master/display timing send+receive handlers, authenticated maximum packets, capability negotiation, legacy/off/OTA/broadcast paths, independent clocks, replay, bad samples, dispatch delay and microsecond wrap");
}
