#pragma once
#include <assert.h>
#include <stdint.h>
#include <malloc.h>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
namespace pool_heap {
inline bool fail = true;
inline unsigned calls = 0;
inline void* allocation = nullptr;
}
inline void* heap_caps_aligned_alloc(size_t align, size_t bytes, uint32_t caps) {
  assert(align == 64 && bytes == 32768 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++pool_heap::calls;
  if (pool_heap::fail) return nullptr;
  assert(!pool_heap::allocation);
  pool_heap::allocation = _aligned_malloc(bytes, align);
  assert(pool_heap::allocation && (reinterpret_cast<uintptr_t>(pool_heap::allocation) % 64) == 0);
  return pool_heap::allocation;
}
