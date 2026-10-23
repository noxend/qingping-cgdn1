// Keeps what the last crash looked like across the reboot it causes: the panic
// handler is wrapped (-Wl,--wrap=esp_panic_handler) and writes the reason, the
// task, the exception registers and a short backtrace to RTC memory, which a
// software or watchdog reset does not clear (a power loss does). With no serial
// cable on the sealed device, GET /status is the only way to see a crash.
//
// Around it, for a reset that leaves no record: how far the panic handler got,
// which tasks the task watchdog listed before it fired, the chip's raw reset code
// and the reset hint IDF's panic path leaves behind.
#pragma once

// Formats the last crash as a JSON object into out, or "null" when none is kept.
int crash_log_json(char *out, int cap);

// Appends the reset and watchdog diagnostics as ",key":value pairs for an open JSON
// object: rst_raw (the ROM reset code of CPU0, 16 is the panic handler's own RTC
// watchdog, 12 a software reset), rst_hint (esp_reset_reason_t IDF's panic path set,
// 0 for none), twdt (the starved tasks at the last task watchdog timeout, or null)
// and panic (the last panic's stage, or null).
int crash_log_diag_json(char *out, int cap);
