// The numbers and struct layouts a plugin is built with (plugins/sdk/plugin_wasm.h),
// one "name value" line each, in the order device_side.c prints them.
#include <stddef.h>
#include <stdio.h>
#include "plugin_wasm.h"

int main(void) {
  printf("sdk %d\n", WASM_SDK_VERSION);
  printf("gfx_w %d\ngfx_h %d\ngfx_keep %d\n", GFX_W, GFX_H, GFX_KEEP);
  printf("font_text %d\nfont_digits %d\n", GFX_FONT_TEXT, GFX_FONT_DIGITS);
  printf("sensor_temp %d\nsensor_hum %d\nsensor_co2 %d\n", SENSOR_TEMP, SENSOR_HUM, SENSOR_CO2);
  printf("sensor_pm25 %d\nsensor_pm10 %d\n", SENSOR_PM25, SENSOR_PM10);
  printf("ev_show %d\nev_hide %d\nev_timer %d\n", CORE_EV_SHOW, CORE_EV_HIDE, CORE_EV_TIMER);
  printf("sensor_size %zu\n", sizeof(wasm_sensor_t));
  printf("sensor_value %zu\n", offsetof(wasm_sensor_t, value));
  printf("sensor_valid %zu\n", offsetof(wasm_sensor_t, valid));
  printf("sensor_age_ms %zu\n", offsetof(wasm_sensor_t, age_ms));
  printf("tm_size %zu\n", sizeof(wasm_tm_t));
  printf("tm_sec %zu\ntm_year %zu\n", offsetof(wasm_tm_t, sec), offsetof(wasm_tm_t, year));
  printf("tm_isdst %zu\n", offsetof(wasm_tm_t, isdst));
  return 0;
}
