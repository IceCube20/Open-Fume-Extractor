#pragma once
#include "bus/OfeDisplayTunnel.h"
#include <NimBLEDevice.h>
#if defined(CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL) || !CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#error "OFE Bluetooth requires the bundled build_opt.h and OfeBleBuildConfig.h (PSRAM allocator)."
#endif
static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS==1,"OFE Bluetooth pairing uses exactly one connection.");
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <optional>
#include <atomic>

namespace ofe_ble {
constexpr const char* SERVICE="b3a1f100-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* IDENTITY="b3a1f101-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* CONFIG="b3a1f102-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* RESULT="b3a1f103-8f54-4d28-b53b-9a1d0fe10001";
constexpr uint8_t MAX_DEVICES=6;
constexpr uint32_t SEARCH_TIMEOUT_MS=120000UL;
enum Phase:uint8_t { IDLE, STARTING, SCANNING, READY, ADVERTISING, CONNECTING,
  CONFIRM, AUTHENTICATING, SAVING, DONE, FAILED, CANCELLED, EXPIRED, LOW_MEMORY };
struct Candidate { uint64_t uid=0; char address[18]={}; uint8_t address_type=0,kind=0; int16_t rssi=0; bool paired=false; };
struct View {
  bool active=false; Phase phase=IDLE; uint32_t token=0,pin=0;
  uint8_t count=0; Candidate devices[MAX_DEVICES];
  uint32_t free_internal=0,largest_internal=0;
  uint8_t memory_stage=0; // 1 worker allocation, 2 before radio, 3 after radio, 4 radio stopped
  uint32_t started_ms=0;
  uint8_t error_phase=0;
  int error_code=0;
};
inline bool selectionReady(const View& v) {
  return (v.active && (v.phase==SCANNING || v.phase==READY)) ||
    (!v.active && (v.phase==DONE || v.phase==FAILED));
}
inline bool searching(const View& v) {
  return v.active && (v.phase==STARTING || v.phase==SCANNING || v.phase==READY);
}
inline uint32_t searchElapsedMs(const View& v,uint32_t now) {
  if (!searching(v)) return 0;
  const uint32_t elapsed=uint32_t(now-v.started_ms);
  return elapsed<SEARCH_TIMEOUT_MS ? elapsed : SEARCH_TIMEOUT_MS;
}
inline bool identity(uint64_t uid) { return (uid>>60)==4; }
inline bool parseAdvertisement(const uint8_t* data,size_t length,Candidate& out) {
  if (length!=15 || data[0]!=0xFF || data[1]!=0xFF || memcmp(data+2,"OFE1",4)) return false;
  const uint64_t uid=jbc_rs485::get_u64_le(data+6);
  if (!identity(uid) || data[14]>3) return false; // 3 = ELECROW P4 800x480.
  out.uid=uid; out.kind=data[14]; return true;
}
inline bool readConfig(const uint8_t* data,size_t length,uint64_t uid,ofe_wifi::Config& c) {
  if (length!=12+sizeof(c) || memcmp(data,"OPB1",4) || jbc_rs485::get_u64_le(data+4)!=uid) return false;
  memcpy(&c,data+12,sizeof(c));
  return ofe_wifi::configured(c) && c.from_master==1;
}
}
class OfeDisplayBle {
public:
  using Provider=bool(*)(uint64_t,ofe_wifi::Config&);
  using Save=bool(*)(const ofe_wifi::Config&);
  using Stored=uint8_t(*)(const ofe_wifi::Config&); // 0 pending, 1 verified, 2 failed
  void configure(uint64_t uid,uint8_t kind,Provider provider,Save save=nullptr,Stored stored=nullptr) {
    uid_=uid; kind_=kind; provider_=provider; save_=save; stored_=stored;
  }
  ofe_ble::View view() {
    portENTER_CRITICAL(&mux_); const auto value=view_; portEXIT_CRITICAL(&mux_); return value;
  }
  bool active() const { return active_.load(); }
  bool start() {
    portENTER_CRITICAL(&mux_);
    if (worker_) { portEXIT_CRITICAL(&mux_); return false; }
    view_=ofe_ble::View(); view_.active=true; view_.phase=ofe_ble::STARTING; view_.token=esp_random(); view_.started_ms=millis();
    active_.store(true); cancel_.store(false); failed_.store(false); selected_=false; approved_=false; secured_=false; received_=false;
    decision_pending_=false; confirmation_.reset();
    worker_=reinterpret_cast<TaskHandle_t>(1);
    portEXIT_CRITICAL(&mux_);
    const bool ok=xTaskCreatePinnedToCore(taskHook,"ofe-ble-pair",6144,this,1,&worker_,0)==pdPASS;
    if (!ok) {
      snapshotMemory(1);
      portENTER_CRITICAL(&mux_);
      worker_=nullptr; view_.active=false; view_.phase=ofe_ble::LOW_MEMORY; active_.store(false);
      portEXIT_CRITICAL(&mux_);
    }
    return ok;
  }
  void cancel() {
    portENTER_CRITICAL(&mux_);
    cancel_.store(true);
    if (!view_.active) { view_.phase=ofe_ble::CANCELLED; view_.token=esp_random(); }
    portEXIT_CRITICAL(&mux_);
  }
  bool select(uint8_t index,uint32_t token) {
    const auto current=view();
    if (!provider_ || !ofe_ble::selectionReady(current) || current.token!=token ||
        index>=current.count || current.devices[index].paired || (current.active && cancel_.load())) return false;
    ofe_wifi::Config config;
    if (!provider_(current.devices[index].uid,config) || !ofe_wifi::configured(config)) return false;
    portENTER_CRITICAL(&mux_);
    const bool resume=!worker_ && !view_.active;
    const bool ok=ofe_ble::selectionReady(view_) && view_.token==token && !selected_ &&
      index<view_.count && !view_.devices[index].paired && (!view_.active || !cancel_.load());
    if (ok) {
      target_=view_.devices[index]; config_=config; selected_=true;
      view_.phase=ofe_ble::CONNECTING;
      if (resume) {
        cancel_.store(false); failed_.store(false); view_.error_phase=0; view_.error_code=0;
        approved_=false; secured_=false; received_=false; decision_pending_=false; confirmation_.reset();
        view_.active=true; view_.pin=0; view_.token=esp_random(); active_.store(true);
        worker_=reinterpret_cast<TaskHandle_t>(1);
      }
    }
    portEXIT_CRITICAL(&mux_);
    if (!ok || !resume) return ok;
    // Start a fresh attempt from the fixed-size scan cache, not another scan.
    const bool started=xTaskCreatePinnedToCore(taskHook,"ofe-ble-pair",6144,this,1,&worker_,0)==pdPASS;
    if (!started) {
      snapshotMemory(1);
      portENTER_CRITICAL(&mux_);
      worker_=nullptr; selected_=false; view_.active=false; view_.phase=ofe_ble::LOW_MEMORY;
      memset(&config_,0,sizeof(config_)); active_.store(false);
      portEXIT_CRITICAL(&mux_);
    }
    return started;
  }
  bool approve(uint32_t token,uint32_t pin,bool accept) {
    portENTER_CRITICAL(&mux_);
    const bool ok=view_.active && view_.phase==ofe_ble::CONFIRM && view_.token==token && view_.pin==pin && confirmation_;
    if (ok) {
      approved_=accept; decision_pending_=true;
      view_.phase=accept ? ofe_ble::AUTHENTICATING : ofe_ble::CANCELLED;
    }
    portEXIT_CRITICAL(&mux_);
    if (!ok) return false;
    if (!accept) cancel_.store(true);
    return true;
  }
private:
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  ofe_ble::View view_;
  ofe_ble::Candidate target_;
  ofe_wifi::Config config_;
  uint64_t uid_=0;
  uint8_t kind_=0;
  Provider provider_=nullptr; Save save_=nullptr; Stored stored_=nullptr;
  TaskHandle_t worker_=nullptr;
  std::atomic<bool> active_{false},cancel_{false},failed_{false};
  bool selected_=false,approved_=false,secured_=false,received_=false,decision_pending_=false;
  std::optional<NimBLEConnInfo> confirmation_;
  NimBLECharacteristic* result_=nullptr;
  class ScanCallbacks:public NimBLEScanCallbacks {
  public: OfeDisplayBle* owner=nullptr;
    void onResult(const NimBLEAdvertisedDevice* device) override { owner->discovered(device); }
  } scan_callbacks_;
  class ServerCallbacks:public NimBLEServerCallbacks {
  public: OfeDisplayBle* owner=nullptr;
    void onConfirmPassKey(NimBLEConnInfo& peer,uint32_t pin) override { owner->confirm(peer,pin); }
    void onAuthenticationComplete(NimBLEConnInfo& peer) override { owner->authenticated(peer); }
    void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int) override {
      if (owner->view().phase!=ofe_ble::DONE) owner->cancel();
    }
  } server_callbacks_;
  class ClientCallbacks:public NimBLEClientCallbacks {
  public: OfeDisplayBle* owner=nullptr;
    void onConfirmPasskey(NimBLEConnInfo& peer,uint32_t pin) override { owner->confirm(peer,pin); }
    void onAuthenticationComplete(NimBLEConnInfo& peer) override { owner->authenticated(peer); }
    void onDisconnect(NimBLEClient*,int reason) override {
      const auto state=owner->view();
      if (state.active && !owner->cancel_.load() && state.phase>=ofe_ble::CONNECTING && state.phase<=ofe_ble::SAVING)
        owner->fail(reason);
    }
  } client_callbacks_;
  class ConfigCallbacks:public NimBLECharacteristicCallbacks {
  public: OfeDisplayBle* owner=nullptr;
    void onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& peer) override {
      owner->write(characteristic,peer);
    }
  } config_callbacks_;
  void discovered(const NimBLEAdvertisedDevice* device) {
    if (!device || !device->isAdvertisingService(NimBLEUUID(ofe_ble::SERVICE))) return;
    const auto manufacturer=device->getManufacturerData();
    ofe_ble::Candidate candidate;
    if (!ofe_ble::parseAdvertisement(reinterpret_cast<const uint8_t*>(manufacturer.data()),manufacturer.size(),candidate)) return;
    strlcpy(candidate.address,device->getAddress().toString().c_str(),sizeof(candidate.address));
    candidate.address_type=device->getAddress().getType(); candidate.rssi=device->getRSSI();
    portENTER_CRITICAL(&mux_);
    if (view_.phase==ofe_ble::SCANNING && view_.count<ofe_ble::MAX_DEVICES) {
      bool duplicate=false;
      for (uint8_t i=0;i<view_.count;++i) if (view_.devices[i].uid==candidate.uid) duplicate=true;
      if (!duplicate) view_.devices[view_.count++]=candidate;
    }
    portEXIT_CRITICAL(&mux_);
  }
  void phase(ofe_ble::Phase value) { portENTER_CRITICAL(&mux_); view_.phase=value; portEXIT_CRITICAL(&mux_); }
  void fail(int code) {
    portENTER_CRITICAL(&mux_);
    if (!failed_.load()) { view_.error_phase=view_.phase; view_.error_code=code; }
    failed_.store(true);
    portEXIT_CRITICAL(&mux_);
  }
  ofe_ble::Phase stoppedPhase(uint32_t started) const {
    if (cancel_.load()) return ofe_ble::CANCELLED;
    if (failed_.load()) return ofe_ble::FAILED;
    return uint32_t(millis()-started)>=120000UL ? ofe_ble::EXPIRED : ofe_ble::FAILED;
  }
  void confirm(NimBLEConnInfo& peer,uint32_t pin) {
    portENTER_CRITICAL(&mux_);
    confirmation_=peer; view_.pin=pin; view_.token=esp_random(); view_.phase=ofe_ble::CONFIRM;
    portEXIT_CRITICAL(&mux_);
  }
  void authenticated(const NimBLEConnInfo& peer) {
    portENTER_CRITICAL(&mux_);
    secured_=approved_ && peer.isEncrypted() && peer.isAuthenticated() && peer.getSecKeySize()==16;
    confirmation_.reset();
    const bool ok=secured_;
    portEXIT_CRITICAL(&mux_);
    if (!ok) fail(BLE_HS_EAUTHEN);
  }
  void write(NimBLECharacteristic* characteristic,const NimBLEConnInfo& peer) {
    const auto value=characteristic->getValue();
    ofe_wifi::Config next;
    portENTER_CRITICAL(&mux_); const bool authorized=secured_ && approved_ && !received_; portEXIT_CRITICAL(&mux_);
    uint8_t status=3;
    if (authorized && peer.isEncrypted() && peer.isAuthenticated() && peer.getSecKeySize()==16 &&
        !cancel_.load() && ofe_ble::readConfig(value.data(),value.size(),uid_,next) && save_ && save_(next)) {
      portENTER_CRITICAL(&mux_);
      config_=next; received_=true; view_.phase=ofe_ble::SAVING;
      portEXIT_CRITICAL(&mux_); status=1;
    }
    if (result_) result_->setValue(&status,1);
  }
  static void taskHook(void* context) {
    static_cast<OfeDisplayBle*>(context)->run();
    // Let C++ stack objects unwind before FreeRTOS releases the task stack.
    vTaskDelete(nullptr);
  }
  bool live(uint32_t started) {
    std::optional<NimBLEConnInfo> peer;
    bool accept=false;
    portENTER_CRITICAL(&mux_);
    if (decision_pending_) {
      peer=confirmation_; confirmation_.reset();
      accept=approved_; decision_pending_=false;
    }
    portEXIT_CRITICAL(&mux_);
    // All main-task decisions are delivered by this worker, never during deinit.
    if (peer && !NimBLEDevice::injectConfirmPasskey(*peer,accept)) fail(BLE_HS_EAUTHEN);
    return !cancel_.load() && !failed_.load() && uint32_t(millis()-started)<120000UL;
  }
  bool stopRadio(bool record_memory=true) {
    const bool ok=!NimBLEDevice::isInitialized() || NimBLEDevice::deinit(true);
    if (record_memory) snapshotMemory(4);
    return ok;
  }
  void finish(ofe_ble::Phase final) {
    phase(final);
    // Preserve the failing RAM snapshot, not the recovered heap after cleanup.
    if (!stopRadio(final!=ofe_ble::LOW_MEMORY)) final=ofe_ble::FAILED;
    portENTER_CRITICAL(&mux_);
    if (provider_ && final==ofe_ble::DONE) {
      for (uint8_t i=0;i<view_.count;++i) if (view_.devices[i].uid==target_.uid) view_.devices[i].paired=true;
    }
    confirmation_.reset(); result_=nullptr; worker_=nullptr;
    selected_=false; approved_=false; secured_=false; received_=false; decision_pending_=false;
    memset(&config_,0,sizeof(config_));
    view_.pin=0; view_.token=esp_random();
    view_.phase=final; view_.active=false; active_.store(false);
    portEXIT_CRITICAL(&mux_);
  }
  void snapshotMemory(uint8_t stage) {
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    const uint32_t free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
    portENTER_CRITICAL(&mux_);
    view_.free_internal=free; view_.largest_internal=largest; view_.memory_stage=stage;
    portEXIT_CRITICAL(&mux_);
  }
  bool startRadio() {
    using namespace ofe_ble;
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    snapshotMemory(2);
    if (heap_caps_get_free_size(caps)<56U*1024U || heap_caps_get_largest_free_block(caps)<16U*1024U) { finish(LOW_MEMORY); return false; }
    // Only one peer is paired; do not reserve the default 100-entry controller cache.
    NimBLEDevice::setScanDuplicateCacheSize(20);
    if (!NimBLEDevice::init(provider_ ? "OFEMaster" : "OFEDisplay")) { finish(FAILED); return false; }
    snapshotMemory(3);
    if (heap_caps_get_free_size(caps)<20U*1024U) { finish(LOW_MEMORY); return false; }
    NimBLEDevice::setSecurityAuth(false,true,true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    NimBLEDevice::setMTU(247);
    return true;
  }
  void run() {
    const uint32_t started=millis();
    scan_callbacks_.owner=this; server_callbacks_.owner=this;
    client_callbacks_.owner=this; config_callbacks_.owner=this;
    if (!startRadio()) return;
    if (provider_) {
      portENTER_CRITICAL(&mux_); const bool resumed=selected_; portEXIT_CRITICAL(&mux_);
      if (resumed) runMasterPair(started);
      else runMaster(started);
    } else runDisplay(started);
  }
  void runMaster(uint32_t started) {
    using namespace ofe_ble;
    bool selected=false;
    while (live(started)) {
      selected=masterSelectionOrPhase(SCANNING);
      if (selected) break;
      // The first scan uses run()'s radio. Later cycles restart only for a short scan.
      if (!NimBLEDevice::isInitialized() && !startRadio()) return;
      {
        auto* scan=NimBLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setScanCallbacks(&scan_callbacks_,false);
        // Process advertisements immediately; retain only bounded candidate copies.
        scan->setMaxResults(0);
        const uint32_t elapsed=uint32_t(millis()-started);
        if (elapsed<120000UL) scan->getResults((120000UL-elapsed)<4000UL ? 120000UL-elapsed : 4000UL);
        scan->clearResults();
      }
      if (!stopRadio()) { finish(FAILED); return; }
      selected=masterSelectionOrPhase(READY);
      if (selected) break;
      if (!live(started)) break;
      // Release the controller between scans. Late displays join the same list.
      const uint32_t stopped_at=millis();
      while (live(started) && uint32_t(millis()-stopped_at)<3000UL) {
        portENTER_CRITICAL(&mux_); selected=selected_; portEXIT_CRITICAL(&mux_);
        if (selected) break;
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      if (selected) break;
    }
    if (!selected) { finish(stoppedPhase(started)); return; }
    if (!stopRadio()) { finish(FAILED); return; }
    if (!startRadio()) return;
    // Selecting a display starts a new bounded attempt; scanning must not consume
    // the time available to compare and confirm the code on both devices.
    runMasterPair(millis());
  }
  bool masterSelectionOrPhase(ofe_ble::Phase next) {
    portENTER_CRITICAL(&mux_);
    const bool selected=selected_;
    if (!selected) view_.phase=next;
    portEXIT_CRITICAL(&mux_);
    return selected;
  }
  void runMasterPair(uint32_t started) {
    using namespace ofe_ble;
    if (!live(started)) { finish(stoppedPhase(started)); return; }
    auto* client=NimBLEDevice::createClient();
    if (!client) { finish(LOW_MEMORY); return; }
    client->setClientCallbacks(&client_callbacks_,false); client->setConnectTimeout(5000);
    if (!client->connect(NimBLEAddress(target_.address,target_.address_type)) || !client->secureConnection(true)) {
      fail(client->getLastError()); finish(FAILED); return;
    }
    bool secured=false;
    while (live(started) && client->isConnected()) {
      portENTER_CRITICAL(&mux_); secured=secured_; portEXIT_CRITICAL(&mux_);
      if (secured) break;
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!secured || !live(started)) { finish(stoppedPhase(started)); return; }
    auto* service=client->getService(SERVICE);
    auto* identity=service ? service->getCharacteristic(IDENTITY) : nullptr;
    auto* config=service ? service->getCharacteristic(CONFIG) : nullptr;
    auto* result=service ? service->getCharacteristic(RESULT) : nullptr;
    const auto id=identity ? identity->readValue() : NimBLEAttValue();
    if (!identity || id.size()!=8 || jbc_rs485::get_u64_le(id.data())!=target_.uid || !config || !result) { finish(FAILED); return; }
    phase(SAVING);
    uint8_t bytes[12+sizeof(ofe_wifi::Config)];
    memcpy(bytes,"OPB1",4); jbc_rs485::put_u64_le(bytes+4,target_.uid); memcpy(bytes+12,&config_,sizeof(config_));
    if (!config->writeValue(bytes,sizeof(bytes),true)) { memset(bytes,0,sizeof(bytes)); finish(FAILED); return; }
    memset(bytes,0,sizeof(bytes));
    const uint32_t saved_at=millis();
    while (live(started) && uint32_t(millis()-saved_at)<5000UL && client->isConnected()) {
      auto status=result->readValue();
      if (status.size()==1 && status[0]==2) { finish(DONE); return; }
      if (status.size()==1 && status[0]==3) { finish(FAILED); return; }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    finish(FAILED);
  }
  void runDisplay(uint32_t started) {
    using namespace ofe_ble;
    auto* server=NimBLEDevice::createServer();
    if (!server) { finish(LOW_MEMORY); return; }
    server->setCallbacks(&server_callbacks_,false);
    auto* service=server->createService(SERVICE);
    if (!service) { finish(LOW_MEMORY); return; }
    auto* identity=service->createCharacteristic(IDENTITY,NIMBLE_PROPERTY::READ,8);
    if (!identity) { finish(LOW_MEMORY); return; }
    uint8_t uid[8]; jbc_rs485::put_u64_le(uid,uid_); identity->setValue(uid,sizeof(uid));
    auto* config=service->createCharacteristic(CONFIG,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC|NIMBLE_PROPERTY::WRITE_AUTHEN,12+sizeof(ofe_wifi::Config));
    if (!config) { finish(LOW_MEMORY); return; }
    config->setCallbacks(&config_callbacks_);
    result_=service->createCharacteristic(RESULT,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC|NIMBLE_PROPERTY::READ_AUTHEN,1);
    if (!result_) { finish(LOW_MEMORY); return; }
    uint8_t status=0; result_->setValue(&status,1);
    uint8_t manufacturer[15]={0xFF,0xFF,'O','F','E','1'};
    jbc_rs485::put_u64_le(manufacturer+6,uid_); manufacturer[14]=kind_;
    auto* advertising=NimBLEDevice::getAdvertising();
    advertising->enableScanResponse(true);
    if (!advertising->addServiceUUID(SERVICE)) { finish(FAILED); return; }
    NimBLEAdvertisementData response;
    if (!response.setName("OFEDisplay") || !response.setManufacturerData(manufacturer,sizeof(manufacturer)) ||
        !advertising->setScanResponseData(response)) { finish(FAILED); return; }
    if (!advertising->start()) { finish(FAILED); return; }
    phase(ADVERTISING);
    while (live(started)) {
      portENTER_CRITICAL(&mux_); const bool received=received_; portEXIT_CRITICAL(&mux_);
      if (received && stored_) {
        const auto saved=stored_(config_);
        if (saved) {
          status=saved==1 ? 2 : 3; result_->setValue(&status,1);
          phase(saved==1 ? DONE : FAILED);
          vTaskDelay(pdMS_TO_TICKS(2000));
          finish(saved==1 ? DONE : FAILED); return;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    finish(stoppedPhase(started));
  }
};
