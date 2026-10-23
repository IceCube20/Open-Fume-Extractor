#pragma once
#include "ofe_reference_base.h"

// Reference-only overrides. The application config is not changed.
#undef LV_MEM_SIZE
#define LV_MEM_SIZE (48U * 1024U)
#undef LV_FONT_MONTSERRAT_20
#define LV_FONT_MONTSERRAT_20 1
#undef LV_FONT_MONTSERRAT_24
#define LV_FONT_MONTSERRAT_24 1
#undef LV_FONT_MONTSERRAT_26
#define LV_FONT_MONTSERRAT_26 1
#undef LV_USE_DEMO_BENCHMARK
#define LV_USE_DEMO_BENCHMARK 1
#undef LV_USE_DEMO_WIDGETS
#define LV_USE_DEMO_WIDGETS 1
#undef LV_USE_SYSMON
#define LV_USE_SYSMON 1
#undef LV_USE_PERF_MONITOR
#define LV_USE_PERF_MONITOR 1
