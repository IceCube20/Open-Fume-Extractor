#pragma once
#include "bus/OfeDisplayTunnel.h"
#include "p4/OfeBleProtocol.h"
#include "p4/OfeP4BleEccFix.h"
#include <esp32-hal-hosted.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
#if !CONFIG_BT_NIMBLE_ENABLED || !CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE || !CONFIG_BT_NIMBLE_ROLE_CENTRAL
#error "P4 MCU requires SDK NimBLE central support over ESP-Hosted."
#endif

// SDK NimBLE central: NimBLE-Arduino initializes a local controller, absent on P4.
class OfeDisplayBle {
public:
  using Provider=bool(*)(uint64_t,ofe_wifi::Config&);
  using Save=bool(*)(const ofe_wifi::Config&);
  using Stored=uint8_t(*)(const ofe_wifi::Config&);
  void configure(uint64_t,uint8_t,Provider provider,Save=nullptr,Stored=nullptr) { provider_=provider; }
  bool active() const { return active_.load(); }
  ofe_ble::View view() {
    portENTER_CRITICAL(&mux_); const auto copy=view_; portEXIT_CRITICAL(&mux_); return copy;
  }
  bool start() {
    portENTER_CRITICAL(&mux_);
    if (active_ || poisoned_ || !provider_) { portEXIT_CRITICAL(&mux_); return false; }
    view_={}; view_.started_ms=millis(); resetAttempt(); selected_=false;
    portEXIT_CRITICAL(&mux_);
    return launch();
  }
  void cancel() { cancel_=true; }
  bool select(uint8_t index,uint32_t token) {
    const auto current=view();
    if (!provider_ || !ofe_ble::selectionReady(current) || token!=current.token ||
        index>=current.count || current.devices[index].paired) return false;
    ofe_wifi::Config config{};
    if (!provider_(current.devices[index].uid,config) || !ofe_wifi::configured(config)) {
      memset(&config,0,sizeof(config)); return false;
    }
    portENTER_CRITICAL(&mux_);
    const bool ok=!poisoned_ && !selected_ && ofe_ble::selectionReady(view_) &&
      token==view_.token && index<view_.count && !view_.devices[index].paired && !cancel_;
    const bool resume=ok && !active_;
    if (ok) {
      if (resume) resetAttempt();
      target_=view_.devices[index]; config_=config; selected_=true; view_.phase=ofe_ble::CONNECTING;
    }
    portEXIT_CRITICAL(&mux_);
    memset(&config,0,sizeof(config));
    return ok && (!resume || launch());
  }
  bool approve(uint32_t token,uint32_t pin,bool accept) {
    portENTER_CRITICAL(&mux_);
    const bool ok=view_.active && view_.phase==ofe_ble::CONFIRM &&
      token==view_.token && pin==view_.pin && !cancel_;
    if (ok) { approved_=accept; pending_=true; view_.phase=ofe_ble::AUTHENTICATING; }
    portEXIT_CRITICAL(&mux_); return ok;
  }
private:
  inline static OfeDisplayBle* owner_=nullptr;
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  ofe_ble::View view_{};
  ofe_ble::Candidate target_{};
  ofe_wifi::Config config_{};
  Provider provider_=nullptr;
  bool poisoned_=false,host_initialized_=false,host_started_=false,hosted_started_=false;
  bool approved_=false,pending_=false;
  std::atomic<bool> active_{false},cancel_{false},failed_{false},synced_{false},host_exited_{false};
  std::atomic<bool> selected_{false},scan_done_{false},secured_{false},operation_done_{false};
  std::atomic<int> operation_status_{0};
  std::atomic<uint16_t> conn_{BLE_HS_CONN_HANDLE_NONE};
  uint8_t own_type_=0;
  ble_uuid_any_t uuids_[4]{};
  uint16_t service_start_=0,service_end_=0,handles_[3]{};
  uint8_t read_bytes_[8]{};
  uint16_t read_length_=0;

  void resetAttempt() { // Caller holds mux_.
    active_=true; cancel_=false; failed_=false; secured_=false; approved_=false; pending_=false;
    view_.active=true; view_.phase=ofe_ble::STARTING; view_.token=esp_random();
    view_.pin=0; view_.error_phase=0; view_.error_code=0;
    conn_=BLE_HS_CONN_HANDLE_NONE; service_start_=service_end_=0;
    memset(handles_,0,sizeof(handles_));
  }
  bool launch() {
    TaskHandle_t worker=nullptr;
    if (xTaskCreatePinnedToCore(workerHook,"p4-ble-master",8192,this,1,&worker,0)==pdPASS) return true;
    finish(ofe_ble::LOW_MEMORY); return false;
  }
  void phase(ofe_ble::Phase value) {
    portENTER_CRITICAL(&mux_); view_.phase=value; portEXIT_CRITICAL(&mux_);
  }
  void fail(int code) {
    portENTER_CRITICAL(&mux_);
    if (!failed_) { view_.error_phase=view_.phase; view_.error_code=code; }
    failed_=true; portEXIT_CRITICAL(&mux_);
  }
  void memory(uint8_t stage) {
    const auto caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    const uint32_t free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
    portENTER_CRITICAL(&mux_);
    view_.memory_stage=stage; view_.free_internal=free; view_.largest_internal=largest;
    portEXIT_CRITICAL(&mux_);
  }
  bool authorized() {
    ble_gap_conn_desc peer{};
    portENTER_CRITICAL(&mux_); const bool approved=approved_; portEXIT_CRITICAL(&mux_);
    return approved && secured_ && !cancel_ && !failed_ &&
      ble_gap_conn_find(conn_,&peer)==0 && peer.sec_state.encrypted &&
      peer.sec_state.authenticated && peer.sec_state.key_size==16;
  }
  static void sync() { if (owner_) owner_->synced_=true; }
  static void reset(int reason) { if (owner_) owner_->fail(reason); }
  static void hostHook(void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    nimble_port_run(); self->host_exited_=true; vTaskDelete(nullptr);
  }
  static void workerHook(void* ptr) { static_cast<OfeDisplayBle*>(ptr)->run(); vTaskDelete(nullptr); }
  static int gap(ble_gap_event* event,void* arg) {
    auto* self=static_cast<OfeDisplayBle*>(arg);
    switch (event->type) {
      case BLE_GAP_EVENT_DISC: {
        ble_hs_adv_fields fields{}; ofe_ble::Candidate candidate{};
        if (ble_hs_adv_parse_fields(&fields,event->disc.data,event->disc.length_data) ||
            !fields.mfg_data || !ofe_ble::parseAdvertisement(fields.mfg_data,fields.mfg_data_len,candidate)) break;
        const auto& address=event->disc.addr;
        candidate.address_type=address.type; candidate.rssi=event->disc.rssi;
        snprintf(candidate.address,sizeof(candidate.address),"%02X:%02X:%02X:%02X:%02X:%02X",
          address.val[5],address.val[4],address.val[3],address.val[2],address.val[1],address.val[0]);
        portENTER_CRITICAL(&self->mux_);
        if (!self->selected_ && ofe_ble::searching(self->view_)) {
          uint8_t index=0;
          while (index<self->view_.count && self->view_.devices[index].uid!=candidate.uid) ++index;
          if (index<ofe_ble::MAX_DEVICES) {
            if (index==self->view_.count) ++self->view_.count;
            candidate.paired=self->view_.devices[index].paired;
            self->view_.devices[index]=candidate; self->view_.phase=ofe_ble::READY;
          }
        }
        portEXIT_CRITICAL(&self->mux_); break;
      }
      case BLE_GAP_EVENT_DISC_COMPLETE: self->scan_done_=true; break;
      case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) self->fail(event->connect.status);
        else self->conn_=event->connect.conn_handle;
        break;
      case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.conn_handle!=self->conn_ || event->passkey.params.action!=BLE_SM_IOACT_NUMCMP) {
          self->fail(BLE_HS_EAUTHEN); break;
        }
        portENTER_CRITICAL(&self->mux_);
        self->view_.pin=event->passkey.params.numcmp; self->view_.token=esp_random();
        self->view_.phase=ofe_ble::CONFIRM; self->approved_=false; self->pending_=false;
        portEXIT_CRITICAL(&self->mux_); break;
      case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.conn_handle==self->conn_ && !event->enc_change.status) {
          self->secured_=true;
          if (!self->authorized()) self->fail(BLE_HS_EAUTHEN);
        } else self->fail(event->enc_change.status ? event->enc_change.status : BLE_HS_EAUTHEN);
        break;
      case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle==self->conn_) {
          self->conn_=BLE_HS_CONN_HANDLE_NONE; self->secured_=false;
          if (!self->cancel_ && self->view().phase!=ofe_ble::DONE) self->fail(event->disconnect.reason);
        }
        break;
      default: break;
    }
    return 0;
  }
  void deliverDecision() {
    bool pending=false,accept=false;
    portENTER_CRITICAL(&mux_);
    if (pending_) { pending=true; accept=approved_; pending_=false; }
    portEXIT_CRITICAL(&mux_);
    if (pending) {
      ble_sm_io input{}; input.action=BLE_SM_IOACT_NUMCMP; input.numcmp_accept=accept;
      const int rc=ble_sm_inject_io(conn_,&input);
      if (rc) fail(rc);
      if (!accept) cancel_=true;
    }
  }
  bool initRadio() {
    memory(2);
    const auto caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(caps)<56U*1024U || heap_caps_get_largest_free_block(caps)<16U*1024U) {
      finish(ofe_ble::LOW_MEMORY); return false;
    }
    if (!hostedInitBLE()) { fail(-1101); finish(ofe_ble::FAILED); return false; }
    hosted_started_=true;
    if (nimble_port_init()!=ESP_OK) { fail(-1102); finish(ofe_ble::FAILED); return false; }
    host_initialized_=true; owner_=this; synced_=false; host_exited_=false;
    const int ecc=ofe_p4_ble_ecc_self_test();
    if (ecc) { fail(ecc); finish(ofe_ble::FAILED); return false; }
    ble_hs_cfg.sync_cb=sync; ble_hs_cfg.reset_cb=reset;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding=0; ble_hs_cfg.sm_mitm=1; ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=0; ble_hs_cfg.sm_their_key_dist=0;
    ble_svc_gap_init(); ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set("OFEMaster") || ble_att_set_preferred_mtu(247)) {
      fail(-1103); finish(ofe_ble::FAILED); return false;
    }
    const char* ids[]={ofe_ble::SERVICE,ofe_ble::IDENTITY,ofe_ble::CONFIG,ofe_ble::RESULT};
    for (unsigned i=0;i<4;++i) if (ble_uuid_from_str(&uuids_[i],ids[i])) {
      fail(-1104); finish(ofe_ble::FAILED); return false;
    }
    TaskHandle_t host=nullptr;
    if (xTaskCreatePinnedToCore(hostHook,"p4-nimble",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,this,5,&host,0)!=pdPASS) {
      finish(ofe_ble::LOW_MEMORY); return false;
    }
    host_started_=true;
    const uint32_t started=millis();
    while (!synced_ && !cancel_ && !failed_ && uint32_t(millis()-started)<10000UL) vTaskDelay(pdMS_TO_TICKS(10));
    if (!synced_ || cancel_ || failed_ || ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0,&own_type_)) {
      fail(-1105); finish(ofe_ble::FAILED); return false;
    }
    memory(3); return true;
  }
  void finish(ofe_ble::Phase final) {
    bool stopped=true;
    if (host_started_) {
      ble_gap_disc_cancel(); ble_gap_conn_cancel();
      if (conn_!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn_,BLE_ERR_REM_USER_CONN_TERM);
      stopped=nimble_port_stop()==ESP_OK;
      const uint32_t at=millis();
      while (stopped && !host_exited_ && uint32_t(millis()-at)<1000UL) vTaskDelay(pdMS_TO_TICKS(10));
      stopped=stopped && host_exited_;
    }
    if (stopped && host_initialized_) stopped=nimble_port_deinit()==ESP_OK;
    if (stopped && hosted_started_) stopped=hostedDeinitBLE();
    if (!stopped) { fail(-1106); poisoned_=true; final=ofe_ble::FAILED; }
    else { owner_=nullptr; host_started_=host_initialized_=hosted_started_=false; }
    memory(4);
    portENTER_CRITICAL(&mux_);
    if (final==ofe_ble::DONE) for (uint8_t i=0;i<view_.count;++i)
      if (view_.devices[i].uid==target_.uid) view_.devices[i].paired=true;
    memset(&config_,0,sizeof(config_));
    selected_=false; pending_=approved_=false; view_.phase=final; view_.token=esp_random();
    view_.active=false; active_=false;
    portEXIT_CRITICAL(&mux_);
  }
  void completeOperation(int status) { operation_status_=status; operation_done_=true; }
  bool waitOperation(int rc,uint32_t limit=5000) {
    if (rc) { fail(rc); return false; }
    const uint32_t at=millis();
    while (!operation_done_ && !failed_ && !cancel_ && uint32_t(millis()-at)<limit) vTaskDelay(pdMS_TO_TICKS(10));
    if (failed_ || cancel_) return false;
    if (!operation_done_) { fail(BLE_HS_ETIMEOUT); return false; }
    if (operation_status_) { fail(operation_status_); return false; }
    return true;
  }
  static int mtuCallback(uint16_t handle,const ble_gatt_error* error,uint16_t,void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    if (handle==self->conn_) self->completeOperation(error->status);
    return 0;
  }
  static int serviceCallback(uint16_t handle,const ble_gatt_error* error,const ble_gatt_svc* svc,void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    if (handle!=self->conn_) return 0;
    if (error->status) self->completeOperation(error->status==BLE_HS_EDONE ? 0 : error->status);
    else if (!svc || self->service_start_) self->fail(BLE_HS_EINVAL);
    else { self->service_start_=svc->start_handle; self->service_end_=svc->end_handle; }
    return 0;
  }
  static int characteristicCallback(uint16_t handle,const ble_gatt_error* error,const ble_gatt_chr* chr,void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    if (handle!=self->conn_) return 0;
    if (error->status) self->completeOperation(error->status==BLE_HS_EDONE ? 0 : error->status);
    else if (chr) for (unsigned i=0;i<3;++i) if (!ble_uuid_cmp(&chr->uuid.u,&self->uuids_[i+1].u)) {
      const uint8_t property=i==1 ? BLE_GATT_CHR_PROP_WRITE : BLE_GATT_CHR_PROP_READ;
      if (self->handles_[i] || !(chr->properties & property)) self->fail(BLE_HS_EINVAL);
      else self->handles_[i]=chr->val_handle;
    }
    return 0;
  }
  static int readCallback(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr* attr,void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    if (handle!=self->conn_) return 0;
    uint16_t copied=0;
    const int rc=error->status ? error->status : !attr || OS_MBUF_PKTLEN(attr->om)>sizeof(self->read_bytes_) ?
      BLE_HS_EMSGSIZE : ble_hs_mbuf_to_flat(attr->om,self->read_bytes_,sizeof(self->read_bytes_),&copied);
    self->read_length_=copied; self->completeOperation(rc); return 0;
  }
  static int writeCallback(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr*,void* ptr) {
    auto* self=static_cast<OfeDisplayBle*>(ptr);
    if (handle==self->conn_) self->completeOperation(error->status);
    return 0;
  }
  bool read(uint16_t handle) {
    operation_done_=false; read_length_=0;
    return waitOperation(ble_gattc_read(conn_,handle,readCallback,this));
  }
  bool pair() {
    phase(ofe_ble::CONNECTING);
    ble_addr_t peer{}; unsigned bytes[6]{};
    if (sscanf(target_.address,"%x:%x:%x:%x:%x:%x",bytes+5,bytes+4,bytes+3,bytes+2,bytes+1,bytes)!=6) return false;
    peer.type=target_.address_type;
    for (unsigned i=0;i<6;++i) peer.val[i]=uint8_t(bytes[i]);
    int rc=ble_gap_connect(own_type_,&peer,5000,nullptr,gap,this);
    if (rc) { fail(rc); return false; }
    uint32_t at=millis();
    while (conn_==BLE_HS_CONN_HANDLE_NONE && !failed_ && !cancel_ && uint32_t(millis()-at)<6000UL) vTaskDelay(pdMS_TO_TICKS(10));
    if (failed_ || cancel_ || conn_==BLE_HS_CONN_HANDLE_NONE) return false;
    rc=ble_gap_security_initiate(conn_);
    if (rc && rc!=BLE_HS_EALREADY) { fail(rc); return false; }
    at=millis();
    while (!secured_ && !failed_ && !cancel_ && uint32_t(millis()-at)<ofe_ble::SEARCH_TIMEOUT_MS) {
      deliverDecision(); vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!authorized()) return false;
    operation_done_=false;
    if (!waitOperation(ble_gattc_exchange_mtu(conn_,mtuCallback,this))) return false;
    if (ble_att_mtu(conn_)<15+sizeof(config_)) { fail(BLE_HS_EMSGSIZE); return false; }
    operation_done_=false;
    if (!waitOperation(ble_gattc_disc_svc_by_uuid(conn_,&uuids_[0].u,serviceCallback,this))) return false;
    if (!service_start_) { fail(BLE_HS_EINVAL); return false; }
    operation_done_=false;
    if (!waitOperation(ble_gattc_disc_all_chrs(conn_,service_start_,service_end_,characteristicCallback,this))) return false;
    if (!handles_[0] || !handles_[1] || !handles_[2]) { fail(BLE_HS_EINVAL); return false; }
    if (!read(handles_[0]) || read_length_!=8 || jbc_rs485::get_u64_le(read_bytes_)!=target_.uid) {
      fail(BLE_HS_EAUTHEN); return false;
    }
    if (!authorized()) return false;
    phase(ofe_ble::SAVING);
    uint8_t payload[12+sizeof(config_)]; memcpy(payload,"OPB1",4);
    jbc_rs485::put_u64_le(payload+4,target_.uid); memcpy(payload+12,&config_,sizeof(config_));
    operation_done_=false;
    const bool written=waitOperation(ble_gattc_write_flat(conn_,handles_[1],payload,sizeof(payload),writeCallback,this));
    memset(payload,0,sizeof(payload));
    if (!written) return false;
    at=millis();
    while (authorized() && uint32_t(millis()-at)<5000UL) {
      if (!read(handles_[2]) || read_length_!=1) return false;
      if (read_bytes_[0]==2) { phase(ofe_ble::DONE); return true; }
      if (read_bytes_[0]==3) { fail(BLE_HS_EAPP); return false; }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    fail(BLE_HS_ETIMEOUT); return false;
  }
  void run() {
    if (!initRadio()) return;
    const uint32_t started=millis();
    while (!selected_ && !cancel_ && !failed_ && uint32_t(millis()-started)<ofe_ble::SEARCH_TIMEOUT_MS) {
      phase(ofe_ble::SCANNING); scan_done_=false;
      ble_gap_disc_params params{}; params.passive=0; params.filter_duplicates=0;
      const int rc=ble_gap_disc(own_type_,4000,&params,gap,this);
      if (rc) { fail(rc); break; }
      while (!scan_done_ && !selected_ && !cancel_ && !failed_) vTaskDelay(pdMS_TO_TICKS(20));
      if (!scan_done_) ble_gap_disc_cancel();
      const uint32_t paused=millis();
      while (!selected_ && !cancel_ && !failed_ && uint32_t(millis()-paused)<1000UL) vTaskDelay(pdMS_TO_TICKS(20));
    }
    bool success=false;
    if (selected_ && !cancel_ && !failed_) success=pair();
    finish(success ? ofe_ble::DONE : cancel_ ? ofe_ble::CANCELLED : failed_ ? ofe_ble::FAILED : ofe_ble::EXPIRED);
  }
};
