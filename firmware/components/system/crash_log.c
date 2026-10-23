#include "crash_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_attr.h"
#include "esp_cpu_utils.h"
#include "esp_debug_helpers.h"
#include "esp_err.h"
#include "esp_private/panic_internal.h"
#include "esp_private/startup_internal.h"
#include "esp_private/system_internal.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "xtensa_context.h"

#define CRASH_MAGIC 0xC4A5C0DEu
#define PANIC_MAGIC 0x9A41C0DEu
#define TWDT_MAGIC 0x7D7C0DEu
#define CRASH_FRAMES 8

typedef struct {
  uint32_t magic;
  int core;
  uint32_t exccause;
  uint32_t excvaddr;
  int depth;
  uint32_t pc[CRASH_FRAMES];
  char reason[40];
  char task[16];
} crash_t;

// How far the last panic got: 1 the wrap was entered, 2 the record was written and
// IDF's handler called. Stage 1 after a reboot means the record walk itself died.
typedef struct {
  uint32_t magic;
  int stage;
  int abort;
  int up_s;
  char details[64];
} panic_mark_t;

// The tasks that had not fed the task watchdog when it fired, as the ISR lists them.
typedef struct {
  uint32_t magic;
  int up_s;
  int core;
  char names[96];
} twdt_mark_t;

static RTC_NOINIT_ATTR crash_t s_crash;
static RTC_NOINIT_ATTR panic_mark_t s_panic;
static RTC_NOINIT_ATTR twdt_mark_t s_twdt;
static DRAM_ATTR crash_t s_scratch;  // filled first, so a fault here leaves the old record intact

// The reset reason hint IDF's panic path leaves in an RTC register, read in the
// core init stage, which runs before the C constructors (esp_system/startup.c), where
// esp_reset_reason_init clears it.
static esp_reset_reason_t s_hint;

void __real_esp_panic_handler(panic_info_t *info);

ESP_SYSTEM_INIT_FN(read_reset_hint, CORE, BIT(0), 1) {
  s_hint = esp_reset_reason_get_hint();
  return ESP_OK;
}

// esp_timer_get_time is a lock-free register read on this target
// (esp_timer_impl_lac.c), so it is safe in the watchdog ISR and in the panic
// handler, where the other core is stalled and may hold any kernel spinlock.
static int IRAM_ATTR uptime_s(void) {
  return (int) (esp_timer_get_time() / 1000000);
}

static void IRAM_ATTR copy_str(char *dst, const char *src, int cap) {
  int i = 0;
  for (; src && src[i] && i < cap - 1; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

static void IRAM_ATTR append_str(char *dst, const char *src, int cap) {
  int n = 0;
  while (dst[n] && n < cap - 1) {
    n++;
  }
  copy_str(dst + n, src, cap - n);
}

// ---- task watchdog ----------------------------------------------------------

static DRAM_ATTR int s_twdt_lines;

// IDF sends the caption first, then "\n - ", the name and the CPU of each task.
static void IRAM_ATTR twdt_line(void *opaque, const char *msg) {
  twdt_mark_t *m = opaque;
  s_twdt_lines++;
  if (s_twdt_lines == 1) {
    return;
  }
  const char *text = msg[0] == '\n' ? msg + 1 : msg;
  append_str(m->names, text, sizeof(m->names));
}

// Runs inside the task watchdog ISR, before IDF aborts (CONFIG_ESP_TASK_WDT_PANIC):
// the only point that still knows which tasks starved. Same rules as any ISR, no
// logging; the list comes from IDF's own iteration over its entries.
void IRAM_ATTR esp_task_wdt_isr_user_handler(void) {
  s_twdt.magic = 0;
  s_twdt.up_s = uptime_s();
  s_twdt.core = xPortGetCoreID();
  s_twdt.names[0] = '\0';
  s_twdt_lines = 0;
  esp_task_wdt_print_triggered_tasks(twdt_line, &s_twdt, NULL);
  s_twdt.magic = TWDT_MAGIC;
}

// ---- panic ------------------------------------------------------------------

// Runs inside the panic, after IDF has re-enabled the flash cache (panicHandler in
// esp_system/port/panic_handler.c), so reading the reason text and the task name is
// as safe as IDF's own print of them. The record in RTC memory is replaced only once
// the new one is complete: a second fault while this runs keeps the previous crash.
// The stage mark is written first, so a panic that never completes still leaves a trace.
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info) {
  s_panic.magic = PANIC_MAGIC;
  s_panic.stage = 1;
  s_panic.abort = g_panic_abort ? 1 : 0;
  s_panic.up_s = uptime_s();
  copy_str(s_panic.details, g_panic_abort ? g_panic_abort_details : "", sizeof(s_panic.details));
  crash_t *c = &s_scratch;
  c->magic = 0;
  c->core = info->core;
  c->depth = 0;
  c->exccause = 0;
  c->excvaddr = 0;
  copy_str(c->reason, info->reason, sizeof(c->reason));
  TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(info->core);
  copy_str(c->task, t ? pcTaskGetName(t) : "", sizeof(c->task));
  const XtExcFrame *f = info->frame;
  if (f) {
    c->exccause = f->exccause;
    c->excvaddr = f->excvaddr;
    esp_backtrace_frame_t fr = {
        .pc = f->pc, .sp = f->a1, .next_pc = f->a0, .exc_frame = (void *) f};
    c->pc[c->depth++] = esp_cpu_process_stack_pc(fr.pc);
    while (c->depth < CRASH_FRAMES && fr.next_pc && esp_backtrace_get_next_frame(&fr)) {
      c->pc[c->depth++] = esp_cpu_process_stack_pc(fr.pc);
    }
  }
  s_crash = *c;
  s_crash.magic = CRASH_MAGIC;
  s_panic.stage = 2;
  __real_esp_panic_handler(info);
}

// ---- JSON -------------------------------------------------------------------

#define CRASH_FORMAT                                                                               \
  "{\"reason\":\"%s\",\"task\":\"%s\",\"core\":%d,\"exccause\":%lu,\"excvaddr\":\"0x%08lx\","      \
  "\"bt\":["

int crash_log_json(char *out, int cap) {
  if (s_crash.magic != CRASH_MAGIC) {
    return snprintf(out, cap, "null");
  }
  int n = snprintf(out, cap, CRASH_FORMAT, s_crash.reason, s_crash.task, s_crash.core,
                   (unsigned long) s_crash.exccause, (unsigned long) s_crash.excvaddr);
  for (int i = 0; i < s_crash.depth && i < CRASH_FRAMES && n < cap; i++) {
    n += snprintf(out + n, cap - n, "%s\"0x%08lx\"", i ? "," : "", (unsigned long) s_crash.pc[i]);
  }
  if (n < cap) {
    n += snprintf(out + n, cap - n, "]}");
  }
  return n < cap ? n : cap - 1;
}

// Appends up to src_cap bytes of src to out as JSON string content: quotes,
// backslashes and control bytes become '?'. Never passes cap - 1 and keeps the
// output a C string; the source bound covers RTC memory left unterminated by a
// reset in the middle of a write.
static void put_text(char *out, int *n, int cap, const char *src, int src_cap) {
  for (int i = 0; i < src_cap && src[i] && *n < cap - 1; i++) {
    unsigned char ch = (unsigned char) src[i];
    bool plain = ch >= 0x20 && ch != '"' && ch != '\\';
    out[(*n)++] = plain ? (char) ch : '?';
  }
  out[*n] = '\0';
}

static void put_fmt(char *out, int *n, int cap, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void put_fmt(char *out, int *n, int cap, const char *fmt, ...) {
  if (*n >= cap - 1) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(out + *n, cap - *n, fmt, ap);
  va_end(ap);
  int room = cap - 1 - *n;
  *n += w < room ? w : room;
}

int crash_log_diag_json(char *out, int cap) {
  int n = 0;
  int raw = (int) esp_rom_get_reset_reason(0);
  put_fmt(out, &n, cap, ",\"rst_raw\":%d,\"rst_hint\":%d,\"twdt\":", raw, (int) s_hint);
  if (s_twdt.magic == TWDT_MAGIC) {
    put_fmt(out, &n, cap, "{\"up_s\":%d,\"core\":%d,\"names\":\"", s_twdt.up_s, s_twdt.core);
    put_text(out, &n, cap, s_twdt.names, sizeof(s_twdt.names));
    put_fmt(out, &n, cap, "\"}");
  } else {
    put_fmt(out, &n, cap, "null");
  }
  put_fmt(out, &n, cap, ",\"panic\":");
  if (s_panic.magic == PANIC_MAGIC) {
    put_fmt(out, &n, cap, "{\"stage\":%d,\"abort\":%d,\"up_s\":%d,\"details\":\"", s_panic.stage,
            s_panic.abort, s_panic.up_s);
    put_text(out, &n, cap, s_panic.details, sizeof(s_panic.details));
    put_fmt(out, &n, cap, "\"}");
  } else {
    put_fmt(out, &n, cap, "null");
  }
  return n;
}
