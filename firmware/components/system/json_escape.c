#include "json_escape.h"

#include <stdio.h>
#include <string.h>

int json_escape_byte(unsigned char c, char out[JSON_ESCAPE_MAX]) {
  if (c == '"' || c == '\\') {
    out[0] = '\\';
    out[1] = (char) c;
    return 2;
  }
  if (c < 0x20) {
    char text[JSON_ESCAPE_MAX + 1];
    snprintf(text, sizeof(text), "\\u%04x", c);
    memcpy(out, text, JSON_ESCAPE_MAX);
    return JSON_ESCAPE_MAX;
  }
  out[0] = (char) c;
  return 1;
}

int json_escape(char *out, int cap, const char *s) {
  int n = 0;
  for (const char *p = s ? s : ""; *p; p++) {
    char one[JSON_ESCAPE_MAX];
    int k = json_escape_byte((unsigned char) *p, one);
    if (n + k > cap - 1) {
      break;
    }
    memcpy(out + n, one, k);
    n += k;
  }
  if (cap > 0) {
    out[n] = '\0';
  }
  return n;
}
