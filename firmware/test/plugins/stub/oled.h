// Host stand-in for the panel: text.c calls oled_pixel, the harness owns the frame.
#pragma once
#include <stdint.h>
#define OLED_W 160
#define OLED_H 128
void oled_pixel(int x, int y, uint8_t level);
