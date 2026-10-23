#include "session.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "settings.h"

#define KEY "sessions"
// Bump on any change to stored_t; a blob of another layout is dropped, which signs
// everyone out once.
#define LAYOUT 1
#define HASH_LEN 32

static const char *TAG = "session";

typedef struct {
  uint8_t hash[HASH_LEN];  // SHA-256 of the token's hex text
  int64_t expires;         // wall clock; 0: made in a boot that never had a clock
  uint8_t used;
} slot_t;

typedef struct {
  uint32_t layout;
  // The latest wall clock this device has seen, saved with the sessions: without
  // SNTP after a reboot, a session that expired before it is still refused.
  int64_t seen;
  slot_t slots[SESSION_SLOTS];
} stored_t;

static stored_t s_store;
static int64_t s_boot_seen;  // s_store.seen as this boot found it

void session_init(void) {
  size_t len = sizeof(s_store);
  esp_err_t err = settings_blob_get(KEY, &s_store, &len);
  if (err != ESP_OK || len != sizeof(s_store) || s_store.layout != LAYOUT) {
    memset(&s_store, 0, sizeof(s_store));
    s_store.layout = LAYOUT;
  }
  // A session with no expiry lives only in the boot that made it.
  for (int i = 0; i < SESSION_SLOTS; i++) {
    if (!s_store.slots[i].expires) {
      memset(&s_store.slots[i], 0, sizeof(slot_t));
    }
  }
  s_boot_seen = s_store.seen;
}

// The wall clock. Until SNTP sets it: the latest time saved before this boot plus the
// uptime, which the real time is surely past; 0 when the device never had a clock.
static int64_t clock_now(void) {
  int64_t now = session_now();
  if (!now && s_boot_seen) {
    now = s_boot_seen + session_uptime();
  }
  if (now > s_store.seen) {
    s_store.seen = now;  // saved with the next change
  }
  return now;
}

static bool save(void) {
  esp_err_t err = settings_blob_set(KEY, &s_store, sizeof(s_store));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "saving sessions: %s", esp_err_to_name(err));
  }
  return err == ESP_OK;
}

static bool token_shape_ok(const char *token) {
  if (strlen(token) != SESSION_TOKEN_HEX) {
    return false;
  }
  return strspn(token, "0123456789abcdef") == SESSION_TOKEN_HEX;
}

static void token_hash(const char *token, uint8_t out[HASH_LEN]) {
  mbedtls_sha256((const unsigned char *) token, strlen(token), out, 0);
}

static bool hash_equal(const uint8_t *a, const uint8_t *b) {
  volatile uint8_t diff = 0;
  for (int i = 0; i < HASH_LEN; i++) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0;
}

// The slot holding this token, or -1.
static int find(const char *token) {
  if (!token_shape_ok(token)) {
    return -1;
  }
  uint8_t hash[HASH_LEN];
  token_hash(token, hash);
  int found = -1;
  for (int i = 0; i < SESSION_SLOTS; i++) {
    if (s_store.slots[i].used && hash_equal(s_store.slots[i].hash, hash)) {
      found = i;
    }
  }
  return found;
}

// A free slot, else the one that expires first.
static int slot_for_new(void) {
  int pick = 0;
  for (int i = 0; i < SESSION_SLOTS; i++) {
    const slot_t *s = &s_store.slots[i];
    if (!s->used) {
      return i;
    }
    if (s->expires < s_store.slots[pick].expires) {
      pick = i;
    }
  }
  return pick;
}

bool session_create(char out[SESSION_TOKEN_BUF]) {
  uint8_t raw[SESSION_TOKEN_HEX / 2];
  esp_fill_random(raw, sizeof(raw));  // HTTP runs only with Wi-Fi up: the RNG is true
  for (size_t i = 0; i < sizeof(raw); i++) {
    snprintf(out + 2 * i, 3, "%02x", raw[i]);
  }
  int64_t now = clock_now();
  slot_t *s = &s_store.slots[slot_for_new()];
  slot_t before = *s;
  token_hash(out, s->hash);
  s->expires = now ? now + SESSION_MAX_AGE_S : 0;
  s->used = 1;
  if (!save()) {
    *s = before;  // the session it would have pushed out stays
    return false;
  }
  return true;
}

session_result_t session_check(const char *token) {
  int i = find(token);
  if (i < 0) {
    return SESSION_NONE;
  }
  int64_t now = clock_now();
  slot_t *s = &s_store.slots[i];
  if (s->expires && now >= s->expires) {
    memset(s, 0, sizeof(*s));
    save();
    return SESSION_NONE;
  }
  if (!now) {
    return SESSION_VALID;  // no clock ever: the session ends with this boot
  }
  int64_t extended = now + SESSION_MAX_AGE_S;
  if (s->expires && extended - s->expires < SESSION_EXTEND_STEP_S) {
    return SESSION_VALID;
  }
  s->expires = extended;
  save();
  return SESSION_EXTENDED;
}

void session_remove(const char *token) {
  int i = find(token);
  if (i >= 0) {
    memset(&s_store.slots[i], 0, sizeof(slot_t));
    save();
  }
}
