// The setup access point's lifetime is owned by net.c: it runs until the station
// has held an address for a while, comes back when the station is lost, and can be
// asked for from the device itself (a hold on the touch strip).
#pragma once

// Bring the setup AP up for a while, from any task: AP_HOLD_US once the station
// holds an address (moving to another router), for as long as it has none otherwise.
void setup_ap_show(void);
