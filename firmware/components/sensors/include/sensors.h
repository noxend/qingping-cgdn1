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
