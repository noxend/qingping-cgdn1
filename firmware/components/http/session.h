// Signed-in browsers. POST /login with the config password gives a random token in
// a cookie; the device keeps only its SHA-256 and an expiry, in nvs_cfg, so a reboot
// (an update, a Wi-Fi change) does not sign anyone out. A factory reset erases them.
// Until SNTP sets the clock after a boot, the latest time the device saved plus the
// uptime stands in for it: never later than the real time, so nothing lives longer
// than it should. A session made in a boot that never had a clock ends with that boot.
//
// Only the HTTP task calls these: there is no lock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SESSION_COOKIE "cgdn1_session"
#define SESSION_SLOTS 8
#define SESSION_TOKEN_HEX 64  // 32 random bytes as hex
#define SESSION_TOKEN_BUF (SESSION_TOKEN_HEX + 1)
#define SESSION_MAX_AGE_S (30 * 24 * 3600)
// The expiry moves in steps of a day, so a session in use costs one flash write a day.
#define SESSION_EXTEND_STEP_S (24 * 3600)

// The wall clock in seconds, 0 while SNTP has not set it, and the seconds since boot.
// session_clock.c on the device; a test brings its own.
int64_t session_now(void);
int64_t session_uptime(void);

// Loads the stored sessions. Call after settings_init.
void session_init(void);

// A new session; out gets its token. The oldest session gives way when all slots
// are taken. False when the token could not be stored.
bool session_create(char out[SESSION_TOKEN_BUF]);

typedef enum {
  SESSION_NONE,      // no such session, or it expired
  SESSION_VALID,     // signed in
  SESSION_EXTENDED,  // signed in, and the expiry moved: the cookie is sent again
} session_result_t;

// Checks a token from the cookie. An expired session is removed here.
session_result_t session_check(const char *token);

// Signs one browser out. A token that is not known is not an error.
void session_remove(const char *token);
