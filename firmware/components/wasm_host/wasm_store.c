#include "wasm_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "settings.h"

// A plugin's module sits in one of two banks; an update writes the other bank and
// then the record that names it, so a power cut or a failed write at any point
// leaves the record naming a complete module.
#define NS_META "plug_meta"
static const char *const NS_CODE[2] = {"plug_code_a", "plug_code_b"};
// The record's layout. 1 (1.0.0-rc3 to rc7) kept the whole manifest; 2 keeps only what
// finds and checks the module, which carries its manifest itself.
#define LAYOUT_V1 1
#define LAYOUT 2
#define NVS_ENTRY 32
#define NVS_PAGE_DATA 4000  // a page's data, its header and bitmap left out (rounded down)

static const char *TAG = "store";

typedef struct {
  uint32_t layout;
  uint32_t bank;  // NS_CODE[bank] holds the module
  uint32_t size;
  uint8_t sha256[32];
} record_t;

const char *store_err_code(store_err_t e) {
  static const char *const CODES[] = {
      [STORE_OK] = "ok",
      [STORE_ERR_HASH] = "hash",
      [STORE_ERR_SPACE] = "space",
      [STORE_ERR_FLASH] = "flash",
      [STORE_ERR_NOT_FOUND] = "not_found",
      [STORE_ERR_MEMORY] = "memory",
  };
  return CODES[e];
}

static esp_err_t record_read(const char *id, record_t *r) {
  size_t len = sizeof(*r);
  esp_err_t e = settings_ns_blob_get(NS_META, id, r, &len);
  if (e == ESP_OK && (len != sizeof(*r) || r->layout != LAYOUT || r->bank > 1)) {
    e = ESP_ERR_INVALID_STATE;
  }
  return e;
}

static bool sha_of(const uint8_t *code, uint32_t len, uint8_t sha[32]) {
  return mbedtls_sha256(code, len, sha, 0) == 0;
}

// ---- boot ----------------------------------------------------------------------------------

typedef struct {
  char (*ids)[STORE_ID_MAX];
  int count;
  int cap;
  bool short_of_memory;
} ids_t;

static bool note_id(const char *id, void *ctx) {
  ids_t *l = ctx;
  if (l->count == l->cap) {
    int cap = l->cap ? l->cap * 2 : 8;
    char (*grown)[STORE_ID_MAX] = realloc(l->ids, cap * sizeof(*grown));
    if (!grown) {
      l->short_of_memory = true;
      return false;
    }
    l->ids = grown;
    l->cap = cap;
  }
  snprintf(l->ids[l->count++], STORE_ID_MAX, "%s", id);
  return true;
}

// The keys of a namespace, collected first: NVS keys are not written or erased while
// their namespace is iterated. -1 when there was no memory for the list.
static int keys_of(const char *ns, char (**out)[STORE_ID_MAX]) {
  ids_t l = {0};
  settings_ns_keys(ns, note_id, &l);
  *out = l.ids;
  if (l.short_of_memory) {
    free(l.ids);
    *out = NULL;
    return -1;
  }
  return l.count;
}

int store_ids(char (**out)[STORE_ID_MAX]) {
  return keys_of(NS_META, out);
}

// A layout 1 record: its layout and bank, then the old manifest, whose size and hash
// this firmware does not read: the module itself gives both. Both copies go to PSRAM: at
// boot the network and the web server already hold the internal heap.
static void upgrade_record(const char *id) {
  size_t len = 0;
  if (settings_ns_blob_get(NS_META, id, NULL, &len) != ESP_OK || len < 8) {
    return;
  }
  uint32_t *old = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  bool v1 = old && settings_ns_blob_get(NS_META, id, old, &len) == ESP_OK && old[0] == LAYOUT_V1 &&
            old[1] <= 1;
  record_t r = {.layout = LAYOUT, .bank = v1 ? old[1] : 0};
  heap_caps_free(old);
  if (!v1) {
    return;
  }
  size_t size = 0;
  uint8_t *code = NULL;
  bool ok = settings_ns_blob_get(NS_CODE[r.bank], id, NULL, &size) == ESP_OK &&
            (code = heap_caps_malloc(size, MALLOC_CAP_SPIRAM)) != NULL &&
            settings_ns_blob_get(NS_CODE[r.bank], id, code, &size) == ESP_OK &&
            sha_of(code, (uint32_t) size, r.sha256);
  heap_caps_free(code);
  r.size = (uint32_t) size;
  if (ok && settings_ns_blob_set(NS_META, id, &r, sizeof(r)) == ESP_OK) {
    ESP_LOGI(TAG, "%s: record moved to layout %d", id, LAYOUT);
  }
}

void store_init(void) {
  char (*ids)[STORE_ID_MAX] = NULL;
  int n = keys_of(NS_META, &ids);
  for (int k = 0; k < n; k++) {
    upgrade_record(ids[k]);
  }
  free(ids);
  // A module with no record, or whose record names the other bank: the module of an
  // install or update that lost power before its record was written. A record that does
  // not read (one the upgrade above could not convert, a read error) names nothing for
  // sure, so its modules stay.
  for (int bank = 0; bank < 2; bank++) {
    n = keys_of(NS_CODE[bank], &ids);
    for (int k = 0; k < n; k++) {
      record_t r;
      size_t len = 0;
      bool none = settings_ns_blob_get(NS_META, ids[k], NULL, &len) == ESP_ERR_NVS_NOT_FOUND;
      bool other = record_read(ids[k], &r) == ESP_OK && (int) r.bank != bank;
      if (none || other) {
        ESP_LOGW(TAG, "erasing the orphaned module %s", ids[k]);
        settings_ns_erase(NS_CODE[bank], ids[k]);
      }
    }
    free(ids);
  }
}

// ---- space ---------------------------------------------------------------------------------

// NVS bytes a blob of len takes: its data entries, a header entry per page it spans
// (one more than its length needs when it starts part way into a page) and its index
// entry.
static size_t nvs_bytes(size_t len) {
  size_t data = (len + NVS_ENTRY - 1) / NVS_ENTRY;
  size_t pages = (len + NVS_PAGE_DATA - 1) / NVS_PAGE_DATA + 1;
  return (data + pages + 1) * NVS_ENTRY;
}

size_t store_install_bytes(uint32_t module_size) {
  // The module, its record, and the namespaces' own entries the first install creates.
  return nvs_bytes(module_size) + nvs_bytes(sizeof(record_t)) + 3 * NVS_ENTRY;
}

void store_space(store_space_t *out) {
  memset(out, 0, sizeof(*out));
  settings_space_t nvs;
  if (settings_space(&nvs) != ESP_OK) {
    return;
  }
  out->partition = nvs.total;
  out->free = nvs.available > STORE_RESERVE_BYTES ? nvs.available - STORE_RESERVE_BYTES : 0;
}

bool store_fits(uint32_t module_size) {
  store_space_t space;
  store_space(&space);
  // An update writes the new module beside the old one before it drops it.
  return store_install_bytes(module_size) <= space.free;
}

// ---- install, remove, read -----------------------------------------------------------------

store_err_t store_save(const char *id, const uint8_t *code, uint32_t len) {
  record_t old;
  bool had = record_read(id, &old) == ESP_OK;
  record_t r = {.layout = LAYOUT, .bank = had ? 1 - old.bank : 0, .size = len};
  if (!sha_of(code, len, r.sha256)) {
    return STORE_ERR_FLASH;
  }
  if (!store_fits(len)) {
    return STORE_ERR_SPACE;
  }
  const char *bank = NS_CODE[r.bank];
  esp_err_t e = settings_ns_blob_set(bank, id, code, len);
  if (e == ESP_OK) {
    // The switch: until this write lands, the record still names the old module.
    e = settings_ns_blob_set(NS_META, id, &r, sizeof(r));
  }
  if (e != ESP_OK) {
    settings_ns_erase(bank, id);
    ESP_LOGW(TAG, "storing %s: %s", id, esp_err_to_name(e));
    return e == ESP_ERR_NVS_NOT_ENOUGH_SPACE ? STORE_ERR_SPACE : STORE_ERR_FLASH;
  }
  if (had) {
    settings_ns_erase(NS_CODE[old.bank], id);
  }
  return STORE_OK;
}

store_err_t store_remove(const char *id, bool erase_settings) {
  size_t len = 0;
  if (settings_ns_blob_get(NS_META, id, NULL, &len) != ESP_OK) {
    return STORE_ERR_NOT_FOUND;
  }
  // The record first: once it is gone the plugin is not installed, whatever happens to
  // the rest, and a module left behind is erased at the next boot.
  esp_err_t e = settings_ns_erase(NS_META, id);
  for (int bank = 0; bank < 2 && e == ESP_OK; bank++) {
    e = settings_ns_erase(NS_CODE[bank], id);
  }
  if (e == ESP_OK && erase_settings) {
    e = settings_erase_screen(id);
  }
  return e == ESP_OK ? STORE_OK : STORE_ERR_FLASH;
}

store_err_t store_size(const char *id, uint32_t *len) {
  record_t r;
  esp_err_t e = record_read(id, &r);
  if (e != ESP_OK) {
    return e == ESP_ERR_NVS_NOT_FOUND ? STORE_ERR_NOT_FOUND : STORE_ERR_FLASH;
  }
  *len = r.size;
  return STORE_OK;
}

store_err_t store_read(const char *id, uint8_t **code, uint32_t *len, void *(*alloc)(size_t size),
                       void (*release)(void *p)) {
  *code = NULL;
  record_t r;
  esp_err_t e = record_read(id, &r);
  if (e != ESP_OK) {
    return e == ESP_ERR_NVS_NOT_FOUND ? STORE_ERR_NOT_FOUND : STORE_ERR_FLASH;
  }
  size_t got = r.size;
  uint8_t *buf = alloc(r.size);
  if (!buf) {
    return STORE_ERR_MEMORY;
  }
  if (settings_ns_blob_get(NS_CODE[r.bank], id, buf, &got) != ESP_OK) {
    release(buf);
    return STORE_ERR_FLASH;
  }
  uint8_t sha[32];
  if (got != r.size || !sha_of(buf, r.size, sha) || memcmp(sha, r.sha256, sizeof(sha)) != 0) {
    release(buf);
    return STORE_ERR_HASH;
  }
  *code = buf;
  *len = r.size;
  return STORE_OK;
}
