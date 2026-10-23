#include "factory_reset.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_PARTITION "nvs_cfg"

static const char *TAG = "reset";

// The credentials live in one namespace of the default partition, beside nothing
// else of ours, so that namespace is erased rather than the partition: the PHY
// calibration and the OTA state stay where they are.
static void forget_wifi(void) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(FACTORY_WIFI_NS, NVS_READWRITE, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return;  // nothing was ever saved
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open " FACTORY_WIFI_NS ": %s", esp_err_to_name(err));
    return;
  }
  err = nvs_erase_all(h);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "erase " FACTORY_WIFI_NS ": %s", esp_err_to_name(err));
  }
}

void factory_reset(void (*forget_wifi_driver)(void)) {
  ESP_LOGW(TAG, "factory reset: settings and Wi-Fi erased, restarting");
  esp_err_t err = nvs_flash_erase_partition(SETTINGS_PARTITION);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "erase " SETTINGS_PARTITION ": %s", esp_err_to_name(err));
  }
  if (forget_wifi_driver) {
    forget_wifi_driver();
  }
  forget_wifi();
  vTaskDelay(pdMS_TO_TICKS(300));  // let the log line out before the restart
  esp_restart();
}
