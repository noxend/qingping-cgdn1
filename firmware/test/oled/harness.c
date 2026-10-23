// Host check of oled_compose.c and night_rules.c. Every expectation here is
// written from the panel layout in docs/hardware.md and the night profile's
// description in night_rules.h, not from the code under test: a panel pixel is
// found by its own coordinates, the window by the clock.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "night_rules.h"
#include "oled_compose.h"

#define W OLED_COMPOSE_W
#define H OLED_COMPOSE_H
#define COL_BYTES OLED_COMPOSE_COL_BYTES

static int failures;

static void fail(const char *what) {
  failures++;
  printf("FAIL %s\n", what);
}

static void check(bool ok, const char *what) {
  if (!ok) {
    fail(what);
  }
}

// ---- the frame under test ------------------------------------------------------

static uint8_t frame[H * COL_BYTES];

static void set_pixel(int x, int y, uint8_t level) {
  uint8_t *b = &frame[y * COL_BYTES + x / 2];
  int shift = (x & 1) * 4;
  *b = (uint8_t) ((*b & ~(0x0F << shift)) | ((level & 0x0F) << shift));
}

static uint8_t get_pixel(int x, int y) {
  if (x < 0 || x >= W || y < 0 || y >= H) {
    return 0;
  }
  uint8_t b = frame[y * COL_BYTES + x / 2];
  return (x & 1) ? b >> 4 : b & 0x0F;
}

// A picture with every level and marks that tell the corners apart: a one pixel
// frame on the outermost pixels, a 16 step gradient bar, a solid block top left,
// a hollow box bottom right, and a diagonal.
static void draw_test_frame(void) {
  memset(frame, 0, sizeof(frame));
  for (int x = 0; x < W; x++) {
    set_pixel(x, 0, 15);
    set_pixel(x, H - 1, 15);
  }
  for (int y = 0; y < H; y++) {
    set_pixel(0, y, 15);
    set_pixel(W - 1, y, 15);
  }
  for (int level = 0; level < 16; level++) {
    for (int x = 20 + level * 8; x < 28 + level * 8; x++) {
      for (int y = 56; y < 72; y++) {
        set_pixel(x, y, (uint8_t) level);
      }
    }
  }
  for (int x = 10; x < 30; x++) {
    for (int y = 10; y < 30; y++) {
      set_pixel(x, y, 15);
    }
  }
  for (int x = 130; x < 150; x++) {
    set_pixel(x, 98, 15);
    set_pixel(x, 117, 15);
  }
  for (int y = 98; y < 118; y++) {
    set_pixel(130, y, 15);
    set_pixel(149, y, 15);
  }
  for (int i = 0; i < 100; i++) {
    set_pixel(30 + i, 30 + i * 60 / 100, 8);
  }
}

// ---- reading the composed output as the glass shows it ----------------------------

// Panel output: 128 streamed columns of 80 bytes. The glass shows column `col`
// as frame row `col` and byte i's low nibble as x = 2i (docs/hardware.md, Frame).
static uint8_t panel[H][COL_BYTES];

static void compose_all(const oled_compose_t *c) {
  for (int col = 0; col < H; col++) {
    oled_compose_column(frame, col, c, panel[col]);
  }
}

static uint8_t panel_pixel(int panel_x, int col) {
  uint8_t b = panel[col][panel_x / 2];
  return (panel_x & 1) ? b >> 4 : b & 0x0F;
}

static void write_pgm(const char *name) {
  char path[64];
  snprintf(path, sizeof(path), "%s.pgm", name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror(path);
    return;
  }
  fprintf(f, "P5\n%d %d\n255\n", W, H);
  for (int col = 0; col < H; col++) {
    for (int panel_x = 0; panel_x < W; panel_x++) {
      fputc(panel_pixel(panel_x, col) * 17, f);
    }
  }
  fclose(f);
}

// ---- compose: orientation -------------------------------------------------------

// Upright: the glass shows the frame as drawn. Mirrored x: panel position p shows
// frame x = 159 - p. Mirrored y: column c shows frame row 127 - c.
static void test_flip(void) {
  oled_compose_t c;
  for (int flip = 0; flip < 4; flip++) {
    oled_compose_init(&c);
    c.flip = (uint8_t) flip;
    compose_all(&c);
    int bad = 0;
    for (int col = 0; col < H; col++) {
      for (int p = 0; p < W; p++) {
        int x = (flip & 1) ? W - 1 - p : p;
        int y = (flip & 2) ? H - 1 - col : col;
        if (panel_pixel(p, col) != get_pixel(x, y)) {
          bad++;
        }
      }
    }
    char what[64];
    snprintf(what, sizeof(what), "flip %d: %d pixels differ from the mirror rule", flip, bad);
    check(bad == 0, what);
    if (flip == 0) {
      write_pgm("as_drawn");
    }
  }
}

// ---- compose: the orbit offset --------------------------------------------------

// The picture moved by (dx, dy): the glass at (x, y) shows the frame pixel at
// (x - dx, y - dy), and black where that lies outside the frame.
static void test_offset(void) {
  oled_compose_t c;
  oled_compose_init(&c);
  c.dx = 2;
  c.dy = -1;
  compose_all(&c);
  int bad = 0;
  int revealed = 0;
  for (int col = 0; col < H; col++) {
    for (int p = 0; p < W; p++) {
      uint8_t want = get_pixel(p - 2, col + 1);
      if (panel_pixel(p, col) != want) {
        bad++;
      }
      bool outside = p - 2 < 0 || col + 1 >= H;
      if (outside && panel_pixel(p, col) == 0) {
        revealed++;
      }
    }
  }
  check(bad == 0, "offset (2,-1): pixels differ from the shift rule");
  // Two columns on the left and one row at the bottom are uncovered: 2*128 + 160 - 2.
  check(revealed == 2 * H + W - 2, "offset (2,-1): uncovered edge is not black");
  write_pgm("orbit_2_m1");
}

// 16 positions, each one pixel from the last, all within the amplitude, none
// repeated, and the walk closes.
static void test_orbit_walk(void) {
  int8_t dx[OLED_ORBIT_STEPS];
  int8_t dy[OLED_ORBIT_STEPS];
  for (int i = 0; i < OLED_ORBIT_STEPS; i++) {
    oled_orbit_offset(i, &dx[i], &dy[i]);
  }
  for (int i = 0; i < OLED_ORBIT_STEPS; i++) {
    int next = (i + 1) % OLED_ORBIT_STEPS;
    int step = abs(dx[next] - dx[i]) + abs(dy[next] - dy[i]);
    check(step == 1, "orbit: a step is not one pixel");
    check(abs(dx[i]) <= OLED_ORBIT_AMPLITUDE && abs(dy[i]) <= OLED_ORBIT_AMPLITUDE,
          "orbit: a position is outside the amplitude");
    for (int j = 0; j < i; j++) {
      check(dx[i] != dx[j] || dy[i] != dy[j], "orbit: a position repeats");
    }
  }
  int8_t x16 = 0;
  int8_t y16 = 0;
  oled_orbit_offset(OLED_ORBIT_STEPS, &x16, &y16);
  check(x16 == dx[0] && y16 == dy[0], "orbit: does not return to the start");
}

// ---- compose: the level table -----------------------------------------------------

static void test_levels(void) {
  oled_compose_t c;
  oled_compose_init(&c);
  night_levels(30, false, c.level);
  compose_all(&c);
  int bad = 0;
  for (int col = 0; col < H; col++) {
    for (int p = 0; p < W; p++) {
      if (panel_pixel(p, col) != c.level[get_pixel(p, col)]) {
        bad++;
      }
    }
  }
  check(bad == 0, "levels 30 %: a pixel is not its table entry");
  write_pgm("dim_30");

  night_levels(100, true, c.level);
  compose_all(&c);
  int lit = 0;
  for (int col = 0; col < H; col++) {
    for (int p = 0; p < W; p++) {
      if (panel_pixel(p, col) != 0) {
        lit++;
      }
    }
  }
  check(lit == 0, "off: a pixel is lit");
  write_pgm("off");
}

// ---- night rules ------------------------------------------------------------------

static void test_night_levels(void) {
  uint8_t t[16];
  night_levels(100, false, t);
  for (int i = 0; i < 16; i++) {
    check(t[i] == i, "100 %: not the identity");
  }
  night_levels(30, false, t);
  check(t[0] == 0, "30 %: black stays black");
  check(t[15] == 5, "30 %: full white is not 5 (15 * 0.3, rounded)");
  check(t[1] == 1, "30 %: the faintest grey dropped out");
  check(t[8] == 2, "30 %: mid grey is not 2");
  night_levels(10, false, t);
  check(t[15] == 2 && t[1] == 1, "10 %: wrong ends");
  night_levels(50, true, t);
  for (int i = 0; i < 16; i++) {
    check(t[i] == 0, "off: a level is lit");
  }
}

static uint16_t hm(int h, int m) {
  return (uint16_t) (h * 60 + m);
}

static void test_window(void) {
  // 22:00 to 08:00 runs through midnight.
  check(!night_in_window(hm(22, 0), hm(8, 0), hm(21, 59)), "22-08 at 21:59");
  check(night_in_window(hm(22, 0), hm(8, 0), hm(22, 0)), "22-08 at 22:00");
  check(night_in_window(hm(22, 0), hm(8, 0), hm(23, 59)), "22-08 at 23:59");
  check(night_in_window(hm(22, 0), hm(8, 0), hm(0, 0)), "22-08 at 00:00");
  check(night_in_window(hm(22, 0), hm(8, 0), hm(3, 0)), "22-08 at 03:00");
  check(night_in_window(hm(22, 0), hm(8, 0), hm(7, 59)), "22-08 at 07:59");
  check(!night_in_window(hm(22, 0), hm(8, 0), hm(8, 0)), "22-08 at 08:00");
  check(!night_in_window(hm(22, 0), hm(8, 0), hm(12, 0)), "22-08 at 12:00");
  // 09:00 to 17:00 inside one day.
  check(!night_in_window(hm(9, 0), hm(17, 0), hm(8, 59)), "09-17 at 08:59");
  check(night_in_window(hm(9, 0), hm(17, 0), hm(9, 0)), "09-17 at 09:00");
  check(night_in_window(hm(9, 0), hm(17, 0), hm(16, 59)), "09-17 at 16:59");
  check(!night_in_window(hm(9, 0), hm(17, 0), hm(17, 0)), "09-17 at 17:00");
  check(!night_in_window(hm(9, 0), hm(17, 0), hm(23, 0)), "09-17 at 23:00");
  // Start equal to end is never.
  check(!night_in_window(hm(6, 0), hm(6, 0), hm(6, 0)), "06-06 at 06:00");
  check(!night_in_window(hm(6, 0), hm(6, 0), hm(12, 0)), "06-06 at 12:00");
}

static void test_profile_bounds(void) {
  night_profile_t p;
  night_profile_default(&p);
  check(night_profile_ok(&p), "default profile rejected");
  check(!p.enabled && p.start_min == hm(22, 0) && p.end_min == hm(8, 0),
        "default is not off, 22:00 to 08:00");
  check(p.screen_pct == 30 && !p.screen_off && p.led_pct == 20, "default levels");
  p.screen_pct = 5;
  check(!night_profile_ok(&p), "screen 5 % accepted");
  p.screen_pct = 101;
  check(!night_profile_ok(&p), "screen 101 % accepted");
  p.screen_pct = 100;
  p.led_pct = 101;
  check(!night_profile_ok(&p), "led 101 % accepted");
  p.led_pct = 0;
  check(night_profile_ok(&p), "led 0 % rejected");
  p.start_min = MINUTES_PER_DAY;
  check(!night_profile_ok(&p), "start 24:00 accepted");
  p.start_min = 0;
  p.version = NIGHT_PROFILE_VERSION + 1;
  check(!night_profile_ok(&p), "another version accepted");
}

int main(void) {
  draw_test_frame();
  test_flip();
  test_offset();
  test_orbit_walk();
  test_levels();
  test_night_levels();
  test_window();
  test_profile_bounds();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("ok\n");
  return 0;
}
