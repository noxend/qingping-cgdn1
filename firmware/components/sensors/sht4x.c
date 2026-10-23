#include "sht4x.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sensirion.h"
#include "sensor_bus.h"

#define SHT4X_ADDR 0x44
#define CMD_MEASURE_HIGH 0xFD

static i2c_master_dev_handle_t s_dev;

bool sht4x_init(void) {
  if (s_dev != NULL) {
    return true;
  }
  s_dev = sensor_bus_add(SHT4X_ADDR);
  return s_dev != NULL;
}

bool sht4x_read(float *rh, float *temp_c) {
  if (s_dev == NULL) {
    return false;
  }
  uint8_t cmd = CMD_MEASURE_HIGH;
  if (i2c_master_transmit(s_dev, &cmd, 1, 100) != ESP_OK) {
    return false;
  }
  // The high-precision conversion takes about 8.3 ms; the sensor NACKs a read
  // that comes too early. Two ticks guarantee at least 10 ms at any tick rate.
  vTaskDelay(pdMS_TO_TICKS(20));
  uint8_t rx[6];
  if (i2c_master_receive(s_dev, rx, sizeof(rx), 100) != ESP_OK) {
    return false;
  }
  if (sensirion_crc8(rx, 2) != rx[2] || sensirion_crc8(rx + 3, 2) != rx[5]) {
    return false;
  }
  uint16_t t_ticks = ((uint16_t) rx[0] << 8) | rx[1];
  uint16_t rh_ticks = ((uint16_t) rx[3] << 8) | rx[4];
  float h = -6.0f + 125.0f * (float) rh_ticks / 65535.0f;
  if (h < 0.0f) {
    h = 0.0f;
  } else if (h > 100.0f) {
    h = 100.0f;
  }
  if (rh != NULL) {
    *rh = h;
  }
  if (temp_c != NULL) {
    *temp_c = -45.0f + 175.0f * (float) t_ticks / 65535.0f;
  }
  return true;
}
