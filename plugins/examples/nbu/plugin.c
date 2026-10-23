// The National Bank of Ukraine's official rate of one currency to the hryvnia, from
// bank.gov.ua, refreshed every half hour while it is shown. The example of a plugin
// that uses the network (core_net_get, then net_buffer and on_net) and reads JSON with
// the SDK's json_ calls.
#include <stdio.h>
#include <string.h>
#include "plugin_wasm.h"

#define TIMER_REFRESH 1
#define REFRESH_MIN 30
#define REFRESH_MS (REFRESH_MIN * 60 * 1000)

typedef enum {
  RATE_NONE,     // nothing asked yet
  RATE_LOADING,  // asked, no answer
  RATE_SHOWN,    // s_rate and s_date hold the answer
  RATE_FAILED,   // the last answer was no good
} rate_state_t;

static char s_currency[8] = "USD";  // as the bank spells it
static rate_state_t s_state = RATE_NONE;
static bool s_shown;
static bool s_waiting;    // a request is out (the core takes one at a time)
static bool s_refetch;    // another was wanted meanwhile: ask once the answer is in
static int s_fetched_at;  // minutes() of the last good answer
static char s_body[1024];
static char s_rate[16];  // "45.03"
static char s_date[16];  // "22.10.2026"

// Minutes on a running count, for the age of the shown rate (a year turning over
// only makes the next show refetch).
static int minutes(void) {
  wasm_tm_t t;
  core_localtime(&t, sizeof(t));
  return (t.yday * 24 + t.hour) * 60 + t.min;
}

static void fetch(void) {
  if (s_waiting) {
    s_refetch = true;
    return;
  }
  char url[128];
  snprintf(url, sizeof(url),
           "https://bank.gov.ua/NBUStatService/v1/statdirectory/exchange?valcode=%s&json",
           s_currency);
  s_waiting = core_net_get(url, sizeof(s_body) - 1);
  if (s_state != RATE_SHOWN || !s_waiting) {
    s_state = s_waiting ? RATE_LOADING : RATE_FAILED;  // a shown rate stays until replaced
  }
  core_redraw();
}

// The setting keeps the option's key ("usd"); the bank wants the code in capitals.
WASM_EXPORT("on_config") void nbu_on_config(void) {
  char v[8];
  plugin_setting_get("currency", v, sizeof(v), "usd");
  for (char *c = v; *c; c++) {
    *c = *c >= 'a' && *c <= 'z' ? (char) (*c - 'a' + 'A') : *c;
  }
  if (strcmp(v, s_currency) != 0) {
    snprintf(s_currency, sizeof(s_currency), "%s", v);
    s_state = RATE_NONE;
    if (s_shown) {
      fetch();
    }
  }
}

WASM_EXPORT("init") void nbu_init(void) {
  nbu_on_config();
}

WASM_EXPORT("on_event") void nbu_on_event(int type, int timer_id) {
  switch (type) {
  case CORE_EV_SHOW:
    s_shown = true;
    if (s_state != RATE_SHOWN || minutes() - s_fetched_at >= REFRESH_MIN) {
      fetch();
    }
    core_timer_start(TIMER_REFRESH, REFRESH_MS, true);
    break;
  case CORE_EV_HIDE:
    s_shown = false;
    core_timer_stop(TIMER_REFRESH);
    break;
  case CORE_EV_TIMER:
    fetch();
    break;
  default:
    break;
  }
}

WASM_EXPORT("net_buffer") char *nbu_net_buffer(int len) {
  return len < (int) sizeof(s_body) ? s_body : NULL;
}

// "45.0256" as hundredths, rounded half up: 4503. False when it is not a number.
static bool parse_hundredths(const char *s, unsigned *out) {
  unsigned whole = 0;
  int digits = 0;
  for (; *s >= '0' && *s <= '9'; s++, digits++) {
    whole = whole * 10 + (unsigned) (*s - '0');
  }
  unsigned thousandths = 0;
  if (*s == '.') {
    s++;
    for (int k = 0; k < 3; k++) {
      unsigned d = *s >= '0' && *s <= '9' ? (unsigned) (*s++ - '0') : 0;
      thousandths = thousandths * 10 + d;
    }
  }
  if (digits == 0) {
    return false;
  }
  *out = whole * 100 + (thousandths + 5) / 10;
  return true;
}

static void answered(void);

WASM_EXPORT("on_net") void nbu_on_net(int status, int len) {
  s_waiting = false;
  if (status != 200 || len <= 0 || len >= (int) sizeof(s_body)) {
    s_state = RATE_FAILED;
    answered();
    return;
  }
  s_body[len] = '\0';
  // [{"r030":840,"txt":"...","rate":45.0256,"cc":"USD","exchangedate":"22.10.2026"}]
  int entry = json_first(s_body, len, 0);
  int rate = json_find(s_body, len, entry, "rate");
  char cc[8];
  unsigned hundredths;
  bool ok = json_string(s_body, len, json_find(s_body, len, entry, "cc"), cc, sizeof(cc)) > 0 &&
            strcmp(cc, s_currency) == 0 && rate >= 0 &&
            parse_hundredths(s_body + rate, &hundredths) &&
            json_string(s_body, len, json_find(s_body, len, entry, "exchangedate"), s_date,
                        sizeof(s_date)) > 0;
  if (ok) {
    snprintf(s_rate, sizeof(s_rate), "%u.%02u", hundredths / 100, hundredths % 100);
    s_fetched_at = minutes();
  }
  s_state = ok ? RATE_SHOWN : RATE_FAILED;
  answered();
}

// After an answer: draw it, and ask again when a fetch was wanted meanwhile (the
// currency changed while the old one was on its way).
static void answered(void) {
  core_redraw();
  if (s_refetch) {
    s_refetch = false;
    fetch();
  }
}

// The standard screen: the pair as the caption, the rate, and under it the date the
// rate is for, or why there is none.
WASM_EXPORT("draw") void nbu_draw(void) {
  static const char *const NOTES[] = {"", "loading", "", "no data"};
  bool shown = s_state == RATE_SHOWN;
  char pair[24];
  snprintf(pair, sizeof(pair), "%s / UAH", s_currency);
  gfx_clear();
  ui_caption(pair);
  ui_value(shown ? s_rate : NULL);
  ui_label(shown ? s_date : NOTES[s_state]);
}
