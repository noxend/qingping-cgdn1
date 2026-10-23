// The core's imports for WebAssembly modules (module "env"): the plugin SDK calls,
// each a thin wrapper that turns module arguments into the SDK's types. The runtime
// checks every pointer against the module's memory before a wrapper runs: "$" is a
// NUL-terminated string inside it, "*~" a buffer and its length. Every wrapper does
// nothing once the call is past its time (wasm_module_in_time).
#include <string.h>
#include <time.h>
#include "json_scan.h"
#include "plugin_sdk.h"
#include "wasm_module.h"

// The structs a module passes, as plugins/sdk/plugin_wasm.h lays them out.
typedef struct {
  float value;
  int32_t valid;
  uint32_t age_ms;
} wasm_sensor_t;

typedef struct {
  int32_t sec, min, hour;
  int32_t mday, mon, year;
  int32_t wday, yday, isdst;
} wasm_tm_t;

static uint8_t level_of(int level) {
  if (level < 0) {
    return 0;
  }
  return level > 15 ? 15 : (uint8_t) level;
}

// A line of text on the 160 px panel is under 20 characters; a module's string is
// cut at TEXT_MAX bytes so one import call stays short whatever the module passes.
#define TEXT_MAX 128

static const char *bounded(const char *s, char *buf) {
  size_t n = strnlen(s, TEXT_MAX);
  if (n < TEXT_MAX) {
    return s;
  }
  memcpy(buf, s, TEXT_MAX - 1);
  buf[TEXT_MAX - 1] = '\0';
  return buf;
}

static void w_gfx_clear(wasm_exec_env_t env) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  gfx_clear();
}

static void w_gfx_pixel(wasm_exec_env_t env, int x, int y, int level) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  gfx_pixel(x, y, level_of(level));
}

static void w_gfx_text_begin(wasm_exec_env_t env) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  gfx_text_begin();
}

static void w_gfx_font(wasm_exec_env_t env, int font) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  gfx_font(font == 1 ? GFX_FONT_DIGITS : GFX_FONT_TEXT);
}

static void w_gfx_text(wasm_exec_env_t env, int x, int y, const char *s) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  char buf[TEXT_MAX];
  gfx_text(x, y, bounded(s, buf));
}

static void w_gfx_text_centered(wasm_exec_env_t env, int y, const char *s) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  char buf[TEXT_MAX];
  gfx_text_centered(y, bounded(s, buf));
}

static int w_gfx_text_width(wasm_exec_env_t env, const char *s) {
  if (!wasm_module_in_time(env)) {
    return 0;
  }
  char buf[TEXT_MAX];
  return gfx_text_width(bounded(s, buf));
}

static void w_gfx_text_blit(wasm_exec_env_t env, int level) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  gfx_text_blit(level_of(level));
}

// A module's offset and length as a native pointer, or NULL when the range is not all
// inside its memory. The check leaves an exception on the instance when it fails, which
// stops the module: it passed a range it does not own.
static const void *range(wasm_exec_env_t env, uint32_t offset, uint32_t len) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
  if (!wasm_runtime_validate_app_addr(inst, offset, len)) {
    return NULL;
  }
  return wasm_runtime_addr_app_to_native(inst, offset);
}

static void w_gfx_blit(wasm_exec_env_t env, int x, int y, int w, int h, uint32_t pixels, int bpp,
                       uint32_t palette) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  bool depth = bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8;
  if (!depth || w <= 0 || h <= 0 || w > GFX_W || h > GFX_H) {
    return;
  }
  uint32_t stride = ((uint32_t) w * (uint32_t) bpp + 7) / 8;
  const void *data = range(env, pixels, stride * (uint32_t) h);
  const void *colours = data ? range(env, palette, 1u << bpp) : NULL;
  if (colours) {
    gfx_blit(x, y, w, h, data, bpp, colours);
  }
}

static void w_setting_get(wasm_exec_env_t env, const char *key, char *out, uint32_t cap,
                          const char *def) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  if (cap > 0) {
    plugin_setting_get(key, out, cap, def);
  }
}

static int w_sensor_get(wasm_exec_env_t env, int id, uint8_t *out, uint32_t size) {
  if (!wasm_module_in_time(env)) {
    return 0;
  }
  if (size != sizeof(wasm_sensor_t)) {
    return 0;
  }
  sensor_reading_t r = {0};
  bool known = id >= 0 && id < SENSOR_COUNT;
  bool valid = known && plugin_sensor_get((sensor_id_t) id, &r);
  wasm_sensor_t reading = {.value = r.value, .valid = valid, .age_ms = r.age_ms};
  // The module's pointer need not be aligned, and the ESP32 faults on an unaligned
  // 32-bit store: out is a byte pointer, so the copy assumes no alignment.
  memcpy(out, &reading, sizeof(reading));
  return valid;
}

static int w_online(wasm_exec_env_t env) {
  if (!wasm_module_in_time(env)) {
    return 0;
  }
  return core_online();
}

static void w_redraw(wasm_exec_env_t env) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  core_redraw();
}

// A module's timers fire at most ten times a second: each one is an event on the
// UI loop's queue, and a 1 ms timer would crowd out touch and network results. Two
// timer ids per module (0 and 1).
#define MODULE_TIMER_MIN_MS 100
#define MODULE_TIMERS 2

static void w_timer_start(wasm_exec_env_t env, int id, int ms, int periodic) {
  if (!wasm_module_in_time(env) || id < 0 || id >= MODULE_TIMERS) {
    return;
  }
  uint32_t period = ms < MODULE_TIMER_MIN_MS ? MODULE_TIMER_MIN_MS : (uint32_t) ms;
  core_timer_start(id, period, periodic != 0);
}

static void w_timer_stop(wasm_exec_env_t env, int id) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  core_timer_stop(id);
}

// "https://<host>/..." with host one of the module's: the host part, compared whole,
// so "bank.gov.ua.example" is not "bank.gov.ua".
static bool url_allowed(const wasm_policy_t *p, const char *url) {
  static const char SCHEME[] = "https://";
  if (strncmp(url, SCHEME, sizeof(SCHEME) - 1) != 0) {
    return false;
  }
  const char *host = url + sizeof(SCHEME) - 1;
  size_t n = strcspn(host, "/?#:@");
  if (host[n] == ':' || host[n] == '@') {
    return false;  // no ports, no credentials
  }
  for (int i = 0; i < p->host_count; i++) {
    if (strlen(p->hosts[i]) == n && strncmp(host, p->hosts[i], n) == 0) {
      return true;
    }
  }
  return false;
}

static int w_net_get(wasm_exec_env_t env, const char *url, int max_bytes) {
  if (!wasm_module_in_time(env)) {
    return 0;
  }
  wasm_policy_t *p = wasm_runtime_get_custom_data(wasm_runtime_get_module_inst(env));
  // One request at a time per module: the core has one network worker for everyone.
  if (!p || p->net_pending > 0 || !url_allowed(p, url) || max_bytes <= 0) {
    return 0;
  }
  size_t cap = max_bytes < WASM_NET_MAX ? (size_t) max_bytes : WASM_NET_MAX;
  if (!core_net_get(url, cap)) {
    return 0;
  }
  p->net_pending++;
  return 1;
}

static void w_stats(wasm_exec_env_t env, const char *json) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  core_stats(json);
}

// The JSON reader (json_scan.h) over a text in the module's memory, which the runtime has
// checked lies inside it ("*~"). Offsets are the module's; they go back as they came.
static int w_json_find(wasm_exec_env_t env, const char *js, uint32_t len, int at, const char *key) {
  if (!wasm_module_in_time(env)) {
    return -1;
  }
  return json_find(js, (int) len, at, key);
}

static int w_json_first(wasm_exec_env_t env, const char *js, uint32_t len, int at) {
  if (!wasm_module_in_time(env)) {
    return -1;
  }
  return json_first(js, (int) len, at);
}

static int w_json_next(wasm_exec_env_t env, const char *js, uint32_t len, int at) {
  if (!wasm_module_in_time(env)) {
    return -1;
  }
  return json_next(js, (int) len, at);
}

static int w_json_string(wasm_exec_env_t env, const char *js, uint32_t len, int at, char *out,
                         uint32_t cap) {
  if (!wasm_module_in_time(env)) {
    return -1;
  }
  return json_string(js, (int) len, at, out, (int) cap);
}

static int w_json_enum(wasm_exec_env_t env, const char *js, uint32_t len, int at, const char *list,
                       uint32_t list_len) {
  if (!wasm_module_in_time(env)) {
    return -1;
  }
  return json_enum(js, (int) len, at, list, (int) list_len);
}

static void w_localtime(wasm_exec_env_t env, uint8_t *out, uint32_t size) {
  if (!wasm_module_in_time(env)) {
    return;
  }
  if (size != sizeof(wasm_tm_t)) {
    return;
  }
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  wasm_tm_t local = {
      .sec = t.tm_sec,
      .min = t.tm_min,
      .hour = t.tm_hour,
      .mday = t.tm_mday,
      .mon = t.tm_mon,
      .year = t.tm_year,
      .wday = t.tm_wday,
      .yday = t.tm_yday,
      .isdst = t.tm_isdst,
  };
  memcpy(out, &local, sizeof(local));  // unaligned, as in w_sensor_get
}

// The runtime keeps the table after registration, so it is static.
static NativeSymbol NATIVES[] = {
    {"gfx_clear", w_gfx_clear, "()", NULL},
    {"gfx_pixel", w_gfx_pixel, "(iii)", NULL},
    {"gfx_text_begin", w_gfx_text_begin, "()", NULL},
    {"gfx_font", w_gfx_font, "(i)", NULL},
    {"gfx_text", w_gfx_text, "(ii$)", NULL},
    {"gfx_text_centered", w_gfx_text_centered, "(i$)", NULL},
    {"gfx_text_width", w_gfx_text_width, "($)i", NULL},
    {"gfx_text_blit", w_gfx_text_blit, "(i)", NULL},
    {"gfx_blit", w_gfx_blit, "(iiiiiii)", NULL},
    {"plugin_setting_get", w_setting_get, "($*~$)", NULL},
    {"plugin_sensor_get", w_sensor_get, "(i*~)i", NULL},
    {"core_online", w_online, "()i", NULL},
    {"core_redraw", w_redraw, "()", NULL},
    {"core_timer_start", w_timer_start, "(iii)", NULL},
    {"core_timer_stop", w_timer_stop, "(i)", NULL},
    {"core_localtime", w_localtime, "(*~)", NULL},
    {"core_stats", w_stats, "($)", NULL},
    {"core_net_get", w_net_get, "($i)i", NULL},
    {"json_find", w_json_find, "(*~i$)i", NULL},
    {"json_first", w_json_first, "(*~i)i", NULL},
    {"json_next", w_json_next, "(*~i)i", NULL},
    {"json_string", w_json_string, "(*~i*~)i", NULL},
    {"json_enum", w_json_enum, "(*~i*~)i", NULL},
};

bool wasm_module_sdk_has(const char *name, size_t len) {
  for (size_t i = 0; i < sizeof(NATIVES) / sizeof(NATIVES[0]); i++) {
    if (strlen(NATIVES[i].symbol) == len && memcmp(NATIVES[i].symbol, name, len) == 0) {
      return true;
    }
  }
  return false;
}

bool wasm_module_register_natives(void) {
  return wasm_runtime_register_natives("env", NATIVES, sizeof(NATIVES) / sizeof(NATIVES[0]));
}
