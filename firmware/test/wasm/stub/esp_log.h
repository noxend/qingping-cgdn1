#pragma once
#include <stdio.h>
// To stderr: the harnesses print their results on stdout.
#define ESP_LOGI(t, ...)                                                                           \
  (fprintf(stderr, "  I %s: ", t), fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n"))
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
