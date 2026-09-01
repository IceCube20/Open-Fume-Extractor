#include <cassert>
#include <cstdio>
#include <vector>
#include "OfeDisplayTunnel.h"
#include "../../../Module/DisplayModule_320x480/src/OfeDisplayMemory.h"
using namespace ofe_wifi;
class SerialStub : public Stream {
public:
  std::vector<uint8_t> bytes; size_t at=0;
  size_t write(uint8_t b) override { bytes.push_back(b); return 1; }
  int available() override { return (int)(bytes.size()-at); }
  int read() override { return bytes[at++]; }
  void flush() override {}
};
int main() {
  using namespace ofe_display_memory;
  assert(!fits(17*1024,16*1024,RUNTIME_RESERVE));
  assert(!fits(160*1024,115200,RUNTIME_RESERVE));
  assert(!fits(0,SIZE_MAX,RUNTIME_RESERVE));
  fake_heap::reset(160*1024,120*1024);
  uint8_t* draw=allocateDraw(80*1024,RUNTIME_RESERVE);
  assert(draw && fake_heap::free_bytes==80*1024);
  heap_caps_free(draw);
  fake_heap::reset(160*1024,8*1024); // enough total RAM, fragmented
  assert(!allocateDraw(16*1024,RUNTIME_RESERVE));
  fake_heap::reset(80*1024,80*1024,64); // allocator overhead must not eat the reserve
  assert(!allocateDraw(16*1024,RUNTIME_RESERVE));
  assert(fake_heap::free_bytes==80*1024 && fake_heap::allocations==1 && fake_heap::releases==1);
  fake_heap::reset(112*1024,112*1024);
  assert(!allocateDraw(16*1024,STARTUP_RESERVE)); // retain memory for a WiFi retry
  uint8_t key[16]; memset(key,0x0b,sizeof(key));
  // Independent RFC 2202/4231-family SHA-256 HMAC vector (16-byte key).
  uint8_t digest[32]; char hexDigest[65];
  assert(mac(key,(const uint8_t*)"Hi There",8,digest)); hex(digest,32,hexDigest);
  assert(strcmp(hexDigest,"492CE020FE2534A5789DC3848806C78F4F6711397F08E7E7A12CA5A4483C8AA6")==0);
  for (unsigned size=0;size<=MAX_PAYLOAD;++size) {
    Frame input; input.dst=0x40; input.src=1; input.seq=27; input.cmd=CMD_DISPLAY_STATUS; input.len=(uint8_t)size;
    for(unsigned i=0;i<size;++i) input.payload[i]=(uint8_t)i;
    Packet p; p.kind=DATA; p.mode=AUTOMATIC; p.uid=0x4000000012345678ULL; p.client=42; p.server=99; p.counter=8;
    setFrame(p,input);
    uint8_t raw[PACKET_MAX+1]; size_t n=encode(p,key,raw);
    assert(n==HEADER+5+size+TAG);
    Packet out; Frame decoded; assert(decode(raw,n,key,out)); assert(getFrame(out,decoded));
    assert(decoded.len==size && decoded.seq==27 && !memcmp(decoded.payload,input.payload,size));
    assert(inSession(out,p.uid,42,99,AUTOMATIC,7));
    assert(!inSession(out,p.uid,42,99,AUTOMATIC,8)); // duplicate
    assert(!inSession(out,p.uid,42,99,AUTOMATIC,9)); // reordered old packet
    assert(!inSession(out,p.uid,42,100,AUTOMATIC,0)); // old session
    assert(!inSession(out,p.uid,43,99,AUTOMATIC,0));
    assert(!inSession(out,p.uid+1,42,99,AUTOMATIC,0));
    assert(!inSession(out,p.uid,42,99,WIRELESS,0));
    for(size_t i=0;i<n;++i) { raw[i]^=1; assert(!decode(raw,n,key,out)); raw[i]^=1; }
    for(size_t i=0;i<n;++i) assert(!decode(raw,i,key,out));
    assert(!decode(raw,n+1,key,out));
    key[0]^=1; assert(!decode(raw,n,key,out)); key[0]^=1;
    p.body[4]=MAX_PAYLOAD; p.length=5; assert(!getFrame(p,decoded));
  }
  Config config; assert(valid(config) && !configured(config));
  strcpy(config.ssid,"SSID"); strcpy(config.host,"192.168.1.2"); memcpy(config.key,key,16); assert(configured(config));
  config.mode=3; assert(!valid(config)); config.mode=AUTOMATIC;
  memset(config.password,'x',sizeof(config.password)); assert(!valid(config));
  char encoded[33]; hex(key,16,encoded); uint8_t decoded[16];
  assert(unhex(encoded,decoded,16) && equal(decoded,key,16));
  encoded[0]='X'; assert(!unhex(encoded,decoded,16)); assert(!unhex("01",decoded,16));
  SerialStub serial; Link link(serial);
  Frame sent; sent.dst=0x40; sent.src=1; sent.seq=42; sent.cmd=CMD_PING; sent.len=2; sent.payload[0]=SOF; sent.payload[1]=ESC;
  link.send(sent); Frame read; assert(link.poll(read) && read.seq==42 && read.len==2);
  size_t count=serial.bytes.size(); int routed=0;
  link.setTransportHooks(&routed,[](void* ctx,const Frame&){ ++*(int*)ctx; return true; },nullptr,nullptr);
  link.send(sent); assert(routed==1 && serial.bytes.size()==count); // no mirrored command
  link.sendPhysical(sent); assert(serial.bytes.size()>count); // explicit cable probe
  puts("PASS: draw-buffer headroom/fragmentation/allocation rollback; 193 payload sizes, byte tampering, truncation, wrong keys, replay/session checks, config validation, serial compatibility and exclusive routing.");
}
