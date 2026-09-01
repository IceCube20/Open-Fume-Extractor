#pragma once

#include <Arduino.h>

#ifndef OFE_STATUS_LED_ENABLE
#define OFE_STATUS_LED_ENABLE 0
#endif

#ifndef OFE_STATUS_LED_PIN
#define OFE_STATUS_LED_PIN -1
#endif

#ifndef OFE_STATUS_LED_COUNT
#define OFE_STATUS_LED_COUNT 2
#endif

#ifndef OFE_STATUS_LED_BRIGHTNESS
#define OFE_STATUS_LED_BRIGHTNESS 32
#endif

#ifndef OFE_STATUS_LED_ACTIVITY_GAP_MS
#define OFE_STATUS_LED_ACTIVITY_GAP_MS 160
#endif

#ifndef OFE_STATUS_LED_SYNC_PERIOD_MS
#define OFE_STATUS_LED_SYNC_PERIOD_MS 3200
#endif

#if OFE_STATUS_LED_ENABLE
#include <Adafruit_NeoPixel.h>
#ifndef OFE_STATUS_LED_PIXEL_TYPE
#define OFE_STATUS_LED_PIXEL_TYPE (NEO_GRBW + NEO_KHZ800)
#endif
#endif

enum OfeLedId : uint8_t {
  OFE_LED_BUS = 0,
  OFE_LED_EVENT = 1,
};

enum OfeLedEffect : uint8_t {
  OFE_LED_SOLID = 0,
  OFE_LED_BREATH,
  OFE_LED_BLINK,
  OFE_LED_DOUBLE_BLINK,
  OFE_LED_FLASH,
  OFE_LED_PULSE,
  OFE_LED_HEARTBEAT,
  OFE_LED_GREEN_WHITE_BREATH,
  OFE_LED_BLUE_WHITE_BREATH,
  OFE_LED_WHITE_BREATH,
};

enum OfeLedEvent : uint8_t {
  OFE_LED_EVENT_OFF = 0,
  OFE_LED_EVENT_BUS_ONLINE,
  OFE_LED_EVENT_BUS_ACTIVITY,
  OFE_LED_EVENT_BUS_OFFLINE,
  OFE_LED_EVENT_NOT_PAIRED,
  OFE_LED_EVENT_FW_UPDATE,
  OFE_LED_EVENT_DEVICE_ONLINE,
  OFE_LED_EVENT_DEVICE_OFFLINE,
  OFE_LED_EVENT_WORK_ACTIVE,
  OFE_LED_EVENT_EXTRACTOR_ON,
  OFE_LED_EVENT_AFTER_RUN,
  OFE_LED_EVENT_CONTINUOUS,
  OFE_LED_EVENT_WARNING,
  OFE_LED_EVENT_CRITICAL,
  OFE_LED_EVENT_COUNT,
};

struct OfeLedColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;
};

struct OfeLedStyle {
  OfeLedColor color;
  OfeLedEffect effect;
  uint16_t period_ms;
  uint8_t priority;
};

static const OfeLedStyle OFE_LED_DEFAULT_STYLES[OFE_LED_EVENT_COUNT] = {
  {{0, 0, 0, 0},       OFE_LED_SOLID,        0, 0},
  {{0, 0, 0, 0},       OFE_LED_GREEN_WHITE_BREATH, OFE_STATUS_LED_SYNC_PERIOD_MS, 10}, // bus online, calm green/white breathing
  {{0, 255, 0, 0},     OFE_LED_FLASH,       35, 70}, // bus tx/rx activity
  {{255, 0, 0, 0},     OFE_LED_BREATH,    1600, 80}, // bus offline
  {{255, 140, 0, 0},   OFE_LED_BLINK,      500, 90}, // not paired / no valid address
  {{0, 0, 0, 0},      OFE_LED_BLUE_WHITE_BREATH, OFE_STATUS_LED_SYNC_PERIOD_MS, 100}, // firmware update, calm blue/white breathing
  {{0, 0, 0, 0},       OFE_LED_WHITE_BREATH, OFE_STATUS_LED_SYNC_PERIOD_MS, 10}, // local device online
  {{255, 0, 0, 0},     OFE_LED_BREATH,    1600, 80}, // local device offline
  {{0, 255, 0, 0},     OFE_LED_SOLID,        0, 60}, // JBC work active
  {{0, 70, 255, 0},    OFE_LED_BREATH,    OFE_STATUS_LED_SYNC_PERIOD_MS, 50}, // extractor/output active
  {{170, 0, 255, 0},   OFE_LED_BLINK,      650, 55}, // afterrun
  {{0, 70, 255, 0},    OFE_LED_BLINK,      500, 55}, // continuous
  {{255, 170, 0, 0},   OFE_LED_BLINK,      500, 70}, // warning
  {{255, 0, 0, 0},     OFE_LED_DOUBLE_BLINK, 900, 95}, // critical
};

class OfeStatusLed {
public:
  OfeStatusLed() {
    for (uint8_t i = 0; i < OFE_LED_EVENT_COUNT; ++i) styles_[i] = OFE_LED_DEFAULT_STYLES[i];
    for (uint8_t i = 0; i < 2; ++i) active_[i] = OFE_LED_EVENT_OFF;
  }

  void begin(uint8_t brightness = OFE_STATUS_LED_BRIGHTNESS) {
    brightness_ = brightness;

#if OFE_STATUS_LED_ENABLE
    if (OFE_STATUS_LED_PIN < 0 || OFE_STATUS_LED_COUNT < 1) return;
    pixels_.begin();
    pixels_.setBrightness(255);
    pixels_.clear();
    pixels_.show();
#endif
  }

  void setBrightness(uint8_t brightness) {
    brightness_ = brightness;

#if OFE_STATUS_LED_ENABLE
    pixels_.setBrightness(255);
#endif
  }

  uint8_t brightness() const { return brightness_; }
  OfeLedEvent busEvent() const { return active_[OFE_LED_BUS]; }
  OfeLedEvent moduleEvent() const { return fw_update_ ? OFE_LED_EVENT_FW_UPDATE : active_[OFE_LED_EVENT]; }

  void syncClock(uint32_t master_ms) {
    phase_offset_ms_ = master_ms - millis();
  }

  void setStyle(OfeLedEvent event, const OfeLedStyle& style) {
    if (event >= OFE_LED_EVENT_COUNT) return;
    styles_[event] = style;
  }

  void setBusEvent(OfeLedEvent event) { setEvent(OFE_LED_BUS, event); }
  void setModuleEvent(OfeLedEvent event) { setEvent(OFE_LED_EVENT, event); }

  void setBusOnline(bool online) {
    setBusEvent(online ? OFE_LED_EVENT_BUS_ONLINE : OFE_LED_EVENT_BUS_OFFLINE);
  }

  void setDeviceOnline(bool online) {
    setModuleEvent(online ? OFE_LED_EVENT_DEVICE_ONLINE : OFE_LED_EVENT_DEVICE_OFFLINE);
  }

  void setFirmwareUpdate(bool active) {
    fw_update_ = active;
    if (active) setModuleEvent(OFE_LED_EVENT_FW_UPDATE);
  }

  void pulseBusActivity() { }

  void tick() {

#if OFE_STATUS_LED_ENABLE
    const uint32_t now = millis() + phase_offset_ms_;
    bool changed = false;
    for (uint8_t led = 0; led < 2 && led < OFE_STATUS_LED_COUNT; ++led) {
      OfeLedEvent event = active_[led];
      if (fw_update_ && led == OFE_LED_EVENT) event = OFE_LED_EVENT_FW_UPDATE;
      OfeLedColor c = applyBrightness(render(styles_[event], now));
      uint32_t packed = pixels_.Color(c.r, c.g, c.b, c.w);
      if (last_packed_[led] != packed) {
        pixels_.setPixelColor(led, packed);
        last_packed_[led] = packed;
        changed = true;
      }
    }
    if (changed) pixels_.show();
#else
    (void)active_;
#endif
  }

private:
  void setEvent(OfeLedId led, OfeLedEvent event) {
    if (led > OFE_LED_EVENT || event >= OFE_LED_EVENT_COUNT) return;
    active_[led] = event;
  }

  static uint8_t scale8(uint8_t v, uint8_t scale) {
    return (uint16_t)v * scale / 255U;
  }

  static OfeLedColor scaleColor(OfeLedColor c, uint8_t scale) {
    c.r = scale8(c.r, scale);
    c.g = scale8(c.g, scale);
    c.b = scale8(c.b, scale);
    c.w = scale8(c.w, scale);
    return c;
  }

  static uint8_t triangle(uint32_t phase, uint16_t period) {
    if (period == 0) return 255;
    uint32_t p = phase % period;
    if (p < period / 2) return (uint32_t)p * 510U / period;
    return 255U - ((uint32_t)(p - period / 2) * 510U / period);
  }

  static uint8_t lerp8(uint8_t a, uint8_t b, uint8_t mix) {
    return a + ((int16_t)b - (int16_t)a) * (int16_t)mix / 255;
  }
  OfeLedColor applyBrightness(OfeLedColor c) const {
    c.r = scale8(c.r, brightness_);
    c.g = scale8(c.g, brightness_);
    c.b = scale8(c.b, brightness_);
    c.w = scale8(c.w, brightness_);
    return c;
  }
  OfeLedColor render(const OfeLedStyle& style, uint32_t now) const {
    switch (style.effect) {
      case OFE_LED_SOLID:
        return style.color;
      case OFE_LED_BREATH: {
        const uint16_t period = style.period_ms ? style.period_ms : OFE_STATUS_LED_SYNC_PERIOD_MS;
        uint8_t wave = triangle(now, period);
        uint8_t level = 24 + ((uint16_t)wave * 231U / 255U);
        return scaleColor(style.color, level);
      }
      case OFE_LED_BLINK:
        return ((now / (style.period_ms ? style.period_ms : 500)) & 1U) ? style.color : OfeLedColor{0, 0, 0, 0};
      case OFE_LED_DOUBLE_BLINK: {
        uint16_t period = style.period_ms ? style.period_ms : 900;
        uint16_t p = now % period;
        return (p < 90 || (p >= 180 && p < 270)) ? style.color : OfeLedColor{0, 0, 0, 0};
      }
      case OFE_LED_FLASH:
        return style.color;
      case OFE_LED_PULSE: {
        uint8_t wave = triangle(now, style.period_ms ? style.period_ms : 900);
        return scaleColor(style.color, wave);
      }
      case OFE_LED_GREEN_WHITE_BREATH: {
        const uint16_t period = style.period_ms ? style.period_ms : OFE_STATUS_LED_SYNC_PERIOD_MS;
        const uint8_t mix = triangle(now, period);
        return OfeLedColor{0, lerp8(255, 0, mix), 0, lerp8(0, 255, mix)};
      }
      case OFE_LED_BLUE_WHITE_BREATH: {
        const uint16_t period = style.period_ms ? style.period_ms : OFE_STATUS_LED_SYNC_PERIOD_MS;
        const uint8_t mix = triangle(now, period);
        return OfeLedColor{0, lerp8(36, 0, mix), lerp8(255, 0, mix), lerp8(0, 255, mix)};
      }
      case OFE_LED_WHITE_BREATH: {
        const uint16_t period = style.period_ms ? style.period_ms : OFE_STATUS_LED_SYNC_PERIOD_MS;
        uint8_t wave = triangle(now, period);
        uint8_t level = 16 + ((uint16_t)wave * 239U / 255U);
        return OfeLedColor{0, 0, 0, level};
      }
      case OFE_LED_HEARTBEAT: {
        uint16_t period = style.period_ms ? style.period_ms : 1800;
        uint16_t p = now % period;
        if (p < 45) return style.color;
        if (p < 150) return scaleColor(style.color, (uint8_t)(255U - ((uint32_t)(p - 45) * 255U / 105U)));
        return OfeLedColor{0, 0, 0, 0};
      }
    }
    return OfeLedColor{0, 0, 0, 0};
  }


#if OFE_STATUS_LED_ENABLE
  Adafruit_NeoPixel pixels_{OFE_STATUS_LED_COUNT, OFE_STATUS_LED_PIN, OFE_STATUS_LED_PIXEL_TYPE};
#endif
  OfeLedStyle styles_[OFE_LED_EVENT_COUNT];
  OfeLedEvent active_[2];
  uint32_t last_packed_[2] = {0xFFFFFFFFUL, 0xFFFFFFFFUL};
  uint32_t flash_until_ms_ = 0;
  uint32_t last_activity_pulse_ms_ = 0;
  uint8_t brightness_ = OFE_STATUS_LED_BRIGHTNESS;
  uint32_t phase_offset_ms_ = 0;
  bool fw_update_ = false;
};
