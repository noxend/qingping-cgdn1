#include "core_net.h"
#include "net_deadline.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "heap_internal.h"
#include "lwip/sockets.h"

#define URL_MAX 256
#define QUEUE_LEN 4
#define TIMEOUT_MS 10000
#define FIRST_CHUNK 4096  // a body grows from this, doubling, up to the caller's cap
// A request ends within this, however slowly the server drips bytes: without a wall
// clock a one-byte-per-9-s server would hold the worker for hours, and the task
// watchdog would restart the device long before. Counted from before the connect.
#define DEADLINE_US (20LL * 1000 * 1000)
// What the heap must keep beside the body: the TLS session (about 40 KB on this
// target) is already paid for when the body is read, this is for everyone else.
// Measured: 24 KB refused one poll in four of the 17 KB alerts answer.
#define HEAP_HEADROOM (16 * 1024)
// A body with no announced length grows by doubling up to this, then in steps,
// so a 17 KB answer ends in a 24 KB buffer rather than a 32 KB one.
#define GROW_DOUBLE_UNTIL (8 * 1024)
#define GROW_STEP (8 * 1024)
#define READ_CHUNK 1024
// How long the worker sleeps between two steps of a connect that is not through yet.
#define CONNECT_STEP_MS 20

static const char *TAG = "net";

typedef struct {
  int owner;
  size_t max_bytes;
  core_net_parse_fn parse;
  core_net_job_fn job;
  char url[URL_MAX];
} request_t;

static QueueHandle_t s_queue;
static core_net_done_fn s_done;

// ---- the deadline --------------------------------------------------------------------
// esp_http_client's reads return only once their whole count has arrived, and their
// timeout counts silence, so a check between reads never sees a server that sends a
// byte every few seconds. The deadline is a timer that shuts the connection's socket,
// which makes the blocked read return. The socket is followed through the client's own
// events, so it is never touched after the client closed it. One worker, so one
// deadline at a time. The connect has no socket to shut yet: fetch() steps through it
// and looks at the clock between steps.

static esp_timer_handle_t s_deadline_timer;
static SemaphoreHandle_t s_deadline_lock;
static int s_deadline_fd = -1;  // the open connection's socket
static int64_t s_deadline_at;   // when it runs out; 0 while none runs
static bool s_deadline_hit;

static void deadline_cb(void *arg) {
  xSemaphoreTake(s_deadline_lock, portMAX_DELAY);
  // A callback that was already on its way when the clock was moved is not due yet.
  if (s_deadline_at != 0 && esp_timer_get_time() >= s_deadline_at) {
    s_deadline_hit = true;
    if (s_deadline_fd >= 0) {
      shutdown(s_deadline_fd, SHUT_RDWR);
    }
  }
  xSemaphoreGive(s_deadline_lock);
}

esp_err_t core_net_deadline_event(esp_http_client_event_t *evt) {
  if (evt->event_id != HTTP_EVENT_ON_CONNECTED && evt->event_id != HTTP_EVENT_DISCONNECTED) {
    return ESP_OK;
  }
  xSemaphoreTake(s_deadline_lock, portMAX_DELAY);
  if (evt->event_id == HTTP_EVENT_DISCONNECTED) {
    s_deadline_fd = -1;
  } else {
    s_deadline_fd = esp_http_client_get_socket(evt->client);
    if (s_deadline_hit && s_deadline_fd >= 0) {
      shutdown(s_deadline_fd, SHUT_RDWR);  // it ran out during the connect
    }
  }
  xSemaphoreGive(s_deadline_lock);
  return ESP_OK;
}

void core_net_deadline_start(int64_t us) {
  esp_timer_stop(s_deadline_timer);
  xSemaphoreTake(s_deadline_lock, portMAX_DELAY);
  s_deadline_at = esp_timer_get_time() + us;
  xSemaphoreGive(s_deadline_lock);
  esp_timer_start_once(s_deadline_timer, (uint64_t) us);
}

bool core_net_deadline_end(void) {
  esp_timer_stop(s_deadline_timer);
  xSemaphoreTake(s_deadline_lock, portMAX_DELAY);
  bool hit = s_deadline_hit;
  s_deadline_at = 0;
  s_deadline_hit = false;
  xSemaphoreGive(s_deadline_lock);
  return hit;
}

// Grows the body buffer to `next` bytes in PSRAM; without room there, in the internal
// heap only while that leaves the headroom, since a server sizes this buffer.
static bool grow(char **body, size_t next) {
  char *grown = heap_caps_realloc(*body, next + 1, HEAP_PSRAM);
  if (!grown) {
    if (heap_caps_get_largest_free_block(HEAP_INTERNAL) < next + 1 + HEAP_HEADROOM) {
      return false;
    }
    grown = heap_caps_realloc(*body, next + 1, HEAP_INTERNAL);
  }
  if (!grown) {
    return false;
  }
  *body = grown;
  return true;
}

// Reads the whole body into a buffer sized to what arrives (the server's length when
// it sends one, else grown by doubling), so a 17 KB answer does not take the whole
// cap while the TLS session still holds its own memory. NULL with r->err set on a
// read error, no memory, the deadline, or a body over the cap.
static char *read_body(esp_http_client_handle_t c, int64_t announced, size_t cap, int64_t start_us,
                       core_net_result_t *r) {
  size_t size = (announced > 0 && (size_t) announced <= cap) ? (size_t) announced : FIRST_CHUNK;
  size = size > cap ? cap : size;
  char *body = NULL;
  if (!grow(&body, size)) {
    r->err = ESP_ERR_NO_MEM;
    return NULL;
  }
  size_t len = 0;
  while (true) {
    if (esp_timer_get_time() - start_us > DEADLINE_US) {
      r->err = ESP_ERR_TIMEOUT;
      free(body);
      return NULL;
    }
    if (len == size) {
      if (size == cap) {
        char probe;
        if (esp_http_client_read(c, &probe, 1) > 0) {
          r->err = ESP_ERR_INVALID_SIZE;  // larger than the caller allows
          free(body);
          return NULL;
        }
        break;
      }
      size_t next = size < GROW_DOUBLE_UNTIL ? size * 2 : size + GROW_STEP;
      next = next > cap ? cap : next;
      if (!grow(&body, next)) {
        r->err = ESP_ERR_NO_MEM;
        free(body);
        return NULL;
      }
      size = next;
    }
    // esp_http_client_read returns only once its whole count has arrived (or the
    // link is silent for TIMEOUT_MS); the deadline's timer ends one that takes longer.
    size_t want = size - len;
    want = want > READ_CHUNK ? READ_CHUNK : want;
    esp_task_wdt_reset();
    int n = esp_http_client_read(c, body + len, (int) want);
    if (n == -ESP_ERR_HTTP_EAGAIN) {
      continue;  // nothing whole to read yet; the deadline above ends the wait
    }
    if (n < 0) {
      r->err = ESP_FAIL;
      free(body);
      return NULL;
    }
    if (n == 0) {
      break;
    }
    len += (size_t) n;
  }
  body[len] = '\0';
  r->len = (int) len;
  return body;
}

static bool past_deadline(int64_t start_us) {
  return esp_timer_get_time() - start_us > DEADLINE_US;
}

// Connects and sends the request. The client is asynchronous, so the TLS handshake is
// a run of short steps instead of one blocking call that a server sending a byte every
// few seconds could hold for hours: the clock is looked at between the steps.
static esp_err_t open_by_deadline(esp_http_client_handle_t c, int64_t start_us) {
  while (true) {
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_ERR_HTTP_EAGAIN) {
      return err;
    }
    if (past_deadline(start_us)) {
      return ESP_ERR_TIMEOUT;
    }
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(CONNECT_STEP_MS));
  }
}

// The response's headers: the body's announced length, 0 for a chunked one, negative
// on an error or the deadline. An asynchronous client says "again" for a silent link
// and for a TLS record that has not wholly arrived.
static int64_t headers_by_deadline(esp_http_client_handle_t c, int64_t start_us) {
  while (true) {
    int64_t announced = esp_http_client_fetch_headers(c);
    if (announced != -ESP_ERR_HTTP_EAGAIN || past_deadline(start_us)) {
      return announced;
    }
    esp_task_wdt_reset();
  }
}

// Fetches one URL into r. A body is returned only for status 200.
static void fetch(const request_t *req, core_net_result_t *r) {
  memset(r, 0, sizeof(*r));
  int64_t start_us = esp_timer_get_time();
  char *body = NULL;
  // The connection is the worker's own, not one of the client's: the client closes a
  // connection only once it is through, and a connect that failed or ran out of time
  // half way would keep its TLS session and socket for good.
  esp_transport_handle_t tls = esp_transport_ssl_init();
  if (!tls) {
    r->err = ESP_ERR_NO_MEM;
    return;
  }
  esp_transport_ssl_crt_bundle_attach(tls, esp_crt_bundle_attach);
  esp_http_client_config_t cfg = {
      .url = req->url,
      .timeout_ms = TIMEOUT_MS,
      .event_handler = core_net_deadline_event,
      .is_async = true,
      .transport = tls,
  };
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) {
    esp_transport_destroy(tls);
    r->err = ESP_FAIL;
    return;
  }
  core_net_deadline_start(DEADLINE_US);
  r->err = open_by_deadline(c, start_us);
  if (r->err != ESP_OK) {
    goto done;
  }
  int64_t announced = headers_by_deadline(c, start_us);
  r->status = esp_http_client_get_status_code(c);
  if (r->status != 200) {
    ESP_LOGW(TAG, "HTTP %d", r->status);
    goto done;
  }
  body = read_body(c, announced, req->max_bytes, start_us, r);
done:
  // A read the deadline cut short looks like the end of the body: not a result.
  if (core_net_deadline_end()) {
    ESP_LOGW(TAG, "request took too long");
    free(body);
    body = NULL;
    r->status = 0;
    r->len = 0;
    r->err = ESP_ERR_TIMEOUT;
  }
  r->body = body;
  esp_http_client_close(c);
  esp_transport_close(tls);  // what a connect that never got through left open
  esp_http_client_cleanup(c);
  esp_transport_destroy(tls);
}

// The parser runs here, on the worker, so the UI loop never holds a body or a
// JSON tree; the core gets the small result and the body is gone by then.
static void net_worker(void *arg) {
  esp_task_wdt_add(NULL);
  request_t req;
  while (true) {
    esp_task_wdt_reset();
    if (xQueueReceive(s_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
      continue;
    }
    if (req.job) {
      req.job();
      continue;
    }
    core_net_result_t r;
    fetch(&req, &r);
    if (req.parse) {
      r.parsed = r.body ? req.parse(r.body, r.len) : NULL;
      free(r.body);
      r.body = NULL;
    }
    s_done(req.owner, &r);
  }
}

void core_net_start(core_net_done_fn done) {
  s_done = done;
  s_queue = xQueueCreate(QUEUE_LEN, sizeof(request_t));
  s_deadline_lock = xSemaphoreCreateMutex();
  const esp_timer_create_args_t deadline = {.callback = deadline_cb, .name = "net_deadline"};
  bool ready = s_queue && s_deadline_lock &&
               esp_timer_create(&deadline, &s_deadline_timer) == ESP_OK &&
               xTaskCreate(net_worker, "net", 8192, NULL, 3, NULL) == pdPASS;
  if (!ready && s_queue) {
    vQueueDelete(s_queue);  // no worker: refuse requests instead of accepting them forever
    s_queue = NULL;
  }
  if (!s_queue) {
    ESP_LOGE(TAG, "net worker start failed");
  }
}

bool core_net_submit(int owner, const char *url, size_t max_bytes, core_net_parse_fn parse) {
  if (!s_queue || strlen(url) >= URL_MAX) {
    return false;
  }
  request_t req = {
      .owner = owner,
      .max_bytes = max_bytes > CORE_NET_BODY_CAP ? CORE_NET_BODY_CAP : max_bytes,
      .parse = parse,
  };
  strcpy(req.url, url);
  return xQueueSend(s_queue, &req, 0) == pdTRUE;
}

bool core_net_run(core_net_job_fn job) {
  request_t req = {.job = job};
  return s_queue && xQueueSend(s_queue, &req, 0) == pdTRUE;
}
