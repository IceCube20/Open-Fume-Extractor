#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Prepare before lv_init(): LVGL's built-in allocator cannot handle a null pool.
int ofe_rgb_widget_pool_prepare(void);
void* ofe_rgb_widget_pool_alloc(size_t bytes);

#ifdef __cplusplus
}
#endif
