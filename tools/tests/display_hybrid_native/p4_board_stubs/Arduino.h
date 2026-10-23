#pragma once
#include <stdint.h>
#include <vector>
#define CONFIG_IDF_TARGET_ESP32P4 1
#define CONFIG_ESP_HOSTED_ENABLED 1
#define BOARD_HAS_PSRAM 1
#define OUTPUT 1
#define INPUT 0
#define LOW 0
#define HIGH 1
namespace fake_p4 {
struct Gpio { int pin, value; };
inline std::vector<Gpio> modes, levels;
inline std::vector<int> delays;
}
inline void pinMode(int pin, int mode) { fake_p4::modes.push_back({pin, mode}); }
inline void digitalWrite(int pin, int value) { fake_p4::levels.push_back({pin, value}); }
inline void delay(int ms) { fake_p4::delays.push_back(ms); }
