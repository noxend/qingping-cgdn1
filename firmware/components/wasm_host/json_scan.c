#include "json_scan.h"

#include <stdint.h>
#include <string.h>

static bool in_range(int len, int at) {
  return at >= 0 && at < len;
}

int json_skip_space(const char *js, int len, int at) {
  if (at < 0) {
    return -1;
  }
  while (at < len) {
    char c = js[at];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      return at;
    }
    at++;
  }
  return -1;
}

json_type_t json_type(const char *js, int len, int at) {
  at = json_skip_space(js, len, at);
  if (at < 0) {
    return JSON_BAD;
  }
  char c = js[at];
  if (c == '{') {
    return JSON_OBJECT;
  }
  if (c == '[') {
    return JSON_ARRAY;
  }
  if (c == '"') {
    return JSON_STRING;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    return JSON_NUMBER;
  }
  if (c == 't') {
    return JSON_TRUE;
  }
  if (c == 'f') {
    return JSON_FALSE;
  }
  if (c == 'n') {
    return JSON_NULL;
  }
  return JSON_BAD;
}

// ---- strings -------------------------------------------------------------------------------

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// The four hex digits of a \u escape whose 'u' is at p, or -1.
static int hex4(const char *js, int len, int p) {
  if (p + 4 >= len) {
    return -1;
  }
  int v = 0;
  for (int i = 1; i <= 4; i++) {
    int d = hex_digit(js[p + i]);
    if (d < 0) {
      return -1;
    }
    v = v * 16 + d;
  }
  return v;
}

// The length of the UTF-8 sequence at p, or 0 when it is not a well-formed one
// (Unicode 15, table 3-7: no overlong forms, no surrogates, nothing past U+10FFFF).
static int utf8_length(const char *js, int len, int p) {
  const uint8_t *s = (const uint8_t *) js;
  uint8_t b = s[p];
  int n;
  uint8_t lo = 0x80;
  uint8_t hi = 0xBF;
  if (b >= 0xC2 && b <= 0xDF) {
    n = 2;
  } else if (b >= 0xE0 && b <= 0xEF) {
    n = 3;
    lo = b == 0xE0 ? 0xA0 : 0x80;
    hi = b == 0xED ? 0x9F : 0xBF;
  } else if (b >= 0xF0 && b <= 0xF4) {
    n = 4;
    lo = b == 0xF0 ? 0x90 : 0x80;
    hi = b == 0xF4 ? 0x8F : 0xBF;
  } else {
    return 0;
  }
  if (p + n > len || s[p + 1] < lo || s[p + 1] > hi) {
    return 0;
  }
  for (int i = 2; i < n; i++) {
    if (s[p + i] < 0x80 || s[p + i] > 0xBF) {
      return 0;
    }
  }
  return n;
}

// Writes the code point as UTF-8 into out at *n when it fits under cap.
static bool put_utf8(char *out, int cap, int *n, uint32_t cp) {
  char b[4];
  int k;
  if (cp < 0x80) {
    b[0] = (char) cp;
    k = 1;
  } else if (cp < 0x800) {
    b[0] = (char) (0xC0 | (cp >> 6));
    b[1] = (char) (0x80 | (cp & 0x3F));
    k = 2;
  } else if (cp < 0x10000) {
    b[0] = (char) (0xE0 | (cp >> 12));
    b[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
    b[2] = (char) (0x80 | (cp & 0x3F));
    k = 3;
  } else {
    b[0] = (char) (0xF0 | (cp >> 18));
    b[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    b[3] = (char) (0x80 | (cp & 0x3F));
    k = 4;
  }
  if (out) {
    if (*n + k >= cap) {
      return false;
    }
    memcpy(out + *n, b, k);
  }
  *n += k;
  return true;
}

// Reads the string whose opening quote is at `at`: the offset after its closing quote,
// or -1 when it is not a valid string. With out, also decodes it there (terminated) and
// fails when it does not fit in cap or holds a NUL; *out_len gets the decoded length.
static int string_scan(const char *js, int len, int at, char *out, int cap, int *out_len) {
  if (!in_range(len, at) || js[at] != '"') {
    return -1;
  }
  int n = 0;
  int p = at + 1;
  while (p < len) {
    unsigned char c = (unsigned char) js[p];
    if (c == '"') {
      if (out) {
        out[n] = '\0';
      }
      if (out_len) {
        *out_len = n;
      }
      return p + 1;
    }
    if (c < 0x20) {
      return -1;
    }
    if (c == '\\') {
      if (p + 1 >= len) {
        return -1;
      }
      char e = js[p + 1];
      uint32_t cp;
      int used = 2;
      switch (e) {
      case '"':
      case '\\':
      case '/':
        cp = (uint32_t) e;
        break;
      case 'b':
        cp = '\b';
        break;
      case 'f':
        cp = '\f';
        break;
      case 'n':
        cp = '\n';
        break;
      case 'r':
        cp = '\r';
        break;
      case 't':
        cp = '\t';
        break;
      case 'u': {
        int v = hex4(js, len, p + 1);
        if (v < 0 || (v >= 0xDC00 && v <= 0xDFFF)) {
          return -1;
        }
        cp = (uint32_t) v;
        used = 6;
        if (v >= 0xD800 && v <= 0xDBFF) {
          bool pair = p + 7 < len && js[p + 6] == '\\' && js[p + 7] == 'u';
          int low = pair ? hex4(js, len, p + 7) : -1;
          if (low < 0xDC00 || low > 0xDFFF) {
            return -1;
          }
          cp = 0x10000 + (((uint32_t) v - 0xD800) << 10) + ((uint32_t) low - 0xDC00);
          used = 12;
        }
        break;
      }
      default:
        return -1;
      }
      if (out && cp == 0) {
        return -1;
      }
      if (!put_utf8(out, cap, &n, cp)) {
        return -1;
      }
      p += used;
      continue;
    }
    int k = c < 0x80 ? 1 : utf8_length(js, len, p);
    if (k == 0) {
      return -1;
    }
    if (out) {
      if (n + k >= cap) {
        return -1;
      }
      memcpy(out + n, js + p, k);
    }
    n += k;
    p += k;
  }
  return -1;
}

// ---- scalars -------------------------------------------------------------------------------

static bool is_digit(const char *js, int len, int p) {
  return p < len && js[p] >= '0' && js[p] <= '9';
}

static int digits_end(const char *js, int len, int p) {
  while (is_digit(js, len, p)) {
    p++;
  }
  return p;
}

// -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
static int number_end(const char *js, int len, int p) {
  if (p < len && js[p] == '-') {
    p++;
  }
  if (!is_digit(js, len, p)) {
    return -1;
  }
  p = js[p] == '0' ? p + 1 : digits_end(js, len, p);
  if (p < len && js[p] == '.') {
    if (!is_digit(js, len, p + 1)) {
      return -1;
    }
    p = digits_end(js, len, p + 1);
  }
  if (p < len && (js[p] == 'e' || js[p] == 'E')) {
    p++;
    if (p < len && (js[p] == '+' || js[p] == '-')) {
      p++;
    }
    if (!is_digit(js, len, p)) {
      return -1;
    }
    p = digits_end(js, len, p);
  }
  return p;
}

static int word_end(const char *js, int len, int p, const char *word) {
  int n = (int) strlen(word);
  if (p + n > len || memcmp(js + p, word, n) != 0) {
    return -1;
  }
  return p + n;
}

// A value that holds no other: the offset after it, or -1.
static int scalar_end(const char *js, int len, int p) {
  switch (js[p]) {
  case '"':
    return string_scan(js, len, p, NULL, 0, NULL);
  case 't':
    return word_end(js, len, p, "true");
  case 'f':
    return word_end(js, len, p, "false");
  case 'n':
    return word_end(js, len, p, "null");
  default:
    return number_end(js, len, p);
  }
}

// ---- values --------------------------------------------------------------------------------

// The key and colon of a member at p: the offset of its value, or -1.
static int key_end(const char *js, int len, int p) {
  p = string_scan(js, len, p, NULL, 0, NULL);
  p = json_skip_space(js, len, p);
  if (p < 0 || js[p] != ':') {
    return -1;
  }
  return json_skip_space(js, len, p + 1);
}

int json_end(const char *js, int len, int at) {
  // The closing bracket each open container waits for.
  char closers[JSON_DEPTH_MAX];
  int depth = 0;
  int p = json_skip_space(js, len, at);
  while (p >= 0) {
    // p is at a value.
    char c = js[p];
    if (c == '{' || c == '[') {
      if (depth == JSON_DEPTH_MAX) {
        return -1;
      }
      closers[depth++] = c == '{' ? '}' : ']';
      p = json_skip_space(js, len, p + 1);
      if (p < 0) {
        return -1;
      }
      if (js[p] == closers[depth - 1]) {
        depth--;
        p++;
      } else {
        p = c == '{' ? key_end(js, len, p) : p;
        continue;
      }
    } else {
      p = scalar_end(js, len, p);
      if (p < 0) {
        return -1;
      }
    }
    // p is just after a value: close what ends here, then the next value or the end.
    while (depth > 0) {
      int q = json_skip_space(js, len, p);
      if (q < 0) {
        return -1;
      }
      if (js[q] == closers[depth - 1]) {
        depth--;
        p = q + 1;
        continue;
      }
      if (js[q] != ',') {
        return -1;
      }
      q = json_skip_space(js, len, q + 1);
      p = closers[depth - 1] == '}' ? key_end(js, len, q) : q;
      break;
    }
    if (depth == 0) {
      return p;
    }
  }
  return -1;
}

bool json_valid(const char *js, int len) {
  int end = json_end(js, len, 0);
  return end >= 0 && json_skip_space(js, len, end) < 0;
}

// ---- containers ----------------------------------------------------------------------------

int json_first(const char *js, int len, int at) {
  json_type_t t = json_type(js, len, at);
  if ((t != JSON_OBJECT && t != JSON_ARRAY) || json_end(js, len, at) < 0) {
    return -1;
  }
  int p = json_skip_space(js, len, json_skip_space(js, len, at) + 1);
  if (p < 0 || js[p] == '}' || js[p] == ']') {
    return -1;
  }
  if (t == JSON_OBJECT && js[p] != '"') {
    return -1;
  }
  return p;
}

int json_member_value(const char *js, int len, int at) {
  at = json_skip_space(js, len, at);
  if (at < 0 || js[at] != '"') {
    return -1;
  }
  return key_end(js, len, at);
}

int json_next(const char *js, int len, int at) {
  int end = json_end(js, len, at);
  int p = json_skip_space(js, len, end);
  if (p >= 0 && js[p] == ':') {
    // `at` is a member's key: the element ends after its value.
    p = json_skip_space(js, len, json_end(js, len, p + 1));
  }
  if (p < 0 || js[p] != ',') {
    return -1;
  }
  return json_skip_space(js, len, p + 1);
}

int json_string(const char *js, int len, int at, char *out, int cap) {
  int n = -1;
  at = json_skip_space(js, len, at);
  if (cap <= 0 || at < 0 || string_scan(js, len, at, out, cap, &n) < 0) {
    if (cap > 0) {
      out[0] = '\0';
    }
    return -1;
  }
  return n;
}

bool json_string_eq(const char *js, int len, int at, const char *s) {
  char buf[JSON_COMPARE_MAX + 1];
  return json_string(js, len, at, buf, sizeof(buf)) >= 0 && strcmp(buf, s) == 0;
}

int json_find(const char *js, int len, int at, const char *key) {
  if (json_type(js, len, at) != JSON_OBJECT) {
    return -1;
  }
  for (int m = json_first(js, len, at); m >= 0; m = json_next(js, len, m)) {
    if (json_string_eq(js, len, m, key)) {
      int value = json_member_value(js, len, m);
      return json_end(js, len, value) >= 0 ? value : -1;
    }
  }
  return -1;
}

int json_enum(const char *js, int len, int at, const char *list, int list_len) {
  char buf[JSON_COMPARE_MAX + 1];
  if (json_string(js, len, at, buf, sizeof(buf)) < 0) {
    return -1;
  }
  int index = 0;
  int p = 0;
  while (p < list_len && list[p] != '\0') {
    const char *entry = list + p;
    const char *nul = memchr(entry, '\0', list_len - p);
    if (!nul) {
      return -1;
    }
    if (strcmp(entry, buf) == 0) {
      return index;
    }
    p += (int) (nul - entry) + 1;
    index++;
  }
  return -1;
}
