#include "fw_update.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "core_net.h"
#include "fw_manifest.h"
#include "settings.h"

#define TIMEOUT_MS 10000
// A whole download, however slowly the server drips it; 1.5 MB takes about a minute.
#define DEADLINE_US (5LL * 60 * 1000 * 1000)
#define HASH_CHUNK 4096
#define AUTO_HOUR 4

static const char *TAG = "fw";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static fw_update_status_t s_status;
static fw_manifest_t s_queued;  // what fw_update_start offered, installed as it was
static bool s_upload;           // POST /ota holds the spare slot
static bool s_auto;
static char s_rolled_back[32];  // the newer version this device rolled back from, or ""

static void set_state(fw_update_state_t state, uint32_t done) {
  taskENTER_CRITICAL(&s_mux);
  s_status.state = state;
  s_status.done = done;
  taskEXIT_CRITICAL(&s_mux);
}

static void fail(const char *why) {
  ESP_LOGE(TAG, "update failed: %s", why);
  taskENTER_CRITICAL(&s_mux);
  s_status.state = FW_UPD_FAILED;
  strlcpy(s_status.err, why, sizeof(s_status.err));
  taskEXIT_CRITICAL(&s_mux);
}

// The image's own description, read from its first bytes before anything else is
// downloaded: the same project, and the version the manifest promised.
static const char *check_desc(esp_https_ota_handle_t h, const fw_manifest_t *m) {
  esp_app_desc_t desc;
  const esp_app_desc_t *self = esp_app_get_description();
  if (esp_https_ota_get_img_desc(h, &desc) != ESP_OK) {
    return "no image description";
  }
  if (strncmp(desc.project_name, self->project_name, sizeof(desc.project_name)) != 0) {
    return "not a cgdn1 firmware image";
  }
  if (strncmp(desc.version, m->version, sizeof(desc.version)) != 0) {
    return "image version differs from the manifest";
  }
  return NULL;
}

static const char *download(esp_https_ota_handle_t h, const fw_manifest_t *m) {
  int64_t start_us = esp_timer_get_time();
  while (true) {
    esp_task_wdt_reset();
    esp_err_t err = esp_https_ota_perform(h);
    int done = esp_https_ota_get_image_len_read(h);
    set_state(FW_UPD_DOWNLOADING, done > 0 ? (uint32_t) done : 0);
    if (err == ESP_OK) {
      break;
    }
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      return esp_err_to_name(err);
    }
    if (done > (int) m->size) {
      return "image larger than the manifest says";
    }
    if (esp_timer_get_time() - start_us > DEADLINE_US) {
      return "download took too long";
    }
  }
  bool whole = esp_https_ota_is_complete_data_received(h);
  if (!whole || esp_https_ota_get_image_len_read(h) != (int) m->size) {
    return "image size differs from the manifest";
  }
  return NULL;
}

// Hashes what was written to the slot, so the check covers the bytes that will boot.
static const char *check_hash(const fw_manifest_t *m) {
  const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
  uint8_t *buf = malloc(HASH_CHUNK);
  if (!slot || !buf) {
    free(buf);
    return "no memory for the hash";
  }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  esp_err_t err = ESP_OK;
  for (uint32_t off = 0; off < m->size && err == ESP_OK; off += HASH_CHUNK) {
    esp_task_wdt_reset();
    uint32_t n = m->size - off < HASH_CHUNK ? m->size - off : HASH_CHUNK;
    err = esp_partition_read(slot, off, buf, n);
    if (err == ESP_OK) {
      mbedtls_sha256_update(&ctx, buf, n);
    }
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  free(buf);
  if (err != ESP_OK) {
    return esp_err_to_name(err);
  }
  if (memcmp(digest, m->sha256, sizeof(digest)) != 0) {
    return "image sha256 differs from the manifest";
  }
  return NULL;
}

// The job on the network worker.
static void install(void) {
  fw_manifest_t m;
  taskENTER_CRITICAL(&s_mux);
  m = s_queued;
  taskEXIT_CRITICAL(&s_mux);
  esp_http_client_config_t http = {
      .url = m.url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = TIMEOUT_MS,
      .keep_alive_enable = true,
  };
  esp_https_ota_config_t cfg = {.http_config = &http};
  esp_https_ota_handle_t h = NULL;
  ESP_LOGI(TAG, "installing %s from %s", m.version, m.url);
  set_state(FW_UPD_DOWNLOADING, 0);
  esp_err_t err = esp_https_ota_begin(&cfg, &h);
  if (err != ESP_OK) {
    fail(esp_err_to_name(err));
    return;
  }
  const char *why = check_desc(h, &m);
  if (!why) {
    why = download(h, &m);
  }
  if (!why) {
    set_state(FW_UPD_VERIFYING, m.size);
    why = check_hash(&m);
  }
  if (why) {
    esp_https_ota_abort(h);
    fail(why);
    return;
  }
  // esp_ota_end inside checks the image signature, then the boot slot is switched.
  err = esp_https_ota_finish(h);
  if (err != ESP_OK) {
    fail(esp_err_to_name(err));
    return;
  }
  ESP_LOGW(TAG, "update %s written, rebooting", m.version);
  set_state(FW_UPD_REBOOTING, m.size);
  vTaskDelay(pdMS_TO_TICKS(1000));  // lets a /status poll see "rebooting"
  esp_restart();
}

// A rollback happens only at boot, so the slot it left invalid is read once.
static void find_rolled_back(void) {
  const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
  esp_app_desc_t desc;
  if (!bad || esp_ota_get_partition_description(bad, &desc) != ESP_OK) {
    return;
  }
  desc.version[sizeof(desc.version) - 1] = '\0';
  bool newer = fw_version_cmp(desc.version, esp_app_get_description()->version) > 0;
  size_t len = strlen(desc.version);
  bool plain = len > 0 && strspn(desc.version, "0123456789abcdefghijklmnopqrstuvwxyz.-") == len;
  if (newer && plain) {  // not an older image left behind, nor a name /status could not quote
    strlcpy(s_rolled_back, desc.version, sizeof(s_rolled_back));
  }
}

void fw_update_init(void) {
  find_rolled_back();
  uint8_t on = 0;
  size_t len = sizeof(on);
  if (settings_blob_get("fw_auto", &on, &len) == ESP_OK && len == sizeof(on)) {
    s_auto = on != 0;
  }
}

esp_err_t fw_update_start(void) {
  fw_manifest_t m;
  if (!fw_manifest_offer(&m)) {
    return ESP_ERR_NOT_FOUND;
  }
  taskENTER_CRITICAL(&s_mux);
  fw_update_status_t before = s_status;
  fw_update_state_t was = s_status.state;
  bool busy = s_upload || (was != FW_UPD_IDLE && was != FW_UPD_FAILED);
  if (!busy) {
    s_status = (fw_update_status_t){.state = FW_UPD_QUEUED, .total = m.size};
    strlcpy(s_status.version, m.version, sizeof(s_status.version));
    s_queued = m;
  }
  taskEXIT_CRITICAL(&s_mux);
  if (busy) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!core_net_run(install)) {
    taskENTER_CRITICAL(&s_mux);
    s_status = before;
    taskEXIT_CRITICAL(&s_mux);
    return ESP_FAIL;
  }
  return ESP_OK;
}

bool fw_update_hold_slot(void) {
  taskENTER_CRITICAL(&s_mux);
  fw_update_state_t st = s_status.state;
  bool free_slot = !s_upload && (st == FW_UPD_IDLE || st == FW_UPD_FAILED);
  s_upload = s_upload || free_slot;
  taskEXIT_CRITICAL(&s_mux);
  return free_slot;
}

void fw_update_release_slot(void) {
  taskENTER_CRITICAL(&s_mux);
  s_upload = false;
  taskEXIT_CRITICAL(&s_mux);
}

void fw_update_status(fw_update_status_t *out) {
  taskENTER_CRITICAL(&s_mux);
  *out = s_status;
  taskEXIT_CRITICAL(&s_mux);
}

const char *fw_update_state_name(fw_update_state_t state) {
  static const char *const NAMES[] = {"idle",      "queued",    "downloading",
                                      "verifying", "rebooting", "failed"};
  return state <= FW_UPD_FAILED ? NAMES[state] : "?";
}

bool fw_update_auto(void) {
  return s_auto;
}

esp_err_t fw_update_set_auto(bool on) {
  uint8_t v = on;
  esp_err_t e = settings_blob_set("fw_auto", &v, sizeof(v));
  if (e == ESP_OK) {
    s_auto = on;
  }
  return e;
}

// True when this version was installed here before and the rollback gate refused it:
// the night would otherwise install it again, and again after every rollback.
static bool already_rolled_back(const char *version) {
  const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
  esp_app_desc_t desc;
  return bad && esp_ota_get_partition_description(bad, &desc) == ESP_OK &&
         strncmp(desc.version, version, sizeof(desc.version)) == 0;
}

bool fw_update_rolled_back(char *out, size_t cap) {
  if (!s_rolled_back[0]) {
    return false;
  }
  strlcpy(out, s_rolled_back, cap);
  return true;
}

void fw_update_tick(void) {
  static int tried_yday = -1;
  if (!s_auto) {
    return;
  }
  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  bool clock_set = local.tm_year + 1900 >= 2025;
  if (!clock_set || local.tm_hour != AUTO_HOUR || local.tm_yday == tried_yday) {
    return;
  }
  tried_yday = local.tm_yday;
  fw_manifest_t m;
  if (!fw_manifest_offer(&m)) {
    return;
  }
  if (already_rolled_back(m.version)) {
    ESP_LOGW(TAG, "auto update: %s rolled back here before, skipped", m.version);
    return;
  }
  ESP_LOGI(TAG, "auto update to %s", m.version);
  esp_err_t e = fw_update_start();
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "auto update: %s", esp_err_to_name(e));
  }
}
