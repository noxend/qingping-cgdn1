// The night profile and the rules that read it: a window of local time, a screen
// brightness or off, an indicator brightness. Pure C with no ESP-IDF include, so
// the host test can compile it; night.c applies the result on the device.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NIGHT_PROFILE_VERSION 1
#define MINUTES_PER_DAY 1440

// Stored as one record in the core settings (settings_blob_*, key "night").
typedef struct {
  uint8_t version;
  bool enabled;
  uint16_t start_min;  // minutes since midnight, local time
  uint16_t end_min;
  uint8_t screen_pct;  // brightness while the night is on, 10..100
  bool screen_off;     // the screen goes dark instead of dim
  uint8_t led_pct;     // indicator brightness while the night is on, 0..100
} night_profile_t;

// Off, 22:00 to 08:00, screen 30 %, indicator 20 %.
void night_profile_default(night_profile_t *p);
// Every field in range, the version ours.
bool night_profile_ok(const night_profile_t *p);

// Whether `now_min` lies in the window [start, end) of a day; a window whose end
// comes before its start runs through midnight. Start equal to end is never.
bool night_in_window(uint16_t start_min, uint16_t end_min, uint16_t now_min);

// The panel's level table for a brightness: level 15 maps to pct of itself and the
// others in proportion, a lit level never below 1 so faint grey stays visible. Off
// or 0 % gives all zero.
void night_levels(uint8_t pct, bool off, uint8_t out[16]);
