// Status RGB LED: three ledc PWM channels, driven by whichever screen is shown.
#pragma once

#include <stdbool.h>
#include <stdint.h>

void led_init(void);
void led_set(uint8_t r, uint8_t g, uint8_t b);
// Scales every colour set from now on, and the one lit now, to `percent`
// (0..100) of its value; the night profile dims the indicator with it.
void led_set_scale(uint8_t percent);
// hue 0..255 around the colour wheel, full saturation and value.
void led_set_hue(uint8_t h);

// ---- For the test hooks only ----------------------------------------------------
// Nothing in this firmware calls these; the test endpoints of the private cgdn1-dev
// repository do (see api/test_hooks.h).
// Colour of `hue` when on, dark when off.
void led_show(bool on, uint8_t hue);
