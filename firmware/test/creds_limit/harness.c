// Host check of the wrong-password limit in components/system/creds.c: the real file
// over the real PBKDF2 (creds_rules.c, IDF's mbedtls), with a fake clock that a task
// delay moves. The question it answers: can other clients' wrong tries keep the
// owner, with the right password, from signing in?
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "creds.h"
#include "creds_rules.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings.h"

#define OWNER_PASS "owner-pass-1"
#define OWNER 0x0a0a0a0a
#define SECOND 1000000

static int s_failed;

static void check(bool ok, const char *what) {
  printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    s_failed++;
  }
}

// ---- the device around creds.c -------------------------------------------------------

static int64_t s_us = 5 * SECOND;
static int s_lock;
static int s_lock_errors;

int64_t esp_timer_get_time(void) {
  return s_us;
}

void vTaskDelay(uint32_t ticks) {
  if (s_lock) {
    s_lock_errors++;  // sleeping with the lock held stops every other check
  }
  s_us += (int64_t) ticks * 1000;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  return &s_lock;
}

int xSemaphoreTake(SemaphoreHandle_t s, uint32_t ticks) {
  (void) ticks;
  if (*s) {
    s_lock_errors++;  // taken twice: a deadlock on the device
  }
  *s = 1;
  return 1;
}

int xSemaphoreGive(SemaphoreHandle_t s) {
  *s = 0;
  return 1;
}

void esp_fill_random(void *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    ((unsigned char *) buf)[i] = (unsigned char) arc4random();
  }
}

static struct {
  char key[16];
  unsigned char data[128];
  size_t len;
} s_blobs[4];

esp_err_t settings_blob_get(const char *key, void *out, size_t *len) {
  for (int i = 0; i < 4; i++) {
    if (strcmp(s_blobs[i].key, key) == 0 && s_blobs[i].len <= *len) {
      memcpy(out, s_blobs[i].data, s_blobs[i].len);
      *len = s_blobs[i].len;
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t settings_blob_set(const char *key, const void *data, size_t len) {
  for (int i = 0; i < 4; i++) {
    if (!s_blobs[i].key[0] || strcmp(s_blobs[i].key, key) == 0) {
      snprintf(s_blobs[i].key, sizeof(s_blobs[i].key), "%s", key);
      memcpy(s_blobs[i].data, data, len);
      s_blobs[i].len = len;
      return ESP_OK;
    }
  }
  return ESP_FAIL;
}

static int s_hashed_locked;

// creds.c calls this for creds_hash_matches (run.sh): PBKDF2 under the lock would
// make creds_config_try on the server task wait for it.
bool traced_hash_matches(const creds_hash_t *h, const char *pass) {
  s_hashed_locked += s_lock;
  return creds_hash_matches(h, pass);
}

// ---- the checks ------------------------------------------------------------------------

// A device whose password was set before this boot: nothing is cached in RAM yet.
static void boot_with_password(void) {
  creds_hash_t h = {.iterations = CREDS_ITERATIONS};
  esp_fill_random(h.salt, sizeof(h.salt));
  creds_hash_fill(&h, OWNER_PASS);
  settings_blob_set("cfg_hash", &h, sizeof(h));
  creds_seed_entropy();
  creds_init();
}

int main(void) {
  boot_with_password();

  uint32_t lone = 0x0a0a0a63;
  check(creds_config_try(OWNER_PASS, OWNER) == CREDS_NEEDS_HASH && creds_last_hash_ms() < 0,
        "try, the right password after a boot: needs the hash, none run");
  check(creds_config_check("guess-0001", lone) == CREDS_WRONG, "a wrong try: wrong");
  check(creds_config_check("guess-0002", lone) == CREDS_BUSY,
        "the same client again at once: busy");
  check(creds_config_try("guess-0002", lone) == CREDS_BUSY, "try tells busy without the hash");
  s_us += SECOND;
  check(creds_config_check("guess-0003", lone) == CREDS_WRONG, "a second later: checked again");

  s_us += 2 * SECOND;
  int busy = 0;
  for (int i = 0; i < 10; i++) {
    busy += creds_config_check("guess-flood", lone) == CREDS_BUSY;
  }
  check(busy == 9, "ten tries at once from one address: one checked, nine busy");

  // Forty other addresses, each with a wrong try, as fast as the server takes them.
  s_us += 2 * SECOND;
  int64_t started = s_us;
  int64_t at[40];
  busy = 0;
  for (int i = 0; i < 40; i++) {
    busy += creds_config_check("guess-other", 0x0a0a0b00 + i) != CREDS_WRONG;
    at[i] = s_us;
  }
  check(busy == 0, "forty other clients: each try checked, none refused");
  int most = 0;
  for (int i = 0; i < 40; i++) {
    int n = 0;
    for (int j = i; j < 40 && at[j] < at[i] + SECOND; j++) {
      n++;
    }
    most = n > most ? n : most;
  }
  check(most <= 4, "no more than four PBKDF2 runs in any second");
  check(s_us - started >= 9 * SECOND, "forty tries took their turns: at least nine seconds");

  int64_t before = s_us;
  check(creds_config_check(OWNER_PASS, OWNER) == CREDS_OK,
        "then the owner, right password, new address: signed in");
  // A turn opens a second after the fourth-last run, plus the tick the delay rounds up.
  check(s_us - before <= SECOND + 10 * 1000, "and waited at most a second for its turn");
  before = s_us;
  check(creds_config_check(OWNER_PASS, OWNER) == CREDS_OK && s_us == before,
        "the right password again: from the cache, no wait");
  check(creds_config_try(OWNER_PASS, 0x0a0a0c01) == CREDS_OK,
        "try, the right password once it matched: ok from the cache");
  check(creds_config_try("", OWNER) == CREDS_WRONG, "try, an empty password: wrong");
  check(s_hashed_locked == 0, "PBKDF2 never ran with the lock held");
  check(s_lock_errors == 0, "no sleep with the lock held, no lock taken twice");

  printf("%d failed\n", s_failed);
  return s_failed ? 1 : 0;
}
