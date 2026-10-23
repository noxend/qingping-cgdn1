#include "test_hooks.h"

__attribute__((weak)) esp_err_t api_test_hooks_register(httpd_handle_t srv) {
  (void) srv;
  return ESP_ERR_NOT_SUPPORTED;
}
