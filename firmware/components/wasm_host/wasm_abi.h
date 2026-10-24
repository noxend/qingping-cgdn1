// What a module and the device agree on, as plugins/sdk/plugin_wasm.h publishes it: the
// numbers a module passes and the structs it hands over. The names are the device's,
// since plugin_sdk.h already has plugin_wasm.h's. test/sdk_abi checks this file against
// plugin_wasm.h; wasm_bindings.c and wasm_plugins.c check plugin_sdk.h against it.
#pragma once

#include <stdint.h>

#define WASM_GFX_W 160
#define WASM_GFX_H 128
#define WASM_GFX_KEEP 255

#define WASM_FONT_TEXT 0
#define WASM_FONT_DIGITS 1

#define WASM_SENSOR_TEMP 0
#define WASM_SENSOR_HUM 1
#define WASM_SENSOR_CO2 2

#define WASM_EV_SHOW 0
#define WASM_EV_HIDE 1
#define WASM_EV_TIMER 2

typedef struct {
  float value;
  int32_t valid;
  uint32_t age_ms;
} wasm_sensor_t;

typedef struct {
  int32_t sec, min, hour;
  int32_t mday, mon, year;
  int32_t wday, yday, isdst;
} wasm_tm_t;
