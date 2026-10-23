#include "touch.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "touch_bus.h"

static const char *TAG = "touch";

// Reads the touch MCU at I2C1 0x0c (2 bytes) while a finger is on the strip, and
// logs every change with a timestamp into a ring buffer that GET /touch dumps. The
// finger stream (down, move, up) and the hold go to the two callbacks touch_start
// was given.
#define TOUCH_ADDR 0x0c
#define TOUCH_READ_TIMEOUT_MS 50
#define TOUCH_RDY_GPIO 32

// Byte 1 of a sample (docs/hardware.md, Touch strip): 0x80 while a finger is down.
// Byte 0 is the position along the strip.
#define TOUCH_DOWN 0x80
// Bits a status byte can have (0x80 down, 0x08 and 0x04 the direction). Some reads
// arrive shifted by a byte, status first; a byte with other bits set is a position.
#define TOUCH_STATUS_BITS 0x8C
#define TOUCH_HOLD_MS 800
#define TOUCH_MOVE_MIN 3  // position counts a finger must travel before it is a drag, not a rest
#define TOUCH_STACK 4096  // the input path posts to the core, which logs and allocates

static touch_rec_t s_tlog[TOUCH_LOG];
static volatile int s_thead = 0;
static volatile int s_tcount = 0;
static volatile int s_shifted;  // reads that came status first and were swapped
static volatile int s_holds;    // holds since boot; shown by GET /touch
static volatile int s_drags;    // drags since boot; shown by GET /touch
static TaskHandle_t s_task;
static touch_finger_fn s_on_finger;
static touch_hold_fn s_on_hold;

// Turns the samples into the finger stream for the core: DOWN at the first touch,
// MOVE whenever the position changes, UP on release. A finger that stays down for
// TOUCH_HOLD_MS without travelling is a hold, reported once while it is down. Returns
// whether the finger is down after this sample.
static bool touch_step(uint8_t pos, uint8_t status, uint32_t now_ms) {
  static bool down;
  static uint32_t down_at;
  static uint8_t start_pos;
  static uint8_t last_pos;
  static bool dragged;
  static bool held;
  bool is_down = (status & TOUCH_DOWN) != 0;
  if (is_down && !down) {
    down_at = now_ms;
    start_pos = pos;
    last_pos = pos;
    dragged = false;
    held = false;
    s_on_finger(TOUCH_PHASE_DOWN, pos);
  } else if (is_down) {
    last_pos = pos;
    s_on_finger(TOUCH_PHASE_MOVE, pos);  // also tells the core the finger is still there
    int travel = pos > start_pos ? pos - start_pos : start_pos - pos;
    if (!dragged && travel >= TOUCH_MOVE_MIN) {
      dragged = true;
      s_drags++;
    }
    if (!dragged && !held && now_ms - down_at >= TOUCH_HOLD_MS) {
      held = true;
      s_holds++;
      s_on_hold();
    }
  } else if (down) {
    s_on_finger(TOUCH_PHASE_UP, last_pos);
  }
  down = is_down;
  return down;
}

static void touch_reader(void *arg) {
  // The driver logs every failed transaction and every bus recovery, five lines a read
  // on a dead bus: more than the console carries. touch_bus paces the reads and logs
  // instead; the sensors on I2C0 log their own failures.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  esp_log_level_set("i2c.common", ESP_LOG_NONE);
  i2c_master_bus_config_t bc = {
      .i2c_port = 1,
      .sda_io_num = 12,
      .scl_io_num = 14,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  i2c_master_bus_handle_t bus = NULL;
  esp_err_t made = i2c_new_master_bus(&bc, &bus);
  if (made != ESP_OK) {
    ESP_LOGE(TAG, "touch: bus init failed: %s", esp_err_to_name(made));
    vTaskDelete(NULL);
    return;
  }
  i2c_device_config_t dc = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = TOUCH_ADDR,
      .scl_speed_hz = 100000,
  };
  i2c_master_dev_handle_t dev = NULL;
  made = i2c_master_bus_add_device(bus, &dc, &dev);
  if (made != ESP_OK) {
    ESP_LOGE(TAG, "touch: add device failed: %s", esp_err_to_name(made));
    vTaskDelete(NULL);
    return;
  }
  gpio_config_t io = {
      .pin_bit_mask = 1ULL << TOUCH_RDY_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);

  touch_bus_t pace = {0};
  bool finger = false;
  int last0 = -1, last1 = -1, lastr = -1;
  int64_t t0 = esp_timer_get_time();
  esp_task_wdt_add(NULL);
  while (true) {
    esp_task_wdt_reset();
    // The strip holds GPIO32 low while a finger is on it, and is read only then and
    // until a read shows the release, as the stock firmware does. An idle strip may
    // leave its address unanswered for minutes.
    int rdy = gpio_get_level(TOUCH_RDY_GPIO);
    if (rdy != 0 && !finger) {
      // The next touch starts at the full read rate, whatever the last one met.
      pace.failures = 0;
      pace.pause_ms = TOUCH_PERIOD_MS;
      vTaskDelay(pdMS_TO_TICKS(TOUCH_PERIOD_MS));
      continue;
    }
    uint8_t buf[2] = {0, 0};
    esp_err_t e = i2c_master_receive(dev, buf, 2, TOUCH_READ_TIMEOUT_MS);
    uint32_t now_ms = (uint32_t) ((esp_timer_get_time() - t0) / 1000);
    if (e != ESP_OK && rdy != 0) {
      // GPIO32 already says the finger is gone, and the strip may not answer once
      // idle: the release is taken from the line.
      finger = touch_step(0, 0, now_ms);
      vTaskDelay(pdMS_TO_TICKS(TOUCH_PERIOD_MS));
      continue;
    }
    touch_bus_next_t next = touch_bus_step(&pace, e == ESP_OK, now_ms);
    if (next.recovered) {
      ESP_LOGW(TAG, "touch: answering again (%lu more reads unanswered since the last line)",
               (unsigned long) next.failed);
    }
    if (e != ESP_OK) {
      // With a finger on it the strip answers every read; a long silence is a stuck
      // bus, which only a reset (SCL clocked out) frees.
      esp_err_t reset = next.reset_bus ? i2c_master_bus_reset(bus) : ESP_OK;
      if (next.log) {
        ESP_LOGW(TAG, "touch: %lu reads unanswered (%s), next in %lu ms%s",
                 (unsigned long) next.failed, esp_err_to_name(e), (unsigned long) next.pause_ms,
                 next.reset_bus ? (reset == ESP_OK ? ", bus reset" : ", bus reset failed") : "");
      }
      vTaskDelay(pdMS_TO_TICKS(next.pause_ms));
      continue;
    }
    bool status_first = (buf[0] & ~TOUCH_STATUS_BITS) == 0 && (buf[1] & ~TOUCH_STATUS_BITS) != 0;
    if (status_first) {
      uint8_t t = buf[0];
      buf[0] = buf[1];
      buf[1] = t;
      s_shifted++;
    }
    finger = touch_step(buf[0], buf[1], now_ms);
    if (buf[0] != last0 || buf[1] != last1 || rdy != lastr) {
      int i = s_thead;
      s_tlog[i].ms = now_ms;
      s_tlog[i].b0 = buf[0];
      s_tlog[i].b1 = buf[1];
      s_tlog[i].rdy = (uint8_t) rdy;
      s_thead = (i + 1) % TOUCH_LOG;
      s_tcount++;
      last0 = buf[0];
      last1 = buf[1];
      lastr = rdy;
    }
    vTaskDelay(pdMS_TO_TICKS(next.pause_ms));
  }
}

void touch_start(touch_finger_fn on_finger, touch_hold_fn on_hold) {
  s_on_finger = on_finger;
  s_on_hold = on_hold;
  xTaskCreate(touch_reader, "touch", TOUCH_STACK, NULL, 3, &s_task);
}

void touch_counts(touch_counts_t *out) {
  out->drags = s_drags;
  out->holds = s_holds;
  out->shifted = s_shifted;
  out->stack_free_min = (unsigned) uxTaskGetStackHighWaterMark(s_task);
}

const touch_rec_t *touch_log(int *start, int *n) {
  *n = s_tcount < TOUCH_LOG ? s_tcount : TOUCH_LOG;
  *start = s_tcount < TOUCH_LOG ? 0 : s_thead;
  return s_tlog;
}
