#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/StartupScanRecovery.h"

static uint32_t now_ms = 0;
static uint32_t millis() { return now_ms; }
struct ModuleRecord { uint8_t addr; bool online; };
struct Registry {
  std::vector<ModuleRecord> records;
  unsigned unseen = 0, pruned = 0;
  ModuleRecord* find(uint8_t addr) {
    for (auto& rec : records) if (rec.addr == addr) return &rec;
    return nullptr;
  }
  void markAllScanUnseen() { ++unseen; }
  uint8_t removeScanUnseen() { ++pruned; return 1; }
};
struct MasterScheduler {
  Registry registry_;
  struct Extractor { unsigned dirtied = 0; void markOutputDirty() { ++dirtied; } } extractor_;
  StartupScanRecovery startup_scan_recovery_;
  bool scan_job_active_ = false, scan_job_missing_only_ = false;
  bool scan_job_auto_address_ = false, scan_job_auto_done_ = true;
  bool scan_job_prune_missing_ = false, scan_job_finished_ = false;
  bool pending_hotplug_scan_ = false, ota = false;
  uint8_t scan_job_next_addr_ = 0x10;
  uint32_t last_scan_job_step_ms_ = 0, hotplug_discovery_window_until_ms_ = 0;
  std::vector<uint8_t> probes;
  unsigned readdressed = 0, roles = 0;
  bool firmwareUpdateActive() const { return ota; }
  void autoAddressModules(bool) { ++readdressed; }
  void scanAddress(uint8_t addr) { probes.push_back(addr); }
  void selectRoles() { ++roles; }
  void updateJbcAggregate() {}
  void updateInputRouting() {}
  void requestScanKnownModules(bool with_auto_address = false, bool prune_missing = false);
  void requestStartupScan();
  void pollStartupScan();
  void startScanJob(bool, bool, bool);
  void pollScanJob();
};
#include "generated_startup_scan.inc.h"

static void finish(MasterScheduler& s) {
  for (unsigned n = 0; s.scan_job_active_ && n < 100; ++n) {
    now_ms += 5;
    s.pollScanJob();
  }
  assert(!s.scan_job_active_ && s.scan_job_finished_);
}

static void test_deadlines(uint32_t start) {
  StartupScanRecovery policy;
  assert(!policy.takeDue(start + 10000, false));
  policy.arm(start);
  assert(!policy.takeDue(start + 1999, false));
  assert(!policy.takeDue(start + 2000, true));
  assert(policy.takeDue(start + 2000, false));
  assert(!policy.takeDue(start + 4999, false));
  assert(policy.takeDue(start + 5000, false));
  assert(!policy.takeDue(start + 9999, false));
  assert(policy.takeDue(start + 10000, false));
  assert(!policy.takeDue(start + 15000, false));
  policy.arm(start);
  assert(!policy.takeDue(start + 30000, true));
  assert(!policy.takeDue(start + 30001, false));
  policy.arm(start);
  policy.cancel();
  assert(!policy.takeDue(start + 2000, false));
}

static void test_startup_and_manual() {
  MasterScheduler s;
  s.registry_.records = {{0x10, true}, {0x11, false}, {0x20, true}, {0x40, true}};
  now_ms = 1000;
  s.requestStartupScan();
  finish(s);
  assert(s.probes.size() == 96); // Initial pass checks every address, including remembered ones.
  for (uint32_t deadline : {2000U, 5000U, 10000U}) {
    s.probes.clear();
    now_ms = 1000 + deadline;
    s.pollStartupScan();
    assert(s.scan_job_active_ && s.scan_job_missing_only_);
    finish(s);
    assert(s.probes.size() == 93);
    for (uint8_t addr : s.probes) assert(addr != 0x10 && addr != 0x20 && addr != 0x40);
    assert(!s.readdressed && !s.registry_.pruned);
  }
  now_ms = 16000;
  s.pollStartupScan();
  assert(!s.scan_job_active_);

  s.requestStartupScan();
  s.requestScanKnownModules(true, true); // An explicit manual scan takes ownership.
  s.probes.clear();
  finish(s);
  assert(s.readdressed == 1 && s.registry_.pruned == 1 && s.probes.size() == 96);
  now_ms += 10000;
  s.pollStartupScan();
  assert(!s.scan_job_active_);
}

static void test_busy() {
  MasterScheduler s;
  now_ms = 0;
  s.requestStartupScan();
  now_ms = 2000;
  s.pollStartupScan();
  assert(!s.scan_job_missing_only_); // Do not replace an in-progress scan.
  finish(s);
  s.ota = true;
  s.pollStartupScan();
  assert(!s.scan_job_active_);
  s.ota = false;
  s.hotplug_discovery_window_until_ms_ = now_ms + 450;
  s.pollStartupScan();
  assert(!s.scan_job_active_);
  s.hotplug_discovery_window_until_ms_ = 0;
  s.pending_hotplug_scan_ = true;
  s.pollStartupScan();
  assert(!s.scan_job_active_);
  s.pending_hotplug_scan_ = false;
  s.pollStartupScan();
  assert(s.scan_job_missing_only_);
  s.ota = true;
  now_ms += 10;
  const auto before = s.probes.size();
  s.pollScanJob();
  assert(s.probes.size() == before);
  s.ota = false;
  finish(s);
}

int main() {
  test_deadlines(0);
  test_deadlines(UINT32_MAX - 1000);
  test_startup_and_manual();
  test_busy();
  std::puts("PASS: bounded startup retries, missing-only scans, stable addresses, manual ownership, OTA/discovery guards, wraparound");
}
