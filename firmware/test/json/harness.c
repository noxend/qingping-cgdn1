// Host check of components/wasm_host/json_scan.c. For every file given it prints one
// line run.sh compares with Python's json module: "bad" when json_valid refuses the
// text, else the tree as json_first, json_next, json_member_value and json_string walk
// it, strings in hex, so the two readings can be compared byte for byte.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_scan.h"

static char s_text[1 << 20];

static void hex(const char *s, int n) {
  for (int i = 0; i < n; i++) {
    printf("%02x", (unsigned char) s[i]);
  }
}

// json_find must land on the first member with this key: the member at m unless an
// earlier one has the same key. Python never prints the marks, so a slip shows as a
// difference.
static void check_find(const char *js, int len, int obj, int m, const char *key, int n) {
  if (n < 0 || n > JSON_COMPARE_MAX) {
    return;
  }
  int first = m;
  for (int e = json_first(js, len, obj); e >= 0 && e < m; e = json_next(js, len, e)) {
    if (json_string_eq(js, len, e, key)) {
      first = e;
      break;
    }
  }
  if (json_find(js, len, obj, key) != json_member_value(js, len, first)) {
    printf("FINDBAD");
  }
}

// json_enum finds the string as the second entry of a list; an empty string ends a
// list, so it is never found.
static void check_enum(const char *js, int len, int at, const char *s, int n) {
  if (n < 0 || n > JSON_COMPARE_MAX) {
    return;
  }
  static char list[JSON_COMPARE_MAX + 8];
  int k = n + 1;
  memcpy(list, "-x-", 4);
  memcpy(list + 4, s, k);
  int got = json_enum(js, len, at, list, 4 + k);
  int want = n == 0 ? -1 : (strcmp(s, "-x-") == 0 ? 0 : 1);
  if (got != want) {
    printf("ENUMBAD");
  }
}

static void walk(const char *js, int len, int at) {
  static char buf[1 << 16];
  switch (json_type(js, len, at)) {
  case JSON_OBJECT:
    printf("{");
    for (int m = json_first(js, len, at); m >= 0; m = json_next(js, len, m)) {
      int n = json_string(js, len, m, buf, sizeof(buf));
      check_find(js, len, at, m, buf, n);
      printf("k");
      hex(buf, n);
      printf(":");
      walk(js, len, json_member_value(js, len, m));
      printf(",");
    }
    printf("}");
    break;
  case JSON_ARRAY:
    printf("[");
    for (int e = json_first(js, len, at); e >= 0; e = json_next(js, len, e)) {
      walk(js, len, e);
      printf(",");
    }
    printf("]");
    break;
  case JSON_STRING: {
    int n = json_string(js, len, at, buf, sizeof(buf));
    check_enum(js, len, at, buf, n);
    printf("s");
    if (n < 0) {
      printf("!");  // a NUL inside: json_string refuses it
    } else {
      hex(buf, n);
    }
    break;
  }
  case JSON_NUMBER: {
    int start = json_skip_space(js, len, at);
    printf("n%.*s", json_end(js, len, at) - start, js + start);
    break;
  }
  case JSON_TRUE:
    printf("t");
    break;
  case JSON_FALSE:
    printf("f");
    break;
  case JSON_NULL:
    printf("z");
    break;
  default:
    printf("?");
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    FILE *f = fopen(argv[i], "rb");
    if (!f) {
      perror(argv[i]);
      return 2;
    }
    int len = (int) fread(s_text, 1, sizeof(s_text), f);
    fclose(f);
    if (!json_valid(s_text, len)) {
      printf("bad\n");
      continue;
    }
    walk(s_text, len, 0);
    printf("\n");
  }
  return 0;
}
