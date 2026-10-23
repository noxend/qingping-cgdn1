// The power button: the ATtiny814 power MCU pulls GPIO36 low while it is held
// (input only, no internal pull). button_press.h decides clicks and long presses.
#pragma once

#include <stdbool.h>

// Called on the button task: a click, and a long press once while still held.
typedef void (*button_fn)(void);

// Starts the polling task. Nothing is reported until the line has been seen
// released, so the press that powered the board on is not a long press.
void button_start(button_fn on_click, button_fn on_long);

// For GET /status: whether presses count yet, the line now (0 while held), and how
// many clicks and long presses were seen since boot.
typedef struct {
  bool armed;
  int level;
  int clicks;
  int longs;
} button_counts_t;

void button_counts(button_counts_t *out);

// True while the button is held.
bool button_pressed(void);
