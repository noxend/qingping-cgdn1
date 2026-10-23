#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_sdk.h"
#include "qrcodegen.h"

// A swipe is a finger that travelled this far between down and up, a tap one that
// travelled less. The strip counts up toward the left of the screen (carousel.c), so
// a finger moving right lowers the count: that is "next", down the list.
#define SWIPE_MIN 40
// A tap is shorter than the touch driver's hold (TOUCH_HOLD_MS in touch.c).
#define TAP_MAX_MS 800

#define TITLE_TOP 4
#define RULE_Y 24
#define ROW_H 20
#define MENU_TOP 7   // first row of the menu itself: six rows and no title
#define LIST_TOP 30  // first row of a list page
#define INFO_TOP 26  // first row of the info page, five rows fit
#define TEXT_LEFT 4
#define LEVEL_TEXT 15
#define LEVEL_DIM 9
#define BAR_LEFT 2  // the selection bar stops short of the edge, where the glass ends

#define IDLE_US (30LL * 1000 * 1000)
#define SETUP_IDLE_US (5LL * 60 * 1000 * 1000)

typedef enum {
  PAGE_LIST,
  PAGE_INFO,
  PAGE_CONFIRM,
  PAGE_HOTSPOT,  // asks before starting the hotspot while the device is online
  PAGE_WIFI,
  PAGE_JOINED,  // after a save: the address the device got on the new network
} page_t;

enum {
  ITEM_POWER_OFF,
  ITEM_RESTART,
  ITEM_WIFI,
  ITEM_RESET,
  ITEM_INFO,
  ITEM_BACK,
  ITEM_COUNT,
};
enum { CONFIRM_NO, CONFIRM_YES, CONFIRM_COUNT };

static const char *const ITEMS[ITEM_COUNT] = {
    "Power off", "Restart", "Wi-Fi setup", "Factory reset", "Info", "Back",
};
static const char *const CONFIRM[CONFIRM_COUNT] = {"No", "Yes, reset"};
static const char *const HOTSPOT[CONFIRM_COUNT] = {"No", "Yes"};

static bool s_open;
static page_t s_page;
static int s_item;     // selected row of the list page
static int s_confirm;  // selected row of the confirm and hotspot pages
static bool s_down;
static int s_start;  // strip position at touch down
static uint32_t s_start_ms;
static bool s_wifi_text;  // the Wi-Fi page shows the hotspot as text, not as the QR code

void menu_open(void) {
  s_open = true;
  s_page = PAGE_LIST;
  s_item = ITEM_BACK;  // a stray tap only closes the menu
  s_down = false;
}

void menu_open_setup(void) {
  menu_open();
  s_page = PAGE_WIFI;
  s_wifi_text = false;
}

void menu_open_joined(void) {
  menu_open();
  s_page = PAGE_JOINED;
}

void menu_close(void) {
  s_open = false;
  s_down = false;
}

bool menu_is_open(void) {
  return s_open;
}

bool menu_idle_blanks(bool has_network) {
  return !has_network && s_open && s_page == PAGE_WIFI;
}

int64_t menu_idle_us(bool has_network) {
  return menu_idle_blanks(has_network) ? SETUP_IDLE_US : IDLE_US;
}

// Moves a selection by step inside [0, count), wrapping.
static int step_in(int selected, int step, int count) {
  return (selected + step + count) % count;
}

menu_action_t menu_back(void) {
  s_down = false;  // a finger on the strip lifts on the new page as no tap
  if (s_page == PAGE_LIST || s_page == PAGE_JOINED) {
    return MENU_CLOSE;
  }
  s_page = PAGE_LIST;
  return MENU_REDRAW;
}

static menu_action_t choose_item(bool online) {
  switch (s_item) {
  case ITEM_POWER_OFF:
    return MENU_POWER_OFF;
  case ITEM_RESTART:
    return MENU_RESTART;
  case ITEM_WIFI:
    if (online) {
      s_page = PAGE_HOTSPOT;
      s_confirm = CONFIRM_NO;
      return MENU_REDRAW;
    }
    s_page = PAGE_WIFI;
    s_wifi_text = false;
    return MENU_WIFI_SETUP;
  case ITEM_RESET:
    s_page = PAGE_CONFIRM;
    s_confirm = CONFIRM_NO;
    return MENU_REDRAW;
  case ITEM_INFO:
    s_page = PAGE_INFO;
    return MENU_REDRAW;
  default:
    return MENU_CLOSE;
  }
}

// A tap. The pages without a choice of rows (info, Wi-Fi, the address after a save)
// take it as their Back.
static menu_action_t choose(bool online) {
  switch (s_page) {
  case PAGE_LIST:
    return choose_item(online);
  case PAGE_CONFIRM:
    if (s_confirm == CONFIRM_YES) {
      return MENU_FACTORY_RESET;
    }
    return menu_back();
  case PAGE_HOTSPOT:
    if (s_confirm == CONFIRM_YES) {
      s_page = PAGE_WIFI;
      s_wifi_text = false;
      return MENU_WIFI_SETUP;
    }
    return menu_back();
  default:
    return menu_back();
  }
}

static menu_action_t swipe(int step) {
  switch (s_page) {
  case PAGE_LIST:
    s_item = step_in(s_item, step, ITEM_COUNT);
    return MENU_REDRAW;
  case PAGE_CONFIRM:
  case PAGE_HOTSPOT:
    s_confirm = step_in(s_confirm, step, CONFIRM_COUNT);
    return MENU_REDRAW;
  case PAGE_WIFI:
    s_wifi_text = !s_wifi_text;
    return MENU_REDRAW;
  default:
    return MENU_NONE;
  }
}

menu_action_t menu_finger(touch_phase_t phase, int pos, uint32_t ms, bool online) {
  if (phase == TOUCH_PHASE_DOWN) {
    s_down = true;
    s_start = pos;
    s_start_ms = ms;
    return MENU_NONE;
  }
  if (phase != TOUCH_PHASE_UP || !s_down) {
    return MENU_NONE;
  }
  s_down = false;
  int travel = pos - s_start;
  if (abs(travel) >= SWIPE_MIN) {
    return swipe(travel < 0 ? 1 : -1);
  }
  if (ms - s_start_ms >= TAP_MAX_MS) {
    return MENU_NONE;
  }
  return choose(online);
}

// ---- drawing -----------------------------------------------------------------

// Characters of the 9 px font that fit on one row.
#define ROW_CHARS 16

// Copies at most max_chars UTF-8 characters (not bytes) of src into dst.
static void copy_chars(char *dst, size_t cap, const char *src, int max_chars) {
  size_t n = 0;
  int chars = 0;
  while (src[n] && n + 1 < cap) {
    bool starts_char = (src[n] & 0xC0) != 0x80;
    if (starts_char) {
      if (chars == max_chars) {
        break;
      }
      chars++;
    }
    dst[n] = src[n];
    n++;
  }
  dst[n] = '\0';
}

static void draw_title(const char *title) {
  gfx_text_begin();
  gfx_font(GFX_FONT_TEXT);
  gfx_text_centered(TITLE_TOP, title);
  gfx_text_blit(LEVEL_TEXT);
  for (int x = TEXT_LEFT; x < GFX_W - TEXT_LEFT; x++) {
    gfx_pixel(x, RULE_Y, LEVEL_DIM);
  }
}

// A row of text; the selected one sits on a filled bar with the text cut out of it.
static void draw_row(int top, const char *text, bool selected) {
  if (selected) {
    for (int y = top - 3; y < top - 3 + ROW_H; y++) {
      for (int x = BAR_LEFT; x < GFX_W - BAR_LEFT; x++) {
        gfx_pixel(x, y, LEVEL_TEXT);
      }
    }
  }
  gfx_text_begin();
  gfx_font(GFX_FONT_TEXT);
  gfx_text(TEXT_LEFT, top, text);
  gfx_text_blit(selected ? 0 : LEVEL_TEXT);
}

static void draw_rows(int top, const char *const *rows, int count, int selected) {
  for (int i = 0; i < count; i++) {
    draw_row(top + i * ROW_H, rows[i], i == selected);
  }
}

static void draw_list(void) {
  draw_rows(MENU_TOP, ITEMS, ITEM_COUNT, s_item);
}

static void draw_info(const menu_info_t *info) {
  draw_title("Info");
  char version[40];
  char ip[40];
  char ssid[48];
  char uptime[40];
  snprintf(version, sizeof(version), "v%s", info->version ? info->version : "?");
  snprintf(ip, sizeof(ip), "%s", info->ip[0] ? info->ip : "No IP address");
  copy_chars(ssid, sizeof(ssid), info->ssid[0] ? info->ssid : "No Wi-Fi", ROW_CHARS);
  int hours = info->up_s / 3600;
  int minutes = (info->up_s / 60) % 60;
  snprintf(uptime, sizeof(uptime), "Up %d:%02d", hours, minutes);
  const char *const rows[] = {version, ip, ssid, uptime, "Back"};
  draw_rows(INFO_TOP, rows, 5, 4);
}

static void draw_confirm(void) {
  draw_title("Factory reset?");
  draw_rows(LIST_TOP, CONFIRM, CONFIRM_COUNT, s_confirm);
}

static void draw_hotspot(void) {
  draw_title("Start hotspot?");
  draw_rows(LIST_TOP, HOTSPOT, CONFIRM_COUNT, s_confirm);
}

// Writes the hotspot as a Wi-Fi QR payload, the format phone cameras join from
// (ZXing's "Barcode Contents"): backslash before \ ; , " and :. False when it does
// not fit in cap.
static bool wifi_qr_text(char *out, size_t cap, const char *ssid, const char *pass) {
  const char *const parts[] = {"WIFI:T:WPA;S:", ssid, ";P:", pass, ";;"};
  size_t n = 0;
  for (int i = 0; i < 5; i++) {
    bool escape = i == 1 || i == 3;
    for (const char *c = parts[i]; *c; c++) {
      if (escape && strchr("\\;,\":", *c)) {
        if (n + 1 >= cap) {
          return false;
        }
        out[n++] = '\\';
      }
      if (n + 1 >= cap) {
        return false;
      }
      out[n++] = *c;
    }
  }
  out[n] = '\0';
  return true;
}

// The QR code: version 3 (29 modules) holds the 46 bytes of a CGDN1-setup-XXXX
// hotspot with its 12-character password at the low error level. 3 px modules and a
// 3-module quiet zone make a 105 px square, the largest that leaves a text row under
// it; dark modules on a lit field, the polarity every phone camera reads.
#define QR_VERSION 3
#define QR_MODULE 3
#define QR_QUIET 3
#define QR_TOP 2
#define QR_TEXT_GAP 3

// Draws the code centred at the top; returns the height used, 0 when the text does
// not fit the version.
static int draw_qr(const char *text) {
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
  uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
  bool ok = qrcodegen_encodeText(text, scratch, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                                 QR_VERSION, qrcodegen_Mask_AUTO, true);
  if (!ok) {
    return 0;
  }
  int modules = qrcodegen_getSize(qr) + 2 * QR_QUIET;
  int side = modules * QR_MODULE;
  int left = (GFX_W - side) / 2;
  for (int my = 0; my < modules; my++) {
    for (int mx = 0; mx < modules; mx++) {
      bool dark = qrcodegen_getModule(qr, mx - QR_QUIET, my - QR_QUIET);
      int level = dark ? 0 : LEVEL_TEXT;
      for (int y = 0; y < QR_MODULE; y++) {
        for (int x = 0; x < QR_MODULE; x++) {
          gfx_pixel(left + mx * QR_MODULE + x, QR_TOP + my * QR_MODULE + y, level);
        }
      }
    }
  }
  return side;
}

// The setup hotspot as text: its name, password and the page to open.
static void draw_wifi_text(const menu_info_t *info) {
  draw_title("Wi-Fi setup");
  char pass[40];
  snprintf(pass, sizeof(pass), "Pass %s", info->ap_pass);
  const char *const rows[] = {info->ap_ssid, pass, "http://10.47.0.1/"};
  draw_rows(LIST_TOP, rows, 3, -1);
}

// The setup hotspot as a QR code with its name under it; a phone that scans it joins
// and opens the setup page by itself. A swipe shows the same as text.
static void draw_wifi(const menu_info_t *info) {
  char text[96];
  int side = 0;
  if (!s_wifi_text && wifi_qr_text(text, sizeof(text), info->ap_ssid, info->ap_pass)) {
    side = draw_qr(text);
  }
  if (side == 0) {
    draw_wifi_text(info);
    return;
  }
  gfx_text_begin();
  gfx_font(GFX_FONT_TEXT);
  gfx_text_centered(QR_TOP + side + QR_TEXT_GAP, info->ap_ssid);
  gfx_text_blit(LEVEL_TEXT);
}

// Where to open the device once it joined the network saved from the setup page.
static void draw_joined(const menu_info_t *info) {
  draw_title("Connected");
  const char *const rows[] = {info->host, info->ip[0] ? info->ip : "No IP address", "Back"};
  draw_rows(LIST_TOP, rows, 3, 2);
}

void menu_draw(const menu_info_t *info) {
  gfx_clear();
  switch (s_page) {
  case PAGE_LIST:
    draw_list();
    break;
  case PAGE_INFO:
    draw_info(info);
    break;
  case PAGE_CONFIRM:
    draw_confirm();
    break;
  case PAGE_HOTSPOT:
    draw_hotspot();
    break;
  case PAGE_WIFI:
    draw_wifi(info);
    break;
  case PAGE_JOINED:
    draw_joined(info);
    break;
  }
}
