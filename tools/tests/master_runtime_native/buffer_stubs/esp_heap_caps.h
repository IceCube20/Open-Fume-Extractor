#pragma once
#include <cstdlib>
#include <cstdint>
static constexpr uint32_t MALLOC_CAP_INTERNAL=1,MALLOC_CAP_SPIRAM=2,MALLOC_CAP_8BIT=4;
inline bool fail_all=false,fail_external=false;
inline unsigned heap_calls=0;
inline void* heap_caps_realloc(void* pointer,size_t bytes,uint32_t caps) {
  ++heap_calls;
  if(fail_all || (fail_external && (caps&MALLOC_CAP_SPIRAM)))return nullptr;
  return realloc(pointer,bytes);
}
