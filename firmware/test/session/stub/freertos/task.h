#pragma once
// The harness runs a task as a thread of its own (harness.c).
#include "freertos/FreeRTOS.h"
static inline void vTaskDelay(int ticks) {
  (void) ticks;
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                       void *handle);
