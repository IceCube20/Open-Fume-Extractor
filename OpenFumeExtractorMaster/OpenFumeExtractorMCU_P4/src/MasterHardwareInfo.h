#pragma once
#include <stdint.h>

struct MasterHardwareInfo {
  const char* model="-";
  const char* flash_mode="-";
  const char* arduino="-";
  const char* idf="-";
  uint16_t revision=0;
  uint8_t cores=0;
  uint32_t cpu_mhz=0,flash_bytes=0,flash_hz=0,psram_bytes=0,psram_mhz=0;
  uint32_t c6_current=0,c6_host=0;
  bool p4=false;
};
