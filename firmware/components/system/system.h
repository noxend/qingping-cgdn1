// Start-up of the chip's own services: flash storage and the task watchdog.
#pragma once

#include <stdbool.h>

// Mounts the default NVS partition, erasing it when its layout is unusable. When it
// still fails, the saved Wi-Fi is not available this boot.
void system_nvs_init(void);

// Our own tasks join the task watchdog (ui in its first event, net, sensors,
// touch in their loops). The net worker cannot feed it inside a TLS handshake or
// a header read, each up to 10 s of silence, so the window is 60 s for everyone:
// a hung task then panics with its name in the crash log within a minute.
void system_task_watchdog_init(void);

// Clean power off, in the order of the stock firmware's System_Power_Off: notes the
// reason in the settings store, lets the GPIO23 latch go, then waits while held()
// is true (the power MCU keeps the rail while the button is down, POWER_OFF_HELD_MS
// at most) and the rail collapses. Returns false when the board is still running
// POWER_OFF_GRACE_MS after that: USB power keeps the rail. The note stays, since
// pulling the cable then ends the power off.
#define POWER_OFF_HELD_MS 15000
#define POWER_OFF_GRACE_MS 5000
bool system_power_off(const char *reason, bool (*held)(void));

// Takes the note back and restarts: the way on after a power off that kept the rail.
void system_power_on(void);

// The reason the last power off noted, or "" when the board last went down another
// way. Read once at boot, after settings_init, which also clears the note.
void system_last_off_load(void);
const char *system_last_off(void);
