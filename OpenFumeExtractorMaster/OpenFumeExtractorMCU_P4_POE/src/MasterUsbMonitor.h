#pragma once
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino.h>
#include <esp_err.h>
namespace ofe_usb::monitor {
esp_err_t begin();
void hostResult(esp_err_t error);
void mscResult(esp_err_t error);
void eventError(esp_err_t error);
void appendJson(String& json);
}
#endif
