#include "temp_offset.h"

#include <math.h>

// Magnus parameters above water, -45 to 60 °C (Sensirion, Physics of Humidity, table 1).
#define MAGNUS_BETA 17.62f
#define MAGNUS_LAMBDA 243.12f

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

// The exponent of the Magnus formula; the saturation vapour pressure is
// alpha * exp(magnus(t)), and alpha cancels in a ratio of two.
static float magnus(float t) {
  return MAGNUS_BETA * t / (MAGNUS_LAMBDA + t);
}

// Sensirion, Physics of Humidity, equation (10): relative humidity after a change of
// temperature at a constant amount of water vapour.
float temp_offset_humidity(float rh, float measured, int16_t tenths) {
  if (tenths == 0) {
    return rh;
  }
  float corrected = temp_offset_apply(measured, tenths);
  float out = rh * expf(magnus(measured) - magnus(corrected));
  return fminf(fmaxf(out, 0.0f), 100.0f);
}
