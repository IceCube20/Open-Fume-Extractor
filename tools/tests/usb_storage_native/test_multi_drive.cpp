#define USB_MULTI_DRIVE_TEST
#include "test_transfer.cpp"
#include <thread>

unsigned slotFor(unsigned address) {
  for (unsigned i = 0; i < MAX_DRIVES; ++i)
    if (drives[i].handle.load() && drives[i].address == address) return i;
  return NO_DRIVE;
}
void drain() { for (unsigned i = 0; i < 132; ++i) pump(); }
void choose(unsigned index) {
  const auto previousMounts = mountCalls;
  assert(selectDrive(state->generation, index, drives[index].instance));
  assert(mountCalls == previousMounts); pump();
  assert(ready && selectedDrive == index && device == drives[index].handle && mountedHandle == device);
}
void burstConnections() {
  reset(false);
  std::thread a([] { connect(1); connect(1); });
  std::thread b([] { connect(33); });
  std::thread c([] { connect(65); });
  std::thread d([] { connect(127); });
  a.join(); b.join(); c.join(); d.join();
  connect(0); connect(128); assert(detected() && !ready && !installCalls);
  drain(); assert(installed.size() == 4 && installCalls == 4 && mountCalls == 1);
  assert(selectedDrive == 0 && device->address == 1);
  for (unsigned address : {1U, 33U, 65U, 127U}) {
    const auto slot = slotFor(address); assert(slot != NO_DRIVE && drives[slot].instance);
    assert(drives[slot].capacity == uint64_t(UINT32_MAX) * 4096);
    assert(!strcmp(drives[slot].serial, "serial-123"));
    for (unsigned other = slot + 1; other < MAX_DRIVES; ++other) assert(drives[slot].instance != drives[other].instance);
    connect(address);
  }
  drain(); assert(installCalls == 4 && mountCalls == 1);
  const auto generation = state->generation;
  connect(2); drain(); assert(driveLimitReached && installed.size() == 4 && installCalls == 4);
  assert(state->generation == generation && ready && device->address == 1);
  auto status = statusJson(false);
  assert(status.find("\"drive_limit\":4") != std::string::npos);
  assert(status.find("\"drive_limit_reached\":true") != std::string::npos);
  assert(status.find("\"capacity_bytes\":17592186040320") != std::string::npos);
  assert(status.find("\"diagnostics\"") == std::string::npos);
  assert(statusJson(true).find("\"diagnostics\":{}") != std::string::npos);
  puts("PASS: concurrent connection bitmap, duplicates, four-slot limit, unique identities and status");
}
void selectedSwitchAndStaleGuards() {
  reset(); disk["/usb/sentinel.bin"].bytes = {1,2,3}; connect(2); pump();
  const auto first = device; const auto second = drives[1].handle.load();
  const auto generation = state->generation;
  assert(!selectDrive(generation - 1, 1, drives[1].instance));
  assert(!selectDrive(generation, MAX_DRIVES, drives[1].instance));
  assert(!selectDrive(generation, 1, 0) && !selectDrive(generation, 1, drives[0].instance));
  assert(matchesDrive(0, drives[0].instance) && !matchesDrive(1, drives[1].instance));
  assert(!matchesDrive(NO_DRIVE, 1) && !matchesDrive(0, 0));
  assert(!command("refresh", generation - 1, 0) && !command("eject", generation - 1, 0));
  choose(1); assert(installed.count(first) && installed.count(second) && !uninstallCalls);
  assert(state->generation != generation && !disk.count("/usb/sentinel.bin"));
  assert(lifecycle[lifecycle.size()-2] == "unmount:1" && lifecycle.back() == "mount:2");
  assert(!matchesDrive(0, drives[0].instance) && matchesDrive(1, drives[1].instance));
  assert(!receiveBegin("stale.bin", 1, generation, DEFAULT_DIRECTORY));
  assert(!receiveBegin("stale.bin", 1, generation, 0));
  auto token = receiveBegin("current.bin", 0, state->generation); assert(token); commit(token);
  choose(0); assert((disk.at("/usb/sentinel.bin").bytes == std::vector<uint8_t>{1,2,3}));
  assert(!disk.count("/usb/OFE/current.bin"));
  choose(1); assert(disk.count("/usb/OFE/current.bin"));
  assert(statusJson(false).find("\"drive\":1,\"drive_instance\":" + std::to_string(drives[1].instance)) != std::string::npos);
  puts("PASS: queued selection, single VFS switch ordering, per-volume identity and stale request guards");
}
void removalsAndPendingUpload() {
  reset(); connect(2); pump(); const auto inactive = drives[1].handle.load();
  auto token = upload("survives.bin", 4); writeAll(token, 2);
  const auto generation = state->generation, oldUnmounts = unmountCalls;
  const auto descriptor = state->descriptor;
  connect(65); disconnect(inactive); assert(present && ready && !gone); pump();
  assert(!drives[1].handle && installed.size() == 1 && uninstallCalls == 1);
  assert(state->token == token && state->descriptor == descriptor && state->received == 2);
  assert(state->generation == generation && unmountCalls == oldUnmounts);
  assert(pendingAddresses[2].load() == 2 && slotFor(65) == NO_DRIVE);
  assert(!selectDrive(state->generation, 0, drives[0].instance));
  const uint8_t tail[]{7,8}; assert(receiveChunk(token, 2, tail, 2)); commit(token);
  pump(); assert(slotFor(65) != NO_DRIVE && ready && device->address == 1);
  assert((disk.at("/usb/survives.bin").bytes == std::vector<uint8_t>{pattern(0),pattern(1),7,8}));
  token = upload("cancelled.bin", 4); writeAll(token, 2);
  const auto active = device; const auto beforeClose = closeCalls, beforeUnlink = unlinkCalls;
  disconnect(active); assert(!ready && !present && gone); pump(); clean();
  assert(closeCalls == beforeClose + 1 && unlinkCalls == beforeUnlink);
  assert(!installed.count(active) && selectedDrive == NO_DRIVE && !ready && detected());
  assert(state->generation != generation && state->phase == Phase::Error);
  assert(!release(token) && !receiveCommit(token));
  connect(66); drain(); assert(!ready && selectedDrive == NO_DRIVE); // Never auto-fallback.
  choose(slotFor(65));
  assert(!disk.count("/usb/cancelled.bin") && !disk.count("/usb/survives.bin"));
  puts("PASS: inactive removal preserves live upload, deferred connects survive, active removal cancels without fallback");
}
void slotReuseAndQueuedRemoval() {
  reset(); connect(2); pump(); const unsigned slot = slotFor(2);
  const auto oldHandle = drives[slot].handle.load(); const auto oldInstance = drives[slot].instance;
  const auto generation = state->generation;
  assert(selectDrive(generation, slot, oldInstance));
  disconnect(oldHandle); connect(3); pump();
  assert(!drives[slot].handle && selectedDrive == 0 && ready && state->phase == Phase::Error);
  pump();
  assert(drives[slot].address == 3 && drives[slot].instance != oldInstance);
  assert(selectedDrive == 0 && ready && device->address == 1 && state->phase == Phase::Error);
  assert(!selectDrive(state->generation, slot, oldInstance));
  const auto replacement = drives[slot].handle.load();
  disconnect(oldHandle); pump(); assert(drives[slot].handle == replacement && drives[slot].connected);
  connect(2); pump(); assert(slotFor(2) != NO_DRIVE && slotFor(2) != slot);
  choose(slot); assert(!matchesDrive(slot, oldInstance));
  reset(false); nextDriveInstance = UINT32_MAX; connect(3); pump(); assert(drives[0].instance == 1);
  puts("PASS: removed queued target, slot/address reuse, stale callback/instance and identity wrap guards");
}
void ejectionAndFailures() {
  reset(); connect(2); pump(); const auto first = device;
  assert(command("eject", state->generation, 0)); pump(); clean();
  assert(!ready && !vfs && !device && present && detected() && selectedDrive == 0);
  assert(installed.count(first) && !uninstallCalls);
  connect(3); pump(); assert(!ready && selectedDrive == 0);
  choose(0); assert(device == first);
  failInstallAddress = 4; connect(4); pump(); assert(slotFor(4) == NO_DRIVE && ready && device == first);
  partialInstallFailure = true; connect(4); pump(); assert(slotFor(4) == NO_DRIVE && installed.size() == 3 && ready);
  failInfoAddress = 4; failInstallAddress = 0; connect(4); pump();
  const auto slot = slotFor(4); assert(slot != NO_DRIVE && !drives[slot].capacity && ready && device == first);
  assert(!strcmp(drives[slot].label, "USB 4"));
  failMountAddress = 4; assert(selectDrive(state->generation, slot, drives[slot].instance)); pump();
  assert(!ready && !vfs && !mountedHandle && installed.size() == 4 && state->phase == Phase::Error);
  assert(drives[0].connected && drives[slot].connected);
  choose(0); assert(device == first);
  failMountAddress = 0; choose(slot); assert(ready);
  reset(false); failMountAddress = 1; connect(1); pump(); assert(!ready && installed.size() == 1);
  connect(2); pump(); assert(!ready && installed.size() == 2); choose(1);
  puts("PASS: ejection/remount retains handles, install/info/mount failures isolate registry and allow recovery");
}
void lifecycleCallbacks() {
  reset(false); duringInfo = [](auto handle) { disconnect(handle); connect(33); };
  connect(1); pump(); assert(!ready && drives[0].removed); pump();
  assert(!installed.empty() && slotFor(33) != NO_DRIVE && !drives[slotFor(33)].removed);
  // No selection happened before that removal, so the initial auto-selection remains available.
  assert(ready && selectedDrive == slotFor(33));
  reset(false); duringMount = [](auto handle) { disconnect(handle); };
  connect(1); pump(); assert(!ready && !vfs && !mountedHandle); pump();
  assert(installed.empty() && selectedDrive == NO_DRIVE && !gone); clean();
  puts("PASS: disconnect callbacks during info and VFS registration are processed before dispatch");
}
void queuedSelectionSurvivesSourceRemoval() {
  reset(); connect(2); pump(); const auto source = device;
  const auto target = drives[1].handle.load(); const auto generation = state->generation;
  assert(selectDrive(generation, 1, drives[1].instance));
  disconnect(source); assert(!ready && state->phase == Phase::Queued); pump();
  assert(ready && selectedDrive == 1 && device == target && state->phase == Phase::Idle);
  assert(!installed.count(source) && installed.count(target) && state->generation != generation);
  assert(!command("refresh", generation, 0));
  assert(lifecycle[lifecycle.size()-3] == "unmount:1");
  assert(lifecycle[lifecycle.size()-2] == "uninstall:1" && lifecycle.back() == "mount:2");
  reset(); connect(2); pump(); const auto targetB = drives[1].handle.load();
  const auto installsBefore = installCalls;
  assert(selectDrive(state->generation, 1, drives[1].instance));
  disconnectCurrent(); failInstallAddress = 3; connect(3); pump();
  assert(ready && device == targetB && selectedDrive == 1 && state->phase == Phase::Idle);
  assert(installCalls == installsBefore && pendingAddresses[0].load() == (uint32_t(1) << 3));
  pump(); assert(installCalls == installsBefore + 1 && slotFor(3) == NO_DRIVE);
  assert(ready && device == targetB && state->phase == Phase::Idle && !pendingAddresses[0].load());
  for (const char* action : {"read", "refresh", "eject"}) {
    reset(); connect(2); pump(); select("existing.bin", 1);
    disk["/usb/existing.bin"].bytes = {9};
    assert(command(action, state->generation, 0)); disconnectCurrent(); pump();
    assert(!ready && selectedDrive == NO_DRIVE && state->phase == Phase::Error && mountCalls == 1);
    clean();
  }
  reset(); connect(2); pump();
  assert(selectDrive(state->generation, 1, drives[1].instance));
  disconnectCurrent(); disconnect(drives[1].handle); pump();
  assert(!ready && installed.empty() && state->phase == Phase::Error);
  puts("PASS: accepted selection survives source removal, missing target rejects, queued file actions stay cancelled");
}
void reusedMetadataAndInstallNotices() {
  reset(); connect(2); pump(); assert(drives[1].vendor && drives[1].product);
  disconnect(drives[1].handle); pump();
  assert(!drives[1].vendor && !drives[1].product && !drives[1].capacity && !*drives[1].serial);
  failInfoAddress = 3; connect(3); pump(); assert(slotFor(3) == 1);
  assert(!drives[1].vendor && !drives[1].product && !drives[1].capacity && !*drives[1].serial);
  assert(!strcmp(drives[1].label, "USB 2"));
  reset(); const auto first = device; const auto initialInfoCalls = infoCalls, initialMounts = mountCalls;
  MscDevice unknown[MAX_DRIVES + 2]{};
  duringInstall = [&](auto handle) {
    assert(installInProgress);
    for (unsigned i = 0; i < MAX_DRIVES + 1; ++i) disconnect(&unknown[0]); // Duplicate notices do not overflow.
    disconnect(handle);
  };
  connect(2); pump(); assert(drives[1].removed && !drives[1].connected);
  assert(infoCalls == initialInfoCalls && mountCalls == initialMounts && !installDisconnectOverflow);
  pump(); assert(installed.size() == 1 && ready && device == first);
  duringInstall = [&](auto) {
    for (auto& handle : unknown) disconnect(&handle);
  };
  connect(3); pump(); assert(installDisconnectOverflow && drives[1].removed && !drives[1].connected);
  assert(infoCalls == initialInfoCalls && mountCalls == initialMounts); pump();
  assert(installed.size() == 1 && ready && device == first);
  duringInstall = [](auto) { connect(33); connect(65); };
  connect(4); drain(); assert(slotFor(4) != NO_DRIVE && slotFor(33) != NO_DRIVE && slotFor(65) != NO_DRIVE);
  assert(installed.size() == 4 && ready && device == first && !installInProgress && !installDisconnectOverflow);
  puts("PASS: slot metadata reset, early GONE duplicate/overflow bounds, info skipped and install-time connects retained");
}
void selectionAdmittedDuringInstallFailure() {
  for (bool partial : {false, true}) {
    reset(); connect(2); pump(); const auto target = drives[1].handle.load();
    disconnectCurrent(); pump();
    assert(selectedDrive == NO_DRIVE && !ready && installed.size() == 1);
    const auto generation = state->generation, instance = drives[1].instance;
    const auto initialInstalls = installCalls, initialInfo = infoCalls, initialMounts = mountCalls;
    const auto initialUninstalls = uninstallCalls;
    bool admitted = false;
    failInstallAddress = 3; partialInstallFailure = partial;
    duringInstall = [&](auto handle) {
      assert(installInProgress && selectedDrive == NO_DRIVE && !ready && !guardDepth);
      assert(bool(handle) == partial);
      admitted = selectDrive(generation, 1, instance); assert(admitted);
      assert(state->phase == Phase::Queued && state->requestedDrive == 1 && state->requestedInstance == instance);
      assert(state->generation == generation && mountCalls == initialMounts);
    };
    connect(3); pump();
    assert(admitted && installCalls == initialInstalls + 1 && slotFor(3) == NO_DRIVE);
    assert(ready && selectedDrive == 1 && device == target && mountedHandle == target && state->phase == Phase::Idle);
    assert(installed.size() == 1 && installed.count(target) && infoCalls == initialInfo);
    assert(mountCalls == initialMounts + 1 && uninstallCalls == initialUninstalls + unsigned(partial));
    assert(state->generation != generation && matchesDrive(1, instance) && !installInProgress);
    assert(!pendingAddresses[0].load() && !duringInstall);
    pump(); assert(ready && device == target && state->phase == Phase::Idle); clean();
  }
  puts("PASS: selection admitted during failing install survives and dispatches with no selected source, including partial-handle cleanup");
}
bool installDisconnectRace() {
  reset(false);
  duringInstall = [](auto handle) { disconnect(handle); };
  connect(1); pump(); drain();
  assert(!infoCalls && !mountCalls && uninstallCalls == 1 && !installInProgress);
  const bool ok = !ready && !detected() && installed.empty() && selectedDrive == NO_DRIVE;
  if (!ok) printf("FAIL: disconnect during install was lost: mounted=%u detected=%u registry_handles=%zu selected=%u\n",
                  unsigned(ready.load()), unsigned(detected()), installed.size(), selectedDrive.load());
  clean(); resetMsc();
  if (ok) puts("PASS: disconnect before install returns cannot publish a ghost registry handle");
  return ok;
}
void labelsAndProbeIsolation() {
  reset(false); labelsForAddress[1] = "SYSTEM"; connect(1); pump();
  assert(!strcmp(drives[0].volumeName, "SYSTEM") && sizeof(drives[0].volumeName) == 64);
  assert(probeMountCalls == 1 && probeUnmountCalls == 1 && mountCalls == 1 && labelReads.size() == 2);
  assert(labelReads[0]->probe && !labelReads[1]->probe && labelReads[1] == vfs);
  assert(lifecycle[1] == "probe-mount:1" && lifecycle[2] == "probe-unmount:1" && lifecycle[3] == "mount:1");
  disk["/usb/sentinel.bin"].bytes = {1,2,3};
  select("sentinel.bin", 3); state->directoryCount = 2;
  strlcpy(state->directories[1].path, "nested", MAX_PATH);
  const State before = *state;
  const auto active = device; const auto activeMount = vfs;
  const auto* root = &disk.at("/usb"); const auto initialRefresh = refreshCalls;
  labelsForAddress[2] = "PROJECTS \"A\"\\backup\n\xC3\x84";
  duringProbeMount = [&](auto handle) {
    assert(handle->address == 2 && ready && present && !gone);
    assert(device == active && vfs == activeMount && activeDevice == active);
    assert(mountedHandle == active && mountedVfs == activeMount && registeredVfs.size() == 1);
  };
  duringLabel = [&](auto mount) {
    assert(mount == probeVfs && mount->probe && mount->device->address == 2);
    assert(registeredVfs.size() == 2 && device == active && vfs == activeMount);
    assert(&disk.at("/usb") == root && disk.at("/usb/sentinel.bin").bytes.size() == 3);
  };
  connect(2); pump();
  assert(!strcmp(drives[1].volumeName, labelsForAddress[2].c_str()));
  assert(!strcmp(drives[0].volumeName, "SYSTEM") && !strcmp(drives[1].label, "USB 2 - Native storage"));
  assert(ready && present && !gone && selectedDrive == 0 && activeDevice == active);
  assert(device == active && vfs == activeMount && mountedVfs == activeMount && !probeVfs);
  assert(registeredVfs.size() == 1 && mountCalls == 1 && !unmountCalls && refreshCalls == initialRefresh);
  assert(state->generation == before.generation && state->phase == before.phase && state->count == before.count);
  assert(state->directoryCount == before.directoryCount && !memcmp(state->directories, before.directories, sizeof(before.directories)));
  assert(!memcmp(state->files, before.files, sizeof(before.files)) && !strcmp(state->message, before.message));
  assert(&disk.at("/usb") == root && (disk.at("/usb/sentinel.bin").bytes == std::vector<uint8_t>{1,2,3}));
  assert(probeMountCalls == 2 && probeUnmountCalls == 2);
  const auto json = statusJson(false);
  assert(json.find("\"volume_name\":\"SYSTEM\"") != std::string::npos);
  assert(json.find("\"volume_name\":\"PROJECTS \\\"A\\\"\\\\backup\\n\xC3\x84\"") != std::string::npos);
  const auto probeCalls = probeMountCalls;
  const auto target = drives[1].handle.load(); volumeLabels[target] = "RENAMED";
  choose(1); assert(!strcmp(drives[1].volumeName, "RENAMED") && !strcmp(drives[0].volumeName, "SYSTEM"));
  assert(labelReads.back() == vfs && !labelReads.back()->probe && labelReads.back()->device == target);
  assert(probeMountCalls == probeCalls && !probeVfs && registeredVfs.size() == 1);
  volumeLabels[target].clear(); choose(1); assert(!*drives[1].volumeName);
  assert(statusJson(false).find("\"volume_name\":\"\"") != std::string::npos);
  puts("PASS: per-device labels, escaped status, isolated second VFS probe, unchanged active root/cache and activation reread");
}
void labelFallbacks() {
  reset(false); labelsForAddress[1] = "LATER"; failProbeAddress = 1;
  connect(1); pump();
  assert(ready && !strcmp(drives[0].volumeName, "LATER") && labelReads.size() == 1 && labelReads.back() == vfs);
  assert(probeMountCalls == 1 && !probeUnmountCalls && !probeVfs && registeredVfs.size() == 1);
  const auto generation = state->generation; const auto active = device; const auto activeMount = vfs;
  const auto oldRefresh = refreshCalls, oldMounts = mountCalls;
  failProbeAddress = 2; labelsForAddress[2] = "PROBE FAILED"; connect(2); pump();
  assert(!*drives[1].volumeName && !strcmp(drives[1].label, "USB 2 - Native storage"));
  assert(state->generation == generation && ready && device == active && vfs == activeMount);
  assert(refreshCalls == oldRefresh && mountCalls == oldMounts && registeredVfs.size() == 1);
  const auto probes = probeMountCalls; failProbeAddress = 0; choose(1);
  assert(!strcmp(drives[1].volumeName, "PROBE FAILED") && probeMountCalls == probes);
  failLabelAddress = 3; labelsForAddress[3] = "READ FAILED"; connect(3); pump();
  assert(!*drives[2].volumeName && !strcmp(drives[2].label, "USB 3 - Native storage"));
  assert(!probeVfs && probeUnmountCalls == 1 && registeredVfs.size() == 1);
  choose(2); assert(ready && !*drives[2].volumeName);
  failLabelAddress = 0; choose(2); assert(!strcmp(drives[2].volumeName, "READ FAILED"));
  failLabelAddress = 3; choose(2); assert(ready && !strcmp(drives[2].volumeName, "READ FAILED"));
  labelsForAddress[4] = ""; connect(4); pump(); assert(!*drives[3].volumeName);
  assert(!strcmp(drives[3].label, "USB 4 - Native storage") && !probeVfs);
  puts("PASS: probe VFS failure, read failure, descriptor fallback, successful retry and empty-label fallback");
}
void labelDisconnectsAndReuse() {
  for (bool inAccessor : {false, true}) {
    reset(); const auto active = device; const auto activeMount = vfs;
    const auto generation = state->generation; labelsForAddress[2] = "MUST NOT CACHE";
    if (inAccessor) duringLabel = [](auto mount) { assert(mount->probe); disconnect(mount->device); };
    else duringProbeMount = [](auto handle) { disconnect(handle); };
    connect(2); pump(); const auto removed = drives[1].handle.load();
    assert(removed && drives[1].removed && !drives[1].connected && !*drives[1].volumeName);
    assert(!probeVfs && probeMountCalls == probeUnmountCalls && registeredVfs.size() == 1);
    assert(ready && device == active && vfs == activeMount && state->generation == generation);
    pump(); assert(!installed.count(removed) && !drives[1].handle && !*drives[1].volumeName);
    labelsForAddress[3] = "REPLACEMENT"; connect(3); pump();
    assert(slotFor(3) == 1 && !strcmp(drives[1].volumeName, "REPLACEMENT"));
    disconnect(removed); pump(); assert(drives[1].connected && !strcmp(drives[1].volumeName, "REPLACEMENT"));
  }
  reset(); labelsForAddress[2] = "OLD LABEL"; connect(2); pump();
  assert(!strcmp(drives[1].volumeName, "OLD LABEL"));
  const auto oldInstance = drives[1].instance; disconnect(drives[1].handle); pump();
  assert(!*drives[1].volumeName && !*drives[1].label && !*drives[1].serial);
  failLabelAddress = 3; labelsForAddress[3] = "FAILED NEW LABEL"; connect(3); pump();
  assert(slotFor(3) == 1 && drives[1].instance != oldInstance && !*drives[1].volumeName);
  assert(statusJson(false).find("OLD LABEL") == std::string::npos);
  const auto target = drives[1].handle.load();
  // A label arriving for a replaced handle must not update the new slot occupant.
  volumeLabels[device] = "WRONG SOURCE";
  workerContext = true; char previous[64]; strlcpy(previous, drives[1].volumeName, sizeof(previous));
  cacheVolumeLabel(1, device, vfs); workerContext = false;
  assert(drives[1].handle == target && !strcmp(drives[1].volumeName, previous));
  failLabelAddress = 0;
  duringLabel = [](auto mount) { assert(!mount->probe); disconnect(mount->device); };
  assert(selectDrive(state->generation, 1, drives[1].instance)); pump();
  assert(!ready && !vfs && !mountedVfs && !probeVfs && !*drives[1].volumeName);
  assert(state->phase == Phase::Error); pump(); assert(!drives[1].handle && !*drives[1].volumeName);
  puts("PASS: disconnect during probe mount/read or active reread, stale-handle rejection and cleared slot-reuse cache");
}
int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "--probe-install-disconnect")) return installDisconnectRace() ? 0 : 1;
  static_assert(MAX_DRIVES == 4 && NO_DRIVE == UINT32_MAX, "Four connected drives; one selected VFS");
  burstConnections(); selectedSwitchAndStaleGuards(); removalsAndPendingUpload();
  slotReuseAndQueuedRemoval(); ejectionAndFailures(); lifecycleCallbacks();
  queuedSelectionSurvivesSourceRemoval(); reusedMetadataAndInstallNotices(); assert(installDisconnectRace());
  selectionAdmittedDuringInstallFailure();
  labelsAndProbeIsolation(); labelFallbacks(); labelDisconnectsAndReuse();
  clean(); resetMsc(); assert(!workerContext && !guardDepth);
  puts("PASS: extracted production multi-drive registry/event/mount/unmount/selection/worker integration");
}
