// The battery, read the way the stock Battery_Task reads it (docs/hardware.md):
// the cell through a 1:2 divider on GPIO39 (ADC1 channel 3), USB power on GPIO34
// (low while USB-C feeds the board) and the charger's state on GPIO35 (low while
// it charges). Charging itself is the charger's own: the ESP32 only reads it.
#pragma once

#include <stdbool.h>

typedef struct {
  int mv;         // the cell, 0 when the ADC is not available
  int percent;    // from mv by the stock table, -1 without mv; a guess while charging
  bool usb;       // USB-C power present
  bool charging;  // the charger reports charging (only with usb)
} battery_t;

// After creds_seed_entropy, which borrows the SAR ADC, and before button_start:
// powering the ADC up once pulses GPIO36 low for a moment (ESP32 errata 3.11),
// and later reads keep it powered.
void battery_init(void);

// Reads now (5 samples a tick apart) and keeps the result for battery_last.
battery_t battery_read(void);

// The last battery_read, from any task.
battery_t battery_last(void);
