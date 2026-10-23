#include "touch_bus.h"

touch_bus_next_t touch_bus_step(touch_bus_t *b, bool ok, uint32_t now_ms) {
  touch_bus_next_t next = {.pause_ms = TOUCH_PERIOD_MS};
  if (ok) {
    next.recovered = b->failures > TOUCH_BACKOFF_AFTER;
    next.failed = b->unlogged;
    b->failures = 0;
    b->unlogged = 0;
    b->pause_ms = TOUCH_PERIOD_MS;
    return next;
  }
  b->failures++;
  b->unlogged++;
  if (b->failures <= TOUCH_BACKOFF_AFTER) {
    b->pause_ms = TOUCH_PERIOD_MS;
  } else {
    uint32_t doubled = b->pause_ms * 2;
    b->pause_ms = doubled < TOUCH_PAUSE_MAX_MS ? doubled : TOUCH_PAUSE_MAX_MS;
  }
  next.pause_ms = b->pause_ms;
  uint32_t past_first = b->failures - TOUCH_RESET_AFTER;
  next.reset_bus = b->failures >= TOUCH_RESET_AFTER && past_first % TOUCH_RESET_EVERY == 0;
  bool silent = b->failures > TOUCH_BACKOFF_AFTER;
  if (silent && (!b->have_logged || now_ms - b->logged_at >= TOUCH_LOG_EVERY_MS)) {
    next.log = true;
    next.failed = b->unlogged;
    b->unlogged = 0;
    b->logged_at = now_ms;
    b->have_logged = true;
  }
  return next;
}
