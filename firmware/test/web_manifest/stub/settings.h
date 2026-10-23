#pragma once
#include <stddef.h>
#include "esp_err.h"
esp_err_t settings_blob_get(const char *key, void *out, size_t *len);
esp_err_t settings_blob_set(const char *key, const void *data, size_t len);
