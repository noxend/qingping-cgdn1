#include "rollback.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hal/wdt_hal.h"
#include "dev_switches.h"

#define SOAK_US (60LL * 1000 * 1000)
#define PENDING_DEADLINE_US (180LL * 1000 * 1000)
#define BOOTLOOP_LIMIT 3
#define BOOTLOOP_CLEAR_US (10LL * 60 * 1000 * 1000)
#define BOOTLOOP_MAGIC 0xC6D1B007u

static const char *TAG = "cgdn1";
static volatile int s_confirm_ms = -1;

static RTC_NOINIT_ATTR uint32_t s_bootloop_magic;
static RTC_NOINIT_ATTR uint32_t s_bootloop_count;

// ---- RTC watchdog (armed by the bootloader) ----------------------------------

static void rtc_wdt_feed(void) {
  wdt_hal_context_t ctx = RWDT_HAL_CONTEXT_DEFAULT();
  wdt_hal_write_protect_disable(&ctx);
  wdt_hal_feed(&ctx);
  wdt_hal_write_protect_enable(&ctx);
}

static void rtc_wdt_off(void) {
  wdt_hal_context_t ctx = RWDT_HAL_CONTEXT_DEFAULT();
  wdt_hal_write_protect_disable(&ctx);
  wdt_hal_disable(&ctx);
  wdt_hal_write_protect_enable(&ctx);
}

bool rollback_boot(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t boot_state = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(running, &boot_state);
  bool pending_verify = (boot_state == ESP_OTA_IMG_PENDING_VERIFY);
  if (!pending_verify) {
    rtc_wdt_off();  // confirmed image: the bootloader watchdog is not needed
  }
  return pending_verify;
}

// ---- boot-loop counter ------------------------------------------------------

static void bootloop_clear_cb(void *arg) {
  s_bootloop_count = 0;
  ESP_LOGI(TAG, "stable for 10 min, boot-loop counter cleared");
}

static bool reset_was_crash(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

const char *rollback_reset_reason(void) {
  switch (esp_reset_reason()) {
  case ESP_RST_POWERON:
    return "poweron";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_PANIC:
    return "panic";
  case ESP_RST_INT_WDT:
    return "int_wdt";
  case ESP_RST_TASK_WDT:
    return "task_wdt";
  case ESP_RST_WDT:
    return "wdt";
  case ESP_RST_BROWNOUT:
    return "brownout";
  default:
    return "other";
  }
}

// Brownouts are not counted: a weak battery must not roll back a good image.
void rollback_bootloop_check(void) {
  esp_reset_reason_t r = esp_reset_reason();
  if (s_bootloop_magic != BOOTLOOP_MAGIC || r == ESP_RST_POWERON) {
    s_bootloop_magic = BOOTLOOP_MAGIC;
    s_bootloop_count = 0;
  }
  if (reset_was_crash(r)) {
    s_bootloop_count++;
    ESP_LOGW(TAG, "crash reset %d (count %lu)", r, (unsigned long) s_bootloop_count);
  }
  if (s_bootloop_count >= BOOTLOOP_LIMIT) {
    s_bootloop_count = 0;
    ESP_LOGE(TAG, "%d crash resets in a row, rolling back", BOOTLOOP_LIMIT);
    esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "no other working app to roll back to, staying on this one");
  }
  const esp_timer_create_args_t a = {.callback = bootloop_clear_cb, .name = "bootloop"};
  esp_timer_handle_t t;
  if (esp_timer_create(&a, &t) == ESP_OK) {
    esp_timer_start_once(t, BOOTLOOP_CLEAR_US);
  }
}

unsigned long rollback_bootloops(void) {
  return (unsigned long) s_bootloop_count;
}

const char *rollback_image_state(void) {
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK) {
    return "unknown";
  }
  switch (st) {
  case ESP_OTA_IMG_NEW:
    return "new";
  case ESP_OTA_IMG_PENDING_VERIFY:
    return "pending";
  case ESP_OTA_IMG_VALID:
    return "valid";
  case ESP_OTA_IMG_INVALID:
    return "invalid";
  case ESP_OTA_IMG_ABORTED:
    return "aborted";
  default:
    return "undefined";
  }
}

int rollback_confirm_ms(void) {
  return s_confirm_ms;
}

// ---- rollback gate ------------------------------------------------------------

// Feeds the bootloader's RTC watchdog while waiting, so only a hang (no feed) or
// the deadline ends in a rollback.
static void pending_gate(bool (*healthy)(void)) {
  int64_t start = esp_timer_get_time();
  int64_t healthy_since = 0;
  while (true) {
    rtc_wdt_feed();
    bool ok = healthy();
    int64_t now = esp_timer_get_time();
    if (!ok) {
      healthy_since = 0;
    } else if (healthy_since == 0) {
      healthy_since = now;
    }
    if (healthy_since != 0 && now - healthy_since >= SOAK_US) {
      int64_t t0 = esp_timer_get_time();
      esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
      s_confirm_ms = (int) ((esp_timer_get_time() - t0) / 1000);
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "healthy for 60 s, image confirmed");
        rtc_wdt_off();
        return;
      }
      ESP_LOGE(TAG, "mark valid failed: %s", esp_err_to_name(err));
    }
    if (now - start >= PENDING_DEADLINE_US) {
      ESP_LOGE(TAG, "not healthy within 3 min; rebooting so the bootloader rolls back");
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void rollback_settle(bool pending, bool (*healthy)(void)) {
  if (pending && DEV_EARLY_CONFIRM) {
    // Development only: confirm at once so a crash or reset loop can be watched
    // instead of rolled back.
    // A failed confirm falls through to the gate, which keeps the watchdog and retries.
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      rtc_wdt_off();
      pending = false;
    } else {
      ESP_LOGE(TAG, "early confirm failed: %s", esp_err_to_name(err));
    }
  }
  if (pending) {
    pending_gate(healthy);
  }
}
