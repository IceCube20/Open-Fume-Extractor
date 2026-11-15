#pragma once
// Forced into every C/C++ translation unit by the bundled build_opt.h.
// sdkconfig.h must be loaded first: Arduino SDK defaults otherwise override
// command-line defines, and NimBLE checks INTERNAL with #ifdef, not #if.
#include <sdkconfig.h>

#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_IRAM_8BIT
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_DEFAULT
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1

#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
#undef CONFIG_NIMBLE_MAX_CONNECTIONS
#define CONFIG_NIMBLE_MAX_CONNECTIONS 1
