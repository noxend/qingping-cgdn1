// The installed plugins as the device runs them: a table of entries, one per plugin,
// that grows with each install while memory lasts. An entry keeps its index until the
// plugin is deleted; a deleted plugin's index is used again by the next install. Each
// entry holds the plugin's module, its manifest's text and the counters GET /plugins
// reports, and forwards the core's calls into the module under the module limits.
//
// The UI loop is the one task that changes the table and the one that calls into
// modules; it reads without the lock. Any other task reads under the hooks' lock, through
// the functions marked so. No flash, no network: firmware/test/wasm runs this file.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "manifest.h"
#include "plugin_sdk.h"

typedef enum {
  PLUGIN_FAILED,   // stored, but its module did not load; error says why
  PLUGIN_READY,    // loaded; init runs on the first call
  PLUGIN_STOPPED,  // the module trapped and was unloaded until it is installed again
} plugin_state_t;

typedef struct {
  // Stops the timers the plugin at `index` started: called when its module goes.
  void (*timers_stop)(int index);
  // Held while the table changes; recursive.
  void (*lock)(void);
  void (*unlock)(void);
  // Where entries, modules and manifests live (PSRAM on the device).
  void *(*alloc)(size_t size);
  void (*free)(void *p);
} plugins_hooks_t;

// What GET /plugins reports of an entry, copied under the lock.
typedef struct {
  plugin_state_t state;
  char id[16];
  char name[48];
  char version[16];
  char hosts[4][64];
  int host_count;
  uint32_t size;
  uint32_t pages;
  char error[128];
  int draws;
  int draw_us_max;
  int load_us;
  int psram;
} plugin_info_t;

void plugins_init(const plugins_hooks_t *hooks);

// How many indexes the table has, used or free. Any task, under the lock.
int plugins_count(void);
// The index of the plugin `id`, or -1. Any task.
int plugins_find(const char *id);
// Whether an index holds a plugin. Any task.
bool plugins_used(int i);
// The plugin's id, for the UI loop (settings are keyed by it).
const char *plugins_id(int i);

// An entry for `id`, a module of `size` bytes on flash, in state PLUGIN_FAILED with `why`
// unless it is running: at a free index or a new one. The index, or -1 when there is no
// memory for it. An entry that already has the id is reused.
int plugins_add(const char *id, uint32_t size, const char *why);

// Runs `code` (len bytes, the entry takes it: freed with the hooks' free) as the plugin
// manifest m describes, replacing what the entry ran. The manifest's text is copied.
// False with *why set when the module does not load: the entry is then PLUGIN_FAILED.
bool plugins_load(int i, const manifest_t *m, const char *text, int text_len, uint8_t *code,
                  uint32_t len, const char **why);
// What the caller measured of the load, for GET /plugins.
void plugins_measured(int i, int load_us, int psram);

// The entry stays listed with why, its module unloaded.
void plugins_fail(int i, const char *why);
// Forgets the entry; its index is free.
void plugins_drop(int i);

// Calls into the module, from the UI loop. A trap stops the plugin. plugins_start runs
// the module's init and on_config once; any other call does first if it has not run.
bool plugins_ready(int i);
void plugins_start(int i);
void plugins_draw(int i);
void plugins_event(int i, const core_event_t *ev);
void plugins_config(int i);
// The LED colour the plugin asks for: false for off.
bool plugins_led(int i, uint8_t *r, uint8_t *g, uint8_t *b);

// The counters the plugin published with core_stats, into out. Any task.
bool plugins_stats_set(int i, const char *json);
bool plugins_stats(const char *id, char *out, int cap);

// GET /plugins' view of the entry. Any task.
bool plugins_info(int i, plugin_info_t *out);
// The manifest's text and its length, copied into a buffer the caller gives back with
// plugins_free, or NULL. Any task.
char *plugins_manifest(int i, int *len);
void plugins_free(void *p);
