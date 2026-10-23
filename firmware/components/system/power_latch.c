#include "power_latch.h"

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_log.h"

#define POWER_LATCH_GPIO 23

static const char *TAG = "cgdn1";

// Runs before app_main. Raw writes keep it independent of driver init order:
// drive high first so enabling the output cannot glitch the latch low.
static void __attribute__((constructor)) power_latch_early(void) {
  volatile uint32_t *const gpio_out_w1ts = (volatile uint32_t *) 0x3FF44008;
  volatile uint32_t *const gpio_enable_w1ts = (volatile uint32_t *) 0x3FF44024;
  *gpio_out_w1ts = 1u << POWER_LATCH_GPIO;
  *gpio_enable_w1ts = 1u << POWER_LATCH_GPIO;
}

void power_latch_hold(void) {
  gpio_set_level(POWER_LATCH_GPIO, 1);
  gpio_config_t io = {
      .pin_bit_mask = 1ULL << POWER_LATCH_GPIO,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);
  gpio_set_level(POWER_LATCH_GPIO, 1);
  if (gpio_hold_en(POWER_LATCH_GPIO) != ESP_OK) {
    ESP_LOGE(TAG, "gpio_hold_en(%d) failed", POWER_LATCH_GPIO);
  }
}

void power_latch_release(void) {
  gpio_hold_dis(POWER_LATCH_GPIO);
  gpio_set_level(POWER_LATCH_GPIO, 0);
}
