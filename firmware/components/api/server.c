#include "api.h"
#include "handlers.h"

#include "esp_log.h"
#include "pass_queue.h"
#include "screen.h"
#include "sensors_http.h"
#include "test_hooks.h"
#include "util.h"
#include "wasm_host.h"

static const char *TAG = "cgdn1";
static bool s_http_up;
static bool s_test_hooks;

bool api_start(void) {
  if (s_http_up) {
    return true;
  }
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  // The deepest handler is GET /status (its 1400-byte answer and the manifest copy on
  // the stack): 924 bytes were left of 6144 on 0.30.1, so 8192 keeps about 3 KB spare.
  cfg.stack_size = 8192;
  // 25 registered, 29 with the test hooks; httpd_register_uri_handler fails past this.
  cfg.max_uri_handlers = 32;
  // Browsers get 7 connections (plus the server's 3); past that the oldest is closed
  // (lru_purge), so they cannot take the sockets the device needs for its own requests.
  cfg.max_open_sockets = 7;
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 10;
  cfg.lru_purge_enable = true;
  cfg.keep_alive_enable = true;
  cfg.keep_alive_idle = 5;
  cfg.keep_alive_interval = 5;
  cfg.keep_alive_count = 3;
  pass_queue_start();
  httpd_handle_t srv = NULL;
  if (httpd_start(&srv, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd start failed");
    return false;
  }
  // Open: the page, sign-in, and the first setup (/scan and /save check for
  // themselves: a session, or no config password yet).
  const httpd_uri_t open[] = {
      {.uri = "/", .method = HTTP_GET, .handler = api_app_get},
      {.uri = "/app", .method = HTTP_GET, .handler = api_embedded_app_get},
      {.uri = "/shell.js", .method = HTTP_GET, .handler = api_shell_js_get},
      {.uri = "/fallback", .method = HTTP_GET, .handler = api_fallback_get},
      {.uri = "/status", .method = HTTP_GET, .handler = api_status_get},
      {.uri = "/login", .method = HTTP_POST, .handler = api_login_post},
      {.uri = "/logout", .method = HTTP_POST, .handler = api_logout_post},
      {.uri = "/save", .method = HTTP_POST, .handler = api_save_post},
      {.uri = "/scan", .method = HTTP_POST, .handler = api_scan_post},
  };
  const httpd_uri_t signed_in[] = {
      {.uri = "/ota", .method = HTTP_POST, .handler = api_ota_post},
      {.uri = "/update", .method = HTTP_POST, .handler = api_update_post},
      {.uri = "/update/auto", .method = HTTP_POST, .handler = api_update_auto_post},
      {.uri = "/update/check", .method = HTTP_POST, .handler = api_update_check_post},
      {.uri = "/reboot", .method = HTTP_POST, .handler = api_reboot_post},
      {.uri = "/reset", .method = HTTP_POST, .handler = api_reset_post},
  };
  bool ok = true;
  for (size_t i = 0; ok && i < sizeof(open) / sizeof(open[0]); i++) {
    ok = http_register_open(srv, &open[i]) == ESP_OK;
  }
  for (size_t i = 0; ok && i < sizeof(signed_in) / sizeof(signed_in[0]); i++) {
    ok = http_register(srv, &signed_in[i]) == ESP_OK;
  }
  if (!ok) {
    ESP_LOGE(TAG, "register endpoints failed");
    httpd_stop(srv);
    return false;
  }
  httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, http_not_found);
  if (screen_http_register(srv) != ESP_OK || wasm_host_http_register(srv) != ESP_OK ||
      sensors_http_register(srv) != ESP_OK) {
    ESP_LOGE(TAG, "register module endpoints failed");
    httpd_stop(srv);
    return false;
  }
  esp_err_t hooks = api_test_hooks_register(srv);
  if (hooks != ESP_OK && hooks != ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGE(TAG, "register test endpoints failed");
    httpd_stop(srv);
    return false;
  }
  s_test_hooks = hooks == ESP_OK;
  if (s_test_hooks) {
    ESP_LOGW(TAG, "test endpoints on: not a release image");
  }
  s_http_up = true;
  ESP_LOGI(TAG, "http up: GET / (config+status), POST /save, POST /ota");
  return true;
}

bool api_is_up(void) {
  return s_http_up;
}

bool api_test_hooks_on(void) {
  return s_test_hooks;
}
