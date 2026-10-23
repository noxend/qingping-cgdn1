#include "button.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "button_press.h"

#define BUTTON_GPIO GPIO_NUM_36
#define POLL_MS 50
#define TASK_STACK 3072  // the task logs, and the power-off path runs on it

static const char *TAG = "button";

static button_fn s_on_click;
static button_fn s_on_hold;
static button_fn s_on_long;
static volatile bool s_armed;
static volatile int s_clicks;
static volatile int s_holds;
static volatile int s_longs;

// Low while the button is held: the stock firmware's button read is
// gpio_get_level(36) == 0 (docs/hardware.md).
static bool pressed_now(void) {
  return gpio_get_level(BUTTON_GPIO) == 0;
}

static uint32_t now_ms(void) {
  return (uint32_t) (esp_timer_get_time() / 1000);
}

// The line is only read, never interrupted on: GPIO36 shares the ADC and RF power
// domain and IDF warns against its interrupt with Wi-Fi power save on (gpio.h,
// ESP32 ECO 3.11), and a poll every 50 ms resolves a press as well as an edge did.
static void button_task(void *arg) {
  button_press_t press = {0};
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    button_event_t ev = button_press_step(&press, pressed_now(), now_ms());
    s_armed = press.armed;
    if (ev == BUTTON_CLICK) {
      s_clicks++;
      ESP_LOGI(TAG, "click");
      s_on_click();
    } else if (ev == BUTTON_HOLD) {
      s_holds++;
      ESP_LOGI(TAG, "hold");
      s_on_hold();
    } else if (ev == BUTTON_LONG) {
      s_longs++;
      ESP_LOGI(TAG, "long press");
      s_on_long();
    }
  }
}

void button_start(button_fn on_click, button_fn on_hold, button_fn on_long) {
  s_on_click = on_click;
  s_on_hold = on_hold;
  s_on_long = on_long;
  gpio_config_t io = {
      .pin_bit_mask = 1ULL << BUTTON_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,  // 34-39 have no pull; the power MCU drives it
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);
  xTaskCreate(button_task, "button", TASK_STACK, NULL, 3, NULL);
}

bool button_pressed(void) {
  return pressed_now();
}

void button_counts(button_counts_t *out) {
  out->armed = s_armed;
  out->level = gpio_get_level(BUTTON_GPIO);
  out->clicks = s_clicks;
  out->holds = s_holds;
  out->longs = s_longs;
}
