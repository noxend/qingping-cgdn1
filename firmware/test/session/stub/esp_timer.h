#pragma once
#include <stdint.h>
// The harness moves this clock (harness.c).
int64_t esp_timer_get_time(void);
