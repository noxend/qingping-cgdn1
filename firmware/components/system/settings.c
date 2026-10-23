#include "settings.h"

#include <string.h>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

// Settings live on their own partition, so a full or corrupt settings store is
// erased on its own and the Wi-Fi credentials on the default partition survive.
#define PART "nvs_cfg"
#define NS "cfg"
#define BLOB_MAX SETTINGS_OBJECT_MAX

static const char *TAG = "settings";
static bool s_ready;

// Settings written before the partition split sit in namespace "cfg" of the
// default partition; copy them over once, then drop them there.
static void migrate(void) {
  nvs_handle_t old;
  // Read-only: a read-write open would create the namespace on a device that never
  // had it, one flash write on every boot for nothing.
  if (nvs_open(NS, NVS_READONLY, &old) != ESP_OK) {
    return;
  }
  nvs_handle_t new;
  if (nvs_open_from_partition(PART, NS, NVS_READWRITE, &new) != ESP_OK) {
    nvs_close(old);
    return;
  }
  int moved = 0;
  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_STR, &it);
  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    char buf[BLOB_MAX];
    size_t len = sizeof(buf);
    bool read = nvs_get_str(old, info.key, buf, &len) == ESP_OK;
    if (read && nvs_set_str(new, info.key, buf) == ESP_OK) {
      moved++;
    }
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  nvs_close(old);
  bool committed = moved > 0 && nvs_commit(new) == ESP_OK;
  nvs_close(new);
  if (committed && nvs_open(NS, NVS_READWRITE, &old) == ESP_OK) {
    nvs_erase_all(old);
    nvs_commit(old);
    nvs_close(old);
    ESP_LOGI(TAG, "moved %d settings to " PART, moved);
  }
}

void settings_init(void) {
  esp_err_t e = nvs_flash_init_partition(PART);
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase_partition(PART);
    e = nvs_flash_init_partition(PART);
  }
  s_ready = e == ESP_OK;
  if (!s_ready) {
    ESP_LOGE(TAG, "partition " PART ": %s (settings read as defaults)", esp_err_to_name(e));
    return;
  }
  migrate();
}

// Loads the screen's JSON object from NVS, or an empty object. Caller deletes.
static cJSON *load(const char *screen) {
  nvs_handle_t h;
  if (!s_ready || nvs_open_from_partition(PART, NS, NVS_READONLY, &h) != ESP_OK) {
    return cJSON_CreateObject();
  }
  char buf[BLOB_MAX];
  size_t len = sizeof(buf);
  esp_err_t e = nvs_get_str(h, screen, buf, &len);
  nvs_close(h);
  cJSON *o = (e == ESP_OK) ? cJSON_Parse(buf) : NULL;
  if (!cJSON_IsObject(o)) {
    cJSON_Delete(o);
    return cJSON_CreateObject();
  }
  return o;
}

void settings_get(const char *screen, const char *key, char *out, size_t cap, const char *def) {
  cJSON *o = load(screen);
  cJSON *v = cJSON_GetObjectItem(o, key);
  const char *s = cJSON_IsString(v) ? v->valuestring : def;
  if (!s) {
    s = "";
  }
  strlcpy(out, s, cap);
  cJSON_Delete(o);
}

void settings_object(const char *screen, char *out, size_t cap) {
  cJSON *o = load(screen);
  char *text = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  strlcpy(out, text && strlen(text) < cap ? text : "{}", cap);
  cJSON_free(text);
}

esp_err_t settings_set(const char *screen, const char *key, const char *value) {
  if (!s_ready) {
    return ESP_ERR_NVS_NOT_INITIALIZED;
  }
  cJSON *o = load(screen);
  cJSON *cur = cJSON_GetObjectItem(o, key);
  if (cJSON_IsString(cur) && strcmp(cur->valuestring, value) == 0) {
    cJSON_Delete(o);
    return ESP_OK;  // already stored: no flash write for a repeat
  }
  cJSON_DeleteItemFromObject(o, key);
  cJSON_AddStringToObject(o, key, value);
  char *text = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (!text) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t e = ESP_OK;
  if (strlen(text) + 1 > BLOB_MAX) {
    e = ESP_ERR_INVALID_SIZE;
  }
  nvs_handle_t h;
  if (e == ESP_OK) {
    e = nvs_open_from_partition(PART, NS, NVS_READWRITE, &h);
  }
  if (e == ESP_OK) {
    e = nvs_set_str(h, screen, text);
    if (e == ESP_OK) {
      e = nvs_commit(h);
    }
    nvs_close(h);
  }
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "set %s.%s: %s", screen, key, esp_err_to_name(e));
  }
  cJSON_free(text);
  return e;
}

#define CORE_NS "core"
#define NVS_ENTRY_BYTES 32

esp_err_t settings_ns_blob_get(const char *ns, const char *key, void *out, size_t *len) {
  nvs_handle_t h;
  esp_err_t e =
      s_ready ? nvs_open_from_partition(PART, ns, NVS_READONLY, &h) : ESP_ERR_NVS_NOT_INITIALIZED;
  if (e != ESP_OK) {
    return e;
  }
  e = nvs_get_blob(h, key, out, len);
  nvs_close(h);
  return e;
}

esp_err_t settings_ns_blob_set(const char *ns, const char *key, const void *data, size_t len) {
  nvs_handle_t h;
  esp_err_t e =
      s_ready ? nvs_open_from_partition(PART, ns, NVS_READWRITE, &h) : ESP_ERR_NVS_NOT_INITIALIZED;
  if (e != ESP_OK) {
    return e;
  }
  e = nvs_set_blob(h, key, data, len);
  if (e == ESP_OK) {
    e = nvs_commit(h);
  }
  nvs_close(h);
  return e;
}

esp_err_t settings_blob_get(const char *key, void *out, size_t *len) {
  return settings_ns_blob_get(CORE_NS, key, out, len);
}

esp_err_t settings_blob_set(const char *key, const void *data, size_t len) {
  return settings_ns_blob_set(CORE_NS, key, data, len);
}

static esp_err_t erase(const char *ns, const char *key) {
  nvs_handle_t h;
  esp_err_t e =
      s_ready ? nvs_open_from_partition(PART, ns, NVS_READWRITE, &h) : ESP_ERR_NVS_NOT_INITIALIZED;
  if (e != ESP_OK) {
    return e;
  }
  e = nvs_erase_key(h, key);
  if (e == ESP_ERR_NVS_NOT_FOUND) {
    e = ESP_OK;
  }
  if (e == ESP_OK) {
    e = nvs_commit(h);
  }
  nvs_close(h);
  return e;
}

esp_err_t settings_ns_erase(const char *ns, const char *key) {
  return erase(ns, key);
}

esp_err_t settings_erase_screen(const char *screen) {
  return erase(NS, screen);
}

void settings_ns_keys(const char *ns, bool (*fn)(const char *key, void *ctx), void *ctx) {
  if (!s_ready) {
    return;
  }
  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find(PART, ns, NVS_TYPE_BLOB, &it);
  bool more = true;
  while (res == ESP_OK && more) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    more = fn(info.key, ctx);
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
}

esp_err_t settings_space(settings_space_t *out) {
  nvs_stats_t st;
  esp_err_t e = s_ready ? nvs_get_stats(PART, &st) : ESP_ERR_NVS_NOT_INITIALIZED;
  if (e != ESP_OK) {
    return e;
  }
  out->used = st.used_entries * NVS_ENTRY_BYTES;
  out->available = st.available_entries * NVS_ENTRY_BYTES;
  out->total = st.total_entries * NVS_ENTRY_BYTES;
  return ESP_OK;
}
