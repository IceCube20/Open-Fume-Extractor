#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#include "HostedC6Firmware.h"
struct IO {
  uint32_t clock=0,slave=0x020C0C,host=ofe_c6::VERSION,count=0,fail_at=0,lose_at=0;
  bool acquire_ok=true,version_ok=true,begin_ok=true,end_ok=true,activate_ok=true,pending_ok=true,timeout=false;
  unsigned acquired=0,begun=0,ended=0,activated=0,marker=0;
  bool lan() {return !lose_at || count<lose_at;}
  bool acquire(){++acquired;return acquire_ok;}
  bool version(uint32_t& out){out=slave;return version_ok;}
  uint32_t hostVersion(){return host;}
  bool pending(uint8_t p){marker=p;return pending_ok;}
  uint32_t now(){return clock;}
  bool begin(){++begun;return begin_ok;}
  bool write(const uint8_t*,uint32_t size){assert(size<=2048 && size>0);if(fail_at&&count>=fail_at)return false;count+=size;return true;}
  bool end(){++ended;assert(count==ofe_c6::IMAGE_SIZE);return end_ok;}
  bool activate(){++activated;assert(ended==1 && marker==2);return activate_ok;}
  void progress(uint32_t n){assert(count==n);}
  void yield(){clock+=timeout?180000:1;}
};
int main(){
  std::vector<uint8_t> image(ofe_c6::IMAGE_SIZE);
  IO io;auto result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));
  assert(result.error==ofe_c6::Ok&&result.committed&&io.activated==1&&io.count==image.size());
  for(auto version:{uint32_t(0),uint32_t(0x020500),ofe_c6::VERSION,uint32_t(ofe_c6::VERSION+1)}){
    io=IO();io.slave=version;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(!result.committed&&!result.recovery&&io.begun==0&&io.activated==0);
  }
  io=IO();io.acquire_ok=false;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::SlaveUnavailable&&io.begun==0);
  io=IO();io.host++;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::HostMismatch&&io.begun==0);
  io=IO();io.pending_ok=false;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::PendingFailed&&io.begun==0);
  io=IO();io.begin_ok=false;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.recovery&&io.activated==0&&io.ended==0);
  for(auto position:{2048U,ofe_c6::IMAGE_SIZE-2048U}){
    io=IO();io.fail_at=position;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::WriteFailed&&result.recovery&&io.ended==0&&io.activated==0);
    io=IO();io.lose_at=position;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::NoLan&&result.recovery&&io.ended==0&&io.activated==0);
  }
  io=IO();io.end_ok=false;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::EndFailed&&io.activated==0);
  io=IO();io.activate_ok=false;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::ActivateFailed&&!result.committed);
  io=IO();io.timeout=true;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::TimedOut&&io.ended==0&&io.activated==0);
  io=IO();io.clock=0xFFFFFFF0U;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.committed);
  io=IO();result=ofe_c6::transfer(io,image.data(),1);assert(result.error==ofe_c6::Size&&io.acquired==0);
  std::cout<<"PASS: actual C6 transfer policy, exact version, markers, chunking, no activation after partial/failing upload, LAN loss, timeout and rollover\n";
}
