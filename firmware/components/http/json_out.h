// A JSON answer sent in chunks as it is written, so no answer has a size of its own:
// what a handler writes goes out in pieces of a small buffer. The first failed send
// sticks, and nothing more is sent after it.
#pragma once

#include "esp_http_server.h"

typedef struct {
  httpd_req_t *req;
  char buf[256];
  int n;
  esp_err_t err;
} json_out_t;

// Starts the answer: 200, application/json.
void json_out_begin(json_out_t *o, httpd_req_t *req);
// Text written as it is: JSON syntax, numbers, a manifest already checked as JSON.
void json_out_raw(json_out_t *o, const char *s, int len);
void json_out_printf(json_out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// A string value, quoted, with what JSON needs escaped.
void json_out_str(json_out_t *o, const char *s);
// Sends what is left and ends the answer; the first error, or ESP_OK.
esp_err_t json_out_end(json_out_t *o);
