#include "system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "power_latch.h"
#include "settings.h"

#define TWDT_TIMEOUT_MS 60000

static const char *TAG = "cgdn1";

void system_nvs_init(void) {
  esp_err_t nv = nvs_flash_init();
  if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nv = nvs_flash_init();
  }
  if (nv != ESP_OK) {
    ESP_LOGE(TAG, "nvs init failed: %s (no saved WiFi this boot)", esp_err_to_name(nv));
  }
}

void system_task_watchdog_init(void) {
  const esp_task_wdt_config_t twdt = {
      .timeout_ms = TWDT_TIMEOUT_MS,
      .idle_core_mask = (1 << 0) | (1 << 1),
      .trigger_panic = true,
  };
  esp_err_t tw = esp_task_wdt_reconfigure(&twdt);
  if (tw != ESP_OK) {
    ESP_LOGE(TAG, "task watchdog: %s", esp_err_to_name(tw));
  }
}

#define OFF_KEY "power_off"
#define OFF_CAP 16

static char s_last_off[OFF_CAP];

bool system_power_off(const char *reason, bool (*held)(void)) {
  ESP_LOGW(TAG, "power off (%s): releasing the latch", reason);
  esp_err_t e = settings_blob_set(OFF_KEY, reason, strnlen(reason, OFF_CAP - 1) + 1);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "power off reason not saved: %s", esp_err_to_name(e));
  }
  vTaskDelay(pdMS_TO_TICKS(100));  // the log line first
  power_latch_release();
  for (int ms = 0; held() && ms < POWER_OFF_HELD_MS; ms += 50) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  vTaskDelay(pdMS_TO_TICKS(POWER_OFF_GRACE_MS));
  ESP_LOGW(TAG, "still powered after %d ms", POWER_OFF_GRACE_MS);
  return false;
}

void system_power_on(void) {
  settings_blob_set(OFF_KEY, "", 1);
  esp_restart();
}

void system_last_off_load(void) {
  size_t len = sizeof(s_last_off);
  if (settings_blob_get(OFF_KEY, s_last_off, &len) != ESP_OK) {
    s_last_off[0] = '\0';
    return;
  }
  s_last_off[sizeof(s_last_off) - 1] = '\0';
  if (s_last_off[0] != '\0') {
    ESP_LOGI(TAG, "last power off: %s", s_last_off);
    settings_blob_set(OFF_KEY, "", 1);
  }
}

const char *system_last_off(void) {
  return s_last_off;
}
