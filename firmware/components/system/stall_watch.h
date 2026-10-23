// Watches for moments when the whole system stops for a while (flash writes with the
// cache off, a long critical section, a starved core): a task above every other one
// of ours that wakes every 20 ms and records how late it was. Shown in GET /status.
#pragma once

void stall_watch_start(void);

// Formats {"max_ms":..,"at_s":..,"n200":..,"n1000":..} into out; returns its length.
int stall_watch_json(char *out, int cap);
