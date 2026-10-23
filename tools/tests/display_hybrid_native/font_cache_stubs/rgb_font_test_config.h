#pragma once
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#include <ofe_lv_conf.h>
// MSVC does not accept the installed LVGL's deprecated-include #warning.
// Keep the production settings; only omit this unused compatibility include.
#undef LV_ASSERT_HANDLER_INCLUDE
