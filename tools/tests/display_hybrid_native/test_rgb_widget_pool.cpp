#include <lvgl.h>
#include <esp_heap_caps.h>
#include <assert.h>
#include <stdio.h>
#include "../../../Module/DisplayModule_800x480/src/OfeRgbWidgetPool.h"

int main() {
  assert(ofe_rgb_widget_pool_alloc(LV_MEM_SIZE) == nullptr);
  assert(pool_heap::calls == 0);
#if OFE_RGB_WIDGET_POOL_PSRAM
  assert(!ofe_rgb_widget_pool_prepare());
  assert(pool_heap::calls == 1 && !pool_heap::allocation);
  assert(ofe_rgb_widget_pool_alloc(LV_MEM_SIZE) == nullptr);
  pool_heap::fail = false;
  assert(ofe_rgb_widget_pool_prepare());
  assert(pool_heap::calls == 2);
  void* memory = ofe_rgb_widget_pool_alloc(LV_MEM_SIZE);
  assert(memory == pool_heap::allocation);
  assert(!ofe_rgb_widget_pool_alloc(0));
  assert(!ofe_rgb_widget_pool_alloc(LV_MEM_SIZE - 1));
  assert(!ofe_rgb_widget_pool_alloc(LV_MEM_SIZE + 1));
  assert(ofe_rgb_widget_pool_prepare());
  assert(ofe_rgb_widget_pool_alloc(LV_MEM_SIZE) == memory && pool_heap::calls == 2);
  _aligned_free(memory);
  puts("PASS: PSRAM-only aligned pool, allocation failure, retry, exact size and idempotent preparation");
#else
  assert(ofe_rgb_widget_pool_prepare());
  assert(pool_heap::calls == 0 && !pool_heap::allocation);
  assert(!ofe_rgb_widget_pool_alloc(LV_MEM_SIZE));
  puts("PASS: default/disabled profile performs no heap allocation and leaves LVGL's built-in pool intact");
#endif
}
