#include "handlers.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "auth.h"
#include "button.h"
#include "crash_log.h"
#include "creds.h"
#include "dev_switches.h"
#include "factory_record.h"
#include "fw_manifest.h"
#include "fw_update.h"
#include "heap_internal.h"
#include "json_escape.h"
#include "net.h"
#include "oled.h"
#include "rollback.h"
#include "screen.h"
#include "stall_watch.h"
#include "system.h"
#include "util.h"
#include "wasm_host.h"
#include "version.h"
#include "web_manifest.h"

// For anyone: the API and firmware versions, whether a config password is set ("cfg"),
// whether this caller is signed in, and where the device is on the LAN ("ip", "host")
// with why it cannot join the saved network ("wifi_fail"): all the page needs to show
// sign-in, the first setup and the address to open after it. Signed in: slot and
// Wi-Fi state, the offered update, the progress of an install, a rolled back version,
// the auto update option, and the diagnostics: reset reason, uptime, signal, channel,
// how often and why the router was lost, the last PBKDF2 time ("cfg_ms"), the
// factory record's serial and lcd_id ("" and -1 without one) with the panel driven
// ("A" or "B", drivers/oled.h), and the last crash.
#define PUBLIC_FORMAT                                                                              \
  "{\"api\":%d,\"fw\":\"%s\",\"cfg\":%s,\"signed_in\":%s,\"ip\":\"%s\",\"host\":\"%s\","           \
  "\"wifi_fail\":\"%s\""
#define STATUS_FORMAT                                                                              \
  ",\"slot\":\"%s\",\"wifi\":\"%s\",\"ssid\":\"%s\",\"dev\":%s,\"test_hooks\":%s,\"web\":\"%s\""
// The least free stack each of the firmware's own tasks has had, in bytes, as
// "name":bytes pairs; a task that is not running is left out.
static void stack_report(char *out, int cap) {
  static const char *const NAMES[] = {"ui", "net", "sensors", "touch", "button", "httpd", "main"};
  int n = 0;
  out[0] = '\0';
  for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]) && n < cap; i++) {
    TaskHandle_t t = xTaskGetHandle(NAMES[i]);
    if (t) {
      unsigned free_b = (unsigned) uxTaskGetStackHighWaterMark(t);
      n += snprintf(out + n, cap - n, "%s\"%s\":%u", n ? "," : "", NAMES[i], free_b);
    }
  }
}

// How many of lwIP's socket slots are in use: a free slot answers getsockopt with
// EBADF. The ceiling is CONFIG_LWIP_MAX_SOCKETS; at the ceiling accept and connect fail.
static int sockets_in_use(void) {
  int used = 0;
  for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; fd++) {
    int type = 0;
    socklen_t len = sizeof(type);
    if (lwip_getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0) {
      used++;
    }
  }
  return used;
}

// ,"update":{...}|null,"ota":{...}|null,"auto_update":bool. Every string here passed a
// shape check (fw_manifest.c) or is the firmware's own, so none needs escaping.
static void update_report(char *buf, int *n, int cap) {
  fw_offer_text_t m;
  if (fw_manifest_offer_text(&m)) {
    bufappend(buf, n, cap, ",\"update\":{\"version\":\"%s\",\"notes\":\"%s\"}", m.version, m.notes);
  } else {
    bufappend(buf, n, cap, ",\"update\":null");
  }
  fw_update_status_t u;
  fw_update_status(&u);
  if (u.state == FW_UPD_IDLE) {
    bufappend(buf, n, cap, ",\"ota\":null");
  } else {
    bufappend(buf, n, cap,
              ",\"ota\":{\"state\":\"%s\",\"version\":\"%s\",\"done\":%lu,\"total\":%lu,"
              "\"err\":\"%s\"}",
              fw_update_state_name(u.state), u.version, (unsigned long) u.done,
              (unsigned long) u.total, u.err);
  }
  char rolled_back[32];
  if (fw_update_rolled_back(rolled_back, sizeof(rolled_back))) {
    bufappend(buf, n, cap, ",\"rolled_back\":\"%s\"", rolled_back);
  }
  bufappend(buf, n, cap, ",\"checking\":%s,\"check_failed\":%s",
            fw_manifest_checking() ? "true" : "false",
            fw_manifest_check_failed() ? "true" : "false");
  bufappend(buf, n, cap, ",\"auto_update\":%s", fw_update_auto() ? "true" : "false");
}

// ,"reset" to ,"btn":{...}: the diagnostics, a group of related values per append.
static void diag_report(char *buf, int *n, int cap, const net_stats_t *ns) {
  bufappend(buf, n, cap, ",\"reset\":\"%s\",\"up_s\":%d", rollback_reset_reason(),
            (int) (esp_timer_get_time() / 1000000));
  bufappend(buf, n, cap, ",\"rssi\":%d,\"ch\":%d", ns->rssi, ns->channel);
  bufappend(buf, n, cap, ",\"disc\":{\"n\":%d,\"reason\":%d,\"at_s\":%d},\"ips\":%d",
            ns->disc_count, ns->disc_reason, ns->disc_at_s, ns->ip_count);
  bufappend(buf, n, cap, ",\"heap_min\":%u,\"heap_largest\":%u,\"heap_free\":%u",
            (unsigned) heap_caps_get_minimum_free_size(HEAP_INTERNAL),
            (unsigned) heap_caps_get_largest_free_block(HEAP_INTERNAL),
            (unsigned) heap_caps_get_free_size(HEAP_INTERNAL));
  unsigned psram_free = 0;
  unsigned psram_min = 0;
  wasm_host_psram(&psram_free, &psram_min);
  bufappend(buf, n, cap, ",\"psram_free\":%u,\"psram_min\":%u", psram_free, psram_min);
  bufappend(buf, n, cap, ",\"sockets\":%d", sockets_in_use());
  char stacks[160];
  stack_report(stacks, sizeof(stacks));
  char stall[96];
  stall_watch_json(stall, sizeof(stall));
  bufappend(buf, n, cap, ",\"stack_free\":{%s},\"stall\":%s", stacks, stall);
  bufappend(buf, n, cap, ",\"confirm_ms\":%d,\"cfg_ms\":%d", rollback_confirm_ms(),
            creds_last_hash_ms());
  bufappend(buf, n, cap, ",\"img\":\"%s\",\"bootloops\":%lu", rollback_image_state(),
            rollback_bootloops());
  bufappend(buf, n, cap, ",\"ui_timeouts\":%d,\"frames_reposted\":%d", screen_manager_ui_timeouts(),
            screen_manager_frames_reposted());
  bufappend(buf, n, cap, ",\"ap\":%s,\"off\":\"%s\"", net_ap_is_up() ? "true" : "false",
            system_last_off());
  button_counts_t btn;
  button_counts(&btn);
  bufappend(buf, n, cap, ",\"btn\":{\"armed\":%s,\"level\":%d,\"clicks\":%d,\"holds\":%d",
            btn.armed ? "true" : "false", btn.level, btn.clicks, btn.holds);
  bufappend(buf, n, cap, ",\"longs\":%d}", btn.longs);
  // The serial passed the record's shape check (letters and digits only).
  factory_record_t record;
  bool have_record = factory_record_get(&record);
  bufappend(buf, n, cap, ",\"serial\":\"%s\",\"lcd_id\":%d,\"panel\":\"%s\"",
            have_record ? record.serial : "", have_record ? record.lcd_id : -1,
            oled_panel() == OLED_PANEL_B ? "B" : "A");
}

esp_err_t api_status_get(httpd_req_t *req) {
  api_mark_ap_hit(req);
  bool signed_in = auth_signed_in(req);
  char ip[16] = "";
  net_ip(ip, sizeof(ip));
  char host[40];
  net_host(host, sizeof(host));
  char buf[2304];
  int n = 0;
  bufappend(buf, &n, sizeof(buf), PUBLIC_FORMAT, API_VERSION, FW_VERSION,
            creds_config_is_set() ? "true" : "false", signed_in ? "true" : "false", ip, host,
            net_join_problem());
  if (!signed_in) {
    bufappend(buf, &n, sizeof(buf), "}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
  }
  const esp_partition_t *run = esp_ota_get_running_partition();
  bool joined = net_online();
  net_stats_t ns;
  net_stats(&ns);
  char ssid[NET_SSID_MAX * JSON_ESCAPE_MAX + 1] = "";
  if (joined && ns.have_ap) {
    json_escape(ssid, sizeof(ssid), (const char *) ns.ssid);
  }
  // Every append is bounded: the longest answer (a 32-byte SSID of control bytes, 6
  // bytes each escaped, all seven stacks, the button counts, a crash with 8 frames, the
  // watchdog and panic marks, 240 bytes of update notes, the address and a rolled back
  // version) is about 2090 bytes and a truncated one is still a string.
  web_manifest_t wm;
  const char *web_version = web_manifest_get(&wm) ? wm.version : "";
  bufappend(buf, &n, sizeof(buf), STATUS_FORMAT, run->label, joined ? "connected" : "not connected",
            ssid, DEV_ANY || api_test_hooks_on() ? "true" : "false",
            api_test_hooks_on() ? "true" : "false", web_version);
  const char *migrated = creds_migration_result();
  if (migrated) {
    bufappend(buf, &n, sizeof(buf), ",\"cfg_migrated\":\"%s\"", migrated);
  }
  update_report(buf, &n, sizeof(buf));
  diag_report(buf, &n, sizeof(buf), &ns);
  bufappend(buf, &n, sizeof(buf), ",\"crash\":");
  if (n < (int) sizeof(buf) - 1) {
    n += crash_log_json(buf + n, (int) sizeof(buf) - n);  // bounded, returns at most cap - 1
  }
  if (n < (int) sizeof(buf) - 1) {
    n += crash_log_diag_json(buf + n, (int) sizeof(buf) - n);
  }
  bufappend(buf, &n, sizeof(buf), "}");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, buf, n);
  return ESP_OK;
}
