#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include "auth.h"
#include "pass_queue.h"

static const char *status_line(httpd_err_code_t code) {
  switch ((int) code) {
  case HTTPD_400_BAD_REQUEST:
    return "400 Bad Request";
  case HTTPD_401_UNAUTHORIZED:
    return "401 Unauthorized";
  case HTTPD_403_FORBIDDEN:
    return "403 Forbidden";
  case HTTPD_404_NOT_FOUND:
    return "404 Not Found";
  case HTTPD_408_REQ_TIMEOUT:
    return "408 Request Timeout";
  case HTTPD_411_LENGTH_REQUIRED:
    return "411 Length Required";
  case HTTPD_413_CONTENT_TOO_LARGE:
    return "413 Content Too Large";
  case HTTP_429_TOO_MANY:
    return "429 Too Many Requests";
  case HTTP_409_CONFLICT:
    return "409 Conflict";
  case HTTP_503_BUSY:
    return "503 Service Unavailable";
  default:
    return "500 Internal Server Error";
  }
}

esp_err_t http_error(httpd_req_t *req, httpd_err_code_t code, const char *msg) {
  char body[160];
  int n = 0;
  bufappend(body, &n, sizeof(body), "{\"error\":\"");
  for (const char *p = msg ? msg : ""; *p && n < (int) sizeof(body) - 4; p++) {
    if (*p == '"' || *p == '\\') {
      body[n++] = '\\';
    }
    body[n++] = (unsigned char) *p < 0x20 ? ' ' : *p;
  }
  body[n] = '\0';
  bufappend(body, &n, sizeof(body), "\"}");
  httpd_resp_set_status(req, status_line(code));
  httpd_resp_set_type(req, HTTPD_TYPE_JSON);
  return httpd_resp_send(req, body, n);
}

static esp_err_t open_guarded(httpd_req_t *req);
static esp_err_t guarded(httpd_req_t *req);

// After the hash task checked the Bearer password: the wrapper again, which reads the
// result through pass_queue_result instead of queuing.
static esp_err_t open_guarded_again(httpd_req_t *req, creds_check_t result, const char *body) {
  return open_guarded(req);
}

static esp_err_t guarded_again(httpd_req_t *req, creds_check_t result, const char *body) {
  return guarded(req);
}

static esp_err_t open_guarded(httpd_req_t *req) {
  if (!auth_host_ok(req)) {
    return ESP_FAIL;
  }
  char pass[AUTH_PASS_BUF];
  if (auth_bearer_needs_hash(req, pass)) {
    return pass_queue_add(req, pass, auth_client(req), open_guarded_again, NULL);
  }
  esp_err_t (*handler)(httpd_req_t *) = req->user_ctx;
  return handler(req);
}

esp_err_t http_not_found(httpd_req_t *req, httpd_err_code_t err) {
  if (auth_captive_redirect(req)) {
    return ESP_OK;
  }
  httpd_resp_send_err(req, err, NULL);
  return ESP_FAIL;  // as without a handler: the server closes the connection
}

static esp_err_t guarded(httpd_req_t *req) {
  // The Origin first, as auth_request does: a page of another site costs no hash.
  if (!auth_host_ok(req) || !auth_origin_ok(req)) {
    return ESP_FAIL;
  }
  char pass[AUTH_PASS_BUF];
  if (auth_bearer_needs_hash(req, pass)) {
    return pass_queue_add(req, pass, auth_client(req), guarded_again, NULL);
  }
  if (!auth_request(req)) {
    return ESP_FAIL;
  }
  esp_err_t (*handler)(httpd_req_t *) = req->user_ctx;
  return handler(req);
}

static esp_err_t register_with(httpd_handle_t srv, const httpd_uri_t *uri,
                               esp_err_t (*wrapper)(httpd_req_t *)) {
  httpd_uri_t wrapped = *uri;
  wrapped.user_ctx = (void *) uri->handler;
  wrapped.handler = wrapper;
  return httpd_register_uri_handler(srv, &wrapped);
}

esp_err_t http_register(httpd_handle_t srv, const httpd_uri_t *uri) {
  return register_with(srv, uri, guarded);
}

esp_err_t http_register_open(httpd_handle_t srv, const httpd_uri_t *uri) {
  return register_with(srv, uri, open_guarded);
}

void bufappend(char *out, int *n, int cap, const char *fmt, ...) {
  if (*n >= cap - 1) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(out + *n, cap - *n, fmt, ap);
  va_end(ap);
  if (w < 0) {
    return;
  }
  *n += w;
  if (*n >= cap) {
    *n = cap - 1;
  }
}

bool recv_exact(httpd_req_t *req, char *buf, int len) {
  int got = 0;
  int timeouts = 0;
  while (got < len) {
    int r = httpd_req_recv(req, buf + got, len - got);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) {
      if (++timeouts >= RECV_TIMEOUT_LIMIT) {
        return false;
      }
      continue;
    }
    if (r <= 0) {
      return false;
    }
    timeouts = 0;
    got += r;
  }
  return true;
}
