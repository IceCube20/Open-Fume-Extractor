#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cassert>
constexpr uint32_t MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
namespace fake_heap {
inline size_t free_bytes=0, largest=0, overhead=0, charged=0;
inline unsigned allocations=0, releases=0;
inline void reset(size_t free, size_t block, size_t cost=0) {
  assert(!charged);
  free_bytes=free; largest=block; overhead=cost; allocations=releases=0;
}
}
inline size_t heap_caps_get_free_size(uint32_t) { return fake_heap::free_bytes; }
inline void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t caps) {
  assert(alignment==64 && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if (bytes>fake_heap::largest || bytes+fake_heap::overhead>fake_heap::free_bytes) return nullptr;
  void* p=std::malloc(bytes);
  if (p) {
    fake_heap::charged=bytes+fake_heap::overhead;
    fake_heap::free_bytes-=fake_heap::charged;
    ++fake_heap::allocations;
  }
  return p;
}
inline void heap_caps_free(void* p) {
  assert(p && fake_heap::charged);
  std::free(p); fake_heap::free_bytes+=fake_heap::charged; fake_heap::charged=0;
  ++fake_heap::releases;
}
