// The plugin SDK: the calls the device lets a plugin make (module "env") and the
// functions a plugin exports: draw (required), init, on_event, on_config, led, and
// net_buffer and on_net for one that uses the network. README.md says what each one does
// and the limits the device puts on them.
//
// A module links sdk/libc_mini.c (build.sh does): memcpy, snprintf, the string calls,
// run inside the module so the instruction limit counts them.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The version of this contract. A plugin's manifest names the one it was built
// against; a device refuses a plugin built for another one.
#define WASM_SDK_VERSION 2

#define WASM_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define WASM_EXPORT(name) __attribute__((export_name(name)))

// ---- drawing ------------------------------------------------------------------------------

#define GFX_W 160
#define GFX_H 128
// A palette entry that leaves the frame's pixel as it is.
#define GFX_KEEP 255

enum { GFX_FONT_TEXT = 0, GFX_FONT_DIGITS = 1 };

WASM_IMPORT("gfx_clear") void gfx_clear(void);
WASM_IMPORT("gfx_pixel") void gfx_pixel(int x, int y, int level);
WASM_IMPORT("gfx_text_begin") void gfx_text_begin(void);
WASM_IMPORT("gfx_font") void gfx_font(int font);
WASM_IMPORT("gfx_text") void gfx_text(int x, int y, const char *s);
WASM_IMPORT("gfx_text_centered") void gfx_text_centered(int y, const char *s);
WASM_IMPORT("gfx_text_width") int gfx_text_width(const char *s);
WASM_IMPORT("gfx_text_blit") void gfx_text_blit(int level);

// A w x h picture at x, y, at most the frame's size: rows of bpp bits per pixel (1, 2, 4
// or 8), the first pixel in a byte's highest bits, each row starting on a new byte. Each
// pixel's value picks an entry of palette (1 << bpp entries): a grey level 0 to 15, or
// GFX_KEEP.
WASM_IMPORT("gfx_blit")
void gfx_blit(int x, int y, int w, int h, const void *pixels, int bpp, const uint8_t *palette);

// ---- the standard screen -----------------------------------------------------------------
// The device's screens share one layout, each part centred across the panel: a caption at
// the top (optional), the value in GFX_FONT_DIGITS, and a label under it (the unit, a date,
// "no data"). ui_caption, ui_value and ui_label draw each part in its place, font and grey
// level. Call them after gfx_clear, in any order; each is a text batch of its own.

#define UI_CAPTION_Y 8
#define UI_VALUE_Y 32
#define UI_LABEL_Y 90
#define UI_LEVEL_VALUE 15
#define UI_LEVEL_TEXT 11

// The value while there is none: no reading yet, no answer, a clock that is not set.
#define UI_NO_VALUE "--"

// GFX_FONT_TEXT has no degree sign, so text lines draw it as this ring: 5 x 5 at the
// height of a capital, then a 2-pixel gap.
#define UI_RING_W 7

static inline void ui_ring(int x, int y, int level) {
  for (int dy = 0; dy < 5; dy++) {
    for (int dx = 0; dx < 5; dx++) {
      bool edge = dx == 0 || dx == 4 || dy == 0 || dy == 4;
      bool corner = (dx == 0 || dx == 4) && (dy == 0 || dy == 4);
      if (edge && !corner) {
        gfx_pixel(x + dx, y + dy, level);
      }
    }
  }
}

// Places text at x, y in the current font, a ring for each "°", and returns the x after
// it. With draw false it only measures.
static inline int ui_text_run(int x, int y, const char *text, int level, bool draw) {
  char piece[64];
  int n = 0;
  for (const char *p = text;; p++) {
    bool degree = p[0] == '\xC2' && p[1] == '\xB0';
    if (*p && !degree && n < (int) sizeof(piece) - 1) {
      piece[n++] = *p;
      continue;
    }
    piece[n] = '\0';
    n = 0;
    if (draw) {
      gfx_text(x, y, piece);
    }
    x += gfx_text_width(piece);
    if (!degree) {
      return x;
    }
    if (draw) {
      ui_ring(x, y + 1, level);
    }
    x += UI_RING_W;
    p++;
  }
}

// A line of GFX_FONT_TEXT centred across the panel with its top at y.
static inline void ui_text_line(int y, const char *text, int level) {
  gfx_text_begin();
  gfx_font(GFX_FONT_TEXT);
  int width = ui_text_run(0, y, text, level, false);
  ui_text_run((GFX_W - width) / 2, y, text, level, true);
  gfx_text_blit(level);
}

static inline void ui_caption(const char *text) {
  ui_text_line(UI_CAPTION_Y, text, UI_LEVEL_TEXT);
}

// The value in large digits; NULL or "" shows UI_NO_VALUE. GFX_FONT_DIGITS has the digits,
// space and * + , - . / : only.
static inline void ui_value(const char *value) {
  gfx_text_begin();
  gfx_font(GFX_FONT_DIGITS);
  gfx_text_centered(UI_VALUE_Y, value && value[0] ? value : UI_NO_VALUE);
  gfx_text_blit(UI_LEVEL_VALUE);
}

static inline void ui_label(const char *text) {
  ui_text_line(UI_LABEL_Y, text, UI_LEVEL_TEXT);
}

// ---- settings, sensors, time ---------------------------------------------------------------

WASM_IMPORT("plugin_setting_get")
void plugin_setting_get(const char *key, char *out, int cap, const char *def);

enum { SENSOR_TEMP = 0, SENSOR_HUM = 1, SENSOR_CO2 = 2 };

typedef struct {
  float value;
  int32_t valid;
  uint32_t age_ms;
} wasm_sensor_t;

WASM_IMPORT("plugin_sensor_get") bool plugin_sensor_get(int id, wasm_sensor_t *out, int size);

typedef struct {
  int32_t sec, min, hour;
  int32_t mday, mon, year;  // mon 0..11, year since 1900
  int32_t wday, yday, isdst;
} wasm_tm_t;

WASM_IMPORT("core_localtime") void core_localtime(wasm_tm_t *out, int size);

// ---- events --------------------------------------------------------------------------------

enum { CORE_EV_SHOW = 0, CORE_EV_HIDE = 1, CORE_EV_TIMER = 2 };

WASM_IMPORT("core_online") bool core_online(void);
WASM_IMPORT("core_redraw") void core_redraw(void);
WASM_IMPORT("core_timer_start") void core_timer_start(int id, int ms, bool periodic);
WASM_IMPORT("core_timer_stop") void core_timer_stop(int id);

// A short JSON object with the plugin's own counters, served at
// GET /plugin/stats?id=<id>. Kept until the next call; at most 255 bytes.
WASM_IMPORT("core_stats") void core_stats(const char *json);

// ---- network -------------------------------------------------------------------------------

// An HTTPS GET to one of the hosts plugin.json lists, reading at most max_bytes (the
// device caps it at 32 KB). True when it was queued; false while the previous request is
// unanswered, and in a web preview's draw. The answer comes later: net_buffer(len) for a
// buffer of len bytes in the plugin's memory (0 when it has no room), then
// on_net(status, len).
WASM_IMPORT("core_net_get") bool core_net_get(const char *url, int max_bytes);

// ---- JSON ----------------------------------------------------------------------------------
// Reading a JSON text in the plugin's memory (js, len bytes), done by the device: an
// offset `at` points at a value, the text's own start is 0, and -1 means "not there". A
// value that is cut short or not valid JSON counts as not there, so a body cut at
// max_bytes gives nothing rather than half its list.

// The value of the member `key` of the object at `at`.
WASM_IMPORT("json_find") int json_find(const char *js, int len, int at, const char *key);
// The first element of the array at `at`, and the element after the one at `at`.
WASM_IMPORT("json_first") int json_first(const char *js, int len, int at);
WASM_IMPORT("json_next") int json_next(const char *js, int len, int at);
// The string at `at`, decoded, into out with a terminator: its length, or -1 when it is
// not a string or does not fit.
WASM_IMPORT("json_string") int json_string(const char *js, int len, int at, char *out, int cap);
// Which of a list of strings the string at `at` is, or -1. The list is the strings one
// after another, each ending in "\0", ending with an empty one: "red\0yellow\0".
WASM_IMPORT("json_enum")
int json_enum(const char *js, int len, int at, const char *list, int list_len);
