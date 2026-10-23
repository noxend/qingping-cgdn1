// The Plantower particulate sensor at 0x12 on the sensor bus (sensor_bus.h), read the
// way the stock firmware reads it (journal entry pm25): GPIO13 high and GPIO18 low
// power it and run its fan, then a plain 32-byte read returns the latest frame. No
// command is ever sent. The frame and the stock's values: pm_frame.h.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Powers the sensor and adds it to the bus. Idempotent; false if the bus or the
// device cannot be set up, after which pm_sensor_read fails.
bool pm_sensor_init(void);

// Reads one frame and writes what the stock shows for it, 0 to 999 µg/m³. False on a
// bus error or a frame that does not check out.
bool pm_sensor_read(uint16_t *pm25, uint16_t *pm10);
