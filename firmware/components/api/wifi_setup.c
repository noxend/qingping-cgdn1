#include "handlers.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "auth.h"
#include "creds.h"
#include "net.h"
#include "pass_queue.h"
#include "util.h"

#define FORM_BODY_MAX 511
_Static_assert(FORM_BODY_MAX <= PASS_QUEUE_BODY_MAX, "the form goes to the hash task whole");

static const char *TAG = "cgdn1";

static int hex_val(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = (char) (c | 0x20);
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

// In-place percent-decoding. Rejects malformed escapes and %00.
static bool url_decode(char *s) {
  char *o = s;
  for (char *p = s; *p; p++) {
    if (*p == '%') {
      int hi = hex_val(p[1]);
      int lo = hi < 0 ? -1 : hex_val(p[2]);
      if (lo < 0 || (hi == 0 && lo == 0)) {
        return false;
      }
      *o++ = (char) ((hi << 4) | lo);
      p += 2;
    } else if (*p == '+') {
      *o++ = ' ';
    } else {
      *o++ = *p;
    }
  }
  *o = '\0';
  return true;
}

// Read one urlencoded field, decode it, and require 0 < len <= max (or 0 when
// allow_empty). Values are never silently truncated.
static bool form_value(const char *body, const char *key, char *out, size_t max, bool allow_empty) {
  char raw[3 * NET_PASS_MAX + 1];
  esp_err_t err = httpd_query_key_value(body, key, raw, sizeof(raw));
  if (err == ESP_ERR_NOT_FOUND && allow_empty) {
    out[0] = '\0';
    return true;
  }
  if (err != ESP_OK || !url_decode(raw)) {
    return false;
  }
  size_t len = strlen(raw);
  if (len > max || (len == 0 && !allow_empty)) {
    return false;
  }
  memcpy(out, raw, len + 1);
  return true;
}

// A form post from a page of another origin carries that origin; the device's own
// pages send http://10.47.0.1 here, and a client without the header is not a browser.
static bool same_origin_or_none(httpd_req_t *req) {
  char origin[64] = {0};
  esp_err_t e = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
  return e == ESP_ERR_NOT_FOUND || (e == ESP_OK && strcmp(origin, "http://" NET_AP_IP) == 0);
}

// /scan and /save need sign-in, or the config password in the body's `token`, which
// the no-script page at /fallback sends. Before one is set (a new device, or after a
// factory reset) only a client of the setup AP gets in, which needs the AP password
// shown on the device, and only from the device's own page; /save then takes `token`
// as the config password to set.
// Calls next with CREDS_OK when the request may go on, or the password check's
// result for next to answer; answers itself for any other refusal. A token only
// PBKDF2 can tell waits its turn on the hash task, and next runs after it.
static esp_err_t config_authed(httpd_req_t *req, const char *body, pass_resume_t next) {
  if (!creds_config_is_set()) {
    if (!net_is_ap_client(httpd_req_to_sockfd(req))) {
      http_error(req, HTTPD_403_FORBIDDEN, "no config password yet: use the setup AP");
      return ESP_FAIL;
    }
    if (!same_origin_or_none(req)) {
      http_error(req, HTTPD_403_FORBIDDEN, "a page of another site cannot set up this device");
      return ESP_FAIL;
    }
    return next(req, CREDS_OK, body);
  }
  if (!auth_origin_ok(req)) {
    return ESP_FAIL;
  }
  if (auth_signed_in(req)) {
    return next(req, CREDS_OK, body);
  }
  char p[AUTH_PASS_BUF] = {0};
  if (!form_value(body, "token", p, sizeof(p) - 1, true)) {
    return next(req, CREDS_WRONG, body);
  }
  uint32_t client = auth_client(req);
  creds_check_t result = creds_config_try(p, client);
  if (result == CREDS_NEEDS_HASH) {
    return pass_queue_add(req, p, client, next, body);
  }
  return next(req, result, body);
}

// An empty form is a form: a signed-in page sends /scan nothing.
static bool recv_form(httpd_req_t *req, char *body, int cap) {
  if (req->content_len > cap - 1) {
    http_error(req, HTTPD_413_CONTENT_TOO_LARGE, "body too large");
    return false;
  }
  if (!recv_exact(req, body, req->content_len)) {
    return false;
  }
  body[req->content_len] = '\0';
  return true;
}

static esp_err_t scan_answer(httpd_req_t *req, creds_check_t result, const char *body) {
  if (!auth_answer(req, result)) {
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, net_scan_json());
  return ESP_OK;
}

// POST /scan {token} -> JSON {"nets":[...]} (cached). For the React app.
esp_err_t api_scan_post(httpd_req_t *req) {
  char body[FORM_BODY_MAX + 1];
  if (!recv_form(req, body, sizeof(body))) {
    return ESP_FAIL;
  }
  return config_authed(req, body, scan_answer);
}

static esp_err_t save_form(httpd_req_t *req, creds_check_t result, const char *body) {
  if (!auth_answer(req, result)) {
    return ESP_FAIL;
  }
  bool first = !creds_config_is_set();
  char ssid[NET_SSID_MAX + 1];
  if (!form_value(body, "ssid", ssid, NET_SSID_MAX, false) || ssid[0] == '\0') {
    http_error(req, HTTPD_400_BAD_REQUEST, "network name must be 1-32 bytes");
    return ESP_FAIL;
  }
  char pass[NET_PASS_MAX + 1];
  size_t plen = 0;
  if (!form_value(body, "pass", pass, NET_PASS_MAX, true) ||
      ((plen = strlen(pass)) > 0 && plen < 8)) {
    http_error(req, HTTPD_400_BAD_REQUEST,
               "password must be empty (open network), 8-63 chars, "
               "or a 64-digit hex PSK");
    return ESP_FAIL;
  }
  // 64 characters is only ever a raw PSK; the driver refuses anything else at that
  // length, which would stop the station on every boot once it is in flash.
  if (plen == 64 && strspn(pass, "0123456789abcdefABCDEF") != 64) {
    http_error(req, HTTPD_400_BAD_REQUEST, "a 64-char password must be hex (a PSK)");
    return ESP_FAIL;
  }
  if (first) {
    char cfg[NET_PASS_MAX + 1];
    if (!form_value(body, "token", cfg, NET_PASS_MAX, false) || !creds_pass_ok(cfg)) {
      http_error(req, HTTPD_400_BAD_REQUEST,
                 "config password must be 8-63 characters, letters, digits or symbols, "
                 "no spaces");
      return ESP_FAIL;
    }
    esp_err_t set = creds_config_set(cfg);
    if (set != ESP_OK) {
      ESP_LOGE(TAG, "storing the config password failed: %s", esp_err_to_name(set));
      http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save, try again");
      return ESP_FAIL;
    }
  }
  esp_err_t err = net_save_creds(ssid, pass);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "saving creds failed: %s", esp_err_to_name(err));
    // The config password of a first setup is stored by now: a retry must send it.
    const char *msg = first ? "Wi-Fi not saved, try again; the config password you typed is set"
                            : "could not save, try again";
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "saved creds for '%s', rebooting to connect", ssid);
  httpd_resp_sendstr(req, "saved, rebooting\n");
  vTaskDelay(pdMS_TO_TICKS(800));
  esp_restart();
  return ESP_OK;
}

esp_err_t api_save_post(httpd_req_t *req) {
  char body[FORM_BODY_MAX + 1];
  if (!recv_form(req, body, sizeof(body))) {
    return ESP_FAIL;
  }
  return config_authed(req, body, save_form);
}
