// The device menu, drawn by the core over the shown screen: a hold on the power
// button opens it, a swipe on the touch strip moves the selection, a tap chooses, a
// click on the button goes back a page and closes it from the list. Pure state and
// drawing (through the SDK's gfx calls), so the host harness renders it too; the
// screen manager owns the events, the timer and what an action does.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "touch.h"

// What the info page shows; the screen manager fills it before each draw.
typedef struct {
  const char *version;
  char ip[16];    // "" while the station has no address
  char ssid[33];  // "" while not joined
  int up_s;
  char ap_ssid[20];  // the setup hotspot's name and password, shown so a user can join it
  char ap_pass[16];
  char host[40];  // the device's mDNS name, "cgdn1.local"
} menu_info_t;

// What the screen manager has to do after a finger or a click.
typedef enum {
  MENU_NONE,           // nothing changed
  MENU_REDRAW,         // the page or the selection changed
  MENU_POWER_OFF,      // "Power off" was chosen
  MENU_RESTART,        // "Restart" was chosen
  MENU_WIFI_SETUP,     // bring the setup hotspot up; the menu shows its page
  MENU_FACTORY_RESET,  // the user confirmed the reset
  MENU_CLOSE,          // close the menu and repaint the screen
} menu_action_t;

void menu_open(void);
// Opens the menu on its Wi-Fi setup page: what a device with no Wi-Fi shows.
void menu_open_setup(void);
// Opens the menu on the page that says where to open the device after it joined the
// network saved from the setup page.
void menu_open_joined(void);
void menu_close(void);
bool menu_is_open(void);

// What the menu does when left alone. The setup page of a device with no network
// blanks the panel, so its code does not burn in; any other page closes, to the
// setup page while there is no network.
bool menu_idle_blanks(bool has_network);
// How long the menu waits for a touch before that.
int64_t menu_idle_us(bool has_network);

// The finger while the menu is open, `ms` a running clock: a swipe moves the
// selection, a short touch that stays put is a tap and chooses the selected row; a
// longer rest is a hold, which does nothing. `online`: the device is on the home
// Wi-Fi, so the hotspot is asked for before it starts.
menu_action_t menu_finger(touch_phase_t phase, int pos, uint32_t ms, bool online);
// The power button's click while the menu is open: the page before, or close.
menu_action_t menu_back(void);

// Draws the current page into the frame (gfx_clear first).
void menu_draw(const menu_info_t *info);
