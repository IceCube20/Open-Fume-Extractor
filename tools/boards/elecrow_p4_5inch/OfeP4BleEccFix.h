#pragma once
#include <stdbool.h>
#include <sdkconfig.h>
#include <esp_idf_version.h>
#include <esp_arduino_version.h>
#include <soc/soc_caps.h>

// The backport replaces the matching TinyCrypt objects, never the BLE protocol.
// Revalidate its ABI/link map before enabling a different precompiled SDK.
#if !CONFIG_IDF_TARGET_ESP32P4 || !CONFIG_BT_SMP_CRYPTO_STACK_TINYCRYPT
#error "OFE P4 ECC backport requires ESP32-P4 with SDK TinyCrypt"
#endif
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5) || ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "OFE P4 ECC backport validated for Arduino 3.3.12 / IDF 5.5.5 only; review before changing SDK"
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Zero on success; negative stage codes identify a failed known-answer check.
int ofe_p4_ble_ecc_self_test(void);
#ifdef __cplusplus
}
#endif
