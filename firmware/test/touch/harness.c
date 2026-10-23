// Plays a touch strip through touch_bus_step as touch.c reads it: a read that gets no
// answer costs its 50 ms timeout, then the reader sleeps for the pause it was given.
// The 2026-10-23 board whose strip stopped answering logged five lines every 30 ms
// until the console starved CPU 0; these cases hold the reader to a quiet, slow retry.
#include <stdio.h>
#include <string.h>
#include "touch_bus.h"

#define READ_TIMEOUT_MS 50

typedef struct {
  int reads;
  int reads_last_minute;  // reads in the run's last 60 s
  int logs;
  int resets;
  int recovered;
  uint32_t min_pause;
  uint32_t max_pause;
  uint32_t first_pause_answered;  // the pause after the first answer that follows silence
} run_t;

// The strip is silent from silent_from to silent_to (ms since boot), answering otherwise.
static run_t play(uint32_t run_ms, uint32_t silent_from, uint32_t silent_to) {
  touch_bus_t b;
  memset(&b, 0, sizeof(b));
  run_t r = {.min_pause = UINT32_MAX};
  bool was_silent = false;
  for (uint32_t t = 0; t < run_ms;) {
    bool ok = t < silent_from || t >= silent_to;
    t += ok ? 1 : READ_TIMEOUT_MS;
    touch_bus_next_t n = touch_bus_step(&b, ok, t);
    r.reads++;
    if (t + 60000 >= run_ms) {
      r.reads_last_minute++;
    }
    r.logs += n.log;
    r.resets += n.reset_bus;
    r.recovered += n.recovered;
    r.min_pause = n.pause_ms < r.min_pause ? n.pause_ms : r.min_pause;
    r.max_pause = n.pause_ms > r.max_pause ? n.pause_ms : r.max_pause;
    if (ok && was_silent && !r.first_pause_answered) {
      r.first_pause_answered = n.pause_ms;
    }
    was_silent = !ok;
    t += n.pause_ms;
  }
  return r;
}

static int failures;

static void expect(bool ok, const char *what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

int main(void) {
  // A strip dead from boot, for ten minutes.
  run_t dead = play(600000, 0, UINT32_MAX);
  printf("     dead strip, 10 min: %d reads (%d in the last minute), %d log lines, %d resets,"
         " pauses %u to %u ms\n",
         dead.reads, dead.reads_last_minute, dead.logs, dead.resets, dead.min_pause,
         dead.max_pause);
  expect(dead.min_pause >= 10, "the reader sleeps a tick or more after every read: it yields");
  expect(dead.reads_last_minute <= 60, "a dead strip is read once a second or less");
  expect(dead.max_pause <= 5000, "and still at least every few seconds");
  expect(dead.logs <= 600000 / 10000 + 1, "one log line per 10 s at most");
  expect(dead.resets >= 1 && dead.resets <= dead.reads / 10,
         "the bus is reset, but not every read");

  // A live strip.
  run_t live = play(60000, UINT32_MAX, UINT32_MAX);
  expect(live.logs == 0 && live.resets == 0, "a live strip logs nothing and resets nothing");
  expect(live.min_pause == TOUCH_PERIOD_MS && live.max_pause == TOUCH_PERIOD_MS,
         "and is read every 12 ms");

  // A few unanswered reads on a live strip.
  run_t blip = play(60000, 10000, 10000 + 3 * READ_TIMEOUT_MS);
  expect(blip.max_pause == TOUCH_PERIOD_MS && blip.logs == 0 && blip.recovered == 0,
         "a few lost reads neither slow the reader nor log");

  // Silent for a minute, then answering again.
  run_t back = play(120000, 0, 60000);
  expect(back.first_pause_answered == TOUCH_PERIOD_MS,
         "the first answer after a silence brings the 12 ms period back");
  expect(back.reads_last_minute > 4000, "and the strip is read at full rate again");
  expect(back.recovered == 1, "and one line says it answers again");

  printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
