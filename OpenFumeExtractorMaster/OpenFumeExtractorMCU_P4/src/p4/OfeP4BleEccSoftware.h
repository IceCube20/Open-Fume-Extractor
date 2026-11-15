#pragma once
#include "OfeP4BleEccFix.h"

// Translation-unit-local policy for the vendored TinyCrypt implementation only.
// Keep its regularized software ladder for both public keys and shared secrets;
// the installed SDK and other hardware crypto users retain their real SoC caps.
#undef SOC_ECC_SUPPORTED
#define SOC_ECC_SUPPORTED 0
