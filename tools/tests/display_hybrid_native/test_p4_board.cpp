#include <assert.h>
#include <stdio.h>
#include <set>
#include "../../boards/elecrow_p4_5inch/OfeP4Board.h"

int main() {
  using namespace ofe_p4;
  assert(DISPLAY_ROTATION == 0 && DISPLAY_RGB_PCLK_HZ == 18000000);
  assert(DISPLAY_RGB_BOUNCE_BUFFER_LINES == 10);
  std::set<int> pins;
  for (int pin : rgb_pins) assert(pins.insert(pin).second);
  for (int pin : {hsync,vsync,de,pclk,TOUCH_SDA_PIN,TOUCH_SCL_PIN,TOUCH_RST_PIN,TOUCH_INT_PIN,
                  RS485_RX_PIN,RS485_TX_PIN,53,54,52,51,50,49,20}) assert(pins.insert(pin).second);
  assert(!setBacklight(0) && Wire.frames.empty() && Wire.begin_calls == 1);
  Wire.begin_ok = true;
  assert(setBacklight(0));
  assert(Wire.begin_calls == 2 && Wire.frames.size() == 2);
  assert((Wire.frames[0] == std::vector<uint8_t>{0x1B,1}));
  assert((Wire.frames[1] == std::vector<uint8_t>{0x20,0}));
  assert(setBacklight(0) && Wire.frames.size() == 2);
  assert(setBacklight(255));
  assert((Wire.frames.back() == std::vector<uint8_t>{0x20,100}));
  Wire.failures = 1;
  assert(!setBacklight(30));
  size_t attempts = Wire.frames.size();
  assert(setBacklight(30) && Wire.frames.size() == attempts + 1);
  assert((Wire.frames.back() == std::vector<uint8_t>{0x20,30}));
  assert(configureHostedRadio());
  assert(hostedPinsReady());
  assert((WiFi.pins == std::vector<int>{53,54,52,51,50,49,20}));
  WiFi.result = false;
  assert(!configureHostedRadio());
  assert(!hostedPinsReady());
  resetTouch();
  assert(Wire.begin_calls == 2);
  assert(fake_p4::levels.size() == 3);
  assert(fake_p4::levels[0].pin == 42 && fake_p4::levels[0].value == LOW);
  assert(fake_p4::levels[1].pin == 36 && fake_p4::levels[1].value == LOW);
  assert(fake_p4::levels[2].pin == 36 && fake_p4::levels[2].value == HIGH);
  assert((fake_p4::delays == std::vector<int>{20,120}));
  for (const auto& gpio : fake_p4::levels) assert(gpio.pin != 2 && gpio.pin != 3);
  puts("PASS: P4 pin conflicts, STC8 off/dimming/clamp/cache/failure retry, hosted SDIO and touch reset sequence");
}
