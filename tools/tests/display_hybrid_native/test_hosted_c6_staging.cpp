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
enum{ESP_OK=0,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,pdPASS=1};
static bool allocation_ok=true,task_ok=true,wifi_active=false;
static unsigned allocations=0,freed=0,begins=0,writes=0,ends=0,activations=0,restarts=0;
static uint32_t version=0x020C0C;
static void* heap_caps_malloc(size_t n,int flags){assert(flags==3);if(!allocation_ok)return nullptr;++allocations;return malloc(n);}
static void heap_caps_free(void* ptr){++freed;free(ptr);}
static int mbedtls_sha256(const uint8_t* data,size_t len,uint8_t* out,int){return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(data),ULONG(len),out,32)<0?-1:0;}
static void(*task_fn)(void*)=nullptr;static void* task_arg=nullptr;
static int xTaskCreate(decltype(task_fn) fn,const char*,unsigned,void* arg,unsigned,void*){if(!task_ok)return 0;task_fn=fn;task_arg=arg;return pdPASS;}
static void vTaskDelay(int){++time_ms;}static void vTaskDelete(void*){}
struct{bool linkUp(){return true;}bool hasIP(){return true;}}ETH;
static bool hostedIsWiFiActive(){return wifi_active;}
static bool hostedInitWiFi(){wifi_active=true;return true;}
static bool hostedDeinitWiFi(){wifi_active=false;return true;}
static void hostedGetSlaveVersion(uint32_t*a,uint32_t*b,uint32_t*c){*a=version>>16;*b=(version>>8)&255;*c=version&255;}
static void hostedGetHostVersion(uint32_t*a,uint32_t*b,uint32_t*c){*a=2;*b=12;*c=13;}
struct esp_hosted_coprocessor_fwver_t{uint32_t major1,minor1,patch1;};
static int esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t*v){v->major1=version>>16;v->minor1=(version>>8)&255;v->patch1=version&255;return ESP_OK;}
static int esp_hosted_slave_ota_begin(){++begins;return ESP_OK;}
static int esp_hosted_slave_ota_write(uint8_t*,uint32_t n){writes+=n;return ESP_OK;}
static int esp_hosted_slave_ota_end(){++ends;return ESP_OK;}
static int esp_hosted_slave_ota_activate(){++activations;return ESP_OK;}
#include "generated_hosted_c6_updater.h"
static void restart(){++restarts;}
int main(int argc,char**argv){
  HostedC6Updater u;
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
  }else std::cout<<"SKIP: positive vendor-image fixture unavailable (negative staging tests passed)\n";
  assert(allocations==freed);
}
