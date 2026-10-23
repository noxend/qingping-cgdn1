// Host stand-in for components/http/util.h: the same codes and calls.
#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
#define HTTP_429_TOO_MANY ((httpd_err_code_t) (HTTPD_ERR_CODE_MAX + 1))
#define HTTP_409_CONFLICT ((httpd_err_code_t) (HTTPD_ERR_CODE_MAX + 2))
bool recv_exact(httpd_req_t *req, char *buf, int len);
esp_err_t http_error(httpd_req_t *req, httpd_err_code_t code, const char *msg);
esp_err_t http_register(httpd_handle_t srv, const httpd_uri_t *uri);
