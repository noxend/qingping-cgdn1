#pragma once
// A binary semaphore between the harness's threads (harness.c).
#include "freertos/FreeRTOS.h"
typedef struct sem *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
