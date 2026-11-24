#pragma once
#include "HostedC6Firmware.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino.h>
#include <ETH.h>
#include <Preferences.h>
#include <esp32-hal-hosted.h>
#include <esp_hosted.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha1.h>
#include <atomic>
#include <cstring>

class HostedC6Updater {
public:
  enum Phase:uint8_t { Idle,Receiving,Ready,Preparing,Writing,Restarting,Failed,Recovery };
  Phase phase() const {return Phase(phase_.load(std::memory_order_acquire));}
  bool busy() const {const auto p=phase();return version_probing_.load() || (p!=Idle && p!=Failed);}
  bool radioOwned() const {const auto p=phase();return p==Preparing || p==Writing || p==Restarting || p==Recovery;}
  bool canRestart() const {return phase()==Recovery || phase()==Restarting;}
  uint32_t received() const {return received_;}
  uint32_t written() const {return written_.load();}
  bool trial() const {return trial_;}
  bool testImage() const {return test_image_;}
  bool developerImage() const {return selected_.version!=0;}
  const char* sourceCommit() const {return selected_.commit;}
  const char* imageHash() const {return image_hash_;}
  uint32_t targetVersion() const {return developerImage()?selected_.version:test_image_?ofe_c6::TEST_VERSION:ofe_c6::VERSION;}
  uint32_t imageSize() const {return developerImage()?selected_.size:test_image_?ofe_c6::TEST_SIZE:ofe_c6::IMAGE_SIZE;}
  uint32_t slaveVersion() const {
    uint32_t v=slave_.load();if(v)return v;
    uint32_t a=0,b=0,c=0;hostedGetSlaveVersion(&a,&b,&c);return a<=255 && b<=255 && c<=255 ? (a<<16)|(b<<8)|c : 0;
  }
  bool versionProbing() const {return version_probing_.load();}
  int32_t versionError() const {return version_error_.load();}
  int32_t controlError() const {return control_error_.load();}
  uint8_t controlMode() const {return control_mode_.load();}
  void pollVersion(uint32_t now,bool blocked) {
    if (busy() || blocked) return;
    const uint32_t cached=slaveVersion();
    if (cached) {slave_=cached;return;}
    if ((!hostedIsWiFiActive() && !hostedIsBLEActive()) ||
        (version_attempted_ && uint32_t(now-version_last_attempt_.load())<30000U)) return;
    version_attempted_=true;version_last_attempt_=now;
    version_probing_=true;
    if (xTaskCreate(versionWorker,"c6-version",4096,this,1,nullptr)!=pdPASS) {
      version_error_=ESP_ERR_NO_MEM;version_probing_=false;
    }
  }
  uint32_t hostVersion() const {uint32_t a=0,b=0,c=0;hostedGetHostVersion(&a,&b,&c);return (a<<16)|(b<<8)|c;}
  ofe_c6::Error error() const {return ofe_c6::Error(error_.load());}
  uint8_t verification() const {return verification_.load();}

  ofe_c6::Error begin(uint32_t size,uint32_t token,uint32_t now,bool trial=false,bool test_image=false,bool trial_authorized=false,bool approved=false,const ofe_c6::DeveloperImage* selected=nullptr) {
    if (busy()) return ofe_c6::Busy;
    clear();
    if ((test_image && !trial && !approved) || (trial && !trial_authorized)) return fail(ofe_c6::TrialDenied);
    if(selected && (!trial || !trial_authorized || !ofe_c6::developerImageValid(*selected)))return fail(ofe_c6::TrialDenied);
    if(selected)selected_=*selected;
    trial_=trial;test_image_=test_image;
    if (size!=imageSize()) return fail(ofe_c6::Size);
    if (hostVersion()!=ofe_c6::VERSION) return fail(ofe_c6::HostMismatch);
    image_=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!image_) return fail(ofe_c6::NoPsram);
    token_=token;last_activity_=now;received_=0;written_=0;error_=ofe_c6::Ok;
    phase_.store(Receiving,std::memory_order_release);return ofe_c6::Ok;
  }
  bool owns(uint32_t token) const {return token && token==token_;}
  ofe_c6::Error append(uint32_t token,const uint8_t* data,size_t len,uint32_t now) {
    if (!owns(token) || phase()!=Receiving) return ofe_c6::Busy;
    if (!data || len>imageSize()-received_) return fail(ofe_c6::Size);
    memcpy(image_+received_,data,len);received_+=uint32_t(len);last_activity_=now;return ofe_c6::Ok;
  }
  ofe_c6::Error seal(uint32_t token,uint32_t now) {
    if (!owns(token) || phase()!=Receiving) return ofe_c6::Busy;
    if (received_!=imageSize()) return fail(ofe_c6::Incomplete);
    if (image_[0]!=0xE9 || image_[12]!=13 || image_[13]!=0) return fail(ofe_c6::Header);
    uint8_t digest[32];
    if (mbedtls_sha256(image_,received_,digest,0)!=0) return fail(ofe_c6::Hash);
    if(developerImage()) {
      char version[16];snprintf(version,sizeof(version),"%u.%u.%u",unsigned(selected_.version>>16),unsigned((selected_.version>>8)&255),unsigned(selected_.version&255));
      if(image_[32]!=0x32 || image_[33]!=0x54 || image_[34]!=0xCD || image_[35]!=0xAB ||
         memcmp(image_+48,version,strlen(version)+1) || memcmp(image_+80,"network_adapter",16))return fail(ofe_c6::Header);
      // Git blob identity comes from the developer's HTTPS upstream selection, not OFE approval.
      char prefix[32];const int n=snprintf(prefix,sizeof(prefix),"blob %lu",static_cast<unsigned long>(received_));
      mbedtls_sha1_context ctx;mbedtls_sha1_init(&ctx);uint8_t blob[20];
      const int result=mbedtls_sha1_starts(&ctx) || mbedtls_sha1_update(&ctx,reinterpret_cast<const uint8_t*>(prefix),n+1) ||
        mbedtls_sha1_update(&ctx,image_,received_) || mbedtls_sha1_finish(&ctx,blob);
      mbedtls_sha1_free(&ctx);
      if(result || memcmp(blob,selected_.blob,20))return fail(ofe_c6::Hash);
    }
    const uint8_t* expected=developerImage()?digest:test_image_?ofe_c6::TEST_SHA256:ofe_c6::SHA256;
    uint8_t diff=0;for(unsigned i=0;i<32;++i)diff|=digest[i]^expected[i];
    if (diff) return fail(ofe_c6::Hash);
    for(unsigned i=0;i<32;++i)snprintf(image_hash_+i*2,3,"%02x",digest[i]);
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
  bool start(uint32_t token,void(*restart)(),bool legacy=false,bool trial_authorized=false,bool approved=false) {
    if (!owns(token) || phase()!=Ready) return false;
    if (trial_ && (!trial_authorized || legacy)) return false;
    if (test_image_ && !trial_ && !approved) return false;
    approved_=approved && test_image_ && !trial_;
    policy_=trial_?ofe_c6::TrialConfirmed:legacy ? ofe_c6::LegacyConfirmed : ofe_c6::StrictVersion;
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
  std::atomic<bool> version_probing_{false};
  std::atomic<int32_t> version_error_{ESP_OK};
  std::atomic<int32_t> control_error_{ESP_ERR_INVALID_STATE};
  std::atomic<uint8_t> control_mode_{255};
  std::atomic<uint8_t> verification_{0};
  bool version_attempted_=false;
  std::atomic<uint32_t> version_last_attempt_{0};
  uint8_t* image_=nullptr;
  uint32_t received_=0,token_=0,last_activity_=0;
  ofe_c6::UpdatePolicy policy_=ofe_c6::StrictVersion;
  bool trial_=false,test_image_=false;
  ofe_c6::DeveloperImage selected_{};
  char image_hash_[65]{};
  bool approved_=false;
  void(*restart_)()=nullptr;
  void clear() {if(image_)heap_caps_free(image_);image_=nullptr;received_=0;token_=0;trial_=test_image_=approved_=false;selected_={};image_hash_[0]=0;}
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
  static esp_err_t queryVersion(uint32_t& value) {
    value=0;esp_hosted_coprocessor_fwver_t v{};
    const esp_err_t result=esp_hosted_get_coprocessor_fwversion(&v);
    if (result!=ESP_OK) return result;
    if (v.major1>255 || v.minor1>255 || v.patch1>255) return ESP_ERR_INVALID_RESPONSE;
    value=(v.major1<<16)|(v.minor1<<8)|v.patch1;
    return value ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
  }
  bool probeControl() {
    wifi_mode_t mode=WIFI_MODE_NULL;
    const esp_err_t control=esp_wifi_get_mode(&mode);
    control_error_=control;control_mode_=control==ESP_OK ? uint8_t(mode) : 255;
    return control==ESP_OK && uint8_t(mode)<=3;
  }
  void recordVersionResult(esp_err_t result) {
    version_error_=result;control_error_=ESP_ERR_INVALID_STATE;control_mode_=255;
    if (result==ESP_OK) return;
    // Check another RPC without treating an unknown version as an absent chip.
    if (hostedIsWiFiActive()) probeControl();
    Serial.printf("[C6] Version RPC failed: %ld; WiFi-mode RPC: %ld, mode=%u\n",
                  long(result),long(controlError()),unsigned(controlMode()));
  }
  static void versionWorker(void* arg) {
    auto& self=*static_cast<HostedC6Updater*>(arg);
    uint32_t value=0;
    const esp_err_t result=queryVersion(value);
    self.recordVersionResult(result);
    if (result==ESP_OK) {
      // Refresh Arduino's cache too: its BLE initialization uses the slave version.
      (void)hostedHasUpdate();
      self.slave_=value;self.verifyBoot();
      Serial.printf("[C6] Firmware version: %u.%u.%u\n",unsigned(value>>16),unsigned((value>>8)&255),unsigned(value&255));
    } else Serial.printf("[C6] Firmware version query failed: 0x%X\n",unsigned(result));
    self.version_last_attempt_=millis();
    self.version_probing_=false;vTaskDelete(nullptr);
  }
  struct IO {
    HostedC6Updater& self;bool owned=false;
    bool lan() {return ETH.linkUp() && ETH.hasIP();}
    uint32_t now() {return millis();}
    bool acquire() {owned=!hostedIsWiFiActive();return !owned || hostedInitWiFi();}
    bool version(uint32_t& value) {
      const esp_err_t result=queryVersion(value);
      self.recordVersionResult(result);
      if(result!=ESP_OK)return false;
      self.slave_=value;return true;
    }
    bool legacyVersionEligible() {
      const auto error=self.versionError();
      return error==ESP_FAIL || error==ESP_ERR_TIMEOUT || error==ESP_ERR_NOT_SUPPORTED;
    }
    bool controlReady() {return self.probeControl();}
    uint32_t hostVersion() {return self.hostVersion();}
    bool pending(uint8_t phase) {return mark(phase,self.targetVersion());}
    bool begin() {return esp_hosted_slave_ota_begin()==ESP_OK;}
    bool write(const uint8_t* data,uint32_t len) {return esp_hosted_slave_ota_write(const_cast<uint8_t*>(data),len)==ESP_OK;}
    bool end() {return esp_hosted_slave_ota_end()==ESP_OK;}
    bool activate() {return esp_hosted_slave_ota_activate()==ESP_OK;}
    void progress(uint32_t offset) {self.written_=offset;self.phase_.store(Writing,std::memory_order_release);}
    void yield() {vTaskDelay(1);}
  };
  static void worker(void* arg) {
    auto& self=*static_cast<HostedC6Updater*>(arg);
    Serial.printf("[C6] Update policy: %s\n",self.trial_?"developer-trial":self.policy_==ofe_c6::LegacyConfirmed ? "legacy-confirmed" : "strict");
    const auto target=self.targetVersion();
    Serial.printf("[C6] Selected %u.%u.%u; SHA256=%s; upstream=%s\n",unsigned(target>>16),unsigned((target>>8)&255),unsigned(target&255),self.imageHash(),self.sourceCommit());
    IO io{self};const auto result=ofe_c6::transfer(io,self.image_,self.received_,self.policy_,self.test_image_,self.trial_,self.approved_,self.developerImage()?&self.selected_:nullptr);
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
