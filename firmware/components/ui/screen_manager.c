#include "screen.h"
#include "screen_internal.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "carousel.h"
#include "core_net.h"
#include "creds.h"
#include "factory_reset.h"
#include "fw_manifest.h"
#include "fw_update.h"
#include "heap_internal.h"
#include "led.h"
#include "menu.h"
#include "net.h"
#include "night.h"
#include "oled.h"
#include "rollback.h"
#include "settings.h"
#include "text.h"
#include "wasm_host.h"
#include "wasm_plugins.h"
#include "web_manifest.h"
#include "setup_ap.h"

// The screens are the installed plugins, by their index in the host's table
// (wasm_plugins.h). The core names no plugin; the one exception is the GET /alerts
// alias in screen_http.c.

static const char *TAG = "screens";

// ---- Callback budget -------------------------------------------------------------
// Every plugin callback is timed. One over the budget is a fault; FAULT_LIMIT faults
// in a session disable the plugin: its timers stop, it leaves the page order, and
// the next enabled screen is shown if it was the one on the panel.
#define CALLBACK_BUDGET_US (250 * 1000)
#define FAULT_LIMIT 3
#define FAULT_DECAY_S 600  // every ten minutes one fault is forgiven
// A plugin's two timers (ids 0 and 1).
#define PLUGIN_TIMERS 2

// What the core keeps per screen, at the plugin's index in the host's table.
typedef struct {
  int faults;
  // Overruns in web previews have their own count: past the limit that plugin previews
  // as a blank frame, so GET /screen?id= (no token) cannot keep the loop busy with it.
  int preview_faults;
  bool disabled;
  // Grows whenever the index takes another plugin: a timer or a network answer for the
  // plugin it held before is then dropped instead of reaching the one it holds now.
  int gen;
  esp_timer_handle_t timers[PLUGIN_TIMERS];
} screen_t;

// As long as the host's table, grown with it under the registry lock (the HTTP task
// reads faults and disabled).
static screen_t *s_screens;
static int s_screen_n;
// The enabled screens as indexes, in order: read from settings once at boot and
// whenever the list changes (EV_ENABLED), so paging and the 5 s neighbour refresh
// touch neither NVS nor the heap. UI loop only.
static int *s_enabled_ids;
static int s_enabled_n;
// Set by the httpd task after storing a new list; the loop reloads on its next
// event, so a dropped EV_ENABLED cannot leave paging on a list the web no longer shows.
static volatile bool s_enabled_dirty;
static int64_t s_call_start_us;
// Off during web previews: a LAN host must not be able to disable a plugin.
static bool s_count_faults = true;
static int s_disable_pending = -1;  // set by call_end, acted on once the handler is done
static TaskHandle_t s_ui_task;
static void disable_plugin(int idx);

enum {
  EV_BOOT,
  EV_SWITCH,
  EV_CONFIG,
  EV_TIMER,
  EV_NET,
  EV_PREVIEW,
  EV_FRAME,
  EV_REFRESH,
  EV_ENABLED,
  EV_HEARTBEAT,     // once a second, so the task watchdog sees the loop turn over
  EV_REDRAW,        // draw the shown screen again (a probe used the panel)
  EV_MENU_FINGER,   // the finger while the device menu is open (finger_msg_t)
  EV_MENU_TIMEOUT,  // the menu was left alone for menu_idle_us()
  EV_DISPLAY,       // the power button and the power-off path: display_cmd_t
  EV_WAKE,          // a finger on a blanked panel
};

#define UI_QUEUE_LEN 16
#define HEARTBEAT_US (1000 * 1000)
// How long GET /screen waits for the UI loop. The slowest work on the loop (a
// neighbour redraw plus a JSON parse) is under 200 ms; the task watchdog is 60 s.
#define PREVIEW_WAIT_TICKS pdMS_TO_TICKS(2000)

typedef struct {
  int owner;  // request_owner()'s, so a tick for a plugin gone since is dropped
  int id;
} timer_msg_t;

typedef struct {
  int owner;
  core_net_result_t net;
} net_msg_t;

static volatile int s_ui_timeouts;  // GET /screen requests the loop did not answer in time
static esp_timer_handle_t s_heartbeat;
static bool s_heartbeat_on;  // only then may the loop's task join the watchdog

// A plain queue, not an esp_event loop: a loop without a task of its own makes every
// post take the mutex its handlers run under, so a post that cannot wait fails
// whenever a handler is running. This one refuses only when it is full.
static QueueHandle_t s_queue;
static int s_active;  // screen index reported by GET /config; the UI loop and POST /config set it
static int s_shown = -1;  // screen that got CORE_EV_SHOW last
static int s_cur = -1;    // screen whose callback is running, -1 outside callbacks

static void call_begin(int idx) {
  s_cur = idx;
  s_call_start_us = esp_timer_get_time();
}

static void call_end(int idx, const char *what) {
  s_cur = -1;
  int64_t took_us = esp_timer_get_time() - s_call_start_us;
  if (took_us <= CALLBACK_BUDGET_US) {
    return;
  }
  screen_t *sc = &s_screens[idx];
  if (!s_count_faults) {
    sc->preview_faults++;
    ESP_LOGW(TAG, "%s.%s took %lld ms in a preview (%d of %d before previews go blank)",
             plugins_id(idx), what, (long long) (took_us / 1000), sc->preview_faults, FAULT_LIMIT);
    return;
  }
  sc->faults++;
  ESP_LOGW(TAG, "%s.%s took %lld ms (fault %d of %d)", plugins_id(idx), what,
           (long long) (took_us / 1000), sc->faults, FAULT_LIMIT);
  if (sc->faults >= FAULT_LIMIT && !sc->disabled) {
    s_disable_pending = idx;  // not here: the faulting callback's own render is still on the stack
  }
}
static bool s_dirty;  // the shown screen asked for a redraw

// ---- Device menu and display: state ----------------------------------------------
// A hold on the power button opens the menu over the shown screen; while it is open
// the finger goes to the menu, not the carousel, and the shown screen is not
// repainted. A click on the button goes back in the menu, and outside it blanks the
// panel and the LED; the next finger or click brings them back. Both flags are read
// on the touch task, hence atomic.
#define JOINED_SHOW_US (5LL * 60 * 1000 * 1000)  // the address after a save, unless touched
static atomic_bool s_menu_owns_finger;
static atomic_bool s_display_off;
static atomic_bool s_powered_down;  // the power-off path blanks it; nothing wakes it
static bool s_blank_pending;        // the panel must be cleared once the carousel lets go
static esp_timer_handle_t s_menu_timer;
static const char *s_version = "";
static void (*s_power_off)(void);

typedef struct {
  touch_phase_t phase;
  int pos;
  uint32_t ms;  // when the touch task saw it
} finger_msg_t;

typedef enum {
  DISPLAY_CLICK,
  DISPLAY_MENU,
  DISPLAY_OFF,
  DISPLAY_POWERING_OFF,
  DISPLAY_LOW_BATTERY,
} display_cmd_t;
static volatile int s_dropped;   // events lost to a full queue
static bool s_neighbours_stale;  // the carousel must redraw the screens beside the shown one

static void render_offscreen(int idx, uint8_t *fb, bool count);

static bool plugin_led(int idx, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (s_screens[idx].disabled) {
    return false;
  }
  call_begin(idx);
  bool on = plugins_led(idx, r, g, b);
  call_end(idx, "led");
  return on;
}

static void apply_led(int idx) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  if (plugin_led(idx, &r, &g, &b)) {
    led_set(r, g, b);
  } else {
    led_set(0, 0, 0);
  }
}

// The core flushes: a plugin draws into the frame and never touches the panel.
static void render(int idx) {
  if (s_screens[idx].disabled) {
    oled_clear();
  } else {
    call_begin(idx);
    plugins_draw(idx);
    call_end(idx, "draw");
  }
  oled_flush();
  apply_led(idx);
}

// One queued event, its data copied in.
typedef struct {
  int32_t id;
  union {
    int idx;
    timer_msg_t timer;
    net_msg_t net;
    preview_t *preview;
    finger_msg_t finger;
    display_cmd_t display;
  } data;
} ui_msg_t;

static bool queue_send(int32_t id, const void *data, size_t size, TickType_t wait) {
  ui_msg_t msg = {.id = id};
  assert(size <= sizeof(msg.data));
  if (size > 0) {
    memcpy(&msg.data, data, size);
  }
  return s_queue && xQueueSendToBack(s_queue, &msg, wait) == pdTRUE;
}

// Queues an event for the UI loop; counts it as dropped when the queue stays full.
static bool post_event(int32_t id, const void *data, size_t size, TickType_t wait) {
  bool queued = queue_send(id, data, size, wait);
  if (!queued) {
    s_dropped++;
    ESP_LOGW(TAG, "event %d dropped, %d so far", (int) id, s_dropped);
  }
  return queued;
}

static bool post(int32_t id, const void *data, size_t size) {
  return post_event(id, data, size, pdMS_TO_TICKS(50));
}

// ---- Core API --------------------------------------------------------------

bool core_online(void) {
  return net_online();
}

void core_redraw(void) {
  if (s_cur >= 0 && s_cur == s_shown) {
    s_dirty = true;
  }
}

// A plugin's index and its screen's generation in one number, carried by its network
// requests and timer ticks: an answer or a tick for the plugin an index held before is
// dropped instead of reaching the one it holds now.
#define OWNER_INDEXES 0x10000
#define GEN_WRAP 0x4000

static int request_owner(int idx) {
  return idx + OWNER_INDEXES * (s_screens[idx].gen % GEN_WRAP);
}

// The index an answer or a tick belongs to, or -1 when its plugin has gone since.
static int answer_owner(int owner) {
  int idx = owner % OWNER_INDEXES;
  bool same = idx < s_screen_n && owner / OWNER_INDEXES == s_screens[idx].gen % GEN_WRAP;
  return same ? idx : -1;
}

// The timer's argument is its owner and id as a number, not a pointer: a callback
// already running on the esp_timer task when its timer is deleted reads nothing freed.
static void timer_cb(void *arg) {
  uintptr_t v = (uintptr_t) arg;
  timer_msg_t m = {.owner = (int) (v >> 1), .id = (int) (v & 1)};
  post_event(EV_TIMER, &m, sizeof(m), 0);  // a timer callback must never block
}

// The calling plugin's timer `id`, made on first use when create is set.
static esp_timer_handle_t timer_find(int id, bool create) {
  if (id < 0 || id >= PLUGIN_TIMERS) {
    return NULL;
  }
  screen_t *sc = &s_screens[s_cur];
  if (sc->timers[id] || !create) {
    return sc->timers[id];
  }
  uintptr_t arg = ((uintptr_t) request_owner(s_cur) << 1) | (uintptr_t) id;
  esp_timer_create_args_t args = {.callback = timer_cb, .arg = (void *) arg, .name = "core"};
  if (esp_timer_create(&args, &sc->timers[id]) != ESP_OK) {
    sc->timers[id] = NULL;
  }
  return sc->timers[id];
}

// Not from a web preview: GET /screen?id= needs no password, and a plugin that
// fetched as it drew, or started a timer that fetches later, would do so for anyone
// on the network asking for previews.
static bool may_act(void) {
  return s_cur >= 0 && s_count_faults;
}

void core_timer_start(int id, uint32_t ms, bool periodic) {
  esp_timer_handle_t t = may_act() ? timer_find(id, true) : NULL;
  if (!t) {
    ESP_LOGW(TAG, "timer %d not started", id);
    return;
  }
  esp_timer_stop(t);  // restart semantics; an error here only means it was idle
  uint64_t us = (uint64_t) ms * 1000;
  if (periodic) {
    esp_timer_start_periodic(t, us);
  } else {
    esp_timer_start_once(t, us);
  }
}

void core_timer_stop(int id) {
  esp_timer_handle_t t = may_act() ? timer_find(id, false) : NULL;
  if (t) {
    esp_timer_stop(t);
  }
}

bool core_net_get(const char *url, size_t max_bytes) {
  return may_act() && core_net_submit(request_owner(s_cur), url, max_bytes, NULL);
}

void core_stats(const char *json) {
  if (s_cur >= 0) {
    plugins_stats_set(s_cur, json);
  }
}

// Called on the worker task: forward the result to the UI loop, which owns the
// body from then on. A screen waits for exactly one result per request, so when
// the queue refuses the full result, the body is dropped and a small failure
// result is retried until it gets in.
static void net_done(int owner, core_net_result_t *r) {
  net_msg_t m = {.owner = owner, .net = *r};
  if (post_event(EV_NET, &m, sizeof(m), pdMS_TO_TICKS(1000))) {
    return;
  }
  free(r->body);
  free(r->parsed);
  m.net.body = NULL;
  m.net.parsed = NULL;
  m.net.len = 0;
  m.net.err = ESP_ERR_NO_MEM;
  while (!queue_send(EV_NET, &m, sizeof(m), pdMS_TO_TICKS(100))) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---- UI loop ----------------------------------------------------------------

static void deliver(int idx, const core_event_t *ev) {
  if (!s_screens[idx].disabled) {
    call_begin(idx);
    plugins_event(idx, ev);
    call_end(idx, "on_event");
  }
}

static void show(int idx) {
  if (idx < 0 || idx >= s_screen_n) {
    return;
  }
  if (s_screens[idx].disabled && s_enabled_n > 0) {
    idx = s_enabled_ids[0];  // a disabled plugin is never put back on the panel
  }
  if (s_shown >= 0) {
    core_event_t hide = {.type = CORE_EV_HIDE};
    deliver(s_shown, &hide);
  }
  s_active = idx;
  s_shown = idx;
  core_event_t shown = {.type = CORE_EV_SHOW};
  deliver(idx, &shown);
  s_dirty = true;  // a newly shown screen draws once, then reacts to its events
  s_neighbours_stale = true;
}

// Stops and deletes the plugin's timers, so a disabled or removed plugin gets no more
// events. A tick already queued carries the old generation and is dropped.
static void timers_stop_owner(int idx) {
  if (idx >= s_screen_n) {
    return;
  }
  for (int id = 0; id < PLUGIN_TIMERS; id++) {
    esp_timer_handle_t t = s_screens[idx].timers[id];
    if (t) {
      esp_timer_stop(t);
      esp_timer_delete(t);
      s_screens[idx].timers[id] = NULL;
    }
  }
}

static void disable_plugin(int idx) {
  s_screens[idx].disabled = true;
  timers_stop_owner(idx);
  int n = 0;
  for (int i = 0; i < s_enabled_n; i++) {
    if (s_enabled_ids[i] != idx) {
      s_enabled_ids[n++] = s_enabled_ids[i];
    }
  }
  s_enabled_n = n;
  ESP_LOGE(TAG, "%s disabled until reboot after %d faults", plugins_id(idx), s_screens[idx].faults);
  if (s_shown == idx && n > 0) {
    show(s_enabled_ids[0]);
  } else if (s_shown == idx) {
    s_dirty = true;  // nothing else to show: the panel goes blank and the LED off
  }
  s_neighbours_stale = true;
}

// The screen to show after `dir` steps (+1 next, -1 previous) through the enabled
// list, wrapping at the ends; -1 when nothing is enabled. A shown screen that
// is not in the list pages to the first or the last enabled one.

static void enabled_reload(void) {
  cJSON *list = screen_enabled_list();
  s_enabled_n = 0;
  cJSON *e = NULL;
  cJSON_ArrayForEach(e, list) {
    int idx = cJSON_IsString(e) ? screen_index(e->valuestring) : -1;
    bool listed = false;
    for (int k = 0; k < s_enabled_n && !listed; k++) {
      listed = s_enabled_ids[k] == idx;
    }
    if (idx >= 0 && idx < s_screen_n && !s_screens[idx].disabled && !listed) {
      s_enabled_ids[s_enabled_n++] = idx;
    }
  }
  cJSON_Delete(list);
}

static int next_enabled(int dir) {
  int n = s_enabled_n;
  if (n == 0) {
    return -1;
  }
  int pos = -1;
  for (int i = 0; i < n; i++) {
    if (s_enabled_ids[i] == s_shown) {
      pos = i;
    }
  }
  int target = pos < 0 ? (dir > 0 ? 0 : n - 1) : (pos + dir + n) % n;
  return s_enabled_ids[target];
}

// ---- Device menu and display ---------------------------------------------------

static void menu_timer_cb(void *arg) {
  post_event(EV_MENU_TIMEOUT, NULL, 0, pdMS_TO_TICKS(20));
}

// Every touch in the menu gives it another menu_idle_us() before it closes or,
// on the setup page, blanks the panel.
static void menu_touched(void) {
  if (!s_menu_timer) {
    return;
  }
  esp_timer_stop(s_menu_timer);
  esp_timer_start_once(s_menu_timer, menu_idle_us(net_has_network()));
}

static void menu_render(void) {
  menu_info_t info = {.version = s_version};
  net_ip(info.ip, sizeof(info.ip));  // written only while the station has one
  net_stats_t ns;
  net_stats(&ns);
  if (net_online() && ns.have_ap) {
    strlcpy(info.ssid, (const char *) ns.ssid, sizeof(info.ssid));
  }
  info.up_s = (int) (esp_timer_get_time() / 1000000);
  net_ap_ssid(info.ap_ssid, sizeof(info.ap_ssid));
  creds_ap_pass(info.ap_pass, sizeof(info.ap_pass));
  net_host(info.host, sizeof(info.host));
  menu_draw(&info);
  oled_flush();
}

static void menu_show(void) {
  menu_open();
  atomic_store(&s_menu_owns_finger, true);
  carousel_touch(TOUCH_PHASE_UP, 0);  // a finger on the strip is the menu's from here on
  menu_touched();
  menu_render();
}

// A device with no Wi-Fi saved shows how to set it up, at boot, when the panel wakes
// and whenever the menu is left alone; "Back" in the menu shows the screens. Left
// alone itself, the page blanks the panel (menu_idle_blanks) until the next touch.
static void setup_show(void) {
  menu_open_setup();
  atomic_store(&s_menu_owns_finger, true);
  menu_touched();
  menu_render();
}

// Once per boot after a save from the setup page: where to open the device, when it
// gets an address on the new network. A panel busy at that moment skips it, so the
// page never turns up late.
static void joined_check(void) {
  static bool decided;
  if (decided || !net_saved_before_boot() || !net_online()) {
    return;
  }
  decided = true;
  bool panel_free = !menu_is_open() && !carousel_busy() && !atomic_load(&s_display_off);
  if (!panel_free) {
    return;
  }
  menu_open_joined();
  atomic_store(&s_menu_owns_finger, true);
  if (s_menu_timer) {
    esp_timer_stop(s_menu_timer);
    esp_timer_start_once(s_menu_timer, JOINED_SHOW_US);
  }
  menu_render();
}

static void menu_hide(void) {
  menu_close();
  atomic_store(&s_menu_owns_finger, false);
  if (s_menu_timer) {
    esp_timer_stop(s_menu_timer);
  }
  s_dirty = true;  // the shown screen comes back
}

static void menu_act(menu_action_t action) {
  switch (action) {
  case MENU_NONE:
    break;
  case MENU_POWER_OFF:
    if (s_power_off) {
      s_power_off();
    }
    break;
  case MENU_RESTART:
    // Until the image is confirmed a restart rolls the update back, as a power cut does.
    if (strcmp(rollback_image_state(), "pending") == 0) {
      ESP_LOGW(TAG, "restart ignored: the update is not confirmed yet");
      break;
    }
    text_show_message("Restarting");
    esp_restart();
    break;
  case MENU_WIFI_SETUP:
    setup_ap_show();
    menu_render();
    break;
  case MENU_FACTORY_RESET:
    factory_reset(net_forget_driver_creds);  // does not return
    break;
  case MENU_CLOSE:
    menu_hide();
    break;
  case MENU_REDRAW:
    menu_render();
    break;
  }
}

// Clears the panel and the LED for the blanked state. The carousel composes
// straight into the live framebuffer while it drags or settles, so the clear is
// repeated once it is idle again.
static void blank_panel(void) {
  oled_clear();
  oled_flush();
  led_set(0, 0, 0);
  s_blank_pending = carousel_busy();
}

static void display_set(bool off) {
  if (!off && atomic_load(&s_powered_down)) {
    return;
  }
  atomic_store(&s_display_off, off);
  if (!off) {
    s_blank_pending = false;
    s_dirty = true;
    if (!net_has_network()) {
      setup_show();  // blanking closed it
    }
    return;
  }
  if (menu_is_open()) {
    menu_hide();
  }
  blank_panel();
}

// A click on the power button: wakes a blanked panel, goes back in the menu, else
// blanks the panel.
static void handle_click(void) {
  if (atomic_load(&s_display_off)) {
    display_set(false);
    return;
  }
  if (!menu_is_open()) {
    display_set(true);
    return;
  }
  menu_act(menu_back());
  if (menu_is_open()) {
    menu_touched();  // after the page change, which may have left the setup page
  }
}

// A hold on the power button: opens the menu, on a blanked panel too, but not on
// one the power-off path holds dark. An open menu stays on its page.
static void handle_menu(void) {
  if (atomic_load(&s_display_off)) {
    display_set(false);
  }
  if (!atomic_load(&s_display_off) && !carousel_busy() && !menu_is_open()) {
    menu_show();
  }
}

static void handle_display(display_cmd_t cmd) {
  if (cmd == DISPLAY_CLICK) {
    handle_click();
    return;
  }
  if (cmd == DISPLAY_MENU) {
    handle_menu();
    return;
  }
  display_set(true);
  if (cmd == DISPLAY_POWERING_OFF) {
    text_show_message("Powering off");
  } else if (cmd == DISPLAY_LOW_BATTERY) {
    text_show_message("Low battery");
  }
}

// ---- Page carousel hooks (carousel.c does the sliding) -------------------------

static int carousel_shown_idx(void) {
  return s_shown;
}

static int carousel_neighbour(int dir) {
  int idx = next_enabled(dir);
  return idx == s_shown ? -1 : idx;
}

static void carousel_render(int idx, uint8_t *fb) {
  render_offscreen(idx, fb, true);
}

static void carousel_commit(int idx) {
  show(idx);
}

static void carousel_led(int idx, uint8_t rgb[3]) {
  if (!plugin_led(idx, &rgb[0], &rgb[1], &rgb[2])) {
    rgb[0] = rgb[1] = rgb[2] = 0;
  }
}

// No log on a drop: the frame tick would print it fifty times a second.
static bool carousel_post(carousel_work_t work) {
  int32_t id = work == CAROUSEL_FRAME ? EV_FRAME : EV_REFRESH;
  bool queued = queue_send(id, NULL, 0, 0);
  if (!queued) {
    s_dropped++;  // the next tick asks again; only a frame is lost
  }
  return queued;
}

static const carousel_ops_t CAROUSEL_OPS = {
    .shown = carousel_shown_idx,
    .neighbour = carousel_neighbour,
    .render = carousel_render,
    .commit = carousel_commit,
    .led = carousel_led,
    .post = carousel_post,
};

// The screens array as long as the host's table: a new index starts with clean counters.
// False when there was no memory to grow it; the plugins past the end then stay off the
// screens until there is.
static bool screens_fit(void) {
  int n = plugins_count();
  if (n <= s_screen_n) {
    return true;
  }
  // Under the lock from the realloc on: GET /config reads the counters, and realloc may
  // free the block it reads from.
  screen_registry_lock();
  screen_t *grown = realloc(s_screens, n * sizeof(*grown));
  if (grown) {
    s_screens = grown;
    memset(&s_screens[s_screen_n], 0, (n - s_screen_n) * sizeof(*grown));
  }
  int *ids = grown ? realloc(s_enabled_ids, n * sizeof(*ids)) : NULL;
  if (ids) {
    s_enabled_ids = ids;
    s_screen_n = n;
  }
  screen_registry_unlock();
  if (!ids) {
    ESP_LOGE(TAG, "no memory for %d screens", n);
  }
  return ids != NULL;
}

// The first enabled screen, or -1.
static int first_enabled(void) {
  return s_enabled_n > 0 ? s_enabled_ids[0] : -1;
}

// The plugin at index idx changed, or the index is free now: it starts with clean
// counters and a new generation (so what was queued for the old one is dropped), its id
// may be in the enabled list, and on the panel the new plugin gets its SHOW (and starts
// its timers), or the panel moves on when the plugin was removed.
static void screen_changed(int idx) {
  if (idx < 0 || !screens_fit() || idx >= s_screen_n) {
    return;
  }
  screen_t *sc = &s_screens[idx];
  sc->gen++;
  sc->faults = 0;
  sc->preview_faults = 0;
  sc->disabled = false;
  call_begin(idx);
  plugins_start(idx);
  call_end(idx, "init");
  enabled_reload();
  s_neighbours_stale = true;
  if (s_shown == idx && plugins_used(idx)) {
    show(idx);
  } else if (s_shown == idx || s_shown < 0) {
    s_shown = -1;
    if (first_enabled() >= 0) {
      show(first_enabled());
    } else {
      s_dirty = true;  // nothing to show: the panel goes blank
    }
  }
}

// The screens a firmware before 1.0 listed in its settings object ("sys", "enabled"):
// moved once to their own record, which has no length of its own.
#define ENABLED_KEY "screens"

static void enabled_migrate(void) {
  size_t len = 0;
  if (settings_blob_get(ENABLED_KEY, NULL, &len) == ESP_OK) {
    return;
  }
  char old[512];
  settings_get("sys", "enabled", old, sizeof(old), "");
  cJSON *list = cJSON_Parse(old);
  if (cJSON_IsArray(list) && settings_blob_set(ENABLED_KEY, old, strlen(old)) == ESP_OK) {
    ESP_LOGI(TAG, "screens moved to their own record: %s", old);
  }
  cJSON_Delete(list);
}

static void boot(void) {
  text_init();  // the SDK's text layer, once, before any plugin draws
  enabled_migrate();
  const wasm_host_hooks_t hooks = {
      .timers_stop = timers_stop_owner,
      .lock = screen_registry_lock,
      .unlock = screen_registry_unlock,
  };
  wasm_host_init(&hooks);  // on this task: the runtime may only be used from a pthread
  screens_fit();
  // Every plugin starts now, shown or not, so one that publishes stats or lights the LED
  // from its settings does so from the boot on.
  for (int i = 0; i < s_screen_n; i++) {
    call_begin(i);
    plugins_start(i);
    call_end(i, "init");
  }
  enabled_reload();
  // Restore the screen last shown from the web; default to the first enabled one.
  char active[32];
  settings_get("sys", "active", active, sizeof(active), "");
  int idx = screen_index(active);
  idx = idx >= 0 && idx < s_screen_n ? idx : first_enabled();
  if (idx >= 0) {
    show(idx);
  }
  if (!net_has_network()) {
    setup_show();
  }
}

// Runs on the UI loop, so a screen is only ever drawn by the task that runs its
// callbacks; the off-screen target never overlaps a live draw.
// A plugin that is disabled, or in a web preview one that overran its previews, gets
// no callback at all on that path: neither draw nor led.
static bool preview_blank(int idx, bool web_preview) {
  if (idx < 0 || idx >= s_screen_n) {
    return true;
  }
  const screen_t *sc = &s_screens[idx];
  return sc->disabled || (web_preview && sc->preview_faults >= FAULT_LIMIT);
}

// `count` is false for a web preview: GET /screen?id= needs no token, so a host on
// the LAN could otherwise disable a plugin by asking for previews during flash
// erases, when wall time is not the plugin's own.
static void render_offscreen(int idx, uint8_t *fb, bool count) {
  s_count_faults = count;
  oled_render_to(fb);
  oled_clear();  // a disabled plugin, or one that overran its previews, shows a blank frame
  if (!preview_blank(idx, !count)) {
    call_begin(idx);
    plugins_draw(idx);
    call_end(idx, "draw");
  }
  oled_flush();
  oled_render_done();
  s_count_faults = true;
}

static void run_preview(preview_t *p) {
  if (p->live) {
    oled_copy_framebuffer(p->fb);
  } else {
    render_offscreen(p->idx, p->fb, false);
  }
  s_count_faults = false;
  p->led_on = !preview_blank(p->idx, !p->live) && plugin_led(p->idx, &p->r, &p->g, &p->b);
  s_count_faults = true;
  xSemaphoreGive(p->done);
  preview_release(p);
}

static void heartbeat_cb(void *arg) {
  post_event(EV_HEARTBEAT, NULL, 0, 0);
}

static void ui_handler(int32_t id, void *data) {
  if (s_enabled_dirty) {
    s_enabled_dirty = false;
    enabled_reload();
    s_neighbours_stale = true;
  }
  switch (id) {
  case EV_BOOT:
    s_ui_task = xTaskGetCurrentTaskHandle();
    // Subscribed here, on the loop's own task: a plugin callback that never returns
    // now ends in a panic with "ui" in the crash log instead of a silent freeze.
    // Without the heartbeat an idle loop would be the thing that trips it.
    if (!s_heartbeat_on || esp_task_wdt_add(NULL) != ESP_OK) {
      ESP_LOGE(TAG, "ui task not on the watchdog");
    }
    boot();
    break;
  case EV_HEARTBEAT: {
    web_manifest_tick(core_online());
    screen_changed(wasm_host_tick());
    night_tick();
    fw_manifest_tick(core_online());
    fw_update_tick();
    joined_check();
    static int beats;
    if (++beats >= FAULT_DECAY_S) {
      beats = 0;
      for (int i = 0; i < s_screen_n; i++) {
        screen_t *sc = &s_screens[i];
        if (sc->faults > 0 && !sc->disabled) {
          sc->faults--;
        }
        if (sc->preview_faults > 0) {
          sc->preview_faults--;
        }
      }
    }
    break;
  }
  case EV_REDRAW:
    s_dirty = s_shown >= 0;
    if (menu_is_open()) {
      menu_render();
    }
    break;
  case EV_SWITCH: {
    int idx = *(const int *) data;
    if (idx != s_shown) {
      show(idx);
    }
    break;
  }
  case EV_CONFIG: {
    int idx = *(const int *) data;
    if (idx < s_screen_n && !s_screens[idx].disabled) {
      call_begin(idx);
      plugins_config(idx);
      call_end(idx, "on_config");
    }
    if (idx == s_shown) {
      s_dirty = true;
    }
    break;
  }
  case EV_TIMER: {
    const timer_msg_t *m = data;
    core_event_t ev = {.type = CORE_EV_TIMER, .timer_id = m->id};
    if (answer_owner(m->owner) >= 0) {
      deliver(answer_owner(m->owner), &ev);
    }
    break;
  }
  case EV_NET: {
    net_msg_t *m = data;
    if (m->owner == NET_OWNER_CORE) {
      web_manifest_net_done(&m->net);
    } else if (m->owner == NET_OWNER_FW) {
      fw_manifest_net_done(&m->net);
    } else if (answer_owner(m->owner) >= 0) {
      core_event_t ev = {.type = CORE_EV_NET, .net = m->net};
      deliver(answer_owner(m->owner), &ev);
    }
    free(m->net.body);
    free(m->net.parsed);
    break;
  }
  case EV_PREVIEW:
    run_preview(*(preview_t *const *) data);
    break;
  case EV_MENU_FINGER: {
    const finger_msg_t *m = data;
    if (menu_is_open()) {
      menu_act(menu_finger(m->phase, m->pos, m->ms, net_online()));
    }
    if (menu_is_open()) {
      menu_touched();  // after the finger, which may have left the setup page
    }
    break;
  }
  case EV_MENU_TIMEOUT:
    if (menu_idle_blanks(net_has_network())) {
      display_set(true);
    } else if (!net_has_network()) {
      setup_show();
    } else if (menu_is_open()) {
      menu_hide();
    }
    break;
  case EV_DISPLAY:
    handle_display(*(const display_cmd_t *) data);
    break;
  case EV_WAKE:
    if (atomic_load(&s_display_off)) {
      display_set(false);
    }
    break;
  case EV_FRAME:
    carousel_frame();
    break;
  case EV_REFRESH:
    carousel_refresh();
    break;
  case EV_ENABLED:
    break;  // the reload above already ran for this event
  default:
    break;
  }
  if (s_disable_pending >= 0) {
    int idx = s_disable_pending;
    s_disable_pending = -1;
    disable_plugin(idx);
  }
  if (s_blank_pending && !carousel_busy()) {
    blank_panel();
  }
  bool panel_free = !carousel_busy() && !menu_is_open() && !atomic_load(&s_display_off);
  if (s_dirty && s_shown >= 0 && panel_free) {
    s_dirty = false;
    render(s_shown);
  }
  if (s_neighbours_stale && !carousel_busy()) {
    s_neighbours_stale = false;
    carousel_shown();  // after the shown screen is on the panel
  }
  esp_task_wdt_reset();  // after the work, so a handler that never returns is caught
}

// A hold on the strip does nothing; it is kept for later.
void core_hold(void) {
}

// From the touch task. A blanked panel takes the first finger as "wake"; an open
// menu takes the finger (the carousel still hears the release, so a touch that
// began before the menu opened ends for it too).
void core_touch(touch_phase_t phase, int pos) {
  if (atomic_load(&s_display_off)) {
    if (phase == TOUCH_PHASE_DOWN) {
      post(EV_WAKE, NULL, 0);
    }
    if (phase == TOUCH_PHASE_UP) {
      carousel_touch(phase, pos);
    }
    return;
  }
  if (atomic_load(&s_menu_owns_finger)) {
    finger_msg_t m = {
        .phase = phase,
        .pos = pos,
        .ms = (uint32_t) (esp_timer_get_time() / 1000),
    };
    post(EV_MENU_FINGER, &m, sizeof(m));
    if (phase == TOUCH_PHASE_UP) {
      carousel_touch(phase, pos);
    }
    return;
  }
  carousel_touch(phase, pos);
}

void screen_manager_button_click(void) {
  display_cmd_t cmd = DISPLAY_CLICK;
  post(EV_DISPLAY, &cmd, sizeof(cmd));
}

void screen_manager_button_hold(void) {
  display_cmd_t cmd = DISPLAY_MENU;
  post(EV_DISPLAY, &cmd, sizeof(cmd));
}

void screen_manager_set_power_off(void (*power_off)(void)) {
  s_power_off = power_off;
}

// The lock is set here, not on the loop, so a wake already queued cannot undo it.
static void power_down_post(display_cmd_t cmd) {
  atomic_store(&s_powered_down, true);
  for (int i = 0; i < 20; i++) {
    if (post(EV_DISPLAY, &cmd, sizeof(cmd))) {
      break;
    }
  }
}

void screen_manager_display_off(void) {
  power_down_post(DISPLAY_OFF);
}

void screen_manager_powering_off(void) {
  power_down_post(DISPLAY_POWERING_OFF);
}

void screen_manager_low_battery(void) {
  power_down_post(DISPLAY_LOW_BATTERY);
}

void screen_manager_set_version(const char *version) {
  s_version = version;
}

// The loop's own task. WASM plugins run on it, and the WASM runtime asks for
// pthread_self() on every call into a module, which IDF answers only on a task
// pthread_create made, so the loop runs on a pthread rather than its own task.
static void *ui_thread(void *arg) {
  ui_msg_t msg;
  while (true) {
    if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
      ui_handler(msg.id, &msg.data);
    }
  }
  return NULL;
}

static esp_err_t ui_thread_start(void) {
  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  cfg.stack_size = 8192;
  cfg.prio = 4;
  cfg.thread_name = "ui";
  cfg.pin_to_core = tskNO_AFFINITY;
  esp_err_t err = esp_pthread_set_cfg(&cfg);
  pthread_t thread;
  if (err == ESP_OK && pthread_create(&thread, NULL, ui_thread, NULL) != 0) {
    err = ESP_FAIL;
  }
  // The configuration belongs to the calling task; later threads it makes get the defaults.
  esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
  esp_pthread_set_cfg(&defaults);
  return err;
}

void screen_manager_start(void) {
  s_queue = xQueueCreate(UI_QUEUE_LEN, sizeof(ui_msg_t));
  esp_err_t err = s_queue ? ui_thread_start() : ESP_ERR_NO_MEM;
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ui loop create failed: %s", esp_err_to_name(err));
    s_queue = NULL;  // not deleted: an httpd handler may be posting into it
    return;
  }
  core_net_start(net_done);
  carousel_init(&CAROUSEL_OPS);
  night_init();
  const esp_timer_create_args_t mt = {.callback = menu_timer_cb, .name = "menu"};
  if (esp_timer_create(&mt, &s_menu_timer) != ESP_OK) {
    s_menu_timer = NULL;  // the menu then closes only by hand
  }
  const esp_timer_create_args_t hb = {.callback = heartbeat_cb, .name = "ui_hb"};
  s_heartbeat_on = esp_timer_create(&hb, &s_heartbeat) == ESP_OK &&
                   esp_timer_start_periodic(s_heartbeat, HEARTBEAT_US) == ESP_OK;
  post(EV_BOOT, NULL, 0);
}

const char *screen_manager_current_id(void) {
  // Only the UI loop runs callbacks; a stats or parse callback on another task
  // must not be keyed by whatever plugin the loop happens to be inside.
  if (xTaskGetCurrentTaskHandle() != s_ui_task) {
    return NULL;
  }
  return s_cur >= 0 ? plugins_id(s_cur) : NULL;
}

void screen_manager_redraw_shown(void) {
  post(EV_REDRAW, NULL, 0);
}

int screen_manager_ui_timeouts(void) {
  return s_ui_timeouts;
}

int screen_manager_frames_reposted(void) {
  return carousel_frames_reposted();
}

// ---- Shared with screen_http.c ----------------------------------------------

// The HTTP server is up before the UI loop, so the lock is made on first use; two
// tasks racing there both make one and the loser deletes its own.
static _Atomic(SemaphoreHandle_t) s_registry_mutex;

static SemaphoreHandle_t registry_mutex(void) {
  SemaphoreHandle_t m = atomic_load(&s_registry_mutex);
  if (m) {
    return m;
  }
  SemaphoreHandle_t fresh = xSemaphoreCreateRecursiveMutex();
  if (!fresh) {
    return NULL;  // no heap: this call goes unlocked, the next one tries again
  }
  SemaphoreHandle_t expected = NULL;
  if (!atomic_compare_exchange_strong(&s_registry_mutex, &expected, fresh)) {
    vSemaphoreDelete(fresh);
    return expected;
  }
  return fresh;
}

void screen_registry_lock(void) {
  SemaphoreHandle_t m = registry_mutex();
  if (m) {
    xSemaphoreTakeRecursive(m, portMAX_DELAY);
  }
}

void screen_registry_unlock(void) {
  SemaphoreHandle_t m = atomic_load(&s_registry_mutex);
  // A lock taken while the mutex could not be made held nothing to give back.
  if (m && xSemaphoreGetMutexHolder(m) == xTaskGetCurrentTaskHandle()) {
    xSemaphoreGiveRecursive(m);
  }
}

// Index of a screen by id, or -1. The active screen and the enabled list are stored by
// id, so they survive a plugin's index changing when it is deleted and installed again.
int screen_index(const char *id) {
  return plugins_find(id);
}

// The enabled-screen id list (a JSON array) from its record; with none stored, every
// installed plugin in table order, so a new device shows everything.
cJSON *screen_enabled_list(void) {
  size_t len = 0;
  char *text = NULL;
  if (settings_blob_get(ENABLED_KEY, NULL, &len) == ESP_OK && (text = malloc(len + 1))) {
    if (settings_blob_get(ENABLED_KEY, text, &len) == ESP_OK) {
      text[len] = '\0';
    } else {
      text[0] = '\0';
    }
  }
  cJSON *arr = text ? cJSON_Parse(text) : NULL;
  free(text);
  if (cJSON_IsArray(arr)) {
    return arr;
  }
  cJSON_Delete(arr);
  arr = cJSON_CreateArray();
  for (int i = 0; i < plugins_count(); i++) {
    plugin_info_t info;
    if (plugins_info(i, &info)) {
      cJSON_AddItemToArray(arr, cJSON_CreateString(info.id));
    }
  }
  return arr;
}

esp_err_t screen_enabled_store(const char *json) {
  return settings_blob_set(ENABLED_KEY, json, strlen(json));
}

bool screen_disabled(int idx) {
  return idx < s_screen_n && s_screens[idx].disabled;
}

int screen_faults(int idx) {
  return idx < s_screen_n ? s_screens[idx].faults : 0;
}

int screen_active(void) {
  return s_active;
}

int screen_shown(void) {
  return s_shown;
}

bool screen_switch(int idx) {
  if (!post(EV_SWITCH, &idx, sizeof(idx))) {
    return false;
  }
  s_active = idx;  // visible to GET /config at once; the UI loop swaps the screen next
  return true;
}

void screen_enabled_changed(void) {
  s_enabled_dirty = true;
  post(EV_ENABLED, NULL, 0);  // wakes the loop; the flag survives a dropped event
}

bool screen_config_changed(int idx) {
  return post(EV_CONFIG, &idx, sizeof(idx));
}

void preview_release(preview_t *p) {
  if (atomic_fetch_sub(&p->refs, 1) != 1) {
    return;
  }
  vSemaphoreDelete(p->done);
  free(p->fb);
  free(p);
}

preview_t *screen_preview(int idx, bool live, const char **error) {
  preview_t *p = calloc(1, sizeof(*p));
  uint8_t *fb = heap_large_malloc(OLED_FB_BYTES);
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (!p || !fb || !done) {
    free(p);
    free(fb);
    if (done) {
      vSemaphoreDelete(done);
    }
    *error = "oom";
    return NULL;
  }
  p->idx = idx;
  p->live = live;
  p->fb = fb;
  p->done = done;
  atomic_init(&p->refs, 1);
  if (idx < 0) {
    memset(fb, 0, OLED_FB_BYTES);  // before the first screen is shown the panel is blank
    return p;
  }
  // The UI loop draws and copies, so it is the only task that touches a screen's
  // state or the framebuffer. The loop's share of the request is taken before the
  // post, so a request the loop never gets is freed here.
  atomic_fetch_add(&p->refs, 1);
  if (!post_event(EV_PREVIEW, &p, sizeof(p), pdMS_TO_TICKS(500))) {
    preview_release(p);
    preview_release(p);
    *error = "busy";
    return NULL;
  }
  if (xSemaphoreTake(p->done, PREVIEW_WAIT_TICKS) != pdTRUE) {
    s_ui_timeouts++;
    ESP_LOGE(TAG, "ui loop did not answer /screen in time (%d so far)", s_ui_timeouts);
    preview_release(p);
    *error = "ui busy";
    return NULL;
  }
  return p;
}
