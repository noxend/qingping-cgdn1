// The screen manager's HTTP endpoints: GET/POST /config, GET /screen,
// GET /plugin/stats and GET /alerts. They talk to the UI loop through
// screen_internal.h and never touch a screen's state themselves.
#include "screen.h"
#include "screen_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "json_out.h"
#include "manifest.h"
#include "net.h"
#include "night.h"
#include "nvs.h"
#include "oled.h"
#include "sensors.h"
#include "settings.h"
#include "temp_offset.h"
#include "time_zone.h"
#include "util.h"
#include "wasm_host.h"
#include "wasm_plugins.h"

// The longest POST /config: a settings change, or the enabled list, which grows with
// the plugins installed (an id and its quotes and comma each).
#define CONFIG_BODY_MIN 512
#define CONFIG_BODY_PER_PLUGIN (MANIFEST_ID_MAX + 3)

static const char *TAG = "screens";

// The catalog as it is written: a comma before every entry but the first.
typedef struct {
  json_out_t o;
  bool first;
} catalog_t;

static void entry_start(catalog_t *c) {
  json_out_raw(&c->o, ",", c->first ? 0 : 1);
  c->first = false;
}

// One installed plugin: its manifest as the module carries it (checked JSON), its
// settings, and how it runs.
static void installed_json(catalog_t *c, int i) {
  static const char *const STATES[] = {"failed", "ready", "stopped"};
  plugin_info_t info;
  if (!plugins_info(i, &info)) {
    return;
  }
  int len = 0;
  char *manifest = plugins_manifest(i, &len);
  char values[SETTINGS_OBJECT_MAX];
  settings_object(info.id, values, sizeof(values));
  json_out_t *o = &c->o;
  entry_start(c);
  json_out_raw(o, "{\"id\":", 6);
  json_out_str(o, info.id);
  screen_registry_lock();
  bool disabled = screen_disabled(i);
  int faults = screen_faults(i);
  screen_registry_unlock();
  json_out_printf(o,
                  ",\"installed\":true,\"state\":\"%s\",\"active\":%s,\"faults\":%d,"
                  "\"disabled\":%s,\"error\":",
                  STATES[info.state], i == screen_active() ? "true" : "false", faults,
                  disabled ? "true" : "false");
  json_out_str(o, info.error);
  json_out_raw(o, ",\"manifest\":", 12);
  if (manifest) {
    json_out_raw(o, manifest, len);
  } else {
    json_out_raw(o, "null", 4);
  }
  json_out_raw(o, ",\"values\":", 10);
  json_out_raw(o, values, strlen(values));
  json_out_raw(o, "}", 1);
  plugins_free(manifest);
}

// A standard plugin that is not installed: what the store shows to install it again.
static bool missing_json(const char *id, const char *text, int len, void *ctx) {
  catalog_t *c = ctx;
  json_out_t *o = &c->o;
  entry_start(c);
  json_out_raw(o, "{\"id\":", 6);
  json_out_str(o, id);
  json_out_raw(o, ",\"installed\":false,\"manifest\":", 30);
  json_out_raw(o, text, len);
  json_out_raw(o, "}", 1);
  return true;
}

// GET /config: {"catalog":[...],"enabled":[id,...],"night":{...},"temp_offset":°C,
// "time_zone":key},
// sent as it is written, one plugin at a time, so it has no size of its own.
static esp_err_t config_get(httpd_req_t *req) {
  catalog_t c = {.first = true};
  json_out_t *out = &c.o;
  json_out_begin(out, req);
  json_out_raw(out, "{\"catalog\":[", 12);
  for (int i = 0; i < plugins_count(); i++) {
    installed_json(&c, i);
  }
  wasm_host_missing(missing_json, &c);
  json_out_raw(out, "],\"enabled\":[", 13);
  // A disabled plugin stays in the stored list (it is back after a reboot) but is not
  // reported as enabled, so the page agrees with the panel.
  cJSON *stored = screen_enabled_list();
  bool first = true;
  cJSON *e = NULL;
  cJSON_ArrayForEach(e, stored) {
    int idx = cJSON_IsString(e) ? screen_index(e->valuestring) : -1;
    screen_registry_lock();
    bool on = idx >= 0 && !screen_disabled(idx);
    screen_registry_unlock();
    if (on) {
      json_out_raw(out, first ? "" : ",", first ? 0 : 1);
      json_out_str(out, e->valuestring);
      first = false;
    }
  }
  cJSON_Delete(stored);
  json_out_raw(out, "],\"night\":", 10);
  cJSON *night = night_json();
  char *text = cJSON_PrintUnformatted(night);
  cJSON_Delete(night);
  json_out_raw(out, text ? text : "null", text ? strlen(text) : 4);
  cJSON_free(text);
  json_out_printf(out, ",\"temp_offset\":%.1f", sensors_temp_offset() / 10.0);
  json_out_raw(out, ",\"time_zone\":", 13);
  json_out_str(out, net_time_zone());
  json_out_raw(out, "}", 1);
  return json_out_end(out);
}

// Writes one setting and answers the request when that fails, so "saved" is only
// ever sent for values that are in flash.
static bool store(httpd_req_t *req, const char *screen, const char *key, const char *value) {
  esp_err_t e = settings_set(screen, key, value);
  if (e == ESP_OK) {
    return true;
  }
  ESP_LOGE(TAG, "store %s.%s: %s", screen, key, esp_err_to_name(e));
  if (e == ESP_ERR_INVALID_SIZE) {
    http_error(req, HTTPD_413_CONTENT_TOO_LARGE, "value too long");
  } else {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "store failed");
  }
  return false;
}

// The body of POST /config, NUL-terminated, or NULL after answering the request. A
// cross-site POST with a JSON content type needs a CORS preflight, which this server
// never allows: a second wall behind sign-in.
static char *config_body(httpd_req_t *req) {
  char ctype[32] = {0};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype)) != ESP_OK ||
      strncmp(ctype, "application/json", 16) != 0) {
    http_error(req, HTTPD_400_BAD_REQUEST, "need application/json");
    return NULL;
  }
  int most = CONFIG_BODY_MIN + plugins_count() * CONFIG_BODY_PER_PLUGIN;
  if (req->content_len <= 0 || req->content_len > most) {
    http_error(req, HTTPD_400_BAD_REQUEST, "bad length");
    return NULL;
  }
  char *body = malloc(req->content_len + 1);
  if (!body) {
    http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    return NULL;
  }
  if (!recv_exact(req, body, req->content_len)) {
    free(body);
    http_error(req, HTTPD_400_BAD_REQUEST, "recv");
    return NULL;
  }
  body[req->content_len] = '\0';
  return body;
}

// {"enabled":[id,...]} replaces the enabled list with the given ids, keeping only
// ones that exist.
static esp_err_t post_enabled(httpd_req_t *req, cJSON *enabled) {
  cJSON *keep = cJSON_CreateArray();
  cJSON *e = NULL;
  cJSON_ArrayForEach(e, enabled) {
    if (cJSON_IsString(e) && screen_index(e->valuestring) >= 0) {
      cJSON_AddItemToArray(keep, cJSON_CreateString(e->valuestring));
    }
  }
  char *text = cJSON_PrintUnformatted(keep);
  cJSON_Delete(keep);
  if (!text) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
  }
  esp_err_t stored = screen_enabled_store(text);
  cJSON_free(text);
  if (stored != ESP_OK) {
    ESP_LOGE(TAG, "store the screens: %s", esp_err_to_name(stored));
    bool full = stored == ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, full ? "space" : "store failed");
  }
  screen_enabled_changed();
  httpd_resp_sendstr(req, "saved\n");
  return ESP_OK;
}

// {"night":{...}} replaces the night profile; the UI loop applies it within a second.
static esp_err_t post_night(httpd_req_t *req, cJSON *night) {
  night_profile_t profile;
  if (!night_from_json(night, &profile)) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "bad night profile");
  }
  if (!night_store(&profile)) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "store failed");
  }
  httpd_resp_sendstr(req, "saved\n");
  return ESP_OK;
}

// {"temp_offset":°C} sets the temperature calibration; the next reading has it.
static esp_err_t post_temp_offset(httpd_req_t *req, cJSON *offset) {
  int16_t tenths = 0;
  if (!cJSON_IsNumber(offset) || !temp_offset_from_celsius(offset->valuedouble, &tenths)) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "bad temp_offset");
  }
  if (!sensors_set_temp_offset(tenths)) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "store failed");
  }
  httpd_resp_sendstr(req, "saved\n");
  return ESP_OK;
}

// {"time_zone":key} switches the clock to that zone; the panel follows on its next redraw.
static esp_err_t post_time_zone(httpd_req_t *req, cJSON *zone) {
  if (!cJSON_IsString(zone) || !time_zone_posix(zone->valuestring)) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "bad time_zone");
  }
  if (!net_set_time_zone(zone->valuestring)) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "store failed");
  }
  httpd_resp_sendstr(req, "saved\n");
  return ESP_OK;
}

// {"active":"<id>"} shows that screen now and remembers the choice.
static esp_err_t post_active(httpd_req_t *req, const char *id) {
  int idx = screen_index(id);
  if (idx < 0) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "unknown screen");
  }
  if (!screen_switch(idx)) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
  }
  if (!store(req, "sys", "active", id)) {
    return ESP_FAIL;  // shown now, but the next boot goes back; the error says so
  }
  httpd_resp_sendstr(req, "saved\n");
  return ESP_OK;
}

// {"screen":"<id>","values":{key:value,...}}: the values are checked against a copy of
// the plugin's manifest and stored under the id they were checked against.
static esp_err_t post_values(httpd_req_t *req, cJSON *id, cJSON *values) {
  int idx = cJSON_IsString(id) ? screen_index(id->valuestring) : -1;
  int len = 0;
  char *manifest = idx >= 0 ? plugins_manifest(idx, &len) : NULL;
  if (!manifest || !cJSON_IsObject(values)) {
    plugins_free(manifest);
    return http_error(req, HTTPD_400_BAD_REQUEST, "unknown screen");
  }
  cJSON *v = NULL;
  cJSON_ArrayForEach(v, values) {
    if (!cJSON_IsString(v) || !manifest_value_ok(manifest, len, v->string, v->valuestring)) {
      plugins_free(manifest);
      return http_error(req, HTTPD_400_BAD_REQUEST, "bad value");
    }
  }
  plugins_free(manifest);
  char screen_id[MANIFEST_ID_MAX];
  snprintf(screen_id, sizeof(screen_id), "%s", id->valuestring);
  cJSON_ArrayForEach(v, values) {
    if (!store(req, screen_id, v->string, v->valuestring)) {
      // The values before this one are in flash: let the screen re-read them so
      // what it shows and what GET /config reports agree.
      screen_config_changed(idx);
      return ESP_FAIL;
    }
  }
  if (!screen_config_changed(idx)) {
    // Stored, but the screen did not re-read it; the same request again applies it.
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
  }
  httpd_resp_sendstr(req, "saved\n");
  return ESP_OK;
}

// One body changes one thing: the first of these keys it has decides which.
static esp_err_t config_apply(httpd_req_t *req, cJSON *root) {
  cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
  if (cJSON_IsArray(enabled)) {
    return post_enabled(req, enabled);
  }
  cJSON *night = cJSON_GetObjectItem(root, "night");
  if (night) {
    return post_night(req, night);
  }
  cJSON *offset = cJSON_GetObjectItem(root, "temp_offset");
  if (offset) {
    return post_temp_offset(req, offset);
  }
  cJSON *zone = cJSON_GetObjectItem(root, "time_zone");
  if (zone) {
    return post_time_zone(req, zone);
  }
  cJSON *active = cJSON_GetObjectItem(root, "active");
  if (cJSON_IsString(active)) {
    return post_active(req, active->valuestring);
  }
  return post_values(req, cJSON_GetObjectItem(root, "screen"), cJSON_GetObjectItem(root, "values"));
}

static esp_err_t config_post(httpd_req_t *req) {
  char *body = config_body(req);
  if (!body) {
    return ESP_FAIL;
  }
  cJSON *root = cJSON_Parse(body);
  free(body);
  esp_err_t e = config_apply(req, root);
  cJSON_Delete(root);
  return e;
}

// GET /screen: the current framebuffer (OLED_FB_BYTES, 4-bit pixels) so the web
// page can mirror what the panel shows.
// GET /screen renders the live active screen; GET /screen?id=<screen> renders
// that screen off-screen instead, so the web can preview every plugin at once.
// The X-Led header carries the screen's indicator colour ("#RRGGBB" or "off").
// GET /plugin/stats?id=<id>: the counters the plugin last published (core_stats).
// GET /alerts is the same for the alert map, kept for the web page and the soak.
static esp_err_t stats_for(httpd_req_t *req, const char *id) {
  char buf[256];
  if (!plugins_stats(id, buf, sizeof(buf))) {
    return http_error(req, HTTPD_404_NOT_FOUND, "no stats for that plugin");
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, buf);
  return ESP_OK;
}

static esp_err_t stats_get(httpd_req_t *req) {
  char query[48] = {0};
  char id[24] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "need ?id=");
  }
  return stats_for(req, id);
}

static esp_err_t alerts_get(httpd_req_t *req) {
  return stats_for(req, "alerts");
}

static esp_err_t screen_get(httpd_req_t *req) {
  char query[48] = {0};
  char id[24] = {0};
  bool want_preview = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                      httpd_query_key_value(query, "id", id, sizeof(id)) == ESP_OK;
  int idx = want_preview ? screen_index(id) : screen_shown();
  if (want_preview && idx < 0) {
    return http_error(req, HTTPD_400_BAD_REQUEST, "unknown screen");
  }
  const char *error = NULL;
  preview_t *p = screen_preview(idx, !want_preview, &error);
  if (!p) {
    return http_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, error);
  }
  char led[16];
  if (p->led_on) {
    snprintf(led, sizeof(led), "#%02X%02X%02X", p->r, p->g, p->b);
  } else {
    strcpy(led, "off");
  }
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Led", led);
  esp_err_t e = httpd_resp_send(req, (const char *) p->fb, OLED_FB_BYTES);
  preview_release(p);
  return e;
}

esp_err_t screen_http_register(httpd_handle_t srv) {
  const httpd_uri_t uris[] = {
      {.uri = "/config", .method = HTTP_GET, .handler = config_get},
      {.uri = "/config", .method = HTTP_POST, .handler = config_post},
      {.uri = "/screen", .method = HTTP_GET, .handler = screen_get},
      {.uri = "/plugin/stats", .method = HTTP_GET, .handler = stats_get},
      {.uri = "/alerts", .method = HTTP_GET, .handler = alerts_get},
  };
  for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
    esp_err_t e = http_register(srv, &uris[i]);
    if (e != ESP_OK) {
      return e;
    }
  }
  return ESP_OK;
}
