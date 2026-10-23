#include "handlers.h"

#include <string.h>
#include "fw_manifest.h"
#include "fw_update.h"
#include "util.h"

esp_err_t api_update_post(httpd_req_t *req) {
  esp_err_t e = fw_update_start();
  if (e == ESP_ERR_NOT_FOUND) {
    http_error(req, HTTPD_404_NOT_FOUND, "no update offered");
    return ESP_FAIL;
  }
  if (e == ESP_ERR_INVALID_STATE) {
    http_error(req, HTTPD_400_BAD_REQUEST, "an update is already running");
    return ESP_FAIL;
  }
  if (e != ESP_OK) {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "network worker busy");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t api_update_check_post(httpd_req_t *req) {
  if (!fw_manifest_check_now()) {
    http_error(req, HTTP_409_CONFLICT, "the device is not online");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"checking\":true}");
}

// Body "on=1" or "on=0".
esp_err_t api_update_auto_post(httpd_req_t *req) {
  char body[8] = "";
  if (req->content_len <= 0 || req->content_len >= (int) sizeof(body) ||
      !recv_exact(req, body, req->content_len)) {
    http_error(req, HTTPD_400_BAD_REQUEST, "expected on=1 or on=0");
    return ESP_FAIL;
  }
  body[req->content_len] = '\0';
  bool on = strcmp(body, "on=1") == 0;
  if (!on && strcmp(body, "on=0") != 0) {
    http_error(req, HTTPD_400_BAD_REQUEST, "expected on=1 or on=0");
    return ESP_FAIL;
  }
  if (fw_update_set_auto(on) != ESP_OK) {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, on ? "{\"auto_update\":true}" : "{\"auto_update\":false}");
}
