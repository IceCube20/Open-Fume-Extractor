#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
using ppa_client_handle_t = void*;
struct ppa_fill_oper_config_t { int marker; };
struct ppa_blend_oper_config_t { int marker; };
