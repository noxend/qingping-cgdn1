// Test module for lib/libc_mini.c: format(i) writes case i into a buffer and
// returns its address, for firmware/test/wasm to compare with the host's snprintf.
#include <stdio.h>
#include "plugin_wasm.h"

static char s_out[64];

WASM_EXPORT("format") const char *fmt_format(int i) {
  switch (i) {
  case 0:
    snprintf(s_out, sizeof(s_out), "%02d:%02d:%02d", 9, 5, 0);
    break;
  case 1:
    snprintf(s_out, sizeof(s_out), "%04d-%02d-%02d", 2026, 10, 21);
    break;
  case 2:
    snprintf(s_out, sizeof(s_out), "%d|%i|%u", -42, 0, 4000000000u);
    break;
  case 3:
    snprintf(s_out, sizeof(s_out), "%5d|%-5d|%05d", -42, 7, -42);
    break;
  case 4:
    snprintf(s_out, sizeof(s_out), "%x|%X|%08x", 255, 48879, 3054);
    break;
  case 5:
    snprintf(s_out, sizeof(s_out), "%s|%.3s|%-6s|%6s", "abc", "abcdef", "ab", "cd");
    break;
  case 6:
    snprintf(s_out, sizeof(s_out), "%c%c|100%%|%ld", 'o', 'k', -2147483647L - 1);
    break;
  case 7:
    snprintf(s_out, 6, "%s", "truncated");
    break;
  default:
    s_out[0] = '\0';
    break;
  }
  return s_out;
}

WASM_EXPORT("draw") void fmt_draw(void) {
}
