// Per-screen settings, stored in NVS as one small JSON object per screen
// (namespace "cfg", key = screen id). Values are strings; the schema in each
// screen's params says how the web UI renders them.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Reads screen/key into out (NUL-terminated, truncated to cap); falls back to
// def when the screen, the key or the store is missing.
// Mounts the settings partition (nvs_cfg) and moves settings an older firmware kept
// on the default partition. Before this, every get answers the default.
void settings_init(void);

void settings_get(const char *screen, const char *key, char *out, size_t cap, const char *def);

// The longest settings object of one screen, as JSON text with its terminator.
#define SETTINGS_OBJECT_MAX 512

// The screen's whole settings object as stored (JSON text) into out; "{}" when none.
void settings_object(const char *screen, char *out, size_t cap);

// Merges {key: value} into the screen's object and persists it.
esp_err_t settings_set(const char *screen, const char *key, const char *value);

// The core's own binary records (namespace "core" on the settings partition), for
// state that is not a plugin setting, such as the web manifest. *len is the buffer
// size in and the stored size out.
esp_err_t settings_blob_get(const char *key, void *out, size_t *len);
esp_err_t settings_blob_set(const char *key, const void *data, size_t len);

// The same for a namespace of the caller's own on the settings partition (installed
// plugins keep their modules there). nvs_get_blob's rules: out NULL asks for *len.
esp_err_t settings_ns_blob_get(const char *ns, const char *key, void *out, size_t *len);
esp_err_t settings_ns_blob_set(const char *ns, const char *key, const void *data, size_t len);
// Erasing a key that is not there is not an error.
esp_err_t settings_ns_erase(const char *ns, const char *key);
// Calls fn with every key of the namespace that holds a blob; false from fn stops.
void settings_ns_keys(const char *ns, bool (*fn)(const char *key, void *ctx), void *ctx);

// Erases a screen's whole settings object, as for a plugin that was removed.
esp_err_t settings_erase_screen(const char *screen);

// The settings partition in bytes (NVS entries of 32 B): what is used, what a new
// value may still take (NVS keeps one page for itself), and the whole partition.
typedef struct {
  size_t used;
  size_t available;
  size_t total;
} settings_space_t;

esp_err_t settings_space(settings_space_t *out);
