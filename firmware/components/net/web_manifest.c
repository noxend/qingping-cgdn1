#include "web_manifest.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "core_net.h"
#include "settings.h"
#include "signed_envelope.h"

#define FETCH_EVERY_US (60LL * 60 * 1000 * 1000)
#define RETRY_US (5LL * 60 * 1000 * 1000)
#define BODY_MAX 4096

static const char *TAG = "web";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static web_manifest_t s_cur;            // seq 0: none
static uint32_t s_floor = WEB_MIN_SEQ;  // highest seq ever accepted, kept apart from the blob
static bool s_inflight;
static int64_t s_next_us;  // 0: as soon as online

// ---- shape checks ----------------------------------------------------------------

static bool sri_ok(const char *s) {
  if (strncmp(s, "sha384-", 7) != 0 || strlen(s) != 7 + 64) {
    return false;
  }
  for (const char *p = s + 7; *p; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '+' || *p == '/' || *p == '=';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// Everything the shell page writes out comes from here, so these checks are what
// keep a quote or a tag out of the HTML.
static bool manifest_ok(const web_manifest_t *m) {
  const char *prefix = WEB_ORIGIN "/v/";
  if (m->layout != WEB_LAYOUT || !envelope_terminated(m->version, sizeof(m->version)) ||
      !envelope_terminated(m->base, sizeof(m->base)) ||
      !envelope_terminated(m->js, sizeof(m->js)) ||
      !envelope_terminated(m->js_sri, sizeof(m->js_sri))) {
    return false;
  }
  for (int i = 0; i < WEB_MAX_CSS; i++) {
    if (!envelope_terminated(m->css[i], sizeof(m->css[i])) ||
        !envelope_terminated(m->css_sri[i], sizeof(m->css_sri[i]))) {
      return false;
    }
  }
  size_t pl = strlen(prefix);
  size_t bl = strlen(m->base);
  if (m->seq == 0 || strncmp(m->base, prefix, pl) != 0 || bl <= pl || m->base[bl - 1] != '/') {
    return false;
  }
  if (!envelope_path_ok(m->base + pl) || !envelope_path_ok(m->version) ||
      !envelope_path_ok(m->js) || !sri_ok(m->js_sri)) {
    return false;
  }
  if (m->css_n < 0 || m->css_n > WEB_MAX_CSS) {
    return false;
  }
  for (int i = 0; i < m->css_n; i++) {
    if (!envelope_path_ok(m->css[i]) || !sri_ok(m->css_sri[i])) {
      return false;
    }
  }
  return true;
}

// The inner payload: {"seq","version","base","js","css":[...],"integrity":{file:sri}}.
static bool from_json(const char *text, web_manifest_t *m) {
  memset(m, 0, sizeof(*m));
  cJSON *root = cJSON_Parse(text);
  const cJSON *seq = cJSON_GetObjectItem(root, "seq");
  const cJSON *css = cJSON_GetObjectItem(root, "css");
  const cJSON *integ = cJSON_GetObjectItem(root, "integrity");
  bool ok =
      cJSON_IsNumber(seq) && seq->valuedouble >= 1 && seq->valuedouble < 4294967296.0 &&
      envelope_copy_str(m->version, sizeof(m->version), cJSON_GetObjectItem(root, "version")) &&
      envelope_copy_str(m->base, sizeof(m->base), cJSON_GetObjectItem(root, "base")) &&
      envelope_copy_str(m->js, sizeof(m->js), cJSON_GetObjectItem(root, "js")) &&
      cJSON_IsArray(css) && cJSON_GetArraySize(css) <= WEB_MAX_CSS && cJSON_IsObject(integ);
  if (ok) {
    m->layout = WEB_LAYOUT;
    m->seq = (uint32_t) seq->valuedouble;
    ok = envelope_copy_str(m->js_sri, sizeof(m->js_sri), cJSON_GetObjectItem(integ, m->js));
  }
  const cJSON *c = NULL;
  if (ok) {
    cJSON_ArrayForEach(c, css) {
      int i = m->css_n;
      ok = ok && envelope_copy_str(m->css[i], sizeof(m->css[i]), c) &&
           envelope_copy_str(m->css_sri[i], sizeof(m->css_sri[i]),
                             cJSON_GetObjectItem(integ, m->css[i]));
      m->css_n++;
    }
  }
  cJSON_Delete(root);
  return ok && manifest_ok(m);
}

// Runs on the network task: the envelope (signed_envelope.h) around the payload
// above. Returns a checked web_manifest_t, or NULL.
static void *parse(const char *body, int len) {
  char *payload = signed_envelope_open(body, TAG);
  web_manifest_t *m = payload ? malloc(sizeof(*m)) : NULL;
  if (m && !from_json(payload, m)) {
    free(m);
    m = NULL;
  }
  free(payload);
  return m;
}

// ---- state -----------------------------------------------------------------------

void web_manifest_init(void) {
  uint32_t stored = 0;
  size_t slen = sizeof(stored);
  if (settings_blob_get("web_seq", &stored, &slen) == ESP_OK && slen == sizeof(stored) &&
      stored > s_floor) {
    s_floor = stored;
  }
  web_manifest_t m;
  size_t len = sizeof(m);
  if (settings_blob_get("web", &m, &len) == ESP_OK && len == sizeof(m) && manifest_ok(&m) &&
      m.seq >= s_floor) {
    taskENTER_CRITICAL(&s_mux);
    s_cur = m;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "interface %s (seq %lu)", m.version, (unsigned long) m.seq);
  }
}

void web_manifest_tick(bool online) {
  int64_t now = esp_timer_get_time();
  if (!online || s_inflight || now < s_next_us) {
    return;
  }
  s_next_us = now + RETRY_US;  // moved to an hour on success
  s_inflight = core_net_submit(NET_OWNER_CORE, WEB_MANIFEST_URL, BODY_MAX, parse);
}

void web_manifest_net_done(const core_net_result_t *r) {
  s_inflight = false;
  const web_manifest_t *m = r->parsed;
  if (!m) {
    ESP_LOGW(TAG, "manifest fetch failed: HTTP %d, %s", r->status, esp_err_to_name(r->err));
    return;
  }
  s_next_us = esp_timer_get_time() + FETCH_EVERY_US;
  if (m->seq < s_floor || m->seq <= s_cur.seq) {
    return;  // the same build, or an older one replayed
  }
  // The floor first: if the blob write then fails, the older builds stay refused.
  esp_err_t e = settings_blob_set("web_seq", &m->seq, sizeof(m->seq));
  if (e == ESP_OK) {
    e = settings_blob_set("web", m, sizeof(*m));
  }
  s_floor = m->seq;
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "storing the manifest: %s (kept in memory)", esp_err_to_name(e));
  }
  taskENTER_CRITICAL(&s_mux);
  s_cur = *m;
  taskEXIT_CRITICAL(&s_mux);
  ESP_LOGI(TAG, "interface now %s (seq %lu)", m->version, (unsigned long) m->seq);
}

bool web_manifest_get(web_manifest_t *out) {
  taskENTER_CRITICAL(&s_mux);
  *out = s_cur;
  taskEXIT_CRITICAL(&s_mux);
  return out->seq != 0;
}
