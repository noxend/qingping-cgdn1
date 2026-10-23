// The device's own credentials, kept in nvs_cfg and never in the image: the setup
// AP's password (generated on the device) and the config password (chosen by the
// user on the setup page, stored as a PBKDF2 hash). A factory reset erases both.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "creds_rules.h"  // creds_pass_ok, for the setup form

#define CREDS_AP_PASS_LEN 12
#define CREDS_PASS_MAX 63

// Takes entropy for the AP password while the RNG has a physical source. Call it
// first in app_main: before Wi-Fi the RNG is only pseudo-random, and the SAR ADC
// this borrows must be released before touch, ADC or Wi-Fi start.
void creds_seed_entropy(void);

// After settings_init: loads the AP password, generating and storing one when
// there is none.
void creds_init(void);

// The AP password, NUL-terminated (cap > CREDS_AP_PASS_LEN).
void creds_ap_pass(char *out, size_t cap);

bool creds_config_is_set(void);

typedef enum {
  CREDS_OK,
  CREDS_WRONG,
  CREDS_NOT_SET,
  CREDS_BUSY,        // this client tried a wrong password less than a second ago
  CREDS_NEEDS_HASH,  // only PBKDF2 can tell: creds_config_check, off the server task
} creds_check_t;

// Every check that needs no PBKDF2: the right password once it has matched, a client
// still waiting after a wrong one, no password set, an empty or too long one. Anything
// else is CREDS_NEEDS_HASH. Takes microseconds, for the server task.
creds_check_t creds_config_try(const char *pass, uint32_t client);

// creds_config_try, then PBKDF2 when it could not tell (0.48 s on this chip): for the
// hash task (pass_queue.c), never the server task. `client` is the caller's IPv4
// address (0 when unknown): a wrong try blocks that client for a second, not
// everyone. Past four PBKDF2 runs a second, all clients together, a check waits its
// turn (the caller's task sleeps) instead of failing.
creds_check_t creds_config_check(const char *pass, uint32_t client);

// Stores the config password; only while none is set (a factory reset clears it).
// ESP_ERR_INVALID_STATE when one is set, ESP_ERR_INVALID_ARG for a bad length.
esp_err_t creds_config_set(const char *pass);

// The migration build's result: "ok", "fail", "skipped" (no saved Wi-Fi, so a new
// device or a factory reset), or NULL in any other build.
const char *creds_migration_result(void);

// How long the last PBKDF2 took on this chip, in ms (-1 before the first).
int creds_last_hash_ms(void);

// Constant-time string equality: scans the longer length, no early exit on content
// or length, so it leaks neither the secret nor its size by timing.
bool creds_ct_equal(const char *a, const char *b);
