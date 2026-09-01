#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Rs485PeripheralBus.h"
using namespace jbc_rs485;
static uint32_t clock_ms=100;
static uint32_t millis() { return clock_ms; }
static void delay(uint32_t ms) { clock_ms+=ms; }

struct FakeUpdate {
  bool begin_ok=true, write_ok=true, end_ok=true;
  unsigned begins=0, ends=0, aborts=0;
  std::vector<uint8_t> image;
  bool begin(size_t) { ++begins; image.clear(); return begin_ok; }
  size_t write(const uint8_t* p,size_t n) {
    if (!write_ok) return 0;
    image.insert(image.end(),p,p+n); return n;
  }
  bool end(bool) { ++ends; return end_ok; }
  void abort() { ++aborts; }
} Update;
struct FakeWifi {
  bool allowed=true, wireless=true, locked=false;
  bool beginUpdate() { if (!allowed) return false; locked=true; return true; }
  void finishUpdate() { locked=false; }
  bool updateWireless() const { return wireless; }
} display_wifi;
struct FakeEsp {
  unsigned restarts=0;
  uint32_t getFreeSketchSpace() { return 2*1024*1024; }
  void restart() { ++restarts; }
} ESP;

static bool fw_update_active=false;
static uint32_t fw_update_started_ms=0,fw_update_last_ms=0,fw_update_offset=0,fw_update_size=0;
static std::atomic<uint32_t> fw_update_reboot_ms{0};
static constexpr uint32_t FW_UPDATE_TIMEOUT_MS=30000, UPDATE_SIZE_UNKNOWN=UINT32_MAX;
static constexpr size_t FW_UPDATE_WRITE_BUFFER_SIZE=4096;
static uint8_t fw_update_write_buffer[FW_UPDATE_WRITE_BUFFER_SIZE];
static size_t fw_update_write_len=0;
static uint8_t fw_update_last_draw_percent=255,fw_update_last_draw_target=255,module_addr=0x40;
static uint32_t fw_update_last_draw_ms=0;
static uint8_t response=255;
static void send_status_response(const Frame&,uint8_t status) { response=status; }
static void draw_update_progress_throttled(uint8_t,uint8_t,const char*,bool=false) {}
static bool running_in_rs485_task() { return true; }
static void lvgl_timer_handler_profiled() {}
static void lvgl_flush_canvas_if_dirty(bool) {}

#include "generated_display_ota.inc.h"

static void reset(bool wifi=true) {
  Update=FakeUpdate(); display_wifi=FakeWifi(); display_wifi.wireless=wifi;
  ESP.restarts=0; fw_update_active=false; fw_update_reboot_ms.store(0);
  fw_update_write_len=0; fw_update_offset=0; fw_update_size=0;
  fw_update_started_ms=0; fw_update_last_ms=0;
}
static Frame begin_frame(uint32_t size) {
  Frame f; f.len=4; put_u32_le(f.payload,size); return f;
}
static void transfer(uint32_t size) {
  for(uint32_t offset=0;offset<size;) {
    Frame f; const size_t n=(size-offset)>188?188:size-offset;
    f.len=uint8_t(n+4); put_u32_le(f.payload,offset);
    for(size_t j=0;j<n;++j) f.payload[4+j]=uint8_t(offset+j);
    handle_fw_chunk(f); assert(response==STATUS_OK);
    // Lost ACK: repeat the same offset. No flash data may be appended twice.
    handle_fw_chunk(f); assert(response==STATUS_OK);
    offset+=uint32_t(n); assert(fw_update_offset==offset);
  }
}
int main() {
  delay(100); // millis() must be nonzero for committed-image timestamps.
  Frame end;
  reset(); Frame b=begin_frame(8193);
  handle_fw_begin(b); assert(response==STATUS_OK && display_wifi.locked);
  handle_fw_begin(b); assert(response==STATUS_OK && Update.begins==1);
  handle_fw_begin(begin_frame(100)); assert(response==STATUS_BUSY && fw_update_size==8193);
  transfer(8193);
  handle_fw_begin(b); assert(response==STATUS_BUSY && fw_update_offset==8193);
  handle_fw_end(end); assert(response==STATUS_OK && !fw_update_active);
  assert(display_wifi.locked && fw_update_reboot_ms.load() && ESP.restarts==0);
  assert(Update.image.size()==8193);
  for(size_t i=0;i<Update.image.size();++i) assert(Update.image[i]==uint8_t(i));
  handle_fw_end(end); assert(response==STATUS_OK && Update.ends==1);
  fw_update_reboot_ms.store(uint32_t(millis()-12001));
  fw_update_check_timeout(); assert(ESP.restarts==1);

  reset(false); b=begin_frame(200); handle_fw_begin(b); transfer(200);
  handle_fw_end(end); assert(ESP.restarts==1 && !display_wifi.locked && !fw_update_reboot_ms.load());
  reset(); handle_fw_begin(b); transfer(188); handle_fw_end(end);
  assert(response==STATUS_BAD_VALUE && !display_wifi.locked && Update.ends==0 && Update.aborts==1);
  reset(); handle_fw_begin(b); fw_update_abort_local();
  assert(!fw_update_active && !display_wifi.locked && Update.aborts==1);
  handle_fw_begin(b); assert(response==STATUS_OK && fw_update_active);
  fw_update_last_ms=uint32_t(millis()-FW_UPDATE_TIMEOUT_MS-1);
  fw_update_check_timeout(); assert(!fw_update_active && !display_wifi.locked);
  reset(); Update.begin_ok=false; handle_fw_begin(b);
  assert(response==STATUS_BUSY && !display_wifi.locked);
  reset(); display_wifi.allowed=false; handle_fw_begin(b);
  assert(response==STATUS_BUSY && Update.begins==0);
  reset(); handle_fw_begin(b); transfer(200); Update.write_ok=false; handle_fw_end(end);
  assert(response==STATUS_BUSY && !display_wifi.locked && ESP.restarts==0);
  reset(); handle_fw_begin(b); transfer(200); Update.end_ok=false; handle_fw_end(end);
  assert(response==STATUS_BUSY && !display_wifi.locked && ESP.restarts==0);
  puts("PASS: real display OTA handlers: BEGIN/CHUNK/END retries, complete data, incomplete END, abort/restart, timeout, failures and WiFi/wired reboot policy.");
}
