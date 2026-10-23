#include "ModuleRegistry.h"
#include "DisplayValueStream.h"
#include "OfeJbcDetailWire.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace jbc_rs485;
using ofe_display_values::Value;
using ofe_display_values::Request;
struct Entity { bool valid=true; uint8_t id=0,type=1; int16_t value=0; char text[32]={}; };
struct Cache {
  uint8_t addr=0x50,type=MODULE_UNIVERSAL_RS232,flags=1,values_language=1;
  uint32_t universal_descriptor_crc=0x1234,last_ms=0,values_refresh_ms=0,values_refresh_mask=0;
  uint8_t universal_entity_total=32,universal_entity_count=32,request_cursor=0;
  Entity universal_entities[32];
} caches[2];
static Cache* universal_cache_find(uint8_t addr,bool){for(auto& c:caches)if(c.addr==addr)return &c;return nullptr;}
constexpr unsigned DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX=32;
static uint8_t display_language=1;
static bool display_values_supported=true;
static uint32_t now_ms=100;
static uint32_t millis(){return now_ms;}
static struct Selected {bool valid=true;uint8_t addr=0x50,type=MODULE_UNIVERSAL_RS232;Entity universal_entities[32];} selected_module;
static uint8_t selected_universal_request_cursor=0;
static Entity* selected_universal_entity_by_id(uint8_t id){for(auto& e:selected_module.universal_entities)if(e.id==id)return &e;return nullptr;}
static uint8_t ack=255,pending_id=51;
static void send_status_response(const Frame&,uint8_t status){ack=status;}
static void universal_pending_observe(uint8_t addr,const Entity& e){if(addr==0x50&&e.id==pending_id&&e.value==777)pending_id=0;}
static bool worker=true;
static bool running_in_rs485_task(){return worker;}
constexpr uint32_t UI_DEFER_APP_VALUES=1,UI_DEFER_MODULE_DETAIL=2;
static uint32_t deferred=0,home_updates=0,detail_updates=0;
static void ui_defer_flags(uint32_t flags){deferred|=flags;}
static void lv_update_app_values(){++home_updates;}
static void lv_update_module_detail(){++detail_updates;}
#include "OfeDisplayValueStreamState.h"

static Frame request(uint8_t priority=0){Frame r;r.len=6;assert(append_display_value_request(r,0x50,priority));return r;}
static Request decode(const Frame& frame){Request r;assert(ofe_display_values::decodeRequest(frame.payload+7,frame.len-7,r));return r;}
static Frame page(const Value* values,const Request& request){
  Frame frame;frame.len=uint8_t(ofe_display_values::encodePage(frame.payload,sizeof(frame.payload),request.addr,
    MODULE_UNIVERSAL_RS232,1,request.crc,values,request.count,request));assert(frame.len);return frame;
}
int main(){
  using namespace ofe_display_values;
  static_assert(CAPABILITY!=ofe_jbc_detail::REQUEST&&REQUEST!=ofe_jbc_detail::LEGACY_REQUEST);
  assert(statusSlot(0,150)==150&&statusSlot(1,150)==150&&statusSlot(2,150)==100);
  assert(statusSlot(3,150)==66&&statusSlot(4,150)==50&&statusSlot(16,150)==50);
  Value binary; binary.id=20; binary.type=2; binary.number=1;
  const auto binary_hash=hash(binary);strcpy(binary.text,"On");assert(hash(binary)==binary_hash);
  Value values[32];
  for(unsigned i=0;i<32;++i){
    values[i].id=20+i;values[i].type=1;values[i].number=100+i;
    snprintf(values[i].text,sizeof(values[i].text),"%ud 16h 30m",i);
    auto& e=caches[0].universal_entities[i];e.id=20+i;e.type=1;
    selected_module.universal_entities[i]=e;
  }
  caches[1].addr=0x60;caches[1].type=MODULE_MODBUS_RTU;
  for(auto& e:caches[1].universal_entities)e.value=-123;
  // First values round forces every cached entity; acknowledge it over several small pages.
  size_t initial_bytes=0;unsigned pages=0;
  while(caches[0].values_refresh_mask || pages==0){
    const auto req=request();const auto known=decode(req);const auto frame=page(values,known);
    initial_bytes+=frame.len;handle_display_value_page(frame);assert(ack==STATUS_OK);
    assert(++pages<=8);
  }
  assert(caches[0].universal_entities[31].value==131);
  assert(!strcmp(caches[0].universal_entities[31].text,"31d 16h 30m"));
  assert(selected_module.universal_entities[31].value==131&&deferred==(UI_DEFER_APP_VALUES|UI_DEFER_MODULE_DETAIL));
  for(auto& e:caches[1].universal_entities)assert(e.value==-123);
  deferred=0;
  auto stable=page(values,decode(request()));assert(stable.len==HEADER&&stable.payload[8]==0);
  handle_display_value_page(stable);assert(ack==STATUS_OK&&deferred==0);
  // A late entity is sent first after an explicit edit, using exactly the formatted text.
  values[31].number=777;strcpy(values[31].text,"6d 16h 0m");
  auto edit=page(values,decode(request(51)));assert(edit.payload[10]==51&&edit.payload[8]==1);
  auto retry=page(values,decode(request(51)));assert(edit.len==retry.len&&!memcmp(edit.payload,retry.payload,edit.len));
  worker=false;handle_display_value_page(retry);
  assert(ack==STATUS_OK&&!pending_id&&home_updates==1&&detail_updates==1);
  assert(!strcmp(caches[0].universal_entities[31].text,"6d 16h 0m"));
  // Truncated/corrupt packets are atomic. No cached value changes before full validation.
  const auto before=caches[0];
  for(unsigned n=0;n<edit.len;++n){Frame bad=edit;bad.len=n;handle_display_value_page(bad);assert(ack!=STATUS_OK&&!memcmp(&before,&caches[0],sizeof(before)));}
  auto bad=edit;bad.payload[4]^=1;handle_display_value_page(bad);assert(ack==STATUS_BAD_VALUE);
  bad=edit;bad.payload[11]=7;handle_display_value_page(bad);assert(ack==STATUS_BAD_VALUE);
  bad=edit;bad.payload[10]=99;handle_display_value_page(bad);assert(ack==STATUS_BAD_VALUE);
  assert(!memcmp(&before,&caches[0],sizeof(before)));
  // Unknown capability/metadata/language must use the legacy descriptor path.
  Frame legacy;legacy.len=6;display_values_supported=false;
  assert(!append_display_value_request(legacy,0x50)&&legacy.len==6);display_values_supported=true;
  caches[0].values_language=0;assert(!append_display_value_request(legacy,0x50));caches[0].values_language=1;
  --caches[0].universal_entity_count;assert(!append_display_value_request(legacy,0x50));++caches[0].universal_entity_count;
  // Loss repair includes all 32 entries, including a deliberately wrong fingerprint.
  now_ms+=10001;const auto forced=decode(request());for(auto hash:forced.known)assert(!hash);
  caches[0].values_refresh_mask=0;caches[0].values_refresh_ms=0xFFFFFFF0U;now_ms=10;
  assert(decode(request()).known[0]!=0); // Wrapped elapsed time is only 26 ms.
  Request fair=forced;fair.cursor=0;fair.priority=51;
  for(auto& v:values)memset(v.text,'x',31),v.text[31]=0;
  uint32_t seen=0;
  for(unsigned round=0;round<8;++round){
    const auto frame=page(values,fair);Page decoded;assert(decodePage(frame.payload,frame.len,decoded));
    size_t pos=HEADER;for(unsigned i=0;i<decoded.count;++i){Value v;pos=read(decoded,pos,v);seen|=uint32_t(1)<<(v.id-20);}
    fair.cursor=decoded.cursor;
  }
  assert(seen==UINT32_MAX);
  // A malformed request must not replace the last fully decoded request.
  auto wire=request();Request decoded=forced,sentinel=forced;
  for(size_t n=0;n<size_t(wire.len-7);++n){decoded=sentinel;assert(!decodeRequest(wire.payload+7,n,decoded));assert(decoded.crc==sentinel.crc);}
  printf("PASS: compact values: %u initial pages/%zu value bytes; idle 10 B; changed edit %u B; actual display caches/readback, malformed packets, loss, fairness, rollover and legacy fallback\n",pages,initial_bytes,edit.len);
}
