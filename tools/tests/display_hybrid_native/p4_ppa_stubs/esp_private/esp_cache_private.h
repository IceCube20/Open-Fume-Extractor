#pragma once
#include <stddef.h>
#include <esp_heap_caps.h>
extern int cache_result;
extern size_t internal_alignment;
inline int esp_cache_get_alignment(uint32_t caps, size_t* alignment) {
    if (cache_result) return cache_result;
    *alignment = caps == MALLOC_CAP_SPIRAM ? 128 : internal_alignment;
    return 0;
}
