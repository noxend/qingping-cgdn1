// Small helpers shared by the HTTP handlers.
#pragma once

#include <stdbool.h>
#include "esp_http_server.h"

// Consecutive socket timeouts a body read tolerates before giving up on the client.
#define RECV_TIMEOUT_LIMIT 3

// Bounded append: never lets the cursor pass the buffer, so a full buffer cannot
// turn (cap - n) negative into a huge snprintf size. On truncation n stops at
// cap - 1 and the output stays a valid C string.
void bufappend(char *out, int *n, int cap, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

// Receive exactly len bytes of the request body; false after a few consecutive
// timeouts, so a stalled client cannot hold the single server task.
bool recv_exact(httpd_req_t *req, char *buf, int len);

// esp_http_server has no 429, 409 or 503 in httpd_err_code_t; http_error knows these.
#define HTTP_429_TOO_MANY ((httpd_err_code_t) (HTTPD_ERR_CODE_MAX + 1))
#define HTTP_409_CONFLICT ((httpd_err_code_t) (HTTPD_ERR_CODE_MAX + 2))
#define HTTP_503_BUSY ((httpd_err_code_t) (HTTPD_ERR_CODE_MAX + 3))

// Answers the request with an HTTP error and a JSON body {"error":"<msg>"}, so a
// client can always parse an answer (part of the API contract, see API_VERSION).
// A handler ends with `return http_error(...)`. For a body of at most HTTP_DRAIN_MAX
// bytes that is ESP_OK once the answer is sent: the server reads off what is left of the
// body and keeps the connection, where closing it on unread data would make lwIP reset
// it and could lose the answer. A larger body (an upload) closes the connection, so a
// client cannot keep the one server task reading a body it only claims to send.
#define HTTP_DRAIN_MAX 4096
esp_err_t http_error(httpd_req_t *req, httpd_err_code_t code, const char *msg);

// Registers a handler behind the Host check (auth_host_ok) and sign-in
// (auth_request): every endpoint goes through here, so no handler can forget either.
// The uri's user_ctx is taken.
esp_err_t http_register(httpd_handle_t srv, const httpd_uri_t *uri);
// The same behind the Host check only, for what a caller who has not signed in
// needs: the page, the sign-in itself and the first setup.
esp_err_t http_register_open(httpd_handle_t srv, const httpd_uri_t *uri);

// The server's 404 handler: the captive-portal redirect for a setup AP client asking
// for another host (a probe path such as /generate_204), else the usual 404.
esp_err_t http_not_found(httpd_req_t *req, httpd_err_code_t err);
