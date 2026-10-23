static int saves=0;
static ofe_wifi::Config saved;
static uint8_t stored_status=1;
static int stored_checks=0;
static bool save(const ofe_wifi::Config& c) { ++saves; saved=c; return true; }
static uint8_t stored(const ofe_wifi::Config&) { ++stored_checks; return stored_status; }

static void connect(OfeDisplayBle& ble) {
  ble_gap_event e; e.type=BLE_GAP_EVENT_CONNECT; OfeDisplayBle::gap(&e,&ble);
}
static void confirm(OfeDisplayBle& ble,bool accept) {
  ble_gap_event e; e.type=BLE_GAP_EVENT_PASSKEY_ACTION; OfeDisplayBle::gap(&e,&ble);
  const auto view=ble.view();
  assert(view.phase==ofe_ble::CONFIRM);
  assert(!ble.approve(view.token+1,view.pin,accept));
  assert(!ble.approve(view.token,view.pin+1,accept));
  assert(ble.approve(view.token,view.pin,accept));
  assert(!ble.approve(view.token,view.pin,accept));
  const int before=inject_count;
  ble.deliverDecision(); assert(inject_count==before+1 && injected_accept==accept);
  ble.deliverDecision(); assert(inject_count==before+1);
}
static int write(OfeDisplayBle& ble,const std::vector<uint8_t>& data,uint16_t handle=1) {
  os_mbuf buf; buf.bytes=data;
  ble_gatt_access_ctxt ctx{&ble.chars_[1],BLE_GATT_ACCESS_OP_WRITE_CHR,&buf};
  return OfeDisplayBle::access(handle,0,&ctx,&ble);
}
int main() {
  constexpr uint64_t uid=0x4000123456789ABC;
  OfeDisplayBle ble; ble.configure(uid,1,nullptr,save,stored);
  assert(ble.start()); assert(!ble.start()); // No worker/host reentry.
  assert(ble.initRadio()); assert(ble.advertise());
  assert(manufacturer.size()==15 && jbc_rs485::get_u64_le(manufacturer.data()+6)==uid);
  ofe_master_ble::Candidate candidate;
  for (uint8_t kind=0;kind<=3;++kind) {
    manufacturer[14]=kind;
    assert(ofe_master_ble::parseAdvertisement(manufacturer.data(),manufacturer.size(),candidate));
    assert(candidate.uid==uid && candidate.kind==kind);
  }
  manufacturer[14]=4;
  assert(!ofe_master_ble::parseAdvertisement(manufacturer.data(),manufacturer.size(),candidate));
  manufacturer[14]=3;
  assert(!ofe_master_ble::parseAdvertisement(manufacturer.data(),14,candidate));
  assert(ble_hs_cfg.sm_mitm && ble_hs_cfg.sm_sc && !ble_hs_cfg.sm_bonding);
  ofe_wifi::Config c; strcpy(c.ssid,"test"); strcpy(c.host,"192.0.2.1"); c.key[0]=1;
  std::vector<uint8_t> bytes(12+sizeof(c)); memcpy(bytes.data(),"OPB1",4);
  jbc_rs485::put_u64_le(bytes.data()+4,uid); memcpy(bytes.data()+12,&c,sizeof(c));
  connect(ble);
  assert(write(ble,bytes)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN && saves==0);
  confirm(ble,true);
  ble_gap_event enc; enc.type=BLE_GAP_EVENT_ENC_CHANGE;
  peer.sec_state={true,true,16}; OfeDisplayBle::gap(&enc,&ble);
  auto wrong=bytes; wrong[4]^=1;
  assert(write(ble,wrong)!=0 && saves==0);
  assert(write(ble,bytes,2)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN && saves==0);
  peer.sec_state.key_size=15; assert(write(ble,bytes)!=0 && saves==0);
  peer.sec_state.key_size=16;
  wrong=bytes; wrong.pop_back(); assert(write(ble,wrong)!=0 && saves==0);
  wrong=bytes; wrong[12]=99; assert(write(ble,wrong)!=0 && saves==0);
  wrong=bytes; wrong[14]=0; assert(write(ble,wrong)!=0 && saves==0); // Not from Master.
  wrong=bytes; wrong.resize(bytes.size()+1); assert(write(ble,wrong)!=0 && saves==0);
  assert(write(ble,bytes)==0 && saves==1 && !strcmp(saved.ssid,"test"));
  assert(ble.view().phase==ofe_ble::SAVING && ble.result_==1);
  assert(write(ble,bytes)==BLE_ATT_ERR_WRITE_NOT_PERMITTED && saves==1);
  ble.host_exited_=true; ble.finish(ofe_ble::DONE);
  assert(!ble.active() && hosted_stops==1 && hosted_wifi_active);
  assert(ble.start() && ble.initRadio()); connect(ble); confirm(ble,false);
  assert(ble.cancel_ && write(ble,bytes)!=0 && saves==1);
  ble.host_exited_=true; ble.finish(ofe_ble::CANCELLED);
  heap=100; assert(ble.start()); assert(!ble.initRadio());
  assert(ble.view().phase==ofe_ble::LOW_MEMORY && !ble.active()); heap=120000;
  pins_ready=false; assert(ble.start()); assert(!ble.initRadio()); pins_ready=true;
  for (int i=0;i<2;++i) for (int step=1;step<=5;++step) {
    ecc_self_test_result=-1010-i*10-step;
    const int before=hosted_stops;
    assert(ble.start()); assert(!ble.initRadio());
    assert(ble.view().phase==ofe_ble::FAILED && ble.view().error_code==ecc_self_test_result);
    assert(ble.view().error_phase==ofe_ble::STARTING && !ble.active());
    assert(hosted_stops==before+1 && hosted_wifi_active && saves==1);
  }
  ecc_self_test_result=0;
  assert(ble.start() && ble.initRadio());
  ble.host_exited_=true; deinit_error=1; ble.finish(ofe_ble::DONE);
  assert(ble.view().phase==ofe_ble::FAILED && !ble.start());
  deinit_error=0;

  OfeDisplayBle flow; flow.configure(uid,1,nullptr,save,stored);
  stored_status=0; stored_checks=0;
  inject_hook=[&]() { OfeDisplayBle::gap(&enc,&flow); };
  delay_hook=[&](unsigned ms) {
    if (flow.view().phase==ofe_ble::ADVERTISING) { connect(flow); confirm(flow,true); }
    else if (flow.view().phase==ofe_ble::AUTHENTICATING && flow.secured_) {
      assert(write(flow,bytes)==0);
    } else if (flow.view().phase==ofe_ble::SAVING) {
      assert(stored_checks>0 && flow.result_==1); // No success before verified storage.
      stored_status=1;
    }
    if (ms==2000) { assert(flow.result_==2); flow.host_exited_=true; }
  };
  assert(flow.start()); flow.run();
  assert(flow.view().phase==ofe_ble::DONE && !flow.active() && stored_checks>=2 && saves==2);
  delay_hook={}; inject_hook={};

  OfeDisplayBle failed; failed.configure(uid,3,nullptr,save,stored);
  assert(failed.start() && failed.initRadio()); connect(failed);
  failed.phase(ofe_ble::AUTHENTICATING);
  auto failure=enc; failure.enc_change.status=0x208;
  OfeDisplayBle::gap(&failure,&failed);
  assert(failed.failed_ && !failed.cancel_ && !failed.secured_);
  assert(failed.view().error_phase==ofe_ble::AUTHENTICATING && failed.view().error_code==0x208);
  assert(write(failed,bytes)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
  ble_gap_event disconnected; disconnected.type=BLE_GAP_EVENT_DISCONNECT;
  OfeDisplayBle::gap(&disconnected,&failed);
  assert(failed.view().error_code==0x208); // Cleanup must not hide the first cause.
  failed.host_exited_=true; failed.finish(ofe_ble::FAILED);

  OfeDisplayBle late; late.configure(uid,3,nullptr,save,stored);
  now=0; bool connected_late=false;
  delay_hook=[&](unsigned) {
    if (!connected_late) {
      now=119990; connect(late); connected_late=true;
      assert(late.attempt_started_==now);
    } else {
      assert(now>=120015 && late.view().phase==ofe_ble::CONNECTING);
      late.cancel(); late.host_exited_=true;
    }
  };
  assert(late.start()); late.run();
  assert(late.view().phase==ofe_ble::CANCELLED && connected_late);
  delay_hook={};

  std::vector<uint8_t> image(24,0); image[0]=0xE9; image[1]=4; image[12]=0x12;
  const char* marker="OFE_FW_SIG:v1;target=DISPLAY_P4_800X480;version=1.0.0alpha-P4Prep;";
  image.insert(image.end(),marker,marker+strlen(marker));
  for (size_t split=0;split<=image.size();++split) {
    OfeP4FirmwareGuard guard;
    assert(guard.append(image.data(),split)); assert(guard.append(image.data()+split,image.size()-split));
    assert(guard.complete()); guard.reset(); assert(!guard.complete());
  }
  OfeP4FirmwareGuard guard; image[12]=9;
  assert(!guard.append(image.data(),image.size()) && !guard.complete());
  guard.reset(); image[12]=0x12; image.resize(24);
  assert(guard.append(image.data(),image.size()) && !guard.complete());
  guard.reset(); assert(!guard.append(nullptr,1));
  std::cout<<"PASS: P4 BLE authentication, UID/length/security checks, replay, cancel, low RAM, cleanup; streaming OTA guard\n";
}
