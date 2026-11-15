#pragma once
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0
#include "MasterStatusPixels.h"
#define OFE_STATUS_LED_DRIVER MasterStatusPixels
#endif
#include "OfeStatusLed.h"
#if OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0
static_assert(OFE_STATUS_LED_COUNT <= 2, "MCU RMT transport supports up to two RGBW status LEDs");
#endif

// Setters only publish logical state. Only the LED task touches pixels/RMT.
class MasterStatusLedService {
public:
  bool begin(uint8_t brightness = OFE_STATUS_LED_BRIGHTNESS) {
    setBrightness(brightness);
    renderer_.begin(brightness);
#if OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0 && OFE_STATUS_LED_COUNT > 0
    return xTaskCreatePinnedToCore(task, "status-led", 4096, this, 2, &task_, 1) == pdPASS;
#else
    return false;
#endif
  }
  void setBrightness(uint8_t value) { set(0xFFU, 0, value); }
  uint8_t brightness() const { return state_.load() & 0xFFU; }
  void setEcoMode(bool active, uint8_t cap = 51) { set(0x1FF00U, 8, uint32_t(cap) | (uint32_t(active) << 8)); }
  bool ecoMode() const { return state_.load() & (1U << 16); }
  void setBusOnline(bool active) { setBusEvent(active ? OFE_LED_EVENT_BUS_ONLINE : OFE_LED_EVENT_BUS_OFFLINE); }
  void setBusEvent(OfeLedEvent event) { if (event < OFE_LED_EVENT_COUNT) set(0x1E0000U, 17, event); }
  OfeLedEvent busEvent() const { return OfeLedEvent((state_.load() >> 17) & 0xFU); }
  void setModuleEvent(OfeLedEvent event) { if (event < OFE_LED_EVENT_COUNT) set(0x1E00000U, 21, event); }
  OfeLedEvent moduleEvent() const {
    const uint32_t state = state_.load();
    return state & (1U << 25) ? OFE_LED_EVENT_FW_UPDATE : OfeLedEvent((state >> 21) & 0xFU);
  }
  void setFirmwareUpdate(bool active) { set(1U << 25, 25, active); }
  void pulseBusActivity() {}  // Communication flashes are intentionally disabled.
  void tick() { if (!task_) render(); }  // Allocation-failure fallback only.
private:
  void set(uint32_t mask, uint8_t shift, uint32_t value) {
    uint32_t previous = state_.load();
    while (!state_.compare_exchange_weak(previous, (previous & ~mask) | ((value << shift) & mask))) {}
  }
  void render() {
    const uint32_t state = state_.load();
    renderer_.setBrightness(uint8_t(state));
    renderer_.setEcoMode(state & (1U << 16), uint8_t(state >> 8));
    renderer_.setBusEvent(OfeLedEvent((state >> 17) & 0xFU));
    renderer_.setFirmwareUpdate(state & (1U << 25));
    renderer_.setModuleEvent(OfeLedEvent((state >> 21) & 0xFU));
    renderer_.tick();
  }
  static void task(void* context) {
    auto* self = static_cast<MasterStatusLedService*>(context);
    for (;;) { self->render(); vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1); }
  }
  OfeStatusLed renderer_;
  std::atomic<uint32_t> state_{uint32_t(OFE_STATUS_LED_BRIGHTNESS) | (51U << 8)};
  TaskHandle_t task_ = nullptr;
};
