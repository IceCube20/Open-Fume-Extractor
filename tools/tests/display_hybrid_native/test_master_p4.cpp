ble_uuid_any_t* test_uuids=nullptr;
static ofe_wifi::Config sampleConfig() {
  ofe_wifi::Config c{}; strcpy(c.ssid,"test"); strcpy(c.host,"192.0.2.1"); c.key[0]=1; return c;
}
static bool provide(uint64_t,ofe_wifi::Config& c) { c=sampleConfig(); return true; }
static void ready(OfeDisplayBle& ble) {
  now=0; peer.conn_handle=1; peer.sec_state={true,true,16}; written_configs=0;
  remote_uid=0x40001234567890ABULL; persistence_result=2; mtu=247;
  drop_operation=bad_characteristics=false; delay_hook=nullptr; inject_hook=nullptr;
  ble.configure(0,0,provide); test_uuids=ble.uuids_;
  for (int i=0;i<4;++i) ble_uuid_from_str(&ble.uuids_[i],"");
  ble.target_.uid=remote_uid; strcpy(ble.target_.address,"AA:BB:CC:DD:EE:FF");
  ble.config_=sampleConfig(); ble.active_=true; ble.view_.active=true;
  delay_hook=[&ble](unsigned) {
    if (ble.view().phase==ofe_ble::CONFIRM) {
      auto v=ble.view(); assert(!ble.approve(v.token+1,v.pin,true));
      assert(!ble.approve(v.token,v.pin+1,true)); assert(ble.approve(v.token,v.pin,true));
    }
  };
  inject_hook=[] {
    ble_gap_event e{}; e.type=BLE_GAP_EVENT_ENC_CHANGE; central_gap(&e,central_arg);
  };
}
static void testHeaders() {
  uint8_t image[32]{}; image[0]=0xE9; image[1]=4; image[12]=0x12;
  for (const uint8_t expected : {uint8_t(9), uint8_t(18)}) {
    for (const uint8_t actual : {uint8_t(9), uint8_t(18)}) {
      image[12] = actual;
      for (size_t split = 0; split <= sizeof(image); ++split) {
        MasterFirmwareHeader header(expected);
        const bool first = header.append(image, split);
        const bool second = header.append(image + split, sizeof(image) - split);
        assert((first && second && header.complete()) == (expected == actual));
      }
    }
  }
  image[12] = 0x12;
  for (size_t split=0;split<=24;++split) {
    MasterFirmwareHeader h(0x12); assert(h.append(image,split)); assert(!h.complete() || split==24);
    assert(h.append(image+split,sizeof(image)-split)); assert(h.complete());
  }
  MasterFirmwareHeader s3(9); assert(!s3.append(image,sizeof(image))); assert(!s3.complete());
  s3.reset(); image[12]=9; assert(s3.append(image,1)); assert(s3.append(image+1,23)); assert(s3.complete());
  s3.reset(); assert(s3.append(image,23)); assert(!s3.complete());
  assert(!s3.append(nullptr,1)); assert(!s3.append(image,24));
  s3.reset(); image[0]=0; assert(!s3.append(image,24));
  s3.reset(); image[0]=0xE9; image[1]=17; assert(!s3.append(image,24));
  image[1]=4; image[12]=18;
  for (const bool production:{false,true}) {
    const uint16_t minimum=production?301:0,maximum=production?399:199;
    image[15]=uint8_t(minimum); image[16]=uint8_t(minimum>>8);
    image[17]=uint8_t(maximum); image[18]=uint8_t(maximum>>8);
    for(const uint16_t revision:{uint16_t(100),uint16_t(103),uint16_t(300),uint16_t(301),uint16_t(302),uint16_t(399),uint16_t(400)}) {
      for(size_t split=0;split<=sizeof(image);++split) {
        MasterFirmwareHeader header(18,revision);
        const bool a=header.append(image,split),b=header.append(image+split,sizeof(image)-split);
        assert((a&&b&&header.complete())==(revision>=minimum&&revision<=maximum));
      }
    }
  }
  MasterFirmwareHeader revision(18);
  revision.reset(302); assert(revision.append(image,24));
  revision.reset(103); assert(!revision.append(image,24));
  image[15]=0;image[16]=0;image[17]=0;image[18]=0;image[14]=3;
  image[12]=9;
  MasterFirmwareHeader legacy(9,302); assert(legacy.append(image,24));
  legacy.reset(199); assert(!legacy.append(image,24));
  image[12]=18;
  image[14]=0; image[15]=0x90; image[16]=1; image[17]=0x2D;image[18]=1;
  revision.reset(400); assert(!revision.append(image,24));
  image[15]=image[16]=0;image[17]=image[18]=255;
  revision.reset(302); assert(!revision.append(image,24));
  revision.reset(103); assert(!revision.append(image,24));
  std::cout<<"PASS: PRE-V3/V3 revision ranges, V3.2, fragmented headers, legacy minimum and per-upload reset\n";
  std::cout<<"PASS: split image headers, wrong-chip/malformed/null/truncated rejection and reset\n";
}
int main() {
  testHeaders();
  { OfeDisplayBle ble; ready(ble); assert(ble.pair()); assert(written_configs==1);
    ofe_wifi::Config c{};
    assert(ofe_ble::readConfig(written_payload.data(),written_payload.size(),ble.target_.uid,c)); }
  { OfeDisplayBle ble; ready(ble); ++remote_uid; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); peer.sec_state.authenticated=false; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); peer.sec_state.key_size=15; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); mtu=23; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); drop_operation=true; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); bad_characteristics=true; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); persistence_result=3; assert(!ble.pair()); assert(written_configs==1); }
  { OfeDisplayBle ble; ready(ble);
    delay_hook=[&ble](unsigned) { auto v=ble.view(); if (v.phase==ofe_ble::CONFIRM) assert(ble.approve(v.token,v.pin,false)); };
    inject_hook=nullptr; assert(!ble.pair()); assert(written_configs==0); }
  { OfeDisplayBle ble; ready(ble); ble.view_.phase=ofe_ble::SCANNING;
    uint8_t bytes[15]={0xFF,0xFF,'O','F','E','1'};
    ble_gap_event e{}; e.type=BLE_GAP_EVENT_DISC; e.disc.data=bytes; e.disc.length_data=sizeof(bytes);
    for (unsigned i=0;i<12;++i) { jbc_rs485::put_u64_le(bytes+6,0x4000000000000000ULL+i); OfeDisplayBle::gap(&e,&ble); }
    assert(ble.view().count==ofe_ble::MAX_DEVICES);
    auto v=ble.view(); assert(!ble.select(0,v.token+1)); assert(!ble.select(255,v.token));
  }
  delay_hook=nullptr; inject_hook=nullptr;
  std::cout<<"PASS: actual P4 central pairing, matching UID, confirmation tokens, encryption/key size, MTU, timeout, GATT, persistence, reject and bounded discovery\n";
}
