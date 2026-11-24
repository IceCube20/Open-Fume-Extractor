#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace ofe_fanio {
static const uint8_t LED_CHANNELS = 32;
static const uint16_t LED_PIXELS_MAX = 128;
static const uint8_t LED_ENTITY_BANK = 99;
static const uint8_t LED_ENTITY_FIRST = 100;
struct LedChannelConfig {
  uint16_t first, count, period_ms;
  uint8_t enabled, effect, r, g, b, w;
  char name[19];
};
struct LedStripConfig {
  uint16_t count;
  uint8_t pin, model, brightness;
  LedChannelConfig channels[LED_CHANNELS];
};
inline void ledDefaults(LedStripConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));cfg.pin=0xFF;cfg.count=8;cfg.brightness=25;
  for(uint8_t i=0;i<LED_CHANNELS;++i){auto& c=cfg.channels[i];c.count=1;c.period_ms=1000;c.g=255;snprintf(c.name,sizeof(c.name),"LED%u",i+1U);}
}
inline bool ledValidate(const LedStripConfig& cfg) {
  if(cfg.count<1||cfg.count>LED_PIXELS_MAX||cfg.model>3||cfg.brightness>100)return false;
  for(const auto& c:cfg.channels){
    if(c.enabled>1||c.effect>2||c.period_ms<100||c.period_ms>10000||!memchr(c.name,0,sizeof(c.name))||strchr(c.name,'|')||strchr(c.name,'\r')||strchr(c.name,'\n'))return false;
    if(c.enabled&&(!c.count||(uint32_t)c.first+c.count>cfg.count||!c.name[0]))return false;
  }
  return true;
}
inline uint32_t ledEnabledMask(const LedStripConfig& cfg) {
  uint32_t mask=0;if(cfg.pin==0xFF)return 0;
  for(uint8_t i=0;i<LED_CHANNELS;++i)if(cfg.channels[i].enabled)mask|=uint32_t(1)<<i;
  return mask;
}
inline uint8_t ledLevel(const LedChannelConfig& c,uint32_t now) {
  if(!c.effect)return 255;const uint32_t phase=now%c.period_ms;
  if(c.effect==1)return phase<c.period_ms/2?255:0;
  const uint32_t half=c.period_ms/2;return (uint8_t)(phase<half?phase*255/half:(c.period_ms-phase)*255/(c.period_ms-half));
}
}
