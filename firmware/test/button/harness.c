// Plays button presses through button_press_step at the 50 ms poll of button.c and
// checks which events come out and when.
#include <stdio.h>
#include <string.h>
#include "button_press.h"

#define POLL_MS 50

typedef struct {
  int clicks;
  int holds;
  int longs;
  int hold_at;  // ms into the press when the hold came, or -1
  int long_at;  // ms into the press when the long press came, or -1
} result_t;

// segs: alternating line states starting with `first`, each lasting the given ms.
static result_t play(int first_pressed, const int *segs, int n) {
  button_press_t b;
  memset(&b, 0, sizeof(b));
  result_t r = {0, 0, 0, -1, -1};
  uint32_t t = 1000;  // the clock does not start at zero on the device either
  int pressed = first_pressed;
  uint32_t press_start = t;
  for (int i = 0; i < n; i++) {
    if (pressed) {
      press_start = t;
    }
    for (int ms = 0; ms < segs[i]; ms += POLL_MS) {
      t += POLL_MS;
      button_event_t ev = button_press_step(&b, pressed, t);
      if (ev == BUTTON_CLICK) {
        r.clicks++;
      } else if (ev == BUTTON_HOLD) {
        r.holds++;
        r.hold_at = (int) (t - press_start);
      } else if (ev == BUTTON_LONG) {
        r.longs++;
        r.long_at = (int) (t - press_start);
      }
    }
    pressed = !pressed;
  }
  return r;
}

static int failures;

static void expect(const char *name, result_t r, int clicks, int holds, int longs) {
  bool ok = r.clicks == clicks && r.holds == holds && r.longs == longs;
  printf("%s %s: clicks %d holds %d longs %d (want %d, %d, %d)\n", ok ? "ok  " : "FAIL", name,
         r.clicks, r.holds, r.longs, clicks, holds, longs);
  if (!ok) {
    failures++;
  }
}

// The event came at `want` ms into the press, or one poll after.
static void expect_at(const char *what, int at, int want) {
  if (at < want || at > want + POLL_MS) {
    printf("FAIL %s came %d ms into the press, want %d\n", what, at, want);
    failures++;
  }
}

int main(void) {
  // Released for a second after boot, then the press, then released again.
  const int click[] = {1000, 200, 1000};
  expect("short press is a click", play(0, click, 3), 1, 0, 0);

  const int almost[] = {1000, 900, 1000};
  expect("0.9 s press is still a click", play(0, almost, 3), 1, 0, 0);

  const int hold3[] = {1000, 3000, 1000};
  result_t r = play(0, hold3, 3);
  expect("3 s hold is one hold, no click", r, 0, 1, 0);
  expect_at("the hold", r.hold_at, BUTTON_HOLD_MS);

  const int almost_long[] = {1000, 4900, 1000};
  expect("4.9 s hold is still only a hold", play(0, almost_long, 3), 0, 1, 0);

  const int hold6[] = {1000, 6000, 1000};
  r = play(0, hold6, 3);
  expect("6 s hold is a hold, then a long press", r, 0, 1, 1);
  expect_at("the hold", r.hold_at, BUTTON_HOLD_MS);
  expect_at("the long press", r.long_at, BUTTON_LONG_MS);

  // The press that powered the board on, still held at boot.
  const int boot_hold[] = {3000, 1000};
  expect("power-on press held at boot", play(1, boot_hold, 2), 0, 0, 0);

  const int boot_then_click[] = {3000, 1000, 200, 1000};
  expect("click after the power-on press", play(1, boot_then_click, 4), 1, 0, 0);

  const int stuck[] = {60000};
  expect("line low from boot on", play(1, stuck, 1), 0, 0, 0);

  const int too_soon[] = {300, 6000, 1000};
  expect("press before the line was seen released long", play(0, too_soon, 3), 0, 0, 0);

  // A 50 ms bounce up in the middle of a hold: still one press.
  const int bounce[] = {1000, 600, 50, 600, 1000};
  expect("bounce inside a hold", play(0, bounce, 5), 0, 1, 0);

  const int two[] = {1000, 200, 500, 200, 1000};
  expect("two clicks", play(0, two, 5), 2, 0, 0);

  // The menu: open it, click back out of it, then power off with a long hold.
  const int scenario[] = {1000, 1500, 2000, 200, 2000, 6000, 1000};
  expect("hold, click, long hold", play(0, scenario, 7), 1, 2, 1);

  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
