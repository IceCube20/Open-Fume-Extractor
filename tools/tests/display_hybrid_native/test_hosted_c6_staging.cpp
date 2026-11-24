#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
#include "Arduino.h"
#include "Preferences.h"
#include "HostedC6Firmware.h"
static uint32_t time_ms=0;
static uint32_t millis(){return time_ms;}
using esp_err_t=int;
enum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NOT_SUPPORTED=0x106,ESP_ERR_TIMEOUT=0x107,ESP_ERR_INVALID_RESPONSE=0x108,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,pdPASS=1};
using wifi_mode_t=int;
enum{WIFI_MODE_NULL=0};
static int control_result=ESP_OK;
static unsigned control_calls=0;
static int esp_wifi_get_mode(wifi_mode_t* mode){++control_calls;*mode=1;return control_result;}
static bool allocation_ok=true,task_ok=true,wifi_active=false;
static bool ble_active=false,cache_available=true,oversized_reply=false;
static int version_result=ESP_OK;
static unsigned version_calls=0,tasks_created=0,radio_starts=0,radio_stops=0;
struct{template<class...A>int printf(const char* fmt,A...args){return std::printf(fmt,args...);}}Serial;
static unsigned allocations=0,freed=0,begins=0,writes=0,ends=0,activations=0,restarts=0;
static uint32_t version=0x020C0C;
static void* heap_caps_malloc(size_t n,int flags){assert(flags==3);if(!allocation_ok)return nullptr;++allocations;return malloc(n);}
static void heap_caps_free(void* ptr){++freed;free(ptr);}
static int mbedtls_sha256(const uint8_t* data,size_t len,uint8_t* out,int){return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(data),ULONG(len),out,32)<0?-1:0;}
struct mbedtls_sha1_context {std::vector<uint8_t> data;};
static void mbedtls_sha1_init(mbedtls_sha1_context*){}
static int mbedtls_sha1_starts(mbedtls_sha1_context* ctx){ctx->data.clear();return 0;}
static int mbedtls_sha1_update(mbedtls_sha1_context* ctx,const uint8_t* data,size_t len){ctx->data.insert(ctx->data.end(),data,data+len);return 0;}
static int mbedtls_sha1_finish(mbedtls_sha1_context* ctx,uint8_t* out){return BCryptHash(BCRYPT_SHA1_ALG_HANDLE,nullptr,0,ctx->data.data(),ULONG(ctx->data.size()),out,20)<0?-1:0;}
static void mbedtls_sha1_free(mbedtls_sha1_context*){}
static void(*task_fn)(void*)=nullptr;static void* task_arg=nullptr;
static int xTaskCreate(decltype(task_fn) fn,const char*,unsigned,void* arg,unsigned,void*){if(!task_ok)return 0;++tasks_created;task_fn=fn;task_arg=arg;return pdPASS;}
static void vTaskDelay(int){++time_ms;}static void vTaskDelete(void*){}
struct{bool linkUp(){return true;}bool hasIP(){return true;}}ETH;
static bool hostedIsWiFiActive(){return wifi_active;}
static bool hostedIsBLEActive(){return ble_active;}
static bool hostedInitWiFi(){++radio_starts;wifi_active=true;return true;}
static bool hostedDeinitWiFi(){++radio_stops;wifi_active=false;return true;}
static void hostedGetSlaveVersion(uint32_t*a,uint32_t*b,uint32_t*c){*a=cache_available?(oversized_reply?256:version>>16):0;*b=cache_available?(version>>8)&255:0;*c=cache_available?version&255:0;}
static void hostedGetHostVersion(uint32_t*a,uint32_t*b,uint32_t*c){*a=2;*b=12;*c=13;}
struct esp_hosted_coprocessor_fwver_t{uint32_t major1,minor1,patch1;};
static int esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t*v){++version_calls;if(version_result!=ESP_OK)return version_result;v->major1=oversized_reply?256:version>>16;v->minor1=(version>>8)&255;v->patch1=version&255;return ESP_OK;}
static bool hostedHasUpdate(){esp_hosted_coprocessor_fwver_t v{};if(esp_hosted_get_coprocessor_fwversion(&v)==ESP_OK)cache_available=true;return false;}
static int esp_hosted_slave_ota_begin(){++begins;return ESP_OK;}
static int esp_hosted_slave_ota_write(uint8_t*,uint32_t n){writes+=n;return ESP_OK;}
static int esp_hosted_slave_ota_end(){++ends;return ESP_OK;}
static int esp_hosted_slave_ota_activate(){++activations;return ESP_OK;}
#include "generated_hosted_c6_updater.h"
static void restart(){++restarts;}
static void testVersionProbe(){
  auto poll=[](HostedC6Updater& u,uint32_t now,bool blocked=false){time_ms=now;u.pollVersion(now,blocked);};
  cache_available=false;wifi_active=ble_active=false;
  HostedC6Updater p;
  assert(!p.slaveVersion());poll(p,1);assert(!p.busy()&&!tasks_created&&!version_calls);
  wifi_active=true;poll(p,2,true);assert(!p.busy()&&!tasks_created);
  poll(p,3);assert(p.busy()&&p.versionProbing()&&tasks_created==1&&!version_calls);
  assert(p.begin(ofe_c6::IMAGE_SIZE,1,3)==ofe_c6::Busy&&allocations==0);
  poll(p,4);assert(tasks_created==1);task_fn(task_arg);
  assert(!p.busy()&&!p.versionProbing()&&p.versionError()==ESP_OK&&p.slaveVersion()==version&&cache_available);
  cache_available=false;wifi_active=false;poll(p,40000);assert(p.slaveVersion()==version&&tasks_created==1);
  HostedC6Updater failed;wifi_active=true;version_result=42;
  unsigned queries=version_calls;
  poll(failed,5);task_fn(task_arg);assert(!failed.busy()&&failed.versionError()==42&&!failed.slaveVersion());
  assert(version_calls==queries+1 && control_calls==1 && failed.controlError()==ESP_OK && failed.controlMode()==1);
  unsigned before=tasks_created;poll(failed,6);assert(tasks_created==before);
  version_result=ESP_OK;version=0;poll(failed,30006);task_fn(task_arg);
  assert(failed.versionError()==ESP_ERR_INVALID_RESPONSE&&!failed.slaveVersion());
  version=0x020C0C;oversized_reply=true;poll(failed,60007);task_fn(task_arg);
  assert(failed.versionError()==ESP_ERR_INVALID_RESPONSE&&!failed.slaveVersion());
  cache_available=false;oversized_reply=false;task_ok=false;
  HostedC6Updater no_task;poll(no_task,1);assert(!no_task.busy()&&no_task.versionError()==ESP_ERR_NO_MEM);
  before=tasks_created;task_ok=true;poll(no_task,2);assert(tasks_created==before);
  poll(no_task,30001);task_fn(task_arg);assert(no_task.slaveVersion()==version);
  cache_available=false;wifi_active=false;ble_active=true;
  HostedC6Updater ble;poll(ble,0xFFFFFFF0U);task_fn(task_arg);assert(ble.slaveVersion()==version);
  cache_available=false;version_result=42;
  HostedC6Updater rollover;poll(rollover,0xFFFFFFF0U);task_fn(task_arg);
  before=tasks_created;poll(rollover,0x10U);assert(tasks_created==before);
  version_result=ESP_OK;poll(rollover,30000U);task_fn(task_arg);assert(rollover.slaveVersion()==version);
  cache_available=false;version_result=42;
  HostedC6Updater slow;poll(slow,0);time_ms=45000;task_fn(task_arg);
  before=tasks_created;poll(slow,45001);assert(tasks_created==before);
  poll(slow,74999);assert(tasks_created==before);
  version_result=ESP_OK;poll(slow,75000);task_fn(task_arg);assert(slow.slaveVersion()==version);
  cache_available=false;wifi_active=true;ble_active=false;version_result=-1;control_result=-1;
  HostedC6Updater both_failed;poll(both_failed,80000);task_fn(task_arg);
  assert(both_failed.versionError()==-1 && both_failed.controlError()==-1 && both_failed.controlMode()==255 && !both_failed.slaveVersion());
  assert(!radio_starts&&!radio_stops&&!begins&&!writes&&!ends&&!activations&&!restarts);
  cache_available=true;wifi_active=ble_active=false;task_fn=nullptr;task_arg=nullptr;time_ms=0;version_result=control_result=ESP_OK;
  std::cout<<"PASS: deferred C6 version query, SDK cache refresh, cached getters, retained version after WiFi off, single flight, update exclusion, retry/invalid/task guards; no radio reset or flash\n";
}
int main(int argc,char**argv){
  testVersionProbe();
  HostedC6Updater u;
  assert(u.begin(ofe_c6::TEST_SIZE,3,0,false,true,true)==ofe_c6::TrialDenied);
  assert(u.begin(ofe_c6::TEST_SIZE,3,0,true,true,false)==ofe_c6::TrialDenied);
  assert(u.begin(1,3,0)==ofe_c6::Size&&allocations==0);
  allocation_ok=false;assert(u.begin(ofe_c6::IMAGE_SIZE,3,0)==ofe_c6::NoPsram);allocation_ok=true;
  std::vector<uint8_t> bad(ofe_c6::IMAGE_SIZE);assert(u.begin(ofe_c6::IMAGE_SIZE,3,0)==ofe_c6::Ok);
  assert(u.append(4,bad.data(),bad.size(),0)==ofe_c6::Busy&&u.received()==0);
  assert(u.append(3,bad.data(),bad.size(),0)==ofe_c6::Ok);assert(u.seal(3,0)==ofe_c6::Header&&freed==1&&begins==0);
  bad[0]=0xE9;bad[12]=13;
  assert(u.begin(ofe_c6::IMAGE_SIZE,4,0)==ofe_c6::Ok);u.append(4,bad.data(),bad.size(),0);assert(u.seal(4,0)==ofe_c6::Hash&&begins==0);
  assert(u.begin(ofe_c6::IMAGE_SIZE,5,0)==ofe_c6::Ok);u.tick(120000,true);assert(u.error()==ofe_c6::Expired&&!u.busy());
  assert(u.begin(ofe_c6::IMAGE_SIZE,6,0)==ofe_c6::Ok);assert(!u.cancel(7)&&u.cancel(6));
  if(argc>1){
    std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),{});assert(image.size()==ofe_c6::IMAGE_SIZE);
    assert(u.begin(ofe_c6::IMAGE_SIZE,8,0)==ofe_c6::Ok);assert(u.append(8,image.data(),image.size(),0)==ofe_c6::Ok);assert(u.seal(8,0)==ofe_c6::Ok);
    task_ok=false;assert(!u.start(8,restart)&&u.phase()==HostedC6Updater::Ready&&begins==0);task_ok=true;
    assert(u.start(8,restart)&&u.radioOwned());assert(!u.cancel(8));assert(u.begin(ofe_c6::IMAGE_SIZE,9,0)==ofe_c6::Busy);
    task_fn(task_arg);assert(u.phase()==HostedC6Updater::Restarting&&writes==image.size()&&begins==1&&ends==1&&activations==1&&restarts==1);
    version=ofe_c6::VERSION;HostedC6Updater after_boot;after_boot.verifyBoot();assert(after_boot.verification()==1);
    std::cout<<"PASS: actual pinned vendor image SHA256, staging/token/memory lifecycle, worker, NVS commit and post-reboot version verification\n";
    if(argc>2){
      std::ifstream trial_file(argv[2],std::ios::binary);std::vector<uint8_t> trial_image((std::istreambuf_iterator<char>(trial_file)),{});
      assert(trial_image.size()==ofe_c6::TEST_SIZE);
      ofe_c6::DeveloperImage selected;selected.version=ofe_c6::TEST_VERSION;selected.size=uint32_t(trial_image.size());
      memcpy(selected.commit,"0c2733048bd001c87b0d6912843de5f259b2a13e",40);
      mbedtls_sha1_context hash;mbedtls_sha1_starts(&hash);char prefix[32];int n=snprintf(prefix,sizeof(prefix),"blob %u",selected.size);
      mbedtls_sha1_update(&hash,reinterpret_cast<uint8_t*>(prefix),n+1);mbedtls_sha1_update(&hash,trial_image.data(),trial_image.size());mbedtls_sha1_finish(&hash,selected.blob);
      HostedC6Updater arbitrary;
      assert(arbitrary.begin(selected.size,30,0,true,false,false,false,&selected)==ofe_c6::TrialDenied);
      auto stage_selected=[&](){assert(arbitrary.begin(selected.size,30,0,true,false,true,false,&selected)==ofe_c6::Ok);assert(arbitrary.append(30,trial_image.data(),trial_image.size(),0)==ofe_c6::Ok);return arbitrary.seal(30,0);};
      assert(stage_selected()==ofe_c6::Ok);assert(strlen(arbitrary.imageHash())==64 && arbitrary.developerImage());
      assert(!arbitrary.start(30,restart,false,false));assert(arbitrary.cancel(30));
      selected.blob[0]^=1;assert(stage_selected()==ofe_c6::Hash);selected.blob[0]^=1;
      selected.version++;assert(stage_selected()==ofe_c6::Header);selected.version--;
      assert(stage_selected()==ofe_c6::Ok);
      version=0x020D02;assert(arbitrary.start(30,restart,false,true));task_fn(task_arg);
      assert(arbitrary.phase()==HostedC6Updater::Restarting);
      version=ofe_c6::VERSION;
      HostedC6Updater approved;
      assert(approved.begin(ofe_c6::TEST_SIZE,12,0,false,true,false,true)==ofe_c6::Ok);
      approved.append(12,trial_image.data(),trial_image.size(),0);assert(approved.seal(12,0)==ofe_c6::Ok);
      assert(!approved.start(12,restart,false,false,false));
      assert(approved.start(12,restart,false,false,true));task_fn(task_arg);
      assert(approved.phase()==HostedC6Updater::Restarting&&!approved.trial());
      version=ofe_c6::VERSION;
      HostedC6Updater trial;
      assert(trial.begin(ofe_c6::TEST_SIZE,10,0,true,true,true)==ofe_c6::Ok&&trial.targetVersion()==ofe_c6::TEST_VERSION);
      assert(trial.append(10,trial_image.data(),trial_image.size(),0)==ofe_c6::Ok&&trial.seal(10,0)==ofe_c6::Ok);
      assert(!trial.start(10,restart,false,false)&&!trial.start(10,restart,true,true));
      const auto before=writes;
      assert(trial.start(10,restart,false,true));task_fn(task_arg);
      assert(trial.phase()==HostedC6Updater::Restarting&&writes-before==ofe_c6::TEST_SIZE);
      version=ofe_c6::TEST_VERSION;HostedC6Updater confirmed;confirmed.verifyBoot();assert(confirmed.verification()==1);
      HostedC6Updater restore;
      assert(restore.begin(ofe_c6::IMAGE_SIZE,11,0,true,false,true)==ofe_c6::Ok);
      restore.append(11,image.data(),image.size(),0);assert(restore.seal(11,0)==ofe_c6::Ok);
      assert(restore.start(11,restart,false,true));task_fn(task_arg);assert(restore.phase()==HostedC6Updater::Restarting);
      version=ofe_c6::VERSION;HostedC6Updater restored;restored.verifyBoot();assert(restored.verification()==1);
      std::cout<<"PASS: both actual vendor images, developer trial, explicit restore, no legacy mixing, frozen target and per-target boot journal\n";
    }
  }else std::cout<<"SKIP: positive vendor-image fixture unavailable (negative staging tests passed)\n";
  assert(allocations==freed);
}
