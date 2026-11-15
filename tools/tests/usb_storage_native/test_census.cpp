#include "UsbHostDiagnostics.h"
#include <cassert>
#include <cstdio>
#include <algorithm>

using namespace ofe_usb;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr uint8_t USB_B_DESCRIPTOR_TYPE_INTERFACE = 4;
struct usb_standard_desc_t { uint8_t bLength, bDescriptorType; };
struct usb_device_desc_t { uint16_t idVendor = 0, idProduct = 0; uint8_t bDeviceClass = 0; };
struct usb_intf_desc_t { uint8_t bLength = sizeof(usb_intf_desc_t), bDescriptorType = 4, bInterfaceClass = 0, bInterfaceSubClass = 0, bInterfaceProtocol = 0; };
struct usb_config_desc_t { uint8_t bLength = 9, bDescriptorType = 2; uint16_t wTotalLength = 32; const usb_intf_desc_t* iface = nullptr; };
struct usb_device_info_t { uint8_t speed = 2; };
struct FakeDevice {
  bool configured = false;
  uint8_t address = 0;
  usb_device_desc_t descriptor;
  usb_intf_desc_t iface;
  usb_config_desc_t config;
  int openError = 0, descriptorError = 0, configError = 0;
  unsigned opens = 0, closes = 0;
  bool held = false;
};
FakeDevice devices[12];
using usb_device_handle_t = FakeDevice*;
using usb_host_client_handle_t = void*;
usb_host_client_handle_t client = reinterpret_cast<void*>(1);
UsbHostDiagnostics report;
int lock = 0, listError = 0, logLines = 0;
uint32_t tick = 1000;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
uint32_t millis() { return tick; }
struct TestSerial { template<class... T> void printf(const char*, T...) { ++logLines; } } Serial;

esp_err_t usb_host_device_addr_list_fill(size_t capacity, uint8_t* list, int* count) {
  if (listError) return listError;
  *count = 0;
  for (const auto& device : devices) if (device.configured && size_t(*count) < capacity) list[(*count)++] = device.address;
  return ESP_OK;
}
esp_err_t usb_host_device_open(usb_host_client_handle_t, uint8_t address, usb_device_handle_t* out) {
  for (auto& device : devices) if (device.configured && device.address == address) {
    if (device.openError) return device.openError;
    assert(!device.held); device.held = true; ++device.opens; *out = &device; return ESP_OK;
  }
  return ESP_FAIL;
}
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t device, const usb_device_desc_t** out) {
  if (device->descriptorError) return device->descriptorError;
  *out = &device->descriptor; return ESP_OK;
}
esp_err_t usb_host_device_info(usb_device_handle_t, usb_device_info_t* info) { info->speed = 2; return ESP_OK; }
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t device, const usb_config_desc_t** out) {
  if (device->configError) return device->configError;
  device->config.iface = &device->iface; *out = &device->config; return ESP_OK;
}
const usb_standard_desc_t* usb_parse_next_descriptor_of_type(const usb_standard_desc_t* current, uint16_t, uint8_t type, int* offset) {
  assert(type == USB_B_DESCRIPTOR_TYPE_INTERFACE);
  if (*offset) return nullptr;
  *offset = 9;
  return reinterpret_cast<const usb_standard_desc_t*>(reinterpret_cast<const usb_config_desc_t*>(current)->iface);
}
esp_err_t usb_host_device_close(usb_host_client_handle_t, usb_device_handle_t device) {
  assert(device->held); device->held = false; ++device->closes; return ESP_OK;
}
#include "census.h"

size_t observed() { size_t n = 0; for (const auto& device : report.devices) n += device.address != 0; return n; }
void allClosed() { for (const auto& device : devices) { assert(!device.held); assert(device.opens == device.closes); } }

int main() {
  report.allocated = 1;
  census();
  assert(report.allocated == 1 && report.configured == 0 && observed() == 0);
  auto& hub = devices[0]; hub.address = 1; hub.configured = true;
  hub.descriptor = {0x1A86, 0x8091, 9}; hub.iface.bInterfaceClass = 9;
  // Deliberately send NO NEW_DEV callback, as IDF hides hubs from applications.
  census();
  assert(report.configured == 1 && observed() == 1 && report.devices[0].hub);
  assert(hub.opens == 1 && hub.closes == 1); allClosed();
  const int lines = logLines;
  census(); assert(logLines == lines); allClosed();
  auto& stick = devices[1]; stick.address = 2; stick.configured = true;
  stick.descriptor = {0x0781, 0x5581, 0}; stick.iface.bInterfaceClass = 8;
  stick.iface.bInterfaceSubClass = 6; stick.iface.bInterfaceProtocol = 0x50;
  report.allocated = 2;
  census(); assert(report.configured == 2 && observed() == 2 && report.devices[1].bot); allClosed();
  listError = ESP_FAIL; tick = 2000;
  census(); assert(report.observer_error == ESP_FAIL && observed() == 2 && report.census_ms == 1000); allClosed();
  listError = 0; hub.configured = false; stick.configured = false;
  // Deliberately send NO GONE callback either; no hub handle can leak.
  census(); assert(report.configured == 0 && observed() == 0 && report.removals == 2); allClosed();
  stick.configured = true; stick.configError = ESP_FAIL;
  census(); assert(report.observer_error == ESP_FAIL && report.devices[0].error == ESP_FAIL); allClosed();
  stick.configError = 0; stick.descriptorError = ESP_FAIL;
  census(); assert(report.devices[0].error == ESP_FAIL); allClosed();
  stick.descriptorError = 0;
  census(); assert(report.observer_error == ESP_OK && report.devices[0].bot); allClosed();
  stick.openError = ESP_FAIL;
  census(); assert(report.devices[0].error == ESP_FAIL); allClosed();
  stick.openError = 0;
  for (unsigned i = 0; i < 10; ++i) { devices[i].configured = true; devices[i].address = uint8_t(i+1); }
  census(); assert(report.configured == 10 && observed() == UsbHostDiagnostics::CAPACITY); allClosed();
  puts("PASS: actual USB census: allocated != configured, hub without NEW/GONE events, cached SCSI/BOT classification, no retained handles, descriptor/list/open failures, bounded details");
}
