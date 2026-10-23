// Environment sensors behind one call. A sampler task owns the I2C bus and keeps
// the latest value of each sensor; any task reads it with sensor_get, which never
// touches the bus and never blocks. Particulate matter and the battery are not
// here yet: their pins are not recorded (docs/hardware.md), and the ids are added
// when a source exists.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// sensor_id_t and sensor_reading_t are the SDK's: plugins read the same values.
#include "plugin_sdk.h"

// Starts the sampler task. Call once, before the screens run.
void sensors_start(void);

// Copies the latest reading of `id` into out and returns out->valid. A reading
// older than a few sampling periods is reported invalid instead of stale.
bool sensor_get(sensor_id_t id, sensor_reading_t *out);

// The temperature calibration in tenths of a degree (temp_offset.h), kept in the core
// settings. sensor_get adds it to every temperature, so the panel, the plugins and
// /sensors all show the corrected value. 0 until set.
int16_t sensors_temp_offset(void);

// Stores the offset and applies it from the next sensor_get. False when it is out of
// range or could not be stored. Any task.
bool sensors_set_temp_offset(int16_t tenths);
