#include "creds.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "creds_rules.h"
#include "factory_reset.h"
#include "settings.h"

#ifdef CGDN1_MIGRATE
#include "secrets.h"
_Static_assert(sizeof(CONFIG_PASS) >= 9, "the migration build needs CONFIG_PASS in secrets.h");
// A longer one fits neither the new rules nor the 64-byte header buffer of auth.c.
_Static_assert(sizeof(CONFIG_PASS) <= CREDS_PASS_MAX + 1, "CONFIG_PASS over 63 chars");
#endif

#define KEY_AP "ap_pass"
#define KEY_CFG "cfg_hash"
#define SEED_LEN 32
#define RETRY_US (1000 * 1000)
#define CLIENT_SLOTS 16
// PBKDF2 runs of wrong or new passwords a second, all clients together. A request
// over the budget waits its turn instead of being refused, so nobody can keep the
// owner out by filling the budget. One PBKDF2 takes 0.48 s on this chip (cfg_ms), so
// the budget never makes a check wait here; it caps a faster hash.
#define HASHES_PER_S 4

static const char *TAG = "creds";

static uint8_t s_seed[SEED_LEN];
static uint8_t s_seed_salt[CREDS_SALT_LEN];
static char s_ap_pass[CREDS_AP_PASS_LEN + 1];
static SemaphoreHandle_t s_lock;
static creds_hash_t s_hash;
static bool s_hash_set;
// The last password that matched the hash, so a client that sends it with every
// request does not pay for PBKDF2 each time. RAM only.
static char s_verified[CREDS_PASS_MAX + 1];
// Clients that tried a wrong password: each waits a second before the next PBKDF2.
// When the table is full the entry that ends first gives way.
static struct {
  uint32_t client;
  int64_t until_us;
} s_wait[CLIENT_SLOTS];
// When the last HASHES_PER_S runs of PBKDF2 started, oldest at s_hash_next.
static int64_t s_hash_at[HASHES_PER_S];
static int s_hash_next;
static int s_hash_ms = -1;
static const char *s_migration;

void creds_seed_entropy(void) {
  bootloader_random_enable();
  esp_fill_random(s_seed, sizeof(s_seed));
  esp_fill_random(s_seed_salt, sizeof(s_seed_salt));
  bootloader_random_disable();
}

bool creds_ct_equal(const char *a, const char *b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  size_t n = la > lb ? la : lb;
  volatile uint8_t diff = (uint8_t) (la ^ lb);
  for (size_t i = 0; i < n; i++) {
    diff |= (uint8_t) ((i < la ? a[i] : 0) ^ (i < lb ? b[i] : 0));
  }
  return diff == 0;
}

static void load_ap_pass(void) {
  char stored[CREDS_AP_PASS_LEN + 1] = {0};
  size_t len = CREDS_AP_PASS_LEN;
  if (settings_blob_get(KEY_AP, stored, &len) == ESP_OK && len == CREDS_AP_PASS_LEN) {
    stored[CREDS_AP_PASS_LEN] = '\0';
    memcpy(s_ap_pass, stored, sizeof(s_ap_pass));
    return;
  }
  uint8_t more[SEED_LEN];
  bool made = creds_make_password(s_seed, sizeof(s_seed), s_ap_pass, CREDS_AP_PASS_LEN);
  while (!made) {
    esp_fill_random(more, sizeof(more));  // 32 rejections in a row: never in practice
    made = creds_make_password(more, sizeof(more), s_ap_pass, CREDS_AP_PASS_LEN);
  }
  esp_err_t err = settings_blob_set(KEY_AP, s_ap_pass, CREDS_AP_PASS_LEN);
  ESP_LOGI(TAG, "new setup AP password: %s", esp_err_to_name(err));
}

static void load_hash(void) {
  size_t len = sizeof(s_hash);
  esp_err_t err = settings_blob_get(KEY_CFG, &s_hash, &len);
  s_hash_set = err == ESP_OK && len == sizeof(s_hash) && s_hash.version == CREDS_HASH_VERSION &&
               s_hash.iterations == CREDS_ITERATIONS;
}

// PBKDF2, timed for GET /status.
static bool hash_matches(const char *pass) {
  int64_t t0 = esp_timer_get_time();
  bool ok = creds_hash_matches(&s_hash, pass);
  s_hash_ms = (int) ((esp_timer_get_time() - t0) / 1000);
  return ok;
}

// Stores the hash of pass with this salt; the caller holds the lock and has checked
// that none is set.
static esp_err_t store_hash(const char *pass, const uint8_t salt[CREDS_SALT_LEN]) {
  creds_hash_t h = {.iterations = CREDS_ITERATIONS};
  memcpy(h.salt, salt, CREDS_SALT_LEN);
  int64_t t0 = esp_timer_get_time();
  bool filled = creds_hash_fill(&h, pass);
  s_hash_ms = (int) ((esp_timer_get_time() - t0) / 1000);
  esp_err_t err = filled ? settings_blob_set(KEY_CFG, &h, sizeof(h)) : ESP_FAIL;
  if (err == ESP_OK) {
    s_hash = h;
    s_hash_set = true;
    strlcpy(s_verified, pass, sizeof(s_verified));
  }
  return err;
}

#ifdef CGDN1_MIGRATE
// True when home Wi-Fi credentials are saved: a device that ran an older build,
// not a new one or one just factory reset (that one sets its password at setup).
static bool wifi_saved(void) {
  nvs_handle_t h;
  if (nvs_open(FACTORY_WIFI_NS, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  size_t len = 0;
  bool saved = nvs_get_str(h, "ssid", NULL, &len) == ESP_OK && len > 1;
  nvs_close(h);
  return saved;
}

// The owner's device carries CONFIG_PASS from the build it ran before this one;
// this build stores its hash once and checks it back through the hash path. The
// salt comes from the entropy taken at boot: the RNG is only pseudo-random before
// Wi-Fi starts.
static void migrate(void) {
  if (!wifi_saved()) {
    s_migration = "skipped";
    ESP_LOGW(TAG, "migration skipped: no saved Wi-Fi");
    return;
  }
  if (!s_hash_set && creds_pass_ok(CONFIG_PASS)) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = store_hash(CONFIG_PASS, s_seed_salt);
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "migration: config password stored: %s", esp_err_to_name(err));
  }
  s_verified[0] = '\0';
  bool ok = s_hash_set && hash_matches(CONFIG_PASS);
  s_migration = ok ? "ok" : "fail";
  ESP_LOGW(TAG, "migration check: %s", s_migration);
}
#endif

void creds_init(void) {
  s_lock = xSemaphoreCreateMutex();
  load_ap_pass();
  load_hash();
  memset(s_seed, 0, sizeof(s_seed));
#ifdef CGDN1_MIGRATE
  migrate();
#endif
  memset(s_seed_salt, 0, sizeof(s_seed_salt));
}

void creds_ap_pass(char *out, size_t cap) {
  strlcpy(out, s_ap_pass, cap);
}

bool creds_config_is_set(void) {
  return s_hash_set;
}

// The client's wait slot, or -1. Called with the lock held.
static int wait_slot(uint32_t client) {
  for (int i = 0; i < CLIENT_SLOTS; i++) {
    if (s_wait[i].until_us && s_wait[i].client == client) {
      return i;
    }
  }
  return -1;
}

// Starts this client's second of waiting after a wrong try: in its own slot, else in
// the one that ends first. Called with the lock held.
static void make_wait(uint32_t client, int64_t now) {
  int slot = wait_slot(client);
  for (int i = 0; slot < 0 && i < CLIENT_SLOTS; i++) {
    if (s_wait[i].until_us < now) {
      slot = i;
    }
  }
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < CLIENT_SLOTS; i++) {
      if (s_wait[i].until_us < s_wait[slot].until_us) {
        slot = i;
      }
    }
  }
  s_wait[slot].client = client;
  s_wait[slot].until_us = now + RETRY_US;
}

// Waits, with the lock released, until a PBKDF2 run fits the budget, and books it.
// Called with the lock held; returns with it held.
static void take_hash_turn(void) {
  int64_t wait_us = s_hash_at[s_hash_next] + RETRY_US - esp_timer_get_time();
  while (s_hash_at[s_hash_next] && wait_us > 0) {
    xSemaphoreGive(s_lock);
    vTaskDelay(pdMS_TO_TICKS(wait_us / 1000) + 1);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wait_us = s_hash_at[s_hash_next] + RETRY_US - esp_timer_get_time();
  }
  s_hash_at[s_hash_next] = esp_timer_get_time();
  s_hash_next = (s_hash_next + 1) % HASHES_PER_S;
}

// What can be told without the lock or PBKDF2; CREDS_NEEDS_HASH to go on.
static creds_check_t check_input(const char *pass) {
#ifdef CGDN1_MIGRATE
  if (creds_ct_equal(pass, CONFIG_PASS)) {
    return CREDS_OK;  // the safety net while the device moves to the stored hash
  }
#endif
  if (!s_hash_set) {
    return CREDS_NOT_SET;
  }
  if (!pass[0] || strlen(pass) > CREDS_PASS_MAX) {
    return CREDS_WRONG;
  }
  return CREDS_NEEDS_HASH;
}

// What can be told from RAM; CREDS_NEEDS_HASH to go on. Called with the lock held.
static creds_check_t check_cached(const char *pass, uint32_t client) {
  if (s_verified[0] && creds_ct_equal(pass, s_verified)) {
    return CREDS_OK;
  }
  int slot = wait_slot(client);
  if (slot >= 0 && esp_timer_get_time() < s_wait[slot].until_us) {
    return CREDS_BUSY;  // this client only
  }
  return CREDS_NEEDS_HASH;
}

creds_check_t creds_config_try(const char *pass, uint32_t client) {
  creds_check_t result = check_input(pass);
  if (result != CREDS_NEEDS_HASH) {
    return result;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  result = check_cached(pass, client);
  xSemaphoreGive(s_lock);
  return result;
}

creds_check_t creds_config_check(const char *pass, uint32_t client) {
  creds_check_t result = check_input(pass);
  if (result != CREDS_NEEDS_HASH) {
    return result;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  result = check_cached(pass, client);
  if (result == CREDS_NEEDS_HASH) {
    take_hash_turn();
  }
  xSemaphoreGive(s_lock);
  if (result != CREDS_NEEDS_HASH) {
    return result;
  }
  // Without the lock: creds_config_try on the server task must not wait for PBKDF2.
  // s_hash does not change once set; only the hash task gets here.
  bool ok = hash_matches(pass);
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (ok) {
    strlcpy(s_verified, pass, sizeof(s_verified));
  } else {
    make_wait(client, esp_timer_get_time());
  }
  xSemaphoreGive(s_lock);
  return ok ? CREDS_OK : CREDS_WRONG;
}

esp_err_t creds_config_set(const char *pass) {
  if (!creds_pass_ok(pass)) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t salt[CREDS_SALT_LEN];
  esp_fill_random(salt, sizeof(salt));  // over HTTP, so Wi-Fi is up and the RNG is true
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t err = s_hash_set ? ESP_ERR_INVALID_STATE : store_hash(pass, salt);
  xSemaphoreGive(s_lock);
  return err;
}

const char *creds_migration_result(void) {
  return s_migration;
}

int creds_last_hash_ms(void) {
  return s_hash_ms;
}
