#pragma once

#ifndef OFE_P4_PPA
#define OFE_P4_PPA 0
#endif

#ifndef OFE_P4_PPA_IMG
#define OFE_P4_PPA_IMG 0
#endif
#if OFE_P4_PPA_IMG && !OFE_P4_PPA
#error "PPA image rendering also requires OFE_P4_PPA"
#endif

#define OFE_P4_DRAW_ALIGNMENT 128U

#if OFE_P4_PPA
#include <sdkconfig.h>
#if !defined(CONFIG_IDF_TARGET_ESP32P4) || !defined(CONFIG_SOC_PPA_SUPPORTED)
#error "OFE PPA rendering requires ESP32-P4 with PPA support"
#endif
#if !defined(CONFIG_CACHE_L2_CACHE_LINE_SIZE) || CONFIG_CACHE_L2_CACHE_LINE_SIZE != 128
#error "Select the OFE P4 High Performance cache SDK before enabling PPA"
#endif
#include <stddef.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
// LVGL 9.6 uses the IDF 6 address-based query; bridge the pinned IDF 5.5 SDK.
#ifdef __cplusplus
extern "C" {
#endif
size_t ofe_p4_ppa_cache_line_size(const void* address);
#ifdef __cplusplus
}
#endif
#define esp_cache_get_line_size_by_addr ofe_p4_ppa_cache_line_size
#endif
#endif
