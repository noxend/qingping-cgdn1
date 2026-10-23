#include "sensor_bus.h"

#include "esp_log.h"

static const char *TAG = "sensor_bus";

#define BUS_PORT 0
#define BUS_SDA 26
#define BUS_SCL 27

static i2c_master_bus_handle_t bus_get(void) {
  static i2c_master_bus_handle_t bus;
  if (bus != NULL) {
    return bus;
  }
  i2c_master_bus_config_t bc = {
      .i2c_port = BUS_PORT,
      .sda_io_num = BUS_SDA,
      .scl_io_num = BUS_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  esp_err_t e = i2c_new_master_bus(&bc, &bus);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "bus init failed: %s", esp_err_to_name(e));
    bus = NULL;
  }
  return bus;
}

void sensor_bus_reset(void) {
  i2c_master_bus_handle_t bus = bus_get();
  if (bus != NULL) {
    esp_err_t e = i2c_master_bus_reset(bus);
    ESP_LOGW(TAG, "bus reset: %s", esp_err_to_name(e));
  }
}

i2c_master_dev_handle_t sensor_bus_add(uint8_t addr) {
  i2c_master_bus_handle_t bus = bus_get();
  if (bus == NULL) {
    return NULL;
  }
  i2c_device_config_t dc = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr,
      .scl_speed_hz = 100000,
  };
  i2c_master_dev_handle_t dev = NULL;
  esp_err_t e = i2c_master_bus_add_device(bus, &dc, &dev);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "add device 0x%02x failed: %s", addr, esp_err_to_name(e));
    return NULL;
  }
  return dev;
}
