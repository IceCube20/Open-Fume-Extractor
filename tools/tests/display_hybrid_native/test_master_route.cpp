#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "Rs485PeripheralBus.h"
#include "../../../OpenFumeExtractorMaster/src/WifiOtaDiagnostics.h"
using namespace jbc_rs485;
static constexpr uint8_t DATA=4;
static uint32_t esp_random() { return 0x12345678u; }
static uint32_t now_ms=0;
static uint32_t millis() { return now_ms; }

// Only peer lookup and network I/O are adapted; routing comes from production.
class MasterDisplayWifi {
public:
  struct Peer { uint64_t uid=123; uint8_t addr=0x40; bool connected=true; };
  Peer peers_[1];
  bool online=true,physical_=false,firmware_wifi_=false,send_ok=true;
  bool firmware_bulk_available_=false,firmware_bulk_confirmed_=false;
  bool firmware_bulk_retry_pending_=false;
  uint32_t firmware_bulk_retry_started_ms_=0;
  uint32_t firmware_bulk_retry_delay_ms_=2000;
  uint32_t firmware_bulk_request_=0;
  ofe_wifi_ota::Diagnostics firmware_bulk_diagnostics_;
  std::atomic<uint8_t> firmware_addr_{0};
  uint64_t firmware_uid_=0;
  unsigned sends=0;
  unsigned receives=0, queued_controls=0;
  bool queued_reply=false, received_packet_=false;
  bool receive(Frame&) {
    ++receives;
    received_packet_=queued_controls || queued_reply;
    if (queued_controls) { --queued_controls; return false; }
    const bool response=queued_reply;
    queued_reply=false;
    return response;
  }
  static bool pollHook(void*, Frame&);
  bool active(uint8_t addr) const { return online && addr==peers_[0].addr; }
  Peer* peer(uint8_t addr) { return addr==peers_[0].addr ? &peers_[0] : nullptr; }
  bool send(Peer&,uint8_t,const Frame*) { ++sends; return send_ok; }
  void beginFirmware(uint8_t addr);
  void endFirmware();
  bool route(const Frame&);
#include "generated_master_bulk_policy.inc.h"
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
  assert(!MasterDisplayWifi::pollHook(&master, frame) && master.receives==1);
  master.receives=0; master.queued_controls=2; master.queued_reply=true;
  assert(MasterDisplayWifi::pollHook(&master, frame) && master.receives==3);
  master.receives=0; master.queued_controls=10; master.queued_reply=true;
  assert(!MasterDisplayWifi::pollHook(&master, frame) && master.receives==4);
  assert(!MasterDisplayWifi::pollHook(&master, frame) && master.receives==8);
  assert(MasterDisplayWifi::pollHook(&master, frame) && master.receives==11);
  master.firmware_bulk_diagnostics_.note(ofe_wifi_ota::Tx);
  master.beginFirmware(0x40);
  assert(master.firmware_bulk_diagnostics_.snapshot().events[ofe_wifi_ota::Tx]==0);
  assert(master.firmware_wifi_ && master.route(frame) && master.sends==1);
  assert(master.firmwareBulkAvailable(0x40) && !master.firmwareBulkConfirmed(0x40));
  master.firmwareBulkFailed(); // Old firmware is not probed repeatedly.
  now_ms=10000;
  assert(!master.firmwareBulkAvailable(0x40));
  master.beginFirmware(0x40);
  master.firmwareBulkSucceeded();
  assert(master.firmwareBulkConfirmed(0x40) && !master.firmwareBulkAvailable(0x41));
  master.firmwareBulkFailed();
  assert(!master.firmwareBulkAvailable(0x40));
  now_ms+=1999;
  assert(!master.firmwareBulkAvailable(0x40));
  ++now_ms;
  assert(master.firmwareBulkAvailable(0x40) && master.firmwareBulkConfirmed(0x40));
  master.firmwareBulkFailed(); // Another failure starts a fresh bounded cooldown.
  assert(!master.firmwareBulkAvailable(0x40));
  assert(master.firmware_bulk_retry_delay_ms_==4000);
  for (unsigned i=0;i<8;++i) master.firmwareBulkFailed();
  assert(master.firmware_bulk_retry_delay_ms_==30000); // A broken large-packet path backs off.
  master.firmwareBulkSucceeded();
  assert(master.firmware_bulk_retry_delay_ms_==2000);
  assert(master.firmwareBulkAvailable(0x40));
  now_ms=0xFFFFFF00u;
  master.firmwareBulkFailed();
  now_ms+=1999;
  assert(!master.firmwareBulkAvailable(0x40));
  ++now_ms;
  assert(master.firmwareBulkAvailable(0x40));
  master.endFirmware();
  assert(!master.firmwareBulkAvailable(0x40) && !master.firmware_bulk_retry_pending_);
  master.beginFirmware(0x40);
  assert(!master.firmwareBulkConfirmed(0x40)); // No capabilities leak across sessions.
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
