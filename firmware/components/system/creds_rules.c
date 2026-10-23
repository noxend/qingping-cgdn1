#include "creds_rules.h"

#include <string.h>
#include "mbedtls/pkcs5.h"

static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
#define ALPHABET_LEN (sizeof(ALPHABET) - 1)

bool creds_make_password(const uint8_t *rnd, size_t n, char *out, size_t len) {
  size_t got = 0;
  for (size_t i = 0; i < n && got < len; i++) {
    // The low five bits give 0..31; 31 is outside the alphabet and is skipped, so
    // every character stays equally likely.
    uint8_t v = rnd[i] & 0x1F;
    if (v < ALPHABET_LEN) {
      out[got++] = ALPHABET[v];
    }
  }
  out[got] = '\0';
  return got == len;
}

bool creds_pass_ok(const char *pass) {
  size_t n = strlen(pass);
  if (n < 8 || n > 63) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    if (pass[i] < 0x21 || pass[i] > 0x7E) {
      return false;
    }
  }
  return true;
}

static bool derive(const creds_hash_t *h, const char *pass, uint8_t key[CREDS_KEY_LEN]) {
  const unsigned char *pw = (const unsigned char *) pass;
  size_t pw_len = strlen(pass);
  int err = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, pw, pw_len, h->salt, CREDS_SALT_LEN,
                                          h->iterations, CREDS_KEY_LEN, key);
  return err == 0;
}

bool creds_hash_fill(creds_hash_t *h, const char *pass) {
  h->version = CREDS_HASH_VERSION;
  return derive(h, pass, h->key);
}

bool creds_hash_matches(const creds_hash_t *h, const char *pass) {
  if (h->version != CREDS_HASH_VERSION || h->iterations != CREDS_ITERATIONS) {
    return false;
  }
  uint8_t key[CREDS_KEY_LEN];
  if (!derive(h, pass, key)) {
    return false;
  }
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < CREDS_KEY_LEN; i++) {
    diff |= key[i] ^ h->key[i];
  }
  return diff == 0;
}
