#include "signed_envelope.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#define SIG_MAX 80  // DER ECDSA P-256 is at most 72 bytes

extern const char web_pub_start[] asm("_binary_web_manifest_pub_pem_start");
extern const char web_pub_end[] asm("_binary_web_manifest_pub_pem_end");

static bool signature_ok(const unsigned char *payload, size_t len, const unsigned char *sig,
                         size_t sig_len) {
  unsigned char hash[32];
  if (mbedtls_sha256(payload, len, hash, 0) != 0) {
    return false;
  }
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  size_t key_len = (size_t) (web_pub_end - web_pub_start);  // EMBED_TXTFILES adds the NUL
  int r = mbedtls_pk_parse_public_key(&pk, (const unsigned char *) web_pub_start, key_len);
  if (r == 0) {
    r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sig_len);
  }
  mbedtls_pk_free(&pk);
  return r == 0;
}

static bool b64(const cJSON *v, unsigned char *out, size_t cap, size_t *len) {
  return cJSON_IsString(v) &&
         mbedtls_base64_decode(out, cap, len, (const unsigned char *) v->valuestring,
                               strlen(v->valuestring)) == 0;
}

char *signed_envelope_open(const char *body, const char *tag) {
  cJSON *root = cJSON_Parse(body);
  unsigned char *payload = malloc(ENVELOPE_PAYLOAD_MAX + 1);
  unsigned char sig[SIG_MAX];
  size_t plen = 0;
  size_t slen = 0;
  bool ok = payload &&
            b64(cJSON_GetObjectItem(root, "payload"), payload, ENVELOPE_PAYLOAD_MAX, &plen) &&
            b64(cJSON_GetObjectItem(root, "sig"), sig, sizeof(sig), &slen);
  cJSON_Delete(root);
  if (ok && signature_ok(payload, plen, sig, slen)) {
    payload[plen] = '\0';
    return (char *) payload;
  }
  if (ok) {
    ESP_LOGE(tag, "manifest signature does not verify");
  }
  free(payload);
  return NULL;
}

bool envelope_path_ok(const char *s) {
  if (!s[0] || s[0] == '/' || strstr(s, "..")) {
    return false;
  }
  for (const char *p = s; *p; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '_' || *p == '-' || *p == '/';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool envelope_terminated(const char *s, size_t cap) {
  return memchr(s, 0, cap) != NULL;
}

bool envelope_copy_str(char *dst, size_t cap, const cJSON *v) {
  if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) {
    return false;
  }
  strcpy(dst, v->valuestring);
  return true;
}
