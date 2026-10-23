// A test module that gets its answers wrong on purpose. Asked for a buffer, it names
// one outside its memory (or, after that, traps). It shows what it was handed by
// asking again: a second request means on_net saw no body, as it must.
#include "plugin_wasm.h"

static int s_answers;

WASM_EXPORT("on_event") void bad_on_event(int type, int timer_id) {
  if (type == CORE_EV_SHOW) {
    core_net_get("https://bank.gov.ua/first", 512);
  }
}

WASM_EXPORT("net_buffer") char *bad_net_buffer(int len) {
  if (s_answers > 0) {
    __builtin_trap();
  }
  // Through a volatile: clang drops a return of a constant it can see is out of range.
  volatile unsigned long outside = 0x7ffffff0;
  return (char *) outside;
}

WASM_EXPORT("on_net") void bad_on_net(int status, int len) {
  s_answers++;
  if (len == 0) {
    core_net_get("https://bank.gov.ua/again", 512);
  }
}

WASM_EXPORT("draw") void bad_draw(void) {
  gfx_clear();
}
