// The same numbers and layouts as the device reads them (wasm_host's wasm_abi.h and
// MANIFEST_SDK), in the order module_side.c prints them.
#include <stddef.h>
#include <stdio.h>
#include "manifest.h"
#include "wasm_abi.h"

int main(void) {
  printf("sdk %d\n", MANIFEST_SDK);
  printf("gfx_w %d\ngfx_h %d\ngfx_keep %d\n", WASM_GFX_W, WASM_GFX_H, WASM_GFX_KEEP);
  printf("font_text %d\nfont_digits %d\n", WASM_FONT_TEXT, WASM_FONT_DIGITS);
  printf("sensor_temp %d\nsensor_hum %d\nsensor_co2 %d\n", WASM_SENSOR_TEMP, WASM_SENSOR_HUM,
         WASM_SENSOR_CO2);
  printf("ev_show %d\nev_hide %d\nev_timer %d\n", WASM_EV_SHOW, WASM_EV_HIDE, WASM_EV_TIMER);
  printf("sensor_size %zu\n", sizeof(wasm_sensor_t));
  printf("sensor_value %zu\n", offsetof(wasm_sensor_t, value));
  printf("sensor_valid %zu\n", offsetof(wasm_sensor_t, valid));
  printf("sensor_age_ms %zu\n", offsetof(wasm_sensor_t, age_ms));
  printf("tm_size %zu\n", sizeof(wasm_tm_t));
  printf("tm_sec %zu\ntm_year %zu\n", offsetof(wasm_tm_t, sec), offsetof(wasm_tm_t, year));
  printf("tm_isdst %zu\n", offsetof(wasm_tm_t, isdst));
  return 0;
}
