#pragma once
#include <cstdlib>
static constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_8BIT = 4;
inline void* heap_caps_calloc(size_t count, size_t bytes, unsigned) { return std::calloc(count, bytes); }
