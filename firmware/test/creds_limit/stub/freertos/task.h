// Host stand-in: a delay moves the fake clock of esp_timer.h.
#pragma once
#include <stdint.h>
void vTaskDelay(uint32_t ticks);
