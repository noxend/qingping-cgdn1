// The SCD40 reading in large type, with the LED as a traffic light.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_READ 1
#define READ_MS 5000  // the SCD40 produces a value every five seconds

// Indoor guidance: fresh air is about 400 ppm, above 1000 to 1200 ppm people report
// stuffiness. The two thresholds are settings (plugin.json has these defaults).
#define WARN_DEFAULT 800
#define ALARM_DEFAULT 1200

static int s_warn_ppm = WARN_DEFAULT;
static int s_alarm_ppm = ALARM_DEFAULT;
static char s_shown[16];  // the value last drawn, for change detection
static int s_ppm;         // last valid reading, for the LED
static bool s_have;       // a reading has succeeded

// A setting the device checked as an integer of up to nine digits, or def when unset.
static int setting_int(const char *key, int def) {
  char text[16];
  plugin_setting_get(key, text, sizeof(text), "");
  if (text[0] == '\0') {
    return def;
  }
  bool negative = text[0] == '-';
  int n = 0;
  for (const char *p = text + negative; *p >= '0' && *p <= '9'; p++) {
    n = n * 10 + (*p - '0');
  }
  return negative ? -n : n;
}

// The red threshold must sit above the amber one; a pair saved the other way round
// keeps the amber value and moves red just above it.
WASM_EXPORT("on_config") void co2_on_config(void) {
  int warn = setting_int("warn_ppm", WARN_DEFAULT);
  if (warn < 1) {
    warn = WARN_DEFAULT;
  }
  int alarm = setting_int("alarm_ppm", ALARM_DEFAULT);
  if (alarm <= warn) {
    alarm = warn + 1;
  }
  s_warn_ppm = warn;
  s_alarm_ppm = alarm;
}

// Reads the sensor into the text the panel shows (whole ppm, or nothing while the sensor
// has nothing valid) and into s_have and s_ppm for the LED.
static void read_value(char *out, int cap) {
  wasm_sensor_t reading;
  if (!plugin_sensor_get(SENSOR_CO2, &reading, sizeof(reading))) {
    s_have = false;
    out[0] = '\0';
    return;
  }
  s_have = true;
  s_ppm = (int) (reading.value + 0.5f);
  snprintf(out, cap, "%d", s_ppm);
}

WASM_EXPORT("on_event") void co2_on_event(int type, int timer_id) {
  if (type == CORE_EV_SHOW) {
    core_timer_start(TIMER_READ, READ_MS, true);
  } else if (type == CORE_EV_HIDE) {
    core_timer_stop(TIMER_READ);
  } else if (type == CORE_EV_TIMER) {
    char value[16];
    read_value(value, sizeof(value));
    if (strcmp(value, s_shown) != 0) {
      strcpy(s_shown, value);
      core_redraw();
    }
  }
}

WASM_EXPORT("draw") void co2_draw(void) {
  char value[16];
  read_value(value, sizeof(value));
  gfx_clear();
  ui_value(value);
  ui_label("ppm");
}

// Green below the warning threshold, amber up to the alarm threshold, red above.
WASM_EXPORT("led") int co2_led(void) {
  if (!s_have) {
    return -1;
  }
  if (s_ppm < s_warn_ppm) {
    return 0x00FF00;
  }
  return s_ppm < s_alarm_ppm ? 0xFF9600 : 0xFF0000;
}
