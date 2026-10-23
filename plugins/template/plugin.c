// A starting point for a screen plugin: copy this folder, rename it, and change what
// you need. It shows a line of text from its settings, the room temperature and the
// time, and redraws once a minute while it is on the panel.
//
// Build and look at it on your computer:   ./preview.sh my_plugin
// Then upload my_plugin/my_plugin.wasm in the web interface: Store, "Add plugin",
// Choose file.
//
// The whole SDK is sdk/plugin_wasm.h; README.md explains every call and limit.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_MINUTE 0  // timers are numbered 0 and 1, and each plugin has its own

// What draw shows. The callbacks below change it; draw only reads it, because the
// core may call draw at any time (a web preview, the carousel's next frame).
static char s_text[48] = "Hello";

// on_config: after the user saved a setting in the web interface, and once at start.
// Read every setting you use here; the keys are the ones in plugin.json "params".
WASM_EXPORT("on_config") void template_on_config(void) {
  plugin_setting_get("text", s_text, sizeof(s_text), "Hello");
}

// on_event: the screen came onto the panel (CORE_EV_SHOW), left it (CORE_EV_HIDE), or
// one of its timers fired (CORE_EV_TIMER, with the timer's number).
WASM_EXPORT("on_event") void template_on_event(int type, int timer_id) {
  if (type == CORE_EV_SHOW) {
    // A timer runs only while the screen is shown: start it here, stop it on HIDE.
    core_timer_start(TIMER_MINUTE, 60 * 1000, true);
  } else if (type == CORE_EV_HIDE) {
    core_timer_stop(TIMER_MINUTE);
  } else if (type == CORE_EV_TIMER && timer_id == TIMER_MINUTE) {
    core_redraw();  // draw runs again once this callback returns
  }
}

// draw: the whole frame, 160 x 128 pixels, 16 grey levels (0 off, 15 full). The ui_
// calls lay it out as every screen of the device is: a caption at the top, a value in
// large digits, a label under it. Anything else is drawn with the gfx_ calls.
WASM_EXPORT("draw") void template_draw(void) {
  gfx_clear();
  ui_caption(s_text);

  // The time the device keeps, in its time zone. The large digits have digits and a few
  // signs only.
  wasm_tm_t now;
  core_localtime(&now, sizeof(now));
  char clock[8];
  snprintf(clock, sizeof(clock), "%02d:%02d", (int) now.hour, (int) now.min);
  ui_value(clock);

  // A sensor reading: valid is 0 until the first reading, or when it is too old, and the
  // label then says so. snprintf has no %f: print the tenths as integers, the sign apart
  // so -0.5 keeps it.
  wasm_sensor_t temp;
  char line[24] = "no data";
  if (plugin_sensor_get(SENSOR_TEMP, &temp, sizeof(temp))) {
    int tenths = (int) (temp.value * 10.0f);
    const char *sign = tenths < 0 ? "-" : "";
    tenths = tenths < 0 ? -tenths : tenths;
    snprintf(line, sizeof(line), "%s%d.%d °C", sign, tenths / 10, tenths % 10);
  }
  ui_label(line);
}
