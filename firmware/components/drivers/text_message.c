#include "oled.h"
#include "text.h"

extern const uint8_t u8g2_font_9x15_t_cyrillic[];

void text_show_message(const char *s) {
  text_begin();
  text_font(u8g2_font_9x15_t_cyrillic);
  text_draw_centered(OLED_H / 2 - 8, s);
  oled_clear();
  text_blit(15);
  oled_flush();
}
