#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <cstdio>
#include <string>
#include <functional>
#include <vector>
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static uint32_t now_ms=0;
static uint32_t millis() { return now_ms; }
static uint32_t esp_random() { static uint32_t n=100; return ++n; }
static std::function<void()> scan_hook,delay_hook;
static void vTaskDelay(uint32_t ms) { now_ms+=ms; if(delay_hook) delay_hook(); }
#define pdMS_TO_TICKS(ms) (ms)
enum { pdPASS=1 };
using TaskHandle_t=void*;
static bool task_success=true;
static int created_tasks=0;
static int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,void** handle,int) {
  ++created_tasks;
  if(!task_success) return 0;
  *handle=reinterpret_cast<void*>(1); return pdPASS;
}
struct NimBLEConnInfo {
  bool encrypted=true,authenticated=true; int key=16;
  bool isEncrypted() const { return encrypted; }
  bool isAuthenticated() const { return authenticated; }
  int getSecKeySize() const { return key; }
};
enum { MALLOC_CAP_INTERNAL=0x800, MALLOC_CAP_8BIT=4, BLE_HS_IO_DISPLAY_YESNO=1, BLE_HS_EAUTHEN=23 };
static uint32_t free_internal=120U*1024U, largest_internal=64U*1024U;
static uint32_t controller_bytes=68U*1024U;
static uint32_t heap_caps_get_free_size(uint32_t caps) {
  assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)); return free_internal;
}
static uint32_t heap_caps_get_largest_free_block(uint32_t caps) {
  assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)); return largest_internal;
}
struct NimBLEUUID { explicit NimBLEUUID(const char*) {} };
struct FakeAddress {
  std::string toString() const { return "01:02:03:04:05:06"; }
  uint8_t getType() const { return 1; }
};
struct NimBLEAdvertisedDevice {
  bool service=true;
  std::string manufacturer;
  bool isAdvertisingService(const NimBLEUUID&) const { return service; }
  std::string getManufacturerData() const { return manufacturer; }
  FakeAddress getAddress() const { return {}; }
  int16_t getRSSI() const { return -42; }
};
static size_t strlcpy(char* destination,const char* source,size_t capacity) {
  const size_t length=strlen(source);
  if(capacity) { const size_t count=length<capacity-1 ? length : capacity-1; memcpy(destination,source,count); destination[count]=0; }
  return length;
}
struct FakeScan {
  int scans=0;
  void setActiveScan(bool active) { assert(active); }
  void setScanCallbacks(void*,bool duplicates) { assert(!duplicates); }
  void setMaxResults(int maximum) { assert(maximum==0); }
  void getResults(uint32_t duration) { assert(duration && duration<=4000); ++scans; now_ms+=duration; if(scan_hook) scan_hook(); }
  void clearResults() {}
};
using NimBLEAttValue=std::vector<uint8_t>;
struct NimBLEAddress { NimBLEAddress(const char*,uint8_t) {} };
static std::function<void()> secure_hook;
struct FakeCharacteristic {
  bool identity=false;
  NimBLEAttValue readValue() {
    if (!identity) return {2};
    return {42,0,0,0,0,0,0,0x40};
  }
  bool writeValue(const uint8_t*,size_t,bool response) { assert(response); return true; }
};
struct FakeService {
  FakeCharacteristic identity{true},value;
  FakeCharacteristic* getCharacteristic(const char* uuid) {
    return std::string(uuid).find("f101")!=std::string::npos ? &identity : &value;
  }
};
struct FakeClient {
  bool connected=true,connect_ok=true,secure_ok=true;
  int service_calls=0;
  FakeService service;
  void setClientCallbacks(void*,bool) {}
  void setConnectTimeout(unsigned) {}
  bool connect(NimBLEAddress) { connected=connect_ok; return connect_ok; }
  bool secureConnection(bool async) { assert(async); if(secure_hook) secure_hook(); return secure_ok; }
  bool isConnected() const { return connected; }
  int getLastError() const { return 77; }
  FakeService* getService(const char*) { ++service_calls; return &service; }
};
struct NimBLEDevice {
  static inline FakeClient client;
  static FakeClient* createClient() { return &client; }
  static inline FakeScan scan;
  static FakeScan* getScan() { assert(initialized); return &scan; }
  static inline int injected=0;
  static inline bool accepted=false;
  static bool injectConfirmPasskey(const NimBLEConnInfo&,bool accept) { ++injected; accepted=accept; return true; }
  static inline bool initialized=false,fail_init=false;
  static inline int inits=0,deinits=0,cache_size=100,security=0;
  static bool isInitialized() { return initialized; }
  static void setScanDuplicateCacheSize(int n) { assert(!initialized); cache_size=n; }
  static bool init(const char*) {
    ++inits; if(fail_init) return false;
    assert(!initialized); assert(free_internal>=controller_bytes);
    free_internal-=controller_bytes; initialized=true; return true;
  }
  static bool deinit(bool clear) {
    assert(clear && initialized); ++deinits;
    free_internal+=controller_bytes; initialized=false; return true;
  }
  static void setSecurityAuth(bool bonding,bool mitm,bool secure) { assert(!bonding && mitm && secure); ++security; }
  static void setSecurityIOCap(int cap) { assert(cap==BLE_HS_IO_DISPLAY_YESNO); }
  static bool setMTU(int mtu) { assert(mtu==247); return true; }
};
namespace jbc_rs485 {
uint64_t get_u64_le(const uint8_t* bytes) {
  uint64_t n=0; for(int i=0;i<8;++i)n|=uint64_t(bytes[i])<<(i*8); return n;
}
void put_u64_le(uint8_t* bytes,uint64_t n) { for(int i=0;i<8;++i) bytes[i]=uint8_t(n>>(i*8)); }
}
#include "generated_ble.inc.h"
static ofe_wifi::Config config() {
  ofe_wifi::Config c; strcpy_s(c.ssid,"test-wifi"); strcpy_s(c.password,"test-password");
  strcpy_s(c.host,"192.0.2.1"); c.key[0]=42; return c;
}
static void protocol() {
  const uint64_t uid=0x400000000000002A;
  uint8_t ad[15]={255,255,'O','F','E','1'};
  jbc_rs485::put_u64_le(ad+6,uid); ad[14]=2;
  ofe_ble::Candidate candidate;
  assert(ofe_ble::parseAdvertisement(ad,15,candidate)); assert(candidate.uid==uid && candidate.kind==2);
  assert(!ofe_ble::parseAdvertisement(ad,14,candidate));
  ad[14]=3; assert(ofe_ble::parseAdvertisement(ad,15,candidate));
  ad[14]=4; assert(!ofe_ble::parseAdvertisement(ad,15,candidate)); ad[14]=1;
  ad[0]=0; assert(!ofe_ble::parseAdvertisement(ad,15,candidate)); ad[0]=255;
  ad[13]=0x10; assert(!ofe_ble::parseAdvertisement(ad,15,candidate));
  uint8_t payload[12+sizeof(ofe_wifi::Config)]; memcpy(payload,"OPB1",4);
  jbc_rs485::put_u64_le(payload+4,uid); auto c=config(); memcpy(payload+12,&c,sizeof(c));
  ofe_wifi::Config decoded;
  assert(ofe_ble::readConfig(payload,sizeof(payload),uid,decoded));
  assert(!memcmp(&decoded,&c,sizeof(c)));
  assert(!ofe_ble::readConfig(payload,sizeof(payload)-1,uid,decoded));
  assert(!ofe_ble::readConfig(payload,sizeof(payload),uid+1,decoded));
  payload[0]='X'; assert(!ofe_ble::readConfig(payload,sizeof(payload),uid,decoded)); payload[0]='O';
  c.from_master=0; memcpy(payload+12,&c,sizeof(c)); assert(!ofe_ble::readConfig(payload,sizeof(payload),uid,decoded));
  c=config(); memset(c.host,'X',sizeof(c.host)); memcpy(payload+12,&c,sizeof(c)); assert(!ofe_ble::readConfig(payload,sizeof(payload),uid,decoded));
}
static void approval() {
  Approval a; a.view_.active=true; a.view_.phase=ofe_ble::CONFIRM; a.view_.token=7; a.view_.pin=123;
  a.confirmation_=NimBLEConnInfo{};
  assert(!a.approve(8,123,true)); assert(!a.approve(7,124,true));
  assert(a.approve(7,123,true)); assert(NimBLEDevice::injected==0);
  assert(!a.approve(7,123,true)); assert(a.live(0)); assert(NimBLEDevice::injected==1 && NimBLEDevice::accepted);
  assert(a.live(0)); assert(NimBLEDevice::injected==1);
  NimBLEConnInfo peer;
  a.authenticated(peer); assert(a.secured_);
  peer.key=15; a.authenticated(peer); assert(!a.secured_ && a.failed_ && !a.cancel_);
  Approval unapproved; unapproved.authenticated(NimBLEConnInfo{}); assert(!unapproved.secured_);
  Approval denied; denied.view_=a.view_; denied.view_.phase=ofe_ble::CONFIRM; denied.confirmation_=NimBLEConnInfo{};
  assert(denied.approve(7,123,false)); assert(!denied.live(0)); assert(!NimBLEDevice::accepted);
  Approval timeout; now_ms=0xFFFFFFF0; assert(timeout.live(now_ms)); now_ms=16; assert(timeout.live(0xFFFFFFF0));
  now_ms=120000-16; assert(!timeout.live(0xFFFFFFF0)); now_ms=0;
}
static void forget() {
  Forget f; f.shared_.config=config(); const auto original=f.shared_.config;
  assert(f.forgetPairing()); assert(f.save_pending_ && f.shared_.result==1);
  assert(!memcmp(&original,&f.shared_.config,sizeof(original)));
  assert(!strcmp(f.pending_.ssid,original.ssid)); assert(!strcmp(f.pending_.password,original.password));
  assert(f.pending_.mode==original.mode && f.pending_.from_master==0);
  assert(f.pending_.host[0]==0 && !ofe_wifi::nonzero(f.pending_.key,16));
  assert(!f.forgetPairing()); f.save_pending_=false; f.update_transport_=2; assert(!f.forgetPairing());
  f.update_transport_=0; f.worker_=false; assert(!f.forgetPairing());
  f.worker_=true; auto bad=config(); bad.version=255; assert(!f.save(bad));
}
static void radio() {
  Radio r; r.active_=true; r.view_.active=true;
  strcpy_s(r.config_.password,"temporary-secret");
  assert(r.startRadio() && NimBLEDevice::initialized);
  assert(NimBLEDevice::cache_size==20 && r.view_.memory_stage==3);
  assert(r.view_.free_internal==52U*1024U);
  assert(r.stopRadio() && !NimBLEDevice::initialized);
  assert(r.view_.memory_stage==4 && r.view_.free_internal==120U*1024U);
  assert(r.startRadio() && NimBLEDevice::security==2);
  r.finish(ofe_ble::DONE);
  assert(!r.active_ && !r.view_.active && r.view_.phase==ofe_ble::DONE);
  assert(!NimBLEDevice::initialized && free_internal==120U*1024U);
  assert(r.config_.password[0]==0 && r.worker_==nullptr);
  const int inits=NimBLEDevice::inits;
  free_internal=55U*1024U; assert(!r.startRadio());
  assert(r.view_.phase==ofe_ble::LOW_MEMORY && NimBLEDevice::inits==inits);
  assert(r.view_.memory_stage==2 && r.view_.free_internal==55U*1024U);
  free_internal=120U*1024U; largest_internal=15U*1024U; assert(!r.startRadio());
  assert(NimBLEDevice::inits==inits);
  largest_internal=64U*1024U; controller_bytes=101U*1024U;
  assert(!r.startRadio() && r.view_.phase==ofe_ble::LOW_MEMORY);
  assert(r.view_.memory_stage==3 && r.view_.free_internal==19U*1024U);
  assert(!NimBLEDevice::initialized && free_internal==120U*1024U);
  controller_bytes=68U*1024U; NimBLEDevice::fail_init=true;
  assert(!r.startRadio() && r.view_.phase==ofe_ble::FAILED);
  assert(!NimBLEDevice::initialized && free_internal==120U*1024U);
  NimBLEDevice::fail_init=false;
}
static void scan_store() {
  ScanStore store; store.view_.phase=ofe_ble::SCANNING;
  NimBLEAdvertisedDevice d;
  uint8_t advertisement[15]={255,255,'O','F','E','1'};
  jbc_rs485::put_u64_le(advertisement+6,0x400000000000002A);
  d.manufacturer.assign(reinterpret_cast<const char*>(advertisement),sizeof(advertisement));
  d.service=false; store.discovered(&d); assert(store.view_.count==0);
  d.service=true; d.manufacturer[0]=0; store.discovered(&d); assert(store.view_.count==0);
  d.manufacturer.assign(reinterpret_cast<const char*>(advertisement),sizeof(advertisement));
  store.discovered(&d); assert(store.view_.count==1);
  store.discovered(&d); assert(store.view_.count==1);
  assert(!strcmp(store.view_.devices[0].address,"01:02:03:04:05:06"));
  assert(store.view_.devices[0].address_type==1 && store.view_.devices[0].rssi==-42);
  for(uint64_t n=43;n<43+ofe_ble::MAX_DEVICES+4;++n) {
    jbc_rs485::put_u64_le(advertisement+6,0x4000000000000000+n);
    d.manufacturer.assign(reinterpret_cast<const char*>(advertisement),sizeof(advertisement));
    store.discovered(&d);
  }
  assert(store.view_.count==ofe_ble::MAX_DEVICES);
  d.manufacturer.clear(); assert(store.view_.devices[0].uid==0x400000000000002A);
  store.view_.phase=ofe_ble::READY; store.view_.count=0;
  d.manufacturer.assign(reinterpret_cast<const char*>(advertisement),sizeof(advertisement));
  store.discovered(&d); store.discovered(nullptr); assert(store.view_.count==0);
}
static void cached_selection() {
  Selection s; s.provider_=[](uint64_t,ofe_wifi::Config& out) { out=config(); return true; };
  s.view_.count=2; s.view_.devices[0].uid=0x4000000000000001; s.view_.devices[1].uid=0x4000000000000002;
  s.active_=true; s.view_.active=true; s.view_.phase=ofe_ble::READY; s.view_.token=77;
  s.worker_=reinterpret_cast<void*>(1);
  assert(!s.select(0,76) && !s.select(2,77));
  const int before=created_tasks;
  assert(s.select(0,77) && s.target_.uid==s.view_.devices[0].uid);
  assert(created_tasks==before && !s.select(1,77));
  s.finish(ofe_ble::DONE);
  assert(!s.active_ && !s.worker_ && s.view_.phase==ofe_ble::DONE);
  assert(s.view_.devices[0].paired && !s.view_.devices[1].paired);
  assert(s.view_.token!=77 && s.view_.pin==0 && s.config_.password[0]==0);
  assert(!s.select(1,77));
  assert(!s.select(0,s.view_.token));
  s.cancel_=true; // Authentication failure on a previous attempt must not poison the next.
  s.view_.phase=ofe_ble::FAILED;
  assert(s.select(1,s.view_.token));
  assert(created_tasks==before+1 && s.active_ && s.view_.active && !s.cancel_);
  assert(s.selected_ && s.view_.phase==ofe_ble::CONNECTING && s.target_.uid==s.view_.devices[1].uid);
  assert(!s.approved_ && !s.secured_ && !s.confirmation_ && !s.decision_pending_);
  s.finish(ofe_ble::FAILED); assert(!s.view_.devices[1].paired);
  task_success=false;
  assert(!s.select(1,s.view_.token));
  assert(s.view_.phase==ofe_ble::LOW_MEMORY && !s.worker_ && !s.active_ && !s.selected_);
  task_success=true;
  s.view_.phase=ofe_ble::DONE; s.cancel();
  assert(s.view_.phase==ofe_ble::CANCELLED && !s.select(1,s.view_.token));
  printf("BLE memory: Candidate=%zu B, six-entry View=%zu B, 6-to-16 cache delta=%zu B\n",
         sizeof(ofe_ble::Candidate),sizeof(ofe_ble::View),10*sizeof(ofe_ble::Candidate));
}
static void repeated_search() {
  now_ms=0; NimBLEDevice::scan.scans=0;
  MasterSearch s; s.provider_=[](uint64_t,ofe_wifi::Config& out) { out=config(); return true; };
  s.active_=true; s.view_.active=true; s.view_.token=50; s.worker_=reinterpret_cast<void*>(1);
  bool released_between_scans=false;
  delay_hook=[&]() { if(!NimBLEDevice::initialized) released_between_scans=true; };
  scan_hook=[&]() {
    if(NimBLEDevice::scan.scans!=2) return;
    uint8_t ad[15]={255,255,'O','F','E','1'};
    jbc_rs485::put_u64_le(ad+6,0x4000000000000042);
    NimBLEAdvertisedDevice d; d.manufacturer.assign(reinterpret_cast<const char*>(ad),sizeof(ad));
    s.discovered(&d); assert(s.view_.count==1);
    assert(s.select(0,s.view_.token)); // Select while a scan is running.
    assert(s.masterSelectionOrPhase(ofe_ble::READY) && s.view_.phase==ofe_ble::CONNECTING);
  };
  assert(s.startRadio()); s.runMaster(0);
  assert(s.pair_attempts==1 && NimBLEDevice::scan.scans==2 && released_between_scans);
  assert(s.view_.phase==ofe_ble::DONE && s.view_.devices[0].paired && !NimBLEDevice::initialized);
  scan_hook={}; delay_hook={};

  now_ms=0; MasterSearch timed; timed.provider_=s.provider_; timed.active_=true; timed.view_.active=true;
  assert(timed.startRadio()); timed.runMaster(0);
  assert(timed.view_.phase==ofe_ble::EXPIRED && now_ms==120000 && !NimBLEDevice::initialized);
  now_ms=0; MasterSearch cancelled; cancelled.provider_=s.provider_; cancelled.active_=true; cancelled.view_.active=true;
  delay_hook=[&]() { cancelled.cancel(); };
  assert(cancelled.startRadio()); cancelled.runMaster(0);
  assert(cancelled.view_.phase==ofe_ble::CANCELLED && !NimBLEDevice::initialized);
  delay_hook={}; now_ms=0;
  ofe_ble::View timer; timer.active=true; timer.phase=ofe_ble::READY; timer.started_ms=0xFFFFFFF0;
  assert(ofe_ble::searching(timer) && ofe_ble::searchElapsedMs(timer,16)==32);
  assert(ofe_ble::searchElapsedMs(timer,120100)==120000);
  timer.phase=ofe_ble::CONNECTING; assert(!ofe_ble::searching(timer) && ofe_ble::searchElapsedMs(timer,16)==0);

  MasterSearch late; late.provider_=s.provider_; late.active_=true; late.view_.active=true;
  late.view_.count=1; late.view_.devices[0].uid=0x400000000000002A;
  late.worker_=reinterpret_cast<void*>(1);
  scan_hook=[&]() {
    now_ms=119990; assert(late.select(0,late.view_.token));
    now_ms=120050; // The scan/cleanup completes after a timely selection.
  };
  assert(late.startRadio()); late.runMaster(0);
  assert(late.pair_started==120050 && late.view_.phase==ofe_ble::DONE);
  scan_hook={}; now_ms=0;
}
static void master_pair() {
  MasterPair s; s.view_.active=true; s.active_=true; s.view_.phase=ofe_ble::CONNECTING;
  s.target_.uid=0x400000000000002A; s.config_=config();
  NimBLEDevice::client=FakeClient{};
  now_ms=119990;
  secure_hook=[&]() {
    s.view_.phase=ofe_ble::CONFIRM; s.view_.token=77; s.view_.pin=123;
    s.confirmation_=NimBLEConnInfo{};
    assert(s.approve(77,123,true));
  };
  delay_hook=[&]() { s.authenticated(NimBLEConnInfo{}); };
  s.runMasterPair(now_ms);
  assert(s.view_.phase==ofe_ble::DONE && NimBLEDevice::client.service_calls==1);
  secure_hook={}; delay_hook={};

  MasterPair dropped; dropped.view_.active=true; dropped.active_=true;
  dropped.view_.phase=ofe_ble::CONNECTING;
  NimBLEDevice::client=FakeClient{};
  secure_hook=[]() { NimBLEDevice::client.connected=false; };
  dropped.runMasterPair(now_ms);
  assert(dropped.view_.phase==ofe_ble::FAILED); // An early drop is not a 120 s expiry.
  assert(NimBLEDevice::client.service_calls==0);
  secure_hook={};

  MasterPair rejected; rejected.view_.active=true; rejected.active_=true;
  rejected.view_.phase=ofe_ble::AUTHENTICATING;
  NimBLEDevice::client=FakeClient{};
  secure_hook=[&]() { rejected.fail(0x208); };
  rejected.runMasterPair(now_ms);
  assert(rejected.view_.phase==ofe_ble::FAILED && rejected.view_.error_code==0x208);
  assert(rejected.view_.error_phase==ofe_ble::AUTHENTICATING && NimBLEDevice::client.service_calls==0);
  secure_hook={};

  MasterPair timed; timed.view_.active=true; timed.active_=true;
  NimBLEDevice::client=FakeClient{};
  const uint32_t started=now_ms;
  timed.runMasterPair(started);
  assert(timed.view_.phase==ofe_ble::EXPIRED && uint32_t(now_ms-started)==120000);
  assert(NimBLEDevice::client.service_calls==0); // No config without authentication.
  now_ms=0;
}
int main() {
  protocol(); approval(); forget(); radio(); scan_store(); cached_selection(); repeated_search(); master_pair();
  puts("PASS: BLE protocol/security, bounded candidate copies, radio stop/restart, RAM guards, cleanup, rollover and asynchronous forget");
}
