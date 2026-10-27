// The settings API (components/system/settings.h) over an in-memory NVS that keeps
// NVS's accounting, not the store's estimate of it: pages of 126 entries of 32 B, a
// blob split into one chunk per page it lands on (a header entry plus its data
// entries) and an index entry, one entry per namespace, one page kept back, and erased
// entries counted free as nvs_get_stats counts them (nvs_page.cpp). A write that does
// not fit fails as NVS does, so the store's room check is measured against this.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "settings.h"

#define PAGE_ENTRIES 126
#define ENTRY 32
#define MAX_ITEMS 512

typedef struct {
  char ns[16];
  char key[16];
  uint8_t *data;
  size_t len;
  int entries;
} item_t;

static item_t s_items[MAX_ITEMS];
static int s_pages = 32;           // nvs_cfg: 0x20000
static int s_used;                 // live entries
static int s_tail = PAGE_ENTRIES;  // free entries left on the page being written
static char s_namespaces[16][16];
static int s_namespace_count;
static char s_fail_ns[16];  // the next write to this namespace fails, as a flash error

// Called before every blob write, so a test can act while a write is under way.
void (*fake_nvs_before_set)(const char *ns, const char *key);

void fake_nvs_fail_next(const char *ns) {
  snprintf(s_fail_ns, sizeof(s_fail_ns), "%s", ns);
}

void fake_nvs_reset(int pages) {
  for (int i = 0; i < MAX_ITEMS; i++) {
    free(s_items[i].data);
  }
  memset(s_items, 0, sizeof(s_items));
  s_pages = pages;
  s_used = 0;
  s_tail = PAGE_ENTRIES;
  s_namespace_count = 0;
}

static int capacity(void) {
  return (s_pages - 1) * PAGE_ENTRIES;
}

// Entries a blob of len takes when written from the current tail: a chunk per page.
static int place(size_t len, int *tail) {
  int remaining = (int) ((len + ENTRY - 1) / ENTRY);
  int used = 0;
  while (remaining > 0) {
    if (*tail < 2) {
      *tail = PAGE_ENTRIES;  // the rest of the page stays free for small items
    }
    int chunk = remaining < *tail - 1 ? remaining : *tail - 1;
    used += chunk + 1;
    *tail -= chunk + 1;
    remaining -= chunk;
  }
  if (*tail < 1) {
    *tail = PAGE_ENTRIES;
  }
  *tail -= 1;  // the blob index
  return used + 1;
}

static item_t *find(const char *ns, const char *key) {
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].data && strcmp(s_items[i].ns, ns) == 0 && strcmp(s_items[i].key, key) == 0) {
      return &s_items[i];
    }
  }
  return NULL;
}

static int namespace_cost(const char *ns) {
  for (int i = 0; i < s_namespace_count; i++) {
    if (strcmp(s_namespaces[i], ns) == 0) {
      return 0;
    }
  }
  return 1;
}

esp_err_t settings_ns_blob_set(const char *ns, const char *key, const void *data, size_t len) {
  if (fake_nvs_before_set) {
    fake_nvs_before_set(ns, key);
  }
  if (strcmp(ns, s_fail_ns) == 0) {
    s_fail_ns[0] = '\0';
    return ESP_FAIL;
  }
  int tail = s_tail;
  int cost = place(len, &tail) + namespace_cost(ns);
  item_t *old = find(ns, key);
  // NVS writes the new value in full before it erases the old one.
  if (s_used + cost > capacity()) {
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  }
  if (namespace_cost(ns)) {
    strcpy(s_namespaces[s_namespace_count++], ns);
  }
  s_tail = tail;
  s_used += cost;
  if (old) {
    s_used -= old->entries;
    free(old->data);
    old->data = NULL;
  }
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (!s_items[i].data) {
      strcpy(s_items[i].ns, ns);
      strcpy(s_items[i].key, key);
      s_items[i].data = malloc(len ? len : 1);
      memcpy(s_items[i].data, data, len);
      s_items[i].len = len;
      s_items[i].entries = cost;
      return ESP_OK;
    }
  }
  return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
}

esp_err_t settings_ns_blob_get(const char *ns, const char *key, void *out, size_t *len) {
  item_t *it = find(ns, key);
  if (!it) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  if (out) {
    if (*len < it->len) {
      return ESP_FAIL;
    }
    memcpy(out, it->data, it->len);
  }
  *len = it->len;
  return ESP_OK;
}

esp_err_t settings_ns_erase(const char *ns, const char *key) {
  item_t *it = find(ns, key);
  if (it) {
    s_used -= it->entries;
    free(it->data);
    it->data = NULL;
  }
  return ESP_OK;
}

void settings_ns_keys(const char *ns, bool (*fn)(const char *key, void *ctx), void *ctx) {
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].data && strcmp(s_items[i].ns, ns) == 0 && !fn(s_items[i].key, ctx)) {
      return;
    }
  }
}

esp_err_t settings_blob_get(const char *key, void *out, size_t *len) {
  return settings_ns_blob_get("core", key, out, len);
}

esp_err_t settings_blob_set(const char *key, const void *data, size_t len) {
  return settings_ns_blob_set("core", key, data, len);
}

esp_err_t settings_erase_screen(const char *screen) {
  return settings_ns_erase("cfg", screen);
}

esp_err_t settings_space(settings_space_t *out) {
  out->used = (size_t) s_used * ENTRY;
  out->available = (size_t) (capacity() - s_used) * ENTRY;
  out->total = (size_t) s_pages * PAGE_ENTRIES * ENTRY;
  return ESP_OK;
}

// What a plugin's settings object would take, for the "settings still fit" check.
bool fake_nvs_settings_fit(size_t len) {
  int tail = s_tail;
  return s_used + place(len, &tail) <= capacity();
}

// The whole store to a file and back, so a test can boot the firmware's code again on
// what the last boot left.
void fake_nvs_save(const char *path) {
  FILE *f = fopen(path, "wb");
  fwrite(&s_pages, sizeof(s_pages), 1, f);
  fwrite(&s_used, sizeof(s_used), 1, f);
  fwrite(&s_tail, sizeof(s_tail), 1, f);
  fwrite(&s_namespace_count, sizeof(s_namespace_count), 1, f);
  fwrite(s_namespaces, sizeof(s_namespaces), 1, f);
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].data) {
      fwrite(&s_items[i], sizeof(s_items[i]), 1, f);
      fwrite(s_items[i].data, 1, s_items[i].len, f);
    }
  }
  fclose(f);
}

bool fake_nvs_load(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  fake_nvs_reset(48);
  bool ok = fread(&s_pages, sizeof(s_pages), 1, f) == 1 &&
            fread(&s_used, sizeof(s_used), 1, f) == 1 &&
            fread(&s_tail, sizeof(s_tail), 1, f) == 1 &&
            fread(&s_namespace_count, sizeof(s_namespace_count), 1, f) == 1 &&
            fread(s_namespaces, sizeof(s_namespaces), 1, f) == 1;
  for (int i = 0; ok && i < MAX_ITEMS; i++) {
    item_t it;
    if (fread(&it, sizeof(it), 1, f) != 1) {
      break;
    }
    it.data = malloc(it.len ? it.len : 1);
    ok = fread(it.data, 1, it.len, f) == it.len;
    s_items[i] = it;
  }
  fclose(f);
  return ok;
}
