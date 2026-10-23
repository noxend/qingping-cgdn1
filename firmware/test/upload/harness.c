// Host check of components/wasm_host/wasm_host.c: the standard plugins installed at the
// first boot, the HTTP task handing an upload or a standard plugin to the UI loop's tick,
// GET /plugins reporting how it ended, deletion, the settings an older firmware stored,
// standard plugins whose stored module stopped reading, and installing until the storage
// is full. The real wasm_host.c, plugin table, store, manifest check, loader and bindings
// run over WAMR and the in-memory NVS of test/store, which run.sh keeps in a file between
// boots; each scenario is one boot. The modules are the clock under other ids, built by
// build.sh.
//
// Usage: harness <modules dir> <nvs file> <scenario>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "plugin_sdk.h"
#include "settings.h"
#include "util.h"
#include "wasm_host.h"
#include "wasm_plugins.h"
#include "wasm_store.h"

void host_sdk_init(void);
void fake_nvs_reset(int pages);
void fake_nvs_save(const char *path);
bool fake_nvs_load(const char *path);
extern uint8_t host_frame[GFX_H][GFX_W];

int host_heap_caps_frees;

static int s_failed;

static void check(bool ok, const char *what) {
  printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    s_failed++;
  }
}

// ---- the HTTP side, as the device's server would call it ---------------------------

static httpd_uri_t s_uris[8];
static int s_uri_n;

esp_err_t http_register(httpd_handle_t srv, const httpd_uri_t *uri) {
  (void) srv;
  s_uris[s_uri_n++] = *uri;
  return ESP_OK;
}

bool auth_diag_allowed(httpd_req_t *req) {
  (void) req;
  return true;
}

bool recv_exact(httpd_req_t *req, char *buf, int len) {
  if (req->read + len > req->content_len) {
    return false;
  }
  memcpy(buf, req->body + req->read, len);
  req->read += len;
  return true;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val,
                                      size_t val_size) {
  if (strcmp(field, "Content-Type") != 0 || !req->content_type) {
    return ESP_FAIL;
  }
  snprintf(val, val_size, "%s", req->content_type);
  return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status) {
  snprintf(req->status, sizeof(req->status), "%s", status);
  return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) {
  (void) req;
  (void) type;
  return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, ssize_t len) {
  if (!buf || len == 0) {
    return ESP_OK;
  }
  req->answer = realloc(req->answer, req->answer_len + len + 1);
  memcpy(req->answer + req->answer_len, buf, len);
  req->answer_len += len;
  req->answer[req->answer_len] = '\0';
  return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str) {
  req->answer_len = 0;
  return httpd_resp_send_chunk(req, str, (ssize_t) strlen(str));
}

esp_err_t http_error(httpd_req_t *req, httpd_err_code_t code, const char *msg) {
  const char *status = "500 Internal Server Error";
  if (code == HTTPD_400_BAD_REQUEST) {
    status = "400 Bad Request";
  } else if (code == HTTP_409_CONFLICT) {
    status = "409 Conflict";
  } else if (code == HTTP_429_TOO_MANY) {
    status = "429 Too Many Requests";
  }
  httpd_resp_set_status(req, status);
  char body[160];
  snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg);
  httpd_resp_sendstr(req, body);
  return ESP_FAIL;
}

typedef struct {
  const char *uri;
  httpd_method_t method;
  const char *content_type;
  const char *body;
  int len;
} request_t;

// The status code of one request to the handler for its uri; its answer, malloc'd, in
// *answer when given.
static int request(const request_t *r, char **answer) {
  httpd_req_t req = {
      .content_len = r->len,
      .body = r->body,
      .content_type = r->content_type,
  };
  snprintf(req.status, sizeof(req.status), "200 OK");
  bool handled = false;
  for (int i = 0; i < s_uri_n && !handled; i++) {
    handled = strcmp(s_uris[i].uri, r->uri) == 0 && s_uris[i].method == r->method;
    if (handled) {
      s_uris[i].handler(&req);
    }
  }
  if (!handled) {
    fprintf(stderr, "no handler for %s\n", r->uri);
    exit(2);
  }
  if (answer) {
    *answer = req.answer;
  } else {
    free(req.answer);
  }
  return atoi(req.status);
}

// ---- the modules -------------------------------------------------------------------

typedef struct {
  char *bytes;
  int len;
} module_t;

static const char *s_dir;

static module_t module(const char *name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.wasm", s_dir, name);
  module_t m = {0};
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(2);
  }
  fseek(f, 0, SEEK_END);
  m.len = (int) ftell(f);
  fseek(f, 0, SEEK_SET);
  m.bytes = malloc(m.len);
  if (fread(m.bytes, 1, m.len, f) != (size_t) m.len) {
    exit(2);
  }
  fclose(f);
  return m;
}

// POST /plugins/upload of a module, with the Content-Type given (NULL for none).
static int upload_as(const char *name, const char *content_type) {
  module_t m = module(name);
  const request_t r = {
      .uri = "/plugins/upload",
      .method = HTTP_POST,
      .content_type = content_type,
      .body = m.bytes,
      .len = m.len,
  };
  int status = request(&r, NULL);
  free(m.bytes);
  return status;
}

static int upload(const char *name) {
  return upload_as(name, "application/wasm");
}

static int post_id(const char *uri, const char *id) {
  char body[64];
  snprintf(body, sizeof(body), "{\"id\":\"%s\"}", id);
  const request_t r = {
      .uri = uri,
      .method = HTTP_POST,
      .content_type = "application/json",
      .body = body,
      .len = (int) strlen(body),
  };
  return request(&r, NULL);
}

// GET /plugins, parsed; the caller deletes it. NULL when it is not JSON.
static cJSON *plugins(void) {
  char *answer = NULL;
  const request_t r = {
      .uri = "/plugins",
      .method = HTTP_GET,
  };
  request(&r, &answer);
  cJSON *root = cJSON_Parse(answer);
  free(answer);
  return root;
}

static const char *job_text(cJSON *root, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItem(root, "job"), key);
  return cJSON_IsString(v) ? v->valuestring : "";
}

static int job_n(cJSON *root) {
  const cJSON *n = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItem(root, "job"), "n");
  return cJSON_IsNumber(n) ? n->valueint : -1;
}

static const cJSON *installed(cJSON *root, const char *id) {
  const cJSON *it = NULL;
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(root, "installed")) {
    if (strcmp(cJSON_GetObjectItem(it, "id")->valuestring, id) == 0) {
      return it;
    }
  }
  return NULL;
}

static int installed_n(cJSON *root) {
  return cJSON_GetArraySize(cJSON_GetObjectItem(root, "installed"));
}

static const char *field_of(cJSON *root, const char *id, const char *key) {
  const cJSON *it = installed(root, id);
  return it ? cJSON_GetObjectItem(it, key)->valuestring : "absent";
}

static const char *state_of(cJSON *root, const char *id) {
  return field_of(root, id, "state");
}

// One install through the HTTP side and the tick: the job's code, "" when it is done.
static const char *install_job(void) {
  static char code[32];
  wasm_host_tick();
  cJSON *root = plugins();
  snprintf(code, sizeof(code), "%s",
           strcmp(job_text(root, "stage"), "done") == 0 ? "" : job_text(root, "code"));
  cJSON_Delete(root);
  return code;
}

static int s_missing;

static bool count_missing(const char *id, const char *text, int len, void *ctx) {
  s_missing++;
  if (ctx && strcmp(id, ctx) == 0 && len > 0 && text[0] == '{') {
    ((char *) ctx)[0] = '\0';
  }
  return true;
}

// ---- the scenarios, one boot each --------------------------------------------------

// A device's first boot: the standard plugins installed, in their order.
static void first_boot(void) {
  cJSON *root = plugins();
  static const char *const ORDER[] = {"alerts", "clock", "humidity", "temperature", "co2"};
  bool all = installed_n(root) == 5;
  for (int k = 0; k < 5 && all; k++) {
    const cJSON *it = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "installed"), k);
    all = strcmp(cJSON_GetObjectItem(it, "id")->valuestring, ORDER[k]) == 0 &&
          strcmp(state_of(root, ORDER[k]), "ready") == 0;
  }
  check(all, "the first boot installs the five standard plugins, in order, running");
  cJSON_Delete(root);
  size_t len = 0;
  check(settings_blob_get("defaults", NULL, &len) == ESP_OK, "and records that it did");
  s_missing = 0;
  wasm_host_missing(count_missing, NULL);
  check(s_missing == 0, "no standard plugin is offered while all are installed");

  check(post_id("/plugins/remove", "clock") == 202, "deleting the clock is accepted");
  settings_set("clock", "seconds", "1");
  wasm_host_tick();
  root = plugins();
  size_t n = 0;
  check(installed_n(root) == 4 && !installed(root, "clock") &&
            settings_ns_blob_get("cfg", "clock", NULL, &n) != ESP_OK,
        "the tick deletes it, with its settings");
  cJSON_Delete(root);
  char wanted[16] = "clock";
  s_missing = 0;
  wasm_host_missing(count_missing, wanted);
  check(s_missing == 1 && wanted[0] == '\0',
        "a deleted standard plugin is offered with its manifest");
}

// The next boot: a deleted standard plugin stays deleted until it is installed again.
static void second_boot(void) {
  cJSON *root = plugins();
  check(installed_n(root) == 4 && !installed(root, "clock"),
        "after a reboot the deleted standard plugin stays deleted");
  cJSON_Delete(root);
  check(post_id("/plugins/install", "nope") == 400, "installing an unknown standard id: 400");
  check(post_id("/plugins/install", "clock") == 202, "installing the clock again is accepted");
  check(install_job()[0] == '\0', "and the tick installs it from the image");
  root = plugins();
  check(strcmp(state_of(root, "clock"), "ready") == 0, "it runs");
  cJSON_Delete(root);
}

// Settings an older firmware stored as labels, and a plugin it installed in format 1:
// the next boot stores the labels as keys and lists the old plugin as needing a rebuild.
static void older_firmware(void) {
  char value[32];
  settings_get("clock", "time_format", value, sizeof(value), "");
  bool time_key = strcmp(value, "12h") == 0;
  settings_get("clock", "date_format", value, sizeof(value), "");
  check(time_key && strcmp(value, "ymd") == 0,
        "labels an older firmware stored (\"12 годин (AM/PM)\") are stored as keys");
  settings_get("clock", "seconds", value, sizeof(value), "");
  check(strcmp(value, "1") == 0, "a value that is already right is left as it was");
  cJSON *root = plugins();
  check(strcmp(state_of(root, "old"), "failed") == 0 &&
            strcmp(field_of(root, "old", "error"), "old_sdk") == 0,
        "a format 1 plugin stays installed, listed as failed: old_sdk");
  cJSON_Delete(root);
}

// A plugin whose module went bad on flash, and the upload handover.
static void uploads(void) {
  cJSON *root = plugins();
  check(strcmp(state_of(root, "broken"), "failed") == 0 &&
            strcmp(field_of(root, "broken", "error"), "hash") == 0,
        "a damaged module is listed as failed: hash");
  cJSON_Delete(root);

  // Without its password the upload is kept from other sites' pages by its type: a
  // cross-site body without a preflight has none, or text/plain.
  check(upload_as("w0", NULL) == 400 && upload_as("w0", "text/plain") == 400,
        "an upload without Content-Type: application/wasm is refused");
  root = plugins();
  int before = job_n(root);
  cJSON_Delete(root);

  check(upload("w0") == 202, "an upload is accepted");
  check(upload("w1") == 409, "a second upload before the tick took the first is busy");
  check(install_job()[0] == '\0', "the tick installs the first");
  root = plugins();
  check(job_n(root) == before + 1 && strcmp(job_text(root, "id"), "w0") == 0 &&
            strcmp(state_of(root, "w0"), "ready") == 0 && !installed(root, "w1"),
        "the job reports it under its own number; the busy one is not installed");
  cJSON_Delete(root);
  check(upload("broken") == 202 && install_job()[0] == '\0',
        "the damaged plugin can be uploaded again");
  root = plugins();
  check(strcmp(state_of(root, "broken"), "ready") == 0, "and then runs");
  cJSON_Delete(root);
}

// Installs copies until the storage refuses one; then every plugin installed runs, its
// settings save, and GET /plugins lists them all.
static void until_full(void) {
  int kept = 0;
  const char *code = "";
  char id[16];
  for (int k = 0; k < 64; k++) {
    snprintf(id, sizeof(id), "w%d", k);
    int status = upload(id);
    if (status != 202) {
      code = status == 400 ? "space" : "status";
      break;
    }
    code = install_job();
    if (code[0]) {
      break;
    }
    kept++;
  }
  cJSON *root = plugins();
  int n = installed_n(root);
  printf("  %d uploads kept beside the standard plugins, then: %s\n", kept, code);
  check(kept > 8 && strcmp(code, "space") == 0,
        "more than eight install, then the storage refuses: space");
  bool all_run = true;
  const cJSON *it = NULL;
  cJSON_ArrayForEach(it, cJSON_GetObjectItem(root, "installed")) {
    all_run = all_run && strcmp(cJSON_GetObjectItem(it, "state")->valuestring, "ready") == 0;
  }
  check(root && n == kept + 5 && all_run, "GET /plugins lists every plugin, each running");
  cJSON_Delete(root);
  bool drawn = true;
  for (int i = 0; i < plugins_count(); i++) {
    memset(host_frame, 0, sizeof(host_frame));
    plugins_draw(i);
    int ink = 0;
    for (int y = 0; y < GFX_H; y++) {
      for (int x = 0; x < GFX_W; x++) {
        ink += host_frame[y][x] != 0;
      }
    }
    drawn = drawn && plugins_ready(i) && ink > 0;
  }
  check(drawn, "every one of them draws its frame");
  check(settings_set("w0", "seconds", "1") == ESP_OK &&
            settings_set("alerts", "region", "lviv") == ESP_OK,
        "settings still save with the storage full");
  check(post_id("/plugins/remove", "w0") == 202, "deleting one is accepted");
  wasm_host_tick();
  check(upload("w63") == 202 && install_job()[0] == '\0', "and its room takes another");
}

// ---- the core's hooks --------------------------------------------------------------

static void timers_stop(int index) {
  (void) index;
}

// Standard plugins whose stored module no longer reads, one gone from flash and one
// with a flipped bit: the boot puts back the image's copies, their settings kept.
static void unreadable_standard(void) {
  cJSON *root = plugins();
  check(strcmp(state_of(root, "co2"), "failed") == 0 &&
            strcmp(state_of(root, "humidity"), "failed") == 0,
        "standard plugins whose module does not read are listed failed at boot");
  cJSON_Delete(root);
  for (int t = 0; t < WASM_RESTORE_AFTER_TICKS - 1; t++) {
    wasm_host_tick();
  }
  root = plugins();
  check(strcmp(state_of(root, "co2"), "failed") == 0 &&
            strcmp(state_of(root, "humidity"), "failed") == 0,
        "and left alone while the board comes up");
  cJSON_Delete(root);
  check(post_id("/plugins/remove", "humidity") == 202, "one of them is deleted meanwhile");
  wasm_host_tick();  // the deletion
  wasm_host_tick();
  wasm_host_tick();
  root = plugins();
  check(strcmp(state_of(root, "co2"), "ready") == 0, "then the tick runs the other from the image");
  check(!installed(root, "humidity"), "and the deleted one stays deleted");
  cJSON_Delete(root);
  char value[8];
  settings_get("co2", "alarm", value, sizeof(value), "");
  check(strcmp(value, "1") == 0, "and it keeps its settings");
  uint8_t *code = NULL;
  uint32_t len = 0;
  bool co2 = store_read("co2", &code, &len, malloc, free) == STORE_OK;
  free(code);
  check(co2, "and its module reads from flash again");
}

static void nothing(void) {
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: harness <modules dir> <nvs file> <scenario>\n");
    return 2;
  }
  s_dir = argv[1];
  const char *nvs = argv[2];
  const char *scenario = argv[3];
  host_sdk_init();
  if (!fake_nvs_load(nvs)) {
    fake_nvs_reset(48);
  }
  if (strcmp(scenario, "older_firmware") == 0) {
    // What a device on 1.0.0-rc7 holds: the clock's settings as labels and a format 1
    // plugin (the store's record upgrade has its own test in test/store).
    settings_set("clock", "time_format", "12 годин (AM/PM)");
    settings_set("clock", "date_format", "РРРР-ММ-ДД");
    settings_set("clock", "seconds", "1");
    module_t old = module("old");
    store_save("old", (uint8_t *) old.bytes, (uint32_t) old.len);
    free(old.bytes);
  }
  if (strcmp(scenario, "uploads") == 0) {
    // A plugin stored before this boot whose module went bad on flash.
    module_t broken = module("broken");
    store_save("broken", (uint8_t *) broken.bytes, (uint32_t) broken.len);
    broken.bytes[broken.len / 2] ^= 1;
    settings_ns_blob_set("plug_code_a", "broken", broken.bytes, broken.len);
    settings_ns_blob_set("plug_code_b", "broken", broken.bytes, broken.len);
    free(broken.bytes);
  }
  if (strcmp(scenario, "unreadable_standard") == 0) {
    settings_set("co2", "alarm", "1");
    settings_ns_erase("plug_code_a", "co2");
    settings_ns_erase("plug_code_b", "co2");
    uint8_t *code = NULL;
    uint32_t len = 0;
    bool read = store_read("humidity", &code, &len, malloc, free) == STORE_OK;
    check(read, "the humidity module reads before it is damaged");
    if (read) {
      code[len / 2] ^= 1;
      settings_ns_blob_set("plug_code_a", "humidity", code, len);
      settings_ns_blob_set("plug_code_b", "humidity", code, len);
      free(code);
    }
  }
  const wasm_host_hooks_t hooks = {
      .timers_stop = timers_stop,
      .lock = nothing,
      .unlock = nothing,
  };
  wasm_host_init(&hooks);
  wasm_host_http_register(NULL);
  if (strcmp(scenario, "first_boot") == 0) {
    first_boot();
  } else if (strcmp(scenario, "second_boot") == 0) {
    second_boot();
  } else if (strcmp(scenario, "older_firmware") == 0) {
    older_firmware();
  } else if (strcmp(scenario, "uploads") == 0) {
    uploads();
  } else if (strcmp(scenario, "unreadable_standard") == 0) {
    unreadable_standard();
  } else if (strcmp(scenario, "until_full") == 0) {
    until_full();
  }
  fake_nvs_save(nvs);
  printf("%d failed\n", s_failed);
  return s_failed ? 1 : 0;
}
