#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "Rs485PeripheralBus.h"
using namespace jbc_rs485;
static constexpr uint8_t DATA=4;
static uint32_t esp_random() { return 0x12345678u; }

// Only peer lookup and network I/O are adapted; routing comes from production.
class MasterDisplayWifi {
public:
  struct Peer { uint64_t uid=123; uint8_t addr=0x40; bool connected=true; };
  Peer peers_[1];
  bool online=true,physical_=false,firmware_wifi_=false,send_ok=true;
  bool firmware_bulk_available_=false,firmware_bulk_confirmed_=false;
  uint32_t firmware_bulk_request_=0;
  std::atomic<uint8_t> firmware_addr_{0};
  uint64_t firmware_uid_=0;
  unsigned sends=0;
  bool active(uint8_t addr) const { return online && addr==peers_[0].addr; }
  Peer* peer(uint8_t addr) { return addr==peers_[0].addr ? &peers_[0] : nullptr; }
  bool send(Peer&,uint8_t,const Frame*) { ++sends; return send_ok; }
  void beginFirmware(uint8_t addr);
  bool route(const Frame&);
};
#include "generated_master_route.inc.h"

int main() {
  assert(strcmp(ofe_module_default_name(MODULE_JBC_BUS), "JBC FAE Bus")==0);
  assert(strcmp(ofe_module_default_name(MODULE_JBC_USB), "JBC USB")==0);
  assert(strcmp(ofe_module_default_name(MODULE_FAN_IO_PRO), "Fan/IO Pro")==0);
  assert(strcmp(ofe_module_default_name(MODULE_WELLER_ZERO_SMOG), "Weller Zero Smog Bus")==0);
  assert(strcmp(ofe_module_default_name(MODULE_DISPLAY, CAP_DISPLAY_320X480), "Display 320x480")==0);
  assert(strcmp(ofe_module_default_name(MODULE_DISPLAY, CAP_DISPLAY_800X480), "Display 800x480")==0);
  assert(strcmp(ofe_module_default_name(MODULE_UNIVERSAL_RS232), "Universal RS232 Bridge")==0);
  assert(strcmp(ofe_module_default_name(MODULE_MODBUS_RTU), "Modbus RTU Bridge")==0);
  MasterDisplayWifi master;
  Frame frame; frame.dst=0x40; frame.cmd=CMD_FW_CHUNK;
  master.beginFirmware(0x40);
  assert(master.firmware_wifi_ && master.route(frame) && master.sends==1);
  master.online=false; master.peers_[0].connected=false;
  assert(!master.route(frame) && master.sends==1); // allow bounded physical fallback
  master.physical_=true;
  assert(!master.route(frame) && master.sends==1);
  master.online=true; master.peers_[0].connected=true; master.peers_[0].uid=456;
  assert(!master.route(frame) && master.sends==1); // different device at same address
  master.peers_[0].uid=123;
  assert(master.route(frame) && master.sends==2);
  master.firmware_addr_.store(0); master.online=false; master.physical_=false;
  master.beginFirmware(0x40);
  assert(!master.firmware_wifi_ && !master.route(frame));
  master.online=true;
  assert(!master.route(frame)); // wired transfer cannot switch mid-image
  master.firmware_addr_.store(0);
  assert(master.route(frame) && master.sends==3);
  master.send_ok=false;
  assert(!master.route(frame) && master.sends==4); // failed UDP send remains retryable
  master.send_ok=true;
  master.physical_=true;
  assert(!master.route(frame)); // normal physical probes still work
  puts("PASS: actual master OTA router pins transport and device identity, permits bounded physical fallback, and resumes normal routing after release.");
}
