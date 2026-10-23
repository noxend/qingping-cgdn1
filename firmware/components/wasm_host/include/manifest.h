// A plugin's plugin.json (format 2), as its module carries it in the custom section
// "cgdn1.plugin": checked field by field, and read again for its settings. Read in
// place with json_scan.h, so a manifest costs no heap. No firmware headers:
// firmware/test/store runs it on the Mac.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The custom section that carries it (plugins/sdk/embed_manifest.py).
#define MANIFEST_SECTION "cgdn1.plugin"
// The longest section the device reads.
#define MANIFEST_MAX 8192
// The plugin SDK this firmware runs (plugins/sdk/plugin_wasm.h); a manifest names
// the one its module was built against.
#define MANIFEST_SDK 2

#define MANIFEST_ID_MAX 16  // 15 characters, the NVS key limit, and the NUL
#define MANIFEST_HOSTS_MAX 4

// What the device itself needs of a manifest; the web page reads the rest from the
// text. name is the English one: the device's own screens are in English.
typedef struct {
  char id[MANIFEST_ID_MAX];
  char name[48];
  char version[16];
  char category[16];
  int sdk;
  char hosts[MANIFEST_HOSTS_MAX][64];
  int host_count;
} manifest_t;

// 1 to 15 of a-z, 0-9 and _, and not "sys" (the core's own settings object).
bool manifest_id_ok(const char *id);

// Checks the text and fills m. NULL when it is a manifest this firmware takes; else what
// is wrong: "json" (not JSON, over MANIFEST_MAX, nested over 8 deep, or a key repeated in
// an object), "sdk"
// (built for a newer SDK), "old_sdk" (for an older one: plugin.json format 1), "fields"
// (a field the format does not have), or the name of the first field missing or out of
// its rules.
const char *manifest_parse(const char *js, int len, manifest_t *m);

// For a checked manifest: whether value is one the setting `key` takes (an option's
// key, "0" or "1", an integer, a short text).
bool manifest_value_ok(const char *js, int len, const char *key, const char *value);

// For a checked manifest: when `key` is a select and value is not one of its keys but
// one of its options' labels, in any language, that option's key into out (cap bytes).
bool manifest_key_for_label(const char *js, int len, const char *key, const char *value, char *out,
                            int cap);

// Calls fn with the key of every select setting; false from fn stops.
void manifest_selects(const char *js, int len, bool (*fn)(const char *key, void *ctx), void *ctx);
