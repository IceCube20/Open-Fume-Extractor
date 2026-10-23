#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_DMA = 2, MALLOC_CAP_8BIT = 4, MALLOC_CAP_SPIRAM = 8;
void* heap_caps_aligned_alloc(size_t, size_t, uint32_t);
void heap_caps_free(void*);
size_t heap_caps_get_free_size(uint32_t);
