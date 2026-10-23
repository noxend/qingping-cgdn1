// SHT4x temperature and humidity sensor at 0x44 on the sensor bus (sensor_bus.h),
// confirmed by the on-device scan in docs/hardware.md. Protocol
// from the Sensirion SHT4x datasheet: command 0xFD (high precision) then a
// 6-byte read of temperature and humidity, each with a CRC8.
#pragma once

#include <stdbool.h>

// Adds the device to the sensor bus. Idempotent; returns false if the bus or
// device cannot be set up, after which sht4x_read fails. Note: while the sensor
// bus holds port 0, the /i2c diagnostic cannot re-create port 0.
bool sht4x_init(void);

// One high-precision measurement. Writes rh (%, clamped 0..100) and temp_c when
// the pointers are non-NULL, returns true only when both CRCs are valid. Blocks
// about 20 ms for the conversion.
bool sht4x_read(float *rh, float *temp_c);
