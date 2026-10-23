// Installed plugins on flash: each one's module, in two namespaces of the settings
// partition (nvs_cfg) under the plugin id, and a record naming the namespace that holds
// it, with its size and SHA-256. The module carries its own manifest, read again at every
// load. Room for the core's own settings is always kept. No runtime: firmware/test/store
// runs this file on the Mac.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "manifest.h"

#define STORE_ID_MAX MANIFEST_ID_MAX
// Three NVS pages are always left to the settings, so an install can never be what
// makes a settings save fail.
#define STORE_RESERVE_BYTES (3 * 126 * 32)

typedef enum {
  STORE_OK,
  STORE_ERR_HASH,       // the module on flash is not the one its record names
  STORE_ERR_SPACE,      // does not fit in the plugin storage
  STORE_ERR_FLASH,      // the flash write or read failed
  STORE_ERR_NOT_FOUND,  // not installed
  STORE_ERR_MEMORY,     // no memory to read the module into
} store_err_t;

// A short stable code for the web page ("space", "flash", ...); it words them.
const char *store_err_code(store_err_t e);

// Once at boot: turns records an older firmware wrote into this one's, and erases
// modules no record names (an install cut short).
void store_init(void);

// Keeps a module under id: writes it to the bank the current version does not use, then
// the record (whose presence is what "installed" means and which names the bank). An
// earlier version stays whole until the record switches, and its module goes after.
store_err_t store_save(const char *id, const uint8_t *code, uint32_t len);

// Forgets a plugin: its record, its module and, with erase_settings, its settings.
store_err_t store_remove(const char *id, bool erase_settings);

// The installed module's size.
store_err_t store_size(const char *id, uint32_t *len);

// The installed module, in a buffer from alloc (freed by the caller), checked against
// its record's SHA-256.
store_err_t store_read(const char *id, uint8_t **code, uint32_t *len, void *(*alloc)(size_t size),
                       void (*release)(void *p));

// The ids of every installed plugin, in a malloc'd array (freed by the caller): how many,
// or -1 when there was no memory for the list.
int store_ids(char (**out)[STORE_ID_MAX]);

// What an install may still use (the settings reserve kept back) and the whole settings
// partition, in bytes.
typedef struct {
  size_t free;
  size_t partition;
} store_space_t;

void store_space(store_space_t *out);

// NVS bytes one installed plugin with a module of this size takes, at most.
size_t store_install_bytes(uint32_t module_size);

// Whether a module of this size fits now, an update's second copy included.
bool store_fits(uint32_t module_size);
