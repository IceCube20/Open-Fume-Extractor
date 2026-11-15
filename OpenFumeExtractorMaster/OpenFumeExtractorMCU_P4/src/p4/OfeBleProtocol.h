#pragma once
namespace ofe_ble {
constexpr const char* SERVICE="b3a1f100-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* IDENTITY="b3a1f101-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* CONFIG="b3a1f102-8f54-4d28-b53b-9a1d0fe10001";
constexpr const char* RESULT="b3a1f103-8f54-4d28-b53b-9a1d0fe10001";
constexpr uint8_t MAX_DEVICES=6;
constexpr uint32_t SEARCH_TIMEOUT_MS=120000UL;
enum Phase:uint8_t { IDLE, STARTING, SCANNING, READY, ADVERTISING, CONNECTING,
  CONFIRM, AUTHENTICATING, SAVING, DONE, FAILED, CANCELLED, EXPIRED, LOW_MEMORY };
struct Candidate { uint64_t uid=0; char address[18]={}; uint8_t address_type=0,kind=0; int16_t rssi=0; bool paired=false; };
struct View {
  bool active=false; Phase phase=IDLE; uint32_t token=0,pin=0;
  uint8_t count=0; Candidate devices[MAX_DEVICES];
  uint32_t free_internal=0,largest_internal=0;
  uint8_t memory_stage=0; // 1 worker allocation, 2 before radio, 3 after radio, 4 radio stopped
  uint32_t started_ms=0;
  uint8_t error_phase=0;
  int error_code=0;
};
inline bool selectionReady(const View& v) {
  return (v.active && (v.phase==SCANNING || v.phase==READY)) ||
    (!v.active && (v.phase==DONE || v.phase==FAILED));
}
inline bool searching(const View& v) {
  return v.active && (v.phase==STARTING || v.phase==SCANNING || v.phase==READY);
}
inline uint32_t searchElapsedMs(const View& v,uint32_t now) {
  if (!searching(v)) return 0;
  const uint32_t elapsed=uint32_t(now-v.started_ms);
  return elapsed<SEARCH_TIMEOUT_MS ? elapsed : SEARCH_TIMEOUT_MS;
}
inline bool identity(uint64_t uid) { return (uid>>60)==4; }
inline bool parseAdvertisement(const uint8_t* data,size_t length,Candidate& out) {
  if (length!=15 || data[0]!=0xFF || data[1]!=0xFF || memcmp(data+2,"OFE1",4)) return false;
  const uint64_t uid=jbc_rs485::get_u64_le(data+6);
  if (!identity(uid) || data[14]>3) return false; // 3 = ELECROW P4 800x480.
  out.uid=uid; out.kind=data[14]; return true;
}
inline bool readConfig(const uint8_t* data,size_t length,uint64_t uid,ofe_wifi::Config& c) {
  if (length!=12+sizeof(c) || memcmp(data,"OPB1",4) || jbc_rs485::get_u64_le(data+4)!=uid) return false;
  memcpy(&c,data+12,sizeof(c));
  return ofe_wifi::configured(c) && c.from_master==1;
}
}
