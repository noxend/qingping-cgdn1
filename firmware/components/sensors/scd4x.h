// Sensirion SCD40 CO2 sensor at 0x62 on the sensor bus. Commands, timings and
// the data-ready rule are from Sensirion's reference driver (embedded-i2c-scd4x):
// periodic measurement, a new value every few seconds, read once per value.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  SCD4X_OK,         // a fresh, CRC-checked value was read
  SCD4X_NOT_READY,  // the sensor has no new value yet
  SCD4X_ERROR,      // the bus or a CRC failed
} scd4x_result_t;

// Adds the device and (re)starts periodic measurement: it stops a measurement an
// earlier boot may have left running, waits 500 ms, then starts. Blocks about
// 0.5 s. False if the bus, the device or the start command failed; calling it
// again repeats the stop and start, so a failed init can be retried.
bool scd4x_init(void);

// Reads the CO2 concentration in ppm when a new value is ready.
scd4x_result_t scd4x_read(uint16_t *co2_ppm);
