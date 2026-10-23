#pragma once
#include "FreeRTOS.h"
SemaphoreHandle_t xSemaphoreCreateMutex();
void vSemaphoreDelete(SemaphoreHandle_t);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
void xSemaphoreGive(SemaphoreHandle_t);
