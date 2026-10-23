#pragma once
#include "OfeDisplayTunnel.h"
#include "../OfeP4Board.h"
#include "OfeP4BleEccFix.h"
#include <esp32-hal-hosted.h>
#include <esp_heap_caps.h>
#include <atomic>
extern "C" {
#include <nimble/nimble_port.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_sm.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
}

#if !defined(CONFIG_BT_NIMBLE_ENABLED) || !defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
#error "P4 pairing requires the stock ESP-Hosted NimBLE SDK"
#endif

namespace ofe_ble {
constexpr const char* SERVICE="b3a1f100-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* IDENTITY="b3a1f101-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* CONFIG="b3a1f102-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* RESULT="b3a1f103-8f54-4d28-b53b-9a1d0fe10001";
enum Phase:uint8_t { IDLE, STARTING, SCANNING, READY, ADVERTISING, CONNECTING,
  CONFIRM, AUTHENTICATING, SAVING, DONE, FAILED, CANCELLED, EXPIRED, LOW_MEMORY };
struct Candidate { uint64_t uid=0; char address[18]={}; uint8_t address_type=0,kind=0; int16_t rssi=0; };
struct View {
  bool active=false; Phase phase=IDLE; uint32_t token=0,pin=0;
  uint8_t count=0; Candidate devices[6];
  uint32_t free_internal=0,largest_internal=0; uint8_t memory_stage=0;
  uint8_t error_phase=0;
  int error_code=0;
};
inline bool readConfig(const uint8_t* data,size_t length,uint64_t uid,ofe_wifi::Config& c) {
  if (length!=12+sizeof(c) || memcmp(data,"OPB1",4) || jbc_rs485::get_u64_le(data+4)!=uid) return false;
  memcpy(&c,data+12,sizeof(c));
  return ofe_wifi::configured(c) && c.from_master==1;
}
}

// Peripheral-only implementation. The existing S3 Master remains the central.
// Use SDK NimBLE, not NimBLE-Arduino's local-controller initialization on the P4.
class OfeDisplayBle {
public:
  using Provider=bool(*)(uint64_t,ofe_wifi::Config&);
  using Save=bool(*)(const ofe_wifi::Config&);
  using Stored=uint8_t(*)(const ofe_wifi::Config&);
  void configure(uint64_t uid,uint8_t kind,Provider provider,Save save=nullptr,Stored stored=nullptr) {
    uid_=uid; kind_=kind; save_=save; stored_=stored; peripheral_=provider==nullptr;
  }
  bool active() const { return active_.load(); }
  ofe_ble::View view() {
    portENTER_CRITICAL(&mux_); const auto copy=view_; portEXIT_CRITICAL(&mux_); return copy;
  }
  bool start() {
    portENTER_CRITICAL(&mux_);
    if (active_.load() || poisoned_ || !peripheral_ || !save_ || !stored_ || (uid_>>60)!=4) {
      portEXIT_CRITICAL(&mux_); return false;
    }
    view_={}; view_.active=true; view_.phase=ofe_ble::STARTING; view_.token=esp_random();
    active_=true; cancel_=false; failed_=false; approved_=false; pending_=false;
    received_=false; secured_=false; result_=0; conn_=BLE_HS_CONN_HANDLE_NONE;
    portEXIT_CRITICAL(&mux_);
    TaskHandle_t worker=nullptr;
    if (xTaskCreatePinnedToCore(workerHook,"p4-ble-pair",6144,this,1,&worker,0)==pdPASS) return true;
    finish(ofe_ble::LOW_MEMORY); return false;
  }
  void cancel() { cancel_=true; }
  bool approve(uint32_t token,uint32_t pin,bool accept) {
    portENTER_CRITICAL(&mux_);
    const bool ok=view_.active && view_.phase==ofe_ble::CONFIRM && view_.token==token && view_.pin==pin;
    if (ok) { approved_=accept; pending_=true; view_.phase=ofe_ble::AUTHENTICATING; }
    portEXIT_CRITICAL(&mux_);
    return ok;
  }
private:
  inline static OfeDisplayBle* owner_=nullptr;
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  ofe_ble::View view_;
  ofe_wifi::Config config_{};
  uint64_t uid_=0; uint8_t kind_=0;
  Save save_=nullptr; Stored stored_=nullptr;
  bool peripheral_=true,poisoned_=false,host_initialized_=false,host_started_=false,hosted_started_=false;
  bool approved_=false,pending_=false;
  std::atomic<bool> active_{false},cancel_{false},failed_{false},synced_{false},host_exited_{false},received_{false},secured_{false};
  std::atomic<uint32_t> attempt_started_{0};
  std::atomic<uint16_t> conn_{BLE_HS_CONN_HANDLE_NONE};
  std::atomic<uint8_t> result_{0};
  ble_uuid_any_t uuids_[4]{};
  ble_gatt_chr_def chars_[4]{};
  ble_gatt_svc_def services_[2]{};

  void phase(ofe_ble::Phase p) {
    portENTER_CRITICAL(&mux_); view_.phase=p; portEXIT_CRITICAL(&mux_);
  }
  void fail(int code) {
    portENTER_CRITICAL(&mux_);
    if (!failed_.load()) { view_.error_phase=view_.phase; view_.error_code=code; }
    failed_=true;
    portEXIT_CRITICAL(&mux_);
  }
  void memory(uint8_t stage) {
    const uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    const auto free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
    portENTER_CRITICAL(&mux_);
    view_.memory_stage=stage; view_.free_internal=free; view_.largest_internal=largest;
    portEXIT_CRITICAL(&mux_);
  }
  bool authorized(uint16_t handle) {
    ble_gap_conn_desc peer{};
    return handle==conn_.load() && secured_.load() && !cancel_.load() && !failed_.load() &&
      ble_gap_conn_find(handle,&peer)==0 && peer.sec_state.encrypted &&
      peer.sec_state.authenticated && peer.sec_state.key_size==16;
  }
  static int access(uint16_t handle,uint16_t,ble_gatt_access_ctxt* ctx,void* arg) {
    auto* self=static_cast<OfeDisplayBle*>(arg);
    if (ble_uuid_cmp(ctx->chr->uuid,&self->uuids_[1].u)==0 && ctx->op==BLE_GATT_ACCESS_OP_READ_CHR) {
      uint8_t uid[8]; jbc_rs485::put_u64_le(uid,self->uid_);
      return os_mbuf_append(ctx->om,uid,sizeof(uid))==0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (!self->authorized(handle)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ble_uuid_cmp(ctx->chr->uuid,&self->uuids_[3].u)==0 && ctx->op==BLE_GATT_ACCESS_OP_READ_CHR) {
      const uint8_t status=self->result_.load();
      return os_mbuf_append(ctx->om,&status,1)==0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctx->chr->uuid,&self->uuids_[2].u)!=0 || ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)
      return BLE_ATT_ERR_UNLIKELY;
    if (self->received_.load()) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t bytes[12+sizeof(ofe_wifi::Config)]; uint16_t copied=0;
    ofe_wifi::Config next{};
    const bool valid=OS_MBUF_PKTLEN(ctx->om)==sizeof(bytes) &&
      ble_hs_mbuf_to_flat(ctx->om,bytes,sizeof(bytes),&copied)==0 && copied==sizeof(bytes) &&
      ofe_ble::readConfig(bytes,sizeof(bytes),self->uid_,next);
    memset(bytes,0,sizeof(bytes));
    if (!valid || !self->save_ || !self->save_(next)) {
      memset(&next,0,sizeof(next)); self->result_=3; return BLE_ATT_ERR_UNLIKELY;
    }
    self->config_=next; memset(&next,0,sizeof(next));
    self->result_=1; self->received_=true; self->phase(ofe_ble::SAVING);
    return 0;
  }
  static int gap(ble_gap_event* event,void* arg) {
    auto* self=static_cast<OfeDisplayBle*>(arg);
    switch (event->type) {
      case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) { self->fail(event->connect.status); break; }
        if (self->conn_.load()!=BLE_HS_CONN_HANDLE_NONE) {
          ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM); break;
        }
        self->attempt_started_=millis();
        self->conn_=event->connect.conn_handle; self->phase(ofe_ble::CONNECTING); break;
      case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.conn_handle!=self->conn_.load() || event->passkey.params.action!=BLE_SM_IOACT_NUMCMP) {
          self->fail(BLE_HS_EINVAL); break;
        }
        portENTER_CRITICAL(&self->mux_);
        self->view_.pin=event->passkey.params.numcmp; self->view_.token=esp_random();
        self->approved_=false; self->pending_=false; self->view_.phase=ofe_ble::CONFIRM;
        portEXIT_CRITICAL(&self->mux_); break;
      case BLE_GAP_EVENT_ENC_CHANGE: {
        ble_gap_conn_desc peer{};
        portENTER_CRITICAL(&self->mux_); const bool approved=self->approved_; portEXIT_CRITICAL(&self->mux_);
        const bool ok=event->enc_change.conn_handle==self->conn_.load() && event->enc_change.status==0 &&
          approved && ble_gap_conn_find(self->conn_.load(),&peer)==0 && peer.sec_state.encrypted &&
          peer.sec_state.authenticated && peer.sec_state.key_size==16;
        self->secured_=ok;
        if (!ok) self->fail(event->enc_change.status ? event->enc_change.status : BLE_HS_EAUTHEN);
        break;
      }
      case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle==self->conn_.load()) {
          self->conn_=BLE_HS_CONN_HANDLE_NONE; self->secured_=false;
          if (self->result_.load()!=2 && !self->cancel_.load()) self->fail(event->disconnect.reason);
        }
        break;
      default: break;
    }
    return 0;
  }
  static void sync() {
    if (owner_) owner_->synced_=true;
  }
  static void reset(int reason) {
    if (owner_) { owner_->secured_=false; owner_->fail(reason); }
  }
  static void hostHook(void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    nimble_port_run();
    self->host_exited_=true;
    vTaskDelete(nullptr);
  }
  static void workerHook(void* ptr) {
    static_cast<OfeDisplayBle*>(ptr)->run();
    vTaskDelete(nullptr);
  }
  bool initRadio() {
    memory(2);
    const auto caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(caps)<56U*1024U || heap_caps_get_largest_free_block(caps)<16U*1024U) {
      finish(ofe_ble::LOW_MEMORY); return false;
    }
    if (!ofe_p4::hostedPinsReady() || !hostedInitBLE()) { finish(ofe_ble::FAILED); return false; }
    hosted_started_=true;
    if (nimble_port_init()!=ESP_OK) { finish(ofe_ble::FAILED); return false; }
    host_initialized_=true; owner_=this; synced_=false; host_exited_=false;
    const int ecc_test=ofe_p4_ble_ecc_self_test();
    if (ecc_test) { fail(ecc_test); finish(ofe_ble::FAILED); return false; }
    ble_hs_cfg.sync_cb=sync; ble_hs_cfg.reset_cb=reset;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding=0; ble_hs_cfg.sm_mitm=1; ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=0; ble_hs_cfg.sm_their_key_dist=0;
    ble_svc_gap_init(); ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set("OFEDisplay") || ble_att_set_preferred_mtu(247)) {
      finish(ofe_ble::FAILED); return false;
    }
    const char* ids[]={ofe_ble::SERVICE,ofe_ble::IDENTITY,ofe_ble::CONFIG,ofe_ble::RESULT};
    for (unsigned i=0;i<4;++i) if (ble_uuid_from_str(&uuids_[i],ids[i])) { finish(ofe_ble::FAILED); return false; }
    memset(chars_,0,sizeof(chars_)); memset(services_,0,sizeof(services_));
    for (unsigned i=0;i<3;++i) {
      chars_[i].uuid=&uuids_[i+1].u; chars_[i].access_cb=access; chars_[i].arg=this;
      chars_[i].flags=i==1 ? BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN : BLE_GATT_CHR_F_READ;
      if (i==2) chars_[i].flags|=BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN;
      if (i) chars_[i].min_key_size=16;
    }
    services_[0].type=BLE_GATT_SVC_TYPE_PRIMARY; services_[0].uuid=&uuids_[0].u;
    services_[0].characteristics=chars_;
    if (ble_gatts_count_cfg(services_) || ble_gatts_add_svcs(services_)) { finish(ofe_ble::FAILED); return false; }
    TaskHandle_t host=nullptr;
    if (xTaskCreatePinnedToCore(hostHook,"p4-nimble",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,this,5,&host,0)!=pdPASS) {
      finish(ofe_ble::LOW_MEMORY); return false;
    }
    host_started_=true;
    const uint32_t started=millis();
    while (!synced_.load() && !cancel_.load() && uint32_t(millis()-started)<10000UL) vTaskDelay(pdMS_TO_TICKS(10));
    if (!synced_.load() || cancel_.load()) { finish(ofe_ble::FAILED); return false; }
    memory(3);
    if (heap_caps_get_free_size(caps)<20U*1024U) { finish(ofe_ble::LOW_MEMORY); return false; }
    return true;
  }
  bool advertise() {
    uint8_t own_type=0;
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0,&own_type)) return false;
    ble_hs_adv_fields adv{};
    adv.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128=&uuids_[0].u128; adv.num_uuids128=1; adv.uuids128_is_complete=1;
    if (ble_gap_adv_set_fields(&adv)) return false;
    uint8_t manufacturer[15]={0xFF,0xFF,'O','F','E','1'};
    jbc_rs485::put_u64_le(manufacturer+6,uid_); manufacturer[14]=kind_;
    ble_hs_adv_fields response{};
    response.name=reinterpret_cast<const uint8_t*>("OFEDisplay"); response.name_len=10; response.name_is_complete=1;
    response.mfg_data=manufacturer; response.mfg_data_len=sizeof(manufacturer);
    if (ble_gap_adv_rsp_set_fields(&response)) return false;
    ble_gap_adv_params params{}; params.conn_mode=BLE_GAP_CONN_MODE_UND; params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(own_type,nullptr,BLE_HS_FOREVER,&params,gap,this)==0;
  }
  void finish(ofe_ble::Phase final) {
    bool stopped=true;
    if (host_started_) {
      ble_gap_adv_stop();
      const auto handle=conn_.load();
      if (handle!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
      stopped=nimble_port_stop()==ESP_OK;
      const uint32_t at=millis();
      while (stopped && !host_exited_.load() && uint32_t(millis()-at)<1000UL) vTaskDelay(pdMS_TO_TICKS(10));
      stopped=stopped && host_exited_.load();
    }
    if (stopped && host_initialized_) stopped=nimble_port_deinit()==ESP_OK;
    if (stopped && hosted_started_) stopped=hostedDeinitBLE(); // WiFi keeps its own shared SDIO reference.
    if (!stopped) { poisoned_=true; final=ofe_ble::FAILED; }
    else { host_initialized_=false; host_started_=false; hosted_started_=false; owner_=nullptr; }
    if (final!=ofe_ble::LOW_MEMORY) memory(4);
    if (stopped) memset(&config_,0,sizeof(config_));
    portENTER_CRITICAL(&mux_);
    view_.phase=final; view_.active=false; active_=false; pending_=false; approved_=false;
    portEXIT_CRITICAL(&mux_);
  }
  void deliverDecision() {
    bool decision=false,accept=false;
    portENTER_CRITICAL(&mux_);
    if (pending_) { decision=true; accept=approved_; pending_=false; }
    portEXIT_CRITICAL(&mux_);
    if (decision) {
      ble_sm_io input{}; input.action=BLE_SM_IOACT_NUMCMP; input.numcmp_accept=accept;
      const int rc=ble_sm_inject_io(conn_.load(),&input);
      if (rc) fail(rc);
      if (!accept) cancel_=true;
    }
  }
  void run() {
    if (!initRadio()) return;
    attempt_started_=millis();
    phase(ofe_ble::ADVERTISING);
    if (!advertise()) { finish(ofe_ble::FAILED); return; }
    while (!cancel_.load() && !failed_.load() && uint32_t(millis()-attempt_started_.load())<120000UL) {
      deliverDecision();
      if (cancel_.load() || failed_.load()) continue;
      if (received_.load()) {
        const uint8_t saved=stored_(config_);
        if (saved) {
          result_=saved==1 ? 2 : 3; phase(saved==1 ? ofe_ble::DONE : ofe_ble::FAILED);
          // Give the Master time to read verified persistence before tearing down BLE.
          vTaskDelay(pdMS_TO_TICKS(2000));
          finish(saved==1 ? ofe_ble::DONE : ofe_ble::FAILED); return;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(25));
    }
    finish(cancel_.load() ? ofe_ble::CANCELLED : failed_.load() ? ofe_ble::FAILED : ofe_ble::EXPIRED);
  }
};
