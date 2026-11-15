#pragma once
#include "p4_ble_stubs.h"
#include <cstdio>
#define BLE_GAP_EVENT_DISC 4
#define BLE_GAP_EVENT_DISC_COMPLETE 5
#define BLE_HS_EDONE 14
#define BLE_HS_ETIMEOUT 13
#define BLE_HS_EMSGSIZE 4
#define BLE_HS_EALREADY 2
#define BLE_HS_EAPP 9
#define BLE_GATT_CHR_PROP_WRITE 8
#define BLE_GATT_CHR_PROP_READ 2
struct ble_gap_disc_params { uint8_t passive=0,filter_duplicates=0; };
struct ble_gatt_error { int status=0; };
struct ble_gatt_svc { uint16_t start_handle=1,end_handle=8; };
struct ble_gatt_chr { ble_uuid_any_t uuid{}; uint16_t val_handle=0; uint8_t properties=0; };
struct ble_gatt_attr { os_mbuf* om=nullptr; };
inline int(*central_gap)(ble_gap_event*,void*)=nullptr;
inline void* central_arg=nullptr;
inline uint64_t remote_uid=0x40001234567890ABULL;
inline bool drop_operation=false,bad_characteristics=false;
inline uint16_t mtu=247;
inline unsigned written_configs=0;
inline std::vector<uint8_t> written_payload;
inline uint8_t persistence_result=2;
inline int ble_hs_adv_parse_fields(ble_hs_adv_fields* fields,const uint8_t* data,uint8_t size) {
  fields->mfg_data=data; fields->mfg_data_len=size; return 0;
}
inline int ble_gap_disc(uint8_t,int,const ble_gap_disc_params*,int(*cb)(ble_gap_event*,void*),void* arg) {
  central_gap=cb; central_arg=arg; return 0;
}
inline int ble_gap_disc_cancel() { return 0; }
inline int ble_gap_conn_cancel() { return 0; }
inline int ble_gap_connect(uint8_t,const ble_addr_t*,int,const void*,int(*cb)(ble_gap_event*,void*),void* arg) {
  central_gap=cb; central_arg=arg; ble_gap_event e{}; e.type=BLE_GAP_EVENT_CONNECT; cb(&e,arg); return 0;
}
inline int ble_gap_security_initiate(uint16_t) {
  ble_gap_event e{}; e.type=BLE_GAP_EVENT_PASSKEY_ACTION; central_gap(&e,central_arg); return 0;
}
inline int ble_att_mtu(uint16_t) { return mtu; }
inline int ble_gattc_exchange_mtu(uint16_t conn,int(*cb)(uint16_t,const ble_gatt_error*,uint16_t,void*),void* ptr) {
  ble_gatt_error error{}; if (!drop_operation) cb(conn,&error,mtu,ptr); return 0;
}
inline int ble_gattc_disc_svc_by_uuid(uint16_t conn,const ble_uuid_t*,int(*cb)(uint16_t,const ble_gatt_error*,const ble_gatt_svc*,void*),void* ptr) {
  ble_gatt_error error{}; ble_gatt_svc svc{}; cb(conn,&error,&svc,ptr);
  error.status=BLE_HS_EDONE; cb(conn,&error,nullptr,ptr); return 0;
}
inline int ble_gattc_disc_all_chrs(uint16_t conn,uint16_t,uint16_t,int(*cb)(uint16_t,const ble_gatt_error*,const ble_gatt_chr*,void*),void* ptr) {
  // Test runner supplies the real instance's UUIDs, not a second protocol definition.
  extern ble_uuid_any_t* test_uuids;
  ble_gatt_error error{};
  for (unsigned i=0;i<3;++i) {
    ble_gatt_chr chr{}; chr.uuid=test_uuids[i+1]; chr.val_handle=uint16_t(i+2);
    chr.properties=bad_characteristics ? 0 : i==1 ? BLE_GATT_CHR_PROP_WRITE : BLE_GATT_CHR_PROP_READ;
    cb(conn,&error,&chr,ptr);
  }
  error.status=BLE_HS_EDONE; cb(conn,&error,nullptr,ptr); return 0;
}
inline int ble_gattc_read(uint16_t conn,uint16_t handle,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* ptr) {
  os_mbuf om{};
  if (handle==2) { om.bytes.resize(8); jbc_rs485::put_u64_le(om.bytes.data(),remote_uid); }
  else om.bytes.push_back(persistence_result);
  ble_gatt_attr attr{&om}; ble_gatt_error error{}; cb(conn,&error,&attr,ptr); return 0;
}
inline int ble_gattc_write_flat(uint16_t conn,uint16_t,const void* bytes,uint16_t size,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* ptr) {
  ++written_configs;
  const auto* data=static_cast<const uint8_t*>(bytes); written_payload.assign(data,data+size);
  ble_gatt_error error{}; cb(conn,&error,nullptr,ptr); return 0;
}
