// SPI OLED of the CGDN1. Pins, init streams and frame formats are in
// docs/hardware.md (Display).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Landscape, as the device stands. The stock firmware streams only 127 of the
// 128 columns and leaves power-on garbage in the last one; we send all 128.
#define OLED_W 160
#define OLED_H 128

// The two panels the stock firmware drives, picked by the factory record's lcd_id:
// A for lcd_id 1 (4 bit grey, column addressed, 8 MHz), B for any other (1 bit,
// page addressed, 2 MHz). On panel B every level above 0 is fully lit.
typedef enum {
  OLED_PANEL_A,
  OLED_PANEL_B,
} oled_panel_t;

esp_err_t oled_init(oled_panel_t panel);
// The panel oled_init was given.
oled_panel_t oled_panel(void);
void oled_clear(void);
// level 0 (off) .. 15 (full).
void oled_pixel(int x, int y, uint8_t level);
void oled_flush(void);

// Burn-in protection, applied when a frame is streamed; the framebuffer and the
// web mirror keep the picture as drawn.
// The brightness: output level for each of the 16 logical levels, on the panel
// at once. All zero turns the picture off.
void oled_set_levels(const uint8_t levels[16]);
// Moves the picture one pixel along the orbit (a 2 px square walk) and streams
// it.
void oled_orbit_step(void);

// Size of the framebuffer: OLED_H rows of OLED_W/2 bytes, two 4-bit pixels per
// byte, low nibble = even x. Logical orientation (before flip).
#define OLED_FB_BYTES (OLED_H * (OLED_W / 2))
// Copies the framebuffer for a web preview. Taken under the flush lock; a draw
// in progress can still show half done, which a preview polled every few
// seconds tolerates.
void oled_copy_framebuffer(uint8_t *out);

// Puts a finished frame (OLED_FB_BYTES, same packing) on the panel. Call from the UI
// loop only.
void oled_show_frame(const uint8_t *fb);

// Lends the live framebuffer for writing a frame in place, under the flush lock;
// oled_frame_end puts it on the panel and returns the lock. For the page carousel,
// which composes straight into it instead of keeping a 10 KB frame of its own.
// NULL when the panel is not up. UI loop only.
uint8_t *oled_frame_begin(void);
void oled_frame_end(void);

// Off-screen rendering: redirect drawing to `buf` (OLED_FB_BYTES), draw a screen,
// then restore the live framebuffer. The panel is not touched meanwhile. Only
// call from the UI loop task, which owns drawing.
void oled_render_to(uint8_t *buf);
void oled_render_done(void);

// ---- For the test hooks only ----------------------------------------------------
// Nothing in this firmware calls these; the test endpoints of the private cgdn1-dev
// repository do (see api/test_hooks.h).
// Raw controller commands (DC = 0). The panel takes some of these as
// one-byte commands even after a 0x81, so a stray value can shift or flip the
// picture; oled_reset() is the way back.
void oled_command(const uint8_t *bytes, size_t len);
// Hardware reset, the verified init stream, then redraw the framebuffer.
void oled_reset(void);
// Orientation 0..3 (bit 0 mirrors x, bit 1 mirrors y), applied immediately.
void oled_set_flip(uint8_t flip);
// Calibration picture for finding which part of the panel RAM the glass shows:
// frame on the outermost pixels, ticks every 10 px (long every 50), a centre
// cross and solid corner marks of different sizes. Research probe.
void oled_test_pattern(void);
