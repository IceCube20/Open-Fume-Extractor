#pragma once
#include <assert.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_SPIRAM = 4;
namespace font_heap {
inline unsigned allocations = 0, releases = 0;
inline size_t live = 0;
inline bool fail = false;
}
inline void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  assert(!font_heap::live);
  ++font_heap::allocations;
  if (font_heap::fail) return nullptr;
  void* p = _aligned_malloc(bytes, alignment);
  if (p) font_heap::live = bytes;
  return p;
}
inline void heap_caps_free(void* p) {
  assert(p && font_heap::live);
  ++font_heap::releases;
  font_heap::live = 0;
  _aligned_free(p);
}
