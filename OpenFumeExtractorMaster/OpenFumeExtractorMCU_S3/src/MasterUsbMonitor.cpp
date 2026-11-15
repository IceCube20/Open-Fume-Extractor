#include "MasterUsbMonitor.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "UsbHostDiagnostics.h"
#include "usb/usb_host.h"
#include "hal/usb_dwc_ll.h"
#include <freertos/task.h>

namespace ofe_usb::monitor {
namespace {
UsbHostDiagnostics report;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
usb_host_client_handle_t client = nullptr;
// IDF consumes hub NEW_DEV/GONE events internally. Census must not depend on them.
void callback(const usb_host_client_event_msg_t*, void*) {}

esp_err_t describe(uint8_t address, UsbObservedDevice& observed) {
  observed.address = address;
  usb_device_handle_t device = nullptr;
  esp_err_t error = usb_host_device_open(client, address, &device);
  if (error != ESP_OK) return error;
  const usb_device_desc_t* descriptor = nullptr;
  error = usb_host_get_device_descriptor(device, &descriptor);
  if (error == ESP_OK) {
    observed.vendor = descriptor->idVendor; observed.product = descriptor->idProduct;
    observed.device_class = descriptor->bDeviceClass;
    observed.hub = descriptor->bDeviceClass == 9;
  }
  usb_device_info_t info{};
  if (usb_host_device_info(device, &info) == ESP_OK) observed.speed = uint8_t(info.speed);
  const usb_config_desc_t* config = nullptr;
  const esp_err_t configError = usb_host_get_active_config_descriptor(device, &config);
  if (configError == ESP_OK) {
    const auto* desc = reinterpret_cast<const usb_standard_desc_t*>(config);
    int offset = 0;
    while ((desc = usb_parse_next_descriptor_of_type(desc, config->wTotalLength, USB_B_DESCRIPTOR_TYPE_INTERFACE, &offset))) {
      if (desc->bLength < sizeof(usb_intf_desc_t)) break;
      const auto* iface = reinterpret_cast<const usb_intf_desc_t*>(desc);
      observed.hub |= iface->bInterfaceClass == 9;
      observed.storage |= iface->bInterfaceClass == 8;
      observed.bot |= iface->bInterfaceClass == 8 && iface->bInterfaceSubClass == 6 && iface->bInterfaceProtocol == 0x50;
    }
  }
  // Never retain a hub handle: its GONE event is also hidden from this client.
  const esp_err_t closeError = usb_host_device_close(client, device);
  return error != ESP_OK ? error : configError != ESP_OK ? configError : closeError;
}
void census() {
  uint8_t addresses[128]{};
  int count = 0;
  const esp_err_t error = usb_host_device_addr_list_fill(sizeof(addresses), addresses, &count);
  if (error != ESP_OK) {
    portENTER_CRITICAL(&lock); report.observer_error = error; portEXIT_CRITICAL(&lock);
    return;
  }
  UsbObservedDevice devices[UsbHostDiagnostics::CAPACITY]{};
  int firstError = ESP_OK;
  for (size_t i = 0; i < UsbHostDiagnostics::CAPACITY && i < size_t(count); ++i) {
    devices[i].error = describe(addresses[i], devices[i]);
    if (devices[i].error && firstError == ESP_OK) firstError = devices[i].error;
  }
  UsbHostDiagnostics before;
  portENTER_CRITICAL(&lock);
  before = report;
  report.configured = count; report.census_ms = millis(); report.observer_error = firstError;
  for (const auto& previous : before.devices) if (previous.address) {
    bool found = false;
    for (const auto& current : devices) if (current.address == previous.address) found = true;
    if (!found) report.remove(previous.address);
  }
  for (const auto& current : devices) if (current.address) report.attach(current);
  portEXIT_CRITICAL(&lock);
  for (const auto& current : devices) if (current.address) {
    bool unchanged = false;
    for (const auto& old : before.devices)
      if (old.address == current.address && old.vendor == current.vendor && old.product == current.product &&
          old.device_class == current.device_class && old.hub == current.hub && old.bot == current.bot &&
          old.storage == current.storage && old.error == current.error) unchanged = true;
    if (!unchanged) Serial.printf("[USB] Configured addr=%u VID=%04X PID=%04X class=%02X hub=%u storage=%u BOT=%u error=0x%X\n",
      current.address, current.vendor, current.product, current.device_class, current.hub, current.storage, current.bot, unsigned(current.error));
  }
}
void task(void*) {
  bool previousLink = false;
  int previousCount = -1;
  uint32_t lastCensus = 0;
  for (;;) {
    const esp_err_t error = usb_host_client_handle_events(client, pdMS_TO_TICKS(250));
    usb_host_lib_info_t info{};
    const esp_err_t infoError = usb_host_lib_info(&info);
    // Read-only controller state; PRTPWR is logical port enable, NOT a 5 V measurement.
    auto* hw = USB_DWC_LL_GET_HW(0);
    const bool connected = usb_dwc_ll_hprt_get_conn_status(hw);
    const bool enabled = hw->hprt_reg.prtena;
    const bool powered = hw->hprt_reg.prtpwr;
    portENTER_CRITICAL(&lock);
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) report.observer_error = error;
    if (infoError == ESP_OK) { report.allocated = info.num_devices; report.clients = info.num_clients; }
    report.root_connected = connected; report.root_enabled = enabled; report.root_powered = powered; report.sample_ms = millis();
    portEXIT_CRITICAL(&lock);
    if (connected != previousLink || (infoError == ESP_OK && info.num_devices != previousCount)) {
      Serial.printf("[USB] HS root: link=%u enabled=%u power=%u allocated=%d\n", connected, enabled, powered, infoError == ESP_OK ? info.num_devices : -1);
      previousLink = connected; previousCount = info.num_devices;
    }
    if (!lastCensus || uint32_t(millis() - lastCensus) >= 1000U) { census(); lastCensus = millis(); }
    vTaskDelay(1);
  }
}
}

esp_err_t begin() {
  usb_host_client_config_t config{};
  config.max_num_event_msg = 16;
  config.async.client_event_callback = callback;
  esp_err_t error = usb_host_client_register(&config, &client);
  if (error == ESP_OK && xTaskCreatePinnedToCore(task, "usb_probe", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
    usb_host_client_deregister(client); client = nullptr; error = ESP_ERR_NO_MEM;
  }
  portENTER_CRITICAL(&lock); report.observer_error = error; report.observer_ready = error == ESP_OK; portEXIT_CRITICAL(&lock);
  Serial.printf("[USB] Observer: %s (0x%X)\n", esp_err_to_name(error), unsigned(error));
  return error;
}
void hostResult(esp_err_t error) {
  portENTER_CRITICAL(&lock); report.host_error = error; report.host_ready = error == ESP_OK; portEXIT_CRITICAL(&lock);
  Serial.printf("[USB] HS host: %s (0x%X), SDK hubs=%u\n", esp_err_to_name(error), unsigned(error), unsigned(CONFIG_USB_HOST_HUBS_SUPPORTED));
}
void mscResult(esp_err_t error) {
  portENTER_CRITICAL(&lock); report.msc_ready = error == ESP_OK; report.msc_error = error; portEXIT_CRITICAL(&lock);
  Serial.printf("[USB] MSC driver: %s (0x%X)\n", esp_err_to_name(error), unsigned(error));
}
void eventError(esp_err_t error) {
  portENTER_CRITICAL(&lock); report.event_error = error; portEXIT_CRITICAL(&lock);
}
void appendJson(String& json) {
  UsbHostDiagnostics copy;
  portENTER_CRITICAL(&lock); copy = report; portEXIT_CRITICAL(&lock);
  json += ",\"diagnostics\":{\"host_ready\":"; json += copy.host_ready ? "true" : "false";
  json += ",\"observer_ready\":"; json += copy.observer_ready ? "true" : "false";
  json += ",\"msc_ready\":"; json += copy.msc_ready ? "true" : "false";
  json += ",\"root_connected\":"; json += copy.root_connected ? "true" : "false";
  json += ",\"root_enabled\":"; json += copy.root_enabled ? "true" : "false";
  json += ",\"root_powered\":"; json += copy.root_powered ? "true" : "false";
  json += ",\"host_error\":"; json += copy.host_error;
  json += ",\"observer_error\":"; json += copy.observer_error;
  json += ",\"msc_error\":"; json += copy.msc_error;
  json += ",\"event_error\":"; json += copy.event_error;
  json += ",\"enumerated\":"; json += copy.allocated;  // Legacy field: SDK object count, not enumeration completion.
  json += ",\"allocated\":"; json += copy.allocated;
  json += ",\"configured\":"; json += copy.configured;
  json += ",\"clients\":"; json += copy.clients;
  json += ",\"sample_ms\":"; json += copy.sample_ms;
  json += ",\"census_ms\":"; json += copy.census_ms;
  json += ",\"attachments\":"; json += copy.attachments;
  json += ",\"removals\":"; json += copy.removals;
  json += ",\"overflow\":"; json += copy.overflow;
  json += ",\"devices\":[";
  bool first = true;
  for (const auto& device : copy.devices) if (device.address) {
    if (!first) json += ','; first = false;
    json += "{\"address\":"; json += device.address;
    json += ",\"vendor\":"; json += device.vendor;
    json += ",\"product\":"; json += device.product;
    json += ",\"class\":"; json += device.device_class;
    json += ",\"error\":"; json += device.error;
    json += ",\"hub\":"; json += device.hub ? "true" : "false";
    json += ",\"storage\":"; json += device.storage ? "true" : "false";
    json += ",\"bot\":"; json += device.bot ? "true" : "false";
    json += ",\"speed\":"; json += device.speed; json += '}';
  }
  json += "]}";
}
}  // namespace ofe_usb::monitor
#endif
