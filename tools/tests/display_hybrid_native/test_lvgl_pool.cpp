#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/stdlib/builtin/lv_tlsf.h"
#include "src/lv_conf_internal.h"

// Only the allocator is linked; the UI is not needed to reproduce the boot failure.
extern "C" void* lv_memcpy(void* dst, const void* src, size_t len) {
  return std::memcpy(dst, src, len);
}

int main() {
  static_assert(LV_MEM_SIZE == 64U * 1024U, "Use the display's actual configuration");
  alignas(64) static unsigned char internal[LV_MEM_SIZE];
  alignas(64) static unsigned char external[2][LV_MEM_SIZE];
  alignas(64) static unsigned char oversized[128U * 1024U];
  lv_tlsf_t tlsf = lv_tlsf_create_with_pool(internal, sizeof(internal));
  assert(tlsf);
  assert(lv_tlsf_block_size_max() == LV_MEM_SIZE);
  assert(!lv_tlsf_add_pool(tlsf, oversized, sizeof(oversized)));
  for (auto& pool : external) assert(lv_tlsf_add_pool(tlsf, pool, sizeof(pool)));
  std::vector<void*> allocations;
  while (void* p = lv_tlsf_malloc(tlsf, 256)) {
    std::memset(p, 0x5a, 256);
    allocations.push_back(p);
  }
  assert(allocations.size() * 256 > 160U * 1024U);
  for (void* p : allocations) lv_tlsf_free(tlsf, p);
  assert(lv_tlsf_check(tlsf) == 0);
  for (auto& pool : external) assert(lv_tlsf_check_pool(pool) == 0);
  lv_tlsf_destroy(tlsf);
  puts("PASS: installed LVGL rejects the old 128-KiB pool; two 64-KiB pools provide capacity without corrupting TLSF.");
}
