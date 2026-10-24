#include "oled_compose.h"

#include <string.h>

void oled_compose_init(oled_compose_t *c) {
  memset(c, 0, sizeof(*c));
  for (int i = 0; i < OLED_COMPOSE_LEVELS; i++) {
    c->level[i] = (uint8_t) i;
  }
}

// The pixel at x of a frame row, or 0 outside the row.
static uint8_t pixel_at(const uint8_t *row, int x) {
  if (x < 0 || x >= OLED_COMPOSE_W) {
    return 0;
  }
  uint8_t byte = row[x / 2];
  if (x & 1) {
    return byte >> 4;
  }
  return byte & 0x0F;
}

void oled_compose_column(const uint8_t *fb, int col, const oled_compose_t *c, uint8_t *out) {
  // The frame row this column shows, before the orbit moves the picture.
  int y = (c->flip & 2) ? OLED_COMPOSE_H - 1 - col : col;
  int source_y = y - c->dy;
  if (source_y < 0 || source_y >= OLED_COMPOSE_H) {
    // The orbit uncovered this row: nothing to show.
    memset(out, 0, OLED_COMPOSE_COL_BYTES);
    return;
  }
  const uint8_t *row = fb + source_y * OLED_COMPOSE_COL_BYTES;
  for (int i = 0; i < OLED_COMPOSE_COL_BYTES; i++) {
    uint8_t packed = 0;
    for (int half = 0; half < 2; half++) {
      int panel_x = 2 * i + half;
      int x = (c->flip & 1) ? OLED_COMPOSE_W - 1 - panel_x : panel_x;
      uint8_t level = c->level[pixel_at(row, x - c->dx)];
      packed |= (uint8_t) (level << (half * 4));
    }
    out[i] = packed;
  }
}

void oled_compose_pages(const uint8_t *line, int col, uint8_t *pages) {
  int byte = OLED_PAGE_BYTES - 1 - col;
  for (int page = 0; page < OLED_PAGES; page++) {
    uint8_t bits = 0;
    for (int bit = 0; bit < 8; bit++) {
      int row = page * 8 + bit;
      uint8_t level = (line[row / 2] >> ((row & 1) * 4)) & 0x0F;
      if (level) {
        bits |= (uint8_t) (1 << bit);
      }
    }
    pages[page * OLED_PAGE_BYTES + byte] = bits;
  }
}

void oled_orbit_offset(int step, int8_t *dx, int8_t *dy) {
  const int a = OLED_ORBIT_AMPLITUDE;
  const int side = 2 * a;
  int k = step % OLED_ORBIT_STEPS;
  if (k < side) {
    // Top edge, left to right.
    *dx = (int8_t) (-a + k);
    *dy = (int8_t) (-a);
    return;
  }
  k -= side;
  if (k < side) {
    // Right edge, top to bottom.
    *dx = (int8_t) a;
    *dy = (int8_t) (-a + k);
    return;
  }
  k -= side;
  if (k < side) {
    // Bottom edge, right to left.
    *dx = (int8_t) (a - k);
    *dy = (int8_t) a;
    return;
  }
  k -= side;
  // Left edge, bottom to top.
  *dx = (int8_t) (-a);
  *dy = (int8_t) (a - k);
}
