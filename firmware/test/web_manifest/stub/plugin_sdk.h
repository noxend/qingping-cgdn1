#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
typedef void *(*core_net_parse_fn)(const char *body, int len);
typedef struct {
  int status;
  esp_err_t err;
  void *parsed;
  char *body;
  int len;
} core_net_result_t;
