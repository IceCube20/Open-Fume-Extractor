#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "Rs485PeripheralBus.h"
#include "../../../OpenFumeExtractorMaster/src/WifiOtaDiagnostics.h"
using namespace jbc_rs485;
static constexpr uint16_t BULK_DATA_MAX=1024;
static constexpr size_t BULK_HEADER=40, TAG=16;
static constexpr size_t BULK_PACKET_MAX=BULK_HEADER+BULK_DATA_MAX+TAG;
static constexpr int MSG_PEEK=2, MSG_DONTWAIT=0;
using socklen_t=int;
struct sockaddr {};
struct sockaddr_in { uint32_t ip=1; uint16_t port=2; };
static uint32_t now_ms=0;
static uint32_t millis() { return now_ms; }
static uint32_t micros() { return now_ms*1000U; }
static void delay(uint32_t ms) { now_ms+=ms; }
struct BulkPacketView {
  bool ack=true; uint8_t mode=1,status=STATUS_OK;
  uint64_t uid=123,client=42,server=99;
  uint32_t request=0,offset=0;
};
enum Scenario { Good, Corrupt, WrongEndpoint, WrongSession, WrongRequest,
                WrongOffset, RemoteBusy, Silence, SendFail, Control, ReadFailure, NotAck };
static Scenario scenario=Good;
struct Datagram { BulkPacketView ack; sockaddr_in from; bool valid=true,normal=false,read_fail=false; };
static std::deque<Datagram> incoming;
static Datagram consumed;
static std::vector<uint32_t> sent_requests;
static bool samePeer(const sockaddr_in& a,const sockaddr_in& b) { return a.ip==b.ip && a.port==b.port; }
// Only socket delivery/authentication outcomes are adapted. The complete wait,
// filtering, retry and acceptance function is extracted from production below.
static bool sendBulkPacket(int,const sockaddr_in& to,bool ack,uint8_t mode,uint8_t,
    uint64_t uid,uint64_t client,uint64_t server,uint32_t request,uint32_t offset,
    const uint8_t*,uint16_t len,const uint8_t*) {
  assert(!ack);
  sent_requests.push_back(request);
  if (scenario==SendFail) return false;
  if (scenario==Silence) return true;
  Datagram good; good.from=to; good.ack={true,mode,STATUS_OK,uid,client,server,request,offset+len};
  if (scenario!=Good) {
    auto bad=good;
    switch(scenario) {
      case Corrupt: bad.valid=false; break;
      case WrongEndpoint: ++bad.from.port; break;
      case WrongSession: ++bad.ack.server; break;
      case WrongRequest: --bad.ack.request; break;
      case WrongOffset: ++bad.ack.offset; break;
      case RemoteBusy: bad.ack.status=STATUS_BUSY; break;
      case Control: bad.normal=true; break;
      case ReadFailure: bad.read_fail=true; break;
      case NotAck: bad.ack.ack=false; break;
      default: assert(false);
    }
    incoming.push_back(bad);
    if (scenario==WrongOffset || scenario==RemoteBusy) return true;
  }
  incoming.push_back(good);
  return true;
}
static int recvfrom(int,void* data,size_t capacity,int flags,sockaddr* from,socklen_t* size) {
  if (incoming.empty()) return -1;
  const auto p=incoming.front();
  std::memcpy(from,&p.from,sizeof(p.from)); *size=sizeof(p.from);
  if (!(flags&MSG_PEEK)) {
    incoming.pop_front(); consumed=p;
    if (p.read_fail) return -1;
  }
  const size_t n=std::min(capacity,BULK_HEADER+TAG);
  std::memset(data,0,n); std::memcpy(data,p.normal ? "OFD1" : "OFA1",std::min(n,size_t(4)));
  return int(n);
}
static bool decodeBulk(const uint8_t*,size_t,const uint8_t*,BulkPacketView& out) {
  out=consumed.ack; return consumed.valid;
}
class MasterDisplayWifi {
 public:
  struct Peer {
    uint64_t uid=123,client=42,server=99;
    uint8_t mode=1;
    uint32_t seen=0;
    bool connected=true;
    sockaddr_in endpoint;
  } peer_;
  std::atomic<uint8_t> firmware_addr_{0x40};
  bool firmware_wifi_=true,firmware_bulk_available_=true,firmware_bulk_confirmed_=false;
  bool firmware_bulk_retry_pending_=false;
  uint32_t firmware_bulk_retry_started_ms_=0,firmware_bulk_retry_delay_ms_=2000;
  uint32_t firmware_bulk_request_=1;
  uint64_t firmware_uid_=123;
  int fd_=7;
  unsigned controls=0;
  ofe_wifi_ota::Diagnostics firmware_bulk_diagnostics_;
  bool active(uint8_t addr) const { return addr==0x40; }
  Peer* peer(uint8_t addr) { return addr==0x40 ? &peer_ : nullptr; }
  bool derive(uint64_t,uint8_t* key) { std::memset(key,1,16); return true; }
  void publish(const Peer&) {}
  bool receive(Frame&) { assert(!incoming.empty()); incoming.pop_front(); ++controls; return false; }
  bool firmwareBulkChunk(uint8_t,uint32_t,const uint8_t*,uint16_t);
#include "generated_master_bulk_policy.inc.h"
};
#include "generated_master_bulk.inc.h"
namespace ofe_wifi { static constexpr uint16_t BULK_DATA_MAX=1024; }
static uint16_t module_update_wifi_bulk_limit=0;
#include "generated_bulk_limit.inc.h"

int main() {
  for (uint16_t previous : {0,256,512,1024}) {
    module_update_wifi_bulk_limit=previous;
    reset_bulk_limit();
    assert(module_update_wifi_bulk_limit==ofe_wifi::BULK_DATA_MAX);
  }
  uint8_t bytes[1024]={};
  for (Scenario which : {Good,Corrupt,WrongEndpoint,WrongSession,WrongRequest,Control,ReadFailure,NotAck}) {
    scenario=which; incoming.clear(); sent_requests.clear(); now_ms=0;
    MasterDisplayWifi master;
    assert(master.firmwareBulkChunk(0x40,4096,bytes,sizeof(bytes)));
    const auto s=master.firmware_bulk_diagnostics_.snapshot();
    assert(master.firmware_bulk_confirmed_ && s.events[ofe_wifi_ota::Tx]==1 && !s.events[ofe_wifi_ota::Timeout]);
    if (which==Corrupt || which==NotAck) assert(s.events[ofe_wifi_ota::Invalid]==1);
    if (which==WrongEndpoint) assert(s.events[ofe_wifi_ota::Endpoint]==1);
    if (which==WrongSession) assert(s.events[ofe_wifi_ota::Session]==1);
    if (which==WrongRequest) assert(s.events[ofe_wifi_ota::Request]==1);
    if (which==ReadFailure) assert(s.events[ofe_wifi_ota::ReadFail]==1);
    if (which==Control) assert(master.controls==1);
  }
  for (Scenario which : {WrongOffset,RemoteBusy,Silence,SendFail}) {
    scenario=which; incoming.clear(); sent_requests.clear(); now_ms=0xfffffc00U;
    MasterDisplayWifi master;
    assert(!master.firmwareBulkChunk(0x40,4096,bytes,sizeof(bytes)));
    const auto s=master.firmware_bulk_diagnostics_.snapshot();
    assert(!master.firmware_bulk_available_ && s.events[ofe_wifi_ota::Tx]==3);
    assert(sent_requests.size()==3 && sent_requests[0]==sent_requests[1] && sent_requests[1]==sent_requests[2]);
    if (which==WrongOffset) assert(s.events[ofe_wifi_ota::Offset]==3 && !s.events[ofe_wifi_ota::Timeout]);
    if (which==RemoteBusy) assert(s.events[ofe_wifi_ota::Status]==3 && !s.events[ofe_wifi_ota::Timeout]);
    if (which==Silence) assert(s.events[ofe_wifi_ota::Timeout]==3 && s.poll_gap_max_us==1000);
    if (which==SendFail) assert(s.events[ofe_wifi_ota::TxFail]==3 && !s.events[ofe_wifi_ota::Timeout]);
  }
  scenario=Good; incoming.clear(); sent_requests.clear();
  MasterDisplayWifi master;
  master.peer_.uid=456;
  assert(!master.firmwareBulkChunk(0x40,4096,bytes,sizeof(bytes)) && sent_requests.empty());
  master.firmware_bulk_diagnostics_.reset();
  for (auto n:master.firmware_bulk_diagnostics_.snapshot().events) assert(n==0);
  puts("PASS: production bulk ACK loop, identity/auth/session/request/offset/status filtering, control packets, socket errors, retries, time wrap and diagnostic counters.");
}
