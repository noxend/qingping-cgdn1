#include "fw_manifest.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "core_net.h"
#include "dev_switches.h"
#include "net.h"
#include "settings.h"
#include "signed_envelope.h"

// A development build may point the channel at a test stand (DEV_FW_ORIGIN).
#if DEV_FW_STAND
#define FW_ORIGIN DEV_FW_ORIGIN
#else
#define FW_ORIGIN WEB_ORIGIN
#endif
#define FW_MANIFEST_URL FW_ORIGIN "/firmware.json"
#define FETCH_EVERY_US (60LL * 60 * 1000 * 1000)
// The first check waits out the boot burst (the setup AP, the alerts fetch, the web
// manifest); a failure backs off from five minutes to an hour.
#define FIRST_DELAY_US (3LL * 60 * 1000 * 1000)
#define RETRY_MIN_US (5LL * 60 * 1000 * 1000)
#define RETRY_MAX_US (60LL * 60 * 1000 * 1000)
#define CHECK_NOW_MIN_US (30LL * 1000 * 1000)
#define BODY_MAX 4096
// Both OTA slots are 0x1E0000 (partitions.csv); fw_update.c checks the real slot.
#define IMAGE_MAX 0x1E0000

static const char *TAG = "fw";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static fw_manifest_t s_cur;            // seq 0: none
static uint32_t s_floor = FW_MIN_SEQ;  // highest seq ever accepted, kept apart from the blob
static char s_running[24];
static bool s_inflight;
static bool s_was_online;
static int64_t s_next_us;
static int64_t s_retry_us = RETRY_MIN_US;
static volatile bool s_check_now;    // set from the HTTP task, taken by the tick
static volatile bool s_last_failed;  // the latest fetch got no usable manifest

// ---- shape checks ----------------------------------------------------------------

// "1", "0.27.6": digits separated by single dots.
static bool version_ok(const char *s) {
  if (!s[0] || s[0] == '.') {
    return false;
  }
  for (const char *p = s; *p; p++) {
    bool digit = *p >= '0' && *p <= '9';
    bool dot = *p == '.' && p[1] != '.' && p[1] != '\0';
    if (!digit && !dot) {
      return false;
    }
  }
  return true;
}

static bool notes_ok(const char *s) {
  for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
    if (*p < 0x20 || *p == 0x7f || *p == '"' || *p == '\\') {
      return false;
    }
  }
  return true;
}

static bool manifest_ok(const fw_manifest_t *m) {
  const char *prefix = FW_ORIGIN "/fw/";
  size_t pl = strlen(prefix);
  bool strings = envelope_terminated(m->version, sizeof(m->version)) &&
                 envelope_terminated(m->url, sizeof(m->url)) &&
                 envelope_terminated(m->notes, sizeof(m->notes));
  if (m->layout != FW_LAYOUT || !strings || m->seq == 0) {
    return false;
  }
  if (m->size == 0 || m->size > IMAGE_MAX) {
    return false;
  }
  if (strncmp(m->url, prefix, pl) != 0 || !envelope_path_ok(m->url + pl)) {
    return false;
  }
  return version_ok(m->version) && notes_ok(m->notes);
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

// 64 lowercase hex digits into 32 bytes.
static bool hex32(const cJSON *v, uint8_t out[32]) {
  if (!cJSON_IsString(v) || strlen(v->valuestring) != 64) {
    return false;
  }
  for (int i = 0; i < 32; i++) {
    int hi = hex_val(v->valuestring[2 * i]);
    int lo = hex_val(v->valuestring[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = (uint8_t) (hi << 4 | lo);
  }
  return true;
}

static bool number_in(const cJSON *v, double lo, double hi) {
  return cJSON_IsNumber(v) && v->valuedouble >= lo && v->valuedouble < hi;
}

static bool from_json(const char *text, fw_manifest_t *m) {
  memset(m, 0, sizeof(*m));
  cJSON *root = cJSON_Parse(text);
  const cJSON *kind = cJSON_GetObjectItem(root, "kind");
  const cJSON *seq = cJSON_GetObjectItem(root, "seq");
  const cJSON *size = cJSON_GetObjectItem(root, "size");
  bool ok =
      cJSON_IsString(kind) && strcmp(kind->valuestring, "firmware") == 0 &&
      number_in(seq, 1, 4294967296.0) && number_in(size, 1, IMAGE_MAX + 1) &&
      envelope_copy_str(m->version, sizeof(m->version), cJSON_GetObjectItem(root, "version")) &&
      envelope_copy_str(m->url, sizeof(m->url), cJSON_GetObjectItem(root, "url")) &&
      envelope_copy_str(m->notes, sizeof(m->notes), cJSON_GetObjectItem(root, "notes")) &&
      hex32(cJSON_GetObjectItem(root, "sha256"), m->sha256);
  if (ok) {
    m->layout = FW_LAYOUT;
    m->seq = (uint32_t) seq->valuedouble;
    m->size = (uint32_t) size->valuedouble;
  }
  cJSON_Delete(root);
  return ok && manifest_ok(m);
}

// Runs on the network task. Returns a checked fw_manifest_t, or NULL.
static void *parse(const char *body, int len) {
  char *payload = signed_envelope_open(body, TAG);
  fw_manifest_t *m = payload ? malloc(sizeof(*m)) : NULL;
  if (m && !from_json(payload, m)) {
    free(m);
    m = NULL;
  }
  free(payload);
  return m;
}

// ---- versions --------------------------------------------------------------------

int fw_version_cmp(const char *a, const char *b) {
  while (*a || *b) {
    char *end_a;
    char *end_b;
    unsigned long x = strtoul(a, &end_a, 10);
    unsigned long y = strtoul(b, &end_b, 10);
    if (x != y) {
      return x < y ? -1 : 1;
    }
    if (end_a == a && end_b == b) {
      // No number on either side: a pre-release suffix ("-rc1") after equal
      // numbers is the lower version, two suffixes compare as text.
      if (*a == '\0' && *b != '\0') {
        return 1;
      }
      if (*b == '\0' && *a != '\0') {
        return -1;
      }
      return strcmp(a, b);
    }
    a = end_a + (*end_a == '.');
    b = end_b + (*end_b == '.');
  }
  return 0;
}

// ---- state -----------------------------------------------------------------------

void fw_manifest_init(const char *running) {
  strlcpy(s_running, running, sizeof(s_running));
  uint32_t stored = 0;
  size_t slen = sizeof(stored);
  if (settings_blob_get("fw_seq", &stored, &slen) == ESP_OK && slen == sizeof(stored) &&
      stored > s_floor) {
    s_floor = stored;
  }
  fw_manifest_t m;
  size_t len = sizeof(m);
  if (settings_blob_get("fw", &m, &len) == ESP_OK && len == sizeof(m) && manifest_ok(&m) &&
      m.seq >= s_floor) {
    taskENTER_CRITICAL(&s_mux);
    s_cur = m;
    taskEXIT_CRITICAL(&s_mux);
  }
}

void fw_manifest_tick(bool online) {
  int64_t now = esp_timer_get_time();
  if (online && !s_was_online) {
    // The station just joined: wait out the boot burst, but never pull a scheduled
    // hourly check or a failure back-off closer.
    int64_t earliest = now + FIRST_DELAY_US;
    if (s_next_us < earliest) {
      s_next_us = earliest;
    }
  }
  s_was_online = online;
  if (s_check_now && online && !s_inflight) {
    s_check_now = false;
    s_next_us = now;
  }
  if (!online || s_inflight || now < s_next_us) {
    return;
  }
  s_next_us = now + s_retry_us;  // moved to an hour on success
  s_inflight = core_net_submit(NET_OWNER_FW, FW_MANIFEST_URL, BODY_MAX, parse);
  if (!s_inflight) {
    ESP_LOGW(TAG, "manifest check not queued, the network worker is full");
  }
}

void fw_manifest_net_done(const core_net_result_t *r) {
  s_inflight = false;
  const fw_manifest_t *m = r->parsed;
  s_last_failed = m == NULL;
  if (!m) {
    ESP_LOGW(TAG, "manifest fetch failed: HTTP %d, %s; next try in %lld min", r->status,
             esp_err_to_name(r->err), (long long) (s_retry_us / 60000000));
    s_retry_us = s_retry_us * 2 > RETRY_MAX_US ? RETRY_MAX_US : s_retry_us * 2;
    return;
  }
  s_retry_us = RETRY_MIN_US;
  s_next_us = esp_timer_get_time() + FETCH_EVERY_US;
  if (m->seq < s_floor || m->seq <= s_cur.seq) {
    return;  // the same manifest, or an older one replayed
  }
  // The floor first: if the blob write then fails, the older manifests stay refused.
  esp_err_t e = settings_blob_set("fw_seq", &m->seq, sizeof(m->seq));
  if (e == ESP_OK) {
    e = settings_blob_set("fw", m, sizeof(*m));
  }
  s_floor = m->seq;
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "storing the manifest: %s (kept in memory)", esp_err_to_name(e));
  }
  taskENTER_CRITICAL(&s_mux);
  s_cur = *m;
  taskEXIT_CRITICAL(&s_mux);
  ESP_LOGI(TAG, "latest firmware %s (seq %lu)", m->version, (unsigned long) m->seq);
}

bool fw_manifest_check_now(void) {
  if (!net_online()) {
    return false;
  }
  static int64_t asked_at;
  int64_t now = esp_timer_get_time();
  if (asked_at != 0 && now - asked_at < CHECK_NOW_MIN_US) {
    return true;  // the answer of under 30 s ago stands; nor can a page keep the worker busy
  }
  asked_at = now;
  s_check_now = true;
  return true;
}

bool fw_manifest_checking(void) {
  return s_check_now || s_inflight;
}

bool fw_manifest_check_failed(void) {
  return s_last_failed;
}

bool fw_manifest_offer_text(fw_offer_text_t *out) {
  taskENTER_CRITICAL(&s_mux);
  bool on = s_cur.seq != 0 && fw_version_cmp(s_cur.version, s_running) > 0;
  memcpy(out->version, s_cur.version, sizeof(out->version));
  memcpy(out->notes, s_cur.notes, sizeof(out->notes));
  taskEXIT_CRITICAL(&s_mux);
  return on;
}

bool fw_manifest_offer(fw_manifest_t *out) {
  taskENTER_CRITICAL(&s_mux);
  *out = s_cur;
  taskEXIT_CRITICAL(&s_mux);
  return out->seq != 0 && fw_version_cmp(out->version, s_running) > 0;
}
