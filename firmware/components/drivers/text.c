#include "text.h"

#include "oled.h"

#define FB_W OLED_W  // 160
#define FB_H OLED_H  // 128
#define TILE_W (FB_W / 8)
#define TILE_H (FB_H / 8)

static u8g2_t s_u8g2;
static uint8_t s_buf[FB_W * TILE_H];  // u8g2 full buffer: one byte per (x, 8-row tile)

// A display with our resolution and no hardware. u8g2 only reads the geometry
// from here; the timing fields are irrelevant without a real bus.
static const u8x8_display_info_t FB_INFO = {
    .tile_width = TILE_W,
    .tile_height = TILE_H,
    .pixel_width = FB_W,
    .pixel_height = FB_H,
};

static uint8_t fb_display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void) arg_int;
  (void) arg_ptr;
  if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) {
    u8x8_d_helper_display_setup_memory(u8x8, &FB_INFO);
  }
  return 1;  // succeed for every message; nothing is sent to hardware
}

void text_init(void) {
  u8g2_SetupDisplay(&s_u8g2, fb_display_cb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
  u8g2_SetupBuffer(&s_u8g2, s_buf, TILE_H, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8g2_SetFontMode(&s_u8g2, 1);
  u8g2_SetFontPosTop(&s_u8g2);
}

void text_begin(void) {
  u8g2_ClearBuffer(&s_u8g2);
}

void text_font(const uint8_t *font) {
  u8g2_SetFont(&s_u8g2, font);
}

void text_draw(int x, int y, const char *s) {
  u8g2_DrawUTF8(&s_u8g2, x, y, s);
}

int text_width(const char *s) {
  return (int) u8g2_GetUTF8Width(&s_u8g2, s);
}

void text_draw_centered(int y, const char *s) {
  text_draw((FB_W - text_width(s)) / 2, y, s);
}

void text_blit(uint8_t level) {
  const uint8_t *buf = u8g2_GetBufferPtr(&s_u8g2);
  for (int ty = 0; ty < TILE_H; ty++) {
    for (int x = 0; x < FB_W; x++) {
      uint8_t col = buf[ty * FB_W + x];
      if (!col) {
        continue;
      }
      for (int bit = 0; bit < 8; bit++) {
        if (col & (1 << bit)) {
          oled_pixel(x, ty * 8 + bit, level);
        }
      }
    }
  }
}
