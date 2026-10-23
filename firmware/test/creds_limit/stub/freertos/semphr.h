// Host stand-in: one task, so a lock only has to be counted.
#pragma once
#include <stdint.h>
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t s, uint32_t ticks);
int xSemaphoreGive(SemaphoreHandle_t s);
