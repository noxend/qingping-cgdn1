// GET /sensors: the readings as JSON, for the web page and for checking a sensor
// without a screen.
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t sensors_http_register(httpd_handle_t srv);
