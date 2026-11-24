#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
constexpr uint8_t CMD_ENTITY_GET=0x71,STATUS_OK=0,MODULE_FAN_IO=2,MODULE_FAN_IO_PRO=3;
struct Frame{uint8_t cmd=0,len=0,payload[192]{};};
struct Entry{uint8_t id=0,len=0,data[8]{};uint16_t age_ms=0;};
struct ModuleRecord{static constexpr uint8_t UNIVERSAL_ENTITY_MAX=64;uint8_t type=2,universal_entity_count=0;Entry universal_entities[64];};
uint16_t get_u16_le(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
uint32_t get_u32_le(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
Entry* find_universal_entity_state(ModuleRecord& r,uint8_t id){for(uint8_t i=0;i<r.universal_entity_count;++i)if(r.universal_entities[i].id==id)return &r.universal_entities[i];return nullptr;}
uint8_t universal_cached_debug_count(ModuleRecord&){return 0;}
void remember_universal_entity_state(ModuleRecord& r,uint8_t id,const uint8_t* data,uint8_t len){auto* e=find_universal_entity_state(r,id);if(!e){assert(r.universal_entity_count<64);e=&r.universal_entities[r.universal_entity_count++];e->id=id;}e->len=len;memcpy(e->data,data,len);}
#include "generated_bank.inc.h"
int main(){
  Frame f;f.cmd=CMD_ENTITY_GET|0x80;f.len=14;f.payload[1]=1;f.payload[2]=99;f.payload[3]=8;
  f.payload[6]=1;f.payload[9]=0x80;memset(f.payload+10,255,4);
  for(uint8_t type:{uint8_t(2),uint8_t(3)}){
    ModuleRecord r;r.type=type;uint8_t seen[32]{};
    assert(universal_merge_entity_response(r,f,32,seen));assert(r.universal_entity_count==32);
    for(uint8_t i=0;i<32;++i){auto* e=find_universal_entity_state(r,100+i);assert(e&&e->len==1&&e->data[0]==(i==0||i==31));assert(seen[(100+i)>>3]&(1U<<((100+i)&7)));}
    f.payload[6]=0;f.payload[9]=0;assert(universal_merge_entity_response(r,f,32,seen));assert(!find_universal_entity_state(r,131)->data[0]);assert(r.universal_entity_count==32);
    f.payload[6]=1;f.payload[9]=0x80;
  }
  ModuleRecord other;other.type=7;assert(universal_merge_entity_response(other,f,32,nullptr));assert(other.universal_entity_count==1&&other.universal_entities[0].id==99);
  f.len=13;ModuleRecord truncated;assert(universal_merge_entity_response(truncated,f,32,nullptr));assert(!truncated.universal_entity_count);
  puts("PASS: actual MCU LED-bank decoder, 32 expanded states, bit31, polling bitmap, type guard and truncated frame");
}
