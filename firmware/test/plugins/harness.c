// Host check of the standard plugins and the device menu. Every case of cases.h runs
// the plugin's module (built by plugins/build.sh) through the device's own code:
// the manifest check, the plugin table, the SDK bindings and WAMR with the device's
// limits, drawing with the real text.c and u8g2. Each case writes its frame as
// <case>.pgm and a line "<case> <LED> <stats>" that run.sh compares with golden/. The
// menu pages are drawn and checked as before.
// Usage: harness <modules dir> <data dir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cases.h"
#include "manifest.h"
#include "menu.h"
#include "plugin_sdk.h"
#include "wasm_binary.h"
#include "wasm_module.h"
#include "wasm_plugins.h"

#define MARGIN 2

extern uint8_t host_frame[GFX_H][GFX_W];
extern bool host_online;
extern char host_stats[256];
extern bool (*host_net_get)(const char *url, size_t max_bytes);
void host_sdk_init(void);
void host_set_reading(sensor_id_t id, float value, bool valid);
void host_set_setting(const char *key, const char *value);

int host_heap_caps_frees;  // the stub heap_caps_free counts; nothing here reads it

// The clock the bindings read (core_localtime calls time()).
static long long s_now;

time_t time(time_t *t) {
  if (t) {
    *t = (time_t) s_now;
  }
  return (time_t) s_now;
}

static bool s_asked;

static bool note_request(const char *url, size_t max_bytes) {
  s_asked = true;
  return true;
}

static void write_pgm(const char *name) {
  char path[64];
  snprintf(path, sizeof(path), "%s.pgm", name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror(path);
    return;
  }
  fprintf(f, "P5\n%d %d\n255\n", GFX_W, GFX_H);
  for (int y = 0; y < GFX_H; y++) {
    for (int x = 0; x < GFX_W; x++) {
      fputc(host_frame[y][x] * 17, f);  // 0..15 to 0..255
    }
  }
  fclose(f);
}

// Lit pixels, and whether any sits inside the outer MARGIN of the frame: a menu page
// that does is clipped.
static int count_ink(bool *in_margin) {
  int ink = 0;
  *in_margin = false;
  for (int y = 0; y < GFX_H; y++) {
    for (int x = 0; x < GFX_W; x++) {
      if (host_frame[y][x] == 0) {
        continue;
      }
      ink++;
      bool edge_x = x < MARGIN || x >= GFX_W - MARGIN;
      bool edge_y = y < MARGIN || y >= GFX_H - MARGIN;
      if (edge_x || edge_y) {
        *in_margin = true;
      }
    }
  }
  return ink;
}

static char *read_file(const char *path, uint32_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(n + 1);
  if (buf && fread(buf, 1, n, f) != (size_t) n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf) {
    buf[n] = '\0';
  }
  *len = (uint32_t) n;
  return buf;
}

// The plugin's module into a new table entry, as the host loads an installed one; -1
// with why when it is refused.
static int load(const char *dir, const char *id) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.wasm", dir, id);
  uint32_t len = 0;
  uint8_t *code = (uint8_t *) read_file(path, &len);
  uint32_t text_len = 0;
  const char *text =
      code ? (const char *) wasm_custom_section(code, len, MANIFEST_SECTION, &text_len) : NULL;
  manifest_t m;
  const char *why = text ? manifest_parse(text, (int) text_len, &m) : "no module";
  int i = why ? -1 : plugins_add(id, len, "");
  if (i < 0 || !plugins_load(i, &m, text, (int) text_len, code, len, &why)) {
    fprintf(stderr, "%s: refused: %s\n", id, why ? why : "table");
    return -1;
  }
  return i;
}

// One case: the plugin shown, its network answer when it asked for one, a timer tick,
// then its frame, its LED and its stats.
static int run_case(const char *dir, const char *data, const plugin_case_t *c) {
  s_now = c->now;
  host_set_setting(c->key, c->setting);
  if (c->sensor != NO_SENSOR) {
    host_set_reading((sensor_id_t) c->sensor, c->value, c->valid);
  }
  host_online = c->body != NULL;
  host_stats[0] = '\0';
  s_asked = false;
  int i = load(dir, c->plugin);
  if (i < 0) {
    return 1;
  }
  plugins_event(i, &(core_event_t){.type = CORE_EV_SHOW});
  if (s_asked && c->body) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", data, c->body);
    uint32_t len = 0;
    char *body = read_file(path, &len);
    core_event_t ev = {.type = CORE_EV_NET};
    ev.net.status = body ? 200 : 0;
    ev.net.body = body;
    ev.net.len = (int) len;
    plugins_event(i, &ev);
    free(body);
  }
  plugins_event(i, &(core_event_t){.type = CORE_EV_TIMER, .timer_id = 1});
  plugins_draw(i);
  write_pgm(c->name);
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  bool on = plugins_led(i, &r, &g, &b);
  bool ready = plugins_ready(i);
  plugin_info_t info;
  plugins_info(i, &info);
  plugins_drop(i);
  printf("%s ", c->name);
  if (on) {
    printf("#%02X%02X%02X", r, g, b);
  } else {
    printf("off");
  }
  printf("%s%s\n", host_stats[0] ? " " : "", host_stats);
  if (!ready) {
    fprintf(stderr, "%s: stopped: %s\n", c->name, info.error);
  }
  return ready ? 0 : 1;
}

static void *heap_alloc(size_t size) {
  return calloc(1, size);
}

static size_t heap_size(void *p) {
  (void) p;
  return 0;
}

static const wasm_heap_t HEAP = {
    .alloc = heap_alloc,
    .realloc = realloc,
    .free = free,
    .size = heap_size,
};

// The row the selection bar sits on: the first frame row that is lit almost
// across (the bar), mapped onto the 20 px rows under the title; -1 without a bar.
static int bar_row(void) {
  for (int y = 0; y < GFX_H; y++) {
    int lit = 0;
    for (int x = 0; x < GFX_W; x++) {
      lit += host_frame[y][x] == 15;
    }
    if (lit > GFX_W - 10) {
      return (y + 3 - 30) / 20;  // LIST_TOP 30, ROW_H 20, bar starts 3 px above the text
    }
  }
  return -1;
}

// Draws the menu as it stands, writes the frame and applies the ink and margin
// checks (the menu has no LED); `selected` is the row the bar must be on, -1 for
// a page without a bar.
static int menu_case(const char *name, const menu_info_t *info, int selected) {
  menu_draw(info);
  write_pgm(name);
  bool in_margin = false;
  int ink = count_ink(&in_margin);
  int bar = bar_row();
  bool ok = ink > 0 && !in_margin && bar == selected;
  fprintf(stderr, "%-18s %-12s ink %5d  bar %2d (want %2d)  %s\n", name, "menu", ink, bar, selected,
          ok ? "ok" : "FAIL");
  return ok ? 0 : 1;
}

// A swipe over the strip: the finger lands at `from` and lifts at `to`.
static void menu_swipe(int from, int to) {
  menu_finger(TOUCH_PHASE_DOWN, from);
  menu_finger(TOUCH_PHASE_UP, to);
}

// The menu left alone: the setup page blanks the panel only after minutes, so a
// phone has time to scan it; any other page closes after seconds, as before.
static int idle_case(const char *what, bool has_network, bool blanks) {
  int64_t wait = menu_idle_us(has_network);
  bool ok = menu_idle_blanks(has_network) == blanks;
  if (blanks) {
    ok = ok && wait >= 60LL * 1000 * 1000;
  } else {
    ok = ok && wait <= 60LL * 1000 * 1000;
  }
  fprintf(stderr, "idle %-22s blanks %d after %lld s  %s\n", what, menu_idle_blanks(has_network),
          (long long) (wait / 1000000), ok ? "ok" : "FAIL");
  return !ok;
}

// Every page of the menu: the list with each row selected in turn, the info page
// with sample values and with nothing known, the confirm page both ways, the
// Wi-Fi page; the list is walked by swipes and the pages opened by holds, so the
// state machine is exercised, not only the drawing.
static int run_menu_cases(void) {
  menu_info_t info = {
      .version = "0.29.0",
      .ip = "192.168.10.172",
      .ssid = "Oh, hi Mark",
      .up_s = 3 * 3600 + 7 * 60,
      .ap_ssid = "CGDN1-setup-C08D",
      .ap_pass = "mwx7k2pq9tre",
      .host = "cgdn1.local",
  };
  menu_info_t unknown = {.version = "0.29.0"};
  menu_info_t offline = {
      .version = "1.0.0",
      .up_s = 75,
      .ap_ssid = "CGDN1-setup-C08D",
      .ap_pass = "mwx7k2pq9tre",
  };
  int failed = 0;
  menu_open();
  failed += menu_case("menu_list_0", &info, 0);
  menu_swipe(200, 100);  // right: next row
  failed += menu_case("menu_list_1", &info, 1);
  menu_swipe(200, 100);
  failed += menu_case("menu_list_2", &info, 2);
  menu_swipe(200, 100);
  failed += menu_case("menu_list_3", &info, 3);
  menu_swipe(100, 200);  // left: back up to row 2 ("Factory reset")
  if (menu_hold(false) != MENU_NONE) {
    fprintf(stderr, "confirm page expected after a hold on the reset row\n");
    failed++;
  }
  failed += menu_case("menu_confirm_no", &info, 0);
  menu_swipe(200, 100);
  failed += menu_case("menu_confirm_yes", &info, 1);
  if (menu_hold(false) != MENU_FACTORY_RESET) {
    fprintf(stderr, "a hold on \"Yes, reset\" must ask for the reset\n");
    failed++;
  }
  menu_open();
  menu_hold(false);  // row 0: info
  failed += menu_case("menu_info", &info, -1);
  failed += menu_case("menu_info_unknown", &unknown, -1);
  menu_hold(false);  // back to the list
  menu_swipe(200, 100);
  if (menu_hold(false) != MENU_WIFI_SETUP) {
    fprintf(stderr, "a hold on the Wi-Fi row must ask for the setup AP\n");
    failed++;
  }
  failed += menu_case("menu_wifi", &offline, -1);
  menu_swipe(200, 100);  // the hotspot as text
  failed += menu_case("menu_wifi_text", &offline, -1);
  menu_swipe(100, 200);  // and back to the QR code
  failed += menu_case("menu_wifi_qr", &offline, -1);
  menu_hold(false);  // back to the list
  if (menu_hold(true) != MENU_NONE) {
    fprintf(stderr, "online, a hold on the Wi-Fi row must ask before the hotspot\n");
    failed++;
  }
  failed += menu_case("menu_hotspot_no", &info, 0);
  if (menu_hold(true) != MENU_NONE) {
    fprintf(stderr, "\"No\" must not start the hotspot\n");
    failed++;
  }
  menu_hold(true);
  menu_swipe(200, 100);
  failed += menu_case("menu_hotspot_yes", &info, 1);
  if (menu_hold(true) != MENU_WIFI_SETUP) {
    fprintf(stderr, "\"Yes\" must start the hotspot\n");
    failed++;
  }
  failed += menu_case("menu_wifi_online", &info, -1);
  menu_close();
  menu_open_setup();
  failed += menu_case("menu_setup_boot", &offline, -1);
  failed += idle_case("setup page", false, true);
  failed += idle_case("setup page online", true, false);
  menu_swipe(200, 100);  // the hotspot as text burns in the same
  failed += idle_case("setup page as text", false, true);
  menu_swipe(100, 200);
  if (menu_hold(false) != MENU_NONE) {
    fprintf(stderr, "a hold on the setup page goes to the list\n");
    failed++;
  }
  failed += menu_case("menu_setup_list", &offline, 0);
  failed += idle_case("list with no network", false, false);
  menu_close();
  // The QR format's special characters, which the real hotspot never has.
  menu_info_t odd = {
      .ap_ssid = "a;b:c\\d",
      .ap_pass = "p,q\"r12345",
  };
  menu_open_setup();
  failed += menu_case("menu_wifi_escaped", &odd, -1);
  menu_close();
  menu_open_joined();
  failed += menu_case("menu_joined", &info, -1);
  if (menu_hold(true) != MENU_CLOSE) {
    fprintf(stderr, "a hold on the address page closes the menu\n");
    failed++;
  }
  menu_close();
  return failed;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: harness <modules dir> <data dir>\n");
    return 2;
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  host_sdk_init();
  host_net_get = note_request;
  if (!wasm_module_runtime_init(&HEAP)) {
    fprintf(stderr, "the runtime does not start\n");
    return 1;
  }
  const plugins_hooks_t hooks = {.alloc = heap_alloc, .free = free};
  plugins_init(&hooks);
  int failed = 0;
  for (int i = 0; i < CASE_COUNT; i++) {
    failed += run_case(argv[1], argv[2], &CASES[i]);
  }
  failed += run_menu_cases();
  fprintf(stderr, "%d cases failed\n", failed);
  return failed ? 1 : 0;
}
