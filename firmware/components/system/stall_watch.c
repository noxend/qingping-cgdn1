#include "stall_watch.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define PERIOD_MS 20
#define LATE_MS 200
#define VERY_LATE_MS 1000

static volatile int s_max_ms;
static volatile int s_max_at_s;
static volatile int s_n_late;
static volatile int s_n_very_late;

static void watcher(void *arg) {
  int64_t last = esp_timer_get_time();
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    int64_t now = esp_timer_get_time();
    int late = (int) ((now - last) / 1000) - PERIOD_MS;
    last = now;
    if (late > s_max_ms) {
      s_max_ms = late;
      s_max_at_s = (int) (now / 1000000);
    }
    if (late >= LATE_MS) {
      s_n_late++;
    }
    if (late >= VERY_LATE_MS) {
      s_n_very_late++;
    }
  }
}

void stall_watch_start(void) {
  xTaskCreate(watcher, "stall", 2048, NULL, configMAX_PRIORITIES - 2, NULL);
}

int stall_watch_json(char *out, int cap) {
  return snprintf(out, cap, "{\"max_ms\":%d,\"at_s\":%d,\"n200\":%d,\"n1000\":%d}", s_max_ms,
                  s_max_at_s, s_n_late, s_n_very_late);
}
