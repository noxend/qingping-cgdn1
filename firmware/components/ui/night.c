#include "night.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "carousel.h"
#include "led.h"
#include "oled.h"
#include "settings.h"

#define NIGHT_KEY "night"
// The orbit moves one pixel this often.
#define ORBIT_PERIOD_S 60
// A clock that was never set still reads 1970; the first year a sync can give.
#define FIRST_REAL_YEAR 2023

static const char *TAG = "night";

static night_profile_t s_profile;
// Set by night_store from any task, cleared by the tick on the UI loop.
static atomic_bool s_reload;
// What is on the panel now, so the tick writes the drivers only on a change.
static bool s_applied_once;
static bool s_night_on;
static int s_orbit_beats;

// The stored record, or the default when there is none or it does not pass the
// bounds. Any task: the settings store takes its own lock.
static void read_stored(night_profile_t *out) {
  night_profile_t stored;
  size_t len = sizeof(stored);
  esp_err_t e = settings_blob_get(NIGHT_KEY, &stored, &len);
  bool usable = e == ESP_OK && len == sizeof(stored) && night_profile_ok(&stored);
  if (usable) {
    *out = stored;
    return;
  }
  night_profile_default(out);
}

static void load(void) {
  read_stored(&s_profile);
}

void night_init(void) {
  load();
  atomic_store(&s_reload, true);
}

bool night_store(const night_profile_t *p) {
  if (!night_profile_ok(p)) {
    return false;
  }
  esp_err_t e = settings_blob_set(NIGHT_KEY, p, sizeof(*p));
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "profile store: %s", esp_err_to_name(e));
    return false;
  }
  atomic_store(&s_reload, true);
  return true;
}

// The local minute of the day, or -1 while the clock was never set.
static int local_minute(void) {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_year + 1900 < FIRST_REAL_YEAR) {
    return -1;
  }
  return t.tm_hour * 60 + t.tm_min;
}

static void apply(bool night_on) {
  uint8_t levels[16];
  if (night_on) {
    night_levels(s_profile.screen_pct, s_profile.screen_off, levels);
    led_set_scale(s_profile.led_pct);
  } else {
    night_levels(100, false, levels);
    led_set_scale(100);
  }
  oled_set_levels(levels);
  ESP_LOGI(TAG, "%s", night_on ? "night on" : "day");
}

void night_tick(void) {
  if (atomic_exchange(&s_reload, false)) {
    load();
    s_applied_once = false;
  }
  int minute = local_minute();
  bool night_on = s_profile.enabled && minute >= 0 &&
                  night_in_window(s_profile.start_min, s_profile.end_min, (uint16_t) minute);
  if (!s_applied_once || night_on != s_night_on) {
    s_applied_once = true;
    s_night_on = night_on;
    apply(night_on);
  }
  // The orbit runs day and night; a drag in progress streams its own frames and
  // picks the new offset up on the next one.
  if (++s_orbit_beats >= ORBIT_PERIOD_S && !carousel_busy()) {
    s_orbit_beats = 0;
    oled_orbit_step();
  }
}

// "HH:MM" for a minute of the day (the profile keeps them below 1440).
static void minutes_to_text(uint16_t minutes, char out[8]) {
  unsigned hour = (minutes / 60) % 24;
  unsigned minute = minutes % 60;
  snprintf(out, 8, "%02u:%02u", hour, minute);
}

cJSON *night_json(void) {
  // From the store, not from s_profile: the UI loop owns that copy and may be
  // rewriting it while the HTTP task is here.
  night_profile_t p;
  read_stored(&p);
  char start[8];
  char end[8];
  minutes_to_text(p.start_min, start);
  minutes_to_text(p.end_min, end);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "enabled", p.enabled);
  cJSON_AddStringToObject(o, "start", start);
  cJSON_AddStringToObject(o, "end", end);
  cJSON_AddNumberToObject(o, "screen_pct", p.screen_pct);
  cJSON_AddBoolToObject(o, "screen_off", p.screen_off);
  cJSON_AddNumberToObject(o, "led_pct", p.led_pct);
  return o;
}

// "HH:MM" to minutes of the day; false for anything else.
static bool text_to_minutes(const char *text, uint16_t *out) {
  unsigned hour = 0;
  unsigned minute = 0;
  char tail = 0;
  if (sscanf(text, "%2u:%2u%c", &hour, &minute, &tail) != 2 || strlen(text) != 5) {
    return false;
  }
  if (hour > 23 || minute > 59) {
    return false;
  }
  *out = (uint16_t) (hour * 60 + minute);
  return true;
}

static bool read_bool(const cJSON *o, const char *key, bool *out) {
  const cJSON *v = cJSON_GetObjectItem(o, key);
  if (!cJSON_IsBool(v)) {
    return false;
  }
  *out = cJSON_IsTrue(v);
  return true;
}

static bool read_pct(const cJSON *o, const char *key, uint8_t *out) {
  const cJSON *v = cJSON_GetObjectItem(o, key);
  if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 100) {
    return false;
  }
  double whole = (double) (int) v->valuedouble;
  if (whole != v->valuedouble) {
    return false;
  }
  *out = (uint8_t) whole;
  return true;
}

static bool read_time(const cJSON *o, const char *key, uint16_t *out) {
  const cJSON *v = cJSON_GetObjectItem(o, key);
  if (!cJSON_IsString(v)) {
    return false;
  }
  return text_to_minutes(v->valuestring, out);
}

bool night_from_json(const cJSON *o, night_profile_t *out) {
  if (!cJSON_IsObject(o)) {
    return false;
  }
  night_profile_t p;
  memset(&p, 0, sizeof(p));
  p.version = NIGHT_PROFILE_VERSION;
  bool ok = read_bool(o, "enabled", &p.enabled) && read_time(o, "start", &p.start_min) &&
            read_time(o, "end", &p.end_min) && read_pct(o, "screen_pct", &p.screen_pct) &&
            read_bool(o, "screen_off", &p.screen_off) && read_pct(o, "led_pct", &p.led_pct);
  if (!ok || !night_profile_ok(&p)) {
    return false;
  }
  *out = p;
  return true;
}
