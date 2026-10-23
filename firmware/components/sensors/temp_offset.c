#include "temp_offset.h"

#include <math.h>

// A value the page computed as 0.1 * n may arrive as 0.30000000000000004.
#define STEP_SLACK 1e-6

bool temp_offset_ok(int tenths) {
  return tenths >= -TEMP_OFFSET_MAX_TENTHS && tenths <= TEMP_OFFSET_MAX_TENTHS;
}

bool temp_offset_from_celsius(double celsius, int16_t *tenths) {
  if (!isfinite(celsius)) {
    return false;
  }
  double scaled = celsius * 10.0;
  double whole = round(scaled);
  if (fabs(whole) > TEMP_OFFSET_MAX_TENTHS || fabs(scaled - whole) > STEP_SLACK) {
    return false;
  }
  *tenths = (int16_t) whole;
  return true;
}

float temp_offset_apply(float measured, int16_t tenths) {
  return measured + (float) tenths / 10.0f;
}
