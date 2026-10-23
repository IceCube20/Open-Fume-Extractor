#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_arduino_version.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "This preparation is only for ELECROW CrowPanel Advanced 5-inch ESP32-P4, not ESP32-S3"
#endif
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "Use Arduino-ESP32 3.3.12 or newer with ESP-Hosted WiFi.setPins support"
#endif
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !defined(BOARD_HAS_PSRAM)
#error "Enable P4 PSRAM and use an ESP-Hosted-enabled P4 SDK"
#endif

// Values verified against ELECROW's 5-inch V1.0 Arduino lessons 04/07/16.
// This is the RGB/ST7262 board, not the larger EK79007/MIPI models.
#define DISPLAY_ROTATION 0
#define DISPLAY_RGB_WIDTH 800
#define DISPLAY_RGB_HEIGHT 480
// The board owner's RNG test is artifact-free with this scan/bounce profile.
#define DISPLAY_RGB_PCLK_HZ 24000000
#define DISPLAY_RGB_BOUNCE_BUFFER_LINES 20
#define DISPLAY_RGB_INVERT_COLORS 0
#define RS485_RX_PIN 48
#define RS485_TX_PIN 47
#define BACKLIGHT_PIN -1
#define TOUCH_SDA_PIN 45
#define TOUCH_SCL_PIN 46
#define TOUCH_RST_PIN 36
#define TOUCH_INT_PIN 42

namespace ofe_p4 {
constexpr int rgb_pins[16] = {8, 7, 6, 5, 4, 14, 13, 12, 11, 10, 9, 19, 18, 17, 16, 15};
constexpr int hsync = 40, vsync = 41, de = 2, pclk = 3;
constexpr uint8_t stc8_address = 0x2F;
constexpr uint8_t backlight_power_reg = 0x1B; // SET_GPIO (0x18) + LCD_BL_POWER (3)
constexpr uint8_t backlight_pwm_reg = 0x20;   // SET_PWM + LCD_BL_EN (0)

inline bool beginI2c() {
  static bool ready = false;
  if (!ready) ready = Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN, 400000);
  return ready;
}

inline bool writeStc8(uint8_t reg, uint8_t value) {
  if (!beginI2c()) return false;
  Wire.beginTransmission(stc8_address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

inline bool setBacklight(uint8_t percent) {
  static bool powered = false;
  static uint8_t previous = 255;
  if (percent > 100) percent = 100;
  if (powered && percent == previous) return true;
  if (!powered) {
    if (!writeStc8(backlight_power_reg, 1)) return false;
    powered = true;
  }
  if (!writeStc8(backlight_pwm_reg, percent)) return false;
  previous = percent;
  return true;
}

inline bool& hostedPinsReady() {
  static bool configured = false;
  return configured;
}

inline bool configureHostedRadio() {
  // Configure before WiFi.mode()/BLE init; never use the Dev Module's EV-board pins.
  hostedPinsReady() = WiFi.setPins(53, 54, 52, 51, 50, 49, 20);
  return hostedPinsReady();
}

inline void resetTouch() {
  if (!beginI2c()) return;
  pinMode(TOUCH_INT_PIN, OUTPUT);
  digitalWrite(TOUCH_INT_PIN, LOW); // GT911 0x5D, alternate address is probed too.
  pinMode(TOUCH_RST_PIN, OUTPUT);
  digitalWrite(TOUCH_RST_PIN, LOW);
  delay(20);
  digitalWrite(TOUCH_RST_PIN, HIGH);
  delay(120);
  pinMode(TOUCH_INT_PIN, INPUT);
}
} // namespace ofe_p4
