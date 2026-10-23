#pragma once
#include "esp_err.h"
#define MDNS_NAME_BUF_LEN 64
esp_err_t mdns_hostname_get(char *hostname);
