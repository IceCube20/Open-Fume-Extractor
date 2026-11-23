#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Multipart upload boundaries are not image-header boundaries.
class MasterFirmwareHeader {
public:
  explicit MasterFirmwareHeader(uint16_t chip,uint16_t revision=0xFFFF) : chip_(chip),revision_(revision) {}
  void reset(uint16_t revision=0xFFFF) { revision_=revision; used_=0; valid_=true; memset(bytes_,0,sizeof(bytes_)); }
  bool append(const uint8_t* data,size_t len) {
    if (!valid_ || (!data && len)) return valid_=false;
    const size_t remaining=sizeof(bytes_)-used_;
    const size_t count=len<remaining ? len : remaining;
    if (count) { memcpy(bytes_+used_,data,count); used_+=count; }
    if (used_==sizeof(bytes_)) valid_=bytes_[0]==0xE9 && bytes_[1]>0 &&
      bytes_[1]<=16 && bytes_[2]<=3 && uint16_t(bytes_[12]|uint16_t(bytes_[13])<<8)==chip_;
    if (valid_ && used_==sizeof(bytes_) && revision_!=0xFFFF) {
      // ESP image header: full revisions are major * 100 + minor, little-endian.
      const uint16_t minimum=uint16_t(bytes_[15]|uint16_t(bytes_[16])<<8);
      const uint16_t maximum=uint16_t(bytes_[17]|uint16_t(bytes_[18])<<8);
      const uint16_t legacy_minimum=uint16_t(bytes_[14])*100U;
      const uint16_t lower=minimum ? minimum : legacy_minimum;
      valid_=revision_>=lower && (!maximum || (lower<=maximum && revision_<=maximum));
      if (valid_ && chip_==18) {
        // P4 SDK families are incompatible; an unbounded Arduino header is ambiguous.
        valid_=revision_<300 ? maximum && maximum<300 : minimum>=300;
      }
    }
    return valid_;
  }
  bool complete() const { return valid_ && used_==sizeof(bytes_); }
private:
  uint16_t chip_;
  uint16_t revision_;
  uint8_t bytes_[24]{};
  size_t used_=0;
  bool valid_=true;
};
