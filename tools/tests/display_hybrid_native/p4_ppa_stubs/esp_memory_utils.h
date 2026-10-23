#pragma once
#include <stdint.h>
inline bool esp_ptr_internal(const void* p) {
    const auto address = reinterpret_cast<uintptr_t>(p);
    return address >= 0x1000 && address < 0x2000;
}
inline bool esp_ptr_external_ram(const void* p) {
    const auto address = reinterpret_cast<uintptr_t>(p);
    return address >= 0x3000 && address < 0x4000;
}
