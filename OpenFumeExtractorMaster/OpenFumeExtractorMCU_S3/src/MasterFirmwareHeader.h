#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Multipart upload boundaries are not image-header boundaries.
class MasterFirmwareHeader {
public:
  explicit MasterFirmwareHeader(uint16_t chip) : chip_(chip) {}
  void reset() { used_=0; valid_=true; memset(bytes_,0,sizeof(bytes_)); }
  bool append(const uint8_t* data,size_t len) {
    if (!valid_ || (!data && len)) return valid_=false;
    const size_t remaining=sizeof(bytes_)-used_;
    const size_t count=len<remaining ? len : remaining;
    if (count) { memcpy(bytes_+used_,data,count); used_+=count; }
    if (used_==sizeof(bytes_)) valid_=bytes_[0]==0xE9 && bytes_[1]>0 &&
      bytes_[1]<=16 && bytes_[2]<=3 && uint16_t(bytes_[12]|uint16_t(bytes_[13])<<8)==chip_;
    return valid_;
  }
  bool complete() const { return valid_ && used_==sizeof(bytes_); }
private:
  uint16_t chip_;
  uint8_t bytes_[24]{};
  size_t used_=0;
  bool valid_=true;
};
