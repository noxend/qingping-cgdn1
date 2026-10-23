// The time and date in large type. The device keeps the clock set (time zone and NTP);
// this plugin only formats what core_localtime says.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_TICK 1
#define SECONDS_GAP 4  // between the minutes and the smaller seconds
// From the digits' top to the seconds' top: the digits are 42 px tall, the text's
// capitals 10, so their bottoms meet.
#define SECONDS_DROP 32

static bool s_12h = false;
static int s_date_mode = 0;  // 0 dd.mm.yyyy, 1 yyyy-mm-dd, 2 mm/dd/yyyy
static bool s_seconds = false;

// What the screen shows. The sizes fit the compiler's worst-case %d width, not the real
// 2, 5 or 10 characters.
typedef struct {
  char time[40];     // "HH:MM", or "" while the clock is not set
  char seconds[16];  // "SS", or "" when the setting is off
  char ampm[3];      // "AM" / "PM" / ""
  char date[40];     // "DD.MM.YYYY", or what the label says instead
} clock_text_t;

static clock_text_t s_shown;  // for the tick's change check

WASM_EXPORT("on_config") void clk_on_config(void) {
  char v[32];
  plugin_setting_get("time_format", v, sizeof(v), "24h");
  s_12h = strcmp(v, "12h") == 0;
  plugin_setting_get("date_format", v, sizeof(v), "dmy");
  s_date_mode = strcmp(v, "ymd") == 0 ? 1 : (strcmp(v, "mdy") == 0 ? 2 : 0);
  plugin_setting_get("seconds", v, sizeof(v), "0");
  s_seconds = v[0] == '1';
}

WASM_EXPORT("init") void clk_init(void) {
  clk_on_config();
}

// Formats the current time and date per the settings. Used by both the tick (for
// change detection) and draw (so a preview without a tick still shows the time). A clock
// that was never set still says 1970.
static void format_now(clock_text_t *out) {
  wasm_tm_t t;
  core_localtime(&t, sizeof(t));
  memset(out, 0, sizeof(*out));
  if (t.year + 1900 < 2023) {
    strcpy(out->date, "time not set");
    return;
  }
  int hour = t.hour;
  if (s_12h) {
    strcpy(out->ampm, hour < 12 ? "AM" : "PM");
    hour = hour % 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  snprintf(out->time, sizeof(out->time), "%02d:%02d", hour, t.min);
  if (s_seconds) {
    snprintf(out->seconds, sizeof(out->seconds), "%02d", t.sec);
  }
  int y = t.year + 1900, m = t.mon + 1, d = t.mday;
  if (s_date_mode == 1) {
    snprintf(out->date, sizeof(out->date), "%04d-%02d-%02d", y, m, d);
  } else if (s_date_mode == 2) {
    snprintf(out->date, sizeof(out->date), "%02d/%02d/%04d", m, d, y);
  } else {
    snprintf(out->date, sizeof(out->date), "%02d.%02d.%04d", d, m, y);
  }
}

// NTP syncs in the background and the RTC drives the display, so the clock only
// needs its own once-a-second tick, offline too.
WASM_EXPORT("on_event") void clk_on_event(int type, int timer_id) {
  switch (type) {
  case CORE_EV_SHOW:
    core_timer_start(TIMER_TICK, 1000, true);
    break;
  case CORE_EV_HIDE:
    core_timer_stop(TIMER_TICK);
    break;
  case CORE_EV_TIMER: {
    clock_text_t now;
    format_now(&now);
    if (memcmp(&now, &s_shown, sizeof(now)) != 0) {
      s_shown = now;
      core_redraw();
    }
    break;
  }
  default:
    break;
  }
}

// HH:MM:SS in the large digits is about 180 px wide, more than the panel: the seconds follow
// the minutes in the text font, on the digits' baseline, and the two are centred
// together.
static void draw_time_with_seconds(const char *time, const char *seconds) {
  gfx_text_begin();
  gfx_font(GFX_FONT_TEXT);
  int seconds_width = gfx_text_width(seconds);
  gfx_text_begin();
  gfx_font(GFX_FONT_DIGITS);
  int time_width = gfx_text_width(time);
  int x = (GFX_W - (time_width + SECONDS_GAP + seconds_width)) / 2;
  gfx_text(x, UI_VALUE_Y, time);
  gfx_text_blit(UI_LEVEL_VALUE);

  gfx_text_begin();
  gfx_font(GFX_FONT_TEXT);
  gfx_text(x + time_width + SECONDS_GAP, UI_VALUE_Y + SECONDS_DROP, seconds);
  gfx_text_blit(UI_LEVEL_TEXT);
}

// Formats fresh into a local copy: s_shown belongs to the tick's change check.
WASM_EXPORT("draw") void clk_draw(void) {
  clock_text_t now;
  format_now(&now);
  gfx_clear();
  if (now.ampm[0]) {
    ui_caption(now.ampm);
  }
  if (now.seconds[0]) {
    draw_time_with_seconds(now.time, now.seconds);
  } else {
    ui_value(now.time);
  }
  ui_label(now.date);
}
