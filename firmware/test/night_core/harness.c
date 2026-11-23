// Host check of night.c (run.sh builds it): what reaches the panel's level table when
// one of the core's own pages is shown during a night that turns the screen off.
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "night.h"
#include "settings.h"

static int s_failed;

static void check(bool ok, const char *what) {
  printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    s_failed++;
  }
}

// ---- what night.c drives -------------------------------------------------------------

static uint8_t s_levels[16];
static night_profile_t s_stored;

void oled_set_levels(const uint8_t levels[16]) {
  memcpy(s_levels, levels, sizeof(s_levels));
}

void oled_orbit_step(void) {
}

void led_set_scale(uint8_t percent) {
  (void) percent;
}

bool carousel_busy(void) {
  return false;
}

esp_err_t settings_blob_get(const char *key, void *out, size_t *len) {
  (void) key;
  memcpy(out, &s_stored, sizeof(s_stored));
  *len = sizeof(s_stored);
  return ESP_OK;
}

esp_err_t settings_blob_set(const char *key, const void *data, size_t len) {
  (void) key;
  memcpy(&s_stored, data, len);
  return ESP_OK;
}

const char *esp_err_to_name(esp_err_t e) {
  (void) e;
  return "";
}

static bool panel_dark(void) {
  for (int level = 0; level < 16; level++) {
    if (s_levels[level] != 0) {
      return false;
    }
  }
  return true;
}

// A profile whose window holds the current minute.
static night_profile_t night_now(bool screen_off) {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  int minute = t.tm_hour * 60 + t.tm_min;
  night_profile_t p;
  night_profile_default(&p);
  p.enabled = true;
  p.start_min = (uint16_t) ((minute + 1440 - 5) % 1440);
  p.end_min = (uint16_t) ((minute + 5) % 1440);
  p.screen_pct = 40;
  p.screen_off = screen_off;
  return p;
}

int main(void) {
  s_stored = night_now(true);
  check(night_profile_ok(&s_stored), "the test's profile is one the device accepts");
  night_init();
  night_tick();
  check(panel_dark(), "a night with the screen off blanks the panel");

  night_core_page(true);
  check(!panel_dark() && s_levels[15] == 6, "a core page shows at the night's brightness");
  night_tick();
  check(!panel_dark(), "and stays through the next tick");
  night_core_page(false);
  check(panel_dark(), "the panel goes dark again when the page closes");

  s_stored = night_now(false);
  night_store(&s_stored);
  night_tick();
  uint8_t dimmed[16];
  memcpy(dimmed, s_levels, sizeof(dimmed));
  night_core_page(true);
  check(memcmp(dimmed, s_levels, sizeof(dimmed)) == 0, "a dimmed night is the same with a page");
  night_core_page(false);

  printf("%d failed\n", s_failed);
  return s_failed ? 1 : 0;
}
