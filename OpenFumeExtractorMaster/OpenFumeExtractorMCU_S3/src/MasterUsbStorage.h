#pragma once
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino.h>
#include <WebServer.h>

namespace ofe_usb {
void begin();
bool detected();
bool mounted();
String statusJson(bool developerMode = false);
bool command(const String& action, uint32_t generation = 0, unsigned index = 0);
uint32_t receiveBegin(const char* name, size_t bytes);
bool receiveChunk(uint32_t token, size_t offset, const uint8_t* data, size_t bytes);
bool receiveCommit(uint32_t token);
bool release(uint32_t token);
bool sendFile(WebServer& server, uint32_t token);
}  // namespace ofe_usb
#endif
