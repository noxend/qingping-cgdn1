// A module that asks the JSON reader about a text running past its memory: the runtime's
// check of the buffer must stop the module before the reader starts.
#include "plugin_wasm.h"

static char s_text[16] = "{\"a\":1}";

WASM_EXPORT("draw") void jsonbad_draw(void) {
  volatile int len = 70000;
  json_find(s_text, len, 0, "a");
}
