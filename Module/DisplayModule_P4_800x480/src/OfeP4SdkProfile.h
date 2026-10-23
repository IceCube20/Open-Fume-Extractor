#pragma once
#include <Arduino.h>
#include <sdkconfig.h>

#if defined(OFE_REQUIRE_P4_CACHE256_SDK)
#if !defined(CONFIG_CACHE_L2_CACHE_SIZE) || CONFIG_CACHE_L2_CACHE_SIZE != 0x40000 || \
    !defined(CONFIG_CACHE_L2_CACHE_LINE_SIZE) || CONFIG_CACHE_L2_CACHE_LINE_SIZE != 128
#error "OFE P4 High Performance requires the complete 256 KiB / 128-byte cache SDK"
#endif
#endif

// Read the prebuilt SDK configuration; sketch flags do not rebuild its caches.
static inline void ofe_p4_log_sdk_cache_profile() {
#if defined(CONFIG_CACHE_L2_CACHE_SIZE) && defined(CONFIG_CACHE_L2_CACHE_LINE_SIZE)
    Serial.printf("[P4 SDK] L2 cache: %u KiB / %u-byte lines\n",
                  (unsigned)(CONFIG_CACHE_L2_CACHE_SIZE / 1024U),
                  (unsigned)CONFIG_CACHE_L2_CACHE_LINE_SIZE);
#else
    Serial.println("[P4 SDK] L2 cache configuration unavailable");
#endif
}
