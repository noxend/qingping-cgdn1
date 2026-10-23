// Plays button presses through button_press_step at the 50 ms poll of button.c and
// checks which events come out and when.
#include <stdio.h>
#include <string.h>
#include "button_press.h"

#define POLL_MS 50

typedef struct {
  int clicks;
  int longs;
  int long_at;  // ms into the press when the long press came, or -1
} result_t;

// segs: alternating line states starting with `first`, each lasting the given ms.
static result_t play(int first_pressed, const int *segs, int n) {
  button_press_t b;
  memset(&b, 0, sizeof(b));
  result_t r = {0, 0, -1};
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

static void expect(const char *name, result_t r, int clicks, int longs) {
  bool ok = r.clicks == clicks && r.longs == longs;
  printf("%s %s: clicks %d longs %d (want %d, %d)\n", ok ? "ok  " : "FAIL", name, r.clicks, r.longs,
         clicks, longs);
  if (!ok) {
    failures++;
  }
}

int main(void) {
  // Released for a second after boot, then the press, then released again.
  const int click[] = {1000, 200, 1000};
  expect("short press is a click", play(0, click, 3), 1, 0);

  const int hold3[] = {1000, 3000, 1000};
  result_t r = play(0, hold3, 3);
  expect("3 s hold is one long press, no click", r, 0, 1);
  if (r.long_at < BUTTON_LONG_MS || r.long_at > BUTTON_LONG_MS + POLL_MS) {
    printf("FAIL long press came %d ms into the hold, want %d\n", r.long_at, BUTTON_LONG_MS);
    failures++;
  }

  const int almost[] = {1000, 1900, 1000};
  expect("1.9 s hold is still a click", play(0, almost, 3), 1, 0);

  // The press that powered the board on, still held at boot.
  const int boot_hold[] = {3000, 1000};
  expect("power-on press held at boot", play(1, boot_hold, 2), 0, 0);

  const int boot_then_click[] = {3000, 1000, 200, 1000};
  expect("click after the power-on press", play(1, boot_then_click, 4), 1, 0);

  const int stuck[] = {60000};
  expect("line low from boot on", play(1, stuck, 1), 0, 0);

  const int too_soon[] = {300, 3000, 1000};
  expect("press before the line was seen released long", play(0, too_soon, 3), 0, 0);

  // A 50 ms bounce up in the middle of a long hold.
  const int bounce[] = {1000, 1200, 50, 1200, 1000};
  expect("bounce inside a hold", play(0, bounce, 5), 0, 1);

  const int two[] = {1000, 200, 500, 200, 1000};
  expect("two clicks", play(0, two, 5), 2, 0);

  const int scenario[] = {1000, 200, 5000, 3000, 5000, 200, 1000};
  expect("user's test: click, hold 3 s, click", play(0, scenario, 7), 2, 1);

  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
