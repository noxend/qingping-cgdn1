#include "carousel.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "heap_internal.h"
#include "led.h"
#include "oled.h"

static const char *TAG = "carousel";

// Raw strip positions run from about 23 to 250 end to end (measured on the device).
#define STRIP_SPAN 227
#define STRIP_SIGN -1   // the strip counts up toward the left of the screen (checked by hand)
#define FRAME_US 20000  // 50 frames a second; a full flush takes about 12 ms
#define REFRESH_US (5 * 1000000)  // neighbour frames are redrawn this often while idle
#define FOLLOW 0.55f              // share of the gap to the finger closed each frame
#define SETTLE 0.35f              // the same while settling after release
#define COMMIT_PX 48              // dragged this far, the page settles on the neighbour
#define FLING_PX_S 250            // or released this fast in the same direction
#define FLING_MIN_PX 8
#define STALE_US 300000  // no sample this long while dragging counts as a release
#define ROW_BYTES (OLED_W / 2)
#define TRAVEL_MIN 3  // strip counts before a touch is a drag; taps and holds keep the panel

typedef enum { IDLE, DRAG, SETTLE_TO } state_t;

static const carousel_ops_t *s_ops;
static uint8_t *s_cur;   // the shown screen when the drag began
static uint8_t *s_prev;  // the previous screen, ready
static uint8_t *s_next;  // the next screen, ready
static int s_prev_idx = -1;
static int s_next_idx = -1;
static bool s_stale = true;

// Written by the touch task, read by the UI loop.
static volatile int s_pos;
static volatile bool s_down;
// Time of the newest sample, in ms and 32 bits: one word, so the touch task's
// store and the UI loop's load cannot tear it the way a 64-bit value would.
static volatile uint32_t s_seen_ms;
static volatile int s_down_pos;  // where the finger landed

static state_t s_state;
static int s_start;
static int s_drag_shown;  // the shown screen when the drag began
static float s_dx;
static int s_goal;
static int64_t s_hist_us[4];  // recent (time, offset) pairs for the release velocity
static float s_hist_dx[4];
static int s_hist_n;

static uint8_t s_led_cur[3];   // LED colour of the shown screen when the drag began
static uint8_t s_led_prev[3];  // and of each neighbour; the LED blends between them
static uint8_t s_led_next[3];

static esp_timer_handle_t s_frame_timer;
static esp_timer_handle_t s_refresh_timer;
// Raised by the timer task before it posts a frame, cleared by the UI loop when it
// takes one; the two run on different cores.
static atomic_bool s_frame_queued;
static int s_queued_ticks;  // ticks in a row that found the flag up (timer task only)
static volatile int s_frames_reposted;
#define REPOST_TICKS 10  // 200 ms without the loop taking a frame: post it again

static int s_frame_us_max;
static int s_render_us_max;

// The flag goes up before the post and is never written after it, so a clear by
// the loop on the other core stays a clear whenever it lands.
static void frame_tick(void *arg) {
  if (atomic_load(&s_frame_queued)) {
    if (++s_queued_ticks < REPOST_TICKS) {
      return;
    }
    s_frames_reposted++;  // the loop is late or the event was lost: ask again
  }
  s_queued_ticks = 0;
  atomic_store(&s_frame_queued, true);
  if (!s_ops->post(CAROUSEL_FRAME)) {
    atomic_store(&s_frame_queued, false);
  }
}

static void refresh_tick(void *arg) {
  s_ops->post(CAROUSEL_REFRESH);
}

bool carousel_init(const carousel_ops_t *ops) {
  s_cur = heap_caps_malloc(OLED_FB_BYTES, HEAP_INTERNAL);
  s_prev = heap_caps_malloc(OLED_FB_BYTES, HEAP_INTERNAL);
  s_next = heap_caps_malloc(OLED_FB_BYTES, HEAP_INTERNAL);
  esp_timer_create_args_t f = {.callback = frame_tick, .name = "carousel"};
  esp_timer_create_args_t r = {.callback = refresh_tick, .name = "carousel_r"};
  bool ok = s_cur && s_prev && s_next && esp_timer_create(&f, &s_frame_timer) == ESP_OK &&
            esp_timer_create(&r, &s_refresh_timer) == ESP_OK;
  if (!ok) {
    ESP_LOGE(TAG, "no memory for the page frames; paging is off");
    return false;  // init runs once at boot, so the partial allocations are left as they are
  }
  esp_timer_start_periodic(s_refresh_timer, REFRESH_US);
  s_ops = ops;  // last: the touch task checks it before using the timers
  return true;
}

void carousel_touch(touch_phase_t phase, int pos) {
  if (!s_ops) {
    return;
  }
  s_pos = pos;
  s_seen_ms = (uint32_t) (esp_timer_get_time() / 1000);
  if (phase == TOUCH_PHASE_DOWN && !s_down) {
    s_down_pos = pos;
    s_down = true;
  } else if (phase == TOUCH_PHASE_UP) {
    s_down = false;
    return;
  }
  // Also on MOVE: a stop on the UI loop can race a touch-down and leave the timer off.
  if (s_down && !esp_timer_is_active(s_frame_timer)) {
    esp_timer_start_periodic(s_frame_timer, FRAME_US);
  }
}

// The page shifted by dx (even, -W..W) with the neighbour filling the gap: from the
// left for dx > 0, from the right for dx < 0. Whole bytes per row, two pixels each.
static void compose(int dx) {
  uint8_t *out = oled_frame_begin();
  if (!out) {
    return;
  }
  int k = (dx < 0 ? -dx : dx) / 2;
  const uint8_t *side = dx > 0 ? s_prev : s_next;
  for (int y = 0; y < OLED_H; y++) {
    uint8_t *o = out + y * ROW_BYTES;
    const uint8_t *c = s_cur + y * ROW_BYTES;
    const uint8_t *n = side + y * ROW_BYTES;
    if (dx >= 0) {
      memcpy(o, n + ROW_BYTES - k, k);
      memcpy(o + k, c, ROW_BYTES - k);
    } else {
      memcpy(o, c + k, ROW_BYTES - k);
      memcpy(o + ROW_BYTES - k, n, k);
    }
  }
  oled_frame_end();
}

// The LED moves from the shown screen's colour to the neighbour's as the page moves.
static void blend_led(int dx) {
  const uint8_t *to = dx > 0 ? s_led_prev : s_led_next;
  int t = dx < 0 ? -dx : dx;
  uint8_t c[3];
  for (int i = 0; i < 3; i++) {
    c[i] = (uint8_t) ((s_led_cur[i] * (OLED_W - t) + to[i] * t) / OLED_W);
  }
  led_set(c[0], c[1], c[2]);
}

static void render_neighbours(void) {
  int64_t t = esp_timer_get_time();
  s_prev_idx = s_ops->neighbour(-1);
  s_next_idx = s_ops->neighbour(1);
  if (s_prev_idx >= 0) {
    s_ops->render(s_prev_idx, s_prev);
  }
  if (s_next_idx == s_prev_idx && s_next_idx >= 0) {
    memcpy(s_next, s_prev, OLED_FB_BYTES);
  } else if (s_next_idx >= 0) {
    s_ops->render(s_next_idx, s_next);
  }
  int us = (int) (esp_timer_get_time() - t);
  if (us > s_render_us_max) {
    s_render_us_max = us;
  }
  s_stale = false;
}

static void finish(void) {
  if (!s_down) {
    esp_timer_stop(s_frame_timer);  // a finger already down starts the next drag on the next tick
  }
  s_state = IDLE;
  if (s_ops->shown() != s_drag_shown) {
    return;  // the web switched screens meanwhile; its redraw follows
  }
  int target = s_goal > 0 ? s_prev_idx : (s_goal < 0 ? s_next_idx : -1);
  if (target >= 0) {
    s_ops->commit(target);  // shows the same picture the panel already has
  } else {
    oled_show_frame(s_cur);
  }
}

static float release_velocity(void) {
  if (s_hist_n < 2) {
    return 0;
  }
  int a = (s_hist_n - 1) % 4;
  int b = (s_hist_n >= 4 ? s_hist_n - 4 : 0) % 4;
  float dt = (float) (s_hist_us[a] - s_hist_us[b]) / 1e6f;
  return dt > 0 ? (s_hist_dx[a] - s_hist_dx[b]) / dt : 0;
}

static void release(void) {
  float v = release_velocity();
  int dir = 0;
  if (s_dx >= COMMIT_PX || (v > FLING_PX_S && s_dx > FLING_MIN_PX)) {
    dir = 1;
  } else if (s_dx <= -COMMIT_PX || (v < -FLING_PX_S && s_dx < -FLING_MIN_PX)) {
    dir = -1;
  }
  int side = dir > 0 ? s_prev_idx : s_next_idx;
  s_goal = (dir != 0 && side >= 0) ? dir * OLED_W : 0;
  s_state = SETTLE_TO;
}

void carousel_frame(void) {
  atomic_store(&s_frame_queued, false);
  if (!s_ops) {
    return;
  }
  int64_t now = esp_timer_get_time();
  int64_t t0 = now;
  if (s_state == IDLE) {
    if (!s_down || s_ops->shown() < 0) {
      esp_timer_stop(s_frame_timer);
      return;
    }
    int travel = s_pos > s_down_pos ? s_pos - s_down_pos : s_down_pos - s_pos;
    if (travel < TRAVEL_MIN) {
      return;  // not a drag yet
    }
    if (s_stale) {
      render_neighbours();  // only when a swipe comes right after a page change
    }
    oled_copy_framebuffer(s_cur);
    s_ops->led(s_ops->shown(), s_led_cur);
    if (s_prev_idx >= 0) {
      s_ops->led(s_prev_idx, s_led_prev);
    }
    if (s_next_idx >= 0) {
      s_ops->led(s_next_idx, s_led_next);
    }
    s_drag_shown = s_ops->shown();
    s_start = s_pos;
    s_dx = 0;
    s_hist_n = 0;
    s_state = DRAG;
  }
  if (s_state == DRAG) {
    if (!s_down || (uint32_t) (now / 1000) - s_seen_ms > STALE_US / 1000) {
      s_down = false;
      release();
    } else {
      float target = (float) ((s_pos - s_start) * STRIP_SIGN * OLED_W) / STRIP_SPAN;
      if (target > 0 && s_prev_idx < 0) {
        target = 0;
      } else if (target < 0 && s_next_idx < 0) {
        target = 0;
      }
      target = target > OLED_W ? OLED_W : (target < -OLED_W ? -OLED_W : target);
      s_dx += (target - s_dx) * FOLLOW;
      int i = s_hist_n % 4;
      s_hist_us[i] = now;
      s_hist_dx[i] = s_dx;
      s_hist_n++;
    }
  }
  if (s_state == SETTLE_TO) {
    s_dx += ((float) s_goal - s_dx) * SETTLE;
    float gap = (float) s_goal - s_dx;
    if (gap < 2.0f && gap > -2.0f) {
      s_dx = (float) s_goal;
    }
  }
  int dx = (int) (s_dx >= 0 ? s_dx + 0.5f : s_dx - 0.5f) & ~1;
  compose(dx);
  blend_led(dx);
  int us = (int) (esp_timer_get_time() - t0);
  if (us > s_frame_us_max) {
    s_frame_us_max = us;
  }
  if (s_state == SETTLE_TO && s_dx == (float) s_goal) {
    finish();
  }
}

void carousel_refresh(void) {
  if (s_ops && s_state == IDLE && !s_down && s_ops->shown() >= 0) {
    render_neighbours();
  }
}

void carousel_shown(void) {
  s_stale = true;
  carousel_refresh();
}

bool carousel_busy(void) {
  return s_state != IDLE;
}

int carousel_frames_reposted(void) {
  return s_frames_reposted;
}

int carousel_stats(char *out, int cap) {
  return snprintf(out, cap, "frame_us_max=%d render_us_max=%d heap_free=%u", s_frame_us_max,
                  s_render_us_max, (unsigned) heap_caps_get_free_size(HEAP_INTERNAL));
}
