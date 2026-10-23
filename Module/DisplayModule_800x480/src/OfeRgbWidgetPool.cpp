#include <lvgl.h>
#include "OfeRgbWidgetPool.h"

#if OFE_RGB_WIDGET_POOL_PSRAM
#include <esp_heap_caps.h>

// One boot-lifetime pool. Keep TLSF, block sizes and total widget capacity intact.
static void* widget_pool = nullptr;

int ofe_rgb_widget_pool_prepare(void) {
  if (!widget_pool) {
    widget_pool = heap_caps_aligned_alloc(64, LV_MEM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return widget_pool != nullptr;
}

void* ofe_rgb_widget_pool_alloc(size_t bytes) {
  return bytes == LV_MEM_SIZE ? widget_pool : nullptr;
}
#else
int ofe_rgb_widget_pool_prepare(void) { return 1; }
void* ofe_rgb_widget_pool_alloc(size_t bytes) {
  (void)bytes;
  return nullptr;
}
#endif
