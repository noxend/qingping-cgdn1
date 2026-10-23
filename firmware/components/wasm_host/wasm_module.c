#include "wasm_module.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "wasm_binary.h"

static const char *const EXPORT_NAMES[WASM_FN_COUNT] = {
    "init", "on_event", "draw", "on_config", "net_buffer", "on_net", "led",
};

// The type each export must have: i32 parameters, i32 results. The runtime copies an
// export's results into the caller's argv whatever their number, so a mismatch would
// let a module write past it (or to NULL, where no argv is passed).
static const struct {
  uint8_t params;
  uint8_t results;
} EXPORT_TYPES[WASM_FN_COUNT] = {
    [WASM_FN_INIT] = {0, 0},      [WASM_FN_ON_EVENT] = {2, 0},   [WASM_FN_DRAW] = {0, 0},
    [WASM_FN_ON_CONFIG] = {0, 0}, [WASM_FN_NET_BUFFER] = {1, 1}, [WASM_FN_ON_NET] = {2, 0},
    [WASM_FN_LED] = {0, 1},
};

static bool all_i32(const wasm_valkind_t *kinds, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    if (kinds[i] != WASM_I32) {
      return false;
    }
  }
  return true;
}

static bool export_type_ok(wasm_module_inst_t inst, wasm_function_inst_t fn, wasm_fn_t which) {
  uint32_t params = wasm_func_get_param_count(fn, inst);
  uint32_t results = wasm_func_get_result_count(fn, inst);
  if (params != EXPORT_TYPES[which].params || results != EXPORT_TYPES[which].results) {
    return false;
  }
  wasm_valkind_t kinds[2];
  wasm_func_get_param_types(fn, inst, kinds);
  bool ok = all_i32(kinds, params);
  wasm_func_get_result_types(fn, inst, kinds);
  return ok && all_i32(kinds, results);
}

static int64_t s_deadline_us;  // of the call running now; calls never nest
static wasm_heap_t s_heap;

static void *heap_malloc(mem_alloc_usage_t usage, unsigned int size) {
  void *p = s_heap.alloc(size);
  if (p && usage == Alloc_For_LinearMemory) {
    memset(p, 0, size);  // a module's statics start at zero
  }
  return p;
}

// Linear memory grows by whole pages, so its old size is the allocation's size
// rounded down to a page; what the growth adds is zeroed.
static void *heap_realloc(mem_alloc_usage_t usage, bool full_size_mmaped, void *p,
                          unsigned int size) {
  bool linear = usage == Alloc_For_LinearMemory;
  size_t old = linear && p ? s_heap.size(p) / WASM_PAGE * WASM_PAGE : 0;
  void *q = s_heap.realloc(p, size);
  if (q && linear && size > old) {
    memset((char *) q + old, 0, size - old);
  }
  return q;
}

static void heap_free(mem_alloc_usage_t usage, void *p) {
  s_heap.free(p);
}

bool wasm_module_runtime_init(const wasm_heap_t *heap) {
  s_heap = *heap;
  RuntimeInitArgs args = {0};
  args.mem_alloc_type = Alloc_With_Allocator;
  args.mem_alloc_option.allocator.malloc_func = heap_malloc;
  args.mem_alloc_option.allocator.realloc_func = heap_realloc;
  args.mem_alloc_option.allocator.free_func = heap_free;
  return wasm_runtime_full_init(&args) && wasm_module_register_natives();
}

static int64_t now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

bool wasm_module_in_time(wasm_exec_env_t env) {
  if (now_us() <= s_deadline_us) {
    return true;
  }
  wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), "time limit exceeded");
  return false;
}

// Exports the runtime calls by itself while it instantiates, outside any limit.
static const char *const AUTORUN_EXPORTS[] = {"__post_instantiate", "__wasm_call_ctors",
                                              "_initialize"};

enum {
  SECTION_IMPORT = 2,
  SECTION_TABLE = 4,
  SECTION_MEMORY = 5,
  SECTION_EXPORT = 7,
  SECTION_START = 8,
};
// WAMR's own default table size (WASM_TABLE_MAX_SIZE). The runtime allocates a table's
// whole size when it instantiates the module and does not cap a declared one.
#define TABLE_MAX 1024
enum { KIND_FUNC = 0, KIND_TABLE = 1, KIND_MEMORY = 2, KIND_GLOBAL = 3 };

// Records why the module is refused and what it names, as printable ASCII.
static void refuse(wasm_mod_t *m, const char *reason, const uint8_t *name, uint32_t len) {
  m->reason = reason;
  size_t n = len < sizeof(m->reason_name) - 1 ? len : sizeof(m->reason_name) - 1;
  for (size_t i = 0; i < n; i++) {
    m->reason_name[i] = name[i] >= 0x20 && name[i] < 0x7f ? (char) name[i] : '?';
  }
  m->reason_name[n] = '\0';
}

// A length-prefixed name at *p: its bytes and length, *p moved past it.
static bool read_name(const uint8_t **p, const uint8_t *end, const uint8_t **name, uint32_t *len) {
  if (!wasm_leb_read(p, end, len) || *len > (uint32_t) (end - *p)) {
    return false;
  }
  *name = *p;
  *p += *len;
  return true;
}

// Table and memory limits: a flag, the minimum, and the maximum when the flag says so.
static bool read_limits(const uint8_t **p, const uint8_t *end, uint32_t *min, uint32_t *max) {
  if (*p >= end) {
    return false;
  }
  uint8_t flag = *(*p)++;
  *max = UINT32_MAX;
  return wasm_leb_read(p, end, min) && (!(flag & 1) || wasm_leb_read(p, end, max));
}

// Every import must be one of the SDK's functions (module "env").
static bool check_imports(wasm_mod_t *m, const uint8_t *p, const uint8_t *end) {
  uint32_t count;
  if (!wasm_leb_read(&p, end, &count)) {
    return true;  // malformed: the loader refuses it
  }
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *mod;
    const uint8_t *name;
    uint32_t mod_len;
    uint32_t name_len;
    if (!read_name(&p, end, &mod, &mod_len) || !read_name(&p, end, &name, &name_len) || p >= end) {
      return true;
    }
    uint8_t kind = *p++;
    bool env = mod_len == 3 && memcmp(mod, "env", 3) == 0;
    if (kind != KIND_FUNC || !env || !wasm_module_sdk_has((const char *) name, name_len)) {
      refuse(m, "import", name, name_len);
      return false;
    }
    uint32_t type;
    if (!wasm_leb_read(&p, end, &type)) {
      return true;
    }
  }
  return true;
}

// One memory whose maximum is its size, as build.sh links it: a module with no maximum,
// or a higher one, could take more PSRAM with memory.grow once it runs. How many pages
// it declares is for the caller to weigh against the PSRAM free (wasm_module_pages).
static bool check_memory(wasm_mod_t *m, const uint8_t *p, const uint8_t *end) {
  uint32_t count;
  uint32_t min;
  uint32_t max;
  if (!wasm_leb_read(&p, end, &count) || count == 0) {
    return true;
  }
  if (count > 1 || !read_limits(&p, end, &min, &max) || max != min) {
    refuse(m, "pages", NULL, 0);
    return false;
  }
  return true;
}

static bool find_pages(uint8_t id, const uint8_t *body, uint32_t size, void *ctx) {
  const uint8_t *p = body;
  const uint8_t *end = body + size;
  uint32_t count;
  uint32_t max;
  if (id != SECTION_MEMORY) {
    return true;
  }
  if (wasm_leb_read(&p, end, &count) && count == 1) {
    read_limits(&p, end, ctx, &max);
  }
  return false;
}

uint32_t wasm_module_pages(const uint8_t *code, uint32_t len) {
  uint32_t pages = 0;
  wasm_sections(code, len, find_pages, &pages);
  return pages;
}

// Every table at most TABLE_MAX entries, its maximum too: a few bytes of module could
// otherwise ask for megabytes of PSRAM.
static bool check_tables(wasm_mod_t *m, const uint8_t *p, const uint8_t *end) {
  uint32_t count;
  if (!wasm_leb_read(&p, end, &count)) {
    return true;
  }
  for (uint32_t i = 0; i < count; i++) {
    uint32_t min;
    uint32_t max;
    if (p >= end) {
      return true;
    }
    p++;  // the element type
    if (!read_limits(&p, end, &min, &max)) {
      return true;
    }
    if (min > TABLE_MAX || (max != UINT32_MAX && max > TABLE_MAX)) {
      refuse(m, "table", NULL, 0);
      return false;
    }
  }
  return true;
}

static bool check_exports(wasm_mod_t *m, const uint8_t *p, const uint8_t *end) {
  uint32_t count;
  if (!wasm_leb_read(&p, end, &count)) {
    return true;
  }
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *name;
    uint32_t len;
    if (!read_name(&p, end, &name, &len)) {
      return true;
    }
    for (size_t k = 0; k < sizeof(AUTORUN_EXPORTS) / sizeof(AUTORUN_EXPORTS[0]); k++) {
      if (strlen(AUTORUN_EXPORTS[k]) == len && memcmp(name, AUTORUN_EXPORTS[k], len) == 0) {
        refuse(m, "autorun", name, len);
        return false;
      }
    }
    if (p >= end) {
      return true;
    }
    p++;  // the kind
    uint32_t index;
    if (!wasm_leb_read(&p, end, &index)) {
      return true;
    }
  }
  return true;
}

static bool check_section(uint8_t id, const uint8_t *body, uint32_t size, void *ctx) {
  wasm_mod_t *m = ctx;
  const uint8_t *end = body + size;
  switch (id) {
  case SECTION_IMPORT:
    return check_imports(m, body, end);
  case SECTION_TABLE:
    return check_tables(m, body, end);
  case SECTION_MEMORY:
    return check_memory(m, body, end);
  case SECTION_EXPORT:
    return check_exports(m, body, end);
  case SECTION_START:
    refuse(m, "start", NULL, 0);
    return false;
  default:
    return true;
  }
}

// What the runtime would not refuse by itself, refused before it sees the module: a
// call outside the SDK, more memory than one page, a table past TABLE_MAX, and code
// that would run during instantiation, before the instruction and time limits apply (a
// start section, or an export the runtime calls by name). A malformed binary passes,
// for the loader to refuse.
static bool binary_ok(wasm_mod_t *m, const uint8_t *code, uint32_t len) {
  wasm_sections(code, len, check_section, m);
  return m->reason == NULL;
}

bool wasm_module_load(wasm_mod_t *m, uint8_t *code, uint32_t len) {
  memset(m, 0, sizeof(*m));
  m->instruction_limit = -1;
  if (!binary_ok(m, code, len)) {
    snprintf(m->error, sizeof(m->error), "refused: %s %s", m->reason, m->reason_name);
    return false;
  }
  m->module = wasm_runtime_load(code, len, m->error, sizeof(m->error));
  if (!m->module) {
    m->reason = "invalid";
    return false;
  }
  // No host-managed heap: a module has no malloc, its data lives in its linear memory.
  m->inst = wasm_runtime_instantiate(m->module, WASM_MODULE_STACK, 0, m->error, sizeof(m->error));
  if (m->inst) {
    m->env = wasm_runtime_create_exec_env(m->inst, WASM_MODULE_STACK);
  }
  if (!m->env) {
    m->reason = "instantiate";
    if (!m->error[0]) {
      snprintf(m->error, sizeof(m->error), "no memory for the execution environment");
    }
    wasm_module_unload(m);
    return false;
  }
  wasm_runtime_set_custom_data(m->inst, &m->policy);
  for (int i = 0; i < WASM_FN_COUNT; i++) {
    m->fn[i] = wasm_runtime_lookup_function(m->inst, EXPORT_NAMES[i]);
    if (m->fn[i] && !export_type_ok(m->inst, m->fn[i], (wasm_fn_t) i)) {
      snprintf(m->error, sizeof(m->error), "the export %s has the wrong type", EXPORT_NAMES[i]);
      refuse(m, "export_type", (const uint8_t *) EXPORT_NAMES[i], strlen(EXPORT_NAMES[i]));
      wasm_module_unload(m);
      return false;
    }
  }
  if (!m->fn[WASM_FN_DRAW]) {
    snprintf(m->error, sizeof(m->error), "the module exports no draw");
    m->reason = "no_draw";
    wasm_module_unload(m);
    return false;
  }
  return true;
}

bool wasm_module_copy_in(wasm_mod_t *m, uint32_t offset, const void *data, uint32_t len) {
  if (!wasm_runtime_validate_app_addr(m->inst, offset, len)) {
    // The check leaves an out-of-bounds exception on the instance, which would fail
    // the module's next call for something it did not do there.
    wasm_runtime_clear_exception(m->inst);
    return false;
  }
  memcpy(wasm_runtime_addr_app_to_native(m->inst, offset), data, len);
  return true;
}

bool wasm_module_trial(uint8_t *scratch, uint32_t len, wasm_refusal_t *why) {
  wasm_mod_t trial;
  bool loads = wasm_module_load(&trial, scratch, len);
  *why = (wasm_refusal_t){.reason = trial.reason};
  snprintf(why->name, sizeof(why->name), "%s", trial.reason_name);
  snprintf(why->text, sizeof(why->text), "%s", trial.error);
  wasm_module_unload(&trial);
  return loads;
}

bool wasm_module_call(wasm_mod_t *m, wasm_fn_t fn, uint32_t argc, uint32_t *argv) {
  if (!m->fn[fn]) {
    return true;
  }
  wasm_runtime_set_instruction_count_limit(m->env, m->instruction_limit);
  s_deadline_us = now_us() + WASM_CALL_TIME_US;
  if (wasm_runtime_call_wasm(m->env, m->fn[fn], argc, argv)) {
    return true;
  }
  const char *why = wasm_runtime_get_exception(m->inst);
  snprintf(m->error, sizeof(m->error), "%s: %s", EXPORT_NAMES[fn], why ? why : "trap");
  wasm_runtime_clear_exception(m->inst);
  return false;
}

void wasm_module_unload(wasm_mod_t *m) {
  if (m->env) {
    wasm_runtime_destroy_exec_env(m->env);
  }
  if (m->inst) {
    wasm_runtime_deinstantiate(m->inst);
  }
  if (m->module) {
    wasm_runtime_unload(m->module);
  }
  m->env = NULL;
  m->inst = NULL;
  m->module = NULL;
}
