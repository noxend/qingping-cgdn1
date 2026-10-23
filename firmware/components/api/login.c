#include "handlers.h"

#include <string.h>
#include "cJSON.h"
#include "auth.h"
#include "creds.h"
#include "pass_queue.h"
#include "session.h"
#include "util.h"

#define LOGIN_BODY_MAX 160

// The answer to a sign-in once its password is checked: a session cookie for the
// right one.
static esp_err_t login_answer(httpd_req_t *req, creds_check_t result, const char *body) {
  if (!auth_answer(req, result)) {
    return ESP_FAIL;
  }
  char token[SESSION_TOKEN_BUF];
  if (!session_create(token)) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not sign in, try again");
  }
  auth_set_cookie(req, token);
  httpd_resp_set_type(req, HTTPD_TYPE_JSON);
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// POST /login {"password"}: the config password once, a session cookie back. A wrong
// password makes this client wait a second before the next try (creds.c). A password
// only PBKDF2 can tell waits its turn on the hash task (pass_queue.c).
esp_err_t api_login_post(httpd_req_t *req) {
  if (!auth_origin_ok(req)) {
    return ESP_FAIL;
  }
  char body[LOGIN_BODY_MAX + 1];
  if (req->content_len <= 0 || req->content_len > LOGIN_BODY_MAX ||
      !recv_exact(req, body, req->content_len)) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need {\"password\"}");
  }
  body[req->content_len] = '\0';
  cJSON *root = cJSON_Parse(body);
  const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(root, "password"));
  uint32_t client = auth_client(req);
  creds_check_t result = pass ? creds_config_try(pass, client) : CREDS_WRONG;
  esp_err_t err = result == CREDS_NEEDS_HASH ? pass_queue_add(req, pass, client, login_answer, NULL)
                                             : login_answer(req, result, NULL);
  cJSON_Delete(root);
  return err;
}

// POST /logout: forgets this browser's session and clears its cookie.
esp_err_t api_logout_post(httpd_req_t *req) {
  if (!auth_origin_ok(req)) {
    return ESP_FAIL;
  }
  char token[SESSION_TOKEN_BUF];
  auth_session_token(req, token);
  if (token[0]) {
    session_remove(token);
  }
  auth_set_cookie(req, NULL);
  httpd_resp_set_type(req, HTTPD_TYPE_JSON);
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}
