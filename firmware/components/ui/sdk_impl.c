// The plugin SDK's drawing, settings and sensor calls, on top of the core's
// modules. Plugins never see oled.h, text.h, settings.h or sensors.h; this file is
// the one place where the SDK meets them.
#include "plugin_sdk.h"

#include <string.h>
#include "oled.h"
#include "screen.h"
#include "sensors.h"
#include "settings.h"
#include "text.h"

_Static_assert(GFX_W == OLED_W && GFX_H == OLED_H, "the SDK frame is the panel");

extern const uint8_t u8g2_font_logisoso42_tn[];
extern const uint8_t u8g2_font_9x15_t_cyrillic[];

void gfx_clear(void) {
  oled_clear();
}

void gfx_pixel(int x, int y, uint8_t level) {
  if (x >= 0 && x < OLED_W && y >= 0 && y < OLED_H) {
    oled_pixel(x, y, level);
  }
}

void gfx_text_begin(void) {
  text_begin();
}

void gfx_font(gfx_font_t font) {
  text_font(font == GFX_FONT_DIGITS ? u8g2_font_logisoso42_tn : u8g2_font_9x15_t_cyrillic);
}

void gfx_text(int x, int y, const char *s) {
  text_draw(x, y, s);
}

void gfx_text_centered(int y, const char *s) {
  text_draw_centered(y, s);
}

int gfx_text_width(const char *s) {
  return text_width(s);
}

void gfx_text_blit(uint8_t level) {
  text_blit(level);
}

void plugin_setting_get(const char *key, char *out, size_t cap, const char *def) {
  const char *id = screen_manager_current_id();
  if (!id) {
    // Not inside a UI loop callback (a stats or parse callback on another task, or
    // outside any callback): there is no caller to key by, so the default, no NVS.
    if (cap > 0) {
      out[0] = '\0';
      if (def) {
        strlcpy(out, def, cap);
      }
    }
    return;
  }
  settings_get(id, key, out, cap, def);
}

bool plugin_sensor_get(sensor_id_t id, sensor_reading_t *out) {
  return sensor_get(id, out);
}
