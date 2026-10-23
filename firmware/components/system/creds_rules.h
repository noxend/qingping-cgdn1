// The pure part of creds.c: the AP password alphabet, the config password rules
// and its PBKDF2 hash. No ESP-IDF include besides mbedtls, so the host test
// compiles it against IDF's mbedtls and checks it with Python's hashlib.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CREDS_SALT_LEN 16
#define CREDS_KEY_LEN 32
#define CREDS_ITERATIONS 4096
#define CREDS_HASH_VERSION 1

// Stored as one record in the core settings (settings_blob_*, key "cfg_hash").
typedef struct {
  uint8_t version;
  uint8_t salt[CREDS_SALT_LEN];
  uint32_t iterations;
  uint8_t key[CREDS_KEY_LEN];
} creds_hash_t;

// Fills out with `len` characters of an alphabet without look-alikes (no i, l, o,
// 0, 1), one character per accepted random byte. False when the bytes ran out.
bool creds_make_password(const uint8_t *rnd, size_t n, char *out, size_t len);

// 8 to 63 characters, printable ASCII without space: the value travels in an HTTP
// header, where a server may trim spaces and non-ASCII has no agreed encoding.
bool creds_pass_ok(const char *pass);

// PBKDF2-HMAC-SHA256 of pass with h->salt and h->iterations into h->key, and
// h->version set. False on an mbedtls error.
bool creds_hash_fill(creds_hash_t *h, const char *pass);

// The record is ours and pass hashes to its key; the key compare is constant-time.
bool creds_hash_matches(const creds_hash_t *h, const char *pass);
