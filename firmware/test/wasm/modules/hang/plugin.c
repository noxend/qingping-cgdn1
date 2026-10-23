// A module that never returns from draw: the case the instruction limit is for.
#include "plugin_wasm.h"

WASM_EXPORT("draw") void hang_draw(void) {
  gfx_clear();
  volatile int spin = 0;
  for (;;) {
    spin++;
  }
}
