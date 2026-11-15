#pragma once
#include <Adafruit_NeoPixel.h>
#include <esp32-hal-rmt.h>

// Keep NeoPixel's GRBW packing, but fit the entire two-pixel frame into RMT RAM.
class MasterStatusPixels : public Adafruit_NeoPixel {
public:
  using Adafruit_NeoPixel::Adafruit_NeoPixel;
  void show() {
    if (!pixels || !numBytes || numBytes > sizeof(symbols_) / sizeof(symbols_[0]) / 8) return;
    if (!ready_) {
      ready_ = rmtInit(getPin(), RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 10000000);
      if (!ready_) return;
    }
    for (uint16_t byte = 0; byte < numBytes; ++byte) {
      for (uint8_t bit = 0; bit < 8; ++bit) {
        auto& symbol = symbols_[byte * 8 + bit];
        const bool one = pixels[byte] & (1U << (7 - bit));
        symbol.level0 = 1; symbol.duration0 = one ? 8 : 4;
        symbol.level1 = 0; symbol.duration1 = one ? 4 : 8;
      }
    }
    while (!canShow()) delayMicroseconds(1);
    // Do not reuse the symbols until the driver has completed this frame.
    rmtWrite(getPin(), symbols_, numBytes * 8, RMT_WAIT_FOR_EVER);
    endTime = micros();
  }
private:
  rmt_data_t symbols_[64]{};
  bool ready_ = false;
};
