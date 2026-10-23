#pragma once
#include <stdio.h>
#define ESP_LOGI(t, fmt, ...)                                                                      \
  do {                                                                                             \
    if (0) {                                                                                       \
      printf("%s" fmt, t, ##__VA_ARGS__);                                                          \
    }                                                                                              \
  } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
