// Screen plugins: WebAssembly modules kept on flash (wasm_store.h) and run from the
// table in wasm_plugins.h. Every plugin is one: the standard ones ship in the image and
// are installed at the first boot and after a factory reset, and are deleted and
// installed again like any other. Everything the runtime allocates lives in PSRAM;
// without PSRAM no plugin runs.
#pragma once

#include <stdbool.h>
#include "esp_http_server.h"

// What the host needs from the core, which owns the timers and the lock the table's
// readers take.
typedef struct {
  void (*timers_stop)(int index);
  void (*lock)(void);
  void (*unlock)(void);
} wasm_host_hooks_t;

// Starts the runtime, loads the installed plugins, and installs the standard ones the
// first time. Once, on the UI loop, before the core shows a screen.
void wasm_host_init(const wasm_host_hooks_t *hooks);

// From the UI loop, once a second: does a requested deletion or an install, or, from
// WASM_RESTORE_AFTER_TICKS on, puts back a standard plugin whose stored module did not
// read at boot. Returns the table index that now holds another plugin or none, so the
// core re-reads its screens; else -1.
int wasm_host_tick(void);

// Ticks after boot before a standard plugin is restored: the network and the first
// screens are up by then, and the restore runs as an install from the web page does.
#define WASM_RESTORE_AFTER_TICKS 30

// GET /plugins, POST /plugins/upload (the module itself), POST /plugins/install {"id"} (a
// standard plugin, from its copy in the image), POST /plugins/remove {"id"}, all four
// behind sign-in.
esp_err_t wasm_host_http_register(httpd_handle_t srv);

// Calls fn with each standard plugin that is not installed: its id and its manifest's
// text; false from fn stops. Any task.
void wasm_host_missing(bool (*fn)(const char *id, const char *text, int len, void *ctx), void *ctx);

// The free PSRAM now and at its lowest, in bytes; both 0 without PSRAM.
void wasm_host_psram(unsigned *free_now, unsigned *free_min);
