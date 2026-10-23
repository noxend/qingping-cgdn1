// The endpoints server.c registers, one file per area.
#pragma once

#include <stdbool.h>
#include "esp_http_server.h"

// pages.c
esp_err_t api_app_get(httpd_req_t *req);           // GET /
esp_err_t api_embedded_app_get(httpd_req_t *req);  // GET /app
esp_err_t api_shell_js_get(httpd_req_t *req);      // GET /shell.js
esp_err_t api_fallback_get(httpd_req_t *req);      // GET /fallback
// Tells net about the request, so a page opened over the setup AP counts for the gate.
void api_mark_ap_hit(httpd_req_t *req);

esp_err_t api_status_get(httpd_req_t *req);   // status.c, GET /status
esp_err_t api_login_post(httpd_req_t *req);   // login.c, POST /login
esp_err_t api_logout_post(httpd_req_t *req);  // login.c, POST /logout
esp_err_t api_scan_post(httpd_req_t *req);    // wifi_setup.c, POST /scan
esp_err_t api_save_post(httpd_req_t *req);    // wifi_setup.c, POST /save
esp_err_t api_ota_post(httpd_req_t *req);     // ota.c, POST /ota
// update.c: install the offered firmware, and the auto update option.
esp_err_t api_update_post(httpd_req_t *req);       // POST /update
esp_err_t api_update_auto_post(httpd_req_t *req);  // POST /update/auto, body on=1|0
// Fetches the release manifest now, at most every 30 s; 409 while offline.
esp_err_t api_update_check_post(httpd_req_t *req);  // POST /update/check

// server.c: whether a test build added its endpoints (test_hooks.h).
bool api_test_hooks_on(void);

// device.c: the System page's Restart and Factory reset, for a signed-in caller.
esp_err_t api_reboot_post(httpd_req_t *req);  // POST /reboot
esp_err_t api_reset_post(httpd_req_t *req);   // POST /reset
