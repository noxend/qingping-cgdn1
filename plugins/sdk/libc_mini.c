// The libc a module links: the string and memory calls and snprintf, as plain
// WebAssembly inside the module. Run there, every byte they touch costs metered
// instructions; a libc the core provided would run natively, where a 64 KB memset
// is one instruction and the instruction limit does not see it.
//
// snprintf knows %d %i %u %x %X %c %s %%, the flags '-' and '0', a width, a
// precision for %s and the length 'l' (32 bits on wasm32); anything else is copied.
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--) {
    *d++ = *s++;
  }
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s) {
    return memcpy(dst, src, n);
  }
  while (n--) {
    d[n] = s[n];
  }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = dst;
  while (n--) {
    *d++ = (unsigned char) c;
  }
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a;
  const unsigned char *y = b;
  for (size_t i = 0; i < n; i++) {
    if (x[i] != y[i]) {
      return x[i] - y[i];
    }
  }
  return 0;
}

size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) {
    n++;
  }
  return n;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char x = a[i];
    unsigned char y = b[i];
    if (x != y || !x) {
      return x - y;
    }
  }
  return 0;
}

int strcmp(const char *a, const char *b) {
  return strncmp(a, b, (size_t) -1);
}

char *strcpy(char *dst, const char *src) {
  return memcpy(dst, src, strlen(src) + 1);
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i]; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = '\0';
  }
  return dst;
}

char *strchr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char) c) {
      return (char *) s;
    }
    if (!*s) {
      return NULL;
    }
  }
}

size_t strcspn(const char *s, const char *reject) {
  size_t n = 0;
  while (s[n] && !strchr(reject, s[n])) {
    n++;
  }
  return n;
}

char *strstr(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  for (; *hay; hay++) {
    if (strncmp(hay, needle, n) == 0) {
      return (char *) hay;
    }
  }
  return n == 0 ? (char *) hay : NULL;
}

// ---- snprintf ------------------------------------------------------------------------

typedef struct {
  char *out;
  size_t cap;
  size_t len;  // what the whole result would take
} sink_t;

static void put(sink_t *k, char c) {
  if (k->len + 1 < k->cap) {
    k->out[k->len] = c;
  }
  k->len++;
}

static void put_padded(sink_t *k, const char *s, size_t n, int width, bool left, char pad) {
  int fill = width > (int) n ? width - (int) n : 0;
  if (!left) {
    // A zero pad goes after the sign: -05, not 0-5.
    if (pad == '0' && n > 0 && *s == '-') {
      put(k, *s++);
      n--;
    }
    while (fill--) {
      put(k, pad);
    }
  }
  for (size_t i = 0; i < n; i++) {
    put(k, s[i]);
  }
  while (left && fill-- > 0) {
    put(k, ' ');
  }
}

// The digits of v in `base`, written backwards from the end of buf.
static size_t format_unsigned(char *end, unsigned v, unsigned base, bool upper) {
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t n = 0;
  do {
    *--end = digits[v % base];
    v /= base;
    n++;
  } while (v);
  return n;
}

int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
  sink_t k = {.out = out, .cap = cap, .len = 0};
  for (const char *f = fmt; *f; f++) {
    if (*f != '%') {
      put(&k, *f);
      continue;
    }
    f++;
    bool left = false;
    char pad = ' ';
    for (; *f == '-' || *f == '0'; f++) {
      if (*f == '-') {
        left = true;
      } else {
        pad = '0';
      }
    }
    int width = 0;
    for (; *f >= '0' && *f <= '9'; f++) {
      width = width * 10 + (*f - '0');
    }
    int precision = -1;
    if (*f == '.') {
      precision = 0;
      for (f++; *f >= '0' && *f <= '9'; f++) {
        precision = precision * 10 + (*f - '0');
      }
    }
    if (*f == 'l') {
      f++;
    }
    char buf[12];
    char *end = buf + sizeof(buf);
    switch (*f) {
    case 'd':
    case 'i': {
      int v = va_arg(ap, int);
      unsigned magnitude = v < 0 ? 0u - (unsigned) v : (unsigned) v;
      size_t n = format_unsigned(end, magnitude, 10, false);
      if (v < 0) {
        end[-(int) n - 1] = '-';
        n++;
      }
      put_padded(&k, end - n, n, width, left, left ? ' ' : pad);
      break;
    }
    case 'u':
    case 'x':
    case 'X': {
      unsigned base = *f == 'u' ? 10 : 16;
      size_t n = format_unsigned(end, va_arg(ap, unsigned), base, *f == 'X');
      put_padded(&k, end - n, n, width, left, left ? ' ' : pad);
      break;
    }
    case 'c': {
      char c = (char) va_arg(ap, int);
      put_padded(&k, &c, 1, width, left, ' ');
      break;
    }
    case 's': {
      const char *s = va_arg(ap, const char *);
      s = s ? s : "(null)";
      size_t n = strlen(s);
      if (precision >= 0 && (size_t) precision < n) {
        n = (size_t) precision;
      }
      put_padded(&k, s, n, width, left, ' ');
      break;
    }
    case '\0':
      f--;  // a lone % at the end
      break;
    default:
      put(&k, *f);  // %% and anything unknown
      break;
    }
  }
  if (cap > 0) {
    out[k.len < cap ? k.len : cap - 1] = '\0';
  }
  return (int) k.len;
}

int snprintf(char *out, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(out, cap, fmt, ap);
  va_end(ap);
  return n;
}
