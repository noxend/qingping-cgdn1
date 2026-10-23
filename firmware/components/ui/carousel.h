// Page carousel: the shown screen follows the finger along the touch strip and the
// previous or next screen slides in beside it; on release it settles on the
// neighbour or back. Three frames are kept ready (previous, shown, next) whatever
// the number of screens, and the list wraps, so paging never ends.
//
// The touch strip reports about 20 positions a second in coarse steps, so the
// carousel animates on its own frame tick and eases the page towards the finger
// instead of jumping to each sample.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "touch.h"

typedef enum {
  CAROUSEL_FRAME,    // call carousel_frame() on the UI loop
  CAROUSEL_REFRESH,  // call carousel_refresh() on the UI loop
} carousel_work_t;

// What the carousel needs from the screen manager. All are called on the UI loop,
// except post, which is called from timer and touch tasks.
typedef struct {
  int (*shown)(void);                    // index of the shown screen, -1 before boot
  int (*neighbour)(int dir);             // +1 next, -1 previous, wrapping; -1 when none
  void (*render)(int idx, uint8_t *fb);  // draw screen idx off-screen into fb
  void (*commit)(int idx);               // make idx the shown screen
  void (*led)(int idx, uint8_t rgb[3]);  // the LED colour screen idx asks for, 0,0,0 for off
  bool (*post)(carousel_work_t work);    // queue work for the UI loop; must not block
} carousel_ops_t;

// Allocates the frames. False when there is not enough memory; paging is then off.
bool carousel_init(const carousel_ops_t *ops);

// The finger, from the touch task. Positions are raw strip values.
void carousel_touch(touch_phase_t phase, int pos);

// UI loop only.
void carousel_frame(void);    // one animation step
void carousel_refresh(void);  // redraw the neighbour frames, unless a drag is on
void carousel_shown(void);    // the shown screen changed: neighbours are stale
bool carousel_busy(void);     // the carousel owns the panel (drag or settle)

// How often the frame tick had to post a frame again because the UI loop had not
// taken the previous one within REPOST_TICKS. Zero on a healthy device.
int carousel_frames_reposted(void);

// ---- For the test hooks only ----------------------------------------------------
// Nothing in this firmware calls these; the test endpoints of the private cgdn1-dev
// repository do (see api/test_hooks.h).
// One line of timing and memory figures for diagnostics.
int carousel_stats(char *out, int cap);
