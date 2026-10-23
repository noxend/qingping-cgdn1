// The per-screen settings of components/system/settings.c over the in-memory NVS of
// fake_nvs.c: one JSON object per screen, in namespace "cfg", read and merged with cJSON.
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "settings.h"

static cJSON *load(const char *screen) {
  char text[SETTINGS_OBJECT_MAX] = "";
  size_t len = sizeof(text) - 1;
  cJSON *o = NULL;
  if (settings_ns_blob_get("cfg", screen, text, &len) == ESP_OK) {
    text[len] = '\0';
    o = cJSON_Parse(text);
  }
  if (!cJSON_IsObject(o)) {
    cJSON_Delete(o);
    o = cJSON_CreateObject();
  }
  return o;
}

void settings_get(const char *screen, const char *key, char *out, size_t cap, const char *def) {
  cJSON *o = load(screen);
  const cJSON *v = cJSON_GetObjectItem(o, key);
  const char *s = cJSON_IsString(v) ? v->valuestring : (def ? def : "");
  snprintf(out, cap, "%s", s);
  cJSON_Delete(o);
}

esp_err_t settings_set(const char *screen, const char *key, const char *value) {
  cJSON *o = load(screen);
  cJSON_DeleteItemFromObject(o, key);
  cJSON_AddStringToObject(o, key, value);
  char *text = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  esp_err_t e = settings_ns_blob_set("cfg", screen, text, strlen(text));
  cJSON_free(text);
  return e;
}

void settings_object(const char *screen, char *out, size_t cap) {
  cJSON *o = load(screen);
  char *text = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  snprintf(out, cap, "%s", text);
  cJSON_free(text);
}
