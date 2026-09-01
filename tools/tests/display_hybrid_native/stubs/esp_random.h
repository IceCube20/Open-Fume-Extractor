#pragma once
inline void esp_fill_random(void* p,size_t n) { static uint8_t x=1; auto* b=(uint8_t*)p; while(n--) *b++=++x; }
