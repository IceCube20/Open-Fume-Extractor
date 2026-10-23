#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "DisplayJbcCatalog.h"
#include "OfeJbcDetailState.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
using namespace ofe_jbc_detail;
using namespace jbc_rs485;
static bool developer = false;
bool master_developer_mode_enabled_for_modules() { return developer; }
static const uint8_t DISPLAY_VIEW_MODULE_DETAIL = 2;
static const uint32_t DISPLAY_WIFI_ASYNC_TIMEOUT_MS = 1000;
static uint32_t fnv1a32_local(const uint8_t* p,size_t n) { uint32_t h=2166136261UL;while(n--){h^=*p++;h*=16777619UL;}return h; }
struct { bool enabled=false;bool active(uint8_t)const{return enabled;} } master_display_wifi;
struct TestMaster {
  struct Registry { ModuleRecord target;ModuleRecord* find(uint8_t addr){return addr==target.addr?&target:nullptr;} } registry_;
  bool updating=false;int writes=0;uint8_t pending_jbc_usb_state_addr_=0;
  std::vector<uint8_t> sent;bool via_wifi=false;
  bool firmwareUpdateActive()const{return updating;}
  bool setJbcUsbConfig(uint8_t,const uint8_t*,uint8_t){++writes;return true;}
  bool startAsyncDisplayRequest(uint8_t,uint8_t cmd,const uint8_t* p,uint8_t n,uint32_t){assert(cmd==CMD_DISPLAY_DETAIL_PAGE);sent.assign(p,p+n);via_wifi=true;return true;}
  bool request(uint8_t,uint8_t cmd,const uint8_t* p,uint8_t n,Frame& response,uint32_t){sent.assign(p,p+n);via_wifi=false;response.cmd=cmd|0x80;response.len=1;response.payload[0]=STATUS_OK;return true;}
  bool consumeDisplayJbcExtension(ModuleRecord&,const Frame&,uint8_t&,uint8_t);
  bool sendDisplayJbcPage(ModuleRecord&,const ModuleRecord&);
};
#include "generated_jbc_detail.inc.h"

static ModuleRecord module(const char* model, uint8_t ports = 1) {
  ModuleRecord m; m.type = MODULE_JBC_USB; m.addr = 0x12; m.online = true;
  m.uid = 0x1100000000000001ULL; m.jbc_link_flags = 1;
  strcpy_s(m.jbc_usb_model, model); strcpy_s(m.jbc_usb_model_raw, model);
  m.jbc_usb_command_protocol = 2; m.jbc_usb_port_count = ports;
  for (auto& p : m.jbc_usb_ports) {
    p.valid = true; p.detail_value_flags = 0xFFFF; p.ha_value_flags = 0xFFFF;
    p.sold_extra_flags = 0xFFFF; p.sold_special_counter_flags = 0xFFFF;
    p.cl_flags = 0xFFFF; p.ph_flags = 0xFFFF; p.fe_flags = 0xFFFF; p.sf_flags = 0xFFFF;
    p.selected_temp = 3150; p.level_temp[0] = 2700; p.level_temp[1] = 3150; p.level_temp[2] = 3600;
    p.counter_plug_min = 2450; p.cl_counter_plug_min = 2450;
  }
  return m;
}
static std::vector<uint8_t> page(const ModuleRecord& m, const State& s, uint8_t first,
                                 uint8_t cookie = 0, uint8_t status = 0xFF, size_t capacity = MAX_PAYLOAD) {
  std::vector<uint8_t> data(capacity); Row row;
  const uint8_t total = ofe_jbc_catalog::entry(m,s.section,s.port,true,first,row);
  data[0]=VERSION;data[1]=m.addr;data[2]=s.section;data[3]=s.port;data[4]=first;data[5]=total;
  put32(data.data()+6,ofe_jbc_catalog::revision(m));data[10]=cookie;data[11]=status;
  data[12]=m.jbc_usb_port_count;data[13]=3;
  size_t used=HEADER_SIZE;
  for(uint8_t i=first;i<total&&i/WINDOW==first/WINDOW;++i){
    ofe_jbc_catalog::entry(m,s.section,s.port,true,i,row);
    size_t bytes=encode(row,data.data()+used,capacity-used);if(!bytes)break;
    used+=bytes;++data[14];
  }
  data.resize(used);return data;
}
static void load(ModuleRecord& m,State& s,uint32_t now=100) {
  uint8_t request[32];
  for(int attempt=0;attempt<24;++attempt){
    const size_t size=s.request(request,sizeof(request),now);
    if(!size)return;
    const auto response=page(m,s,request[4]);assert(s.accept(response.data(),response.size()));
    if(s.received && s.loaded==uint16_t((1U<<((s.total-s.base)<WINDOW?s.total-s.base:WINDOW))-1U))return;
    now+=100;
  }
  assert(false);
}
static void codec() {
  Row row;copy(row.label,sizeof(row.label),"T\303\274r offen");copy(row.text,sizeof(row.text),"Ein");
  uint8_t data[160];const size_t used=encode(row,data,sizeof(data));assert(used);
  Row decoded;size_t length=0;assert(decode(data,used,decoded,length)&&length==used);
  assert(!strcmp(row.label,decoded.label));
  for(size_t n=0;n<used;++n)assert(!decode(data,n,decoded,length));
  data[11]=200;assert(!decode(data,sizeof(data),decoded,length));
  char text[3];copy(text,sizeof(text),"a\303\274");assert(!strcmp(text,"a"));
  row.kind=NUMBER;row.writable=true;row.minimum=100;row.maximum=0;
  const size_t bad=encode(row,data,sizeof(data));assert(!decode(data,bad,decoded,length));
}
static void categories_and_classes() {
  for(const char* name:{"DDE","JTSE","CLM","PHSE","F1","SF"}) {
    auto m=module(name);Row r;
    const auto family=jbc_usb_core_state(m).family;assert(family!=JBC_USB_CORE_UNKNOWN);
    for(uint8_t section=0;section<SECTION_COUNT;++section){
      State s;s.select(m.addr,section,0);load(m,s);assert(s.received);
      if(section==STATION||section==PORT||section==COUNTERS||section==DIAGNOSTICS)assert(s.total>0);
    }
    const uint32_t old=ofe_jbc_catalog::revision(m);m.jbc_link_flags=0;
    assert(old!=ofe_jbc_catalog::revision(m));
  }
  auto m=module("DDE",4);
  for(uint8_t port=0;port<4;++port){
    State s;s.select(m.addr,SETTINGS,port);load(m,s);assert(s.total>6);
    Row r;for(uint8_t i=0;i<s.total;++i){ofe_jbc_catalog::entry(m,SETTINGS,port,true,i,r);assert(r.id/16==port);}
  }
  auto cl=module("CLM");Row row;
  assert(ofe_jbc_catalog::entry(cl,SETTINGS,0,true,0,row)==0);
  State swap;swap.select(m.addr,PORT,3);load(m,swap);auto fewer_ports=page(cl,swap,0);
  assert(swap.accept(fewer_ports.data(),fewer_ports.size())&&swap.port==0&&!swap.received);
  load(cl,swap);assert(swap.received&&swap.total>0);
  auto ph=module("PHSE");ph.jbc_usb_ph_tc[3].flags=8;ph.jbc_usb_ph_tc[3].selected_temp=3150;
  const uint8_t tc_count=ofe_jbc_catalog::entry(ph,SETTINGS,0,true,0,row);assert(tc_count==3);
  ofe_jbc_catalog::entry(ph,SETTINGS,0,true,tc_count-1,row);assert(row.id==67&&row.writable);
  auto jt=module("JTSE");strcpy_s(jt.jbc_usb_model_type,"CAP");jt.jbc_usb_model_version=1;
  assert(ofe_jbc_catalog::entry(jt,SETTINGS,0,true,0,row)==2);
  auto dde=module("DDE");State s;s.select(dde.addr,COUNTERS,0);load(dde,s);
  assert(!strcmp(s.rows[0].text,"1d 16h 50m"));
  auto n=ofe_jbc_catalog::entry(dde,DIAGNOSTICS,0,false,0,row);
  developer=true;assert(ofe_jbc_catalog::entry(dde,DIAGNOSTICS,0,false,0,row)==n+2);developer=false;
}
static void paging_and_writes() {
  auto m=module("DDE");State s;s.select(m.addr,STATION,0);load(m,s);
  assert(s.total>WINDOW);assert(s.loaded==0xFFF);s.select(m.addr,STATION,0,WINDOW);load(m,s);assert(s.base==WINDOW);
  auto valid=page(m,s,WINDOW);auto old=s;
  valid[2]=COUNTERS;assert(!s.accept(valid.data(),valid.size()));assert(s.revision==old.revision);
  valid=page(m,s,WINDOW);valid.push_back(0);assert(!s.accept(valid.data(),valid.size()));
  valid=page(m,s,WINDOW);valid.pop_back();assert(!s.accept(valid.data(),valid.size()));
  s.select(m.addr,SETTINGS,0);load(m,s);
  assert(s.rows[0].writable&&s.rows[0].id==0);assert(!s.queue(0,6000,200));
  assert(s.queue(0,3600,200));assert(!s.queue(0,3700,201));
  s.select(m.addr,PORT,0);assert(s.section==SETTINGS); // pending write locks navigation
  uint8_t request[32];assert(s.request(request,sizeof(request),250)==15);
  assert(request[5]==COMMAND&&request[7]==0&&u16(request+8)==3600);
  uint8_t payload[23],len=0;assert(ha_jbc_control_payload(m,request[7],int16_t(u16(request+8))/10.0f,payload,len));
  assert(len==4&&get_u16_le(payload+2)==3240);
  auto ack=page(m,s,0,s.cookie,STATUS_OK);assert(s.accept(ack.data(),ack.size()));assert(s.pending&&s.acknowledged&&s.result==STATUS_OK);
  assert(s.request(request,sizeof(request),300)==5); // accepted commands are not resent during readback
  m.jbc_usb_ports[0].selected_temp=3240;ack=page(m,s,0,s.cookie,STATUS_OK);
  assert(s.accept(ack.data(),ack.size()));assert(!s.pending);
  load(m,s,500);m.jbc_usb_port_count=0;m.jbc_link_flags=0;
  auto changed=page(m,s,0);changed[12]=0;changed[13]=1;assert(s.accept(changed.data(),changed.size()));assert(s.total==0&&s.loaded==0);
  assert(s.request(request,sizeof(request),2500)==5);changed=page(m,s,0);
  assert(s.accept(changed.data(),changed.size()));assert(s.request(request,sizeof(request),2600)==0);
  m=module("DDE");s.select(m.addr,SETTINGS,0);load(m,s,1000);
  assert(s.queue(0,3600,1200));const auto queued_revision=s.command_revision;
  m.jbc_usb_device_id[0]=1;auto replacement=page(m,s,0);assert(s.accept(replacement.data(),replacement.size()));
  assert(s.revision!=queued_revision&&!s.pending&&s.result==2);
  s.select(m.addr,SETTINGS,0);load(m,s,1500);
  assert(s.queue(0,3600,UINT32_MAX-100));s.request(request,sizeof(request),16000);assert(!s.pending&&s.result==2);
  printf("JBC visible-window PSRAM cache: %zu B\n",sizeof(State));
  puts("PASS: JBC detail codec, six classes/four ports, capability filtering, bounded paging, UTF-8, durations, stale pages, writes, acknowledgements and timer rollover");
}
static void master_roundtrip() {
  TestMaster master;master.registry_.target=module("DDE");const auto& m=master.registry_.target;
  ModuleRecord display;display.addr=0x40;display.display_view_mode=DISPLAY_VIEW_MODULE_DETAIL;display.display_view_arg=m.addr;display.display_language=1;
  State state;state.select(m.addr,SETTINGS,0);
  auto exchange=[&](uint32_t now){
    Frame response;response.len=uint8_t(state.request(response.payload,sizeof(response.payload),now));
    uint8_t cursor=0;
    while(cursor<response.len){const uint8_t marker=response.payload[cursor++];assert(master.consumeDisplayJbcExtension(display,response,cursor,marker));}
    assert(master.sendDisplayJbcPage(display,m));assert(state.accept(master.sent.data(),master.sent.size()));
  };
  exchange(100);assert(state.received&&state.rows[0].id==0);
  assert(display.display_jbc_window==12);
  // A previous six-row display explicitly retains its original page boundaries.
  Frame legacy;legacy.len=4;legacy.payload[0]=m.addr;legacy.payload[1]=SETTINGS;legacy.payload[2]=0;legacy.payload[3]=5;
  uint8_t legacy_cursor=0;
  assert(master.consumeDisplayJbcExtension(display,legacy,legacy_cursor,LEGACY_REQUEST));
  assert(display.display_jbc_window==6&&master.sendDisplayJbcPage(display,m));
  assert(master.sent[4]==5&&master.sent[14]==1);
  exchange(101);assert(display.display_jbc_window==12);
  master_display_wifi.enabled=true;assert(state.queue(0,3600,200));exchange(201);assert(master.via_wifi&&master.writes==1&&state.pending&&state.acknowledged);
  master.registry_.target.jbc_usb_ports[0].selected_temp=3240;exchange(301);assert(!state.pending&&master.writes==1);
  assert(master.pending_jbc_usb_state_addr_==m.addr);
  // Repeated UDP status datagrams must not repeat the physical station write.
  Frame repeated;repeated.len=9;repeated.payload[0]=m.addr;repeated.payload[1]=0;put16(repeated.payload+2,3600);
  put32(repeated.payload+4,state.command_revision);repeated.payload[8]=state.cookie;
  uint8_t cursor=0;assert(master.consumeDisplayJbcExtension(display,repeated,cursor,COMMAND));assert(master.writes==1);
  repeated.payload[8]++;put32(repeated.payload+4,state.command_revision+1);cursor=0;
  assert(master.consumeDisplayJbcExtension(display,repeated,cursor,COMMAND));assert(master.writes==1&&display.display_jbc_command_status==STATUS_BAD_VALUE);
  put32(repeated.payload+4,state.command_revision);repeated.payload[8]++;put16(repeated.payload+2,6000);cursor=0;
  assert(master.consumeDisplayJbcExtension(display,repeated,cursor,COMMAND));assert(master.writes==1);
  put16(repeated.payload+2,3500);repeated.payload[8]++;master.updating=true;cursor=0;
  assert(master.consumeDisplayJbcExtension(display,repeated,cursor,COMMAND));assert(master.writes==1);
  master.updating=false;display.display_view_arg=0x13;cursor=0;
  assert(master.consumeDisplayJbcExtension(display,repeated,cursor,COMMAND));assert(master.writes==1);
  repeated.len=8;cursor=0;assert(!master.consumeDisplayJbcExtension(display,repeated,cursor,COMMAND));
  master_display_wifi.enabled=false;
  puts("PASS: actual Master request/write/page handlers, RS485/WiFi parity, duplicate suppression, station revision, bounds, OTA and target guards");
}
int main(){codec();categories_and_classes();paging_and_writes();master_roundtrip();}
