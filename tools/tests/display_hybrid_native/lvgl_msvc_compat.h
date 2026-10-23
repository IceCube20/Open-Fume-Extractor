#pragma once
// Keep the production allocator geometry; translate only legacy v9.6 names
// whose GCC #warning directives are not accepted by the MSVC test compiler.
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_DISABLE_ASSERT_HANDLER_INCLUDE_WARNING 1
#include "ofe_lv_conf.h"
#if defined(LV_MEM_POOL_EXPAND_SIZE) && LV_MEM_POOL_EXPAND_SIZE == 0
#undef LV_MEM_POOL_EXPAND_SIZE
#endif
