#include "wasm_plugins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "wasm_module.h"

static const char *TAG = "plugins";

typedef struct {
  plugin_state_t state;
  bool started;  // the module's init and on_config ran
  char id[MANIFEST_ID_MAX];
  char name[48];
  char version[16];
  char hosts[MANIFEST_HOSTS_MAX][64];
  int host_count;
  char *manifest;  // the manifest's text, NULL until a module loads
  int manifest_len;
  uint8_t *code;  // kept while the module is loaded: the runtime points into it
  uint32_t code_len;
  uint32_t size;  // the module on flash, also when it did not load
  uint32_t pages;
  wasm_mod_t mod;
  char error[128];
  char stats[256];
  int draws;
  int draw_us_max;
  int64_t draw_us_sum;
  int load_us;
  int psram;
} entry_t;

// The table: pointers to entries, NULL at a free index. The array is in the internal
// heap (a pointer per plugin); the entries, with their modules, where the hooks put them.
static entry_t **s_entries;
static int s_count;
static plugins_hooks_t s_hooks;

static void lock(void) {
  if (s_hooks.lock) {
    s_hooks.lock();
  }
}

static void unlock(void) {
  if (s_hooks.unlock) {
    s_hooks.unlock();
  }
}

void plugins_init(const plugins_hooks_t *hooks) {
  s_hooks = *hooks;
}

int plugins_count(void) {
  lock();
  int n = s_count;
  unlock();
  return n;
}

// The index of `id`, or -1; the caller holds the lock.
static int find(const char *id) {
  for (int i = 0; i < s_count; i++) {
    if (s_entries[i] && strcmp(s_entries[i]->id, id) == 0) {
      return i;
    }
  }
  return -1;
}

int plugins_find(const char *id) {
  lock();
  int i = find(id);
  unlock();
  return i;
}

bool plugins_used(int i) {
  lock();
  bool used = i >= 0 && i < s_count && s_entries[i];
  unlock();
  return used;
}

const char *plugins_id(int i) {
  return i >= 0 && i < s_count && s_entries[i] ? s_entries[i]->id : "";
}

// GET /plugins puts the text into a JSON string: no quote, backslash or control byte.
static void set_error(entry_t *e, const char *why) {
  snprintf(e->error, sizeof(e->error), "%s", why);
  for (char *c = e->error; *c; c++) {
    if (*c == '"' || *c == '\\' || (unsigned char) *c < 0x20) {
      *c = '\'';
    }
  }
}

int plugins_add(const char *id, uint32_t size, const char *why) {
  lock();
  int i = find(id);
  if (i < 0) {
    for (int k = 0; k < s_count && i < 0; k++) {
      i = s_entries[k] ? -1 : k;
    }
  }
  if (i < 0) {
    entry_t **grown = realloc(s_entries, (s_count + 1) * sizeof(*grown));
    if (grown) {
      s_entries = grown;
      s_entries[s_count] = NULL;
      i = s_count++;
    }
  }
  if (i >= 0 && !s_entries[i]) {
    s_entries[i] = s_hooks.alloc(sizeof(entry_t));
    if (s_entries[i]) {
      memset(s_entries[i], 0, sizeof(entry_t));
    } else {
      i = -1;
    }
  }
  if (i >= 0) {
    entry_t *e = s_entries[i];
    snprintf(e->id, sizeof(e->id), "%s", id);
    e->size = size;
    if (!e->name[0]) {
      snprintf(e->name, sizeof(e->name), "%s", id);
    }
    if (e->state != PLUGIN_READY) {
      e->state = PLUGIN_FAILED;
      set_error(e, why);
    }
  }
  unlock();
  return i;
}

// ---- loading -------------------------------------------------------------------------------

static void unload(int i) {
  entry_t *e = s_entries[i];
  if (s_hooks.timers_stop) {
    s_hooks.timers_stop(i);  // so none fires into the next module
  }
  wasm_module_unload(&e->mod);
  s_hooks.free(e->code);
  e->code = NULL;
  e->code_len = 0;
  e->started = false;
}

bool plugins_load(int i, const manifest_t *m, const char *text, int text_len, uint8_t *code,
                  uint32_t len, const char **why) {
  entry_t *e = s_entries[i];
  unload(i);
  char *copy = s_hooks.alloc(text_len + 1);
  lock();
  s_hooks.free(e->manifest);
  e->manifest = copy;
  e->manifest_len = copy ? text_len : 0;
  if (copy) {
    memcpy(copy, text, text_len);
    copy[text_len] = '\0';
  }
  snprintf(e->id, sizeof(e->id), "%s", m->id);
  snprintf(e->name, sizeof(e->name), "%s", m->name);
  snprintf(e->version, sizeof(e->version), "%s", m->version);
  memcpy(e->hosts, m->hosts, sizeof(e->hosts));
  e->host_count = m->host_count;
  e->size = len;
  e->pages = wasm_module_pages(code, len);
  e->state = PLUGIN_FAILED;
  e->error[0] = '\0';
  e->stats[0] = '\0';
  e->draws = 0;
  e->draw_us_max = 0;
  e->draw_us_sum = 0;
  unlock();
  if (!copy) {
    s_hooks.free(code);
    *why = "memory";
    plugins_fail(i, *why);
    return false;
  }
  e->code = code;
  e->code_len = len;
  if (!wasm_module_load(&e->mod, code, len)) {
    ESP_LOGE(TAG, "%s does not load: %s", m->id, e->mod.error);
    *why = e->mod.reason ? e->mod.reason : "invalid";
    plugins_fail(i, e->mod.error);
    return false;
  }
  e->mod.instruction_limit = WASM_INSTRUCTION_LIMIT;
  for (int k = 0; k < m->host_count && k < WASM_HOSTS_MAX; k++) {
    snprintf(e->mod.policy.hosts[k], sizeof(e->mod.policy.hosts[k]), "%s", m->hosts[k]);
  }
  e->mod.policy.host_count = m->host_count < WASM_HOSTS_MAX ? m->host_count : WASM_HOSTS_MAX;
  lock();
  e->state = PLUGIN_READY;
  unlock();
  ESP_LOGI(TAG, "%s %s (%lu B, %lu pages)", m->id, m->version, (unsigned long) len,
           (unsigned long) e->pages);
  return true;
}

void plugins_measured(int i, int load_us, int psram) {
  lock();
  s_entries[i]->load_us = load_us;
  s_entries[i]->psram = psram;
  unlock();
}

void plugins_fail(int i, const char *why) {
  entry_t *e = s_entries[i];
  unload(i);
  lock();
  e->state = PLUGIN_FAILED;
  set_error(e, why);
  unlock();
}

void plugins_drop(int i) {
  entry_t *e = s_entries[i];
  unload(i);
  lock();
  s_entries[i] = NULL;
  unlock();
  s_hooks.free(e->manifest);
  s_hooks.free(e);
}

// ---- calls into the module -----------------------------------------------------------------

static void stop(int i, const char *why) {
  entry_t *e = s_entries[i];
  lock();
  set_error(e, why);
  e->state = PLUGIN_STOPPED;
  unlock();
  ESP_LOGE(TAG, "%s stopped: %s", e->id, why);
  unload(i);
}

bool plugins_ready(int i) {
  return i >= 0 && i < s_count && s_entries[i] && s_entries[i]->state == PLUGIN_READY;
}

void plugins_start(int i) {
  entry_t *e = plugins_ready(i) ? s_entries[i] : NULL;
  if (!e || e->started) {
    return;
  }
  e->started = true;
  if (!wasm_module_call(&e->mod, WASM_FN_INIT, 0, NULL) ||
      !wasm_module_call(&e->mod, WASM_FN_ON_CONFIG, 0, NULL)) {
    stop(i, e->mod.error);
  }
}

// Calls into the module; a trap unloads it, the device carries on.
static void call(int i, wasm_fn_t fn, uint32_t argc, uint32_t *argv) {
  plugins_start(i);
  if (!plugins_ready(i)) {
    return;
  }
  entry_t *e = s_entries[i];
  if (!wasm_module_call(&e->mod, fn, argc, argv)) {
    stop(i, e->mod.error);
  }
}

// The answer to the module's core_net_get: the body goes into a buffer the module
// names (net_buffer), then on_net learns the status and the length, 0 when there was
// no body or the module had no room for it. An answer to a request this module never
// made (the entry held another one then) is dropped.
static void deliver_net(int i, const core_net_result_t *net) {
  entry_t *e = s_entries[i];
  if (!plugins_ready(i) || e->mod.policy.net_pending <= 0) {
    return;
  }
  e->mod.policy.net_pending--;
  uint32_t len = 0;
  // Without net_buffer the call would leave the length where the offset comes back.
  if (net->body && net->len > 0 && e->mod.fn[WASM_FN_NET_BUFFER]) {
    uint32_t argv[2] = {(uint32_t) net->len, 0};
    call(i, WASM_FN_NET_BUFFER, 1, argv);
    bool copied = plugins_ready(i) && argv[0] != 0 &&
                  wasm_module_copy_in(&e->mod, argv[0], net->body, (uint32_t) net->len);
    len = copied ? (uint32_t) net->len : 0;
  }
  uint32_t argv[2] = {(uint32_t) net->status, len};
  call(i, WASM_FN_ON_NET, 2, argv);
}

void plugins_event(int i, const core_event_t *ev) {
  if (ev->type == CORE_EV_NET) {
    deliver_net(i, &ev->net);
    return;
  }
  uint32_t argv[2] = {(uint32_t) ev->type, (uint32_t) ev->timer_id};
  call(i, WASM_FN_ON_EVENT, 2, argv);
}

void plugins_draw(int i) {
  entry_t *e = i >= 0 && i < s_count ? s_entries[i] : NULL;
  if (!e) {
    gfx_clear();
    return;
  }
  if (!plugins_ready(i)) {
    // A plugin that stopped or did not load stays listed until it is deleted or
    // installed again.
    gfx_clear();
    gfx_text_begin();
    gfx_font(GFX_FONT_TEXT);
    gfx_text_centered(40, e->name);
    gfx_text_centered(70, "stopped");
    gfx_text_blit(8);
    return;
  }
  int64_t t0 = esp_timer_get_time();
  call(i, WASM_FN_DRAW, 0, NULL);
  int us = (int) (esp_timer_get_time() - t0);
  e->draws++;
  e->draw_us_sum += us;
  if (us > e->draw_us_max) {
    e->draw_us_max = us;
  }
}

void plugins_config(int i) {
  call(i, WASM_FN_ON_CONFIG, 0, NULL);
}

bool plugins_led(int i, uint8_t *r, uint8_t *g, uint8_t *b) {
  entry_t *e = s_entries[i];
  if (!plugins_ready(i) || !e->mod.fn[WASM_FN_LED]) {
    return false;
  }
  uint32_t argv[1] = {UINT32_MAX};
  call(i, WASM_FN_LED, 0, argv);
  int32_t rgb = (int32_t) argv[0];
  if (!plugins_ready(i) || rgb < 0) {
    return false;
  }
  *r = (uint8_t) (rgb >> 16);
  *g = (uint8_t) (rgb >> 8);
  *b = (uint8_t) rgb;
  return true;
}

// ---- reads from other tasks ----------------------------------------------------------------

bool plugins_stats_set(int i, const char *json) {
  lock();
  bool ok = i >= 0 && i < s_count && s_entries[i];
  if (ok) {
    snprintf(s_entries[i]->stats, sizeof(s_entries[i]->stats), "%s", json);
  }
  unlock();
  return ok;
}

bool plugins_stats(const char *id, char *out, int cap) {
  lock();
  int i = find(id);
  bool ok = i >= 0 && s_entries[i]->stats[0];
  if (ok) {
    snprintf(out, cap, "%s", s_entries[i]->stats);
  }
  unlock();
  return ok;
}

bool plugins_info(int i, plugin_info_t *out) {
  memset(out, 0, sizeof(*out));
  lock();
  const entry_t *e = i >= 0 && i < s_count ? s_entries[i] : NULL;
  if (e) {
    out->state = e->state;
    snprintf(out->id, sizeof(out->id), "%s", e->id);
    snprintf(out->name, sizeof(out->name), "%s", e->name);
    snprintf(out->version, sizeof(out->version), "%s", e->version);
    memcpy(out->hosts, e->hosts, sizeof(out->hosts));
    out->host_count = e->host_count;
    out->size = e->size;
    out->pages = e->pages;
    snprintf(out->error, sizeof(out->error), "%s", e->error);
    out->draws = e->draws;
    out->draw_us_max = e->draw_us_max;
    out->load_us = e->load_us;
    out->psram = e->psram;
  }
  unlock();
  return e != NULL;
}

char *plugins_manifest(int i, int *len) {
  char *copy = NULL;
  lock();
  const entry_t *e = i >= 0 && i < s_count ? s_entries[i] : NULL;
  if (e && e->manifest) {
    copy = s_hooks.alloc(e->manifest_len + 1);
    if (copy) {
      memcpy(copy, e->manifest, e->manifest_len + 1);
      *len = e->manifest_len;
    }
  }
  unlock();
  return copy;
}

void plugins_free(void *p) {
  s_hooks.free(p);
}
