// One WebAssembly plugin module: load, call its exports under an instruction limit,
// unload. Knows nothing of the plugin table, the network or the screens, so the host test
// (firmware/test/wasm) runs this file unchanged on the Mac.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wasm_export.h"

// The functions a module may export (plugins/sdk/plugin_wasm.h). Only draw
// is required.
typedef enum {
  WASM_FN_INIT,
  WASM_FN_ON_EVENT,  // (type, timer_id)
  WASM_FN_DRAW,
  WASM_FN_ON_CONFIG,
  WASM_FN_NET_BUFFER,  // (len) -> where in its memory the module takes a body of len
  WASM_FN_ON_NET,      // (status, len): the answer to its core_net_get is in that buffer
  WASM_FN_LED,         // () -> 0xRRGGBB, or -1 for the LED off
  WASM_FN_COUNT,
} wasm_fn_t;

// What a module may reach on the network: the hosts its manifest names. The imports
// read it through the instance; the slot that runs the module fills it.
#define WASM_HOSTS_MAX 4

typedef struct {
  char hosts[WASM_HOSTS_MAX][64];
  int host_count;
  int net_pending;  // requests made and not answered yet
} wasm_policy_t;

// A module's request body is capped here, whatever it asks for.
#define WASM_NET_MAX (32 * 1024)

// A module's linear memory: whole pages, as many as it declares, never grown.
#define WASM_PAGE (64 * 1024)

// The module's operand and call stack, allocated by the runtime beside the instance.
#define WASM_MODULE_STACK 8192

// Instructions one callback may run before it is stopped (set from the host test's
// measurement, firmware/test/wasm).
#define WASM_INSTRUCTION_LIMIT 50000

// Wall time one callback may take, imports included: the instruction limit does not
// see the time a module spends inside gfx_text. Under the core's 250 ms budget.
#define WASM_CALL_TIME_US (200 * 1000)

typedef struct {
  wasm_module_t module;
  wasm_module_inst_t inst;
  wasm_exec_env_t env;
  wasm_function_inst_t fn[WASM_FN_COUNT];
  int instruction_limit;  // per call; -1 for none
  char error[128];        // why the last load or call failed, in the runtime's words
  // Why the last load refused the module, as a code the web page words ("import",
  // "pages", "start", "autorun", "export_type", "no_draw", "invalid", "instantiate"),
  // and the name it is about (the import or export), or NULL and "".
  const char *reason;
  char reason_name[48];
  wasm_policy_t policy;  // the imports find it as the instance's custom data
} wasm_mod_t;

// The heap the runtime takes everything from (PSRAM on the device). size is what an
// allocation holds, at least what was asked.
typedef struct {
  void *(*alloc)(size_t size);
  void *(*realloc)(void *p, size_t size);
  void (*free)(void *p);
  size_t (*size)(void *p);
} wasm_heap_t;

// Starts the runtime on that heap and registers the core's imports
// (wasm_bindings.c). A module's linear memory comes out zeroed, as WebAssembly
// requires; the runtime leaves that to the allocator when it does not map it itself.
bool wasm_module_runtime_init(const wasm_heap_t *heap);

// Registers the core's imports; wasm_module_runtime_init does it.
bool wasm_module_register_natives(void);

// True when name (len bytes, not terminated) is one of the SDK's functions.
bool wasm_module_sdk_has(const char *name, size_t len);

// Loads `code` (WebAssembly binary, kept alive by the caller until unload: the
// runtime points into it) and instantiates it. False with m->error and m->reason set.
// Refused before the runtime sees it: an import that is not an SDK function, memory that
// can grow, a start function or an export the runtime would run at instantiation,
// before any limit. The runtime rewrites opcodes inside `code` as it loads
// (wasm_export.h, "may change its content"), so a buffer is loaded once: a second load
// from it fails, and it is no longer the bytes that were checked.
bool wasm_module_load(wasm_mod_t *m, uint8_t *code, uint32_t len);

// Why a module does not load: wasm_mod_t's reason and reason_name, and its error text.
typedef struct {
  const char *reason;
  char name[48];
  char text[128];
} wasm_refusal_t;

// The pages of linear memory the module declares, from its memory section, without
// loading it; 0 when it has none or the section does not read.
uint32_t wasm_module_pages(const uint8_t *code, uint32_t len);

// Whether `scratch` (a copy of the module, its bytes spent by the load) loads and
// instantiates; why not in *why when it does not.
bool wasm_module_trial(uint8_t *scratch, uint32_t len, wasm_refusal_t *why);

// For the imports (wasm_bindings.c): false once the running call is past its
// WASM_CALL_TIME_US, with the call marked to stop as soon as the import returns, so
// the import must skip its work.
bool wasm_module_in_time(wasm_exec_env_t env);

// Copies len bytes into the module's memory at offset, when that range is inside it.
bool wasm_module_copy_in(wasm_mod_t *m, uint32_t offset, const void *data, uint32_t len);

// Calls an export with up to two i32 arguments; a result comes back in argv[0]. A
// module without that export is fine (true), and argv is left as it was. False when
// the call trapped: out of instructions, out of bounds, a bad import argument;
// m->error says which, and the module should be unloaded.
bool wasm_module_call(wasm_mod_t *m, wasm_fn_t fn, uint32_t argc, uint32_t *argv);

void wasm_module_unload(wasm_mod_t *m);
