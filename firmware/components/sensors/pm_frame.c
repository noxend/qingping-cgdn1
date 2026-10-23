#include "pm_frame.h"

#define VERSION_AS_READ 0x80
#define CORRECT_ABOVE 61
#define SATURATED_ABOVE 592
#define SHOWN_MAX 999

static uint16_t word_at(const uint8_t *frame, int i) {
  return (uint16_t) (frame[i] << 8 | frame[i + 1]);
}

bool pm_frame_parse(const uint8_t frame[PM_FRAME_LEN], pm_frame_t *out) {
  if (frame[0] != 'B' || frame[1] != 'M' || word_at(frame, 2) != PM_FRAME_LEN - 4) {
    return false;
  }
  uint16_t sum = 0;
  for (int i = 0; i < PM_FRAME_LEN - 2; i++) {
    sum += frame[i];
  }
  if (sum != word_at(frame, PM_FRAME_LEN - 2)) {
    return false;
  }
  out->pm25 = word_at(frame, 12);
  out->pm10 = word_at(frame, 14);
  out->version = frame[28];
  return true;
}

// The stock's curve, in its order of operations so the truncation matches.
static uint16_t corrected(uint16_t raw) {
  double v = 1.0 / (0.874 / raw - 0.000475);
  if (v < 0 || v > SHOWN_MAX) {
    return SHOWN_MAX;  // the curve's pole sits near 1840; past it only PM10 can be
  }
  return (uint16_t) v;
}

static uint16_t capped(uint16_t v) {
  return v > SHOWN_MAX ? SHOWN_MAX : v;
}

void pm_frame_shown(const pm_frame_t *f, uint16_t *pm25, uint16_t *pm10) {
  uint16_t p = f->pm25;
  uint16_t q = f->pm10;
  if (f->version == VERSION_AS_READ) {
    *pm25 = capped(p);
    *pm10 = capped(q);
  } else if (p == 0 || q == 0) {
    *pm25 = 0;
    *pm10 = 0;
  } else if (p > SATURATED_ABOVE) {
    *pm25 = SHOWN_MAX;
    *pm10 = SHOWN_MAX;
  } else if (p > CORRECT_ABOVE) {
    *pm25 = corrected(p);
    *pm10 = corrected(q);
  } else {
    *pm25 = capped(p);
    *pm10 = capped(q);
  }
}
