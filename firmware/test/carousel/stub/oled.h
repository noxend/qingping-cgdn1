// Host stand-in for the panel: frames are plain buffers the harness owns.
#pragma once
#include <stdint.h>
#define OLED_W 160
#define OLED_H 128
#define OLED_FB_BYTES (OLED_H * (OLED_W / 2))
void oled_copy_framebuffer(uint8_t *out);
void oled_show_frame(const uint8_t *fb);
uint8_t *oled_frame_begin(void);
void oled_frame_end(void);
