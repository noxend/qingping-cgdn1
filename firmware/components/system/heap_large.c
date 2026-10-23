#include "heap_internal.h"

#include "cJSON.h"
#include "mbedtls/platform.h"

void heap_large_install(void) {
  mbedtls_platform_set_calloc_free(heap_large_calloc, heap_caps_free);
  cJSON_Hooks hooks = {
      .malloc_fn = heap_large_malloc,
      .free_fn = heap_caps_free,
  };
  cJSON_InitHooks(&hooks);
}
