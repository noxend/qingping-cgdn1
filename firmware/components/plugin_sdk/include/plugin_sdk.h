// The plugin SDK on the device's side: the calls the WASM host (components/wasm_host)
// makes on a plugin's behalf, which the core implements (ui's sdk_impl.c, gfx_blit.c and
// screen_manager.c), and the events it hands a plugin. A plugin itself sees
// plugins/sdk/plugin_wasm.h, whose imports are thin wrappers over these.
//
// Every call is for the UI loop, inside a plugin callback: the core keys settings,
// timers and requests by the plugin whose callback is running, and answers with the
// default or does nothing elsewhere.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// ---- Drawing --------------------------------------------------------------------
// The frame is 160 x 128, 4-bit grey (0 off, 15 full). The core puts the frame on the
// panel once the plugin's draw returns.
#define GFX_W 160
#define GFX_H 128

void gfx_clear(void);
void gfx_pixel(int x, int y, uint8_t level);  // outside the frame: ignored

// Text goes through a 1-bit text layer: draw a batch, blit it at one grey level,
// start another batch for another level.
typedef enum {
  GFX_FONT_TEXT,    // 9 x 15, Cyrillic: labels, dates
  GFX_FONT_DIGITS,  // 42 px tall numerals and ":": clock faces
} gfx_font_t;

void gfx_text_begin(void);
void gfx_font(gfx_font_t font);
void gfx_text(int x, int y, const char *s);  // top-left anchored, UTF-8
void gfx_text_centered(int y, const char *s);
int gfx_text_width(const char *s);
void gfx_text_blit(uint8_t level);

// A w x h picture at x, y, clipped to the frame: rows of bpp bits per pixel (1, 2, 4 or
// 8), the first pixel in a byte's highest bits, each row starting on a new byte. A
// pixel's value picks a palette entry (1 << bpp of them): a grey level, or GFX_KEEP to
// leave the frame's pixel.
#define GFX_KEEP 255
void gfx_blit(int x, int y, int w, int h, const void *pixels, int bpp, const uint8_t *palette);

// ---- Settings ------------------------------------------------------------------
// Reads one of the calling plugin's own settings into out (def when unset). Writes
// go through the web form only, so no plugin can change another's settings.
void plugin_setting_get(const char *key, char *out, size_t cap, const char *def);

// ---- Sensors --------------------------------------------------------------------
typedef enum {
  SENSOR_TEMP,  // degrees Celsius
  SENSOR_HUM,   // relative humidity in percent
  SENSOR_CO2,   // parts per million
  SENSOR_COUNT,
} sensor_id_t;

typedef struct {
  float value;
  bool valid;  // false until the first reading, and again once it is too old
  uint32_t age_ms;
} sensor_reading_t;

// Copies the latest reading of `id` into out and returns out->valid. Never blocks.
bool plugin_sensor_get(sensor_id_t id, sensor_reading_t *out);

// ---- Events ---------------------------------------------------------------------
typedef enum {
  CORE_EV_SHOW,   // the plugin's screen became the visible one
  CORE_EV_HIDE,   // another screen replaced it
  CORE_EV_TIMER,  // a timer from core_timer_start fired (timer_id)
  CORE_EV_NET,    // a request from core_net_get finished (net)
} core_ev_type_t;

typedef struct {
  int status;     // HTTP status, 0 when there was no response
  esp_err_t err;  // transport error, ESP_OK when a response arrived
  void *parsed;   // the core's own requests only: what their parser made of the body
  char *body;     // NUL-terminated, NULL unless status is 200; valid during the callback only
  int len;
} core_net_result_t;

typedef struct {
  core_ev_type_t type;
  int timer_id;           // CORE_EV_TIMER
  core_net_result_t net;  // CORE_EV_NET
} core_event_t;

// ---- Core API --------------------------------------------------------------------

// Publishes the caller's counters, a short JSON object, for GET /plugin/stats.
void core_stats(const char *json);

// True while the station has an IP address.
bool core_online(void);

// Redraw the shown screen once the callback returns. A no-op unless the caller is
// the shown plugin, so a background result never repaints someone else.
void core_redraw(void);

// Timer `id` (the plugin's own number, 0 or 1) fires CORE_EV_TIMER every `ms`, or once.
// Starting a running timer restarts it. Neither works during a web preview's draw.
void core_timer_start(int id, uint32_t ms, bool periodic);
void core_timer_stop(int id);

// Queues an HTTPS GET; the answer comes as CORE_EV_NET to the caller. One request
// runs at a time. Reads at most max_bytes, into PSRAM. False when the queue is full,
// and during a web preview's draw.
bool core_net_get(const char *url, size_t max_bytes);
