#include "oled.h"

#include <string.h>
#include "oled_compose.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define PIN_MOSI 4
#define PIN_SCLK 5
#define PIN_DC 2
#define PIN_CS 15
#define PIN_RST 22
#define SPI_HZ (8 * 1000 * 1000)

// Native GRAM: 128 columns by 160 rows. A column is 80 bytes, each byte two
// vertically adjacent pixels as nibbles (low nibble = even row). Landscape x
// runs along the 160 rows, y along the streamed columns.
#define COL_BYTES (160 / 2)

// How the frame is streamed: orientation (bit 0 mirrors x, the 160 panel rows;
// bit 1 mirrors y, the 128 panel columns; 3 turns the picture by 180 degrees;
// 0 is upright, chosen by eye on the device), the burn-in orbit offset and the
// level table. Changed under the flush lock only.
static oled_compose_t s_compose;
static int s_orbit_step;

static const char *TAG = "oled";

// Init stream for lcd_id == 1, from oled_drive_write_init_command in the
// stock firmware (verified on this unit through the ESPHome component). The
// stock sends the closing 0xAF (display on) only after the whole panel RAM has
// been cleared, so it is not part of this table.
static const uint8_t INIT_SEQ[] = {
    0xAE, 0x20, 0x00, 0x81, 0xD0, 0xAD, 0x00, 0xA0, 0xC8, 0xA8, 0x7F, 0xD3, 0x10, 0xD5,
    0x07, 0xD9, 0x40, 0x40, 0x40, 0x05, 0x98, 0x00, 0xDB, 0x2B, 0xDA, 0xC0, 0xA4, 0xA6,
};
#define CMD_DISPLAY_ON 0xAF

// NULL until init succeeds; flush and commands are no-ops before that.
static spi_device_handle_t s_dev;
// Frame writes and raw commands come from different tasks.
static SemaphoreHandle_t s_lock;
static uint8_t s_fb[OLED_H * COL_BYTES] __attribute__((aligned(4)));
// Where oled_pixel/oled_clear write. Normally the live framebuffer; a preview
// redirects it to an off-screen buffer so a screen can be rendered without
// touching the panel. Only ever swapped from the UI loop task.
static uint8_t *s_target = s_fb;

static void send(const uint8_t *data, size_t len, int dc) {
  gpio_set_level(PIN_DC, dc);
  spi_transaction_t t = {
      .length = len * 8,
      .tx_buffer = data,
  };
  spi_device_polling_transmit(s_dev, &t);
}

static void cmd(uint8_t c) {
  send(&c, 1, 0);
}

// Active-low reset pulse, as in the stock oled_drive_init.
static void panel_reset(void) {
  gpio_set_level(PIN_DC, 0);
  gpio_set_level(PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
}

// Every column goes through the compose step (orientation, orbit, levels), so
// the panel never shows the framebuffer as drawn; the web mirror does.
static void flush_locked(void) {
  static uint8_t line[COL_BYTES] __attribute__((aligned(4)));
  cmd(0x00);  // column address low nibble = 0
  cmd(0x10);  // column address high nibble = 0
  for (int col = 0; col < OLED_H; col++) {
    oled_compose_column(s_fb, col, &s_compose, line);
    send(line, COL_BYTES, 1);
  }
}

// Stock order: init table, RAM written while the display is still off, then
// display on.
static void panel_bring_up(void) {
  panel_reset();
  for (size_t i = 0; i < sizeof(INIT_SEQ); i++) {
    cmd(INIT_SEQ[i]);
  }
  flush_locked();
  cmd(CMD_DISPLAY_ON);
}

esp_err_t oled_init(void) {
  // Chip select is held low for good, as the stock firmware does (single
  // device on the bus): toggling it per byte makes the controller drop the
  // argument of a two byte command.
  gpio_config_t io = {
      .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << PIN_CS),
      .mode = GPIO_MODE_OUTPUT,
  };
  gpio_config(&io);
  gpio_set_level(PIN_CS, 0);

  spi_bus_config_t bus = {
      .mosi_io_num = PIN_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = PIN_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = COL_BYTES,
  };
  esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "spi bus: %s", esp_err_to_name(e));
    return e;
  }
  spi_device_interface_config_t dev = {
      .clock_speed_hz = SPI_HZ,
      .mode = 0,
      .spics_io_num = -1,
      .queue_size = 1,
  };
  s_lock = xSemaphoreCreateMutex();
  if (!s_lock) {
    return ESP_ERR_NO_MEM;
  }
  oled_compose_init(&s_compose);
  oled_orbit_offset(s_orbit_step, &s_compose.dx, &s_compose.dy);
  spi_device_handle_t h = NULL;
  e = spi_bus_add_device(SPI2_HOST, &dev, &h);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "spi device: %s", esp_err_to_name(e));
    return e;
  }
  s_dev = h;

  oled_clear();
  xSemaphoreTake(s_lock, portMAX_DELAY);
  panel_bring_up();
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

void oled_copy_framebuffer(uint8_t *out) {
  if (!s_lock) {
    memset(out, 0, OLED_FB_BYTES);
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  memcpy(out, s_fb, OLED_FB_BYTES);
  xSemaphoreGive(s_lock);
}

void oled_set_flip(uint8_t flip) {
  if (!s_dev) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_compose.flip = flip & 3;
  flush_locked();
  xSemaphoreGive(s_lock);
}

void oled_set_levels(const uint8_t levels[16]) {
  if (!s_dev) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  memcpy(s_compose.level, levels, sizeof(s_compose.level));
  flush_locked();
  xSemaphoreGive(s_lock);
}

void oled_orbit_step(void) {
  if (!s_dev) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_orbit_step = (s_orbit_step + 1) % OLED_ORBIT_STEPS;
  oled_orbit_offset(s_orbit_step, &s_compose.dx, &s_compose.dy);
  flush_locked();
  xSemaphoreGive(s_lock);
}

void oled_reset(void) {
  if (!s_dev) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  panel_bring_up();
  xSemaphoreGive(s_lock);
}

void oled_command(const uint8_t *bytes, size_t len) {
  if (!s_dev) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (size_t i = 0; i < len; i++) {
    cmd(bytes[i]);
  }
  xSemaphoreGive(s_lock);
}

void oled_clear(void) {
  memset(s_target, 0, OLED_H * COL_BYTES);
}

void oled_pixel(int x, int y, uint8_t level) {
  if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) {
    return;
  }
  uint8_t *b = &s_target[y * COL_BYTES + x / 2];
  int shift = (x & 1) * 4;
  *b = (uint8_t) ((*b & ~(0x0F << shift)) | ((level & 0x0F) << shift));
}

void oled_show_frame(const uint8_t *fb) {
  if (!s_dev) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  memcpy(s_fb, fb, OLED_FB_BYTES);
  flush_locked();
  xSemaphoreGive(s_lock);
}

uint8_t *oled_frame_begin(void) {
  if (!s_dev) {
    return NULL;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  return s_fb;
}

void oled_frame_end(void) {
  flush_locked();
  xSemaphoreGive(s_lock);
}

void oled_flush(void) {
  // A preview renders to an off-screen buffer; never push it to the panel.
  if (!s_dev || s_target != s_fb) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  flush_locked();
  xSemaphoreGive(s_lock);
}

// Redirect drawing to `buf` (OLED_FB_BYTES, same layout as the live buffer) so a
// screen can be rendered off-screen; oled_render_done() restores the live one.
void oled_render_to(uint8_t *buf) {
  s_target = buf;
}

void oled_render_done(void) {
  s_target = s_fb;
}

void oled_test_pattern(void) {
  oled_clear();
  for (int x = 0; x < OLED_W; x++) {
    oled_pixel(x, 0, 15);
    oled_pixel(x, OLED_H - 1, 15);
  }
  for (int y = 0; y < OLED_H; y++) {
    oled_pixel(0, y, 15);
    oled_pixel(OLED_W - 1, y, 15);
  }
  for (int x = 10; x < OLED_W; x += 10) {
    int len = (x % 50 == 0) ? 8 : 4;
    for (int k = 1; k <= len; k++) {
      oled_pixel(x, k, 15);
      oled_pixel(x, OLED_H - 1 - k, 15);
    }
  }
  for (int y = 10; y < OLED_H; y += 10) {
    int len = (y % 50 == 0) ? 8 : 4;
    for (int k = 1; k <= len; k++) {
      oled_pixel(k, y, 15);
      oled_pixel(OLED_W - 1 - k, y, 15);
    }
  }
  for (int k = -6; k <= 6; k++) {
    oled_pixel(OLED_W / 2 + k, OLED_H / 2, 15);
    oled_pixel(OLED_W / 2, OLED_H / 2 + k, 15);
  }
  // Corner marks: top-left 10x10, top-right 5x5, bottom-left 3x3.
  for (int a = 0; a < 10; a++) {
    for (int b = 0; b < 10; b++) {
      oled_pixel(a, b, 15);
    }
  }
  for (int a = 0; a < 5; a++) {
    for (int b = 0; b < 5; b++) {
      oled_pixel(OLED_W - 1 - a, b, 15);
    }
  }
  for (int a = 0; a < 3; a++) {
    for (int b = 0; b < 3; b++) {
      oled_pixel(a, OLED_H - 1 - b, 15);
    }
  }
  oled_flush();
}
