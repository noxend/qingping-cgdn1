// Prints what creds_rules.c computes, for run.sh to hold against Python's hashlib:
//   key <salt-hex> <password>   the PBKDF2 key creds_hash_fill stores, in hex
//   match <salt-hex> <stored-pass> <tried-pass>   1 or 0 from creds_hash_matches
//   ok <password>               1 or 0 from creds_pass_ok
//   gen <bytes-hex>             the AP password made from those random bytes, or "short"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "creds_rules.h"

static size_t unhex(const char *s, uint8_t *out, size_t cap) {
  size_t n = 0;
  for (; s[0] && s[1] && n < cap; s += 2) {
    char b[3] = {s[0], s[1], 0};
    out[n++] = (uint8_t) strtoul(b, NULL, 16);
  }
  return n;
}

static void salted(creds_hash_t *h, const char *salt_hex) {
  memset(h, 0, sizeof(*h));
  h->iterations = CREDS_ITERATIONS;
  unhex(salt_hex, h->salt, CREDS_SALT_LEN);
}

int main(int argc, char **argv) {
  if (argc >= 4 && strcmp(argv[1], "key") == 0) {
    creds_hash_t h;
    salted(&h, argv[2]);
    if (!creds_hash_fill(&h, argv[3])) {
      return 2;
    }
    for (int i = 0; i < CREDS_KEY_LEN; i++) {
      printf("%02x", h.key[i]);
    }
    printf("\n");
    return 0;
  }
  if (argc >= 5 && strcmp(argv[1], "match") == 0) {
    creds_hash_t h;
    salted(&h, argv[2]);
    creds_hash_fill(&h, argv[3]);
    printf("%d\n", creds_hash_matches(&h, argv[4]));
    return 0;
  }
  if (argc >= 3 && strcmp(argv[1], "ok") == 0) {
    printf("%d\n", creds_pass_ok(argv[2]));
    return 0;
  }
  if (argc >= 3 && strcmp(argv[1], "gen") == 0) {
    uint8_t rnd[256];
    size_t n = unhex(argv[2], rnd, sizeof(rnd));
    char out[16];
    printf("%s\n", creds_make_password(rnd, n, out, 12) ? out : "short");
    return 0;
  }
  return 2;
}
