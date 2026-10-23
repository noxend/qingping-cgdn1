// The plugin SDK on the host: drawing into a byte frame, settings and sensors fed
// by the harness, the core calls as no-ops or hooks. The WASM host's bindings call it as
// they call the core's on the device.
#include <stdio.h>
#include <string.h>
#include "plugin_sdk.h"
#include "oled.h"
#include "text.h"

extern const uint8_t u8g2_font_logisoso42_tn[];
extern const uint8_t u8g2_font_9x15_t_cyrillic[];

uint8_t host_frame[GFX_H][GFX_W];

// What plugin_sensor_get answers, set per case by the harness.
static sensor_reading_t host_readings[SENSOR_COUNT];

// Setting values the harness or the preview set; any other key reads its default.
#define HOST_SETTINGS_MAX 8
static struct {
  char key[32];
  char value[64];
} host_settings[HOST_SETTINGS_MAX];
static int host_settings_n;

void host_sdk_init(void) {
  text_init();
}

void host_set_reading(sensor_id_t id, float value, bool valid) {
  host_readings[id].value = value;
  host_readings[id].valid = valid;
  host_readings[id].age_ms = 0;
}

void host_add_setting(const char *key, const char *value) {
  if (host_settings_n < HOST_SETTINGS_MAX) {
    snprintf(host_settings[host_settings_n].key, sizeof(host_settings[0].key), "%s", key);
    snprintf(host_settings[host_settings_n].value, sizeof(host_settings[0].value), "%s", value);
    host_settings_n++;
  }
}

// The one setting a test case changes, "" for none.
void host_set_setting(const char *key, const char *value) {
  host_settings_n = 0;
  if (key && key[0]) {
    host_add_setting(key, value ? value : "");
  }
}

void oled_pixel(int x, int y, uint8_t level) {
  host_frame[y][x] = level;
}

void gfx_clear(void) {
  memset(host_frame, 0, sizeof(host_frame));
}

void gfx_pixel(int x, int y, uint8_t level) {
  if (x >= 0 && x < GFX_W && y >= 0 && y < GFX_H) {
    host_frame[y][x] = level;
  }
}

void gfx_text_begin(void) {
  text_begin();
}

void gfx_font(gfx_font_t font) {
  text_font(font == GFX_FONT_DIGITS ? u8g2_font_logisoso42_tn : u8g2_font_9x15_t_cyrillic);
}

void gfx_text(int x, int y, const char *s) {
  text_draw(x, y, s);
}

void gfx_text_centered(int y, const char *s) {
  text_draw_centered(y, s);
}

int gfx_text_width(const char *s) {
  return text_width(s);
}

void gfx_text_blit(uint8_t level) {
  text_blit(level);
}

void plugin_setting_get(const char *key, char *out, size_t cap, const char *def) {
  const char *value = def ? def : "";
  for (int i = 0; i < host_settings_n; i++) {
    if (strcmp(key, host_settings[i].key) == 0) {
      value = host_settings[i].value;
    }
  }
  snprintf(out, cap, "%s", value);
}

bool plugin_sensor_get(sensor_id_t id, sensor_reading_t *out) {
  *out = host_readings[id];
  return out->valid;
}

// Whether the device has a network address; a harness sets it.
bool host_online;

bool core_online(void) {
  return host_online;
}

void core_redraw(void) {
}

// What the plugin last published with core_stats.
char host_stats[256];

void core_stats(const char *json) {
  snprintf(host_stats, sizeof(host_stats), "%s", json);
}

// The preview points this at its own record of the module's timers.
void (*host_timer_start)(int id, uint32_t ms, bool periodic);

void core_timer_start(int id, uint32_t ms, bool periodic) {
  if (host_timer_start) {
    host_timer_start(id, ms, periodic);
  }
}

void core_timer_stop(int id) {
  (void) id;
}

// A harness that tests a network plugin points this at its own fake; otherwise the
// network is down.
bool (*host_net_get)(const char *url, size_t max_bytes);

bool core_net_get(const char *url, size_t max_bytes) {
  return host_net_get && host_net_get(url, max_bytes);
}
