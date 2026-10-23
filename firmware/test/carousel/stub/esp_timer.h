// Host stand-in: the harness owns the clock and calls the timer callbacks itself.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);
typedef struct {
  esp_timer_cb_t callback;
  void *arg;
  const char *name;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t t);
bool esp_timer_is_active(esp_timer_handle_t t);
int64_t esp_timer_get_time(void);
