// The SHT4x reading in large type, in Celsius or Fahrenheit.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_READ 1
#define READ_MS 2000

static bool s_fahrenheit;
static char s_shown[16];  // the value last drawn, for change detection

WASM_EXPORT("on_config") void temp_on_config(void) {
  char unit[8];
  plugin_setting_get("unit", unit, sizeof(unit), "c");
  s_fahrenheit = strcmp(unit, "f") == 0;
}

// The value with one decimal, as printf's "%.1f" writes it: the nearest tenth, a tie to
// the even one. f * 10 is exact in a double (a float has 24 bits), so the tie test is too.
static void format_tenths(char *out, int cap, float f) {
  bool negative = __builtin_signbit(f);
  double x = (double) f * 10.0;
  double a = negative ? -x : x;
  int n = (int) a;
  double frac = a - (double) n;
  if (frac > 0.5 || (frac == 0.5 && (n & 1))) {
    n++;
  }
  snprintf(out, cap, "%s%d.%d", negative ? "-" : "", n / 10, n % 10);
}

// The value as the panel shows it: one decimal, or nothing while the sensor has nothing
// valid.
static void format_value(char *out, int cap) {
  wasm_sensor_t reading;
  if (!plugin_sensor_get(SENSOR_TEMP, &reading, sizeof(reading))) {
    out[0] = '\0';
    return;
  }
  float value = reading.value;
  if (s_fahrenheit) {
    value = value * 9.0f / 5.0f + 32.0f;
  }
  format_tenths(out, cap, value);
}

WASM_EXPORT("on_event") void temp_on_event(int type, int timer_id) {
  if (type == CORE_EV_SHOW) {
    core_timer_start(TIMER_READ, READ_MS, true);
  } else if (type == CORE_EV_HIDE) {
    core_timer_stop(TIMER_READ);
  } else if (type == CORE_EV_TIMER) {
    char value[16];
    format_value(value, sizeof(value));
    if (strcmp(value, s_shown) != 0) {
      strcpy(s_shown, value);
      core_redraw();
    }
  }
}

WASM_EXPORT("draw") void temp_draw(void) {
  char value[16];
  format_value(value, sizeof(value));
  gfx_clear();
  ui_value(value);
  ui_label(s_fahrenheit ? "°F" : "°C");
}
