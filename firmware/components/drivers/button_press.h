// Turns samples of the power button line into clicks, holds and long presses: a
// press released before BUTTON_HOLD_MS is a click, a press held for BUTTON_HOLD_MS is
// a hold (the device menu), and one held on to BUTTON_LONG_MS is a long press (power
// off), each reported once while still held. The stock firmware's long click is 2 s
// (docs/hardware.md, Power button); the menu took that range, so power off waits
// longer. No hardware here, so the host test runs it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BUTTON_HOLD_MS 1000
#define BUTTON_LONG_MS 5000
// The line must be seen released this long before a press counts: the press that
// powered the board on may still be held at boot, and a line stuck low is no press.
#define BUTTON_ARM_MS 500
// A release must last this long, so a bounce inside a press does not split it.
#define BUTTON_RELEASE_MS 100

typedef enum {
  BUTTON_NONE,
  BUTTON_CLICK,
  BUTTON_HOLD,
  BUTTON_LONG,
} button_event_t;

typedef struct {
  bool armed;
  bool down;
  bool hold_sent;
  bool long_sent;
  bool have_up;
  uint32_t up_since;  // the line released since then
  uint32_t down_at;
} button_press_t;

// One sample: pressed is the line's state now (GPIO36 low), now_ms a running clock.
// Start from a zeroed button_press_t.
button_event_t button_press_step(button_press_t *b, bool pressed, uint32_t now_ms);
