#pragma once
#include <Adafruit_NeoPixel.h>
#include <atomic>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "FanIoLedConfig.h"

class FanIoVirtualLeds {
public:
  void begin(){core_=xPortGetCoreID();mutex_=xSemaphoreCreateMutex();}
  uint32_t mask()const{return mask_.load();}
  void setMask(uint32_t value){mask_.store(value);}
  void setChannel(uint8_t index,bool on){if(index>=32)return;const uint32_t bit=uint32_t(1)<<index;if(on)mask_.fetch_or(bit);else mask_.fetch_and(~bit);}
  void allow(bool value){allowed_.store(value);if(!value)mask_.store(0);}
  void stop(){
    if(!mutex_)return;xSemaphoreTake(mutex_,portMAX_DELAY);mask_.store(0);
    if(strip_){strip_->clear();strip_->show();const int pin=strip_->getPin();delete strip_;strip_=nullptr;if(pin>=0)pinMode(pin,INPUT);}
    xSemaphoreGive(mutex_);
  }
  bool configure(const ofe_fanio::LedStripConfig& cfg){
    if(!ofe_fanio::ledValidate(cfg))return false;
    if(cfg.pin==0xFF)return true;
    if(!mutex_)return false;
    static const neoPixelType models[]={NEO_GRB+NEO_KHZ800,NEO_GRBW+NEO_KHZ800,NEO_RGB+NEO_KHZ800,NEO_RGBW+NEO_KHZ800};
    xSemaphoreTake(mutex_,portMAX_DELAY);
    Adafruit_NeoPixel* next=new(std::nothrow)Adafruit_NeoPixel(cfg.count,cfg.pin,models[cfg.model]);
    if(!next||!next->getPixels()||!next->begin()){delete next;xSemaphoreGive(mutex_);return false;}
    if(!task_&&xTaskCreatePinnedToCore(taskEntry,"fanioLED",4096,this,1,&task_,core_)!=pdPASS){delete next;pinMode(cfg.pin,INPUT);task_=nullptr;xSemaphoreGive(mutex_);return false;}
    config_=cfg;strip_=next;mask_.store(0);dirty_=true;strip_->clear();strip_->show();xSemaphoreGive(mutex_);return true;
  }
private:
  static void taskEntry(void* p){for(;;){static_cast<FanIoVirtualLeds*>(p)->tick();vTaskDelay(pdMS_TO_TICKS(40));}}
  void tick(){
    if(xSemaphoreTake(mutex_,0)!=pdTRUE)return;
    if(strip_){
      const uint32_t active=allowed_.load()?mask_.load():0,now=millis();strip_->clear();
      // Higher channel numbers win if two active zones overlap.
      for(uint8_t i=0;i<ofe_fanio::LED_CHANNELS;++i){const auto& c=config_.channels[i];if(!c.enabled||!(active&(uint32_t(1)<<i)))continue;
        const uint32_t level=(uint32_t)ofe_fanio::ledLevel(c,now)*config_.brightness;
        const uint32_t color=strip_->Color(c.r*level/25500,c.g*level/25500,c.b*level/25500,c.w*level/25500);
        for(uint16_t p=c.first;p<(uint16_t)(c.first+c.count);++p)strip_->setPixelColor(p,color);
      }
      const uint8_t* pixels=strip_->getPixels();const uint16_t bytes=config_.count*(config_.model==1||config_.model==3?4:3);
      uint32_t hash=2166136261UL;for(uint16_t i=0;i<bytes;++i)hash=(hash^pixels[i])*16777619UL;
      if(dirty_||hash!=lastHash_){strip_->show();lastHash_=hash;dirty_=false;}
    }
    xSemaphoreGive(mutex_);
  }
  SemaphoreHandle_t mutex_=nullptr;
  TaskHandle_t task_=nullptr;
  BaseType_t core_=1;
  Adafruit_NeoPixel* strip_=nullptr;
  ofe_fanio::LedStripConfig config_{};
  std::atomic<uint32_t> mask_{0};
  std::atomic<bool> allowed_{false};
  uint32_t lastHash_=0;
  bool dirty_=true;
};
