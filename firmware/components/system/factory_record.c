#include "factory_record.h"

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"

// The partition over the record. A unit on the table before it has none and uses
// the copy, on the default NVS partition: the raw bytes, so the record could be
// written back as it was.
#define PARTITION "factory_rec"
#define COPY_NS "factory"
#define COPY_KEY "record"

static const char *TAG = "factory";
static factory_record_t s_record;
static bool s_found;

static bool read_flash(uint8_t *raw) {
  const esp_partition_t *part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION);
  if (!part) {
    return false;
  }
  return esp_partition_read(part, 0, raw, FACTORY_RECORD_BYTES) == ESP_OK;
}

static bool read_copy(uint8_t *raw) {
  nvs_handle_t h;
  if (nvs_open(COPY_NS, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  size_t len = FACTORY_RECORD_BYTES;
  esp_err_t e = nvs_get_blob(h, COPY_KEY, raw, &len);
  nvs_close(h);
  return e == ESP_OK && len == FACTORY_RECORD_BYTES;
}

// Written only when it differs from the copy already kept, so a boot with the
// record still in flash costs no flash write.
static void keep_copy(const uint8_t *raw) {
  uint8_t kept[FACTORY_RECORD_BYTES];
  if (read_copy(kept) && memcmp(kept, raw, FACTORY_RECORD_BYTES) == 0) {
    return;
  }
  nvs_handle_t h;
  esp_err_t e = nvs_open(COPY_NS, NVS_READWRITE, &h);
  if (e == ESP_OK) {
    e = nvs_set_blob(h, COPY_KEY, raw, FACTORY_RECORD_BYTES);
    if (e == ESP_OK) {
      e = nvs_commit(h);
    }
    nvs_close(h);
  }
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "record not kept: %s", esp_err_to_name(e));
  }
}

void factory_record_load(void) {
  uint8_t raw[FACTORY_RECORD_BYTES];
  if (read_flash(raw) && factory_record_parse(raw, &s_record)) {
    keep_copy(raw);
    s_found = true;
  } else {
    s_found = read_copy(raw) && factory_record_parse(raw, &s_record);
  }
  if (s_found) {
    ESP_LOGI(TAG, "serial %s, lcd_id %d", s_record.serial, s_record.lcd_id);
  } else {
    ESP_LOGI(TAG, "no factory record");
  }
}

bool factory_record_get(factory_record_t *out) {
  if (s_found) {
    *out = s_record;
  }
  return s_found;
}
