// The cases the standard plugins are checked against: what each draws and lights for a
// sensor reading, a setting, a time or a network answer. golden/ holds the frames in the
// standard screen layout (plugins/README.md), and the LED colours and alert stats the
// native plugins of 1.0.0-rc7 gave for the same cases, before they became WebAssembly
// modules.
#pragma once

#include <stdbool.h>

#define NO_SENSOR (-1)
// 2026-10-22 09:05:07 UTC, 21:45:00 UTC, and 2026-01-03 00:00:59 UTC.
#define MORNING 1792659907LL
#define EVENING 1792705500LL
#define NEW_YEAR 1767398459LL

typedef struct {
  const char *name;
  const char *plugin;
  int sensor;  // SENSOR_* or NO_SENSOR
  float value;
  bool valid;
  const char *key;  // the one setting changed, NULL for the defaults
  const char *setting;
  long long now;     // the clock, seconds since 1970 UTC
  const char *body;  // a network answer in data/, NULL for none
} plugin_case_t;

static const plugin_case_t CASES[] = {
    {"clock_default", "clock", NO_SENSOR, 0, false, NULL, NULL, MORNING, NULL},
    {"clock_12h", "clock", NO_SENSOR, 0, false, "time_format", "12h", MORNING, NULL},
    {"clock_12h_pm", "clock", NO_SENSOR, 0, false, "time_format", "12h", EVENING, NULL},
    {"clock_seconds", "clock", NO_SENSOR, 0, false, "seconds", "1", MORNING, NULL},
    {"clock_ymd", "clock", NO_SENSOR, 0, false, "date_format", "ymd", NEW_YEAR, NULL},
    {"clock_mdy", "clock", NO_SENSOR, 0, false, "date_format", "mdy", NEW_YEAR, NULL},
    {"clock_unset", "clock", NO_SENSOR, 0, false, NULL, NULL, 0, NULL},
    {"temp_23_4", "temperature", 0, 23.4f, true, NULL, NULL, MORNING, NULL},
    {"temp_minus_5", "temperature", 0, -5.0f, true, NULL, NULL, MORNING, NULL},
    {"temp_invalid", "temperature", 0, 0.0f, false, NULL, NULL, MORNING, NULL},
    {"temp_fahrenheit", "temperature", 0, 23.4f, true, "unit", "f", MORNING, NULL},
    {"temp_tie", "temperature", 0, 0.25f, true, NULL, NULL, MORNING, NULL},
    {"temp_23_45", "temperature", 0, 23.45f, true, NULL, NULL, MORNING, NULL},
    {"temp_minus_0", "temperature", 0, -0.04f, true, NULL, NULL, MORNING, NULL},
    {"temp_99_95", "temperature", 0, 99.95f, true, NULL, NULL, MORNING, NULL},
    {"temp_f_minus_40", "temperature", 0, -40.0f, true, "unit", "f", MORNING, NULL},
    {"co2_420", "co2", 2, 420.0f, true, NULL, NULL, MORNING, NULL},
    {"co2_950", "co2", 2, 950.0f, true, NULL, NULL, MORNING, NULL},
    {"co2_1234", "co2", 2, 1234.0f, true, NULL, NULL, MORNING, NULL},
    {"co2_2500", "co2", 2, 2500.0f, true, NULL, NULL, MORNING, NULL},
    {"co2_invalid", "co2", 2, 0.0f, false, NULL, NULL, MORNING, NULL},
    {"co2_warn_1000", "co2", 2, 950.0f, true, "warn_ppm", "1000", MORNING, NULL},
    {"co2_alarm_low", "co2", 2, 950.0f, true, "alarm_ppm", "500", MORNING, NULL},
    {"co2_399_5", "co2", 2, 399.5f, true, NULL, NULL, MORNING, NULL},
    {"hum_45", "humidity", 1, 45.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_5", "humidity", 1, 5.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_1", "humidity", 1, 1.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_100", "humidity", 1, 100.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_99_5", "humidity", 1, 99.5f, true, NULL, NULL, MORNING, NULL},
    {"hum_invalid", "humidity", 1, 0.0f, false, NULL, NULL, MORNING, NULL},
    {"hum_31", "humidity", 1, 31.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_65", "humidity", 1, 65.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_75", "humidity", 1, 75.0f, true, NULL, NULL, MORNING, NULL},
    {"hum_80_both", "humidity", 1, 80.8f, true, NULL, NULL, MORNING, NULL},
    {"alerts_answer", "alerts", NO_SENSOR, 0, false, NULL, NULL, MORNING, "alerts.json"},
    {"alerts_donetsk", "alerts", NO_SENSOR, 0, false, "region", "donetsk", MORNING, "alerts.json"},
    {"alerts_lviv", "alerts", NO_SENSOR, 0, false, "region", "lviv", MORNING, "alerts.json"},
    {"alerts_odesa", "alerts", NO_SENSOR, 0, false, "region", "odesa", MORNING, "alerts.json"},
    {"alerts_cut", "alerts", NO_SENSOR, 0, false, NULL, NULL, MORNING, "alerts_cut.json"},
    {"alerts_calm", "alerts", NO_SENSOR, 0, false, NULL, NULL, MORNING, "alerts_calm.json"},
    {"alerts_none", "alerts", NO_SENSOR, 0, false, NULL, NULL, MORNING, NULL},
};
#define CASE_COUNT ((int) (sizeof(CASES) / sizeof(CASES[0])))
