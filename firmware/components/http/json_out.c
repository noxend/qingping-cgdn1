#include "json_out.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "json_escape.h"

static void flush(json_out_t *o) {
  if (o->n > 0 && o->err == ESP_OK) {
    o->err = httpd_resp_send_chunk(o->req, o->buf, o->n);
  }
  o->n = 0;
}

void json_out_begin(json_out_t *o, httpd_req_t *req) {
  o->req = req;
  o->n = 0;
  o->err = ESP_OK;
  httpd_resp_set_type(req, "application/json");
}

void json_out_raw(json_out_t *o, const char *s, int len) {
  while (len > 0 && o->err == ESP_OK) {
    int room = (int) sizeof(o->buf) - o->n;
    int k = len < room ? len : room;
    memcpy(o->buf + o->n, s, k);
    o->n += k;
    s += k;
    len -= k;
    if (o->n == (int) sizeof(o->buf)) {
      flush(o);
    }
  }
}

void json_out_printf(json_out_t *o, const char *fmt, ...) {
  char text[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  json_out_raw(o, text, n < (int) sizeof(text) ? n : (int) sizeof(text) - 1);
}

void json_out_str(json_out_t *o, const char *s) {
  json_out_raw(o, "\"", 1);
  for (const char *p = s; *p; p++) {
    char one[JSON_ESCAPE_MAX];
    json_out_raw(o, one, json_escape_byte((unsigned char) *p, one));
  }
  json_out_raw(o, "\"", 1);
}

esp_err_t json_out_end(json_out_t *o) {
  flush(o);
  if (o->err == ESP_OK) {
    o->err = httpd_resp_send_chunk(o->req, NULL, 0);
  }
  return o->err;
}
