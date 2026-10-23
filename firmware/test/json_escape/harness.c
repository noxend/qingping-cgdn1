// Prints json_escape of the bytes given in hex, cut to a buffer of cap bytes:
//   harness <hex> <cap>
#include <stdio.h>
#include <stdlib.h>
#include "json_escape.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    return 2;
  }
  char in[256];
  int n = 0;
  for (const char *h = argv[1]; h[0] && h[1] && n < (int) sizeof(in) - 1; h += 2) {
    char pair[3] = {h[0], h[1], 0};
    in[n++] = (char) strtol(pair, NULL, 16);
  }
  in[n] = '\0';
  int cap = atoi(argv[2]);
  char out[2048];
  int len = json_escape(out, cap, in);
  fwrite(out, 1, len, stdout);
  return 0;
}
