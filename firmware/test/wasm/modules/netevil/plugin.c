// A test module whose manifest allows bank.gov.ua and which asks for everything else:
// a look-alike host, plain http, a port, credentials, and then the allowed host.
#include "plugin_wasm.h"

static const char *const URLS[] = {
    "https://bank.gov.ua.example/x",       "http://bank.gov.ua/x",
    "https://bank.gov.ua:8443/x",          "https://user@bank.gov.ua/x",
    "https://evil.example/?h=bank.gov.ua", "https://bank.gov.ua/allowed",
};

WASM_EXPORT("on_event") void evil_on_event(int type, int timer_id) {
  if (type != CORE_EV_SHOW) {
    return;
  }
  for (unsigned i = 0; i < sizeof(URLS) / sizeof(URLS[0]); i++) {
    core_net_get(URLS[i], 512);
  }
}

WASM_EXPORT("draw") void evil_draw(void) {
  gfx_clear();
}
