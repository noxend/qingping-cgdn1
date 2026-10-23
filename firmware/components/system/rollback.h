// The image's way back: the bootloader's RTC watchdog, the boot-loop counter and
// the rollback gate.
//
//   - Rollback gate: a fresh OTA image is confirmed only after 60 s of being
//     healthy. Otherwise it reboots within 3 min and the bootloader rolls back.
//   - The bootloader's RTC watchdog stays armed while an image is unconfirmed,
//     so a hang before confirmation also ends in a rollback.
//   - Boot-loop counter: three crash resets in a row roll back even a confirmed
//     image, provided the other slot holds a working app.
#pragma once

#include <stdbool.h>

// True while the running image waits for confirmation. A confirmed image does not
// need the bootloader's watchdog, so it is turned off here. Call early in app_main.
bool rollback_boot(void);

// Counts crash resets in a row and rolls back after three; clears the count after
// 10 minutes up.
void rollback_bootloop_check(void);

// For an unconfirmed image, waits until healthy() has held for 60 s, then confirms
// it; reboots into a rollback when that does not happen within 3 min. healthy() is
// called once a second, right after the RTC watchdog is fed. Returns at once for a
// confirmed image.
void rollback_settle(bool pending, bool (*healthy)(void));

// For GET /status.
const char *rollback_reset_reason(void);  // why the chip last restarted
const char *rollback_image_state(void);   // the running slot's state in otadata
unsigned long rollback_bootloops(void);   // crash resets in a row
int rollback_confirm_ms(void);            // how long confirming took; -1 until it did
