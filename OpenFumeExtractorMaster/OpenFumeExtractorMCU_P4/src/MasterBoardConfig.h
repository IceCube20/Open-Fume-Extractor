#pragma once
#if __has_include("MasterBoardLocalConfig.h")
#include "MasterBoardLocalConfig.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#if !defined(OFE_MASTER_P4_WAVESHARE)
#error "Use the generated Waveshare P4 MCU sketch; P4 pinouts are board-specific."
#endif
#if defined(OFE_MASTER_P4_WAVESHARE_POE)
#define OFE_MASTER_BOARD_NAME "Waveshare ESP32-P4-WIFI6-POE-ETH"
#define OFE_MASTER_CHIP_PROFILE "V3.1+ (V3.2 compatible)"
#else
#define OFE_MASTER_BOARD_NAME "Waveshare Module DEV KIT"
#define OFE_MASTER_CHIP_PROFILE "PRE-V3"
#endif
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 21
#endif
#ifndef RS485_TX_PIN
#define RS485_TX_PIN 20
#endif
// GPIO4 drives the external OFE status LED, not the carrier's power LED.
#ifndef OFE_STATUS_LED_ENABLE
#define OFE_STATUS_LED_ENABLE 1
#endif
#ifndef OFE_STATUS_LED_PIN
#define OFE_STATUS_LED_PIN 4
#endif
constexpr bool master_p4_reserved_pin(int pin) {
#if defined(OFE_MASTER_P4_WAVESHARE_POE)
  // C6 IO2, codec and SD socket are also wired on the PoE carrier.
  if (pin == 6 || (pin >= 7 && pin <= 13) || (pin >= 39 && pin <= 45)) return true;
#endif
  return (pin >= 14 && pin <= 19) || (pin >= 28 && pin <= 31) ||
    (pin >= 34 && pin <= 38) || (pin >= 49 && pin <= 54);
}
static_assert(RS485_RX_PIN >= 0 && RS485_RX_PIN <= 54 &&
              RS485_TX_PIN >= 0 && RS485_TX_PIN <= 54 &&
              RS485_RX_PIN != RS485_TX_PIN &&
              !master_p4_reserved_pin(RS485_RX_PIN) && !master_p4_reserved_pin(RS485_TX_PIN),
              "RS485 pins conflict with Waveshare C6, Ethernet, boot or debug wiring");
static_assert(!OFE_STATUS_LED_ENABLE ||
              (OFE_STATUS_LED_PIN >= 0 && OFE_STATUS_LED_PIN <= 54 &&
               !master_p4_reserved_pin(OFE_STATUS_LED_PIN) &&
               OFE_STATUS_LED_PIN != RS485_RX_PIN && OFE_STATUS_LED_PIN != RS485_TX_PIN),
              "External status LED pin conflicts with P4 MCU peripherals");
#else
#ifndef OFE_MASTER_BOARD_NAME
#define OFE_MASTER_BOARD_NAME "ESP32-S3 (MCU-S3)"
#endif
#endif
