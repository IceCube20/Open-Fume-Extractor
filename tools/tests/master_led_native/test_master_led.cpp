#include <cassert>
#include <cstdio>
#include <thread>
#include "MasterStatusLedService.h"

int main() {
  MasterStatusPixels pixels(2, 4, NEO_GRBW | NEO_KHZ800);
  pixels.begin(); pixels.setPixelColor(0, pixels.Color(0x12, 0x34, 0x56, 0x78));
  pixels.setPixelColor(1, pixels.Color(0x9A, 0xBC, 0xDE, 0xF0)); pixels.show();
  const auto first_end = micros(); pixels.show(); assert(uint32_t(micros() - first_end) >= 300);
  const uint8_t expected[] = {0x34, 0x12, 0x56, 0x78, 0xBC, 0x9A, 0xDE, 0xF0};
  for (unsigned i = 0; i < 64; ++i) {
    const auto& symbol = last_frame[i]; const bool one = expected[i / 8] & (1U << (7 - i % 8));
    assert(symbol.level0 == 1 && symbol.level1 == 0);
    assert(symbol.duration0 == (one ? 8U : 4U) && symbol.duration1 == (one ? 4U : 8U));
  }
  MasterStatusLedService leds;
  assert(leds.begin()); leds.setBusOnline(true); leds.setModuleEvent(OFE_LED_EVENT_EXTRACTOR_ON);
  const unsigned before = rmt_writes;
  for (unsigned i = 0; i < 10000; ++i) { leds.tick(); leds.pulseBusActivity(); }
  assert(rmt_writes == before);  // Loop/service calls cannot write pixels while the task owns them.
  try { worker(worker_context); } catch (StopTask&) {}
  assert(rmt_writes > before && base_shows == 0);
  std::thread brightness([&] { for (int i = 0; i < 100000; ++i) leds.setBrightness(uint8_t(i)); leds.setBrightness(128); });
  std::thread eco([&] { for (int i = 0; i < 100000; ++i) leds.setEcoMode(i & 1); leds.setEcoMode(true); });
  std::thread events([&] { for (int i = 0; i < 100000; ++i) { leds.setBusOnline(i & 1); leds.setModuleEvent(OfeLedEvent(i % OFE_LED_EVENT_COUNT)); } leds.setBusOnline(true); leds.setModuleEvent(OFE_LED_EVENT_WARNING); });
  std::thread ota([&] { for (int i = 0; i < 100000; ++i) leds.setFirmwareUpdate(i & 1); leds.setFirmwareUpdate(false); });
  brightness.join(); eco.join(); events.join(); ota.join();
  assert(leds.brightness() == 128 && leds.ecoMode());
  assert(leds.busEvent() == OFE_LED_EVENT_BUS_ONLINE && leds.moduleEvent() == OFE_LED_EVENT_WARNING);
  leds.setFirmwareUpdate(true); assert(leds.moduleEvent() == OFE_LED_EVENT_FW_UPDATE);
  leds.setFirmwareUpdate(false); assert(leds.moduleEvent() == OFE_LED_EVENT_WARNING);
  create_ok = false; MasterStatusLedService fallback;
  assert(!fallback.begin()); fallback.setBusOnline(true);
  const unsigned fallback_before = rmt_writes; fallback.tick(); assert(rmt_writes > fallback_before);
  for (const auto event : {OFE_LED_EVENT_OFF, OFE_LED_EVENT_EXTRACTOR_ON, OFE_LED_EVENT_AFTER_RUN,
                            OFE_LED_EVENT_CONTINUOUS, OFE_LED_EVENT_WARNING, OFE_LED_EVENT_CRITICAL}) {
    fallback.setModuleEvent(event);
    for (unsigned time = 0; time < 6400; time += 20) {
      test_millis = time; fallback.tick();
      for (unsigned bit = 56; bit < 64; ++bit) assert(last_frame[bit].duration0 == 4);  // EVT white channel stays off.
    }
  }
  std::puts("PASS: whole-frame 64-bit GRBW RMT encoding; one pixel owner; autonomous cadence; concurrent state updates; OTA restoration; task-allocation fallback");
}
