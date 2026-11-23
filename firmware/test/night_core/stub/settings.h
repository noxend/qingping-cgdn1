#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
esp_err_t settings_blob_get(const char *key, void *out, size_t *len);
esp_err_t settings_blob_set(const char *key, const void *data, size_t len);
const char *esp_err_to_name(esp_err_t e);
