// Host check of the carousel's frame tick against its UI loop: the tick runs on the
// timer task, the frame on the UI loop, on different cores on the device. The
// harness owns the clock, the timers and the "post" of a frame event, so it can
// play the loop taking the event at any moment: between the post and the tick's
// store (the race that froze the panel on 2026-10-20), never (a lost event), or
// after the tick (the normal case). Exit status is the number of failed cases.
#include <stdio.h>
#include <string.h>
#include "carousel.h"
#include "esp_timer.h"
#include "oled.h"

// ---- stubs the carousel links against ------------------------------------------

static int64_t s_now_us;
static esp_timer_cb_t s_frame_cb;
static bool s_frame_active;
static uint8_t s_panel[OLED_FB_BYTES];

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
  if (strcmp(args->name, "carousel") == 0) {
    s_frame_cb = args->callback;
  }
  *out = (esp_timer_handle_t) args->name;
  return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us) {
  if (strcmp((const char *) t, "carousel") == 0) {
    s_frame_active = true;
  }
  return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t t) {
  if (strcmp((const char *) t, "carousel") == 0) {
    s_frame_active = false;
  }
  return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t t) {
  return strcmp((const char *) t, "carousel") == 0 && s_frame_active;
}

int64_t esp_timer_get_time(void) {
  return s_now_us;
}

void oled_copy_framebuffer(uint8_t *out) {
  memcpy(out, s_panel, OLED_FB_BYTES);
}

void oled_show_frame(const uint8_t *fb) {
  memcpy(s_panel, fb, OLED_FB_BYTES);
}

uint8_t *oled_frame_begin(void) {
  return s_panel;
}

void oled_frame_end(void) {
}

void led_set(uint8_t r, uint8_t g, uint8_t b) {
}

// ---- the screen manager, as the carousel sees it ---------------------------------

typedef enum { TAKE_INSIDE_POST, TAKE_NEVER, TAKE_AFTER_TICK } take_t;

static take_t s_take;
static int s_posts;
static int s_pending;  // events posted and not yet taken (TAKE_AFTER_TICK)
static int s_shown_idx;

static int shown(void) {
  return s_shown_idx;
}

static int neighbour(int dir) {
  return (s_shown_idx + dir + 3) % 3;  // three screens, wrapping
}

static void render(int idx, uint8_t *fb) {
  memset(fb, (uint8_t) (idx * 0x11), OLED_FB_BYTES);
}

static void commit(int idx) {
  s_shown_idx = idx;
  carousel_shown();  // as the screen manager does: the neighbours are stale now
}

static void led(int idx, uint8_t rgb[3]) {
  rgb[0] = rgb[1] = rgb[2] = 0;
}

static bool post(carousel_work_t work) {
  if (work != CAROUSEL_FRAME) {
    return true;
  }
  s_posts++;
  if (s_take == TAKE_INSIDE_POST) {
    carousel_frame();  // the loop on the other core took it before the tick stored its result
  } else if (s_take == TAKE_AFTER_TICK) {
    s_pending++;
  }
  return true;
}

static const carousel_ops_t OPS = {
    .shown = shown,
    .neighbour = neighbour,
    .render = render,
    .commit = commit,
    .led = led,
    .post = post,
};

// One timer period: the clock moves, the tick runs, then the loop takes what it
// was given (in the normal case).
static void tick(void) {
  s_now_us += 20000;
  if (s_frame_active) {
    s_frame_cb(NULL);
  }
  while (s_take == TAKE_AFTER_TICK && s_pending > 0) {
    s_pending--;
    carousel_frame();
  }
}

// A finger lands, drags far enough to page, and lifts.
static void swipe(void) {
  carousel_touch(TOUCH_PHASE_DOWN, 100);
  tick();
  carousel_touch(TOUCH_PHASE_MOVE, 140);
  tick();
  carousel_touch(TOUCH_PHASE_MOVE, 200);
  tick();
  carousel_touch(TOUCH_PHASE_UP, 200);
}

static int check(const char *name, bool ok, const char *detail) {
  printf("%-26s %s %s\n", name, ok ? "ok  " : "FAIL", detail);
  return ok ? 0 : 1;
}

int main(void) {
  int failed = 0;
  char detail[96];
  if (!carousel_init(&OPS)) {
    printf("carousel_init failed\n");
    return 1;
  }

  // A drag is under way (the loop took the first frames normally), then the loop
  // takes an event right inside the post, before the tick stores its result. The
  // old tick stored "queued" after the post and never posted again: the page froze
  // mid-slide for good, busy forever.
  s_take = TAKE_AFTER_TICK;
  int shown_before = s_shown_idx;
  carousel_touch(TOUCH_PHASE_DOWN, 100);
  tick();
  carousel_touch(TOUCH_PHASE_MOVE, 140);
  tick();
  carousel_touch(TOUCH_PHASE_MOVE, 200);
  tick();
  s_take = TAKE_INSIDE_POST;
  s_posts = 0;
  carousel_touch(TOUCH_PHASE_UP, 200);
  for (int i = 0; i < 60; i++) {
    tick();
  }
  snprintf(detail, sizeof(detail), "posts %d, shown %d -> %d, busy %d, reposted %d", s_posts,
           shown_before, s_shown_idx, carousel_busy(), carousel_frames_reposted());
  failed += check("loop takes inside post",
                  !carousel_busy() && s_shown_idx != shown_before && s_posts >= 10, detail);

  // The loop never takes the event (lost). The tick must post again after a while
  // instead of waiting forever, and count that it had to.
  s_take = TAKE_NEVER;
  s_posts = 0;
  int reposted_before = carousel_frames_reposted();
  carousel_touch(TOUCH_PHASE_DOWN, 100);
  tick();
  carousel_touch(TOUCH_PHASE_MOVE, 140);
  for (int i = 0; i < 25; i++) {
    tick();
  }
  snprintf(detail, sizeof(detail), "posts %d, reposted %d", s_posts,
           carousel_frames_reposted() - reposted_before);
  failed += check("lost event is re-posted",
                  s_posts >= 3 && s_posts <= 4 && carousel_frames_reposted() - reposted_before >= 2,
                  detail);
  carousel_touch(TOUCH_PHASE_UP, 140);

  // The normal case: the loop takes each event after the tick. A swipe pages to the
  // neighbour, the carousel goes idle, and nothing is counted as re-posted.
  s_take = TAKE_AFTER_TICK;
  while (s_pending > 0) {
    s_pending--;
    carousel_frame();
  }
  for (int i = 0; i < 60; i++) {
    tick();  // let the lost-event drag settle first
  }
  reposted_before = carousel_frames_reposted();
  shown_before = s_shown_idx;
  s_posts = 0;
  swipe();
  for (int i = 0; i < 60; i++) {
    tick();
  }
  snprintf(detail, sizeof(detail), "posts %d, shown %d -> %d, busy %d, reposted %d", s_posts,
           shown_before, s_shown_idx, carousel_busy(),
           carousel_frames_reposted() - reposted_before);
  failed += check("loop takes after the tick",
                  !carousel_busy() && s_shown_idx != shown_before &&
                      carousel_frames_reposted() - reposted_before == 0,
                  detail);

  printf("%d cases failed\n", failed);
  return failed;
}

// An older carousel.c has no counter; the race case then fails on "busy" alone.
__attribute__((weak)) int carousel_frames_reposted(void) {
  return 0;
}
