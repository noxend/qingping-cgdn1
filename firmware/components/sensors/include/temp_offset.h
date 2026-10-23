// The temperature calibration: an offset the user sets, in tenths of a degree, added
// to what the SHT4x measures. Pure C with no ESP-IDF include, so the host test can
// compile it; sensors.c stores the offset and applies it, the /config handler reads
// it from the API.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// -3.0 to +3.0 °C in steps of 0.1 °C.
#define TEMP_OFFSET_MAX_TENTHS 30

bool temp_offset_ok(int tenths);

// An offset given in °C, as the API sends it, to tenths; false outside the range or
// off the 0.1 °C step.
bool temp_offset_from_celsius(double celsius, int16_t *tenths);

float temp_offset_apply(float measured, int16_t tenths);
