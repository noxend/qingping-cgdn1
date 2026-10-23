// Host stand-in for ESP-IDF's esp_err.h: plugin_sdk.h needs the type only.
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
