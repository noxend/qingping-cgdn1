// Capacitive touch strip: reads the touch MCU, passes the finger and a hold to the
// callbacks it is started with, and keeps a ring buffer of raw samples for GET /touch.
#pragma once

#include <stdint.h>

// The finger on the touch strip. pos is the raw position along the strip, 0..255.
typedef enum {
  TOUCH_PHASE_DOWN,
  TOUCH_PHASE_MOVE,
  TOUCH_PHASE_UP,
} touch_phase_t;

// Called from the touch task: the finger stream (down, move whenever the position
// changes, up), and a hold, once while a finger rests without travelling.
typedef void (*touch_finger_fn)(touch_phase_t phase, int pos);
typedef void (*touch_hold_fn)(void);

void touch_start(touch_finger_fn on_finger, touch_hold_fn on_hold);

// Raw samples as read, for GET /touch.
#define TOUCH_LOG 256

typedef struct {
  uint32_t ms;
  uint8_t b0, b1, rdy;
} touch_rec_t;

typedef struct {
  int drags;
  int holds;
  int shifted;  // reads that came status first and were swapped
  unsigned stack_free_min;
} touch_counts_t;

void touch_counts(touch_counts_t *out);

// The ring of samples, read live: entry k (0 = oldest) of n is at (start + k) % TOUCH_LOG.
const touch_rec_t *touch_log(int *start, int *n);
