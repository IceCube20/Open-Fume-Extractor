#pragma once
#include <stdint.h>
constexpr int NEO_GRBW = 1;
constexpr int NEO_KHZ800 = 2;
inline unsigned led_constructs = 0, led_begins = 0, led_shows = 0;
inline unsigned uninitialized_shows = 0;
inline uint32_t led_colors[2] = {};
class Adafruit_NeoPixel {
public:
    Adafruit_NeoPixel(int, int, int) { ++led_constructs; }
    void begin() { ++led_begins; begun = true; }
    void setBrightness(uint8_t) {}
    void clear() { led_colors[0] = led_colors[1] = 0; }
    void show() { ++led_shows; if (!begun) ++uninitialized_shows; }
    uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
        return (uint32_t(w) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
    void setPixelColor(uint8_t i, uint32_t color) { if (i < 2) led_colors[i] = color; }
private:
    bool begun = false;
};
