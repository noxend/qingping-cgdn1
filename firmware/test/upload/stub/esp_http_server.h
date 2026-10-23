// Host stand-in: the request is the harness's own, its body read from memory and its
// answer collected for the harness to read back.
#pragma once
#include <stddef.h>
#include <sys/types.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef enum {
  HTTPD_400_BAD_REQUEST,
  HTTPD_404_NOT_FOUND,
  HTTPD_500_INTERNAL_SERVER_ERROR,
  HTTPD_ERR_CODE_MAX,
} httpd_err_code_t;
typedef enum { HTTP_GET, HTTP_POST } httpd_method_t;
typedef struct {
  int content_len;
  const char *body;          // what recv_exact hands out
  const char *content_type;  // the Content-Type header, or NULL for none
  int read;
  char status[32];
  char *answer;  // malloc'd, NUL-terminated
  size_t answer_len;
} httpd_req_t;
typedef struct {
  const char *uri;
  httpd_method_t method;
  esp_err_t (*handler)(httpd_req_t *req);
  void *user_ctx;
} httpd_uri_t;
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val,
                                      size_t val_size);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str);
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, ssize_t len);
