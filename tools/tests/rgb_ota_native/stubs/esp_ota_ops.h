#pragma once
#include <stddef.h>
#include <stdint.h>
using esp_err_t=int;
using esp_ota_handle_t=uint32_t;
constexpr esp_err_t ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_SIZE=0x104;
constexpr uint32_t OTA_SIZE_UNKNOWN=0xffffffffU, OTA_WITH_SEQUENTIAL_WRITES=0xfffffffeU;
struct esp_partition_t { size_t size; };
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
const esp_partition_t* esp_ota_get_running_partition();
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
