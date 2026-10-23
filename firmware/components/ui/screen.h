// The core side of the plugin system: what app_main, the touch wiring and the HTTP
// handlers call.
#pragma once

#include "plugin_sdk.h"
#include "esp_http_server.h"
#include "touch.h"

// Input drivers call these from any task. core_touch drives the page slider: the
// screen follows the finger and settles on the next or previous enabled screen, and
// while the device menu is open it moves the menu's selection and chooses.
void core_touch(touch_phase_t phase, int pos);
void core_hold(void);

// The power button, from any task: a click goes back in the device menu, and
// outside it blanks the panel and the LED or brings them back; a hold opens the
// menu. The power-off path shows "Powering off" with the LED dark, or blanks both,
// for good.
void screen_manager_button_click(void);
void screen_manager_button_hold(void);
void screen_manager_powering_off(void);
void screen_manager_display_off(void);
// The firmware version the device menu shows (api owns the define).
void screen_manager_set_version(const char *version);
// What the menu's "Power off" calls, on the UI loop; it must not wait for the loop.
void screen_manager_set_power_off(void (*power_off)(void));

// Starts the core: the UI loop owns the display and the LED and runs the active
// plugin; a worker task runs network requests.
void screen_manager_start(void);
// How many GET /screen requests the UI loop did not answer in time, for /status.
int screen_manager_ui_timeouts(void);
// Frames the carousel's tick had to post again because the UI loop had not taken
// the previous one in time (carousel_frames_reposted). Zero on a healthy device.
int screen_manager_frames_reposted(void);
// The id of the plugin whose callback is running on the UI loop, or NULL. The SDK
// keys settings by it.
const char *screen_manager_current_id(void);
// Asks the UI loop to draw the shown screen again (after a probe used the panel).
void screen_manager_redraw_shown(void);

// GET /config, POST /config, GET /screen, GET /plugin/stats and GET /alerts.
esp_err_t screen_http_register(httpd_handle_t srv);
