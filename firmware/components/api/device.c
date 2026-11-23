#include "handlers.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "factory_reset.h"
#include "net.h"
#include "rollback.h"
#include "util.h"

static const char *TAG = "cgdn1";

// The answer goes out before the restart, which would otherwise cut it short.
static void answer_then_wait(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  vTaskDelay(pdMS_TO_TICKS(800));
}

// Until the image is confirmed a restart rolls the update back, as a power cut does.
static bool update_pending(void) {
  if (!rollback_pending()) {
    return false;
  }
  ESP_LOGW(TAG, "restart refused: the update is not confirmed yet");
  return true;
}

esp_err_t api_reboot_post(httpd_req_t *req) {
  if (update_pending()) {
    return http_error(req, HTTP_409_CONFLICT, UPDATE_PENDING_MSG);
  }
  ESP_LOGI(TAG, "restart from the web page");
  answer_then_wait(req);
  esp_restart();
  return ESP_OK;
}

esp_err_t api_reset_post(httpd_req_t *req) {
  if (update_pending()) {
    return http_error(req, HTTP_409_CONFLICT, UPDATE_PENDING_MSG);
  }
  answer_then_wait(req);
  factory_reset(net_forget_driver_creds);  // does not return
  return ESP_OK;
}
