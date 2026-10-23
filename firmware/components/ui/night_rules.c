#include "night_rules.h"

#include <string.h>

void night_profile_default(night_profile_t *p) {
  memset(p, 0, sizeof(*p));
  p->version = NIGHT_PROFILE_VERSION;
  p->enabled = false;
  p->start_min = 22 * 60;
  p->end_min = 8 * 60;
  p->screen_pct = 30;
  p->screen_off = false;
  p->led_pct = 20;
}

bool night_profile_ok(const night_profile_t *p) {
  if (p->version != NIGHT_PROFILE_VERSION) {
    return false;
  }
  if (p->start_min >= MINUTES_PER_DAY || p->end_min >= MINUTES_PER_DAY) {
    return false;
  }
  if (p->screen_pct < 10 || p->screen_pct > 100) {
    return false;
  }
  return p->led_pct <= 100;
}

bool night_in_window(uint16_t start_min, uint16_t end_min, uint16_t now_min) {
  if (start_min == end_min) {
    return false;
  }
  if (start_min < end_min) {
    return now_min >= start_min && now_min < end_min;
  }
  return now_min >= start_min || now_min < end_min;
}

void night_levels(uint8_t pct, bool off, uint8_t out[16]) {
  if (off || pct == 0) {
    memset(out, 0, 16);
    return;
  }
  out[0] = 0;
  for (int level = 1; level < 16; level++) {
    int scaled = (level * pct + 50) / 100;
    if (scaled < 1) {
      scaled = 1;
    }
    out[level] = (uint8_t) scaled;
  }
}
