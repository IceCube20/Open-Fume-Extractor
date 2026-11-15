#include "MasterUsbStorage.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "UsbStoragePolicy.h"
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
struct Entry { char name[MAX_NAME]; size_t size; bool folder; };
struct State {
  Entry files[MAX_FILES]{};
  size_t count = 0;
  uint32_t generation = 1, token = 0, touched = 0;
  Phase phase = Phase::Idle;
  char message[96] = "Starting USB host";
  char operation[12]{}, filename[MAX_NAME]{};
  unsigned selected = 0;
  uint8_t* buffer = nullptr;
  size_t size = 0, received = 0;
  bool truncated = false;
};
State* state = nullptr;
SemaphoreHandle_t mutex = nullptr;
std::atomic<bool> present{false}, ready{false}, gone{false}, started{false};
std::atomic<unsigned> nextAddress{0};
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
}
bool busy() {
  return state->phase == Phase::Queued || state->phase == Phase::Reading ||
         state->phase == Phase::Writing || state->phase == Phase::Receiving || state->buffer;
}
void event(const msc_host_event_t* evt, void*) {
  if (evt->event == msc_host_event_t::MSC_DEVICE_CONNECTED) {
    Serial.printf("[USB] MSC accepted addr=%u\n", evt->device.address);
    // A single volume is supported. Never replace an active device in a callback.
    if (!activeDevice.load()) { present.store(true); nextAddress.store(evt->device.address); }
  } else if (evt->event == msc_host_event_t::MSC_DEVICE_DISCONNECTED &&
             (!activeDevice.load() || evt->device.handle == activeDevice.load())) {
    present.store(false); ready.store(false); gone.store(true);
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
  if (vfs) { msc_host_vfs_unregister(vfs); vfs = nullptr; }
  activeDevice.store(nullptr);
  if (device) { msc_host_uninstall_device(device); device = nullptr; }
  Guard guard;
  freeBuffer();
  state->count = 0;
  ++state->generation;
}
void listDirectory(const char* path, bool folder) {
  DIR* dir = opendir(path);
  if (!dir) return;
  while (present.load()) {
    dirent* item = readdir(dir);
    if (!item) break;
    if (!visibleFile(item->d_name)) continue;
    char full[128];
    snprintf(full, sizeof(full), "%s/%s", path, item->d_name);
    struct stat st{};
    if (stat(full, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || uint64_t(st.st_size) > MAX_FILE_BYTES) continue;
    Guard guard;
    if (state->count == MAX_FILES) { state->truncated = true; break; }
    Entry& entry = state->files[state->count++];
    strlcpy(entry.name, item->d_name, sizeof(entry.name));
    entry.size = size_t(st.st_size);
    entry.folder = folder;
  }
  closedir(dir);
}
void refresh() {
  { Guard guard; state->count = 0; state->truncated = false; ++state->generation; }
  listDirectory("/usb", false);
  listDirectory("/usb/OFE", true);
  message(present.load() ? "Ready" : "USB disconnected", present.load() ? Phase::Idle : Phase::Error);
}
void mount(unsigned address) {
  if (device) return;
  esp_err_t err = msc_host_install_device(uint8_t(address), &device);
  Serial.printf("[USB] MSC install addr=%u: %s (0x%X)\n", address, esp_err_to_name(err), unsigned(err));
  activeDevice.store(device);
  if (err == ESP_OK) {
    esp_vfs_fat_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 4;
    config.allocation_unit_size = 8192;
    err = msc_host_vfs_register(device, "/usb", &config, &vfs);
    Serial.printf("[USB] FAT mount: %s (0x%X)\n", esp_err_to_name(err), unsigned(err));
  }
  if (err != ESP_OK || gone.load()) {
    unmount();
    message("Cannot mount FAT volume; stick was NOT formatted", Phase::Error);
    return;
  }
  ready.store(true);
  refresh();
}
void readSelected() {
  Entry entry{};
  { Guard guard; entry = state->files[state->selected]; state->phase = Phase::Reading; }
  char path[128];
  snprintf(path, sizeof(path), "%s/%s", entry.folder ? "/usb/OFE" : "/usb", entry.name);
  FILE* file = fopen(path, "rb");
  struct stat st{};
  uint8_t* data = nullptr;
  bool ok = file && fstat(fileno(file), &st) == 0 && S_ISREG(st.st_mode) && size_t(st.st_size) == entry.size;
  if (ok) data = static_cast<uint8_t*>(heap_caps_malloc(entry.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ok = ok && data;
  size_t done = 0;
  while (ok && done < entry.size) {
    const size_t n = std::min(size_t(4096), entry.size - done);
    ok = present.load() && fread(data + done, 1, n, file) == n;
    if (ok) done += n;
    vTaskDelay(1);
  }
  if (file) fclose(file);
  if (!ok || !present.load()) { heap_caps_free(data); message("USB read failed", Phase::Error); return; }
  Guard guard;
  state->buffer = data; state->size = entry.size; state->received = entry.size;
  strlcpy(state->filename, entry.name, sizeof(state->filename));
  state->token = esp_random() | 1; state->touched = millis();
  state->phase = Phase::Ready;
  strlcpy(state->message, "File ready", sizeof(state->message));
}
void writeStaged() {
  char path[128], temporary[132];
  snprintf(path, sizeof(path), "/usb/OFE/%s", state->filename);
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  bool ok = mkdir("/usb/OFE", 0755) == 0 || errno == EEXIST;
  struct stat existing{};
  if (stat(path, &existing) == 0) ok = false;
  int fd = ok ? open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
  ok = ok && fd >= 0;
  const bool ownTemp = fd >= 0;
  size_t done = 0;
  while (ok && done < state->size) {
    const size_t count = std::min(size_t(4096), state->size - done);
    const ssize_t written = present.load() ? write(fd, state->buffer + done, count) : -1;
    ok = written == ssize_t(count);
    if (ok) done += count;
    vTaskDelay(1);
  }
  if (fd >= 0) { if (ok) ok = fsync(fd) == 0; if (close(fd) != 0) ok = false; }
  if (ok) ok = present.load() && rename(temporary, path) == 0;
  if (!ok && ownTemp && present.load()) unlink(temporary);
  { Guard guard; freeBuffer(); }
  if (ok) refresh();
  message(ok ? "Saved to /OFE" : "USB write failed; existing files were not overwritten", ok ? Phase::Idle : Phase::Error);
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
    if (gone.exchange(false)) { unmount(); message("USB disconnected", Phase::Error); }
    const unsigned address = nextAddress.exchange(0);
    if (address) mount(address);
    char action[12]{};
    { Guard guard;
      if (state->phase == Phase::Queued) {
        strlcpy(action, state->operation, sizeof(action));
        state->phase = !strcmp(action, "write") ? Phase::Writing : Phase::Reading;
      }
      if ((state->phase == Phase::Ready || state->phase == Phase::Receiving || state->phase == Phase::Error) &&
          state->buffer && uint32_t(millis() - state->touched) > BUFFER_LEASE_MS) {
        freeBuffer(); state->phase = Phase::Idle;
      }
    }
    if (*action) {
      if (!ready.load()) message("No mounted USB stick", Phase::Error);
      else if (!strcmp(action, "read")) readSelected();
      else if (!strcmp(action, "write")) writeStaged();
      else if (!strcmp(action, "refresh")) refresh();
      else if (!strcmp(action, "eject")) { unmount(); present.store(false); message("Ejected; remove and reinsert stick", Phase::Idle); }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
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
bool detected() { return present.load(); }
bool mounted() { return ready.load(); }
String statusJson(bool developerMode) {
  if (!state || !mutex) return "{\"detected\":false,\"mounted\":false,\"error\":\"USB host allocation failed\"}";
  Guard guard;
  const char* phases[] = {"idle", "queued", "reading", "ready", "receiving", "writing", "error"};
  String json;
  json.reserve(700 + state->count * 140);
  json = "{\"detected\":"; json += detected() ? "true" : "false";
  json += ",\"mounted\":"; json += mounted() ? "true" : "false";
  json += ",\"phase\":"; quoted(json, phases[unsigned(state->phase)]);
  json += ",\"message\":"; quoted(json, state->message);
  json += ",\"generation\":"; json += state->generation;
  json += ",\"token\":"; json += state->token;
  json += ",\"size\":"; json += unsigned(state->size);
  json += ",\"received\":"; json += unsigned(state->received);
  json += ",\"truncated\":"; json += state->truncated ? "true" : "false";
  json += ",\"developer_mode\":"; json += developerMode ? "true" : "false";
  if (developerMode) monitor::appendJson(json);
  json += ",\"files\":[";
  for (size_t i = 0; i < state->count; ++i) {
    if (i) json += ',';
    json += "{\"index\":"; json += unsigned(i); json += ",\"name\":"; quoted(json, state->files[i].name);
    json += ",\"size\":"; json += unsigned(state->files[i].size);
    json += ",\"folder\":"; json += state->files[i].folder ? "true" : "false"; json += '}';
  }
  json += "]}";
  return json;
}
bool command(const String& action, uint32_t generation, unsigned index) {
  if (!state || !mutex) return false;
  Guard guard;
  if (!ready.load() || busy()) return false;
  if (action != "read" && action != "refresh" && action != "eject") return false;
  if (action == "read" && (generation != state->generation || index >= state->count)) return false;
  state->selected = index;
  action.toCharArray(state->operation, sizeof(state->operation));
  state->phase = Phase::Queued;
  return true;
}
uint32_t receiveBegin(const char* name, size_t bytes) {
  if (!state || !mutex || !visibleFile(name) || !range(bytes, 0, bytes)) return 0;
  Guard guard;
  if (!ready.load() || busy()) return 0;
  state->buffer = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!state->buffer) return 0;
  state->size = bytes; state->received = 0;
  strlcpy(state->filename, name, sizeof(state->filename));
  state->token = esp_random() | 1; state->touched = millis(); state->phase = Phase::Receiving;
  return state->token;
}
bool receiveChunk(uint32_t token, size_t offset, const uint8_t* data, size_t bytes) {
  if (!state || !mutex || !data || !bytes) return false;
  Guard guard;
  if (!token || token != state->token || state->phase != Phase::Receiving || !range(state->size, offset, bytes)) return false;
  if (offset < state->received) return bytes <= state->received - offset && memcmp(state->buffer + offset, data, bytes) == 0;
  if (offset != state->received) return false;
  memcpy(state->buffer + offset, data, bytes); state->received += bytes; state->touched = millis();
  return true;
}
bool receiveCommit(uint32_t token) {
  if (!state || !mutex) return false;
  Guard guard;
  if (!token || token != state->token || state->phase != Phase::Receiving || state->received != state->size || !ready.load()) return false;
  strlcpy(state->operation, "write", sizeof(state->operation)); state->phase = Phase::Queued;
  return true;
}
bool release(uint32_t token) {
  if (!state || !mutex) return false;
  Guard guard;
  if (!token || token != state->token || (state->phase != Phase::Ready && state->phase != Phase::Receiving && state->phase != Phase::Error)) return false;
  freeBuffer(); state->phase = Phase::Idle;
  return true;
}
bool sendFile(WebServer& server, uint32_t token) {
  if (!state || !mutex) return false;
  Guard guard;
  if (!token || token != state->token || state->phase != Phase::Ready || !state->buffer) return false;
  state->touched = millis();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + state->filename + "\"");
  server.setContentLength(state->size);
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char*>(state->buffer), state->size);
  state->touched = millis();
  return true;
}
}  // namespace ofe_usb
#endif
