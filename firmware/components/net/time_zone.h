// The time zones the owner picks from, each a key the API uses and the POSIX TZ string
// with its daylight-saving rules. Kept apart from net.c so a host test can check them.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// The longest key with its terminator.
#define TIME_ZONE_KEY_MAX 16

#define TIME_ZONE_DEFAULT "kyiv"

// The POSIX TZ string for a key, or NULL when the key is not one of the zones.
const char *time_zone_posix(const char *key);

// A stored record of len bytes holds a key with its terminator, and the key is a zone.
bool time_zone_record_ok(const char *record, size_t len);
