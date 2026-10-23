#include "led.h"

#include "driver/ledc.h"

// Stock drives the status LED as three ledc PWM channels on GPIO25/21/19
// (from the RGB_LED init in the stock dump).
static const int LED_PIN[3] = {25, 21, 19};
static const ledc_channel_t LED_CH[3] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};

void led_init(void) {
  ledc_timer_config_t t = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 25000,  // above the audible range
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&t);
  for (int i = 0; i < 3; i++) {
    ledc_channel_config_t c = {
        .gpio_num = LED_PIN[i],
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LED_CH[i],
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&c);
  }
}

// The colour asked for last and the scale applied to it, so a scale change can
// re-apply the colour without waiting for the next led_set.
static uint8_t s_colour[3];
static uint8_t s_scale_pct = 100;

static void apply(void) {
  for (int i = 0; i < 3; i++) {
    uint32_t duty = (uint32_t) s_colour[i] * s_scale_pct / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_CH[i], duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_CH[i]);
  }
}

void led_set(uint8_t r, uint8_t g, uint8_t b) {
  s_colour[0] = r;
  s_colour[1] = g;
  s_colour[2] = b;
  apply();
}

void led_set_scale(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  s_scale_pct = percent;
  apply();
}

// hue 0..255 around the colour wheel, full saturation and value.
static void hue_to_rgb(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (h < 85) {
    *r = 255 - h * 3;
    *g = h * 3;
    *b = 0;
  } else if (h < 170) {
    h -= 85;
    *r = 0;
    *g = 255 - h * 3;
    *b = h * 3;
  } else {
    h -= 170;
    *r = h * 3;
    *g = 0;
    *b = 255 - h * 3;
  }
}

void led_set_hue(uint8_t h) {
  uint8_t r, g, b;
  hue_to_rgb(h, &r, &g, &b);
  led_set(r, g, b);
}

void led_show(bool on, uint8_t hue) {
  if (on) {
    led_set_hue(hue);
  } else {
    led_set(0, 0, 0);
  }
}
