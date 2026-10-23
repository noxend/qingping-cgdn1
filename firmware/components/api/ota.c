#include "handlers.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "fw_update.h"
#include "net.h"
#include "util.h"

static const char *TAG = "cgdn1";

// Writes the uploaded image to the spare slot and reboots into it; returns only on
// a failure, which it has already answered.
static esp_err_t receive_image(httpd_req_t *req) {
  // Sign-in travels over plaintext HTTP and gates who may install. What keeps
  // a foreign image out is the ECDSA signature esp_ota_end checks
  // (CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT); the project_name check below
  // is a second, cheaper filter.

  const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
  if (target == NULL) {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota slot");
    return ESP_FAIL;
  }
  if (req->content_len <= 0) {
    http_error(req, HTTPD_411_LENGTH_REQUIRED, "empty upload");
    return ESP_FAIL;
  }
  if ((size_t) req->content_len > target->size) {
    http_error(req, HTTPD_413_CONTENT_TOO_LARGE, "image larger than slot");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "OTA -> %s (%ld bytes)", target->label, (long) req->content_len);

  char *buf = malloc(4096);
  if (buf == NULL) {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    return ESP_FAIL;
  }
  // Passing the size erases only the sectors the image needs.
  esp_ota_handle_t ha = 0;
  esp_err_t err = esp_ota_begin(target, req->content_len, &ha);
  if (err != ESP_OK) {
    free(buf);
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    return ESP_FAIL;
  }

  int remaining = req->content_len;
  int timeouts = 0;
  while (remaining > 0) {
    int r = httpd_req_recv(req, buf, remaining > 4096 ? 4096 : remaining);
    if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < RECV_TIMEOUT_LIMIT) {
      continue;
    }
    if (r <= 0 || esp_ota_write(ha, buf, r) != ESP_OK) {
      free(buf);
      esp_ota_abort(ha);
      http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota recv/write failed");
      return ESP_FAIL;
    }
    timeouts = 0;
    remaining -= r;
  }
  free(buf);

  err = esp_ota_end(ha);
  if (err != ESP_OK) {
    http_error(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    return ESP_FAIL;
  }
  // Only images of this project: a foreign app would not carry this OTA
  // endpoint or the rollback gate.
  esp_app_desc_t desc;
  const esp_app_desc_t *self = esp_app_get_description();
  if (esp_ota_get_partition_description(target, &desc) != ESP_OK ||
      strncmp(desc.project_name, self->project_name, sizeof(desc.project_name)) != 0) {
    http_error(req, HTTPD_400_BAD_REQUEST, "not a cgdn1 firmware image");
    return ESP_FAIL;
  }
  err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "OTA ok: %s %s, rebooting into %s", desc.project_name, desc.version, target->label);
  httpd_resp_sendstr(req, "OK, rebooting. The new firmware confirms itself after 60 s of "
                          "being healthy; if you updated over the setup AP, open "
                          "http://" NET_AP_IP "/ within 3 minutes.\n");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

esp_err_t api_ota_post(httpd_req_t *req) {
  if (!fw_update_hold_slot()) {
    http_error(req, HTTPD_400_BAD_REQUEST, "an update is already running");
    return ESP_FAIL;
  }
  esp_err_t err = receive_image(req);
  fw_update_release_slot();
  return err;
}
