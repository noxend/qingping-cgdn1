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
#include "rollback.h"
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
EMBEDDED(pm25)
EMBEDDED(pm10)

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
    {"pm25", pm25_start, pm25_end},
    {"pm10", pm10_start, pm10_end},
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

// ---- which stored modules came from an image -----------------------------------------------
// A standard plugin runs from its stored copy, so a firmware update would leave the old
// module in place. A stored module that some image put there is replaced by this image's
// copy once the image is confirmed; one the user uploaded under a standard id is theirs
// and stays. "Some image put it there" is: the list below names it as the image module
// last installed under that id, or it is one of the modules of 1.0.0, which kept no list.

#define SHA_LEN 32
#define IMAGE_MODULES_KEY "std_modules"
#define IMAGE_MODULES_MAX 16

typedef struct {
  char id[STORE_ID_MAX];
  uint8_t sha[SHA_LEN];
} image_module_t;

// The list, read once at boot and kept in step with the store. UI loop only.
static image_module_t s_image_modules[IMAGE_MODULES_MAX];
static size_t s_image_module_n;

// The standard modules of 1.0.0, as cgdn1.swithy.com/fw/1.0.0/cgdn1.bin carries them.
static const uint8_t RELEASE_1_0_0[][SHA_LEN] = {
    // alerts
    {0x60, 0x9e, 0xaf, 0xbc, 0x33, 0x4e, 0xda, 0xa1, 0x04, 0x65, 0x44,
     0x1e, 0xc9, 0xfe, 0xc7, 0x31, 0x3c, 0x0e, 0x29, 0x1a, 0x9a, 0xea,
     0x72, 0xb4, 0x85, 0x40, 0x1b, 0x23, 0xe4, 0xd7, 0xf8, 0xd6},
    // clock
    {0x75, 0x4e, 0x6a, 0x95, 0x9e, 0x6c, 0xc5, 0xb6, 0x04, 0x03, 0x0a,
     0x2b, 0x53, 0xda, 0xe2, 0x2e, 0xf0, 0x4c, 0xdc, 0x00, 0x13, 0xd6,
     0x44, 0x5b, 0x99, 0xa2, 0xc5, 0x73, 0xd7, 0x3d, 0xa7, 0x6c},
    // co2
    {0x27, 0x51, 0xcf, 0x36, 0xa0, 0x65, 0xa1, 0xc5, 0x78, 0x3f, 0x56,
     0x8e, 0xfd, 0xcf, 0x3a, 0x19, 0x93, 0x70, 0xe9, 0x0f, 0xc6, 0x16,
     0x09, 0xf6, 0x30, 0xfb, 0xe6, 0x70, 0x08, 0x9f, 0x4c, 0x6c},
    // humidity
    {0xc0, 0xb5, 0x21, 0xd8, 0x7e, 0xd0, 0xbb, 0x18, 0x2f, 0x91, 0xe1,
     0x3b, 0x02, 0x84, 0xdb, 0x93, 0xe3, 0x43, 0xd5, 0x09, 0xc6, 0xd7,
     0x98, 0x1f, 0x4c, 0x7f, 0xee, 0x68, 0xf9, 0xb4, 0xd8, 0x91},
    // pm10
    {0x25, 0x55, 0xec, 0x8a, 0x19, 0x12, 0x63, 0x3f, 0x29, 0x4b, 0xe8,
     0xfe, 0x51, 0x89, 0xcf, 0xb5, 0xdf, 0x2e, 0x60, 0xef, 0x75, 0x58,
     0x12, 0x62, 0x02, 0x2d, 0x5c, 0xd2, 0x2c, 0x8e, 0xe4, 0x98},
    // pm25
    {0xfb, 0x0f, 0x16, 0xf2, 0x08, 0x46, 0x2e, 0x8a, 0x91, 0x90, 0x50,
     0xf1, 0x0b, 0x68, 0xcb, 0x06, 0x29, 0x11, 0xcd, 0x7b, 0x48, 0x35,
     0x90, 0x8d, 0x9f, 0x24, 0xfb, 0x0b, 0x9d, 0xb9, 0x4a, 0x84},
    // temperature
    {0x93, 0x31, 0xdd, 0x23, 0x99, 0x42, 0x11, 0xd9, 0xd3, 0x6a, 0xa7,
     0xb6, 0xc3, 0x0d, 0xbb, 0x68, 0xb3, 0xac, 0x3a, 0x23, 0xe7, 0xc3,
     0xa6, 0xa5, 0x03, 0xbf, 0x47, 0xbd, 0x29, 0x84, 0x48, 0x65},
};
#define RELEASE_1_0_0_COUNT (sizeof(RELEASE_1_0_0) / sizeof(RELEASE_1_0_0[0]))

static void image_modules_load(void) {
  size_t bytes = sizeof(s_image_modules);
  esp_err_t err = settings_blob_get(IMAGE_MODULES_KEY, s_image_modules, &bytes);
  bool whole = err == ESP_OK && bytes % sizeof(image_module_t) == 0;
  s_image_module_n = whole ? bytes / sizeof(image_module_t) : 0;
}

static image_module_t *image_module_of(const char *id) {
  for (size_t i = 0; i < s_image_module_n; i++) {
    if (strncmp(s_image_modules[i].id, id, STORE_ID_MAX) == 0) {
      return &s_image_modules[i];
    }
  }
  return NULL;
}

static bool from_an_image(const char *id, const uint8_t sha[SHA_LEN]) {
  for (size_t i = 0; i < RELEASE_1_0_0_COUNT; i++) {
    if (memcmp(RELEASE_1_0_0[i], sha, SHA_LEN) == 0) {
      return true;
    }
  }
  const image_module_t *known = image_module_of(id);
  return known && memcmp(known->sha, sha, SHA_LEN) == 0;
}

// Records sha as the image module stored under id, in place of an earlier one.
static void note_image_module(const char *id, const uint8_t sha[SHA_LEN]) {
  image_module_t *known = image_module_of(id);
  if (known && memcmp(known->sha, sha, SHA_LEN) == 0) {
    return;
  }
  if (!known) {
    if (s_image_module_n == IMAGE_MODULES_MAX) {
      ESP_LOGW(TAG, "image module %s not noted: the list is full", id);
      return;
    }
    known = &s_image_modules[s_image_module_n++];
    memset(known, 0, sizeof(*known));
    strlcpy(known->id, id, sizeof(known->id));
  }
  memcpy(known->sha, sha, SHA_LEN);
  size_t bytes = s_image_module_n * sizeof(image_module_t);
  esp_err_t err = settings_blob_set(IMAGE_MODULES_KEY, s_image_modules, bytes);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "image module %s not noted: %s", id, esp_err_to_name(err));
  }
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

// Standard plugins the tick installs again from the image: one whose stored module did
// not read at boot, and one whose stored module is an earlier image's (sha is that
// module's, so a module the user uploads before the tick is left alone).
typedef struct {
  const standard_t *standard;
  bool outdated;
  uint8_t sha[SHA_LEN];
} restore_t;

static restore_t s_restore[STANDARD_COUNT];
static int s_restore_n;

// What the boot found on flash under a standard id: this image's module, an earlier
// image's, or one the user uploaded. This image's is noted, so a later image knows it.
static bool outdated(const standard_t *standard, uint8_t stored[SHA_LEN]) {
  uint8_t embedded[SHA_LEN];
  uint32_t len = (uint32_t) (standard->end - standard->start);
  if (store_sha256(standard->id, stored) != STORE_OK ||
      !store_sha256_of(standard->start, len, embedded)) {
    return false;
  }
  if (memcmp(stored, embedded, SHA_LEN) == 0) {
    note_image_module(standard->id, stored);
    return false;
  }
  return from_an_image(standard->id, stored);
}

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
    s_restore[s_restore_n++] = (restore_t){.standard = standard};
  }
  if (s_runtime_up && standard && e == STORE_OK) {
    restore_t r = {.standard = standard, .outdated = true};
    if (outdated(standard, r.sha)) {
      ESP_LOGW(TAG, "installed %s is an earlier firmware's: updated after boot", id);
      s_restore[s_restore_n++] = r;
    }
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
// record). One already installed under the same id is left as it is here: the user's
// own stays, an earlier image's copy is updated by the tick. The record is kept unless
// one was refused for memory that may be free at the next boot (PSRAM, the internal
// heap); refused for the storage, it stays for the user to install from the store once
// there is room.
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
  image_modules_load();
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
  const standard_t *standard = standard_of(m.id);
  if (standard && len == (uint32_t) (standard->end - standard->start) &&
      memcmp(received, standard->start, len) == 0) {
    uint8_t sha[SHA_LEN];
    if (store_sha256(m.id, sha) == STORE_OK) {
      note_image_module(m.id, sha);
    }
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

// Whether a standard plugin picked at boot is still the one to install again: since
// then the user may have deleted it or installed another module under its id.
static bool restore_due(const restore_t *r) {
  int i = plugins_find(r->standard->id);
  if (i < 0) {
    return false;
  }
  if (r->outdated) {
    uint8_t now[SHA_LEN];
    return store_sha256(r->standard->id, now) == STORE_OK && memcmp(now, r->sha, SHA_LEN) == 0;
  }
  plugin_info_t info;
  return plugins_info(i, &info) && info.state == PLUGIN_FAILED;
}

// Installs the next standard plugin picked at boot from the image; -1 when none is due.
static int restore_tick(void) {
  static int ticks;
  if (s_restore_n == 0 || ++ticks < WASM_RESTORE_AFTER_TICKS) {
    return -1;
  }
  // An update that has not proved itself yet may still roll back to a firmware that
  // cannot run this image's modules: the stored ones wait for the confirmation.
  if (s_restore[s_restore_n - 1].outdated && rollback_pending()) {
    return -1;
  }
  const restore_t *r = &s_restore[--s_restore_n];
  if (!restore_due(r)) {
    return -1;
  }
  const standard_t *standard = r->standard;
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
