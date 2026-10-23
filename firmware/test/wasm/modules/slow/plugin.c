// A module that spends its time inside the core's imports, where the instruction
// limit does not count: a long string drawn again and again.
#include <string.h>
#include "plugin_wasm.h"

static char s_text[40000];

WASM_EXPORT("draw") void slow_draw(void) {
  memset(s_text, '8', sizeof(s_text) - 1);
  for (;;) {
    gfx_text_begin();
    gfx_text(0, 0, s_text);
    gfx_text_blit(15);
  }
}
