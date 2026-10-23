// GPIO23 power latch: raised from a constructor before app_main, then pad hold
// keeps it high through software, panic and watchdog resets.
#pragma once

// Takes the pin over in the driver and holds the pad. Call first in app_main; the
// call also links the object whose constructor raises the pin before app_main.
void power_latch_hold(void);

// Lets go of the latch: the pad hold is dropped and the pin driven low, which cuts
// the board's power when nothing else (USB, the power MCU) keeps the rail up. Only
// the clean power-off path calls this.
void power_latch_release(void);
