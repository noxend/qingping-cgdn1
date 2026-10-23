#include "session.h"

#include <time.h>
#include "esp_timer.h"

// Any time before this is the clock's power-on value, not one SNTP set.
#define CLOCK_SET_AFTER 1767225600  // 2026-01-01

int64_t session_now(void) {
  time_t now = time(NULL);
  return now < CLOCK_SET_AFTER ? 0 : (int64_t) now;
}

int64_t session_uptime(void) {
  return esp_timer_get_time() / 1000000;
}
