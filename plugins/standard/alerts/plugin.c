// The air raid alert map: Ukraine's regions, those under alert filled, and the LED lit
// for the region chosen in the settings.
//
// NEPTUN: free, no key, per-oblast alert level and the threat type. Its one condition is
// a visible "neptun.in.ua" credit near the data (web page, README).
// https://neptun.in.ua/developers . Attribution: data (c) neptun.in.ua.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define ALERTS_URL "https://neptun.in.ua/api/v1/alerts"
#define POLL_MS (30 * 1000)
#define TIMER_POLL 1
#define BODY_MAX (32 * 1024)  // the answer is about 20 KB; the device's cap is 32 KB
// About five minutes of failed polls: stop showing alerts that may be over.
#define STALE_FAILS 10
#define LEVEL_ALERT 15  // a region under alert
#define LEVEL_EDGE 4    // a border between two regions
#define LEVEL_COAST 11  // the country's outline
// The country's outline while there is no answer to show, so the map does not read as all
// calm.
#define LEVEL_NO_DATA 4
// "loading" or "no data" goes in the corner the map leaves empty, under its western end.
#define NOTE_X 4
#define NOTE_Y 108

// One byte per pixel: the region's index, EDGE on a border between two regions (it
// belongs to neither), COAST on the country's outline, OUTSIDE beyond it
// (gen_alert_map.py).
#define EDGE 0x40
#define COAST 0x41
#define OUTSIDE 0xFF
static const uint8_t MAP[GFX_W * GFX_H] = {
#embed "map.bin"
};

#define REGION_COUNT 27

// The regions in the map's order: the setting's keys (plugin.json) and the names as the
// API spells them.
static const char *const KEYS[REGION_COUNT] = {
    "vinnytsia",    "volyn",        "dnipropetrovsk",  "donetsk",     "zhytomyr",
    "zakarpattia",  "zaporizhzhia", "ivano_frankivsk", "kyiv_oblast", "kirovohrad",
    "luhansk",      "lviv",         "mykolaiv",        "odesa",       "poltava",
    "rivne",        "sumy",         "ternopil",        "kharkiv",     "kherson",
    "khmelnytskyi", "cherkasy",     "chernivtsi",      "chernihiv",   "crimea",
    "kyiv",         "sevastopol",
};
static const char NAMES[] = "Вінницька область\0Волинська область\0Дніпропетровська область\0"
                            "Донецька область\0Житомирська область\0Закарпатська область\0"
                            "Запорізька область\0Івано-Франківська область\0Київська область\0"
                            "Кіровоградська область\0Луганська область\0Львівська область\0"
                            "Миколаївська область\0Одеська область\0Полтавська область\0"
                            "Рівненська область\0Сумська область\0Тернопільська область\0"
                            "Харківська область\0Херсонська область\0Хмельницька область\0"
                            "Черкаська область\0Чернівецька область\0Чернігівська область\0"
                            "Автономна Республіка Крим\0м. Київ\0Севастополь\0";

// The alert level per region.
#define LVL_NONE 0
#define LVL_YELLOW 1
#define LVL_RED 2
static const char LEVELS[] = "yellow\0red\0";

static uint8_t s_shown[REGION_COUNT];  // the levels drawn
static bool s_have;                    // s_shown holds an answer that is not stale
static int s_led_region = -1;
static int s_fails;
static bool s_visible;   // the map is the shown screen
static bool s_inflight;  // a fetch was asked for and has not answered yet
static char s_body[BODY_MAX];

static int s_fetch_ok;
static int s_fetch_fail;
static int s_http;
static int s_alerts;
static int s_draws;

static void publish_stats(void) {
  uint8_t lv = s_led_region >= 0 ? s_shown[s_led_region] : LVL_NONE;
  const char *level = lv == LVL_RED ? "red" : (lv == LVL_YELLOW ? "yellow" : "none");
  char json[200];
  snprintf(json, sizeof(json),
           "{\"fetch_ok\":%d,\"fetch_fail\":%d,\"http\":%d,\"alerts\":%d,\"draws\":%d,"
           "\"region\":\"%s\",\"level\":\"%s\"}",
           s_fetch_ok, s_fetch_fail, s_http, s_alerts, s_draws,
           s_led_region >= 0 ? KEYS[s_led_region] : "", level);
  core_stats(json);
}

WASM_EXPORT("on_config") void alerts_on_config(void) {
  char key[32];
  plugin_setting_get("region", key, sizeof(key), "kyiv_oblast");
  s_led_region = -1;
  for (int i = 0; i < REGION_COUNT; i++) {
    if (strcmp(KEYS[i], key) == 0) {
      s_led_region = i;
    }
  }
  publish_stats();
}

// Raises each entry's region to at least its level. `field` holds the oblast's name:
// "name" in the oblasts array (whole-oblast alerts), "oblast" in the raions array (so a
// raion alert lights its oblast).
static void merge_alerts(int len, int list, const char *field, uint8_t *level) {
  for (int e = json_first(s_body, len, list); e >= 0; e = json_next(s_body, len, e)) {
    int name = json_find(s_body, len, e, field);
    int i = json_enum(s_body, len, name, NAMES, sizeof(NAMES));
    if (i < 0) {
      continue;
    }
    int lv = json_enum(s_body, len, json_find(s_body, len, e, "level"), LEVELS, sizeof(LEVELS));
    uint8_t v = lv == 1 ? LVL_RED : LVL_YELLOW;
    if (v > level[i]) {
      level[i] = v;
    }
  }
}

// The levels from an answer of len bytes in s_body; false when it is not usable. An
// oblast is under alert if the whole oblast is listed, or any of its raions is.
static bool parse(int len, uint8_t *level) {
  int oblasts = json_find(s_body, len, 0, "oblasts");
  int raions = json_find(s_body, len, 0, "raions");
  if (oblasts < 0 && raions < 0) {
    return false;
  }
  memset(level, LVL_NONE, REGION_COUNT);
  merge_alerts(len, oblasts, "name", level);
  merge_alerts(len, raions, "oblast", level);
  return true;
}

// Applies one fetch outcome. After STALE_FAILS failures in a row (offline counts), the
// alerts are wiped and the map says it has no data, so a stale map is not shown as
// current.
static void refresh(bool ok, const uint8_t *parsed, bool online) {
  uint8_t next[REGION_COUNT];
  bool had = s_have;
  if (ok) {
    memcpy(next, parsed, sizeof(next));
    s_fetch_ok++;
    s_fails = 0;
    s_have = true;
  } else {
    if (online) {
      s_fetch_fail++;
    }
    if (++s_fails < STALE_FAILS) {
      if (!s_have && s_fails == 1) {
        core_redraw();  // "loading" becomes "no data"
      }
      publish_stats();
      return;
    }
    memset(next, LVL_NONE, sizeof(next));
    s_have = false;
  }
  bool changed = memcmp(next, s_shown, sizeof(s_shown)) != 0;
  if (changed || s_have != had) {
    core_redraw();
  }
  if (changed) {
    memcpy(s_shown, next, sizeof(s_shown));
    s_alerts = 0;
    for (int i = 0; i < REGION_COUNT; i++) {
      s_alerts += s_shown[i] != LVL_NONE;
    }
  }
  publish_stats();
}

// Asks for a fresh answer unless one is already on its way. A device that is offline,
// or a full queue, counts as a failed refresh.
static void request(void) {
  if (s_inflight) {
    return;
  }
  if (!core_online()) {
    refresh(false, NULL, false);
  } else if (core_net_get(ALERTS_URL, BODY_MAX)) {
    s_inflight = true;
  } else {
    refresh(false, NULL, true);
  }
}

WASM_EXPORT("on_event") void alerts_on_event(int type, int timer_id) {
  if (type == CORE_EV_SHOW) {
    s_visible = true;
    core_timer_start(TIMER_POLL, POLL_MS, true);
    if (core_online()) {
      request();  // showing the map is not a failed poll when offline
    }
  } else if (type == CORE_EV_HIDE) {
    s_visible = false;
    core_timer_stop(TIMER_POLL);
  } else if (type == CORE_EV_TIMER && s_visible) {
    request();  // a tick queued before HIDE must not start a fetch
  }
}

WASM_EXPORT("net_buffer") char *alerts_net_buffer(int len) {
  return len <= BODY_MAX ? s_body : 0;
}

WASM_EXPORT("on_net") void alerts_on_net(int status, int len) {
  s_inflight = false;
  s_http = status;
  uint8_t level[REGION_COUNT];
  bool ok = status == 200 && len > 0 && parse(len, level);
  refresh(ok, level, true);
}

// Regions under any alert are filled; the borders and the outline look the same whatever
// the alerts. The red and yellow difference is on the LED, not on the grey map. With no
// answer to show, a dim outline and the standard note: "loading" while the first request
// is out, else "no data".
WASM_EXPORT("draw") void alerts_draw(void) {
  uint8_t palette[256];
  memset(palette, 0, sizeof(palette));
  for (int i = 0; i < REGION_COUNT; i++) {
    palette[i] = s_shown[i] != LVL_NONE ? LEVEL_ALERT : 0;
  }
  palette[EDGE] = LEVEL_EDGE;
  palette[COAST] = s_have ? LEVEL_COAST : LEVEL_NO_DATA;
  palette[OUTSIDE] = GFX_KEEP;
  gfx_clear();
  gfx_blit(0, 0, GFX_W, GFX_H, MAP, 8, palette);
  if (!s_have) {
    gfx_text_begin();
    gfx_font(GFX_FONT_TEXT);
    gfx_text(NOTE_X, NOTE_Y, s_inflight && s_fails == 0 ? "loading" : "no data");
    gfx_text_blit(UI_LEVEL_TEXT);
  }
  s_draws++;
  publish_stats();
}

// The LED shows the alert in the chosen region: red for a red-level alert, amber for a
// yellow-level one, off when there is none.
WASM_EXPORT("led") int alerts_led(void) {
  uint8_t lv = s_led_region >= 0 ? s_shown[s_led_region] : LVL_NONE;
  if (lv == LVL_RED) {
    return 0xFF0000;
  }
  if (lv == LVL_YELLOW) {
    return 0xFF9600;
  }
  return -1;
}
