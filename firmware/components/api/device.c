#include "handlers.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "factory_reset.h"
#include "net.h"

static const char *TAG = "cgdn1";

// The answer goes out before the restart, which would otherwise cut it short.
static void answer_then_wait(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  vTaskDelay(pdMS_TO_TICKS(800));
}

esp_err_t api_reboot_post(httpd_req_t *req) {
  ESP_LOGI(TAG, "restart from the web page");
  answer_then_wait(req);
  esp_restart();
  return ESP_OK;
}

esp_err_t api_reset_post(httpd_req_t *req) {
  answer_then_wait(req);
  factory_reset(net_forget_driver_creds);  // does not return
  return ESP_OK;
}
