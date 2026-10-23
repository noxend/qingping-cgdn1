#pragma once
#include <stdio.h>
#define ESP_LOGI(t, ...) (printf("I %s: ", t), printf(__VA_ARGS__), printf("\n"))
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
