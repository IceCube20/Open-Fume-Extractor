#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#include "HostedC6Firmware.h"
struct IO {
  uint32_t clock=0,slave=0x020C0C,host=ofe_c6::VERSION,count=0,fail_at=0,lose_at=0,expected_size=ofe_c6::IMAGE_SIZE;
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
  bool end(){++ended;assert(count==expected_size);return end_ok;}
  bool activate(){++activated;assert(ended==1 && marker==2);return activate_ok;}
  void progress(uint32_t n){assert(count==n);}
  void yield(){clock+=timeout?180000:1;}
};
int main(){
  for(bool poe:{false,true})for(bool dev:{false,true})for(uint16_t revision:{100,301,302,303})for(uint32_t flash:{8U*1024*1024,16U*1024*1024,32U*1024*1024})
    assert(ofe_c6::developerBoardEligible(poe,dev,revision,flash)==(dev && flash>=16U*1024*1024 && (poe?revision>=300:revision<300)));
  ofe_c6::DeveloperImage selected;selected.version=0x020C0B;selected.size=512;selected.blob[0]=1;
  for(unsigned i=0;i<40;++i)selected.commit[i]='a';
  std::vector<uint8_t> selected_bytes(512);
  for(bool authorized:{false,true})for(bool known:{false,true})for(uint32_t version:{0U,0x020500U,0x020C0BU,0x020D03U,0x030000U}) {
    IO custom;custom.slave=version;custom.version_ok=known;custom.expected_size=512;
    const auto result=ofe_c6::transfer(custom,selected_bytes.data(),512,ofe_c6::TrialConfirmed,false,authorized,false,&selected);
    assert(result.committed==(authorized && known && version>=0x020600 && version<=0x02FFFF && version!=selected.version));
  }
  IO strict;assert(ofe_c6::transfer(strict,selected_bytes.data(),512,ofe_c6::StrictVersion,false,true,false,&selected).error==ofe_c6::TrialDenied && !strict.begun);
  selected.size=ofe_c6::DEVELOPER_IMAGE_LIMIT+1;assert(!ofe_c6::developerImageValid(selected));selected.size=512;
  selected.commit[0]='Z';assert(!ofe_c6::developerImageValid(selected));
  assert(ofe_c6::trialBoardEligible(true,true,302,32U*1024*1024));
  for (bool poe:{false,true}) for(bool dev:{false,true}) for(uint16_t rev:{100,301,302,303}) for(uint32_t flash:{16U*1024*1024,32U*1024*1024})
    assert(ofe_c6::trialBoardEligible(poe,dev,rev,flash)==(poe&&dev&&rev==302&&flash==32U*1024*1024));
  std::vector<uint8_t> trial_image(ofe_c6::TEST_SIZE);
  for(bool test_image:{false,true}) {
    IO trial;trial.slave=test_image?ofe_c6::VERSION:ofe_c6::TEST_VERSION;
    trial.expected_size=test_image?ofe_c6::TEST_SIZE:ofe_c6::IMAGE_SIZE;
    auto result=ofe_c6::transfer(trial,trial_image.data(),trial.expected_size,ofe_c6::TrialConfirmed,test_image,true);
    assert(result.committed&&trial.activated==1&&trial.count==trial.expected_size);
    for(bool auth:{false,true}) for(bool known:{false,true}) for(uint32_t version:{0U,0x020500U,0x020C0CU,ofe_c6::VERSION,ofe_c6::TEST_VERSION,0x020D02U}) {
      trial=IO();trial.slave=version;trial.version_ok=known;
      trial.expected_size=test_image?ofe_c6::TEST_SIZE:ofe_c6::IMAGE_SIZE;
      result=ofe_c6::transfer(trial,trial_image.data(),trial.expected_size,ofe_c6::TrialConfirmed,test_image,auth);
      const bool permitted=auth&&known&&version>=0x020600&&version<=0x02ffff&&version!=(test_image?ofe_c6::TEST_VERSION:ofe_c6::VERSION);
      assert(result.committed==permitted&&trial.begun==unsigned(permitted));
    }
    for(unsigned fault=0;fault<6;++fault){
      trial=IO();trial.slave=test_image?ofe_c6::VERSION:ofe_c6::TEST_VERSION;trial.expected_size=test_image?ofe_c6::TEST_SIZE:ofe_c6::IMAGE_SIZE;
      if(fault==0)trial.fail_at=2048;if(fault==1)trial.end_ok=false;if(fault==2)trial.activate_ok=false;
      if(fault==3)trial.lose_at=2048;if(fault==4)trial.timeout=true;if(fault==5)trial.begin_ok=false;
      result=ofe_c6::transfer(trial,trial_image.data(),trial.expected_size,ofe_c6::TrialConfirmed,test_image,true);
      assert(!result.committed&&result.recovery);if(fault!=2)assert(!trial.activated);
    }
  }
  IO no_auth;auto denied=ofe_c6::transfer(no_auth,trial_image.data(),ofe_c6::TEST_SIZE,ofe_c6::StrictVersion,true,true);
  assert(denied.error==ofe_c6::TrialDenied&&!no_auth.begun);
  for(bool poe:{false,true})for(uint16_t revision:{100,301,302,303})for(uint32_t flash:{16U*1024*1024,32U*1024*1024})
    assert(ofe_c6::approved2131Board(poe,revision,flash)==(poe&&revision==302&&flash==32U*1024*1024));
  IO approved;approved.slave=ofe_c6::VERSION;approved.expected_size=ofe_c6::TEST_SIZE;
  auto upgraded=ofe_c6::transfer(approved,trial_image.data(),ofe_c6::TEST_SIZE,ofe_c6::StrictVersion,true,false,true);
  assert(upgraded.committed&&approved.activated==1);
  approved=IO();approved.slave=ofe_c6::TEST_VERSION;
  upgraded=ofe_c6::transfer(approved,trial_image.data(),ofe_c6::TEST_SIZE,ofe_c6::StrictVersion,true,false,true);
  assert(upgraded.error==ofe_c6::AlreadyCurrent&&!approved.begun);
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
