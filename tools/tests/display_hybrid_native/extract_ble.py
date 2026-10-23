"""Compile actual provisioning/approval/forget methods in the native harness."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
ble = (root / 'OpenFumeExtractorMaster/src/OfeDisplayBle.h').read_text(encoding='utf-8')
wifi = (root / 'Module/DisplayModule_320x480/src/OfeDisplayWifi.h').read_text(encoding='utf-8')
tunnel = (root / 'Module/DisplayModule_320x480/src/OfeDisplayTunnel.h').read_text(encoding='utf-8')


def method(source, signature):
    start = source.index(signature)
    first = source.index('{', start)
    depth = 1
    end = first + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


protocol = ble[ble.index('namespace ofe_ble {'):ble.index('class OfeDisplayBle {')]
config = tunnel[tunnel.index('struct Config {'):tunnel.index('static_assert(sizeof(Config)')]
checks = ''.join(method(tunnel, s) + '\n' for s in ['inline bool nonzero(', 'inline bool valid(', 'inline bool configured('])
output = 'namespace ofe_wifi { constexpr uint8_t CONFIG_VERSION=1; enum Mode:uint8_t { AUTOMATIC,WIRED,WIRELESS };\n' + config + checks + '}\n' + protocol
output += '''class Approval {
public:
  int mux_=0;
  ofe_ble::View view_;
  std::optional<NimBLEConnInfo> confirmation_;
  bool approved_=false,secured_=false,decision_pending_=false;
  std::atomic<bool> cancel_{false},failed_{false};
'''
for signature in ['bool approve(', 'void authenticated(', 'bool live(', 'void fail(', 'ofe_ble::Phase stoppedPhase(']:
    output += method(ble, signature) + '\n'
output += '};\nclass Radio { public:\n'
output += '''int mux_=0; ofe_ble::View view_;
  std::optional<NimBLEConnInfo> confirmation_;
  bool provider_=true;
  void* result_=nullptr; void* worker_=nullptr;
  ofe_ble::Candidate target_;
  bool selected_=false,approved_=false,secured_=false,received_=false,decision_pending_=false;
  ofe_wifi::Config config_;
  std::atomic<bool> active_{false};
'''
for signature in ['bool startRadio(', 'bool stopRadio(', 'void finish(', 'void snapshotMemory(', 'void phase(']:
    output += method(ble, signature) + '\n'
output += '};\nclass ScanStore { public: int mux_=0; ofe_ble::View view_;\n'
output += method(ble, 'void discovered(') + '\n'
output += '''};\nclass Selection { public:
  using Provider=bool(*)(uint64_t,ofe_wifi::Config&);
  int mux_=0; ofe_ble::View view_; ofe_ble::Candidate target_;
  ofe_wifi::Config config_; Provider provider_=nullptr;
  void* result_=nullptr; void* worker_=nullptr;
  bool selected_=false,approved_=false,secured_=false,received_=false,decision_pending_=false;
  std::optional<NimBLEConnInfo> confirmation_;
  std::atomic<bool> active_{false},cancel_{false},failed_{false};
  static void taskHook(void*) {}
'''
for signature in ['ofe_ble::View view(', 'bool select(', 'void finish(', 'bool stopRadio(', 'void snapshotMemory(', 'void cancel(',
                  'void phase(', 'void fail(', 'ofe_ble::Phase stoppedPhase(', 'bool live(', 'bool approve(']:
    output += method(ble, signature) + '\n'
output += '''};\nclass MasterSearch:public Selection { public:
  int scan_callbacks_=0,pair_attempts=0; uint32_t pair_started=0;
  void runMasterPair(uint32_t started) { pair_started=started; ++pair_attempts; assert(NimBLEDevice::isInitialized()); finish(ofe_ble::DONE); }
'''
for signature in ['bool startRadio(', 'void runMaster(', 'bool masterSelectionOrPhase(', 'void discovered(']:
    output += method(ble, signature) + '\n'
output += '};\nclass MasterPair:public Selection { public: int client_callbacks_=0;\n'
output += method(ble, 'void runMasterPair(') + '\n'
output += method(ble, 'void authenticated(') + '\n'
output += '};\nclass Forget { public:\n'
output += '''int mux_=0; bool worker_=true,save_pending_=false;
  std::atomic<uint8_t> update_transport_{0};
  struct View { ofe_wifi::Config config; uint8_t result=0; } shared_;
  ofe_wifi::Config pending_;
  View view() { return shared_; }
'''
for signature in ['bool save(', 'bool forgetPairing(', 'bool queueSave(']:
    output += method(wifi, signature) + '\n'
output += '};\n'
Path(sys.argv[1]).write_text(output, encoding='utf-8')
