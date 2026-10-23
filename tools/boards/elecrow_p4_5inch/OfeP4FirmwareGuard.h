#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Additional local compatibility check. Authenticity is still verified by the
// Master's signed-package verifier before FW_END can commit the inactive slot.
class OfeP4FirmwareGuard {
public:
  void reset() { header_bytes_=0; prefix_=0; found_=false; invalid_=false; }
  bool append(const uint8_t* bytes,size_t size) {
    if (invalid_ || (!bytes && size)) return false;
    for (size_t i=0;i<size;++i) {
      const uint8_t c=bytes[i];
      if (header_bytes_<sizeof(header_)) {
        header_[header_bytes_++]=c;
        if (header_bytes_==sizeof(header_) &&
            (header_[0]!=0xE9 || !header_[1] || header_[1]>16 || header_[2]>3 ||
             header_[12]!=0x12 || header_[13]!=0)) { invalid_=true; return false; }
      }
      if (!found_) {
        if (c==expected(prefix_)) ++prefix_;
        else prefix_=c==expected(0) ? 1 : 0;
        if (prefix_==sizeof(marker_prefix_)+sizeof(marker_target_)-2) found_=true;
      }
    }
    return true;
  }
  bool complete() const { return !invalid_ && header_bytes_==sizeof(header_) && found_; }
private:
  // Keep a complete marker out of the validator's own rodata: image scanners
  // must find the deliberately embedded build signature, not this helper.
  inline static constexpr char marker_prefix_[]="OFE_FW_SIG:v1;";
  inline static constexpr char marker_target_[]="target=DISPLAY_P4_800X480;version=";
  static uint8_t expected(size_t index) {
    return uint8_t(index<sizeof(marker_prefix_)-1 ? marker_prefix_[index] : marker_target_[index-(sizeof(marker_prefix_)-1)]);
  }
  uint8_t header_[24]{};
  size_t header_bytes_=0,prefix_=0;
  bool found_=false,invalid_=false;
};
