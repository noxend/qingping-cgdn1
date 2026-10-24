// The PM2.5 reading in large type, with the LED in the stock firmware's six colours.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_READ 1
#define READ_MS 1000  // the device reads the sensor once a second

// The stock firmware's pm25_led_thr and its colours: up to the first threshold green, up
// to the next yellow, then orange, red, purple, and dark red above the last. The colours
// are the stock's duty on the red, green and blue LEDs, out of 255.
static const int LIMITS[] = {35, 75, 115, 150, 250};
static const int COLOURS[] = {0x0BFF06, 0x808000, 0xBF4D00, 0xD90000, 0x4000BF, 0x82007D};

static char s_shown[16];  // the value last drawn, for change detection
static int s_value;       // last valid reading, for the LED
static bool s_have;       // a reading has succeeded

// Reads the sensor into the text the panel shows (whole µg/m³, or nothing while the
// sensor has nothing valid) and into s_have and s_value for the LED.
static void read_value(char *out, int cap) {
  wasm_sensor_t reading;
  if (!plugin_sensor_get(SENSOR_PM25, &reading, sizeof(reading))) {
    s_have = false;
    out[0] = '\0';
    return;
  }
  s_have = true;
  s_value = (int) (reading.value + 0.5f);
  snprintf(out, cap, "%d", s_value);
}

WASM_EXPORT("on_event") void pm25_on_event(int type, int timer_id) {
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

WASM_EXPORT("draw") void pm25_draw(void) {
  char value[16];
  read_value(value, sizeof(value));
  gfx_clear();
  ui_caption("PM2.5");
  ui_value(value);
  ui_label("ug/m3");  // GFX_FONT_TEXT has no µ or ³
}

WASM_EXPORT("led") int pm25_led(void) {
  if (!s_have) {
    return -1;
  }
  int level = 0;
  while (level < 5 && s_value > LIMITS[level]) {
    level++;
  }
  return COLOURS[level];
}
