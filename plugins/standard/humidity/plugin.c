// The SHT4x relative humidity in large type, whole percent, with the LED as a comfort
// light.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_READ 1

static char s_num[8];  // last value, for change detection
static float s_rh;     // last valid reading, for the LED
static bool s_have;    // a reading has succeeded

// The value as the panel shows it, or nothing while the sensor has nothing valid.
static void read_sensor(char *out, int cap) {
  wasm_sensor_t r;
  if (plugin_sensor_get(SENSOR_HUM, &r, sizeof(r))) {
    s_rh = r.value;
    s_have = true;
    snprintf(out, cap, "%d", (int) (r.value + 0.5f));
  } else {
    s_have = false;
    out[0] = '\0';
  }
}

WASM_EXPORT("on_event") void hum_on_event(int type, int timer_id) {
  if (type == CORE_EV_SHOW) {
    core_timer_start(TIMER_READ, 2000, true);
  } else if (type == CORE_EV_HIDE) {
    core_timer_stop(TIMER_READ);
  } else if (type == CORE_EV_TIMER) {
    char num[8];
    read_sensor(num, sizeof(num));
    if (strcmp(num, s_num) != 0) {
      strcpy(s_num, num);
      core_redraw();
    }
  }
}

WASM_EXPORT("draw") void hum_draw(void) {
  char num[8];
  read_sensor(num, sizeof(num));
  gfx_clear();
  ui_value(num);
  ui_label("%");
}

// Comfort band for indoor air: 40 to 60 % healthy, 30 to 70 % acceptable, outside that too
// dry or too humid. Green, amber, red; no reading leaves it off.
WASM_EXPORT("led") int hum_led(void) {
  if (!s_have) {
    return -1;
  }
  if (s_rh >= 40.0f && s_rh <= 60.0f) {
    return 0x00FF00;
  }
  return s_rh >= 30.0f && s_rh <= 70.0f ? 0xFF9600 : 0xFF0000;
}
