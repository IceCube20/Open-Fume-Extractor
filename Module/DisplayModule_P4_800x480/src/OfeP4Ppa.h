#pragma once
#include <stdint.h>
#include "OfeP4PpaConfig.h"

namespace ofe_p4_ppa {
struct Stats {
    uint32_t fills;
    uint32_t errors;
    uint32_t max_us;
    uint32_t blends;
    uint32_t blend_errors;
    uint32_t blend_max_us;
};
Stats snapshot();
}
