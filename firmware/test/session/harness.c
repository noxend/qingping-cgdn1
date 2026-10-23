// Host check of sign-in: the device's own auth.c, session.c, util.c and api/login.c,
// driven as the HTTP server would drive them. The cookie the device sets is the one
// sent back, so the oracle is what a browser would see: status lines and headers.
// The settings store is in memory and survives a "reboot" (session_init again).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "auth.h"
#include "creds.h"
#include "esp_netif.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "handlers.h"
#include "pass_queue.h"
#include "session.h"
#include "settings.h"
#include "util.h"

#define PASSWORD "test-password"
#define DAY (24 * 3600)
#define T0 1792663279  // 2026-10-22

static int s_failed;

static void check(bool ok, const char *what) {
  printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    s_failed++;
  }
}

// ---- what the device's other components give auth.c and session.c ----------------

static int64_t s_now = T0;
static bool s_cfg_set = true;

static int64_t s_uptime;

int64_t session_now(void) {
  return s_now;
}

int64_t session_uptime(void) {
  return s_uptime;
}

bool creds_config_is_set(void) {
  return s_cfg_set;
}

static int s_password_checks;
static bool s_verified;     // the right password matched once since the boot, as creds.c keeps
static pthread_t s_server;  // the thread the requests come in on: the server task
static int s_hashes;
static int s_hashes_on_server;

// PBKDF2 runs the test lets start: -1 any number, else that many more.
static int s_gate = -1;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;

creds_check_t creds_config_try(const char *pass, uint32_t client) {
  (void) client;
  s_password_checks++;
  if (!s_cfg_set) {
    return CREDS_NOT_SET;
  }
  return s_verified && strcmp(pass, PASSWORD) == 0 ? CREDS_OK : CREDS_NEEDS_HASH;
}

creds_check_t creds_config_check(const char *pass, uint32_t client) {
  creds_check_t cheap = creds_config_try(pass, client);
  if (cheap != CREDS_NEEDS_HASH) {
    return cheap;
  }
  pthread_mutex_lock(&s_mu);
  s_hashes++;
  bool on_server = pthread_equal(pthread_self(), s_server);
  s_hashes_on_server += on_server;
  while (s_gate == 0 && !on_server) {  // the server waiting here would hang the test
    pthread_cond_wait(&s_changed, &s_mu);
  }
  if (s_gate > 0) {
    s_gate--;
  }
  bool ok = strcmp(pass, PASSWORD) == 0;
  s_verified |= ok;
  pthread_mutex_unlock(&s_mu);
  return ok ? CREDS_OK : CREDS_WRONG;
}

static void let_hashes(int n) {
  pthread_mutex_lock(&s_mu);
  s_gate = n;
  pthread_cond_broadcast(&s_changed);
  pthread_mutex_unlock(&s_mu);
}

bool creds_ct_equal(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

static unsigned char s_blob[1024];
static size_t s_blob_len;
static int s_blob_writes;
static bool s_blob_full;

esp_err_t settings_blob_get(const char *key, void *out, size_t *len) {
  if (strcmp(key, "sessions") != 0 || !s_blob_len || *len < s_blob_len) {
    return ESP_ERR_NOT_FOUND;
  }
  memcpy(out, s_blob, s_blob_len);
  *len = s_blob_len;
  return ESP_OK;
}

esp_err_t settings_blob_set(const char *key, const void *data, size_t len) {
  if (strcmp(key, "sessions") != 0 || len > sizeof(s_blob) || s_blob_full) {
    return ESP_FAIL;
  }
  memcpy(s_blob, data, len);
  s_blob_len = len;
  s_blob_writes++;
  return ESP_OK;
}

void esp_fill_random(void *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    ((unsigned char *) buf)[i] = (unsigned char) arc4random();
  }
}

esp_err_t mdns_hostname_get(char *hostname) {
  strcpy(hostname, "cgdn1");
  return ESP_OK;
}

static esp_netif_t *const STA = (esp_netif_t *) 1;

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
  return strcmp(key, "WIFI_STA_DEF") == 0 ? STA : NULL;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
  (void) netif;
  info->ip.addr = 192 | 168 << 8 | 10 << 16 | 172u << 24;
  return ESP_OK;
}

bool net_peer_ipv4(int sockfd, uint32_t *out) {
  *out = 0x0a0a0a00 + (uint32_t) sockfd;
  return true;
}

static bool s_ap_client;
static char s_saved_ssid[40];
static char s_set_password[80];

bool net_is_ap_client(int sockfd) {
  (void) sockfd;
  return s_ap_client;
}

const char *net_scan_json(void) {
  return "{\"nets\":[\"home\"]}";
}

esp_err_t net_save_creds(const char *ssid, const char *pass) {
  (void) pass;
  snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", ssid);
  return ESP_OK;
}

void esp_restart(void) {
}

bool creds_pass_ok(const char *pass) {
  return strlen(pass) >= 8 && strlen(pass) <= 63 && !strchr(pass, ' ');
}

esp_err_t creds_config_set(const char *pass) {
  snprintf(s_set_password, sizeof(s_set_password), "%s", pass);
  s_cfg_set = true;
  return ESP_OK;
}

// ---- the server ----------------------------------------------------------------------

static httpd_uri_t s_uris[8];
static int s_uri_n;

esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *uri) {
  (void) h;
  s_uris[s_uri_n++] = *uri;
  return ESP_OK;
}

static const char *header_in(httpd_req_t *r, const char *field) {
  for (int i = 0; i < HTTPD_MAX_HDRS && r->in[i].name; i++) {
    if (strcasecmp(r->in[i].name, field) == 0) {
      return r->in[i].value;
    }
  }
  return NULL;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t size) {
  const char *v = header_in(r, field);
  if (!v) {
    return ESP_ERR_NOT_FOUND;
  }
  snprintf(val, size, "%s", v);
  return strlen(v) < size ? ESP_OK : ESP_ERR_HTTPD_RESULT_TRUNC;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// httpd_query_key_value from ESP-IDF 5.5.5 (httpd_parse.c), copied unchanged: the form
// fields of /scan and /save are read as on the device.
esp_err_t httpd_query_key_value(const char *qry_str, const char *key, char *val, size_t val_size) {
  if (qry_str == NULL || key == NULL || val == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *qry_ptr = qry_str;
  while (strlen(qry_ptr)) {
    const char *val_ptr = strchr(qry_ptr, '=');
    if (!val_ptr) {
      break;
    }
    size_t offset = val_ptr - qry_ptr;
    if ((offset != strlen(key)) || (strncasecmp(qry_ptr, key, offset))) {
      qry_ptr = strchr(val_ptr, '&');
      if (!qry_ptr) {
        break;
      }
      qry_ptr++;
      continue;
    }
    qry_ptr = strchr(++val_ptr, '&');
    if (!qry_ptr) {
      qry_ptr = val_ptr + strlen(val_ptr);
    }
    size_t val_len = qry_ptr - val_ptr;
    size_t copy_len = MIN(val_len, val_size - 1);
    if (copy_len < val_len) {
      return ESP_ERR_HTTPD_RESULT_TRUNC;
    }
    memcpy(val, val_ptr, copy_len);
    val[copy_len] = '\0';
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

// httpd_cookie_key_value from ESP-IDF 5.5.5 (components/esp_http_server/src/httpd_parse.c),
// copied unchanged but for the log line, so the cookie is parsed as on the device.
static esp_err_t httpd_cookie_key_value(const char *cookie_str, const char *key, char *val,
                                        size_t *val_size) {
  if (cookie_str == NULL || key == NULL || val == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *cookie_ptr = cookie_str;
  while (strlen(cookie_ptr)) {
    const char *val_ptr = strchr(cookie_ptr, '=');
    if (!val_ptr) {
      break;
    }
    size_t offset = val_ptr - cookie_ptr;
    if ((offset != strlen(key)) || (strncasecmp(cookie_ptr, key, offset) != 0)) {
      cookie_ptr = strchr(val_ptr, ' ');
      if (!cookie_ptr) {
        break;
      }
      cookie_ptr++;
      continue;
    }
    cookie_ptr = strchr(++val_ptr, ';');
    if (!cookie_ptr) {
      cookie_ptr = val_ptr + strlen(val_ptr);
    }
    size_t val_len = cookie_ptr - val_ptr;
    size_t copy_len = MIN(val_len, *val_size - 1);
    *val_size = val_len + 1;
    if (copy_len < val_len) {
      return ESP_ERR_HTTPD_RESULT_TRUNC;
    }
    memcpy(val, val_ptr, copy_len);
    val[copy_len] = '\0';
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t httpd_req_get_cookie_val(httpd_req_t *r, const char *name, char *val, size_t *size) {
  const char *cookies = header_in(r, "Cookie");
  if (!cookies) {
    return ESP_ERR_NOT_FOUND;
  }
  return httpd_cookie_key_value(cookies, name, val, size);
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t len) {
  int left = r->content_len - r->read;
  int n = (int) len < left ? (int) len : left;
  memcpy(buf, r->body + r->read, n);
  r->read += n;
  return n;
}

int httpd_req_to_sockfd(httpd_req_t *r) {
  return r->fd;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status) {
  snprintf(r->status, sizeof(r->status), "%s", status);
  return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
  (void) r;
  (void) type;
  return ESP_OK;
}

// Keeps the pointer, as the device's server does: the text must live until the send.
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value) {
  r->out[r->out_n].name = field;
  r->out[r->out_n].value = value;
  r->out_n++;
  return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, int len) {
  snprintf(r->answer, sizeof(r->answer), "%.*s", len, buf);
  r->sent = 1;
  return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str) {
  return httpd_resp_send(r, str, (int) strlen(str));
}

// util.c's 404 handler; the harness never reaches it.
esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t code, const char *msg) {
  return httpd_resp_sendstr(r, msg ? msg : "not found");
}

// ---- the hash task: a thread, and the server's work queue -------------------------

static void (*s_task)(void *);

static void *task_thread(void *arg) {
  s_task(arg);
  return NULL;
}

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio,
                       void *handle) {
  s_task = fn;  // one task: pass_queue's
  pthread_t t;
  return pthread_create(&t, NULL, task_thread, arg) == 0 ? pdPASS : 0;
}

struct queue {
  void *items[8];
  int len, head, n;
};

QueueHandle_t xQueueCreate(int len, int item_size) {
  struct queue *q = calloc(1, sizeof(*q));
  q->len = len;
  return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks) {
  pthread_mutex_lock(&s_mu);
  bool room = q->n < q->len;
  if (room) {
    q->items[(q->head + q->n++) % q->len] = *(void *const *) item;
    pthread_cond_broadcast(&s_changed);
  }
  pthread_mutex_unlock(&s_mu);
  return room ? pdTRUE : 0;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks) {
  pthread_mutex_lock(&s_mu);
  while (!q->n) {
    pthread_cond_wait(&s_changed, &s_mu);
  }
  *(void **) item = q->items[q->head];
  q->head = (q->head + 1) % q->len;
  q->n--;
  pthread_mutex_unlock(&s_mu);
  return pdTRUE;
}

struct sem {
  int given;
};

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  return calloc(1, sizeof(struct sem));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) {
  pthread_mutex_lock(&s_mu);
  while (!s->given) {
    pthread_cond_wait(&s_changed, &s_mu);
  }
  s->given = 0;
  pthread_mutex_unlock(&s_mu);
  return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  pthread_mutex_lock(&s_mu);
  s->given = 1;
  pthread_cond_broadcast(&s_changed);
  pthread_mutex_unlock(&s_mu);
  return pdTRUE;
}

static struct {
  httpd_work_fn_t fn;
  void *arg;
} s_work[8];
static int s_work_n;
static int s_completed;
static int s_shutdowns;
static int64_t s_timer_us = 1000000;

int64_t esp_timer_get_time(void) {
  return s_timer_us;
}

// Stands in for lwIP's: pass_queue.c ends a connection with it.
int shutdown(int fd, int how) {
  pthread_mutex_lock(&s_mu);
  s_shutdowns++;
  pthread_mutex_unlock(&s_mu);
  return 0;
}

esp_err_t httpd_queue_work(httpd_handle_t h, httpd_work_fn_t work, void *arg) {
  pthread_mutex_lock(&s_mu);
  s_work[s_work_n].fn = work;
  s_work[s_work_n].arg = arg;
  s_work_n++;
  pthread_cond_broadcast(&s_changed);
  pthread_mutex_unlock(&s_mu);
  return ESP_OK;
}

esp_err_t httpd_req_async_handler_begin(httpd_req_t *r, httpd_req_t **out) {
  httpd_req_t *copy = malloc(sizeof(*copy));
  *copy = *r;
  copy->origin = r;
  r->handed_over = 1;
  *out = copy;
  return ESP_OK;
}

// The answer the copy got goes to the request the harness holds.
esp_err_t httpd_req_async_handler_complete(httpd_req_t *copy) {
  pthread_mutex_lock(&s_mu);
  httpd_req_t *r = copy->origin;
  memcpy(r->status, copy->status, sizeof(r->status));
  memcpy(r->out, copy->out, sizeof(r->out));
  r->out_n = copy->out_n;
  memcpy(r->answer, copy->answer, sizeof(r->answer));
  r->sent = copy->sent;
  r->done = ++s_completed;
  pthread_cond_broadcast(&s_changed);
  pthread_mutex_unlock(&s_mu);
  free(copy);
  return ESP_OK;
}

static bool is_done(httpd_req_t *r) {
  pthread_mutex_lock(&s_mu);
  bool done = r->done;
  pthread_mutex_unlock(&s_mu);
  return done;
}

// The server task's loop until the request handed to the hash task has its answer: it
// runs the work the hash task queues. Gives up after two seconds without any.
static void wait_answer(httpd_req_t *r) {
  pthread_mutex_lock(&s_mu);
  struct timespec until;
  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_sec += 2;
  while (!r->done) {
    if (s_work_n) {
      httpd_work_fn_t fn = s_work[0].fn;
      void *arg = s_work[0].arg;
      memmove(s_work, s_work + 1, sizeof(s_work[0]) * --s_work_n);
      pthread_mutex_unlock(&s_mu);
      fn(arg);
      pthread_mutex_lock(&s_mu);
    } else if (pthread_cond_timedwait(&s_changed, &s_mu, &until) != 0) {
      break;
    }
  }
  pthread_mutex_unlock(&s_mu);
  if (!r->status[0] && r->sent) {
    strcpy(r->status, "200 OK");
  }
}

// ---- requests ------------------------------------------------------------------------

static int s_handler_runs;

// Stands for any endpoint behind sign-in (/config, /ota, /plugins/upload ...).
static esp_err_t protected_get(httpd_req_t *req) {
  s_handler_runs++;
  return httpd_resp_sendstr(req, "{}");
}

// Stands for /status: open, and shows more to a signed-in caller.
static esp_err_t open_get(httpd_req_t *req) {
  return httpd_resp_sendstr(req, auth_signed_in(req) ? "signed in" : "not signed in");
}

static char s_cookie[128];  // what a browser would send back: "cgdn1_session=<token>"

typedef struct {
  const char *origin;
  const char *auth;
  const char *host;
  bool no_cookie;
  int peer;      // the client's address is 10.10.10.<peer>; 10 when 0
  bool no_wait;  // a request handed to the hash task stays unanswered
} opts_t;

// Runs one request through the registered handlers; the answer stays in *r.
static void request(httpd_req_t *r, httpd_method_t method, const char *uri, const char *body,
                    opts_t o) {
  memset(r, 0, sizeof(*r));
  r->uri = uri;
  r->method = method;
  r->body = body ? body : "";
  r->content_len = (int) strlen(r->body);
  r->fd = o.peer ? o.peer : 10;
  s_timer_us += 1000;  // a millisecond between requests
  int n = 0;
  r->in[n++] = (httpd_hdr_t){"Host", o.host ? o.host : "192.168.10.172"};
  if (o.origin) {
    r->in[n++] = (httpd_hdr_t){"Origin", o.origin};
  }
  if (o.auth) {
    r->in[n++] = (httpd_hdr_t){"Authorization", o.auth};
  }
  if (!o.no_cookie && s_cookie[0]) {
    r->in[n++] = (httpd_hdr_t){"Cookie", s_cookie};
  }
  for (int i = 0; i < s_uri_n; i++) {
    if (s_uris[i].method == method && strcmp(s_uris[i].uri, uri) == 0) {
      r->user_ctx = s_uris[i].user_ctx;
      s_uris[i].handler(r);
      if (r->handed_over && !o.no_wait) {
        wait_answer(r);
      }
      if (!r->status[0] && r->sent) {
        strcpy(r->status, "200 OK");
      }
      return;
    }
  }
  strcpy(r->status, "404 Not Found");
}

static const char *out_header(httpd_req_t *r, const char *name) {
  for (int i = 0; i < r->out_n; i++) {
    if (strcasecmp(r->out[i].name, name) == 0) {
      return r->out[i].value;
    }
  }
  return NULL;
}

static const char *set_cookie(httpd_req_t *r) {
  for (int i = 0; i < r->out_n; i++) {
    if (strcasecmp(r->out[i].name, "Set-Cookie") == 0) {
      return r->out[i].value;
    }
  }
  return NULL;
}

static bool status_is(httpd_req_t *r, const char *code) {
  return strncmp(r->status, code, strlen(code)) == 0;
}

// Keeps the cookie a browser would keep from this answer: "name=value", up to the first ';'.
static void keep_cookie(httpd_req_t *r) {
  const char *c = set_cookie(r);
  if (c) {
    snprintf(s_cookie, sizeof(s_cookie), "%.*s", (int) strcspn(c, ";"), c);
  }
}

static void login(httpd_req_t *r, const char *pass) {
  char body[96];
  snprintf(body, sizeof(body), "{\"password\":\"%s\"}", pass);
  request(r, HTTP_POST, "/login", body, (opts_t){.origin = "http://192.168.10.172"});
  keep_cookie(r);
}

static void get_protected(httpd_req_t *r, opts_t o) {
  request(r, HTTP_GET, "/protected", NULL, o);
}

int main(void) {
  const httpd_uri_t open[] = {
      {.uri = "/login", .method = HTTP_POST, .handler = api_login_post},
      {.uri = "/logout", .method = HTTP_POST, .handler = api_logout_post},
      {.uri = "/scan", .method = HTTP_POST, .handler = api_scan_post},
      {.uri = "/save", .method = HTTP_POST, .handler = api_save_post},
      {.uri = "/status", .method = HTTP_GET, .handler = open_get},
  };
  const httpd_uri_t closed = {.uri = "/protected", .method = HTTP_GET, .handler = protected_get};
  for (size_t i = 0; i < sizeof(open) / sizeof(open[0]); i++) {
    http_register_open(NULL, &open[i]);
  }
  http_register(NULL, &closed);
  auth_set_hosts("cgdn1", "10.47.0.1");
  s_server = pthread_self();
  pass_queue_start();
  session_init();
  httpd_req_t r;

  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401") && s_handler_runs == 0, "no cookie: 401, the endpoint does not run");
  request(&r, HTTP_GET, "/protected", NULL, (opts_t){0});
  check(!auth_signed_in(&r) && s_password_checks == 0,
        "no cookie, no header: the wrong-password limit is not touched");
  login(&r, "wrong-password");
  check(status_is(&r, "401") && !set_cookie(&r), "wrong password: 401, no cookie");

  login(&r, PASSWORD);
  const char *c = set_cookie(&r);
  check(status_is(&r, "200") && c, "right password: 200 and a cookie");
  check(c && strstr(c, "HttpOnly") && strstr(c, "SameSite=Strict") && strstr(c, "Path=/"),
        "the cookie is HttpOnly, SameSite=Strict, Path=/");
  check(c && strstr(c, "Max-Age=2592000"), "the cookie lasts 30 days");
  check(strlen(s_cookie) == strlen(SESSION_COOKIE "=") + 64, "the token is 64 characters");
  check(s_blob_len && !memmem(s_blob, s_blob_len, s_cookie + strlen(SESSION_COOKIE "="), 64),
        "the device stores no token in the clear");

  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200") && s_handler_runs == 1, "signed in: the endpoint runs");
  get_protected(&r, (opts_t){.origin = "http://192.168.10.172"});
  check(status_is(&r, "200"), "signed in, from the device's own page: 200");
  get_protected(&r, (opts_t){.origin = "http://cgdn1.local", .host = "cgdn1.local"});
  check(status_is(&r, "200"), "the same by its mDNS name: 200");
  get_protected(&r, (opts_t){.origin = "http://evil.example"});
  check(status_is(&r, "403") && s_handler_runs == 3, "signed in, from another site's page: 403");
  get_protected(&r, (opts_t){.origin = "null"});
  check(status_is(&r, "403"), "signed in, from an opaque origin: 403");
  get_protected(&r, (opts_t){.host = "evil.example"});
  check(status_is(&r, "421"), "a request for another host name: 421");
  s_ap_client = true;
  int runs_before = s_handler_runs;
  get_protected(&r, (opts_t){.host = "captive.apple.com"});
  const char *to = out_header(&r, "Location");
  check(status_is(&r, "302") && to && strcmp(to, "http://10.47.0.1/") == 0 &&
            s_handler_runs == runs_before,
        "a setup AP client asking for another host: 302 to the setup page");
  request(&r, HTTP_GET, "/generate_204", NULL, (opts_t){.host = "connectivitycheck.gstatic.com"});
  http_not_found(&r, HTTPD_404_NOT_FOUND);
  to = out_header(&r, "Location");
  check(status_is(&r, "302") && to && strcmp(to, "http://10.47.0.1/") == 0,
        "the same for a path no handler knows: 302");
  memset(&r, 0, sizeof(r));
  r.in[0] = (httpd_hdr_t){"Host", "10.47.0.1"};
  http_not_found(&r, HTTPD_404_NOT_FOUND);
  check(!out_header(&r, "Location"), "an unknown path on the device's own name: no redirect");
  s_ap_client = false;
  login(&r, PASSWORD);
  char first[128];
  snprintf(first, sizeof(first), "%s", s_cookie);
  request(&r, HTTP_POST, "/login", "{\"password\":\"" PASSWORD "\"}",
          (opts_t){.origin = "http://evil.example"});
  check(status_is(&r, "403") && !set_cookie(&r), "sign-in from another site's page: 403");

  get_protected(&r, (opts_t){.no_cookie = true, .auth = "Bearer " PASSWORD});
  check(status_is(&r, "200"), "a tool with the password as Bearer: 200");
  get_protected(&r, (opts_t){.no_cookie = true, .auth = "Bearer nope"});
  check(status_is(&r, "401"), "a tool with a wrong password: 401");
  char too_long[96];
  snprintf(too_long, sizeof(too_long), "Bearer %s%0*d", PASSWORD, 64 - (int) strlen(PASSWORD), 0);
  int checks = s_password_checks;
  get_protected(&r, (opts_t){.no_cookie = true, .auth = too_long});
  check(status_is(&r, "401") && s_password_checks == checks,
        "a 64-character Bearer: 401, refused before any check");
  get_protected(&r, (opts_t){.no_cookie = true, .auth = "Basic dGVzdA=="});
  check(status_is(&r, "401"), "Basic is not taken: 401");
  get_protected(
      &r, (opts_t){.no_cookie = true, .auth = "Bearer " PASSWORD, .origin = "http://evil.example"});
  check(status_is(&r, "403"), "the password from another site's page: 403");
  snprintf(s_cookie, sizeof(s_cookie), SESSION_COOKIE "=%064d", 0);
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "a made-up token: 401");
  snprintf(s_cookie, sizeof(s_cookie), "%s", first);

  session_init();
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200"), "after a reboot: still signed in");

  int writes = s_blob_writes;
  s_now = T0 + DAY / 2;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200") && !set_cookie(&r) && s_blob_writes == writes,
        "used within a day: no new cookie, no flash write");
  s_now = T0 + 29 * DAY;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200") && set_cookie(&r) && s_blob_writes == writes + 1,
        "used on day 29: the cookie comes again, one flash write");
  s_now = T0 + 58 * DAY;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200"), "used on day 58: still signed in (extended on day 29)");
  s_now = T0 + 89 * DAY;
  int runs = s_handler_runs;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401") && s_handler_runs == runs, "not used for 31 days: expired, 401");
  session_init();
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "expired stays expired after a reboot");

  login(&r, PASSWORD);
  request(&r, HTTP_POST, "/logout", NULL, (opts_t){.origin = "http://192.168.10.172"});
  c = set_cookie(&r);
  check(status_is(&r, "200") && c && strstr(c, "Max-Age=0"), "sign out: 200, the cookie cleared");
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "the signed-out token: 401");

  // The clock: SNTP sets it some seconds after a boot, or never without the internet.
  s_now = T0 + 100 * DAY;
  login(&r, PASSWORD);
  s_now = 0;
  session_init();
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200") && !set_cookie(&r), "a reboot before SNTP: signed in, not extended");
  s_now = T0 + 140 * DAY;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "SNTP back after 40 days: expired");
  login(&r, PASSWORD);  // the last time the device saw: day 140
  s_now = 0;
  session_init();
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200"), "no clock: a session made with one still counts");
  s_uptime = 20 * DAY;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200") && set_cookie(&r), "offline, used on day 20 of uptime: extended");
  s_uptime = 0;
  session_init();
  s_uptime = 31 * DAY;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "offline, a reboot, 31 days unused: expired");
  s_uptime = 0;
  memset(s_blob, 0, sizeof(s_blob));
  s_blob_len = 0;
  session_init();
  login(&r, PASSWORD);
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200"), "a device that never had a clock: sign-in works");
  session_init();
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "and that session ends with the boot");

  s_now = T0;
  char browsers[SESSION_SLOTS + 1][128];  // their cookies, oldest first
  for (int i = 0; i <= SESSION_SLOTS; i++) {
    s_now += 60;
    login(&r, PASSWORD);
    snprintf(browsers[i], sizeof(browsers[i]), "%s", s_cookie);
  }
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200"), "the newest of nine browsers: signed in");
  snprintf(s_cookie, sizeof(s_cookie), "%s", browsers[0]);
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "401"), "the oldest gave way to the ninth: 401");
  s_blob_full = true;
  login(&r, PASSWORD);
  check(status_is(&r, "500") && !set_cookie(&r), "storage full: sign-in fails with 500");
  s_blob_full = false;
  snprintf(s_cookie, sizeof(s_cookie), "%s", browsers[1]);
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200"), "and the oldest signed-in browser keeps its session");

  // Wi-Fi: the page sends the form without a password once signed in.
  login(&r, PASSWORD);
  const opts_t own = {.origin = "http://192.168.10.172"};
  request(&r, HTTP_POST, "/scan", "", own);
  check(status_is(&r, "200") && strstr(r.answer, "nets"), "scan, signed in, an empty form: 200");
  request(&r, HTTP_POST, "/save", "ssid=office&pass=", own);
  check(status_is(&r, "200") && strcmp(s_saved_ssid, "office") == 0,
        "save, signed in, no password in the form: 200");
  request(&r, HTTP_POST, "/scan", "", (opts_t){.origin = "http://evil.example"});
  check(status_is(&r, "403"), "scan from another site's page: 403");
  request(&r, HTTP_POST, "/scan", "", (opts_t){.no_cookie = true});
  check(status_is(&r, "401"), "scan without sign-in: 401");
  request(&r, HTTP_POST, "/scan", "token=" PASSWORD, (opts_t){.no_cookie = true});
  check(status_is(&r, "200"), "scan with the password in the form (/fallback): 200");

  // After a reboot the right password is in no cache: the token goes to the hash task,
  // and the form with it.
  s_verified = false;
  request(&r, HTTP_POST, "/scan", "token=" PASSWORD, (opts_t){.no_cookie = true});
  check(status_is(&r, "200") && r.handed_over,
        "scan with the password after a reboot: hashed, 200");
  s_verified = false;
  s_saved_ssid[0] = '\0';
  request(&r, HTTP_POST, "/save", "ssid=attic&pass=&token=" PASSWORD, (opts_t){.no_cookie = true});
  check(status_is(&r, "200") && strcmp(s_saved_ssid, "attic") == 0,
        "save with the password after a reboot: the form saved after the hash");
  s_verified = false;
  get_protected(&r, (opts_t){.no_cookie = true, .auth = "Bearer " PASSWORD});
  check(status_is(&r, "200") && r.handed_over, "a tool's Bearer after a reboot: hashed, 200");
  get_protected(&r, (opts_t){.no_cookie = true, .auth = "Bearer " PASSWORD});
  check(status_is(&r, "200") && !r.handed_over, "and again: from the cache, not queued");
  s_verified = false;
  get_protected(
      &r, (opts_t){.no_cookie = true, .auth = "Bearer " PASSWORD, .origin = "http://evil.example"});
  check(status_is(&r, "403") && !r.handed_over,
        "the Bearer from another site's page: 403, no hash");
  s_verified = false;
  request(&r, HTTP_GET, "/status", NULL, (opts_t){.no_cookie = true, .auth = "Bearer " PASSWORD});
  check(r.handed_over && strcmp(r.answer, "signed in") == 0,
        "status with the Bearer after a reboot: hashed, signed in");
  request(&r, HTTP_GET, "/status", NULL, (opts_t){.no_cookie = true, .auth = "Bearer nope"});
  check(status_is(&r, "200") && strcmp(r.answer, "not signed in") == 0,
        "status with a wrong Bearer: hashed, answered as not signed in");

  // A flood: wrong passwords from several addresses while each PBKDF2 takes its time.
  login(&r, PASSWORD);  // a browser signed in before the flood: its cookie is kept
  s_verified = false;
  s_shutdowns = 0;
  let_hashes(0);
  httpd_req_t flood[PASS_QUEUE_LEN];
  const opts_t page = {.origin = "http://192.168.10.172", .no_cookie = true, .no_wait = true};
  const char *guess = "{\"password\":\"guess\"}";
  bool queued = true;
  for (int i = 0; i < PASS_QUEUE_LEN; i++) {
    opts_t o = page;
    o.peer = 20 + i;
    request(&flood[i], HTTP_POST, "/login", guess, o);
    queued &= flood[i].handed_over && !flood[i].sent;
  }
  check(queued, "four wrong passwords from four addresses: queued, no answer yet");
  opts_t a30 = page;
  a30.peer = 30;
  httpd_req_t refused;
  request(&refused, HTTP_POST, "/login", guess, a30);
  const char *retry = out_header(&refused, "Retry-After");
  check(status_is(&refused, "503") && retry && strcmp(retry, "1") == 0,
        "a fifth address while four wait: 503, Retry-After 1");
  opts_t a40 = page;
  a40.peer = 40;
  httpd_req_t owner;
  const char *right = "{\"password\":\"" PASSWORD "\"}";
  request(&owner, HTTP_POST, "/login", right, a40);
  check(status_is(&owner, "503"), "the owner during the flood: 503, second in line");
  int flood_runs = s_handler_runs;
  get_protected(&r, (opts_t){0});
  check(status_is(&r, "200") && s_handler_runs == flood_runs + 1 && !r.handed_over,
        "a signed-in browser during the flood: answered at once");
  get_protected(&r, (opts_t){.auth = "Bearer stale-password"});
  check(status_is(&r, "200") && !r.handed_over,
        "the same with a stale Bearer header too: the cookie counts, not queued");
  request(&r, HTTP_GET, "/status", NULL, (opts_t){.no_cookie = true});
  check(status_is(&r, "200") && strcmp(r.answer, "not signed in") == 0,
        "status without sign-in during the flood: answered at once");

  let_hashes(1);
  wait_answer(&flood[0]);
  check(status_is(&flood[0], "401") && !is_done(&flood[1]),
        "one hash done: the first wrong try answered 401, the rest wait");
  opts_t a21 = page;
  a21.peer = 21;
  httpd_req_t twice;
  request(&twice, HTTP_POST, "/login", guess, a21);
  check(status_is(&twice, "503"), "a place free, but this address has a try queued: 503");
  opts_t a50 = page;
  a50.peer = 50;
  request(&r, HTTP_POST, "/login", guess, a50);
  check(status_is(&r, "503"), "a new address while two are in line: 503");
  request(&owner, HTTP_POST, "/login", right, a40);
  check(status_is(&owner, "503"), "the owner asks again, the address before it has not: 503");
  request(&refused, HTTP_POST, "/login", guess, a30);
  check(refused.handed_over, "the address refused first asks again: it takes the place");
  let_hashes(1);
  wait_answer(&flood[1]);
  request(&owner, HTTP_POST, "/login", right, a40);
  check(owner.handed_over && !owner.sent, "the next place: the owner's, queued behind three");
  let_hashes(-1);
  wait_answer(&owner);
  bool in_order = true;
  httpd_req_t *ahead[] = {&flood[2], &flood[3], &refused};
  for (int i = 0; i < 3; i++) {
    wait_answer(ahead[i]);
    in_order &= status_is(ahead[i], "401") && ahead[i]->done < owner.done;
  }
  check(in_order, "the three before the owner: 401, answered first");
  check(status_is(&owner, "200") && set_cookie(&owner), "then the owner: 200 and a cookie");
  check(s_shutdowns == 5, "each wrong one from the queue ends its connection, the owner's stays");

  // An address in line that stops asking gives up its place: the one at 50 has not
  // asked for 6 s.
  s_timer_us += 6 * 1000000;
  s_verified = false;
  let_hashes(0);
  queued = true;
  for (int i = 0; i < PASS_QUEUE_LEN; i++) {
    opts_t o = page;
    o.peer = 70 + i;
    request(&flood[i], HTTP_POST, "/login", guess, o);
    if (i == 0) {
      request(&r, HTTP_POST, "/login", guess, o);
      check(status_is(&r, "503"), "room left, nobody in line, this address queued already: 503");
    }
    queued &= flood[i].handed_over;
  }
  check(queued, "the line empty again: four more addresses queued");
  opts_t a74 = page;
  a74.peer = 74;
  request(&r, HTTP_POST, "/login", guess, a74);
  let_hashes(1);
  wait_answer(&flood[0]);
  opts_t a75 = page;
  a75.peer = 75;
  request(&r, HTTP_POST, "/login", guess, a75);
  check(status_is(&r, "503"), "a place free, an address in line: a newcomer gets 503");
  s_timer_us += 6 * 1000000;
  request(&r, HTTP_POST, "/login", guess, a75);
  check(r.handed_over, "6 s later the one in line has not asked again: the newcomer gets in");
  let_hashes(-1);
  for (int i = 1; i < PASS_QUEUE_LEN; i++) {
    wait_answer(&flood[i]);
  }
  wait_answer(&r);
  check(s_hashes_on_server == 0 && s_hashes > 0, "no PBKDF2 ever ran on the server task");

  s_cfg_set = false;
  s_ap_client = true;
  request(&r, HTTP_POST, "/scan", "", (opts_t){.no_cookie = true});
  check(status_is(&r, "200"), "first setup over the AP: scan with an empty form: 200");
  const opts_t ap = {.origin = "http://10.47.0.1", .host = "10.47.0.1", .no_cookie = true};
  request(&r, HTTP_POST, "/save", "ssid=home&pass=&token=new-password", ap);
  check(status_is(&r, "200") && strcmp(s_set_password, "new-password") == 0,
        "first setup: save sets the password");
  s_cfg_set = false;
  s_ap_client = false;
  request(&r, HTTP_POST, "/scan", "", (opts_t){.no_cookie = true});
  check(status_is(&r, "403"), "refused setup from the LAN: 403");

  s_cfg_set = false;
  get_protected(&r, (opts_t){.no_cookie = true});
  check(status_is(&r, "403"), "no config password yet: 403");
  login(&r, "anything");
  check(status_is(&r, "403") && !set_cookie(&r), "no config password yet: sign-in 403");

  printf("%d failed\n", s_failed);
  return s_failed ? 1 : 0;
}
