#include "MasterUsbStorage.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "UsbStoragePolicy.h"
#include "UsbVolumeLabel.h"
#include "MasterUsbMonitor.h"
#include "JsonTextEscape.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include <esp_heap_caps.h>
#include <atomic>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <new>
#include <freertos/semphr.h>

namespace ofe_usb {
namespace {
enum class Phase { Idle, Queued, Reading, Ready, Receiving, Writing, Error };
struct Entry { char name[MAX_NAME]; size_t size; unsigned directory; };
struct Directory { char path[MAX_PATH]{}; };
struct State {
  Entry files[MAX_FILES]{};
  Directory directories[MAX_DIRECTORIES]{};
  size_t directoryCount = 1;
  size_t count = 0;
  uint32_t generation = 1, token = 0, touched = 0;
  Phase phase = Phase::Idle;
  char message[96] = "Starting USB host";
  char operation[12]{}, filename[MAX_NAME]{};
  unsigned selected = 0;
  unsigned destination = 0;
  uint32_t operationId = 0, completedOperation = 0;
  char result[24]{};
  char writeDirectory[MAX_PATH] = "OFE";
  bool createDefaultDirectory = true;
  uint8_t* buffer = nullptr;
  size_t size = 0, received = 0;
  int descriptor = -1;
  char temporary[MAX_PATH + 24]{}, finalPath[MAX_PATH + 6]{};
  bool upload = false, sending = false, cancel = false, chunkOk = false;
  size_t chunkOffset = 0, chunkSize = 0;
  unsigned requestedDrive = NO_DRIVE;
  uint32_t requestedInstance = 0;
  bool truncated = false;
};
struct Drive {
  std::atomic<msc_host_device_handle_t> handle{nullptr};
  std::atomic<bool> connected{false}, removed{false};
  unsigned address = 0;
  uint32_t instance = 0;
  char label[96]{}, serial[MSC_STR_DESC_SIZE]{}, volumeName[64]{};
  uint16_t vendor = 0, product = 0;
  uint64_t capacity = 0;
};
State* state = nullptr;
SemaphoreHandle_t mutex = nullptr;
std::atomic<bool> present{false}, ready{false}, gone{false}, started{false};
Drive drives[MAX_DRIVES];
std::atomic<unsigned> selectedDrive{NO_DRIVE};
std::atomic<uint32_t> pendingAddresses[4]{};
uint32_t nextDriveInstance = 0;
bool autoSelectDrive = true;
std::atomic<bool> driveLimitReached{false};
bool installInProgress = false, installDisconnectOverflow = false;
msc_host_device_handle_t installDisconnects[MAX_DRIVES + 1]{};
msc_host_device_handle_t device = nullptr;
std::atomic<msc_host_device_handle_t> activeDevice{nullptr};
msc_host_vfs_handle_t vfs = nullptr;

struct Guard {
  Guard() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(mutex); }
};
void message(const char* text, Phase phase) {
  Guard guard;
  strlcpy(state->message, text, sizeof(state->message));
  state->phase = phase;
}
void freeBuffer() {
  heap_caps_free(state->buffer);
  state->buffer = nullptr;
  state->size = state->received = 0;
  state->token = 0;
  state->upload = state->sending = state->cancel = state->chunkOk = false;
  state->chunkOffset = state->chunkSize = 0;
}
// Only the USB worker touches filesystem handles, including cancellation/unmount.
void discardTransfer() {
  if (state->descriptor >= 0) close(state->descriptor);
  if (*state->temporary && present.load()) unlink(state->temporary);
  Guard guard;
  state->descriptor = -1;
  state->temporary[0] = state->finalPath[0] = '\0';
  freeBuffer();
}
bool busy() {
  return state->phase == Phase::Queued || state->phase == Phase::Reading ||
         state->phase == Phase::Writing || state->phase == Phase::Receiving || state->buffer;
}
void event(const msc_host_event_t* evt, void*) {
  if (evt->event == msc_host_event_t::MSC_DEVICE_CONNECTED) {
    Serial.printf("[USB] MSC accepted addr=%u\n", evt->device.address);
    const unsigned address = evt->device.address;
    if (address && address < 128) pendingAddresses[address / 32].fetch_or(uint32_t(1) << (address % 32));
  } else if (evt->event == msc_host_event_t::MSC_DEVICE_DISCONNECTED) {
    Guard guard;
    for (unsigned i = 0; i < MAX_DRIVES; ++i) if (drives[i].handle.load() == evt->device.handle) {
      drives[i].connected.store(false); drives[i].removed.store(true);
      if (selectedDrive.load() == i) { present.store(false); ready.store(false); gone.store(true); }
      return;
    }
    // Installation can receive GONE before its output handle is published.
    if (installInProgress && evt->device.handle) {
      for (auto& handle : installDisconnects) if (handle == evt->device.handle) return;
      for (auto& handle : installDisconnects) if (!handle) { handle = evt->device.handle; return; }
      installDisconnectOverflow = true;
    }
  }
}
void hostTask(void*) {
  for (;;) {
    uint32_t flags = 0;
    const esp_err_t error = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
    if (error == ESP_OK && (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) usb_host_device_free_all();
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) monitor::eventError(error);
    vTaskDelay(1);
  }
}
void unmount() {
  ready.store(false);
  discardTransfer();
  if (vfs) { msc_host_vfs_unregister(vfs); vfs = nullptr; }
  activeDevice.store(nullptr);
  device = nullptr;
  Guard guard;
  state->count = 0;
  state->directoryCount = 1;
  state->directories[0].path[0] = '\0';
  ++state->generation;
}
void listDirectory(unsigned directory, size_t& visited, uint32_t began) {
  char path[MAX_PATH + 6];
  snprintf(path, sizeof(path), "/usb%s%s", *state->directories[directory].path ? "/" : "", state->directories[directory].path);
  DIR* dir = opendir(path);
  if (!dir) return;
  while (present.load()) {
    if (++visited > MAX_SCAN_ENTRIES || uint32_t(millis() - began) >= SCAN_TIMEOUT_MS) { Guard guard; state->truncated = true; break; }
    dirent* item = readdir(dir);
    if (!item) break;
    if (!safeName(item->d_name) || !strcmp(item->d_name, "System Volume Information") || !strcmp(item->d_name, "$RECYCLE.BIN")) continue;
    char relative[MAX_PATH], full[MAX_PATH + 6];
    if (!joinPath(relative, sizeof(relative), state->directories[directory].path, item->d_name)) { Guard guard; state->truncated = true; continue; }
    snprintf(full, sizeof(full), "/usb/%s", relative);
    struct stat st{};
    if (stat(full, &st)) continue;
    { Guard guard;
      if (S_ISDIR(st.st_mode)) {
        if (state->directoryCount == MAX_DIRECTORIES || pathDepth(relative) > MAX_DEPTH) state->truncated = true;
        else strlcpy(state->directories[state->directoryCount++].path, relative, MAX_PATH);
      } else if (S_ISREG(st.st_mode) && visibleFile(item->d_name) && st.st_size >= 0) {
        if (state->count == MAX_FILES || uint64_t(st.st_size) > UINT32_MAX) state->truncated = true;
        else {
          Entry& entry = state->files[state->count++];
          strlcpy(entry.name, item->d_name, sizeof(entry.name));
          entry.size = size_t(st.st_size);
          entry.directory = directory;
        }
      }
    }
    vTaskDelay(1);
  }
  closedir(dir);
}
void refresh(bool finish = true) {
  { Guard guard; if (finish) state->phase = Phase::Reading; state->count = 0; state->directoryCount = 1; state->directories[0].path[0] = '\0'; state->truncated = false; ++state->generation; }
  size_t visited = 0;
  const uint32_t began = millis();
  // Breadth-first traversal keeps only one directory handle open.
  for (unsigned index = 0; index < state->directoryCount && present.load(); ++index) {
    listDirectory(index, visited, began);
    if (visited >= MAX_SCAN_ENTRIES || uint32_t(millis() - began) >= SCAN_TIMEOUT_MS) break;
  }
  if (finish) message(present.load() ? "Ready" : "USB disconnected", present.load() ? Phase::Idle : Phase::Error);
}
void cacheVolumeLabel(unsigned index, msc_host_device_handle_t handle, msc_host_vfs_handle_t mounted) {
  char label[64]{};
  if (ofe_usb_volume_label(mounted, label, sizeof(label)) != ESP_OK) return;
  Guard guard;
  if (index < MAX_DRIVES && drives[index].connected.load() && drives[index].handle.load() == handle)
    strlcpy(drives[index].volumeName, label, sizeof(drives[index].volumeName));
}
void probeVolumeLabel(unsigned index) {
  const auto handle = drives[index].handle.load();
  if (!handle || !drives[index].connected.load()) return;
  esp_vfs_fat_mount_config_t config{};
  config.format_if_mount_failed = false; config.max_files = 1; config.allocation_unit_size = 8192;
  msc_host_vfs_handle_t probe = nullptr;
  if (msc_host_vfs_register(handle, "/usb-label", &config, &probe) != ESP_OK) return;
  cacheVolumeLabel(index, handle, probe);
  msc_host_vfs_unregister(probe);
}
void activateDrive(unsigned index, uint32_t instance) {
  { Guard guard;
    if (index >= MAX_DRIVES || !drives[index].connected.load() ||
        !drives[index].handle.load() || drives[index].instance != instance) {
      strlcpy(state->message, "USB drive changed or disconnected", sizeof(state->message)); state->phase = Phase::Error; return;
    }
  }
  unmount();
  { Guard guard;
    selectedDrive.store(index); autoSelectDrive = false;
    device = drives[index].handle.load(); activeDevice.store(device);
    present.store(drives[index].connected.load());
  }
  if (!present.load()) { message("USB drive disconnected", Phase::Error); return; }
  esp_vfs_fat_mount_config_t config{};
  config.format_if_mount_failed = false; config.max_files = 4; config.allocation_unit_size = 8192;
  const esp_err_t err = msc_host_vfs_register(device, "/usb", &config, &vfs);
  Serial.printf("[USB] FAT mount drive=%u: %s (0x%X)\n", index + 1, esp_err_to_name(err), unsigned(err));
  if (err != ESP_OK || !drives[index].connected.load()) {
    unmount();
    message("Cannot mount FAT volume; stick was NOT formatted", Phase::Error); return;
  }
  cacheVolumeLabel(index, device, vfs);
  bool connected;
  { Guard guard;
    connected = drives[index].connected.load();
    if (connected) { state->phase = Phase::Reading; ready.store(true); }
  }
  if (!connected) { unmount(); message("USB drive disconnected", Phase::Error); return; }
  refresh();
}
void disconnectDrive(unsigned index) {
  if (selectedDrive.load() == index) {
    bool keepSelection;
    { Guard guard; keepSelection = state->phase == Phase::Queued && !strcmp(state->operation, "drive") && state->requestedDrive != index; }
    present.store(false); unmount();
    selectedDrive.store(NO_DRIVE); autoSelectDrive = false; gone.store(false);
    if (!keepSelection) message("USB drive disconnected; select another drive", Phase::Error);
  }
  const auto handle = drives[index].handle.exchange(nullptr);
  if (handle) msc_host_uninstall_device(handle);
  Guard guard;
  drives[index].connected.store(false); drives[index].address = 0; drives[index].instance = 0;
  drives[index].label[0] = drives[index].serial[0] = drives[index].volumeName[0] = '\0';
  drives[index].vendor = drives[index].product = 0;
  drives[index].capacity = 0; driveLimitReached.store(false);
}
void descriptorText(char* out, size_t size, const wchar_t* value) {
  size_t n = 0;
  while (n + 1 < size && n < MSC_STR_DESC_SIZE && value[n]) {
    out[n] = value[n] >= 32 && value[n] < 127 ? char(value[n]) : '?'; ++n;
  }
  out[n] = '\0';
}
void mount(unsigned address) {
  unsigned slot = NO_DRIVE;
  { Guard guard;
    for (unsigned i = 0; i < MAX_DRIVES; ++i) if (drives[i].address == address && drives[i].handle.load()) return;
    for (unsigned i = 0; i < MAX_DRIVES; ++i) if (!drives[i].handle.load()) { slot = i; break; }
    if (slot == NO_DRIVE) { driveLimitReached.store(true); return; }
  }
  msc_host_device_handle_t handle = nullptr;
  { Guard guard;
    installInProgress = true; installDisconnectOverflow = false;
    for (auto& disconnected : installDisconnects) disconnected = nullptr;
  }
  const esp_err_t err = msc_host_install_device(uint8_t(address), &handle);
  Serial.printf("[USB] MSC install addr=%u: %s (0x%X)\n", address, esp_err_to_name(err), unsigned(err));
  if (err != ESP_OK || !handle) {
    { Guard guard; installInProgress = false; }
    if (handle) msc_host_uninstall_device(handle);
    { Guard guard;
      const bool queuedSelection = state->phase == Phase::Queued && !strcmp(state->operation, "drive");
      if (selectedDrive.load() == NO_DRIVE && !queuedSelection) {
        strlcpy(state->message, "Cannot open USB drive", sizeof(state->message)); state->phase = Phase::Error;
      }
    }
    return;
  }
  uint32_t instance;
  { Guard guard;
    Drive& drive = drives[slot];
    if (!++nextDriveInstance) ++nextDriveInstance;
    instance = drive.instance = nextDriveInstance; drive.address = address;
    bool disconnected = installDisconnectOverflow;
    for (const auto removed : installDisconnects) if (removed == handle) disconnected = true;
    drive.removed.store(disconnected); drive.connected.store(!disconnected); drive.handle.store(handle);
    installInProgress = false;
    snprintf(drive.label, sizeof(drive.label), "USB %u", slot + 1);
  }
  msc_host_device_info_t info{};
  if (drives[slot].connected.load() && msc_host_get_device_info(handle, &info) == ESP_OK) {
    char product[MSC_STR_DESC_SIZE]{}; descriptorText(product, sizeof(product), info.iProduct);
    Guard guard;
    Drive& drive = drives[slot];
    drive.vendor = info.idVendor; drive.product = info.idProduct;
    drive.capacity = uint64_t(info.sector_count) * info.sector_size;
    descriptorText(drive.serial, sizeof(drive.serial), info.iSerialNumber);
    snprintf(drive.label, sizeof(drive.label), "USB %u%s%s", slot + 1, *product ? " - " : "", product);
  }
  if (drives[slot].connected.load()) probeVolumeLabel(slot);
  if (autoSelectDrive && selectedDrive.load() == NO_DRIVE && drives[slot].connected.load()) activateDrive(slot, instance);
}
void processDevices() {
  for (unsigned i = 0; i < MAX_DRIVES; ++i) if (drives[i].removed.exchange(false)) disconnectDrive(i);
  { Guard guard;
    if (state->buffer || (state->phase == Phase::Queued && !strcmp(state->operation, "drive"))) return;
  }
  // Keep all connection notices while a transfer runs; callbacks never replace a live volume.
  for (unsigned word = 0; word < 4; ++word) {
    const uint32_t pending = pendingAddresses[word].load();
    if (!pending) continue;
    for (unsigned bit = 0; bit < 32; ++bit) if (pending & (uint32_t(1) << bit)) {
      pendingAddresses[word].fetch_and(~(uint32_t(1) << bit));
      mount(word * 32 + bit); return;
    }
  }
}
void readSelected() {
  Entry entry{};
  { Guard guard; entry = state->files[state->selected]; state->phase = Phase::Reading; }
  char relative[MAX_PATH], path[MAX_PATH + 6];
  if (entry.size > MAX_FILE_BYTES || !joinPath(relative, sizeof(relative), state->directories[entry.directory].path, entry.name)) {
    message("USB file exceeds transfer limit", Phase::Error); return;
  }
  snprintf(path, sizeof(path), "/usb/%s", relative);
  const int file = open(path, O_RDONLY);
  struct stat st{};
  uint8_t* data = nullptr;
  bool ok = file >= 0 && fstat(file, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 && size_t(st.st_size) == entry.size;
  if (ok) data = static_cast<uint8_t*>(heap_caps_malloc(2 * TRANSFER_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ok = ok && data;
  if (!ok || !present.load()) { if (file >= 0) close(file); heap_caps_free(data); message("USB read failed", Phase::Error); return; }
  Guard guard;
  state->buffer = data; state->size = entry.size; state->received = 0;
  state->descriptor = file; state->upload = false; state->sending = state->cancel = false;
  strlcpy(state->filename, entry.name, sizeof(state->filename));
  state->token = esp_random() | 1; state->touched = millis();
  state->phase = Phase::Ready;
  strlcpy(state->message, "File ready", sizeof(state->message));
}
void openWrite() {
  char relative[MAX_PATH], path[MAX_PATH + 6], temporary[MAX_PATH + 24];
  if (!joinPath(relative, sizeof(relative), state->writeDirectory, state->filename)) { { Guard guard; freeBuffer(); } message("USB path too long", Phase::Error); return; }
  snprintf(path, sizeof(path), "/usb/%s", relative);
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  bool ok = !state->createDefaultDirectory || mkdir("/usb/OFE", 0755) == 0 || errno == EEXIST;
  struct stat existing{};
  if (stat(path, &existing) == 0) ok = false;
  int fd = ok ? open(temporary, O_RDWR | O_CREAT | O_EXCL, 0600) : -1;
  // An unplugged stick can retain a partial file; never overwrite it or block retries.
  for (unsigned attempt = 0; ok && fd < 0 && errno == EEXIST && attempt < 8; ++attempt) {
    snprintf(temporary, sizeof(temporary), "%s.%08X.tmp", path, unsigned(esp_random()));
    fd = open(temporary, O_RDWR | O_CREAT | O_EXCL, 0600);
  }
  ok = ok && fd >= 0;
  Guard guard;
  if (ok) {
    state->descriptor = fd;
    strlcpy(state->temporary, temporary, sizeof(state->temporary));
    strlcpy(state->finalPath, path, sizeof(state->finalPath));
    state->phase = Phase::Receiving;
  } else {
    state->phase = Phase::Error;
    strlcpy(state->message, "Cannot create USB file; destination or temporary file may already exist", sizeof(state->message));
  }
}
void transferChunk(bool writing) {
  const size_t count = state->chunkSize, offset = state->chunkOffset;
  const bool duplicate = writing && offset < state->received;
  bool ok = present.load() && state->descriptor >= 0 && count <= TRANSFER_CHUNK_BYTES;
  if (ok && duplicate) {
    ok = count <= state->received - offset && lseek(state->descriptor, off_t(offset), SEEK_SET) == off_t(offset);
    if (ok) ok = read(state->descriptor, state->buffer + TRANSFER_CHUNK_BYTES, count) == ssize_t(count) &&
                 !memcmp(state->buffer, state->buffer + TRANSFER_CHUNK_BYTES, count);
    if (lseek(state->descriptor, off_t(state->received), SEEK_SET) != off_t(state->received)) ok = false;
  } else if (ok) {
    ok = offset == state->received && range(state->size, offset, count);
    size_t done = 0;
    while (ok && done < count && present.load()) {
      const ssize_t n = writing ? write(state->descriptor, state->buffer + done, count - done) :
                                  read(state->descriptor, state->buffer + done, count - done);
      if (n <= 0 || size_t(n) > count - done) ok = false;
      else done += size_t(n);
    }
    ok = ok && done == count && present.load();
  }
  Guard guard;
  state->chunkOk = ok;
  if (ok && !duplicate) state->received += count;
  state->touched = millis();
  state->phase = ok ? (writing ? Phase::Receiving : Phase::Ready) : Phase::Error;
  if (!ok) strlcpy(state->message, "USB chunk failed; no destination file was replaced", sizeof(state->message));
}
void writeStaged() {
  bool ok = present.load() && state->descriptor >= 0 && state->received == state->size;
  if (state->descriptor >= 0) {
    if (ok) ok = fsync(state->descriptor) == 0;
    if (close(state->descriptor)) ok = false;
    state->descriptor = -1;
  }
  struct stat existing{};
  if (ok) ok = stat(state->finalPath, &existing) != 0 && errno == ENOENT;
  if (ok) ok = present.load() && rename(state->temporary, state->finalPath) == 0;
  if (ok) state->temporary[0] = '\0';
  discardTransfer();
  if (ok) refresh(false);
  message(ok ? "Saved" : "USB write failed; existing files were not overwritten", ok ? Phase::Idle : Phase::Error);
}
void manageSelected(const char* action) {
  char relative[MAX_PATH]{}, source[MAX_PATH + 6]{}, target[MAX_PATH + 6]{};
  const char* result = "OK";
  const uint32_t operation = state->operationId;
  Entry entry{};
  struct stat st{};
  if (!present.load()) result = "DISCONNECTED";
  else if (!strcmp(action, "mkdir")) {
    if (!joinPath(relative, sizeof(relative), state->directories[state->destination].path, state->filename) || pathDepth(relative) > MAX_DEPTH) result = "PATH";
    else {
      snprintf(target, sizeof(target), "/usb/%s", relative);
      if (mkdir(target, 0755)) result = errno == EEXIST ? "EXISTS" : "IO";
    }
  } else if (!strcmp(action, "move_dir")) {
    const char* oldPath = state->directories[state->selected].path;
    const char* destination = state->directories[state->destination].path;
    const size_t oldLength = strlen(oldPath);
    if (state->truncated) result = "LIMIT";
    else if (!strcmp(destination, oldPath) ||
             (!strncmp(destination, oldPath, oldLength) && destination[oldLength] == '/')) result = "SUBTREE";
    else if (!joinPath(relative, sizeof(relative), destination, state->filename) || pathDepth(relative) > MAX_DEPTH) result = "PATH";
    else {
      const size_t newLength = strlen(relative);
      // Keep every indexed descendant addressable after the atomic directory rename.
      for (size_t i = 1; i < state->directoryCount && !strcmp(result, "OK"); ++i) {
        const char* child = state->directories[i].path;
        if (!strncmp(child, oldPath, oldLength) && child[oldLength] == '/') {
          if (newLength + strlen(child + oldLength) >= MAX_PATH ||
              pathDepth(relative) + pathDepth(child) - pathDepth(oldPath) > MAX_DEPTH) result = "PATH";
        }
      }
      for (size_t i = 0; i < state->count && !strcmp(result, "OK"); ++i) {
        const Entry& child = state->files[i];
        const char* directory = state->directories[child.directory].path;
        if ((!strcmp(directory, oldPath) || (!strncmp(directory, oldPath, oldLength) && directory[oldLength] == '/')) &&
            newLength + strlen(directory + oldLength) + 1 + strlen(child.name) >= MAX_PATH) result = "PATH";
      }
      snprintf(source, sizeof(source), "/usb/%s", oldPath);
      snprintf(target, sizeof(target), "/usb/%s", relative);
      if (!strcmp(result, "OK")) {
        if (!strcmp(source, target)) result = "SAME_PATH";
        else if (stat(source, &st) || !S_ISDIR(st.st_mode)) result = "STALE";
        else if (!stat(target, &st) || errno != ENOENT) result = "EXISTS";
        else if (!present.load() || rename(source, target)) result = "IO";
      }
    }
  } else if (!strcmp(action, "rmdir")) {
    snprintf(source, sizeof(source), "/usb/%s", state->directories[state->selected].path);
    DIR* directory = opendir(source);
    if (!directory) result = "IO";
    else {
      while (dirent* item = readdir(directory)) {
        if (strcmp(item->d_name, ".") && strcmp(item->d_name, "..")) { result = "NOT_EMPTY"; break; }
      }
      closedir(directory);
      if (!present.load()) result = "DISCONNECTED";
      else if (!strcmp(result, "OK") && rmdir(source)) result = "IO";
    }
  } else {
    entry = state->files[state->selected];
    if (!joinPath(relative, sizeof(relative), state->directories[entry.directory].path, entry.name)) result = "PATH";
    else {
      snprintf(source, sizeof(source), "/usb/%s", relative);
      if (stat(source, &st) || !S_ISREG(st.st_mode) || uint64_t(st.st_size) != entry.size) result = "STALE";
      else if (!strcmp(action, "delete")) {
        if (unlink(source)) result = "IO";
      } else if (!strcmp(action, "move")) {
        if (!joinPath(relative, sizeof(relative), state->directories[state->destination].path, *state->filename ? state->filename : entry.name)) result = "PATH";
        else {
          snprintf(target, sizeof(target), "/usb/%s", relative);
          if (!strcmp(source, target)) result = "SAME_PATH";
          else if (!stat(target, &st) || errno != ENOENT) result = "EXISTS";
          else if (!present.load() || rename(source, target)) result = "IO";
        }
      } else result = "INVALID";
    }
  }
  if (!strcmp(result, "OK") && present.load()) refresh(false);
  if (!present.load()) result = "DISCONNECTED";
  Guard guard;
  state->completedOperation = operation;
  strlcpy(state->result, result, sizeof(state->result));
  strlcpy(state->message, result, sizeof(state->message));
  state->phase = !strcmp(result, "OK") ? Phase::Idle : Phase::Error;
}
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
bool allowEnumeration(const usb_device_desc_t* descriptor, uint8_t* configuration) {
  // With the SDK filter enabled, a null callback rejects every device, including hubs.
  Serial.printf("[USB] Enumerate VID=%04X PID=%04X class=%02X config=%u: allow\n",
    descriptor->idVendor, descriptor->idProduct, descriptor->bDeviceClass, *configuration);
  return true;
}
#endif
void worker(void*) {
  usb_host_config_t host{};
  host.intr_flags = ESP_INTR_FLAG_LEVEL1;
  host.peripheral_map = 1U;  // P4 USB-DWC HS / UTMI, not the separate FS peripheral.
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  host.enum_filter_cb = allowEnumeration;
#endif
  esp_err_t err = usb_host_install(&host);
  monitor::hostResult(err);
  if (err != ESP_OK) { message("USB host unavailable (USB-A jumper must be HOST)", Phase::Error); vTaskDelete(nullptr); return; }
  msc_host_driver_config_t config{};
  config.create_backround_task = true; config.task_priority = 2; config.stack_size = 4096;
  config.core_id = 0; config.callback = event;
  err = msc_host_install(&config);
  monitor::mscResult(err);
  if (err != ESP_OK) { message("USB MSC driver unavailable", Phase::Error); vTaskDelete(nullptr); return; }
  monitor::begin();
  // Register both clients before pumping enumeration; a boot-time attachment must not be missed.
  if (xTaskCreatePinnedToCore(hostTask, "usb_events", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
    monitor::eventError(ESP_ERR_NO_MEM);
    message("USB event task allocation failed", Phase::Error); vTaskDelete(nullptr); return;
  }
  message("No USB stick", Phase::Idle);
  for (;;) {
    processDevices();
    char action[12]{};
    { Guard guard;
      if (state->buffer && !state->sending && (state->cancel ||
          ((state->phase == Phase::Ready || state->phase == Phase::Receiving || state->phase == Phase::Error) &&
           uint32_t(millis() - state->touched) > BUFFER_LEASE_MS))) {
        strlcpy(action, "release", sizeof(action)); state->phase = Phase::Writing;
      } else if (state->phase == Phase::Queued) {
        strlcpy(action, state->operation, sizeof(action));
        state->phase = (!strcmp(action, "read") || !strcmp(action, "refresh")) ? Phase::Reading : Phase::Writing;
      }
    }
    if (*action) {
      if (!strcmp(action, "drive")) activateDrive(state->requestedDrive, state->requestedInstance);
      else if (!ready.load()) message("No mounted USB stick", Phase::Error);
      else if (!strcmp(action, "read")) readSelected();
      else if (!strcmp(action, "open_write")) openWrite();
      else if (!strcmp(action, "chunk")) transferChunk(true);
      else if (!strcmp(action, "read_chunk")) transferChunk(false);
      else if (!strcmp(action, "release")) { discardTransfer(); message("Ready", Phase::Idle); }
      else if (!strcmp(action, "write")) writeStaged();
      else if (!strcmp(action, "move") || !strcmp(action, "move_dir") || !strcmp(action, "delete") || !strcmp(action, "mkdir") || !strcmp(action, "rmdir")) manageSelected(action);
      else if (!strcmp(action, "refresh")) refresh();
      else if (!strcmp(action, "eject")) { unmount(); message("Ejected; safe to remove or select drive to remount", Phase::Idle); }
    }
    bool transferActive;
    { Guard guard; transferActive = state->buffer != nullptr; }
    vTaskDelay(pdMS_TO_TICKS(transferActive ? 1 : 20));
  }
}
void quoted(String& json, const char* text) {
  json += '"';
  ofe_json::appendEscaped(text, [&](const char* data, size_t count) { return json.concat(data, count); });
  json += '"';
}
}  // namespace

void begin() {
  if (started.exchange(true)) return;
  state = static_cast<State*>(heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  mutex = xSemaphoreCreateMutex();
  if (!state || !mutex) { heap_caps_free(state); state = nullptr; if (mutex) vSemaphoreDelete(mutex); mutex = nullptr; return; }
  new (state) State();
  if (xTaskCreatePinnedToCore(worker, "usb_storage", 8192, nullptr, 1, nullptr, 0) != pdPASS)
    message("USB worker allocation failed", Phase::Error);
}
bool detected() {
  for (const Drive& drive : drives) if (drive.connected.load()) return true;
  for (const auto& pending : pendingAddresses) if (pending.load()) return true;
  return false;
}
bool mounted() { return ready.load(); }
bool matchesDrive(unsigned index, uint32_t instance) {
  if (!state || !mutex || index >= MAX_DRIVES || !instance) return false;
  Guard guard;
  return selectedDrive.load() == index && drives[index].connected.load() && drives[index].instance == instance;
}
bool selectDrive(uint32_t generation, unsigned index, uint32_t instance) {
  if (!state || !mutex || index >= MAX_DRIVES || !instance) return false;
  Guard guard;
  if (busy() || generation != state->generation || !drives[index].connected.load() ||
      !drives[index].handle.load() || drives[index].instance != instance) return false;
  state->requestedDrive = index; state->requestedInstance = instance;
  strlcpy(state->operation, "drive", sizeof(state->operation)); state->phase = Phase::Queued;
  return true;
}
String statusJson(bool developerMode) {
  if (!state || !mutex) return "{\"detected\":false,\"mounted\":false,\"error\":\"USB host allocation failed\"}";
  Guard guard;
  const char* phases[] = {"idle", "queued", "reading", "ready", "receiving", "writing", "error"};
  String json;
  json.reserve(900 + state->count * (MAX_PATH + 140) + state->directoryCount * (MAX_PATH + 40));
  json = "{\"detected\":"; json += detected() ? "true" : "false";
  json += ",\"mounted\":"; json += mounted() ? "true" : "false";
  json += ",\"phase\":"; quoted(json, phases[unsigned(state->phase)]);
  json += ",\"message\":"; quoted(json, state->message);
  json += ",\"generation\":"; json += state->generation;
  json += ",\"token\":"; json += state->token;
  json += ",\"size\":"; json += unsigned(state->size);
  json += ",\"received\":"; json += unsigned(state->received);
  json += ",\"truncated\":"; json += state->truncated ? "true" : "false";
  json += ",\"file_limit\":"; json += unsigned(MAX_FILES);
  json += ",\"directory_limit\":"; json += unsigned(MAX_DIRECTORIES);
  json += ",\"transfer_limit\":"; json += unsigned(MAX_FILE_BYTES);
  json += ",\"buffered_read\":false,\"chunk_limit\":"; json += unsigned(TRANSFER_CHUNK_BYTES);
  const unsigned selected = selectedDrive.load();
  json += ",\"drive_limit\":"; json += MAX_DRIVES;
  json += ",\"drive_limit_reached\":"; json += driveLimitReached.load() ? "true" : "false";
  json += ",\"drive\":"; json += selected;
  json += ",\"drive_instance\":"; json += selected < MAX_DRIVES ? drives[selected].instance : 0;
  json += ",\"drives\":[";
  bool first = true;
  for (unsigned i = 0; i < MAX_DRIVES; ++i) {
    const Drive& drive = drives[i];
    if (!drive.handle.load()) continue;
    if (!first) json += ',';
    first = false;
    json += "{\"index\":"; json += i;
    json += ",\"instance\":"; json += drive.instance;
    json += ",\"label\":"; quoted(json, drive.label);
    json += ",\"volume_name\":"; quoted(json, drive.volumeName);
    json += ",\"serial\":"; quoted(json, drive.serial);
    json += ",\"vendor\":"; json += drive.vendor;
    json += ",\"product\":"; json += drive.product;
    char capacity[24]; snprintf(capacity, sizeof(capacity), "%llu", static_cast<unsigned long long>(drive.capacity));
    json += ",\"capacity_bytes\":"; json += capacity;
    json += ",\"connected\":"; json += drive.connected.load() ? "true" : "false";
    json += ",\"selected\":"; json += selected == i ? "true" : "false";
    json += ",\"mounted\":"; json += selected == i && ready.load() ? "true" : "false";
    json += '}';
  }
  json += ']';
  json += ",\"completed_operation\":"; json += state->completedOperation;
  json += ",\"result\":"; quoted(json, state->result);
  json += ",\"developer_mode\":"; json += developerMode ? "true" : "false";
  if (developerMode) monitor::appendJson(json);
  json += ",\"files\":[";
  for (size_t i = 0; i < state->count; ++i) {
    if (i) json += ',';
    json += "{\"index\":"; json += unsigned(i); json += ",\"name\":"; quoted(json, state->files[i].name);
    json += ",\"size\":"; json += unsigned(state->files[i].size);
    json += ",\"folder\":"; json += state->files[i].directory ? "true" : "false";
    json += ",\"directory\":"; json += state->files[i].directory;
    char path[MAX_PATH]{};
    joinPath(path, sizeof(path), state->directories[state->files[i].directory].path, state->files[i].name);
    json += ",\"path\":"; quoted(json, path); json += '}';
  }
  json += "],\"directories\":[";
  for (size_t i = 0; i < state->directoryCount; ++i) {
    if (i) json += ',';
    json += "{\"index\":"; json += unsigned(i);
    json += ",\"path\":"; quoted(json, state->directories[i].path); json += '}';
  }
  json += "]}";
  return json;
}
bool command(const String& action, uint32_t generation, unsigned index) {
  if (!state || !mutex) return false;
  Guard guard;
  if (!ready.load() || busy() || generation != state->generation) return false;
  if (action != "read" && action != "refresh" && action != "eject") return false;
  if (action == "read" && (generation != state->generation || index >= state->count)) return false;
  state->selected = index;
  action.toCharArray(state->operation, sizeof(state->operation));
  state->phase = Phase::Queued;
  return true;
}
uint32_t manage(const String& action, uint32_t generation, unsigned index, unsigned directory, const char* name) {
  if (!state || !mutex || !name) return 0;
  Guard guard;
  if (!ready.load() || busy() || generation != state->generation || directory >= state->directoryCount) return 0;
  if (action == "mkdir") {
    if (!safeName(name)) return 0;
  } else if (action == "move_dir") {
    if (!index || index >= state->directoryCount || !safeName(name)) return 0;
  } else if (action == "rmdir") {
    if (!index || index >= state->directoryCount) return 0;
  } else if (action == "move" || action == "delete") {
    if (index >= state->count || (*name && !visibleFile(name))) return 0;
  } else return 0;
  state->selected = index; state->destination = directory;
  strlcpy(state->filename, name, sizeof(state->filename));
  action.toCharArray(state->operation, sizeof(state->operation));
  if (!++state->operationId) ++state->operationId;
  state->phase = Phase::Queued;
  return state->operationId;
}
uint32_t receiveBegin(const char* name, size_t bytes, uint32_t generation, unsigned directory) {
  if (!state || !mutex || !visibleFile(name) || bytes > MAX_FILE_BYTES) return 0;
  Guard guard;
  if (!ready.load() || busy() || generation != state->generation) return 0;
  if (directory != DEFAULT_DIRECTORY && (generation != state->generation || directory >= state->directoryCount)) return 0;
  state->createDefaultDirectory = directory == DEFAULT_DIRECTORY;
  strlcpy(state->writeDirectory, directory == DEFAULT_DIRECTORY ? "OFE" : state->directories[directory].path, sizeof(state->writeDirectory));
  char path[MAX_PATH];
  if (!joinPath(path, sizeof(path), state->writeDirectory, name)) return 0;
  state->buffer = static_cast<uint8_t*>(heap_caps_malloc(2 * TRANSFER_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!state->buffer) return 0;
  state->size = bytes; state->received = 0;
  strlcpy(state->filename, name, sizeof(state->filename));
  state->token = esp_random() | 1; state->touched = millis();
  state->upload = true; state->sending = state->cancel = state->chunkOk = false;
  state->chunkSize = state->chunkOffset = 0;
  strlcpy(state->operation, "open_write", sizeof(state->operation)); state->phase = Phase::Queued;
  return state->token;
}
bool waitTransfer(uint32_t token, Phase phase) {
  const uint32_t began = millis();
  while (uint32_t(millis() - began) < 15000) {
    { Guard guard;
      if (!ready.load() || !present.load() || token != state->token || state->cancel || state->phase == Phase::Error) return false;
      if (state->phase == phase) return true;
    }
    vTaskDelay(1);
  }
  return false;
}
bool receiveChunk(uint32_t token, size_t offset, const uint8_t* data, size_t bytes) {
  if (!state || !mutex || !data || !bytes || bytes > TRANSFER_CHUNK_BYTES || !token || !waitTransfer(token, Phase::Receiving)) return false;
  { Guard guard;
    if (token != state->token || state->phase != Phase::Receiving || !state->upload || state->cancel ||
        !range(state->size, offset, bytes) || offset > state->received ||
        (offset < state->received && bytes > state->received - offset)) return false;
    memcpy(state->buffer, data, bytes);
    state->chunkOffset = offset; state->chunkSize = bytes; state->chunkOk = false;
    state->touched = millis();
    strlcpy(state->operation, "chunk", sizeof(state->operation)); state->phase = Phase::Queued;
  }
  if (!waitTransfer(token, Phase::Receiving)) return false;
  Guard guard;
  return token == state->token && state->chunkOk;
}
bool receiveCommit(uint32_t token) {
  if (!state || !mutex || !token || !waitTransfer(token, Phase::Receiving)) return false;
  Guard guard;
  if (token != state->token || state->phase != Phase::Receiving || !state->upload || state->cancel ||
      state->received != state->size || !ready.load()) return false;
  strlcpy(state->operation, "write", sizeof(state->operation)); state->phase = Phase::Queued;
  return true;
}
bool release(uint32_t token) {
  if (!state || !mutex) return false;
  Guard guard;
  if (!token || token != state->token || state->sending || !state->buffer) return false;
  if (state->upload && !strcmp(state->operation, "write") &&
      (state->phase == Phase::Queued || state->phase == Phase::Writing)) return false;
  state->cancel = true;
  return true;
}
bool sendFile(WebServer& server, uint32_t token) {
  if (!state || !mutex) return false;
  size_t total;
  char name[MAX_NAME];
  uint8_t* chunk = static_cast<uint8_t*>(heap_caps_malloc(TRANSFER_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!chunk) return false;
  { Guard guard;
    if (!token || token != state->token || state->phase != Phase::Ready || !state->buffer || state->upload || state->sending || state->cancel) {
      heap_caps_free(chunk); return false;
    }
    state->sending = true; state->touched = millis(); total = state->size;
    strlcpy(name, state->filename, sizeof(name));
  }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
  server.setContentLength(total);
  server.send(200, "application/octet-stream", "");
  auto client = server.client();
  bool ok = true;
  size_t offset = 0;
  while (ok && offset < total) {
    const size_t count = std::min(TRANSFER_CHUNK_BYTES, total - offset);
    { Guard guard;
      ok = ready.load() && present.load() && state->token == token && state->phase == Phase::Ready && !state->cancel;
      if (ok) {
        state->chunkOffset = offset; state->chunkSize = count; state->chunkOk = false;
        strlcpy(state->operation, "read_chunk", sizeof(state->operation)); state->phase = Phase::Queued;
      }
    }
    if (ok) ok = waitTransfer(token, Phase::Ready);
    if (ok) {
      Guard guard;
      ok = state->token == token && state->buffer && state->chunkOk && state->chunkSize == count;
      if (ok) memcpy(chunk, state->buffer, count);
    }
    if (ok) ok = client.connected() && client.write(chunk, count) == count;
    if (ok) offset += count;
    vTaskDelay(1);
  }
  heap_caps_free(chunk);
  if (!ok) client.stop(); // A short response must not be mistaken for a complete download.
  { Guard guard;
    if (state->token == token) { state->sending = false; state->cancel = true; }
  }
  return true;
}
}  // namespace ofe_usb
#endif
