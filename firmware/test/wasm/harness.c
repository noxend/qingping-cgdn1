// Host check of the WASM plugin path: the firmware's own wasm_module.c, wasm_bindings.c
// and plugin table on WAMR's classic interpreter (darwin build), against the host SDK of
// firmware/test/plugins. Each check prints a line and the numbers the guide quotes:
// instructions per callback of the standard plugins, the stop of an endless loop and of
// a bad pointer, and the runtime's memory (64-bit host pointers, so only a ceiling for
// the device). test/plugins checks what the standard plugins draw.
// Usage: harness <modules dir> <nbu answers dir> <alerts answer dir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "manifest.h"
#include "plugin_sdk.h"
#include "wasm_binary.h"
#include "wasm_module.h"
#include "wasm_plugins.h"

void host_sdk_init(void);
void host_set_setting(const char *key, const char *value);
void host_set_reading(sensor_id_t id, float value, bool valid);
extern uint8_t host_frame[GFX_H][GFX_W];
extern bool host_online;

// ---- a counting heap, so the runtime's memory can be read -------------------------
// Fresh memory is filled with 0xA5, as a reused heap block holds old data: whatever
// must start at zero (a module's statics) has to be zeroed by the code under test.

typedef struct {
  size_t size;
  size_t pad;  // keeps the payload 16-byte aligned
} block_t;

static size_t s_bytes;
static size_t s_peak;

static void *count_alloc(size_t size) {
  block_t *b = malloc(sizeof(block_t) + size);
  if (!b) {
    return NULL;
  }
  memset(b + 1, 0xA5, size);
  b->size = size;
  s_bytes += size;
  if (s_bytes > s_peak) {
    s_peak = s_bytes;
  }
  return b + 1;
}

static void count_free(void *p) {
  if (p) {
    block_t *b = (block_t *) p - 1;
    s_bytes -= b->size;
    free(b);
  }
}

static size_t count_size(void *p) {
  return ((block_t *) p - 1)->size;
}

static void *count_realloc(void *p, size_t size) {
  void *q = count_alloc(size);
  if (q && p) {
    size_t old = count_size(p);
    memcpy(q, p, old < size ? old : size);
  }
  if (q) {
    count_free(p);
  }
  return q;
}

static const wasm_heap_t COUNT_HEAP = {
    .alloc = count_alloc,
    .realloc = count_realloc,
    .free = count_free,
    .size = count_size,
};

// ---- helpers ------------------------------------------------------------------------

static uint8_t *read_file(const char *path, uint32_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = malloc(n);
  if (buf && fread(buf, 1, n, f) != (size_t) n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  *len = (uint32_t) n;
  return buf;
}

static double now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void write_pgm(const char *name) {
  char path[64];
  snprintf(path, sizeof(path), "%s.pgm", name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P5\n%d %d\n255\n", GFX_W, GFX_H);
  for (int y = 0; y < GFX_H; y++) {
    for (int x = 0; x < GFX_W; x++) {
      fputc(host_frame[y][x] * 17, f);
    }
  }
  fclose(f);
}

static int s_failures;

static void check(bool ok, const char *what) {
  printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    s_failures++;
  }
}

static bool load(wasm_mod_t *m, const char *dir, const char *name, uint8_t **code) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.wasm", dir, name);
  uint32_t len = 0;
  *code = read_file(path, &len);
  if (!*code) {
    printf("cannot read %s\n", path);
    return false;
  }
  if (!wasm_module_load(m, *code, len)) {
    printf("%s: %s\n", name, m->error);
    return false;
  }
  return true;
}

// ---- instructions -------------------------------------------------------------------

// The arguments a callback gets: a timer tick for on_event, an answer of s_body_len for
// on_net, none for the others.
static uint32_t s_body_len;

static bool call_with_limit(wasm_mod_t *m, wasm_fn_t fn, int limit) {
  uint32_t argv[2] = {2, 1};  // CORE_EV_TIMER, timer 1
  uint32_t argc = fn == WASM_FN_ON_EVENT ? 2 : 0;
  if (fn == WASM_FN_ON_NET) {
    argv[0] = 200;
    argv[1] = s_body_len;
    argc = 2;
  }
  m->instruction_limit = limit;
  bool ok = wasm_module_call(m, fn, argc, argv);
  m->instruction_limit = -1;
  return ok;
}

// The smallest limit a callback passes with: its instruction count, give or take one.
static int instructions_of(wasm_mod_t *m, wasm_fn_t fn) {
  int lo = 0;
  int hi = 10 * 1000 * 1000;
  if (!call_with_limit(m, fn, hi)) {
    return -1;
  }
  while (hi - lo > 1) {
    int mid = lo + (hi - lo) / 2;
    if (call_with_limit(m, fn, mid)) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return hi;
}

static char *slurp_text(const char *path);

// Instructions each callback of each standard plugin takes in a heavy case, against the
// device's limit: the clock with seconds, three digits of humidity, the alert map parsing
// the real answer of the alerts API (run.sh fetches it) and drawing the result.
static void check_instructions(const char *dir, const char *alerts_dir) {
  static const char *const NAMES[] = {"init",       "on_event", "draw", "on_config",
                                      "net_buffer", "on_net",   "led"};
  static const char *const PLUGINS[] = {"clock",  "temperature", "co2", "humidity",
                                        "alerts", "pm25",        "pm10"};
  // on_net before draw: the draw after it has the alerts to fill in.
  static const wasm_fn_t ORDER[] = {WASM_FN_INIT,     WASM_FN_ON_CONFIG, WASM_FN_ON_NET,
                                    WASM_FN_ON_EVENT, WASM_FN_DRAW,      WASM_FN_LED};
  int most = 0;
  host_set_setting("seconds", "1");
  host_set_reading(SENSOR_HUM, 100.0f, true);
  host_set_reading(SENSOR_TEMP, -40.5f, true);
  host_set_reading(SENSOR_CO2, 2500.0f, true);
  host_set_reading(SENSOR_PM25, 999.0f, true);
  host_set_reading(SENSOR_PM10, 999.0f, true);
  for (size_t p = 0; p < sizeof(PLUGINS) / sizeof(PLUGINS[0]); p++) {
    wasm_mod_t m;
    uint8_t *code = NULL;
    if (!load(&m, dir, PLUGINS[p], &code)) {
      check(false, PLUGINS[p]);
      continue;
    }
    if (m.fn[WASM_FN_NET_BUFFER]) {
      char path[256];
      snprintf(path, sizeof(path), "%s/alerts.json", alerts_dir);
      char *body = slurp_text(path);
      s_body_len = (uint32_t) strlen(body);
      uint32_t argv[1] = {s_body_len};
      wasm_module_call(&m, WASM_FN_NET_BUFFER, 1, argv);
      wasm_module_copy_in(&m, argv[0], body, s_body_len);
      free(body);
    }
    printf("  %s:", PLUGINS[p]);
    for (size_t k = 0; k < sizeof(ORDER) / sizeof(ORDER[0]); k++) {
      if (!m.fn[ORDER[k]]) {
        continue;
      }
      int n = instructions_of(&m, ORDER[k]);
      printf(" %s %d", NAMES[ORDER[k]], n);
      most = n > most ? n : most;
    }
    printf("\n");
    wasm_module_unload(&m);
    free(code);
  }
  host_set_setting("", "");
  char what[96];
  snprintf(what, sizeof(what), "every callback of the standard plugins is under half the limit");
  check(most > 0 && most < WASM_INSTRUCTION_LIMIT / 2, what);
}

static void check_hang(const char *dir) {
  wasm_mod_t m;
  uint8_t *code = NULL;
  bool loaded = load(&m, dir, "hang", &code);
  check(loaded, "hang.wasm loads");
  if (!loaded) {
    return;
  }
  m.instruction_limit = WASM_INSTRUCTION_LIMIT;
  double t0 = now_us();
  bool returned = wasm_module_call(&m, WASM_FN_DRAW, 0, NULL);
  double took = now_us() - t0;
  printf("  stopped after %.0f us on this Mac: %s\n", took, m.error);
  check(!returned && strstr(m.error, "instruction limit exceeded"),
        "an endless draw is stopped by the instruction limit");
  check(!wasm_module_call(&m, WASM_FN_DRAW, 0, NULL), "and stopped again on the next call");
  wasm_module_unload(&m);
  free(code);
}

// The module's libc (plugins/sdk/libc_mini.c) formats as the host's does.
// fmt.c makes the same calls, case by case.
static void host_format(int i, char *out) {
  switch (i) {
  case 0:
    snprintf(out, 64, "%02d:%02d:%02d", 9, 5, 0);
    break;
  case 1:
    snprintf(out, 64, "%04d-%02d-%02d", 2026, 10, 21);
    break;
  case 2:
    snprintf(out, 64, "%d|%i|%u", -42, 0, 4000000000u);
    break;
  case 3:
    snprintf(out, 64, "%5d|%-5d|%05d", -42, 7, -42);
    break;
  case 4:
    snprintf(out, 64, "%x|%X|%08x", 255, 48879, 3054);
    break;
  case 5:
    snprintf(out, 64, "%s|%.3s|%-6s|%6s", "abc", "abcdef", "ab", "cd");
    break;
  case 6:
    snprintf(out, 64, "%c%c|100%%|%d", 'o', 'k', -2147483647 - 1);
    break;
  case 7:
    snprintf(out, 6, "%s", "truncated");
    break;
  default:
    out[0] = '\0';
    break;
  }
}

static void check_format(const char *dir) {
  wasm_mod_t m;
  uint8_t *code = NULL;
  if (!load(&m, dir, "fmt", &code)) {
    check(false, "fmt.wasm loads");
    return;
  }
  wasm_function_inst_t format = wasm_runtime_lookup_function(m.inst, "format");
  int same = 0;
  const int cases = 8;
  for (int i = 0; i < cases; i++) {
    uint32_t argv[1] = {(uint32_t) i};
    char want[64];
    host_format(i, want);
    const char *got = "(call failed)";
    if (format && wasm_runtime_call_wasm(m.env, format, 1, argv)) {
      got = wasm_runtime_addr_app_to_native(m.inst, argv[0]);
    }
    if (strcmp(got, want) == 0) {
      same++;
    } else {
      printf("  case %d: module \"%s\", host \"%s\"\n", i, got, want);
    }
  }
  char what[64];
  snprintf(what, sizeof(what), "the module's snprintf matches the host's (%d of %d)", same, cases);
  check(same == cases, what);
  wasm_module_unload(&m);
  free(code);
}

static void check_slow_imports(const char *dir) {
  wasm_mod_t m;
  uint8_t *code = NULL;
  if (!load(&m, dir, "slow", &code)) {
    check(false, "slow.wasm loads");
    return;
  }
  // No instruction limit: only the time limit can stop this one.
  m.instruction_limit = -1;
  double t0 = now_us();
  bool returned = wasm_module_call(&m, WASM_FN_DRAW, 0, NULL);
  double took = now_us() - t0;
  printf("  stopped after %.0f us on this Mac: %s\n", took, m.error);
  check(!returned && strstr(m.error, "time limit exceeded") && took < WASM_CALL_TIME_US * 1.5,
        "time spent inside imports is limited too");
  wasm_module_unload(&m);
  free(code);
}

// `name` (test/wasm/modules/wat) is refused at load with this reason, and the name
// it is about when there is one: what the upload tells its author.
static void check_refused_at_load(const char *dir, const char *name, const char *reason,
                                  const char *about, const char *what) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.wasm", dir, name);
  uint32_t len = 0;
  uint8_t *code = read_file(path, &len);
  wasm_mod_t m;
  bool loaded = code && wasm_module_load(&m, code, len);
  printf("  %s\n", loaded ? "loaded" : m.error);
  bool named = !loaded && m.reason && strcmp(m.reason, reason) == 0 &&
               strcmp(m.reason_name, about ? about : "") == 0;
  check(code && named, what);
  if (loaded) {
    wasm_module_unload(&m);
  }
  free(code);
}

static void check_bad_pointer(const char *dir) {
  wasm_mod_t m;
  uint8_t *code = NULL;
  if (!load(&m, dir, "evil", &code)) {
    check(false, "evil.wasm loads");
    return;
  }
  bool returned = wasm_module_call(&m, WASM_FN_DRAW, 0, NULL);
  printf("  %s\n", m.error);
  check(!returned, "a string outside the module's memory is refused");
  wasm_module_unload(&m);
  free(code);
}

// ---- the plugin table ------------------------------------------------------------

int host_heap_caps_frees;
static int s_timers_stopped_for = -1;
static int s_timers_stopped;

static void count_timers_stop(int index) {
  s_timers_stopped_for = index;
  s_timers_stopped++;
}

static void *table_alloc(size_t size) {
  return count_alloc(size);
}

static void table_free(void *p) {
  if (p) {
    host_heap_caps_frees++;
  }
  count_free(p);
}

static uint8_t *module_code(const char *dir, const char *name, uint32_t *len) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.wasm", dir, name);
  uint32_t n = 0;
  uint8_t *file = read_file(path, &n);
  if (!file) {
    return NULL;
  }
  uint8_t *code = table_alloc(n);  // the table frees what it takes with its own free
  memcpy(code, file, n);
  free(file);
  *len = n;
  return code;
}

// The module `name` into the entry for id, as the host loads an installed plugin: the
// index, or -1 (with why printed) when it does not load.
static int load_into(const char *dir, const char *name, const char **why) {
  uint32_t len = 0;
  uint8_t *code = module_code(dir, name, &len);
  uint32_t text_len = 0;
  const char *text =
      code ? (const char *) wasm_custom_section(code, len, MANIFEST_SECTION, &text_len) : NULL;
  manifest_t m;
  *why = text ? manifest_parse(text, (int) text_len, &m) : "no section";
  if (*why) {
    table_free(code);
    printf("  %s: %s\n", name, *why);
    return -1;
  }
  int i = plugins_add(m.id, len, "");
  if (!plugins_load(i, &m, text, (int) text_len, code, len, why)) {
    printf("  %s does not load: %s\n", name, *why);
  }
  return i;
}

static void check_table(const char *dir) {
  const plugins_hooks_t hooks = {
      .timers_stop = count_timers_stop,
      .alloc = table_alloc,
      .free = table_free,
  };
  plugins_init(&hooks);
  size_t runtime = s_bytes;
  const char *why = NULL;
  int clock = load_into(dir, "clock", &why);
  plugin_info_t info;
  plugins_info(clock, &info);
  check(clock >= 0 && plugins_ready(clock) && plugins_find("clock") == clock &&
            strcmp(info.name, "Clock") == 0 && info.pages == 1,
        "a module in the table is a plugin with its id and English name");
  int len = 0;
  char *manifest = plugins_manifest(clock, &len);
  check(manifest && manifest[0] == '{' && (int) strlen(manifest) == len,
        "and its manifest's text, for the web page");
  plugins_free(manifest);

  int hang = load_into(dir, "hang", &why);
  check(hang >= 0 && hang != clock && plugins_count() == 2, "a second plugin takes a new index");
  plugins_draw(hang);
  plugins_info(hang, &info);
  check(info.state == PLUGIN_STOPPED && strstr(info.error, "limit exceeded") && plugins_used(hang),
        "a module that never returns is stopped and stays listed as stopped");

  int frees = host_heap_caps_frees;
  s_timers_stopped = 0;
  plugins_drop(clock);
  check(!plugins_used(clock) && plugins_find("clock") < 0, "a dropped plugin gives up its index");
  check(s_timers_stopped == 1 && s_timers_stopped_for == clock,
        "dropping stops the timers the plugin started");
  check(host_heap_caps_frees >= frees + 2, "and frees its code, its manifest and its entry");
  int again = load_into(dir, "clock", &why);
  check(again == clock && plugins_count() == 2, "the next plugin takes the free index");

  uint32_t start_len = 0;
  uint8_t *start = module_code(dir, "start", &start_len);
  manifest_t m = {.name = "Start"};
  snprintf(m.id, sizeof(m.id), "start");
  int i = plugins_add("start", start_len, "");
  frees = host_heap_caps_frees;
  bool bad = plugins_load(i, &m, "{}", 2, start, start_len, &why);
  plugins_info(i, &info);
  check(!bad && strcmp(why, "start") == 0 && info.state == PLUGIN_FAILED &&
            host_heap_caps_frees > frees,
        "a module that does not load is listed as failed, its code freed");
  plugins_drop(i);
  plugins_drop(hang);
  plugins_drop(again);
  check(s_bytes == runtime, "after all that the table holds no module memory");
}

// ---- bad arguments to the new calls -------------------------------------------------

static void check_stopped(const char *dir, const char *name, const char *what) {
  const char *why = NULL;
  int i = load_into(dir, name, &why);
  plugins_draw(i);
  plugin_info_t info;
  plugins_info(i, &info);
  printf("  %s\n", info.error);
  check(info.state == PLUGIN_STOPPED, what);
  plugins_drop(i);
}

// ---- the network ------------------------------------------------------------------

extern bool (*host_net_get)(const char *url, size_t max_bytes);
static int s_net_calls;
static char s_net_url[200];
static size_t s_net_max;

static bool fake_net_get(const char *url, size_t max_bytes) {
  s_net_calls++;
  snprintf(s_net_url, sizeof(s_net_url), "%s", url);
  s_net_max = max_bytes;
  return true;
}

// The rows the large digits take, drawn natively with text, against the frame. RATE_Y is
// UI_VALUE_Y of plugins/sdk/plugin_wasm.h.
#define RATE_Y 32
static bool rate_row_shows(const char *text) {
  static uint8_t drawn[GFX_H][GFX_W];
  memcpy(drawn, host_frame, sizeof(drawn));
  gfx_clear();
  gfx_text_begin();
  gfx_font(GFX_FONT_DIGITS);
  gfx_text_centered(RATE_Y, text);
  gfx_text_blit(15);
  bool same = memcmp(drawn[RATE_Y], host_frame[RATE_Y], (size_t) 44 * GFX_W) == 0;
  memcpy(host_frame, drawn, sizeof(drawn));
  return same;
}

static void deliver(int i, int status, const char *body) {
  core_event_t ev = {.type = CORE_EV_NET};
  ev.net.status = status;
  ev.net.body = (char *) body;
  ev.net.len = body ? (int) strlen(body) : 0;
  plugins_event(i, &ev);
}

static char *slurp_text(const char *path) {
  uint32_t len = 0;
  char *text = (char *) read_file(path, &len);
  if (text) {
    text = realloc(text, len + 1);
    text[len] = '\0';
  }
  return text;
}

// nbu: the National Bank's real answers (run.sh fetches them) and the rate Python rounds
// from the same answer.
static void check_net(const char *dir, const char *nbu_dir) {
  host_net_get = fake_net_get;
  const char *why = NULL;
  int evil = load_into(dir, "netevil", &why);
  s_net_calls = 0;
  plugins_event(evil, &(core_event_t){.type = CORE_EV_SHOW});
  check(s_net_calls == 1 && strcmp(s_net_url, "https://bank.gov.ua/allowed") == 0,
        "a module reaches only https on the hosts its manifest names");
  plugins_drop(evil);

  char path[256];
  snprintf(path, sizeof(path), "%s/usd.json", nbu_dir);
  char *usd = slurp_text(path);
  snprintf(path, sizeof(path), "%s/eur.json", nbu_dir);
  char *eur = slurp_text(path);
  snprintf(path, sizeof(path), "%s/expected.txt", nbu_dir);
  char *expected = slurp_text(path);
  expected[strcspn(expected, "\n")] = '\0';

  host_set_setting("", "");
  int nbu = load_into(dir, "nbu", &why);
  s_net_calls = 0;
  plugins_event(nbu, &(core_event_t){.type = CORE_EV_SHOW});
  const char *want_url =
      "https://bank.gov.ua/NBUStatService/v1/statdirectory/exchange?valcode=USD&json";
  check(s_net_calls == 1 && strcmp(s_net_url, want_url) == 0 && s_net_max <= WASM_NET_MAX,
        "the rate plugin asks the National Bank for the dollar when shown");
  plugins_event(nbu, &(core_event_t){.type = CORE_EV_TIMER, .timer_id = 1});
  check(s_net_calls == 1, "a module has one request at a time");
  deliver(nbu, 200, usd);
  plugins_draw(nbu);
  write_pgm("nbu_usd");
  printf("  the National Bank says %s\n", expected);
  check(rate_row_shows(expected), "and shows the rate from its answer, rounded to kopecks");
  check(s_net_calls == 2, "the fetch wanted meanwhile goes out once the answer is in");
  deliver(nbu, 200, usd);

  deliver(nbu, 200, eur);  // processed, it would blank the rate (another currency)
  plugins_draw(nbu);
  check(rate_row_shows(expected), "an answer it did not ask for changes nothing");

  plugins_event(nbu, &(core_event_t){.type = CORE_EV_TIMER, .timer_id = 1});
  deliver(nbu, 200, eur);
  plugins_draw(nbu);
  check(rate_row_shows("--"), "an answer for another currency is not shown as this one");

  plugins_event(nbu, &(core_event_t){.type = CORE_EV_TIMER, .timer_id = 1});
  deliver(nbu, 503, NULL);
  plugins_draw(nbu);
  check(rate_row_shows("--"), "a failed request shows no rate");
  check(plugins_ready(nbu), "and the plugin keeps running");

  host_set_setting("currency", "eur");
  plugins_config(nbu);
  plugins_event(nbu, &(core_event_t){.type = CORE_EV_TIMER, .timer_id = 1});
  check(strstr(s_net_url, "valcode=EUR") != NULL, "the setting's key \"eur\" asks for EUR");
  host_set_setting("", "");
  plugins_drop(nbu);

  int bad = load_into(dir, "netbad", &why);
  s_net_calls = 0;
  plugins_event(bad, &(core_event_t){.type = CORE_EV_SHOW});
  deliver(bad, 200, usd);
  check(s_net_calls == 2 && strcmp(s_net_url, "https://bank.gov.ua/again") == 0,
        "a buffer outside the module's memory gets no body, the module is told so");
  deliver(bad, 200, usd);
  check(!plugins_ready(bad), "a module that traps in net_buffer is stopped");
  plugins_drop(bad);
  host_net_get = NULL;
  free(usd);
  free(eur);
  free(expected);
}

// The runtime rewrites the buffer it loads from: a module is loaded from a buffer
// once, and the install job's trial load runs on a copy.
static void check_buffer_rewrite(const char *dir) {
  uint32_t len = 0;
  char path[256];
  snprintf(path, sizeof(path), "%s/clock.wasm", dir);
  uint8_t *code = read_file(path, &len);
  uint8_t *pristine = malloc(len);
  memcpy(pristine, code, len);
  wasm_mod_t first;
  wasm_mod_t second;
  bool once = wasm_module_load(&first, code, len);
  bool changed = memcmp(code, pristine, len) != 0;
  bool twice = wasm_module_load(&second, code, len);
  printf("  a second load from the same buffer: %s\n", twice ? "loads" : second.error);
  check(once && changed && !twice, "a load rewrites its buffer and a second load from it fails");
  if (twice) {
    wasm_module_unload(&second);
  }
  wasm_module_unload(&first);

  uint8_t *scratch = malloc(len);
  memcpy(scratch, pristine, len);
  uint8_t *kept = malloc(len);
  memcpy(kept, pristine, len);
  wasm_refusal_t why;
  bool trial = wasm_module_trial(scratch, len, &why);
  wasm_mod_t real;
  bool loads = wasm_module_load(&real, kept, len);
  check(trial && loads, "after a trial on a copy, the kept bytes still load");
  if (loads) {
    wasm_module_unload(&real);
  }
  free(code);
  free(pristine);
  free(scratch);
  free(kept);
}

static void check_truncated(const char *dir) {
  char path[256];
  snprintf(path, sizeof(path), "%s/clock.wasm", dir);
  uint32_t len = 0;
  uint8_t *code = read_file(path, &len);
  wasm_mod_t m;
  bool loaded = code && wasm_module_load(&m, code, len / 2);
  printf("  %s\n", loaded ? "loaded" : m.error);
  check(code && !loaded, "half a module is refused at load");
  if (loaded) {
    wasm_module_unload(&m);
  }
  free(code);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  const char *nbu_dir = argc > 2 ? argv[2] : ".";
  const char *alerts_dir = argc > 3 ? argv[3] : ".";
  host_sdk_init();
  if (!wasm_module_runtime_init(&COUNT_HEAP)) {
    printf("runtime init failed\n");
    return 1;
  }
  size_t runtime = s_bytes;

  wasm_mod_t clock;
  uint8_t *code = NULL;
  bool loaded = load(&clock, dir, "clock", &code);
  check(loaded, "clock.wasm loads");
  if (!loaded) {
    return 1;
  }
  size_t after_load = s_bytes;
  check(wasm_module_call(&clock, WASM_FN_INIT, 0, NULL), "init runs");
  printf("  runtime %zu B, one clock instance %zu B (64-bit host), peak %zu B\n", runtime,
         after_load - runtime, s_peak);
  wasm_module_unload(&clock);
  free(code);

  check_instructions(dir, alerts_dir);
  check_hang(dir);
  check_format(dir);
  check_slow_imports(dir);
  check_refused_at_load(dir, "start", "start", NULL, "a module with a start function: start");
  check_refused_at_load(dir, "postinst", "autorun", "__post_instantiate",
                        "a module exporting __post_instantiate: autorun, named");
  check_refused_at_load(dir, "badtype", "export_type", "net_buffer",
                        "an export with more results than the core reads: export_type, named");
  check_refused_at_load(dir, "drawret", "export_type", "draw",
                        "a draw that returns a value: export_type, named");
  check_refused_at_load(dir, "badled", "export_type", "led",
                        "an led that takes an argument: export_type, named");
  check_refused_at_load(dir, "import", "import", "gfx_line",
                        "a call the SDK does not have: import, named");
  check_refused_at_load(dir, "growable", "pages", NULL, "memory with no maximum: pages");
  check_refused_at_load(dir, "table", "table", NULL, "a table of 1,500,000 entries: table");
  uint32_t len = 0;
  char path[256];
  snprintf(path, sizeof(path), "%s/twopages.wasm", dir);
  uint8_t *two = read_file(path, &len);
  wasm_mod_t m;
  bool two_loads = two && wasm_module_load(&m, two, len);
  check(two_loads && wasm_module_pages(two, len) == 2,
        "two pages that cannot grow load, and the host sees the two");
  if (two_loads) {
    wasm_module_unload(&m);
  }
  free(two);
  check_bad_pointer(dir);
  check_truncated(dir);
  check_table(dir);
  check_stopped(dir, "blitbad", "a picture outside the module's memory stops it");
  check_stopped(dir, "jsonbad", "a JSON text running past the module's memory stops it");
  check_buffer_rewrite(dir);
  check_net(dir, nbu_dir);

  check(s_bytes == runtime, "every module gave its memory back");
  wasm_runtime_destroy();
  printf("%d failed\n", s_failures);
  return s_failures ? 1 : 0;
}
