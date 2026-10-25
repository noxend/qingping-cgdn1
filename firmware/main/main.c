// CGDN1 custom firmware: app_main brings the components up in order.
//
// Lock-out protection, all of which must hold before the case is sealed:
//   - GPIO23 power latch, held through software, panic and watchdog resets
//     (system/power_latch.h).
//   - Dual OTA slots: an update writes the other slot; the running app is never
//     erased. Only signed images built from this project are accepted (api/ota.c).
//   - Rollback gate, the bootloader's RTC watchdog and the boot-loop counter
//     (system/rollback.h).
// The setup access point and the home Wi-Fi are described in net/net.h.

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "api.h"
#include "auth.h"
#include "battery.h"
#include "button.h"
#include "creds.h"
#include "dev_switches.h"
#include "factory_record.h"
#include "fw_manifest.h"
#include "fw_update.h"
#include "led.h"
#include "net.h"
#include "oled.h"
#include "power_latch.h"
#include "rollback.h"
#include "screen.h"
#include "sensors.h"
#include "settings.h"
#include "stall_watch.h"
#include "heap_internal.h"
#include "system.h"
#include "text.h"
#include "touch.h"
#include "version.h"
#include "web_manifest.h"

#ifndef DEV_BUILD
_Static_assert(!DEV_OPEN_DIAGNOSTICS && !DEV_EARLY_CONFIRM && !DEV_FW_STAND,
               "a DEV_* switch is on in secrets.h; build with idf.py -DDEV_BUILD=1 or turn it off");
#endif

static const char *TAG = "cgdn1";

// The console stays IDF's default, whose writes spin until the UART FIFO has room: a
// flood of log lines starves the idle task (touch_bus keeps the strip's quiet). The
// UART driver in its place was in 1.0.0-rc16, which froze or reset at Wi-Fi start with
// no adapter on the RX pin. A release build logs warnings and errors, and the info lines
// of the core's own tag (the version at boot, Wi-Fi, OTA).
static void console_start(void) {
#ifndef DEV_BUILD
  esp_log_level_set("*", ESP_LOG_WARN);
  esp_log_level_set(TAG, ESP_LOG_INFO);
#endif
}

// Healthy = HTTP server up, and either joined home WiFi or served a request on
// the setup AP. Retries the server first, in case it failed at boot.
static bool gate_healthy(void) {
  api_start();
  return api_is_up() && (net_online() || net_ap_hit());
}

// The power button (drivers/button.h): a click goes back in the device menu, and
// outside it blanks or wakes the panel and the LED; a hold opens the menu; a long
// press powers the board off (system.h) whatever the menu does. On USB the rail
// stays, so the board stays dark instead, as the stock firmware's charging state
// does, until the next hold restarts it.
static volatile bool s_stayed_on;
static StaticSemaphore_t s_menu_power_off_buf;
static SemaphoreHandle_t s_menu_power_off;  // given by the menu, taken by the main task

static void on_button_click(void) {
  if (!s_stayed_on) {
    screen_manager_button_click();
  }
}

static void on_button_hold(void) {
  if (s_stayed_on) {
    system_power_on();
  }
  screen_manager_button_hold();
}

// Until the image is confirmed a power cut rolls the update back, so power off
// waits for that (a minute at most after an update).
static bool update_pending(void) {
  if (strcmp(rollback_image_state(), "pending") != 0) {
    return false;
  }
  ESP_LOGW(TAG, "power off ignored: the update is not confirmed yet");
  return true;
}

// show() posts the message; the UI loop draws it within show_ms, before the rail goes.
static void shut_down(const char *reason, void (*show)(void), int show_ms) {
  show();
  sensors_power_off();
  vTaskDelay(pdMS_TO_TICKS(show_ms));
  s_stayed_on = !system_power_off(reason, button_pressed);
  screen_manager_display_off();
}

static void power_off(const char *reason) {
  if (update_pending()) {
    return;
  }
  shut_down(reason, screen_manager_powering_off, 100);
}

// On battery the stock firmware powers off below 7 % of its table, about 3.3 V
// (docs/hardware.md); left running, the board drains the cell further. Starting
// needs more: Wi-Fi start pulls a cell resting near 3.3 V under it.
#define BATTERY_OFF_MV 3300
#define BATTERY_START_MV 3400
#define BATTERY_NO_READING_MV 2500  // the stock's floor: at or under it, a failed read
#define BATTERY_LOW_READS 3         // in a row, one per pass of the main loop (10 s)
#define LOW_BATTERY_SHOW_MS 3000

static bool s_display_ok;

// Before Wi-Fi: on battery, a flat cell or a brownout restart powers off, so a weak
// cell gets one brownout at most instead of a restart loop. A pending update does
// not hold it: the power cut rolls the update back, a cheaper loss than the cell.
static void battery_start_check(void) {
  battery_t b = battery_read();
  bool flat = b.mv > BATTERY_NO_READING_MV && b.mv < BATTERY_START_MV;
  bool brownout = esp_reset_reason() == ESP_RST_BROWNOUT;
  ESP_LOGI(TAG, "battery %d mV, usb %d, charging %d", b.mv, b.usb, b.charging);
  if (b.usb || !(flat || brownout)) {
    return;
  }
  ESP_LOGW(TAG, "low battery at start (%s): powering off", brownout ? "brownout" : "flat");
  if (s_display_ok) {
    text_show_message("Low battery");
  }
  vTaskDelay(pdMS_TO_TICKS(LOW_BATTERY_SHOW_MS));
  if (!system_power_off("battery", button_pressed)) {
    power_latch_hold();  // USB came in meanwhile: start, and keep the cell when it goes
  }
}

static int s_low_reads;

// Not after a power off that kept the rail: the board is dark on USB already.
static void battery_watch(void) {
  if (s_stayed_on) {
    return;
  }
  battery_t b = battery_read();
  bool low = !b.usb && b.mv > BATTERY_NO_READING_MV && b.mv < BATTERY_OFF_MV;
  s_low_reads = low ? s_low_reads + 1 : 0;
  if (s_low_reads < BATTERY_LOW_READS) {
    return;
  }
  s_low_reads = 0;
  ESP_LOGW(TAG, "low battery: %d mV", b.mv);
  shut_down("battery", screen_manager_low_battery, LOW_BATTERY_SHOW_MS);
}

// A board that stayed on has restarted at the hold already.
static void on_button_long(void) {
  power_off("button");
}

// The menu's "Power off" comes on the UI loop, which has to draw "Powering off"
// first, so the main task does it.
static void on_menu_power_off(void) {
  if (!update_pending()) {
    xSemaphoreGive(s_menu_power_off);
  }
}

// The stock rule: panel A for lcd_id 1, B for any other. With no record (a unit
// that ran an earlier version of this firmware lost it) A, the panel of every unit
// seen so far.
static oled_panel_t panel_type(void) {
  factory_record_t record;
  if (factory_record_get(&record) && record.lcd_id != 1) {
    return OLED_PANEL_B;
  }
  return OLED_PANEL_A;
}

void app_main(void) {
  power_latch_hold();
  console_start();
  heap_large_install();  // before any TLS session or JSON
  creds_seed_entropy();  // before touch and Wi-Fi: it borrows the SAR ADC
  battery_init();

  bool pending_verify = rollback_boot();
  const esp_partition_t *running = esp_ota_get_running_partition();

  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "CGDN1 fw %s (idf %s), build %s %s, slot %s%s", FW_VERSION, app->idf_ver, app->date,
           app->time, running->label, pending_verify ? " (unconfirmed)" : "");

  rollback_bootloop_check();  // before the flash work below, so a crash in it is counted

  system_nvs_init();
  factory_record_load();  // before settings_init, which takes the record's flash
  s_display_ok = oled_init(panel_type()) == ESP_OK;
  if (s_display_ok) {
    text_init();
    text_show_message("Starting");  // until the first screen, a few seconds on
  } else {
    ESP_LOGE(TAG, "display init failed");
  }
  led_init();
  touch_start(core_touch, core_hold);

  settings_init();
  system_last_off_load();
  battery_start_check();  // its power off notes the reason in the settings
  creds_init();
  session_init();
  web_manifest_init();
  fw_manifest_init(FW_VERSION);
  fw_update_init();

  system_task_watchdog_init();

  auth_set_hosts(NET_MDNS_HOSTNAME, NET_AP_IP);
  net_start();
  api_start();
  sensors_start();
  s_menu_power_off = xSemaphoreCreateBinaryStatic(&s_menu_power_off_buf);
  screen_manager_set_version(FW_VERSION);
  screen_manager_set_power_off(on_menu_power_off);
  screen_manager_start();
  button_start(on_button_click, on_button_hold, on_button_long);  // after the loop and the settings

  stall_watch_start();
  rollback_settle(pending_verify, gate_healthy);
  // Confirmed image: keep the server alive if it ever failed to come up, power off
  // when the menu asks, and on a low battery.
  while (true) {
    if (xSemaphoreTake(s_menu_power_off, pdMS_TO_TICKS(10000))) {
      power_off("menu");
    }
    api_start();
    battery_watch();
  }
}
