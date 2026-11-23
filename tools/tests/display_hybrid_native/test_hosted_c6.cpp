#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#include "HostedC6Firmware.h"
struct IO {
  uint32_t clock=0,slave=0x020C0C,host=ofe_c6::VERSION,count=0,fail_at=0,lose_at=0;
  bool acquire_ok=true,version_ok=true,begin_ok=true,end_ok=true,activate_ok=true,pending_ok=true,timeout=false;
  bool legacy_eligible=true,control_ok=true;
  unsigned control_checks=0;
  unsigned acquired=0,begun=0,ended=0,activated=0,marker=0;
  bool lan() {return !lose_at || count<lose_at;}
  bool acquire(){++acquired;return acquire_ok;}
  bool version(uint32_t& out){out=slave;return version_ok;}
  bool legacyVersionEligible(){return legacy_eligible;}
  bool controlReady(){++control_checks;return control_ok;}
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
  io=IO();io.version_ok=false;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()));assert(result.error==ofe_c6::VersionUnavailable&&io.begun==0&&io.activated==0&&!result.recovery);
  assert(io.control_checks==0);
  for(bool known:{false,true}) {
    io=IO();io.version_ok=known;io.slave=0x020500;
    result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()),ofe_c6::LegacyConfirmed);
    assert(result.committed&&io.control_checks==1&&io.count==image.size()&&io.activated==1);
  }
  io=IO();io.version_ok=false;io.control_ok=false;
  result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()),ofe_c6::LegacyConfirmed);
  assert(result.error==ofe_c6::ControlUnavailable&&io.control_checks==1&&io.begun==0&&io.activated==0);
  io=IO();io.version_ok=false;io.legacy_eligible=false;
  result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()),ofe_c6::LegacyConfirmed);
  assert(result.error==ofe_c6::VersionUnavailable&&io.control_checks==0&&io.begun==0);
  for(auto version:{uint32_t(0),ofe_c6::VERSION,uint32_t(ofe_c6::VERSION+1)}) {
    io=IO();io.slave=version;result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()),ofe_c6::LegacyConfirmed);
    assert(!result.committed&&io.begun==0&&io.activated==0);
  }
  for(unsigned fault=0;fault<6;++fault) {
    io=IO();io.version_ok=false;
    if(fault==0)io.fail_at=2048;
    if(fault==1)io.end_ok=false;
    if(fault==2)io.activate_ok=false;
    if(fault==3)io.lose_at=2048;
    if(fault==4)io.timeout=true;
    if(fault==5)io.begin_ok=false;
    result=ofe_c6::transfer(io,image.data(),uint32_t(image.size()),ofe_c6::LegacyConfirmed);
    assert(!result.committed&&result.recovery);
    if(fault!=2)assert(io.activated==0);
    else assert(result.error==ofe_c6::ActivateFailed&&io.marker==2);
  }
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
