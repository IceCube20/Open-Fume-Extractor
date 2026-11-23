#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4) || !defined(BOARD_HAS_PSRAM) || !defined(OFE_MASTER_P4_WAVESHARE)
#error "OFE MCU P4 profile required"
#endif
#if defined(OFE_MASTER_P4_WAVESHARE_POE)
static_assert(CONFIG_ESP32P4_REV_MIN_FULL==301 && CONFIG_ESP32P4_REV_MAX_FULL==399,"Wrong PoE SDK");
static_assert(F_CPU==400000000L,"Wrong PoE CPU clock");
#else
static_assert(CONFIG_ESP32P4_REV_MIN_FULL==1 && CONFIG_ESP32P4_REV_MAX_FULL==199,"Wrong PRE-V3 SDK");
static_assert(F_CPU==360000000L,"Wrong PRE-V3 CPU clock");
#endif
void setup() {}
void loop() { delay(1000); }
