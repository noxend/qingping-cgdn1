// Runs a plugin on this computer as the device would, and writes its screens as
// frames: preview.sh builds it and turns the frames into PNG files. The module goes
// through the device's own code: the plugin.json rules, the loader's checks, the plugin
// table that calls it under the same instruction and time limits, and the same drawing
// and fonts. Its network requests are made for real with curl, to the hosts
// its plugin.json allows.
//
// Usage: preview <module.wasm> <frames-dir> [setting=value ...]
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "manifest.h"
#include "plugin_sdk.h"
#include "wasm_binary.h"
#include "wasm_module.h"
#include "wasm_plugins.h"

void host_sdk_init(void);
void host_add_setting(const char *key, const char *value);
void host_set_reading(sensor_id_t id, float value, bool valid);
extern uint8_t host_frame[GFX_H][GFX_W];
extern bool (*host_net_get)(const char *url, size_t max_bytes);
extern void (*host_timer_start)(int id, uint32_t ms, bool periodic);
extern bool host_online;

int host_heap_caps_frees;  // the stub heap_caps_free counts; nothing here reads it

// What the plugin asked for during its last callback.
static char s_url[256];
static size_t s_url_max;
static int s_timers[2] = {-1, -1};  // period in ms of timer 0 and 1, -1 when not started

static bool note_request(const char *url, size_t max_bytes) {
  snprintf(s_url, sizeof(s_url), "%s", url);
  s_url_max = max_bytes;
  return true;
}

static void note_timer(int id, uint32_t ms, bool periodic) {
  if (id >= 0 && id < 2) {
    s_timers[id] = (int) ms;
  }
  printf("  timer %d started: every %u ms%s\n", id, ms, periodic ? "" : ", once");
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

static uint8_t *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = malloc(n + 1);
  if (buf && fread(buf, 1, n, f) != (size_t) n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  *len = (size_t) n;
  return buf;
}

static void write_frame(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.pgm", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  fprintf(f, "P5\n%d %d\n255\n", GFX_W, GFX_H);
  for (int y = 0; y < GFX_H; y++) {
    for (int x = 0; x < GFX_W; x++) {
      fputc(host_frame[y][x] * 17, f);
    }
  }
  fclose(f);
  printf("  frame %s\n", name);
}

// False, after saying why, when the plugin has been stopped.
static bool still_running(int i) {
  plugin_info_t info;
  plugins_info(i, &info);
  if (info.state != PLUGIN_READY) {
    printf("stopped: %s\n", info.error);
    printf("The device would show \"stopped\" in its place. See \"When it goes wrong\" in "
           "plugins/README.md.\n");
    return false;
  }
  return true;
}

extern char **environ;

// Fetches url with curl into body.tmp; the HTTP status, 0 when the request failed. No
// shell: the URL is the plugin's own text, and only its host has been checked. -q first,
// so no ~/.curlrc adds redirects the device would not follow; time and size are bounded
// so a host that hangs or never stops does not stall the preview.
static int fetch(const char *url) {
  char *const argv[] = {
      "curl",         "-q",       "-s",
      "--globoff",    "--proto",  "=https",
      "--max-time",   "20",       "--max-filesize",
      "1048576",      "-A",       "cgdn1-preview",
      "-o",           "body.tmp", "-w",
      "%{http_code}", "--",       (char *) url,
      NULL,
  };
  int out[2];
  if (pipe(out) != 0) {
    return 0;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, out[1], 1);
  posix_spawn_file_actions_addclose(&actions, out[0]);
  pid_t pid;
  int spawned = posix_spawnp(&pid, "curl", &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(out[1]);
  int status = 0;
  FILE *f = fdopen(out[0], "r");
  if (spawned == 0 && f && fscanf(f, "%d", &status) != 1) {
    status = 0;
  }
  if (f) {
    fclose(f);
  }
  if (spawned == 0) {
    waitpid(pid, NULL, 0);
  }
  return status;
}

// The request the plugin made, fetched with curl and handed to it as the device does.
static void answer_request(int i) {
  char url[256];
  snprintf(url, sizeof(url), "%s", s_url);
  s_url[0] = '\0';
  size_t max = s_url_max < WASM_NET_MAX ? s_url_max : WASM_NET_MAX;
  printf("  request: %s (up to %zu bytes)\n", url, max);
  int status = fetch(url);
  size_t len = 0;
  char *body = (char *) read_file("body.tmp", &len);
  remove("body.tmp");
  if (status != 200) {
    len = 0;  // the device hands over a body only with 200
  }
  if (len > max) {
    len = max;
  }
  printf("  answer: HTTP %d, %zu bytes\n", status, len);
  core_event_t ev = {.type = CORE_EV_NET};
  ev.net.status = status;
  ev.net.body = body;
  ev.net.len = (int) len;
  plugins_event(i, &ev);
  free(body);
}

// The LED colour the plugin asks for, when it has an led export.
static void report_led(int i) {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  if (plugins_led(i, &r, &g, &b)) {
    printf("  LED #%02X%02X%02X\n", r, g, b);
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: preview <module.wasm> <frames-dir> [setting=value ...]\n");
    return 2;
  }
  size_t len = 0;
  uint8_t *code = read_file(argv[1], &len);
  if (!code) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  uint32_t text_len = 0;
  const char *text =
      (const char *) wasm_custom_section(code, (uint32_t) len, MANIFEST_SECTION, &text_len);
  manifest_t m;
  const char *why = text ? manifest_parse(text, (int) text_len, &m) : "section";
  if (why) {
    printf("refused: plugin.json %s\n", why);
    return 1;
  }
  printf("%s %s (%s, %s), %zu bytes, hosts:", m.name, m.version, m.id, m.category, len);
  for (int h = 0; h < m.host_count; h++) {
    printf(" %s", m.hosts[h]);
  }
  printf("%s\n", m.host_count ? "" : " none");

  host_sdk_init();
  host_set_reading(SENSOR_TEMP, 22.5f, true);
  host_set_reading(SENSOR_HUM, 45.0f, true);
  host_set_reading(SENSOR_CO2, 650.0f, true);
  for (int i = 3; i < argc; i++) {
    char *eq = strchr(argv[i], '=');
    if (eq) {
      *eq = '\0';
      host_add_setting(argv[i], eq + 1);
    }
  }
  host_net_get = note_request;
  host_online = true;  // the requests are made for real
  host_timer_start = note_timer;
  if (!wasm_module_runtime_init(&HEAP)) {
    fprintf(stderr, "the runtime does not start\n");
    return 2;
  }

  // The loader's checks, on a copy: a load rewrites its buffer.
  uint8_t *scratch = malloc(len);
  memcpy(scratch, code, len);
  wasm_refusal_t refused;
  if (!wasm_module_trial(scratch, (uint32_t) len, &refused)) {
    printf("refused: %s %s (%s)\n", refused.reason ? refused.reason : "invalid", refused.name,
           refused.text);
    return 1;
  }
  free(scratch);

  const plugins_hooks_t hooks = {.alloc = heap_alloc, .free = free};
  plugins_init(&hooks);
  const char *load_why = NULL;
  int p = plugins_add(m.id, (uint32_t) len, "");
  if (!plugins_load(p, &m, text, (int) text_len, code, (uint32_t) len, &load_why)) {
    printf("refused: %s\n", load_why ? load_why : "load");
    return 1;
  }
  const char *dir = argv[2];

  plugins_event(p, &(core_event_t){.type = CORE_EV_SHOW});
  if (!still_running(p)) {
    return 1;
  }
  plugins_draw(p);
  if (!still_running(p)) {
    return 1;
  }
  write_frame(dir, "1-shown");
  report_led(p);
  for (int round = 0; s_url[0] && round < 3; round++) {
    answer_request(p);
    plugins_draw(p);
    if (!still_running(p)) {
      return 1;
    }
    write_frame(dir, round == 0 ? "2-answered" : "2-answered-again");
    report_led(p);
  }
  for (int id = 0; id < 2; id++) {
    if (s_timers[id] < 0) {
      continue;
    }
    plugins_event(p, &(core_event_t){.type = CORE_EV_TIMER, .timer_id = id});
    if (s_url[0]) {
      answer_request(p);
    }
    plugins_draw(p);
    if (!still_running(p)) {
      return 1;
    }
    char name[32];
    snprintf(name, sizeof(name), "3-timer-%d", id);
    write_frame(dir, name);
  }
  printf("ok: it loads and draws within the device's limits\n");
  return 0;
}
