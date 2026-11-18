#pragma once
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <stddef.h>
#include "usb/msc_host_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif
// Local read-only extension: use the mount's actual FatFS drive, never a guessed index.
esp_err_t ofe_usb_volume_label(msc_host_vfs_handle_t mount, char* output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
