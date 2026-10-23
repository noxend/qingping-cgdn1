// A module that hands the core a string outside its own memory: the import check
// must stop the call before the core reads it.
#include "plugin_wasm.h"

WASM_EXPORT("draw") void evil_draw(void) {
  gfx_clear();
  gfx_text_begin();
  gfx_text(0, 0, (const char *) 0x7ffffff0);
  gfx_text_blit(15);
}
