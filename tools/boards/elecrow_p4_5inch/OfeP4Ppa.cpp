#include "OfeP4Ppa.h"

#if OFE_P4_PPA
#include <Arduino.h>
#include <atomic>
#include <driver/ppa.h>
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>

extern "C" size_t ofe_p4_ppa_cache_line_size(const void* address) {
    uint32_t caps;
    if (esp_ptr_external_ram(address)) caps = MALLOC_CAP_SPIRAM;
    else if (esp_ptr_internal(address)) caps = MALLOC_CAP_INTERNAL;
    else return 0;
    size_t alignment = 0;
    // Keep internal and PSRAM cache levels distinct; uncached RAM returns zero.
    if (esp_cache_get_alignment(caps, &alignment) != ESP_OK) return OFE_P4_DRAW_ALIGNMENT;
    return alignment;
}
#endif

namespace {
std::atomic<uint32_t> fills{0};
std::atomic<uint32_t> errors{0};
std::atomic<uint32_t> max_us{0};
std::atomic<uint32_t> blends{0};
std::atomic<uint32_t> blend_errors{0};
std::atomic<uint32_t> blend_max_us{0};

void observe(std::atomic<uint32_t>& calls, std::atomic<uint32_t>& failed,
             std::atomic<uint32_t>& longest, esp_err_t result, uint32_t elapsed) {
    calls.fetch_add(1, std::memory_order_relaxed);
    if (result != ESP_OK) failed.fetch_add(1, std::memory_order_relaxed);
    uint32_t previous = longest.load(std::memory_order_relaxed);
    while (elapsed > previous && !longest.compare_exchange_weak(
        previous, elapsed, std::memory_order_relaxed)) {}
}
}

extern "C" esp_err_t __real_ppa_do_fill(ppa_client_handle_t client,
                                      const ppa_fill_oper_config_t* config);

// Observe the existing LVGL/IDF operation without changing its pixels or lifetime.
extern "C" esp_err_t __wrap_ppa_do_fill(ppa_client_handle_t client,
                                      const ppa_fill_oper_config_t* config) {
    const uint32_t started = micros();
    const esp_err_t result = __real_ppa_do_fill(client, config);
    const uint32_t elapsed = (uint32_t)(micros() - started);
    observe(fills, errors, max_us, result, elapsed);
    return result;
}
#if OFE_P4_PPA_IMG
extern "C" esp_err_t __real_ppa_do_blend(ppa_client_handle_t client,
                                       const ppa_blend_oper_config_t* config);

extern "C" esp_err_t __wrap_ppa_do_blend(ppa_client_handle_t client,
                                       const ppa_blend_oper_config_t* config) {
    const uint32_t started = micros();
    const esp_err_t result = __real_ppa_do_blend(client, config);
    observe(blends, blend_errors, blend_max_us, result, (uint32_t)(micros() - started));
    return result;
}
#endif
#endif

ofe_p4_ppa::Stats ofe_p4_ppa::snapshot() {
#if OFE_P4_PPA
    return {fills.load(std::memory_order_relaxed), errors.load(std::memory_order_relaxed),
            max_us.load(std::memory_order_relaxed), blends.load(std::memory_order_relaxed),
            blend_errors.load(std::memory_order_relaxed), blend_max_us.load(std::memory_order_relaxed)};
#else
    return {0, 0, 0, 0, 0, 0};
#endif
}
