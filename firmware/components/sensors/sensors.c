#include "sensors.h"

#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "pm_sensor.h"
#include "scd4x.h"
#include "sensor_bus.h"
#include "settings.h"
#include "sht4x.h"
#include "temp_offset.h"

static const char *TAG = "sensors";

#define TICK_MS 1000  // the SCD40 is polled for a new value every tick, the PM sensor read
#define SHT_EVERY_TICKS 2
#define INIT_RETRY_TICKS 10
#define CO2_QUIET_TICKS 20  // no CO2 value for this long: restart the SCD40
#define BUS_RESET_TICKS 30  // nothing on the bus answered for this long: reset it
#define FIRST_READ_WAIT_MS 500
// Outdoor air: a reading under it is a sensor that drifted low. The stock shows it as 400.
#define CO2_FLOOR_PPM 400
#define PM_FRAMES_BEFORE_VALID 3  // the stock counts its first two frames as warm-up
#define PM_MISSES_TO_WARN 5       // a missed frame now and then, or the start, is not news
#define TEMP_OFFSET_KEY "temp_offset"

// Older than this and a reading is reported invalid: a few sampling periods.
static const uint32_t MAX_AGE_MS[SENSOR_COUNT] = {
    [SENSOR_TEMP] = 15000,  // the SHT4x is read every 2 s
    [SENSOR_HUM] = 15000,
    [SENSOR_CO2] = 30000,   // the SCD40 delivers a value every 5 s (periodic mode)
    [SENSOR_PM25] = 10000,  // the PM sensor is read every second
    [SENSOR_PM10] = 10000,
};

typedef struct {
  float value;
  int64_t at_us;
  bool have;
} slot_t;

static slot_t s_slots[SENSOR_COUNT];
static SemaphoreHandle_t s_first_read;  // given once the sampler has tried the SHT4x
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static atomic_int s_temp_offset;  // tenths of a degree
static atomic_bool s_pm_off;      // set by sensors_power_off

static void store(sensor_id_t id, float value) {
  int64_t now = esp_timer_get_time();
  taskENTER_CRITICAL(&s_mux);
  s_slots[id].value = value;
  s_slots[id].at_us = now;
  s_slots[id].have = true;
  taskEXIT_CRITICAL(&s_mux);
}

bool sensor_get(sensor_id_t id, sensor_reading_t *out) {
  *out = (sensor_reading_t){0};
  if ((unsigned) id >= SENSOR_COUNT) {
    return false;
  }
  slot_t slot;
  slot_t temp;  // the SHT4x reads both, so this is the temperature the humidity was measured at
  taskENTER_CRITICAL(&s_mux);
  slot = s_slots[id];
  temp = s_slots[SENSOR_TEMP];
  taskEXIT_CRITICAL(&s_mux);
  int64_t age_ms = slot.have ? (esp_timer_get_time() - slot.at_us) / 1000 : 0;
  if (age_ms > UINT32_MAX) {
    age_ms = UINT32_MAX;  // a long-dead sensor must not wrap back to "fresh"
  }
  int16_t offset = (int16_t) atomic_load(&s_temp_offset);
  out->value = slot.value;
  if (id == SENSOR_TEMP && slot.have) {
    out->value = temp_offset_apply(slot.value, offset);
  }
  if (id == SENSOR_HUM && slot.have && temp.have) {
    out->value = temp_offset_humidity(slot.value, temp.value, offset);
  }
  out->age_ms = (uint32_t) age_ms;
  out->valid = slot.have && out->age_ms <= MAX_AGE_MS[id];
  return out->valid;
}

int16_t sensors_temp_offset(void) {
  return (int16_t) atomic_load(&s_temp_offset);
}

bool sensors_set_temp_offset(int16_t tenths) {
  if (!temp_offset_ok(tenths)) {
    return false;
  }
  esp_err_t e = settings_blob_set(TEMP_OFFSET_KEY, &tenths, sizeof(tenths));
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "temperature offset store: %s", esp_err_to_name(e));
    return false;
  }
  atomic_store(&s_temp_offset, tenths);
  return true;
}

// The stored offset, or 0 when there is none or it is out of range.
static void load_temp_offset(void) {
  int16_t tenths = 0;
  size_t len = sizeof(tenths);
  esp_err_t e = settings_blob_get(TEMP_OFFSET_KEY, &tenths, &len);
  bool usable = e == ESP_OK && len == sizeof(tenths) && temp_offset_ok(tenths);
  atomic_store(&s_temp_offset, usable ? tenths : 0);
}

static bool read_sht(void) {
  float rh = 0.0f;
  float t = 0.0f;
  if (!sht4x_read(&rh, &t)) {
    return false;
  }
  store(SENSOR_HUM, rh);
  store(SENSOR_TEMP, t);
  return true;
}

// One frame from the PM sensor; the values are kept from its third good frame on.
static bool read_pm(int *frames) {
  uint16_t pm25 = 0;
  uint16_t pm10 = 0;
  if (!pm_sensor_read(&pm25, &pm10)) {
    return false;
  }
  if (*frames < PM_FRAMES_BEFORE_VALID) {
    ++*frames;
  }
  if (*frames == PM_FRAMES_BEFORE_VALID) {
    store(SENSOR_PM25, pm25);
    store(SENSOR_PM10, pm10);
  }
  return true;
}

// Logs only when a sensor changes between working and failing, so a dead one
// shows up once instead of every second.
static void note_state(const char *name, bool *was_ok, bool ok) {
  if (ok != *was_ok) {
    ESP_LOGW(TAG, "%s %s", name, ok ? "is answering" : "is not answering");
    *was_ok = ok;
  }
}

static void sampler(void *arg) {
  bool sht_ready = false;
  bool scd_ready = false;
  bool pm_ready = false;
  bool sht_ok = true;
  bool scd_ok = true;
  bool pm_ok = true;
  int pm_frames = 0;
  int pm_misses = 0;  // frames missed in a row
  int tick = 0;
  int quiet_ticks = 0;   // ticks since the SCD40 last delivered a value
  int silent_ticks = 0;  // ticks since any sensor answered
  esp_task_wdt_add(NULL);
  while (true) {
    // A sensor that failed to start is tried again every few seconds.
    if (tick % INIT_RETRY_TICKS == 0) {
      if (!sht_ready) {
        sht_ready = sht4x_init();
        if (sht_ready) {
          read_sht();  // a value right away, so a screen shown at boot has one
        }
        note_state("SHT4x", &sht_ok, sht_ready);
        xSemaphoreGive(s_first_read);  // only the first give is waited for
      }
      if (!scd_ready) {
        scd_ready = scd4x_init();  // blocks about 0.5 s
        note_state("SCD40", &scd_ok, scd_ready);
      }
      if (!pm_ready) {
        pm_ready = pm_sensor_init();
        note_state("PM sensor", &pm_ok, pm_ready);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    esp_task_wdt_reset();
    tick++;
    bool any_ok = false;
    if (sht_ready && tick % SHT_EVERY_TICKS == 0) {
      bool ok = read_sht();
      any_ok |= ok;
      note_state("SHT4x", &sht_ok, ok);
    }
    uint16_t ppm = 0;
    scd4x_result_t r = scd_ready ? scd4x_read(&ppm) : SCD4X_NOT_READY;
    if (r == SCD4X_OK) {
      store(SENSOR_CO2, (float) (ppm < CO2_FLOOR_PPM ? CO2_FLOOR_PPM : ppm));
      quiet_ticks = 0;
    } else if (scd_ready && ++quiet_ticks >= CO2_QUIET_TICKS) {
      scd_ready = false;  // it may have reset on its own: stop, wait, start again
      quiet_ticks = 0;
    }
    if (r != SCD4X_NOT_READY) {
      any_ok |= r == SCD4X_OK;
      note_state("SCD40", &scd_ok, r == SCD4X_OK);
    }
    if (pm_ready && !atomic_load(&s_pm_off)) {
      bool ok = read_pm(&pm_frames);
      any_ok |= ok;
      pm_misses = ok ? 0 : pm_misses + 1;
      if (ok || pm_misses >= PM_MISSES_TO_WARN) {
        note_state("PM sensor", &pm_ok, ok);
      }
    }
    // All sensors silent for a while: a slave holding SDA low looks exactly like
    // this, and only a bus reset (SCL clocked out) frees it.
    silent_ticks = any_ok ? 0 : silent_ticks + 1;
    if (silent_ticks >= BUS_RESET_TICKS) {
      silent_ticks = 0;
      ESP_LOGW(TAG, "no sensor answered for %d s, resetting the bus", BUS_RESET_TICKS);
      sensor_bus_reset();
    }
  }
}

void sensors_power_off(void) {
  atomic_store(&s_pm_off, true);
  pm_sensor_power_off();
}

void sensors_start(void) {
  load_temp_offset();
  s_first_read = xSemaphoreCreateBinary();
  if (xTaskCreate(sampler, "sensors", 4096, NULL, 2, NULL) != pdPASS) {
    ESP_LOGE(TAG, "sampler task create failed");
    return;
  }
  // A screen restored at boot draws at once; give the first value a moment.
  xSemaphoreTake(s_first_read, pdMS_TO_TICKS(FIRST_READ_WAIT_MS));
}
