#include "pm_sensor.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "pm_frame.h"
#include "sensor_bus.h"

static const char *TAG = "pm_sensor";

#define PM_ADDR 0x12
#define PIN_POWER 13    // high: the sensor's supply on
#define PIN_FAN_OFF 18  // high: fan stopped

static i2c_master_dev_handle_t s_dev;

static void power_on(void) {
  gpio_config_t io = {
      .pin_bit_mask = (1ULL << PIN_POWER) | (1ULL << PIN_FAN_OFF),
      .mode = GPIO_MODE_OUTPUT,
  };
  esp_err_t e = gpio_config(&io);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "power pins: %s", esp_err_to_name(e));
  }
  gpio_set_level(PIN_FAN_OFF, 0);
  gpio_set_level(PIN_POWER, 1);
}

void pm_sensor_power_off(void) {
  gpio_set_level(PIN_POWER, 0);
}

bool pm_sensor_init(void) {
  if (s_dev == NULL) {
    power_on();
    s_dev = sensor_bus_add(PM_ADDR);
  }
  return s_dev != NULL;
}

bool pm_sensor_read(uint16_t *pm25, uint16_t *pm10) {
  static bool logged;
  static bool refusal_logged;
  uint8_t frame[PM_FRAME_LEN];
  pm_frame_t f;
  if (s_dev == NULL || i2c_master_receive(s_dev, frame, sizeof(frame), 100) != ESP_OK) {
    return false;
  }
  if (!logged) {
    ESP_LOGI(TAG, "first frame:");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    logged = true;
  }
  if (!pm_frame_parse(frame, &f)) {
    if (!refusal_logged) {
      ESP_LOGW(TAG, "a frame failed its header or checksum check");
      ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
      refusal_logged = true;
    }
    return false;
  }
  pm_frame_shown(&f, pm25, pm10);
  return true;
}
