#include "wasm_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "auth.h"
#include "heap_internal.h"
#include "json_out.h"
#include "manifest.h"
#include "settings.h"
#include "util.h"
#include "wasm_binary.h"
#include "wasm_module.h"
#include "wasm_plugins.h"
#include "wasm_store.h"

// PSRAM a module needs beside its pages of linear memory and its code: the operand
// stack and the runtime's own structures (measured at 15 to 20 KB on rc3), with room to
// spare.
#define PSRAM_OVERHEAD (24 * 1024)
// Internal heap an install must leave: the network, TLS and the web server live there.
#define HEAP_FLOOR (16 * 1024)
#define REMOVALS_MAX 8
// The core's record that the standard plugins were installed once (namespace "core").
#define DEFAULTS_KEY "defaults"

static const char *TAG = "wasm";

static wasm_host_hooks_t s_hooks;
static bool s_runtime_up;

// What the HTTP task hands to the UI loop, and the job GET /plugins reports, under s_mux.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ---- the standard plugins ------------------------------------------------------------------
// Built from plugins/standard/<id> during the firmware build (CMakeLists.txt) and
// embedded. Installed in this order, which is the order of the screens on a new device.

#define EMBEDDED(name)                                                                             \
  extern const uint8_t name##_start[] asm("_binary_" #name "_wasm_start");                         \
  extern const uint8_t name##_end[] asm("_binary_" #name "_wasm_end");
EMBEDDED(alerts)
EMBEDDED(clock)
EMBEDDED(humidity)
EMBEDDED(temperature)
EMBEDDED(co2)

typedef struct {
  const char *id;
  const uint8_t *start;
  const uint8_t *end;
} standard_t;

static const standard_t STANDARD[] = {
    {"alerts", alerts_start, alerts_end},
    {"clock", clock_start, clock_end},
    {"humidity", humidity_start, humidity_end},
    {"temperature", temperature_start, temperature_end},
    {"co2", co2_start, co2_end},
};
#define STANDARD_COUNT (sizeof(STANDARD) / sizeof(STANDARD[0]))

static const standard_t *standard_of(const char *id) {
  for (size_t i = 0; i < STANDARD_COUNT; i++) {
    if (strcmp(STANDARD[i].id, id) == 0) {
      return &STANDARD[i];
    }
  }
  return NULL;
}

// ---- memory --------------------------------------------------------------------------------

// The runtime's heap, linear memory included (WAMR_BUILD_ALLOC_WITH_USAGE).
static void *psram_alloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

static void *psram_realloc(void *p, size_t size) {
  return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM);
}

static const wasm_heap_t PSRAM_HEAP = {
    .alloc = psram_alloc,
    .realloc = psram_realloc,
    .free = heap_caps_free,
    .size = heap_caps_get_allocated_size,
};

void wasm_host_psram(unsigned *free_now, unsigned *free_min) {
  *free_now = (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  *free_min = (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
}

// ---- reading a module ----------------------------------------------------------------------

// The module's manifest: its text inside the module and what it says, or NULL with
// *why: "section" (none, or not WebAssembly) or manifest_parse's code.
static const char *module_manifest(const uint8_t *code, uint32_t len, manifest_t *m, int *text_len,
                                   const char **why) {
  uint32_t n = 0;
  const char *text = (const char *) wasm_custom_section(code, len, MANIFEST_SECTION, &n);
  if (!text || n == 0) {
    *why = "section";
    return NULL;
  }
  *why = manifest_parse(text, (int) n, m);
  *text_len = (int) n;
  return *why ? NULL : text;
}

// A stored select value that is one of the option's labels (what the built-in screens
// stored before 1.0, "24 години") is stored as the option's key from now on.
typedef struct {
  const char *id;
  const char *text;
  int len;
} relabel_t;

static bool relabel(const char *key, void *ctx) {
  const relabel_t *r = ctx;
  char value[96];
  char option[32];
  settings_get(r->id, key, value, sizeof(value), "");
  if (value[0] && manifest_key_for_label(r->text, r->len, key, value, option, sizeof(option))) {
    ESP_LOGI(TAG, "%s.%s: \"%s\" stored as \"%s\"", r->id, key, value, option);
    settings_set(r->id, key, option);
  }
  return true;
}

// Loads code (in PSRAM; the table takes it) into entry i, timed and weighed. NULL, or
// why it did not load. The settings are relabelled first, while text is untouched: the
// load rewrites the module's bytes, and text may lie inside them.
static const char *load(int i, const manifest_t *m, const char *text, int text_len, uint8_t *code,
                        uint32_t len) {
  relabel_t r = {.id = m->id, .text = text, .len = text_len};
  manifest_selects(text, text_len, relabel, &r);
  size_t before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) + len;  // the code counts
  int64_t t0 = esp_timer_get_time();
  const char *why = NULL;
  bool ok = plugins_load(i, m, text, text_len, code, len, &why);
  int us = (int) (esp_timer_get_time() - t0);
  plugins_measured(i, us, (int) (before - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  return ok ? NULL : why;
}

// ---- boot ----------------------------------------------------------------------------------

// Standard plugins whose stored module did not read at boot, put back by the tick.
static const standard_t *s_restore[STANDARD_COUNT];
static int s_restore_n;

static void load_installed(const char *id) {
  uint8_t *code = NULL;
  uint32_t len = 0;
  store_err_t e =
      s_runtime_up ? store_read(id, &code, &len, psram_alloc, heap_caps_free) : STORE_ERR_FLASH;
  // A standard plugin whose stored module no longer reads (a power fault while the flash
  // was written) comes back from its copy in the image, its settings kept. Not during
  // the boot: the tick does it once the board is up, and until then it is listed failed.
  const standard_t *standard = standard_of(id);
  if (s_runtime_up && standard && (e == STORE_ERR_FLASH || e == STORE_ERR_HASH)) {
    ESP_LOGW(TAG, "installed %s does not read (%s): restored after boot", id, store_err_code(e));
    s_restore[s_restore_n++] = standard;
  }
  if (e != STORE_OK && store_size(id, &len) != STORE_OK) {
    len = 0;
  }
  const char *why = !s_runtime_up ? "runtime" : (e != STORE_OK ? store_err_code(e) : NULL);
  manifest_t m;
  int text_len = 0;
  const char *text = why ? NULL : module_manifest(code, len, &m, &text_len, &why);
  int i = plugins_add(id, len, why ? why : "");
  if (i >= 0 && text) {
    why = load(i, &m, text, text_len, code, len);  // the table took the code
    code = NULL;
  }
  heap_caps_free(code);
  if (why) {
    ESP_LOGE(TAG, "installed %s not loaded: %s", id, why);
  }
}

static const char *install(const uint8_t *received, uint32_t len, const char **name, int *changed);

// The standard plugins, at the first boot and after a factory reset (which erases the
// record). One already installed under the same id, the user's own or an earlier copy,
// is left as it is. The record is kept unless one was refused for memory that may be free
// at the next boot (PSRAM, the internal heap); refused for the storage, it stays for the
// user to install from the store once there is room.
static void install_standard(void) {
  size_t len = 0;
  if (!s_runtime_up || settings_blob_get(DEFAULTS_KEY, NULL, &len) == ESP_OK) {
    return;
  }
  bool again = false;
  for (size_t k = 0; k < STANDARD_COUNT; k++) {
    if (plugins_find(STANDARD[k].id) >= 0) {
      continue;
    }
    int changed = -1;
    const char *name;
    const char *why =
        install(STANDARD[k].start, STANDARD[k].end - STANDARD[k].start, &name, &changed);
    if (why) {
      ESP_LOGE(TAG, "standard %s not installed: %s", STANDARD[k].id, why);
      again = again || strcmp(why, "memory") == 0 || strcmp(why, "heap") == 0;
    }
  }
  if (!again) {
    uint8_t done = 1;
    settings_blob_set(DEFAULTS_KEY, &done, sizeof(done));
  }
}

void wasm_host_init(const wasm_host_hooks_t *hooks) {
  s_hooks = *hooks;
  const plugins_hooks_t table = {
      .timers_stop = hooks->timers_stop,
      .lock = hooks->lock,
      .unlock = hooks->unlock,
      .alloc = psram_alloc,
      .free = heap_caps_free,
  };
  plugins_init(&table);
  store_init();
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
    ESP_LOGW(TAG, "no PSRAM: WASM plugins off");
  } else {
    s_runtime_up = wasm_module_runtime_init(&PSRAM_HEAP);
    if (!s_runtime_up) {
      ESP_LOGE(TAG, "runtime failed: WASM plugins off");
    }
  }
  char (*ids)[STORE_ID_MAX] = NULL;
  int n = store_ids(&ids);
  for (int k = 0; k < n; k++) {
    load_installed(ids[k]);
  }
  free(ids);
  install_standard();
}

void wasm_host_missing(bool (*fn)(const char *id, const char *text, int len, void *ctx),
                       void *ctx) {
  for (size_t k = 0; k < STANDARD_COUNT; k++) {
    if (plugins_find(STANDARD[k].id) >= 0) {
      continue;
    }
    uint32_t n = 0;
    const char *text = (const char *) wasm_custom_section(
        STANDARD[k].start, STANDARD[k].end - STANDARD[k].start, MANIFEST_SECTION, &n);
    if (text && !fn(STANDARD[k].id, text, (int) n, ctx)) {
      return;
    }
  }
}

// ---- install -------------------------------------------------------------------------------

// The job GET /plugins reports: the last install, how it ended and why it failed.
typedef enum {
  JOB_IDLE,
  JOB_WAITING,  // received, for the next tick
  JOB_DONE,
  JOB_FAILED,
} job_stage_t;

typedef struct {
  job_stage_t stage;
  int n;  // counts installs since boot, so a client tells this job's end from the last one's
  char id[STORE_ID_MAX];
  char code[16];  // why it failed, for the web page to word
  char name[48];  // what the failure is about: an import, an export, a field
} job_t;

static job_t s_job;

// What the HTTP task handed over for the next tick: a module in PSRAM, or a standard
// plugin's id; and deletions waiting.
static uint8_t *s_upload;
static uint32_t s_upload_len;
static const standard_t *s_standard;
static char s_remove_q[REMOVALS_MAX][STORE_ID_MAX];
static int s_remove_n;

static void job_end(const char *code, const char *name) {
  taskENTER_CRITICAL(&s_mux);
  s_job.stage = code ? JOB_FAILED : JOB_DONE;
  snprintf(s_job.code, sizeof(s_job.code), "%s", code ? code : "");
  snprintf(s_job.name, sizeof(s_job.name), "%s", name ? name : "");
  taskEXIT_CRITICAL(&s_mux);
  if (code) {
    ESP_LOGW(TAG, "install of %s failed: %s %s", s_job.id, code, name ? name : "");
  }
}

// Which memory refuses this module now: "space" (flash), "memory" (PSRAM), "heap"
// (internal), or NULL.
static const char *no_room(const uint8_t *code, uint32_t len) {
  if (!store_fits(len)) {
    return "space";
  }
  // A replaced plugin's PSRAM comes back only after the new one loads. In 64 bits: a
  // module may declare up to 65,536 pages.
  uint64_t pages = (uint64_t) wasm_module_pages(code, len) * WASM_PAGE;
  uint64_t psram = pages + 2ULL * len + PSRAM_OVERHEAD;
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < psram) {
    return "memory";
  }
  if (heap_caps_get_free_size(HEAP_INTERNAL) < HEAP_FLOOR) {
    return "heap";
  }
  return NULL;
}

// Installs a module: its manifest, room for it, a trial load (a module the runtime
// refuses never replaces a working version on flash), the flash, the table. NULL when it
// is installed, else why not, and in *name what the refusal is about (an import, an
// export) or "". *changed is the table index whose plugin changed, or stays -1.
static const char *install(const uint8_t *received, uint32_t len, const char **name, int *changed) {
  static char refused_name[48];
  *name = "";
  manifest_t m;
  int text_len = 0;
  const char *why = NULL;
  const char *text = module_manifest(received, len, &m, &text_len, &why);
  if (!text) {
    return why;
  }
  why = no_room(received, len);
  // A load rewrites the bytes it loads: the trial spends one copy, the table gets another.
  uint8_t *trial = why ? NULL : psram_alloc(len);
  uint8_t *code = trial ? psram_alloc(len) : NULL;
  if (!why && !code) {
    why = "memory";
  }
  if (why) {
    heap_caps_free(trial);
    return why;
  }
  memcpy(trial, received, len);
  memcpy(code, received, len);
  wasm_refusal_t refused;
  bool loads = wasm_module_trial(trial, len, &refused);
  heap_caps_free(trial);
  if (!loads) {
    heap_caps_free(code);
    ESP_LOGW(TAG, "%s does not load: %s", m.id, refused.text);
    snprintf(refused_name, sizeof(refused_name), "%s", refused.name);
    *name = refused_name;
    return refused.reason ? refused.reason : "invalid";
  }
  store_err_t e = store_save(m.id, code, len);
  int i = e == STORE_OK ? plugins_add(m.id, len, "") : -1;
  if (i < 0) {
    heap_caps_free(code);
    if (e == STORE_OK) {
      store_remove(m.id, false);  // not kept when the table has no room to run it
    }
    return e == STORE_OK ? "memory" : store_err_code(e);
  }
  *changed = i;
  // On flash either way: a plugin that did not load is listed with why, and loads at the
  // next boot.
  return load(i, &m, text, text_len, code, len);
}

// ---- remove --------------------------------------------------------------------------------

static int remove_now(const char *id) {
  int i = plugins_find(id);
  store_err_t e = store_remove(id, true);
  if (e != STORE_OK && e != STORE_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "deleting %s: %s", id, store_err_code(e));
    return -1;
  }
  if (i >= 0) {
    plugins_drop(i);
  }
  return i;
}

// ---- the UI loop's tick --------------------------------------------------------------------

// Puts back the next standard plugin that did not read at boot; -1 when none is due.
static int restore_tick(void) {
  static int ticks;
  if (s_restore_n == 0 || ++ticks < WASM_RESTORE_AFTER_TICKS) {
    return -1;
  }
  const standard_t *standard = s_restore[--s_restore_n];
  // Since the boot the user may have deleted it or installed another module under its
  // id: only a plugin still listed failed is put back.
  int i = plugins_find(standard->id);
  plugin_info_t info;
  if (i < 0 || !plugins_info(i, &info) || info.state != PLUGIN_FAILED) {
    return -1;
  }
  int changed = -1;
  const char *name;
  const char *why = install(standard->start, standard->end - standard->start, &name, &changed);
  if (why) {
    ESP_LOGE(TAG, "standard %s not restored: %s", standard->id, why);
  } else {
    ESP_LOGW(TAG, "standard %s restored from the image", standard->id);
  }
  return changed;
}

int wasm_host_tick(void) {
  char remove[STORE_ID_MAX] = "";
  uint8_t *upload = NULL;
  uint32_t upload_len = 0;
  const standard_t *standard = NULL;
  taskENTER_CRITICAL(&s_mux);
  if (s_remove_n > 0) {
    strcpy(remove, s_remove_q[--s_remove_n]);
  } else {
    // One change to the table per tick: an install waits for a second with no deletion.
    upload = s_upload;
    upload_len = s_upload_len;
    standard = s_standard;
    s_upload = NULL;
    s_standard = NULL;
  }
  taskEXIT_CRITICAL(&s_mux);
  if (remove[0]) {
    return remove_now(remove);
  }
  int changed = -1;
  const char *name = "";
  const char *why = NULL;
  if (upload) {
    why = install(upload, upload_len, &name, &changed);
    heap_caps_free(upload);  // install kept its own copy
  } else if (standard) {
    why = install(standard->start, standard->end - standard->start, &name, &changed);
  }
  if (upload || standard) {
    job_end(why, name);
    return changed;
  }
  return restore_tick();
}

// ---- HTTP ----------------------------------------------------------------------------------

static esp_err_t accepted(httpd_req_t *req) {
  httpd_resp_set_status(req, "202 Accepted");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// A 400 that says why the module was refused: {"error": code, "field": name}.
static esp_err_t refused_body(httpd_req_t *req, const char *code, const char *field) {
  json_out_t o;
  httpd_resp_set_status(req, "400 Bad Request");
  json_out_begin(&o, req);
  json_out_raw(&o, "{\"error\":", 9);
  json_out_str(&o, code);
  if (field) {
    json_out_raw(&o, ",\"field\":", 9);
    json_out_str(&o, field);
  }
  json_out_raw(&o, "}", 1);
  return json_out_end(&o);
}

// True when the request says its body is `type`. A browser sends a cross-site body
// with this type only after a CORS preflight, which the device never answers: a second
// wall behind sign-in.
static bool content_type_is(httpd_req_t *req, const char *type) {
  char got[40] = {0};
  esp_err_t e = httpd_req_get_hdr_value_str(req, "Content-Type", got, sizeof(got));
  return e == ESP_OK && strcmp(got, type) == 0;
}

// Hands an install to the tick; false (answered 409) while one waits.
static bool queue_install(httpd_req_t *req, uint8_t *upload, uint32_t len,
                          const standard_t *standard, const char *id) {
  taskENTER_CRITICAL(&s_mux);
  // Waiting lasts until the tick has finished the install, not only until it took it.
  bool busy = s_job.stage == JOB_WAITING;
  if (!busy) {
    s_upload = upload;
    s_upload_len = len;
    s_standard = standard;
    snprintf(s_job.id, sizeof(s_job.id), "%s", id);
    s_job.n++;
    s_job.stage = JOB_WAITING;
    s_job.code[0] = '\0';
    s_job.name[0] = '\0';
  }
  taskEXIT_CRITICAL(&s_mux);
  if (busy) {
    http_error(req, HTTP_409_CONFLICT, "busy");
  }
  return !busy;
}

// POST /plugins/upload: a module with its plugin.json inside (plugins/build.sh puts
// it there), installed on the next tick. Nobody signed it: the user chose the file, and
// the sandbox holds it to the hosts its plugin.json lists. The upload is as large as the
// storage has room for. The answer comes as the install job in GET /plugins.
static esp_err_t upload_post(httpd_req_t *req) {
  if (!content_type_is(req, "application/wasm")) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need Content-Type: application/wasm");
  }
  if (!s_runtime_up) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime");
  }
  int len = req->content_len;
  if (len < 8) {
    return refused_body(req, "section", NULL);
  }
  if (!store_fits((uint32_t) len)) {
    return refused_body(req, "space", NULL);
  }
  uint8_t *code = psram_alloc(len);
  if (!code) {
    return refused_body(req, "memory", NULL);
  }
  if (!recv_exact(req, (char *) code, len)) {
    heap_caps_free(code);
    return http_error(req, HTTPD_400_BAD_REQUEST, "receive");
  }
  manifest_t m;
  int text_len;
  const char *why = NULL;
  if (!module_manifest(code, (uint32_t) len, &m, &text_len, &why)) {
    heap_caps_free(code);
    bool code_word = strcmp(why, "section") == 0 || strcmp(why, "json") == 0 ||
                     strcmp(why, "sdk") == 0 || strcmp(why, "old_sdk") == 0 ||
                     strcmp(why, "fields") == 0;
    return code_word ? refused_body(req, why, NULL) : refused_body(req, "field", why);
  }
  if (!queue_install(req, code, (uint32_t) len, NULL, m.id)) {
    heap_caps_free(code);
    return ESP_FAIL;
  }
  return accepted(req);
}

// The {"id":"..."} body of a POST, checked.
static bool read_id(httpd_req_t *req, char *id) {
  char body[96];
  int len = req->content_len;
  if (len <= 0 || len >= (int) sizeof(body) || !recv_exact(req, body, len)) {
    return false;
  }
  body[len] = '\0';
  cJSON *root = cJSON_Parse(body);
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "id");
  bool ok = cJSON_IsString(v) && manifest_id_ok(v->valuestring);
  if (ok) {
    strcpy(id, v->valuestring);
  }
  cJSON_Delete(root);
  return ok;
}

// POST /plugins/install {"id"}: a standard plugin again, from its copy in the image.
static esp_err_t install_post(httpd_req_t *req) {
  if (!content_type_is(req, "application/json")) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need Content-Type: application/json");
  }
  if (!s_runtime_up) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime");
  }
  char id[STORE_ID_MAX];
  const standard_t *standard = read_id(req, id) ? standard_of(id) : NULL;
  if (!standard) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need {\"id\"} of a standard plugin");
  }
  if (!queue_install(req, NULL, 0, standard, id)) {
    return ESP_FAIL;
  }
  return accepted(req);
}

// POST /plugins/remove {"id"}: deletes the plugin and its settings, answered as an
// upload is.
static esp_err_t remove_post(httpd_req_t *req) {
  if (!content_type_is(req, "application/json")) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need Content-Type: application/json");
  }
  char id[STORE_ID_MAX];
  if (!read_id(req, id)) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need {\"id\"}");
  }
  bool queued = false;
  taskENTER_CRITICAL(&s_mux);
  for (int k = 0; k < s_remove_n && !queued; k++) {
    queued = strcmp(s_remove_q[k], id) == 0;
  }
  if (!queued && s_remove_n < REMOVALS_MAX) {
    strcpy(s_remove_q[s_remove_n++], id);
    queued = true;
  }
  taskEXIT_CRITICAL(&s_mux);
  if (!queued) {
    return http_error(req, HTTP_429_TOO_MANY, "too many deletions waiting");
  }
  return accepted(req);
}

// One installed plugin, how it runs and why it stopped or did not load; its timings go
// only to a signed-in caller.
static void installed_json(json_out_t *o, const plugin_info_t *it, bool diag) {
  static const char *const STATES[] = {"failed", "ready", "stopped"};
  json_out_raw(o, "{\"id\":", 6);
  json_out_str(o, it->id);
  json_out_raw(o, ",\"name\":", 8);
  json_out_str(o, it->name);
  json_out_raw(o, ",\"version\":", 11);
  json_out_str(o, it->version);
  json_out_raw(o, ",\"hosts\":[", 10);
  for (int h = 0; h < it->host_count; h++) {
    json_out_raw(o, h ? "," : "", h ? 1 : 0);
    json_out_str(o, it->hosts[h]);
  }
  json_out_printf(o, "],\"size\":%lu,\"pages\":%lu,\"state\":\"%s\",\"error\":",
                  (unsigned long) it->size, (unsigned long) it->pages, STATES[it->state]);
  json_out_str(o, it->error);
  if (diag) {
    json_out_printf(o, ",\"load_us\":%d,\"psram\":%d,\"draws\":%d,\"draw_us_max\":%d", it->load_us,
                    it->psram, it->draws, it->draw_us_max);
  }
  json_out_raw(o, "}", 1);
}

// GET /plugins: the installed plugins, why one stopped, the storage and PSRAM they live in
// for the memory panel, the install job, and the timings and the internal heap, as
// /status keeps its diagnostics. Sent as it is written, one plugin at a time.
static esp_err_t plugins_get(httpd_req_t *req) {
  static const char *const STAGES[] = {"idle", "waiting", "done", "failed"};
  bool diag = auth_diag_allowed(req);
  job_t job;
  taskENTER_CRITICAL(&s_mux);
  job = s_job;
  taskEXIT_CRITICAL(&s_mux);
  store_space_t space;
  store_space(&space);
  json_out_t o;
  json_out_begin(&o, req);
  json_out_printf(&o, "{\"sdk\":%d,\"runtime\":%s,\"installed\":[", MANIFEST_SDK,
                  s_runtime_up ? "true" : "false");
  size_t modules = 0;
  bool first = true;
  for (int i = 0; i < plugins_count(); i++) {
    plugin_info_t it;
    if (!plugins_info(i, &it)) {
      continue;
    }
    json_out_raw(&o, first ? "" : ",", first ? 0 : 1);
    first = false;
    installed_json(&o, &it, diag);
    modules += store_install_bytes(it.size);
  }
  json_out_printf(&o, "],\"storage\":{\"modules\":%u,\"free\":%u,\"partition\":%u}",
                  (unsigned) modules, (unsigned) space.free, (unsigned) space.partition);
  json_out_printf(&o, ",\"psram\":{\"free\":%u,\"total\":%u}",
                  (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned) heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  json_out_printf(&o, ",\"job\":{\"n\":%d,\"id\":", job.n);
  json_out_str(&o, job.id);
  json_out_printf(&o, ",\"stage\":\"%s\",\"code\":", STAGES[job.stage]);
  json_out_str(&o, job.code);
  json_out_raw(&o, ",\"name\":", 8);
  json_out_str(&o, job.name);
  json_out_raw(&o, "}", 1);
  if (diag) {
    json_out_printf(&o, ",\"heap\":{\"free\":%u,\"min\":%u,\"largest\":%u}",
                    (unsigned) heap_caps_get_free_size(HEAP_INTERNAL),
                    (unsigned) heap_caps_get_minimum_free_size(HEAP_INTERNAL),
                    (unsigned) heap_caps_get_largest_free_block(HEAP_INTERNAL));
  }
  json_out_raw(&o, "}", 1);
  return json_out_end(&o);
}

esp_err_t wasm_host_http_register(httpd_handle_t srv) {
  const httpd_uri_t uris[] = {
      {.uri = "/plugins", .method = HTTP_GET, .handler = plugins_get},
      {.uri = "/plugins/upload", .method = HTTP_POST, .handler = upload_post},
      {.uri = "/plugins/install", .method = HTTP_POST, .handler = install_post},
      {.uri = "/plugins/remove", .method = HTTP_POST, .handler = remove_post},
  };
  for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
    esp_err_t e = http_register(srv, &uris[i]);
    if (e != ESP_OK) {
      return e;
    }
  }
  return ESP_OK;
}
