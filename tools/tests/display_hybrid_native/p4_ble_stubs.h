#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <functional>
#include <vector>
using std::size_t;
using TaskHandle_t=void*;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_OK 0
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ATT_ERR_INSUFFICIENT_RES 1
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 2
#define BLE_ATT_ERR_UNLIKELY 3
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 4
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_PASSKEY_ACTION 2
#define BLE_GAP_EVENT_ENC_CHANGE 3
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_SM_IOACT_NUMCMP 4
#define BLE_HS_IO_DISPLAY_YESNO 1
#define BLE_GATT_CHR_F_READ 1
#define BLE_GATT_CHR_F_WRITE 2
#define BLE_GATT_CHR_F_WRITE_ENC 4
#define BLE_GATT_CHR_F_WRITE_AUTHEN 8
#define BLE_GATT_CHR_F_READ_ENC 16
#define BLE_GATT_CHR_F_READ_AUTHEN 32
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_HS_ADV_F_DISC_GEN 1
#define BLE_HS_ADV_F_BREDR_UNSUP 2
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 1
#define BLE_HS_FOREVER -1
#define BLE_HS_EINVAL 3
#define BLE_HS_EAUTHEN 23
struct ble_uuid_t { int id; };
struct ble_uuid128_t { int id; };
union ble_uuid_any_t { ble_uuid_t u; ble_uuid128_t u128; };
struct os_mbuf { std::vector<uint8_t> bytes; };
struct ble_gatt_access_ctxt;
struct ble_gatt_chr_def {
  const ble_uuid_t* uuid=nullptr;
  int(*access_cb)(uint16_t,uint16_t,ble_gatt_access_ctxt*,void*)=nullptr;
  void* arg=nullptr; int flags=0; uint8_t min_key_size=0;
};
struct ble_gatt_svc_def { int type=0; const ble_uuid_t* uuid=nullptr; const ble_gatt_chr_def* characteristics=nullptr; };
struct ble_gatt_access_ctxt { const ble_gatt_chr_def* chr; int op; os_mbuf* om; };
struct ble_gap_conn_desc { struct { bool encrypted=false,authenticated=false; int key_size=0; } sec_state; uint16_t conn_handle=1; };
struct ble_gap_event {
  int type=0;
  struct { int status=0; uint16_t conn_handle=1; } connect,enc_change;
  struct { ble_gap_conn_desc conn; int reason=19; } disconnect;
  struct { uint16_t conn_handle=1; struct { int action=BLE_SM_IOACT_NUMCMP; uint32_t numcmp=123456; } params; } passkey;
};
struct ble_sm_io { int action=0; bool numcmp_accept=false; };
struct ble_hs_adv_fields {
  int flags=0; const ble_uuid128_t* uuids128=nullptr; int num_uuids128=0,uuids128_is_complete=0;
  const uint8_t* name=nullptr; uint8_t name_len=0; int name_is_complete=0;
  const uint8_t* mfg_data=nullptr; uint8_t mfg_data_len=0;
};
struct ble_gap_adv_params { int conn_mode=0,disc_mode=0; };
inline struct { void(*sync_cb)()=nullptr; void(*reset_cb)(int)=nullptr; int sm_io_cap=0,sm_bonding=0,sm_mitm=0,sm_sc=0,sm_our_key_dist=0,sm_their_key_dist=0; } ble_hs_cfg;
inline uint32_t now=0,heap=120000,largest=64000;
inline bool pins_ready=true,hosted_ok=true,hosted_wifi_active=true;
inline int ecc_self_test_result=0;
inline int ofe_p4_ble_ecc_self_test() { return ecc_self_test_result; }
inline int init_error=0,deinit_error=0,hosted_stops=0,radio_starts=0,task_error=0,inject_count=0,inject_error=0,terminated=0;
inline bool injected_accept=false;
inline ble_gap_conn_desc peer;
inline std::vector<uint8_t> manufacturer;
inline std::function<void(unsigned)> delay_hook;
inline std::function<void()> inject_hook;
inline uint32_t millis() { return now; }
inline uint32_t esp_random() { static uint32_t token=100; return ++token; }
inline void vTaskDelay(unsigned ms) { now+=ms; if (delay_hook) delay_hook(ms); }
inline void vTaskDelete(void*) {}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char* name,unsigned,void*,int,TaskHandle_t* out,int) {
  if (task_error) return 0;
  *out=reinterpret_cast<void*>(1);
  if (!strcmp(name,"p4-nimble")) ble_hs_cfg.sync_cb();
  return pdPASS;
}
inline uint32_t heap_caps_get_free_size(int) { return heap; }
inline uint32_t heap_caps_get_largest_free_block(int) { return largest; }
namespace ofe_p4 { inline bool hostedPinsReady() { return pins_ready; } }
inline bool hostedInitBLE() { ++radio_starts; return hosted_ok; }
inline bool hostedDeinitBLE() { ++hosted_stops; return true; }
inline int nimble_port_init() { return init_error; }
inline void nimble_port_run() {}
inline int nimble_port_stop() { return 0; }
inline int nimble_port_deinit() { return deinit_error; }
inline void ble_svc_gap_init() {}
inline void ble_svc_gatt_init() {}
inline int ble_svc_gap_device_name_set(const char*) { return 0; }
inline int ble_att_set_preferred_mtu(int) { return 0; }
inline int ble_uuid_from_str(ble_uuid_any_t* out,const char*) { static int id=0; out->u.id=++id; return 0; }
inline int ble_uuid_cmp(const ble_uuid_t* a,const ble_uuid_t* b) { return a->id-b->id; }
inline int ble_gatts_count_cfg(const ble_gatt_svc_def*) { return 0; }
inline int ble_gatts_add_svcs(const ble_gatt_svc_def*) { return 0; }
inline int ble_hs_util_ensure_addr(int) { return 0; }
inline int ble_hs_id_infer_auto(int,uint8_t* type) { *type=0; return 0; }
inline int ble_gap_adv_set_fields(const ble_hs_adv_fields*) { return 0; }
inline int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields* fields) {
  manufacturer.assign(fields->mfg_data,fields->mfg_data+fields->mfg_data_len); return 0;
}
inline int ble_gap_adv_start(uint8_t,void*,int,ble_gap_adv_params*,int(*)(ble_gap_event*,void*),void*) { return 0; }
inline int ble_gap_adv_stop() { return 0; }
inline int ble_gap_conn_find(uint16_t handle,ble_gap_conn_desc* out) {
  if (handle!=peer.conn_handle) return 1;
  *out=peer; return 0;
}
inline int ble_gap_terminate(uint16_t,int) { ++terminated; return 0; }
inline int ble_sm_inject_io(uint16_t,ble_sm_io* io) {
  ++inject_count; injected_accept=io->numcmp_accept;
  if (inject_hook) inject_hook();
  return inject_error;
}
#define OS_MBUF_PKTLEN(om) ((om)->bytes.size())
inline int os_mbuf_append(os_mbuf* om,const void* data,size_t n) {
  const auto* p=static_cast<const uint8_t*>(data); om->bytes.insert(om->bytes.end(),p,p+n); return 0;
}
inline int ble_hs_mbuf_to_flat(os_mbuf* om,void* out,size_t capacity,uint16_t* copied) {
  if (om->bytes.size()>capacity) return 1;
  *copied=uint16_t(om->bytes.size()); memcpy(out,om->bytes.data(),*copied); return 0;
}
namespace jbc_rs485 {
constexpr unsigned MAX_PAYLOAD=192;
inline uint64_t get_u64_le(const uint8_t* b) { uint64_t v=0; for(int i=0;i<8;++i) v|=uint64_t(b[i])<<(8*i); return v; }
inline void put_u64_le(uint8_t* b,uint64_t v) { for(int i=0;i<8;++i) b[i]=uint8_t(v>>(8*i)); }
}
