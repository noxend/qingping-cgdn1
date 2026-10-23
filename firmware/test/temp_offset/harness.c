// Checks the temperature calibration: which offsets the API accepts, and what a
// reading and the humidity measured with it become with one.
#include <math.h>
#include <stdio.h>
#include "temp_offset.h"

static int failures;

static void check(const char *name, bool ok) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) {
    failures++;
  }
}

static void accepted(double celsius, int want_tenths) {
  int16_t tenths = 99;
  bool ok = temp_offset_from_celsius(celsius, &tenths);
  char name[64];
  snprintf(name, sizeof(name), "%.17g °C is %d tenths", celsius, want_tenths);
  check(name, ok && tenths == want_tenths);
}

static void refused(double celsius) {
  int16_t tenths = 99;
  bool ok = temp_offset_from_celsius(celsius, &tenths);
  char name[64];
  snprintf(name, sizeof(name), "%.17g °C is refused", celsius);
  check(name, !ok && tenths == 99);
}

static void applied(float measured, int16_t tenths, float want) {
  float got = temp_offset_apply(measured, tenths);
  char name[64];
  snprintf(name, sizeof(name), "%.1f with %d tenths is %.1f (got %.4f)", measured, tenths, want,
           got);
  check(name, fabsf(got - want) < 1e-4f);
}

// The expected values come from Sensirion's "Physics of Humidity" application
// note, equation (10), evaluated in Python, not from temp_offset.c.
static void humidity(float rh, float measured, int16_t tenths, float want) {
  float got = temp_offset_humidity(rh, measured, tenths);
  char name[96];
  snprintf(name, sizeof(name), "%.1f %%RH at %.1f with %d tenths is %.2f (got %.4f)", rh, measured,
           tenths, want, got);
  check(name, fabsf(got - want) < 0.01f);
}

int main(void) {
  accepted(0.0, 0);
  accepted(-0.0, 0);
  accepted(3.0, 30);
  accepted(-3.0, -30);
  accepted(0.1, 1);
  accepted(-2.7, -27);
  accepted(0.1 * 3, 3);  // 0.30000000000000004, as a page computes it

  refused(3.1);
  refused(-3.1);
  refused(3.05);
  refused(0.05);
  refused(1e300);
  refused(-1e300);
  refused(NAN);
  refused(INFINITY);

  check("30 tenths is in range", temp_offset_ok(30));
  check("-30 tenths is in range", temp_offset_ok(-30));
  check("31 tenths is out of range", !temp_offset_ok(31));
  check("-31 tenths is out of range", !temp_offset_ok(-31));

  applied(24.3f, 0, 24.3f);
  applied(25.0f, -15, 23.5f);
  applied(20.0f, 30, 23.0f);
  applied(-5.2f, -30, -8.2f);
  applied(0.0f, 1, 0.1f);

  humidity(50.0f, 25.0f, 0, 50.0f);
  humidity(50.0f, 25.0f, -20, 56.3790f);
  humidity(50.0f, 25.0f, -30, 59.9082f);
  humidity(40.0f, 25.0f, 20, 35.5373f);
  humidity(60.0f, 20.0f, 15, 54.7106f);
  humidity(70.0f, -10.0f, -5, 72.8202f);
  humidity(30.0f, 30.0f, 30, 25.2996f);
  humidity(1.0f, 22.0f, 30, 0.8346f);
  humidity(90.0f, 22.0f, -30, 100.0f);  // 108.28 by the formula: air past saturation
  humidity(0.0f, 25.0f, -30, 0.0f);

  float there = temp_offset_humidity(45.0f, 23.0f, 25);
  float back = temp_offset_humidity(there, 25.5f, -25);
  check("+2.5 then -2.5 gives the measured humidity back", fabsf(back - 45.0f) < 0.01f);

  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
