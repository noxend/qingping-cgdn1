// The I2C bus the environment sensors share: port 0, SDA 26 / SCL 27, which
// carries the SHT4x (0x44) and the SCD40 (0x62), per the scan in docs/hardware.md.
#pragma once

#include "driver/i2c_master.h"

// Adds the 7-bit address to the port-0 bus (created on the first call) at 100 kHz
// and returns its handle, or NULL if the bus or the device cannot be set up. Call
// it from one task, the sensor sampler.
i2c_master_dev_handle_t sensor_bus_add(uint8_t addr);

// Clocks the bus out of a stuck state (a slave holding SDA low); device handles
// stay valid. Same task as sensor_bus_add.
void sensor_bus_reset(void);
