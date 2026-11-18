#pragma once

std::map<unsigned, MscDevice> identities;
std::map<msc_host_device_handle_t, bool> installed;
std::map<msc_host_device_handle_t, std::map<std::string, Node>> volumes;
std::map<unsigned, std::string> labelsForAddress;
std::map<msc_host_device_handle_t, std::string> volumeLabels;
std::map<unsigned, MscVfs> mountIdentities;
std::map<msc_host_vfs_handle_t, bool> registeredVfs;
std::vector<msc_host_vfs_handle_t> labelReads;
std::vector<std::string> lifecycle;
unsigned identitySequence = 0, installCalls = 0, infoCalls = 0, uninstallCalls = 0, mountCalls = 0, unmountCalls = 0;
unsigned mountSequence = 0, probeMountCalls = 0, probeUnmountCalls = 0;
unsigned failInstallAddress = 0, failInfoAddress = 0, failMountAddress = 0;
unsigned failProbeAddress = 0, failLabelAddress = 0;
bool partialInstallFailure = false;
std::function<void(msc_host_device_handle_t)> duringInstall, duringInfo, duringMount, duringProbeMount;
std::function<void(msc_host_vfs_handle_t)> duringLabel;
msc_host_device_handle_t mountedHandle = nullptr;
msc_host_vfs_handle_t mountedVfs = nullptr, probeVfs = nullptr;

const char* esp_err_to_name(esp_err_t result) { return result == ESP_OK ? "OK" : "FAIL"; }
void connect(unsigned address) {
  msc_host_event_t notice{msc_host_event_t::MSC_DEVICE_CONNECTED};
  notice.device.address = uint8_t(address); event(&notice, nullptr);
}
void disconnect(msc_host_device_handle_t handle) {
  msc_host_event_t notice{msc_host_event_t::MSC_DEVICE_DISCONNECTED};
  notice.device.handle = handle; event(&notice, nullptr);
}
void disconnectCurrent() { assert(device); disconnect(device); }
esp_err_t msc_host_install_device(uint8_t address, msc_host_device_handle_t* out) {
  fsContext(); ++installCalls;
  if (address == failInstallAddress && !partialInstallFailure) {
    if (duringInstall) { auto hook = std::move(duringInstall); duringInstall = {}; hook(nullptr); }
    return ESP_FAIL;
  }
  auto& identity = identities[++identitySequence]; identity = {address, identitySequence};
  *out = &identity; assert(installed.emplace(*out, true).second);
  volumes[*out]["/usb"].directory = true;
  volumeLabels[*out] = labelsForAddress[address];
  lifecycle.push_back("install:" + std::to_string(address));
  if (duringInstall) { auto hook = std::move(duringInstall); duringInstall = {}; hook(*out); }
  return address == failInstallAddress ? ESP_FAIL : ESP_OK;
}
esp_err_t msc_host_get_device_info(msc_host_device_handle_t handle, msc_host_device_info_t* info) {
  fsContext(); assert(installed.count(handle)); ++infoCalls;
  if (duringInfo) { auto hook = std::move(duringInfo); duringInfo = {}; hook(handle); }
  if (handle->address == failInfoAddress) return ESP_FAIL;
  info->sector_count = UINT32_MAX; info->sector_size = 4096;
  info->idVendor = uint16_t(0x1200 + handle->address); info->idProduct = 0x5678;
  wcscpy_s(info->iProduct, L"Native storage");
  wcscpy_s(info->iSerialNumber, L"serial-123");
  return ESP_OK;
}
esp_err_t msc_host_vfs_register(msc_host_device_handle_t handle, const char* path,
                              const esp_vfs_fat_mount_config_t* config, msc_host_vfs_handle_t* out) {
  fsContext(); assert(installed.count(handle) && descriptors.empty());
  const bool probe = !strcmp(path, "/usb-label");
  assert((probe || !strcmp(path, "/usb")) && !config->format_if_mount_failed);
  assert(config->max_files == (probe ? 1U : 4U) && config->allocation_unit_size == TRANSFER_CHUNK_BYTES);
  assert(!probeVfs && (probe ? handle != mountedHandle : !mountedVfs));
  auto& calls = probe ? probeMountCalls : mountCalls;
  auto& hook = probe ? duringProbeMount : duringMount;
  ++calls; lifecycle.push_back(std::string(probe ? "probe-mount:" : "mount:") + std::to_string(handle->address));
  if (hook) { auto callback = std::move(hook); hook = {}; callback(handle); }
  if (handle->address == (probe ? failProbeAddress : failMountAddress)) return ESP_FAIL;
  auto& identity = mountIdentities[++mountSequence]; identity = {handle, probe};
  *out = &identity; assert(registeredVfs.emplace(*out, true).second && registeredVfs.size() <= 2);
  if (probe) probeVfs = *out;
  else { disk = std::move(volumes.at(handle)); mountedHandle = handle; mountedVfs = *out; }
  return ESP_OK;
}
esp_err_t msc_host_vfs_unregister(msc_host_vfs_handle_t handle) {
  fsContext(); assert(handle && registeredVfs.erase(handle) == 1 && descriptors.empty());
  if (handle->probe) { assert(handle == probeVfs); probeVfs = nullptr; ++probeUnmountCalls; }
  else {
    assert(handle == mountedVfs && handle->device == mountedHandle);
    volumes.at(mountedHandle) = std::move(disk); disk.clear(); mountedHandle = nullptr; mountedVfs = nullptr;
    ++unmountCalls;
  }
  lifecycle.push_back(std::string(handle->probe ? "probe-unmount:" : "unmount:") + std::to_string(handle->device->address));
  return ESP_OK;
}
esp_err_t ofe_usb_volume_label(msc_host_vfs_handle_t handle, char* output, size_t capacity) {
  fsContext(); assert(handle && registeredVfs.count(handle) && installed.count(handle->device));
  assert(output && capacity == 64);
  labelReads.push_back(handle); output[0] = '\0';
  if (duringLabel) { auto hook = std::move(duringLabel); duringLabel = {}; hook(handle); }
  const auto& label = volumeLabels.at(handle->device);
  if (handle->device->address == failLabelAddress || label.size() >= capacity) return ESP_FAIL;
  strlcpy(output, label.c_str(), capacity); return ESP_OK;
}
esp_err_t msc_host_uninstall_device(msc_host_device_handle_t handle) {
  fsContext(); assert(handle && mountedHandle != handle && installed.erase(handle) == 1);
  for (const auto& mount : registeredVfs) assert(mount.first->device != handle);
  ++uninstallCalls; lifecycle.push_back("uninstall:" + std::to_string(handle->address));
  return ESP_OK;
}
void disconnectDrive(unsigned);
void resetMsc() {
  assert(!workerContext && !guardDepth && descriptors.empty() && !allocated);
  workerContext = true;
  for (unsigned i = 0; i < MAX_DRIVES; ++i) if (drives[i].handle.load()) disconnectDrive(i);
  workerContext = false;
  assert(installed.empty() && !mountedHandle && !mountedVfs && !probeVfs && registeredVfs.empty());
  for (auto& drive : drives) { drive.~Drive(); new (&drive) Drive(); }
  for (auto& pending : pendingAddresses) pending = 0;
  present = ready = gone = false; selectedDrive = NO_DRIVE; activeDevice = nullptr;
  device = nullptr; vfs = nullptr; nextDriveInstance = 0; autoSelectDrive = true; driveLimitReached = false;
  installInProgress = installDisconnectOverflow = false;
  for (auto& handle : installDisconnects) handle = nullptr;
  identities.clear(); volumes.clear(); lifecycle.clear();
  labelsForAddress.clear(); volumeLabels.clear(); mountIdentities.clear(); labelReads.clear();
  identitySequence = installCalls = infoCalls = uninstallCalls = mountCalls = unmountCalls = 0;
  mountSequence = probeMountCalls = probeUnmountCalls = 0;
  failInstallAddress = failInfoAddress = failMountAddress = 0; partialInstallFailure = false;
  failProbeAddress = failLabelAddress = 0;
  duringInstall = duringInfo = duringMount = duringProbeMount = {}; duringLabel = {};
}
