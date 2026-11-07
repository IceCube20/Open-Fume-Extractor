#include <cassert>
#include <cstdio>
#include <vector>
#include "OfeRgbOta.h"

static esp_partition_t running{65536}, target{65536};
static const esp_partition_t* next=&target;
static esp_err_t begin_result=ESP_OK, write_result=ESP_OK, end_result=ESP_OK, boot_result=ESP_OK;
static unsigned begins=0, writes=0, ends=0, aborts=0, boots=0;
static int64_t now_us=0;
static uint32_t write_time_us=12345;
static std::vector<uint8_t> image;
static bool validated=false;
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return next; }
const esp_partition_t* esp_ota_get_running_partition() { return &running; }
esp_err_t esp_ota_begin(const esp_partition_t* p,size_t mode,esp_ota_handle_t* h) {
  assert(p==&target && mode==OTA_WITH_SEQUENTIAL_WRITES);
  ++begins; *h=123; image.clear(); validated=false; return begin_result;
}
esp_err_t esp_ota_write(esp_ota_handle_t h,const void* data,size_t n) {
  assert(h==123); ++writes; now_us+=write_time_us;
  if (write_result==ESP_OK) {
    const auto* p=static_cast<const uint8_t*>(data); image.insert(image.end(),p,p+n);
  }
  return write_result;
}
esp_err_t esp_ota_end(esp_ota_handle_t h) { assert(h==123); ++ends; validated=end_result==ESP_OK; return end_result; }
esp_err_t esp_ota_abort(esp_ota_handle_t h) { assert(h==123); ++aborts; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p) {
  assert(p==&target && validated); ++boots; return boot_result;
}
int64_t esp_timer_get_time() { return now_us; }

int main() {
  uint8_t data[4096]={};
  for (size_t i=0;i<sizeof(data);++i) data[i]=uint8_t(i);
  OfeRgbOta writer;
  assert(writer.begin(8193));
  assert(!writer.begin(8193) && begins==1);
  assert(writer.write(data,4096)==4096);
  write_time_us=50000;
  assert(writer.write(data,4096)==4096 && writer.write(data,1)==1);
  assert(image.size()==8193 && boots==0);
  for(size_t i=0;i<image.size();++i) assert(image[i]==uint8_t(i));
  auto timing=writer.timing();
  assert(timing.writes==3 && timing.total_us==112345 && timing.max_us==50000 && timing.error==ESP_OK);
  assert(writer.end(true) && ends==1 && boots==1);
  writer.abort(); assert(aborts==0); // esp_ota_end consumed the handle.
  assert(writer.write(data,1)==0 && !writer.end(true) && boots==1);

  assert(writer.begin(8192));
  assert(writer.write(data,4096)==4096 && !writer.end(true));
  assert(aborts==1 && ends==1 && boots==1); // Incomplete image is never selected.
  writer.abort(); assert(aborts==1);
  assert(writer.begin(1) && writer.write(data,2)==0);
  writer.abort(); assert(aborts==2 && boots==1);
  assert(writer.begin(1) && writer.write(nullptr,1)==0);
  writer.abort(); assert(aborts==3);

  begin_result=ESP_FAIL;
  assert(!writer.begin(1)); writer.abort(); assert(aborts==3);
  begin_result=ESP_OK; write_result=ESP_FAIL;
  assert(writer.begin(1) && writer.write(data,1)==0);
  assert(aborts==4 && writer.timing().error==ESP_FAIL && boots==1);
  writer.abort(); assert(aborts==4);
  write_result=ESP_OK; end_result=ESP_FAIL;
  assert(writer.begin(1) && writer.write(data,1)==1 && !writer.end(true));
  assert(aborts==4 && boots==1 && writer.timing().error==ESP_FAIL);
  end_result=ESP_OK; boot_result=ESP_FAIL;
  assert(writer.begin(1) && writer.write(data,1)==1 && !writer.end(true));
  assert(aborts==4 && boots==2 && writer.timing().error==ESP_FAIL);
  boot_result=ESP_OK;

  next=nullptr; assert(!writer.begin(1));
  next=&running; assert(!writer.begin(1));
  next=&target; assert(!writer.begin(target.size+1) && !writer.begin(0));
  assert(writer.begin(OTA_SIZE_UNKNOWN) && writer.write(data,1)==1 && writer.end(true));
  assert(boots==3);
  puts("PASS: actual RGB sequential OTA writer, bounds, exact bytes, validation-before-boot, abort/failure cleanup and timing.");
}
