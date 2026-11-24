#pragma once
#include <stdint.h>
namespace ofe_c6 {
// Official Arduino 3.3.12 / ESP-Hosted 2.12.13 C6 SDIO network_adapter image.
static constexpr uint32_t IMAGE_SIZE=1226976;
static constexpr uint32_t VERSION=0x020C0D;
static constexpr char VERSION_TEXT[]="2.12.13";
#if defined(OFE_MASTER_P4_WAVESHARE_POE)
static constexpr char BOARD[]="waveshare-p4-wifi6-poe-eth-v3";
static constexpr char RECOVERY_URL[]="https://docs.waveshare.com/ESP32-P4-WIFI6-POE-ETH/FAQ";
#else
static constexpr char BOARD[]="waveshare-p4-module-dev-kit-prev3";
static constexpr char RECOVERY_URL[]="https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ";
#endif
static constexpr char ARDUINO_VERSION_TEXT[]="3.3.12";
static constexpr char IDF_VERSION_TEXT[]="5.5.5";
static constexpr char CATALOG_URL[]="https://raw.githubusercontent.com/IceCube20/Open-Fume-Extractor/main/updates/hosted-c6-v2.json";
static constexpr char SHA256_TEXT[]="f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9";
static constexpr char URL[]="https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.13.bin";
static constexpr uint8_t SHA256[32]={0xf8,0x50,0x99,0x49,0x82,0x6c,0x19,0x7a,0x4f,0xa3,0xc7,0x95,0x89,0x43,0xb4,0xe8,0x98,0x5c,0xff,0xdf,0xdf,0x42,0x04,0xc2,0x84,0x7d,0x89,0x53,0x90,0x51,0x21,0xc9};
static constexpr uint32_t TEST_VERSION=0x020D01;
static constexpr uint32_t TEST_SIZE=1244160;
static constexpr char TEST_URL[]="https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.13.1.bin";
static constexpr char TEST_SHA256_TEXT[]="2d8a98801680f1feccdc8c20ace05617c425d4f8f5041fd5b17cb8dc802c643f";
static constexpr uint8_t TEST_SHA256[32]={0x2d,0x8a,0x98,0x80,0x16,0x80,0xf1,0xfe,0xcc,0xdc,0x8c,0x20,0xac,0xe0,0x56,0x17,0xc4,0x25,0xd4,0xf8,0xf5,0x04,0x1f,0xd5,0xb1,0x7c,0xb8,0xdc,0x80,0x2c,0x64,0x3f};
inline bool trialBoardEligible(bool poe,bool developer,uint16_t revision,uint32_t flash) {
  return poe && developer && revision==302 && flash==32U*1024U*1024U;
}
inline bool approved2131Board(bool poe,uint16_t revision,uint32_t flash) {
  return poe && revision==302 && flash==32U*1024U*1024U;
}
static constexpr char APPROVED_2131_BOARD[]="waveshare-p4-wifi6-poe-eth-v3.2";
static constexpr uint32_t DEVELOPER_IMAGE_LIMIT=2U*1024U*1024U;
struct DeveloperImage {
  uint32_t version=0,size=0;
  uint8_t blob[20]{};
  char commit[41]{};
};
inline bool developerImageValid(const DeveloperImage& image) {
  if(image.version<0x020600 || image.version>0x02FFFF || image.size<256 || image.size>DEVELOPER_IMAGE_LIMIT || image.commit[40])return false;
  for(unsigned i=0;i<40;++i)if(!((image.commit[i]>='0' && image.commit[i]<='9') || (image.commit[i]>='a' && image.commit[i]<='f')))return false;
  uint8_t any=0;for(auto b:image.blob)any|=b;return any!=0;
}
inline bool developerBoardEligible(bool poe,bool developer,uint16_t revision,uint32_t flash) {
  return developer && flash>=16U*1024U*1024U && (poe ? revision>=300 && revision<400 : revision<300);
}
enum Error:uint8_t { Ok,Busy,NoLan,NoPsram,Size,Offset,Header,Hash,Incomplete,Expired,HostMismatch,SlaveUnavailable,SlaveOld,SlaveNewer,AlreadyCurrent,PendingFailed,BeginFailed,WriteFailed,EndFailed,ActivateFailed,TaskFailed,TimedOut,VersionUnavailable,ControlUnavailable,TrialDenied };
enum UpdatePolicy:uint8_t { StrictVersion,LegacyConfirmed,TrialConfirmed };
inline const char* errorName(Error error) {
  static const char* names[]={"OK","BUSY","LAN_REQUIRED","NO_PSRAM","SIZE","OFFSET","WRONG_CHIP","SHA256","INCOMPLETE","EXPIRED","HOST_SDK_MISMATCH","C6_UNAVAILABLE","C6_TOO_OLD","C6_NEWER","ALREADY_CURRENT","NVS_FAILED","BEGIN_FAILED","WRITE_FAILED","END_FAILED","ACTIVATE_FAILED","TASK_FAILED","TIMEOUT","C6_VERSION_UNAVAILABLE","C6_CONTROL_UNAVAILABLE","C6_TRIAL_NOT_ALLOWED"};
  return error<=TrialDenied ? names[error] : "UNKNOWN";
}
struct TransferResult {Error error;bool recovery;bool committed;};

// IO provides the SDK boundary; a failed or partial transfer never reaches activation.
template<class IO> TransferResult transfer(IO& io,const uint8_t* image,uint32_t size,UpdatePolicy policy=StrictVersion,bool test_image=false,bool trial_authorized=false,bool approved=false,const DeveloperImage* selected=nullptr) {
  const bool trial=policy==TrialConfirmed;
  if(selected && (!trial || !trial_authorized || !developerImageValid(*selected)))return {TrialDenied,false,false};
  if ((test_image && !trial && !approved) || (trial && !trial_authorized)) return {TrialDenied,false,false};
  const uint32_t target=selected?selected->version:test_image?TEST_VERSION:VERSION;
  if (!image || size!=(selected?selected->size:test_image?TEST_SIZE:IMAGE_SIZE)) return {Size,false,false};
  if (!io.lan()) return {NoLan,false,false};
  if (io.hostVersion()!=VERSION) return {HostMismatch,false,false};
  uint32_t slave=0;
  if (!io.acquire()) return {SlaveUnavailable,false,false};
  const bool known=io.version(slave);
  if (known && !slave) return {VersionUnavailable,false,false};
  if (trial && (!known || slave<0x020600 || slave>0x02FFFF)) return {TrialDenied,false,false};
  if (!trial && known && slave>target) return {SlaveNewer,false,false};
  if (known && slave==target) return {AlreadyCurrent,false,false};
  if (!trial && (!known || slave<0x020600)) {
    if (policy!=LegacyConfirmed) return {known ? SlaveOld : VersionUnavailable,false,false};
    if (!known && !io.legacyVersionEligible()) return {VersionUnavailable,false,false};
    // Unknown versions never imply permission; confirmation and a fresh control reply are required.
    if (!io.controlReady()) return {ControlUnavailable,false,false};
  }
  if (!io.lan()) return {NoLan,false,false};
  if (!io.pending(1)) return {PendingFailed,false,false};
  const uint32_t started=io.now();
  if (!io.begin()) return {BeginFailed,true,false};
  for (uint32_t offset=0;offset<size;) {
    if (!io.lan()) return {NoLan,true,false};
    if (uint32_t(io.now()-started)>=180000U) return {TimedOut,true,false};
    const uint32_t count=size-offset>2048U ? 2048U : size-offset;
    if (!io.write(image+offset,count)) return {WriteFailed,true,false};
    offset+=count;io.progress(offset);io.yield();
  }
  if (!io.lan()) return {NoLan,true,false};
  if (uint32_t(io.now()-started)>=180000U) return {TimedOut,true,false};
  if (!io.pending(2)) return {PendingFailed,true,false};
  if (!io.end()) return {EndFailed,true,false};
  if (!io.lan()) return {NoLan,true,false};
  if (!io.activate()) return {ActivateFailed,true,false};
  return {Ok,true,true};
}
}
