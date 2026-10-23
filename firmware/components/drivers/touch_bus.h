// How the touch reader paces itself on the strip's I2C bus (I2C1, the touch MCU at
// 0x0c): the pause before the next read, when to reset the bus, when to log. A strip
// that stopped answering must cost almost nothing: the reads back off to one every
// TOUCH_PAUSE_MAX_MS, the bus is reset now and then rather than on every read, and one
// line is logged every TOUCH_LOG_EVERY_MS at most, and one when it answers again. A few
// lost reads on a live strip are neither slowed nor logged. No hardware here, so the
// host test runs it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The read period while the strip answers: at least one FreeRTOS tick (10 ms here), or
// vTaskDelay would not let the idle task run.
#define TOUCH_PERIOD_MS 12
// Failed reads in a row before the pauses grow: a few NACKs happen on a live strip.
#define TOUCH_BACKOFF_AFTER 8
#define TOUCH_PAUSE_MAX_MS 2000
// Failed reads in a row before the first bus reset, then between resets.
#define TOUCH_RESET_AFTER 8
#define TOUCH_RESET_EVERY 30
#define TOUCH_LOG_EVERY_MS 10000

typedef struct {
  uint32_t failures;  // reads in a row that got no answer
  uint32_t pause_ms;
  uint32_t unlogged;  // failed reads since the last line
  uint32_t logged_at;
  bool have_logged;
} touch_bus_t;

typedef struct {
  uint32_t pause_ms;  // before the next read; TOUCH_PERIOD_MS at least
  bool reset_bus;
  bool log;         // log a line now about the failed reads
  bool recovered;   // the strip answers again after a silence that was logged
  uint32_t failed;  // with log or recovered: failed reads since the last line
} touch_bus_next_t;

// One read's outcome: ok when the strip answered, now_ms a running clock. Start from a
// zeroed touch_bus_t.
touch_bus_next_t touch_bus_step(touch_bus_t *b, bool ok, uint32_t now_ms);
