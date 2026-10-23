// Turns samples of the power button line into clicks and long presses, with the
// stock firmware's timing (docs/hardware.md, Power button): a press released before
// BUTTON_LONG_MS is a click, a press held for BUTTON_LONG_MS is a long press,
// reported once while still held. No hardware here, so the host test runs it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BUTTON_LONG_MS 2000
// The line must be seen released this long before a press counts: the press that
// powered the board on may still be held at boot, and a line stuck low is no press.
#define BUTTON_ARM_MS 500
// A release must last this long, so a bounce inside a press does not split it.
#define BUTTON_RELEASE_MS 100

typedef enum {
  BUTTON_NONE,
  BUTTON_CLICK,
  BUTTON_LONG,
} button_event_t;

typedef struct {
  bool armed;
  bool down;
  bool long_sent;
  bool have_up;
  uint32_t up_since;  // the line released since then
  uint32_t down_at;
} button_press_t;

// One sample: pressed is the line's state now (GPIO36 low), now_ms a running clock.
// Start from a zeroed button_press_t.
button_event_t button_press_step(button_press_t *b, bool pressed, uint32_t now_ms);
