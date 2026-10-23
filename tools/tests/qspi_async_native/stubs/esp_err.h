#pragma once
#include <cassert>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, ESP_ERR_NO_MEM = 2, ESP_ERR_NOT_SUPPORTED = 3;
#define ESP_ERROR_CHECK(call) do { assert((call) == ESP_OK); } while (0)
