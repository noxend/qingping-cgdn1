// The SDK's gfx_blit on top of its gfx_pixel, so the host tests draw it with the same
// code as the panel.
#include "plugin_sdk.h"

void gfx_blit(int x, int y, int w, int h, const void *pixels, int bpp, const uint8_t *palette) {
  const uint8_t *rows = pixels;
  int stride = (w * bpp + 7) / 8;
  int mask = (1 << bpp) - 1;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = rows + r * stride;
    for (int c = 0; c < w; c++) {
      int bit = c * bpp;
      int value = (row[bit / 8] >> (8 - bpp - bit % 8)) & mask;
      if (palette[value] != GFX_KEEP) {
        gfx_pixel(x + c, y + r, palette[value] > 15 ? 15 : palette[value]);
      }
    }
  }
}
