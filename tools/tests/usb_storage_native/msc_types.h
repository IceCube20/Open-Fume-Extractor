#pragma once
#include <cstdint>
#include <cwchar>

struct MscDevice { unsigned address, identity; };
using msc_host_device_handle_t = MscDevice*;
struct MscVfs { msc_host_device_handle_t device; bool probe; };
using msc_host_vfs_handle_t = MscVfs*;
constexpr unsigned MSC_STR_DESC_SIZE = 32;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
struct msc_host_event_t {
  enum Type { MSC_DEVICE_CONNECTED, MSC_DEVICE_DISCONNECTED } event;
  union { uint8_t address; msc_host_device_handle_t handle; } device{};
};
struct msc_host_device_info_t {
  uint32_t sector_count = 0, sector_size = 0;
  uint16_t idProduct = 0, idVendor = 0;
  wchar_t iManufacturer[MSC_STR_DESC_SIZE]{}, iProduct[MSC_STR_DESC_SIZE]{}, iSerialNumber[MSC_STR_DESC_SIZE]{};
};
struct esp_vfs_fat_mount_config_t {
  bool format_if_mount_failed = false;
  unsigned max_files = 0, allocation_unit_size = 0;
};
