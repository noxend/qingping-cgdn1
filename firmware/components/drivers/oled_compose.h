// The step between the logical framebuffer and the bytes streamed to the panel:
// orientation, the burn-in orbit offset and the brightness level table, applied
// per pixel. Pure C with no ESP-IDF include, so the host test can compile it.
#pragma once

#include <stdint.h>

// Logical frame: 160 x 128, 4-bit grey, two pixels per byte, low nibble = even x.
#define OLED_COMPOSE_W 160
#define OLED_COMPOSE_H 128
// One streamed panel column carries the 160 logical pixels of one frame row,
// two per byte, low nibble first.
#define OLED_COMPOSE_COL_BYTES (OLED_COMPOSE_W / 2)
#define OLED_COMPOSE_LEVELS 16

// How far the orbit moves the picture from its drawn position, in pixels.
#define OLED_ORBIT_AMPLITUDE 2
// Positions on the orbit: the perimeter of the square of that amplitude.
#define OLED_ORBIT_STEPS (8 * OLED_ORBIT_AMPLITUDE)

typedef struct {
  // Orientation: bit 0 mirrors x (the 160 panel rows), bit 1 mirrors y (the 128
  // streamed columns).
  uint8_t flip;
  // The picture is drawn dx pixels to the right and dy pixels down from where
  // the plugin put it; the pixels uncovered at the opposite edge are off.
  int8_t dx;
  int8_t dy;
  // Output level for each logical level 0..15.
  uint8_t level[OLED_COMPOSE_LEVELS];
} oled_compose_t;

// The identity: upright, no offset, levels as drawn.
void oled_compose_init(oled_compose_t *c);

// Fills `out` (OLED_COMPOSE_COL_BYTES) with panel column `col` (0..127) of the
// frame `fb`, composed as `c` says.
void oled_compose_column(const uint8_t *fb, int col, const oled_compose_t *c, uint8_t *out);

// Panel B (lcd_id != 1) takes 1 bit per pixel, page addressed: 20 pages of 128
// bytes, each byte 8 stacked panel rows with bit 0 on top, page p holding rows
// 8p..8p+7.
#define OLED_PAGES 20
#define OLED_PAGE_BYTES 128

// Sets the bits of streamed column `col` (0..127) in `pages` (OLED_PAGES *
// OLED_PAGE_BYTES) from that column as composed for panel A (`line`, from
// oled_compose_column); a pixel at any level above 0 is lit. Panel B holds the
// column at byte 127 - col of each page: the stock firmware fills both panels from
// one buffer and streams it to panel A last column first.
void oled_compose_pages(const uint8_t *line, int col, uint8_t *pages);

// The orbit offset at position `step` (0..OLED_ORBIT_STEPS-1): a walk around the
// square's perimeter, one pixel per step, so consecutive positions differ by one
// pixel and the walk closes on itself.
void oled_orbit_offset(int step, int8_t *dx, int8_t *dy);
