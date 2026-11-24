#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define F(value) value
class String : public std::string {
 public:
  using std::string::string;
  using std::string::operator=;
  String(const std::string& value) : std::string(value) {}
  String(int value) : std::string(std::to_string(value)) {}
  String substring(size_t start, size_t end = std::string::npos) const {
    start = start > size() ? size() : start;
    end = end > size() ? size() : end;
    return substr(start, end > start ? end - start : 0);
  }
  int indexOf(const std::string& text, size_t start = 0) const { const auto at=find(text,start);return at==npos?-1:int(at); }
  int indexOf(char c, size_t start = 0) const { const auto at=find(c,start);return at==npos?-1:int(at); }
  void trim() { const auto first=find_first_not_of(" \t\r\n");if(first==npos)clear();else *this=substr(first,find_last_not_of(" \t\r\n")-first+1); }
  void toCharArray(char* target, size_t capacity) const { if(capacity){const size_t n=size()<capacity-1?size():capacity-1;std::memcpy(target,data(),n);target[n]=0;} }
};
static const char* web_text(const char* de, const char*) { return de; }
static constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static bool fail_alloc=false;
static int allocations_before_failure=-1;
static std::map<void*,size_t> live;
static size_t peak_bytes=0;
static void* heap_caps_malloc(size_t size,int) {
  if(fail_alloc||allocations_before_failure==0)return nullptr;
  if(allocations_before_failure>0)--allocations_before_failure;
  void* p=std::malloc(size);if(p)live[p]=size;
  size_t total=0;for(auto item:live)total+=item.second;if(total>peak_bytes)peak_bytes=total;
  return p;
}
static void heap_caps_free(void* p) { if(p){assert(live.erase(p)==1);std::free(p);} }
#include "generated_compiler.inc.h"

static bool logic_fs_ready=true,fail_storage=false;
static unsigned writes=0,installs=0;
static String disk_data;
static LogicCompiledDefinition cached;
static bool logic_save_fs(uint8_t,const String& value) { if(fail_storage)return false;++writes;disk_data=value;return true; }
static bool logic_save_legacy_nvs(uint8_t slot,const String& value) { return logic_save_fs(slot,value); }
static String load_logic_slot_json(uint8_t) { return disk_data; }
static void logic_cache_install(uint8_t,LogicCompiledDefinition& def) { ++installs;logic_def_release(cached);cached=def;def=LogicCompiledDefinition(); }
#include "generated_save.inc.h"

enum {HTTP_GET,HTTP_POST};
struct TestWeb {
  std::map<String,String> arguments;
  int status=0;
  String response;
  bool hasArg(const char* name) {return arguments.count(name)!=0;}
  String arg(const char* name) {return arguments[name];}
  String arg(unsigned i) {auto it=arguments.begin();std::advance(it,i);return it->second;}
  String argName(unsigned i) {auto it=arguments.begin();std::advance(it,i);return it->first;}
  unsigned args() {return unsigned(arguments.size());}
  int method() {return HTTP_POST;}
  void send(int code,const char*,const String& body) {status=code;response=body;}
} web;
static uint8_t active_logic_slot() {return 0;}
static void set_active_logic_slot(uint8_t) {}
static bool logic_name_exists(const String&,uint8_t) {return false;}
#include "generated_route.inc.h"

static String graph(unsigned nodes,unsigned links) {
  String json="{\"name\":\"Capacity\",\"enabled\":true,\"nodes\":[";
  for(unsigned i=0;i<nodes;++i){if(i)json+=',';json+="{\"id\":\"n"+std::to_string(i)+"\",\"type\":\"input\",\"signal\":\"manual\"}";}
  json+="],\"links\":[";
  for(unsigned i=0;i<links;++i){if(i)json+=',';json+="{\"from\":\"n0\",\"to\":\"n"+std::to_string(nodes-1)+"\",\"port\":0}";}
  json+="]}";return json;
}

int main() {
  assert(LOGIC_RT_MAX_NODES==64&&LOGIC_RT_MAX_LINKS==128);
  for(auto size:std::vector<std::pair<unsigned,unsigned>>{{0,0},{1,0},{32,64},{64,128}}){
    LogicCompiledDefinition def;LogicSaveError error;
    assert(logic_compile_json(graph(size.first,size.second),0,def,&error));
    assert(error==LogicSaveError::None&&def.node_count==size.first&&def.link_count==size.second);
    size_t bytes=0;for(auto item:live)bytes+=item.second;
    assert(bytes==sizeof(LogicRtNode)*size.first+sizeof(LogicRtLink)*size.second);
    if(size.second){unsigned count=0;for(uint8_t i=def.nodes[size.first-1].first_incoming;i!=0xFF;i=def.links[i].next_incoming)++count;assert(count==size.second);}
    logic_def_release(def);assert(live.empty());
  }
  LogicCompiledDefinition def;LogicSaveError error;
  const String layout=R"({"nodes":[{"id":"a","type":"input","signal":"manual"},{"id":"b","type":"and"}],"links":[{"route":{"p":[[138,67],[300,67],[300,107],[400,107]],"s":[138,67],"t":[400,107]},"from":"a","to":"b","port":2}]})";
  assert(logic_compile_json(layout,0,def,&error));
  assert(def.node_count==2&&def.link_count==1&&def.links[0].from==0&&def.links[0].to==1&&def.links[0].port==2);
  assert(def.nodes[1].first_incoming==0);
  size_t layout_bytes=0;for(auto item:live)layout_bytes+=item.second;
  assert(layout_bytes==sizeof(LogicRtNode)*2+sizeof(LogicRtLink));logic_def_release(def);assert(live.empty());
  assert(!logic_compile_json(graph(65,0),0,def,&error)&&error==LogicSaveError::NodesLimit&&live.empty());
  assert(!logic_compile_json(graph(64,129),0,def,&error)&&error==LogicSaveError::LinksLimit&&live.empty());
  fail_alloc=true;assert(!logic_compile_json(graph(2,1),0,def,&error)&&error==LogicSaveError::Memory);fail_alloc=false;
  allocations_before_failure=1;assert(!logic_compile_json(graph(2,1),0,def,&error)&&error==LogicSaveError::Memory&&live.empty());allocations_before_failure=-1;
  assert(!logic_compile_json(graph(0,1),0,def,&error)&&error==LogicSaveError::InvalidGraph&&live.empty());
  String outputs="{\"nodes\":[";
  for(unsigned i=0;i<33;++i){if(i)outputs+=',';outputs+="{\"id\":\"o"+std::to_string(i)+"\",\"type\":\"output\",\"signal\":\"uni:80:switch:"+std::to_string(i)+"\"}";}
  outputs+="],\"links\":[]}";
  assert(!logic_compile_json(outputs,0,def,&error)&&error==LogicSaveError::OutputsLimit&&live.empty());
  assert(!logic_compile_json("{\"nodes\":[7],\"links\":[]}",0,def,&error)&&error==LogicSaveError::InvalidGraph);
  assert(logic_compile_json("{\"nodes\":[{\"id\":\"a\",\"type\":\"input\",\"name\":\"quoted \\\"nodes\\\" and [brackets]\"}],\"links\":[]}",0,def,&error));logic_def_release(def);
  for(bool filesystem:{true,false}){
    logic_fs_ready=filesystem;web.arguments={{"plain",graph(64,128)}};web_handle_logic_json();
    assert(web.status==200&&cached.node_count==64&&cached.link_count==128);
    const auto good=disk_data;const unsigned before=writes;
    for(auto bad:std::vector<std::pair<String,const char*>>{{graph(65,0),"Bausteine"},{graph(64,129),"Verbindungen"},{outputs,"Ausgangsziele"}}){
      web.arguments={{"plain",bad.first}};web_handle_logic_json();assert(web.status==400&&web.response.indexOf(bad.second)>=0);
      assert(disk_data==good&&writes==before&&cached.node_count==64&&cached.link_count==128);
    }
    fail_alloc=true;web.arguments={{"plain",graph(2,1)}};web_handle_logic_json();assert(web.status==503&&disk_data==good);fail_alloc=false;
    fail_storage=true;web_handle_logic_json();assert(web.status==507&&disk_data==good);fail_storage=false;
    web.arguments={{"plain",layout}};web_handle_logic_json();assert(web.status==200&&disk_data==layout&&cached.links[0].port==2);
    logic_def_release(cached);assert(live.empty());
  }
  std::printf("PASS: actual logic compiler/save/API: 64 nodes/128 links; exact allocations, empty/old graphs, capacity errors before writes, low memory and storage failures; max graph %zu bytes / peak %zu bytes\n",sizeof(LogicRtNode)*64+sizeof(LogicRtLink)*128,peak_bytes);
}
