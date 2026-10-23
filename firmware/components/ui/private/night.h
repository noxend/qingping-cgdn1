// The night profile on the device: stored in the core settings, read each second
// on the UI loop, applied to the panel's level table and the indicator's scale.
#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "night_rules.h"

// Reads the stored profile (the default when there is none) and applies it on the
// next tick. UI loop, at start.
void night_init(void);

// Once a second on the UI loop: evaluates the window against local time, applies
// a change to the panel and the LED, and steps the burn-in orbit once a minute.
// Nothing is applied while the clock has never been set.
void night_tick(void);

// The profile as the API shows it: {"enabled","start","end","screen_pct",
// "screen_off","led_pct"}, times as "HH:MM". The caller deletes it.
cJSON *night_json(void);

// Reads a profile from the API's shape; false when a field is missing or out of
// range. Nothing is stored.
bool night_from_json(const cJSON *o, night_profile_t *out);

// Stores a profile; the UI loop applies it within a second. Any task.
bool night_store(const night_profile_t *p);
