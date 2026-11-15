#pragma once
#include "HostedC6Firmware.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino.h>
#include <ETH.h>
#include <Preferences.h>
#include <esp32-hal-hosted.h>
#include <esp_hosted.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cstring>

class HostedC6Updater {
public:
  enum Phase:uint8_t { Idle,Receiving,Ready,Preparing,Writing,Restarting,Failed,Recovery };
  Phase phase() const {return Phase(phase_.load(std::memory_order_acquire));}
  bool busy() const {const auto p=phase();return p!=Idle && p!=Failed;}
  bool radioOwned() const {const auto p=phase();return p==Preparing || p==Writing || p==Restarting || p==Recovery;}
  bool canRestart() const {return phase()==Recovery || phase()==Restarting;}
  uint32_t received() const {return received_;}
  uint32_t written() const {return written_.load();}
  uint32_t slaveVersion() const {
    uint32_t v=slave_.load();if(v)return v;
    uint32_t a=0,b=0,c=0;hostedGetSlaveVersion(&a,&b,&c);return (a<<16)|(b<<8)|c;
  }
  uint32_t hostVersion() const {uint32_t a=0,b=0,c=0;hostedGetHostVersion(&a,&b,&c);return (a<<16)|(b<<8)|c;}
  ofe_c6::Error error() const {return ofe_c6::Error(error_.load());}
  uint8_t verification() const {return verification_;}

  ofe_c6::Error begin(uint32_t size,uint32_t token,uint32_t now) {
    if (busy()) return ofe_c6::Busy;
    clear();
    if (size!=ofe_c6::IMAGE_SIZE) return fail(ofe_c6::Size);
    if (hostVersion()!=ofe_c6::VERSION) return fail(ofe_c6::HostMismatch);
    image_=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!image_) return fail(ofe_c6::NoPsram);
    token_=token;last_activity_=now;received_=0;written_=0;error_=ofe_c6::Ok;
    phase_.store(Receiving,std::memory_order_release);return ofe_c6::Ok;
  }
  bool owns(uint32_t token) const {return token && token==token_;}
  ofe_c6::Error append(uint32_t token,const uint8_t* data,size_t len,uint32_t now) {
    if (!owns(token) || phase()!=Receiving) return ofe_c6::Busy;
    if (!data || len>ofe_c6::IMAGE_SIZE-received_) return fail(ofe_c6::Size);
    memcpy(image_+received_,data,len);received_+=uint32_t(len);last_activity_=now;return ofe_c6::Ok;
  }
  ofe_c6::Error seal(uint32_t token,uint32_t now) {
    if (!owns(token) || phase()!=Receiving) return ofe_c6::Busy;
    if (received_!=ofe_c6::IMAGE_SIZE) return fail(ofe_c6::Incomplete);
    if (image_[0]!=0xE9 || image_[12]!=13 || image_[13]!=0) return fail(ofe_c6::Header);
    uint8_t digest[32];
    if (mbedtls_sha256(image_,received_,digest,0)!=0) return fail(ofe_c6::Hash);
    uint8_t diff=0;for(unsigned i=0;i<32;++i)diff|=digest[i]^ofe_c6::SHA256[i];
    if (diff) return fail(ofe_c6::Hash);
    last_activity_=now;phase_.store(Ready,std::memory_order_release);return ofe_c6::Ok;
  }
  bool cancel(uint32_t token) {
    if (!owns(token) || (phase()!=Receiving && phase()!=Ready && phase()!=Failed)) return false;
    clear();error_=ofe_c6::Ok;phase_.store(Idle,std::memory_order_release);return true;
  }
  void tick(uint32_t now,bool lan) {
    if ((phase()==Receiving || phase()==Ready) && (!lan || uint32_t(now-last_activity_)>=120000U))
      fail(lan ? ofe_c6::Expired : ofe_c6::NoLan);
  }
  bool start(uint32_t token,void(*restart)()) {
    if (!owns(token) || phase()!=Ready) return false;
    restart_=restart;error_=ofe_c6::Ok;phase_.store(Preparing,std::memory_order_release);
    if (xTaskCreate(worker,"c6-ota",8192,this,1,nullptr)!=pdPASS) {
      error_=ofe_c6::TaskFailed;phase_.store(Ready,std::memory_order_release);return false;
    }
    return true;
  }
  void verifyBoot() {
    uint8_t record[8];if(!readRecord(record))return;
    const uint32_t want=(uint32_t(record[4])<<16)|(uint32_t(record[5])<<8)|record[6];
    if ((record[2]==2 || record[2]==3) && want && slaveVersion()==want) {
      verification_=1;if(record[2]==2)mark(3,want);
    } else verification_=2;
  }

private:
  std::atomic<uint8_t> phase_{Idle},error_{ofe_c6::Ok};
  std::atomic<uint32_t> written_{0},slave_{0};
  uint8_t* image_=nullptr;
  uint32_t received_=0,token_=0,last_activity_=0;
  uint8_t verification_=0;
  void(*restart_)()=nullptr;
  void clear() {if(image_)heap_caps_free(image_);image_=nullptr;received_=0;token_=0;}
  ofe_c6::Error fail(ofe_c6::Error error) {clear();error_=error;phase_.store(Failed,std::memory_order_release);return error;}
  static bool readRecord(uint8_t* record) {
    Preferences p;if(!p.begin("ofe-c6",true))return false;
    const bool read=p.getBytesLength("update")==8 && p.getBytes("update",record,8)==8;p.end();
    if(!read)return false;uint8_t sum=0;for(unsigned i=0;i<7;++i)sum^=record[i];
    return record[0]==0xC6 && record[1]==1 && record[2]>=1 && record[2]<=3 && record[7]==sum;
  }
  static bool mark(uint8_t phase,uint32_t version=ofe_c6::VERSION) {
    uint8_t record[8]={0xC6,1,phase,0,uint8_t(version>>16),uint8_t(version>>8),uint8_t(version),0};
    for(unsigned i=0;i<7;++i)record[7]^=record[i];
    Preferences p;if(!p.begin("ofe-c6",false))return false;
    uint8_t check[8];const bool ok=p.putBytes("update",record,8)==8 && p.getBytes("update",check,8)==8 && memcmp(record,check,8)==0;
    p.end();return ok;
  }
  struct IO {
    HostedC6Updater& self;bool owned=false;
    bool lan() {return ETH.linkUp() && ETH.hasIP();}
    uint32_t now() {return millis();}
    bool acquire() {owned=!hostedIsWiFiActive();return !owned || hostedInitWiFi();}
    bool version(uint32_t& value) {
      esp_hosted_coprocessor_fwver_t v{};
      if(esp_hosted_get_coprocessor_fwversion(&v)!=ESP_OK)return false;
      if(v.major1>255 || v.minor1>255 || v.patch1>255)return false;
      value=(v.major1<<16)|(v.minor1<<8)|v.patch1;self.slave_=value;return value!=0;
    }
    uint32_t hostVersion() {return self.hostVersion();}
    bool pending(uint8_t phase) {return mark(phase);}
    bool begin() {return esp_hosted_slave_ota_begin()==ESP_OK;}
    bool write(const uint8_t* data,uint32_t len) {return esp_hosted_slave_ota_write(const_cast<uint8_t*>(data),len)==ESP_OK;}
    bool end() {return esp_hosted_slave_ota_end()==ESP_OK;}
    bool activate() {return esp_hosted_slave_ota_activate()==ESP_OK;}
    void progress(uint32_t offset) {self.written_=offset;self.phase_.store(Writing,std::memory_order_release);}
    void yield() {vTaskDelay(1);}
  };
  static void worker(void* arg) {
    auto& self=*static_cast<HostedC6Updater*>(arg);
    IO io{self};const auto result=ofe_c6::transfer(io,self.image_,self.received_);
    // An uncertain/started OTA keeps Hosted owned until an explicit P4 restart.
    if (!result.recovery && io.owned) hostedDeinitWiFi();
    if(self.image_)heap_caps_free(self.image_);self.image_=nullptr;
    self.error_=result.error;
    self.phase_.store(result.committed ? Restarting : result.recovery ? Recovery : Failed,std::memory_order_release);
    if(result.committed && self.restart_)self.restart_();
    vTaskDelete(nullptr);
  }
};
inline HostedC6Updater& hosted_c6() {static HostedC6Updater updater;return updater;}
inline bool hosted_c6_busy() {return hosted_c6().busy();}
inline bool hosted_c6_radio_owned() {return hosted_c6().radioOwned();}
inline bool hosted_c6_restart_blocked() {return hosted_c6_radio_owned() && !hosted_c6().canRestart();}
#else
inline bool hosted_c6_busy() {return false;}
inline bool hosted_c6_radio_owned() {return false;}
inline bool hosted_c6_restart_blocked() {return false;}
#endif
