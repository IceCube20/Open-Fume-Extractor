#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <vector>
#include "../../../OpenFumeExtractorMaster/src/RequestWaitProbe.h"
#include "../../../OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.h"
using namespace jbc_rs485;
#define F(s) s
struct {
  void println(const char*) {}
  template<class... Args> void printf(const char*,Args...) {}
} Serial;
static uint32_t millis() { return 1234; }
static constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_INTERNAL=2, MALLOC_CAP_8BIT=4;
static bool external_available=true, internal_available=true;
static std::vector<int> caps_seen;
static void* heap_caps_malloc(size_t bytes,int caps) {
  caps_seen.push_back(caps);
  if((caps&MALLOC_CAP_SPIRAM) ? !external_available : !internal_available) return nullptr;
  void* p=malloc(bytes); assert(p); memset(p,0xa5,bytes); return p;
}
#include "generated_diagnostics_storage.inc.h"

int main() {
  using Diag=MasterScheduler::BusModuleDiag;
  for(bool psram:{true,false}) {
    caps_seen.clear(); external_available=psram; internal_available=true;
    MasterScheduler s; Diag out;
    assert(!s.busModuleDiag(0x20,out) && out.requests==0 && out.last_timeout_seq==0xff);
    assert(s.initBusDiagnostics());
    assert(caps_seen.front()==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    assert(caps_seen.size()==(psram ? 1u : 2u));
    if(!psram) assert(caps_seen.back()==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    assert(s.busDiagIndex(0x0f)==-1 && s.busDiagIndex(0x70)==-1);
    for(int a=0x10;a<=0x6f;++a) {
      assert(s.busModuleDiag(uint8_t(a),out));
      assert(out.requests==0 && out.tx_wire_bytes==0 && out.last_timeout_seq==0xff);
      assert(out.last_maintenance_timeout_seq==0xff && !out.last_timeout_probe_valid);
      Frame f; f.dst=uint8_t(a); f.src=uint8_t(a); f.len=9;
      s.busDiagRecordTx(f); s.busDiagRecordRx(f);
      assert(s.busModuleDiag(uint8_t(a),out) && out.tx_frames==1 && out.rx_frames==1);
      assert(out.tx_wire_bytes==19 && out.rx_wire_bytes==19 && out.last_activity_ms==1234);
    }
    assert(s.ofe_tx_frames_==96 && s.ofe_rx_frames_==96);
    auto* stored=s.bus_module_diag_;
    assert(s.initBusDiagnostics() && s.bus_module_diag_==stored);
    assert(caps_seen.size()==(psram ? 1u : 2u));
    s.link_.network=true; Frame f; f.dst=0x20; f.src=0x20; f.len=20;
    s.busDiagRecordTx(f); s.busDiagRecordRx(f);
    assert(s.ofe_tx_frames_==96 && s.ofe_rx_frames_==96);
    assert(s.busModuleDiag(0x20,out) && out.tx_frames==2 && out.rx_frames==2);
    free(s.bus_module_diag_);
  }
  // Allocation failure must not dereference a null table or stop the transport totals.
  external_available=false; internal_available=false; MasterScheduler failed;
  assert(!failed.initBusDiagnostics());
  for(int a=0;a<=255;++a) assert(failed.busDiagIndex(uint8_t(a))==-1);
  Diag out; assert(!failed.busModuleDiag(0x20,out));
  Frame f; f.dst=0x20; f.src=0x20; f.len=9;
  failed.busDiagRecordTx(f); failed.busDiagRecordRx(f);
  assert(failed.ofe_tx_frames_==1 && failed.ofe_rx_frames_==1);
  internal_available=true; assert(failed.initBusDiagnostics()); free(failed.bus_module_diag_);
  puts("PASS: actual diagnostics allocation, PSRAM/internal fallback, failure/retry, defaults, 96 addresses, counters and WLAN separation");
}
