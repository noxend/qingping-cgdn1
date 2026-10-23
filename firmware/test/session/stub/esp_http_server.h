// Host stand-in: a request is the harness's own list of headers and a body, and
// the answer (status, headers, body) is kept for the harness to read back.
#pragma once
#include <stddef.h>
#include "esp_err.h"

#define HTTPD_TYPE_JSON "application/json"
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define HTTPD_MAX_HDRS 8

typedef void *httpd_handle_t;
typedef enum {
  HTTPD_400_BAD_REQUEST,
  HTTPD_401_UNAUTHORIZED,
  HTTPD_403_FORBIDDEN,
  HTTPD_404_NOT_FOUND,
  HTTPD_408_REQ_TIMEOUT,
  HTTPD_411_LENGTH_REQUIRED,
  HTTPD_413_CONTENT_TOO_LARGE,
  HTTPD_500_INTERNAL_SERVER_ERROR,
  HTTPD_ERR_CODE_MAX,
} httpd_err_code_t;
typedef enum { HTTP_GET, HTTP_POST } httpd_method_t;

typedef struct {
  const char *name;
  const char *value;
} httpd_hdr_t;

typedef struct httpd_req {
  httpd_handle_t handle;
  const char *uri;
  httpd_method_t method;
  httpd_hdr_t in[HTTPD_MAX_HDRS];  // the request's headers, NULL name ends the list
  const char *body;
  int content_len;
  int read;
  void *user_ctx;
  char status[40];
  httpd_hdr_t out[HTTPD_MAX_HDRS];  // headers the answer set
  int out_n;
  char answer[512];
  int sent;
  int fd;                    // the client: net_peer_ipv4 gives 10.10.10.<fd>
  struct httpd_req *origin;  // in a copy from httpd_req_async_handler_begin: the request
  int handed_over;           // httpd_req_async_handler_begin was called on it
  int done;                  // its copy completed: the order it completed in, from 1
} httpd_req_t;

typedef void (*httpd_work_fn_t)(void *arg);

typedef struct {
  const char *uri;
  httpd_method_t method;
  esp_err_t (*handler)(httpd_req_t *req);
  void *user_ctx;
} httpd_uri_t;

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t size);
esp_err_t httpd_req_get_cookie_val(httpd_req_t *r, const char *name, char *val, size_t *size);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len);
int httpd_req_to_sockfd(httpd_req_t *r);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, int len);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str);
esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t code, const char *msg);
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *uri);
esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t val_size);
esp_err_t httpd_req_async_handler_begin(httpd_req_t *r, httpd_req_t **out);
esp_err_t httpd_req_async_handler_complete(httpd_req_t *r);
esp_err_t httpd_queue_work(httpd_handle_t h, httpd_work_fn_t work, void *arg);
esp_err_t httpd_sess_trigger_close(httpd_handle_t h, int sockfd);
