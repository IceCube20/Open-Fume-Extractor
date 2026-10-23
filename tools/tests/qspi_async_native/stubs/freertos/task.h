#pragma once
#include "FreeRTOS.h"
using TaskFunction_t = void (*)(void*);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
