// Text rendering on the panel, backed by a vendored subset of u8g2. u8g2 draws
// into its own 1-bit buffer; text_blit() copies the lit pixels into the shared
// framebuffer (oled_*) at a grey level. Fonts come straight from u8g2 (UTF-8,
// Cyrillic). This is the device's shared text layer; plugins draw through it.
#pragma once

#include <stdint.h>
#include "u8g2.h"

// Sets up the in-memory 160x128 u8g2 device. Call once before any draw.
void text_init(void);

// Clears the text buffer before a new batch of draws.
void text_begin(void);

void text_font(const uint8_t *font);
void text_draw(int x, int y, const char *s);  // top-left anchored, UTF-8
void text_draw_centered(int y, const char *s);
int text_width(const char *s);

// Copies every lit pixel of the current text buffer into the framebuffer at the
// given grey level (0..15). Draw a batch, blit it, repeat for another level.
void text_blit(uint8_t level);

// A one-line message alone on the panel, flushed at once ("Starting", "Powering
// off"): the text layer and the framebuffer must be the caller's at that moment.
// In text_message.c: the host builds take text.c without the panel's oled_flush.
void text_show_message(const char *s);
