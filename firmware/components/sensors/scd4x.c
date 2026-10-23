#include "scd4x.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sensirion.h"
#include "sensor_bus.h"

static const char *TAG = "scd4x";

#define SCD4X_ADDR 0x62
#define CMD_START_PERIODIC 0x21B1
#define CMD_READ_MEASUREMENT 0xEC05
#define CMD_STOP_PERIODIC 0x3F86
#define CMD_DATA_READY 0xE4B8
#define DATA_READY_MASK 0x07FF  // any of these bits set means a value is waiting
// The sensor needs 1 ms to answer a command. At 100 Hz two ticks are at least 10 ms; one
// tick can end right away, and pdMS_TO_TICKS(2) is zero ticks.
#define CMD_GAP_TICKS 2

static i2c_master_dev_handle_t s_dev;

static bool write_cmd(uint16_t cmd) {
  uint8_t b[2] = {(uint8_t) (cmd >> 8), (uint8_t) cmd};
  return i2c_master_transmit(s_dev, b, sizeof(b), 100) == ESP_OK;
}

bool scd4x_init(void) {
  if (s_dev == NULL) {
    s_dev = sensor_bus_add(SCD4X_ADDR);
  }
  if (s_dev == NULL) {
    return false;
  }
  // Stop first: a reset of the ESP32 alone leaves the sensor measuring, and the
  // start below should begin from a known state.
  write_cmd(CMD_STOP_PERIODIC);
  vTaskDelay(pdMS_TO_TICKS(500));
  if (!write_cmd(CMD_START_PERIODIC)) {
    ESP_LOGE(TAG, "start periodic measurement failed");
    return false;
  }
  return true;
}

// Reads `words` data words (each 2 bytes + CRC) into out; false on a bus or CRC error.
static bool read_words(uint16_t *out, int words) {
  uint8_t rx[9];
  int len = words * 3;
  if (i2c_master_receive(s_dev, rx, len, 100) != ESP_OK) {
    return false;
  }
  for (int i = 0; i < words; i++) {
    if (sensirion_crc8(rx + i * 3, 2) != rx[i * 3 + 2]) {
      return false;
    }
    out[i] = ((uint16_t) rx[i * 3] << 8) | rx[i * 3 + 1];
  }
  return true;
}

scd4x_result_t scd4x_read(uint16_t *co2_ppm) {
  if (s_dev == NULL) {
    return SCD4X_ERROR;
  }
  uint16_t status;
  if (!write_cmd(CMD_DATA_READY)) {
    return SCD4X_ERROR;
  }
  vTaskDelay(CMD_GAP_TICKS);
  if (!read_words(&status, 1)) {
    return SCD4X_ERROR;
  }
  if ((status & DATA_READY_MASK) == 0) {
    return SCD4X_NOT_READY;
  }
  uint16_t w[3];  // CO2, temperature, humidity
  if (!write_cmd(CMD_READ_MEASUREMENT)) {
    return SCD4X_ERROR;
  }
  vTaskDelay(CMD_GAP_TICKS);
  if (!read_words(w, 3)) {
    return SCD4X_ERROR;
  }
  *co2_ppm = w[0];
  return SCD4X_OK;
}
