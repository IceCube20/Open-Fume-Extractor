#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ofe_usb {
struct UsbObservedDevice {
  uint8_t address = 0, device_class = 0, speed = 0;
  uint16_t vendor = 0, product = 0;
  int error = 0;
  bool hub = false, storage = false, bot = false;
};

struct UsbHostDiagnostics {
  static constexpr size_t CAPACITY = 8;
  UsbObservedDevice devices[CAPACITY]{};
  uint32_t attachments = 0, removals = 0, overflow = 0;
  uint32_t sample_ms = 0, census_ms = 0;
  int allocated = 0, configured = 0, clients = 0, host_error = 0, observer_error = 0, msc_error = 0, event_error = 0;
  bool host_ready = false, observer_ready = false, msc_ready = false;
  bool root_connected = false, root_enabled = false, root_powered = false;

  bool attach(const UsbObservedDevice& device) {
    if (!device.address) return false;
    for (auto& entry : devices) if (entry.address == device.address) { entry = device; return true; }
    for (auto& entry : devices) if (!entry.address) { entry = device; ++attachments; return true; }
    ++overflow;
    return false;
  }
  void remove(uint8_t address) {
    if (!address) return;
    for (auto& entry : devices) if (entry.address == address) { entry = {}; ++removals; return; }
  }
};
}  // namespace ofe_usb
