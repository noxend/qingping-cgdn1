// A module that hands gfx_blit a picture outside its own memory: the binding must stop
// the module before the core reads it.
#include "plugin_wasm.h"

WASM_EXPORT("draw") void blitbad_draw(void) {
  static const uint8_t palette[2] = {0, 15};
  // Through a volatile: clang drops a constant it can see is out of range.
  volatile unsigned long outside = 0xfff0;
  gfx_blit(0, 0, 160, 128, (const void *) outside, 1, palette);
}
