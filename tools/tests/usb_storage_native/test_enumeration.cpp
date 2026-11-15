#include <cassert>
#include <cstdint>
#include <cstdio>

#define CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK TEST_ENUM_FILTER
constexpr int ESP_INTR_FLAG_LEVEL1 = 1;
using esp_err_t = int;
struct usb_device_desc_t {
  uint16_t idVendor, idProduct;
  uint8_t bDeviceClass;
};
using usb_host_enum_filter_cb_t = bool (*)(const usb_device_desc_t*, uint8_t*);
struct usb_host_config_t {
  bool skip_phy_setup = false, root_port_unpowered = false;
  int intr_flags = 0;
  usb_host_enum_filter_cb_t enum_filter_cb = nullptr;
  unsigned peripheral_map = 0;
};
struct SerialStub {
  unsigned calls = 0;
  template <typename... Args> void printf(const char*, Args...) { ++calls; }
} Serial;
usb_host_config_t installed;
esp_err_t usb_host_install(const usb_host_config_t* host) { installed = *host; return 0; }

// These are extracted from the production source, including host callback wiring.
#include "enumeration.h"
#include "host_init.h"

bool sdkSelectConfiguration(const usb_host_config_t& host, const usb_device_desc_t& descriptor, uint8_t& configuration) {
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  // IDF 5.5.5 enum.c select_active_configuration(): enabled filter defaults to rejection.
  return host.enum_filter_cb && host.enum_filter_cb(&descriptor, &configuration);
#else
  (void)host; (void)descriptor; (void)configuration;
  return true;
#endif
}
int main() {
  const usb_device_desc_t descriptors[] = {{0x1A40, 0x0101, 9}, {0x0781, 0x5581, 0}, {0x1234, 0x5678, 8}};
  usb_host_config_t oldHost{};
  uint8_t configuration = 1;
  assert(sdkSelectConfiguration(oldHost, descriptors[0], configuration) == !TEST_ENUM_FILTER);
  installHostForTest();
  assert(installed.peripheral_map == 1U && installed.intr_flags == ESP_INTR_FLAG_LEVEL1);
  assert(!installed.skip_phy_setup && !installed.root_port_unpowered);
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  assert(installed.enum_filter_cb != nullptr);
#else
  assert(installed.enum_filter_cb == nullptr);
#endif
  for (unsigned reconnect = 0; reconnect < 3; ++reconnect) {
    for (const auto& descriptor : descriptors) {
      configuration = uint8_t(reconnect + 1);
      assert(sdkSelectConfiguration(installed, descriptor, configuration));
      assert(configuration == reconnect + 1);  // Preserve the SDK's configuration choice.
    }
  }
  assert(Serial.calls == (TEST_ENUM_FILTER ? 9U : 0U));
  std::printf("PASS: production host enumeration filter %s; hub/storage acceptance and reconnects\n", TEST_ENUM_FILTER ? "enabled" : "disabled");
}
