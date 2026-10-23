// Shared by the two halves of the screen manager inside the ui component:
// screen_manager.c (the UI loop) and screen_http.c (its HTTP endpoints).
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

// A request to the UI loop, answered on `done`: draw screen `idx` off-screen
// into `fb`, or with `live` copy the panel's framebuffer into `fb`; either way
// read the screen's LED colour. It lives on the heap and is shared by the HTTP
// task and the UI loop: whichever lets go last frees it, so the HTTP task can
// stop waiting on a loop that does not answer and the loop can still finish
// into a buffer that exists.
typedef struct {
  int idx;
  bool live;
  uint8_t *fb;
  bool led_on;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  SemaphoreHandle_t done;
  atomic_int refs;
} preview_t;

void preview_release(preview_t *p);

// GET /screen: screen idx drawn off-screen, or with live the panel's framebuffer,
// on the UI loop. Before the first screen is shown (idx < 0) the frame is blank.
// NULL when the frame cannot be had, with *error saying why.
preview_t *screen_preview(int idx, bool live, const char **error);

// The table index of the plugin `id`, or -1.
int screen_index(const char *id);

// Held while the plugin table or the core's screens change (the WASM host takes it) and
// while the HTTP task reads them. Recursive.
void screen_registry_lock(void);
void screen_registry_unlock(void);
// The stored enabled ids as a JSON array; the caller deletes it.
cJSON *screen_enabled_list(void);
// Stores the enabled ids, a JSON array, as they are.
esp_err_t screen_enabled_store(const char *json);

bool screen_disabled(int idx);
int screen_faults(int idx);

int screen_active(void);
int screen_shown(void);

// POST /config's requests to the UI loop. switch and config_changed are false
// when the loop's queue is full; the enabled list is reloaded even then.
bool screen_switch(int idx);
void screen_enabled_changed(void);
bool screen_config_changed(int idx);
