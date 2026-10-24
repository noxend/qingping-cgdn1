#include "pass_queue.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "util.h"

// Below the server task (tskIDLE_PRIORITY + 5), so a hash never holds up an answer,
// and above the other tasks, as when PBKDF2 ran on the server task.
#define HASH_TASK_PRIO 4
#define HASH_TASK_STACK 4096
#define RETRY_MS 20
// Addresses refused for want of a place, kept in line. Past this many, one more is
// refused without a place in line: the ceiling of the fairness below.
#define WAITERS 16
// An address in line that has not asked again for this long gives up its place.
#define WAITER_TTL_US (5 * 1000 * 1000)

static const char *TAG = "pass_queue";

typedef struct {
  httpd_req_t *req;  // the copy from httpd_req_async_handler_begin
  pass_resume_t resume;
  uint32_t client;
  creds_check_t result;
  char pass[CREDS_PASS_MAX + 1];
  char body[PASS_QUEUE_BODY_MAX + 1];
} item_t;

// The checks handed over and not yet resumed. Only the server task reads or writes it.
static item_t *s_items[PASS_QUEUE_LEN];
// The item whose resume is running, on the server task.
static item_t *s_resuming;
// Without the hash task, the one check that runs on the server task.
static item_t s_inline;
// A place that comes free goes to the address refused first that asks again, so a
// flood from more addresses than the queue holds cannot keep anyone out by asking
// faster. Only the server task reads or writes it.
static struct {
  uint32_t client;
  int64_t first_us;  // when it was first refused: its place in line
  int64_t last_us;   // when it last asked; 0 for a free entry
} s_waiters[WAITERS];
static QueueHandle_t s_fifo;
static SemaphoreHandle_t s_resumed;

static void resume_on_server(void *arg) {
  item_t *it = arg;
  s_resuming = it;
  esp_err_t err = it->resume(it->req, it->result, it->body);
  s_resuming = NULL;
  // What the handler left of the body, read off as the server does after a handler,
  // so the next request on this connection starts where it should.
  char sink[64];
  int got = 0;
  while (err == ESP_OK && (got = httpd_req_recv(it->req, sink, sizeof(sink))) > 0) {
  }
  if (err != ESP_OK || got < 0) {
    // As the server does after a handler's error. While the request is still handed
    // over its socket is this connection's; once completed the server sees the end
    // and closes the session.
    shutdown(httpd_req_to_sockfd(it->req), SHUT_RDWR);
  }
  for (int i = 0; i < PASS_QUEUE_LEN; i++) {
    if (s_items[i] == it) {
      s_items[i] = NULL;
    }
  }
  xSemaphoreGive(s_resumed);
}

// The hash task. It completes the request itself: httpd_req_async_handler_complete
// waits for room in the server's control queue, which on the server task would wait
// for itself.
static void hash_task(void *arg) {
  item_t *it;
  for (;;) {
    xQueueReceive(s_fifo, &it, portMAX_DELAY);
    it->result = creds_config_check(it->pass, it->client);
    httpd_handle_t srv = it->req->handle;
    while (httpd_queue_work(srv, resume_on_server, it) != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
    }
    xSemaphoreTake(s_resumed, portMAX_DELAY);
    httpd_req_async_handler_complete(it->req);
    free(it);
  }
}

void pass_queue_start(void) {
  if (s_fifo) {
    return;
  }
  s_fifo = xQueueCreate(PASS_QUEUE_LEN, sizeof(item_t *));
  s_resumed = xSemaphoreCreateBinary();
  if (!s_fifo || !s_resumed ||
      xTaskCreate(hash_task, "pass_queue", HASH_TASK_STACK, NULL, HASH_TASK_PRIO, NULL) != pdPASS) {
    ESP_LOGE(TAG, "hash task not started: PBKDF2 runs on the server task");
    s_fifo = NULL;
  }
}

static esp_err_t busy(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Retry-After", "1");
  return http_error(req, HTTP_503_BUSY, "checking other passwords, try again in a second");
}

// The live entry of the address first in line, or -1.
static int first_waiter(int64_t now) {
  int first = -1;
  for (int i = 0; i < WAITERS; i++) {
    bool live = s_waiters[i].last_us && now - s_waiters[i].last_us < WAITER_TTL_US;
    if (live && (first < 0 || s_waiters[i].first_us < s_waiters[first].first_us)) {
      first = i;
    }
  }
  return first;
}

// Keeps the client's place in line, or gives it the last one while there is room.
static void keep_place(uint32_t client, int64_t now) {
  int slot = -1;
  for (int i = 0; i < WAITERS; i++) {
    bool live = s_waiters[i].last_us && now - s_waiters[i].last_us < WAITER_TTL_US;
    if (live && s_waiters[i].client == client) {
      s_waiters[i].last_us = now;
      return;
    }
    if (!live && slot < 0) {
      slot = i;
    }
  }
  if (slot >= 0) {
    s_waiters[slot].client = client;
    s_waiters[slot].first_us = now;
    s_waiters[slot].last_us = now;
  }
}

// The check on the server task, for when the hash task could not start.
static esp_err_t check_here(httpd_req_t *req, const char *pass, uint32_t client,
                            pass_resume_t resume, const char *body) {
  s_inline.req = req;
  strlcpy(s_inline.pass, pass, sizeof(s_inline.pass));
  s_inline.result = creds_config_check(pass, client);
  s_resuming = &s_inline;
  esp_err_t err = resume(req, s_inline.result, body ? body : "");
  s_resuming = NULL;
  return err;
}

esp_err_t pass_queue_add(httpd_req_t *req, const char *pass, uint32_t client, pass_resume_t resume,
                         const char *body) {
  // A second password in a request that is resumed already (a Bearer header and a
  // password in the body): its copy cannot be handed over twice.
  if (s_resuming && s_resuming->req == req) {
    return busy(req);
  }
  if (!s_fifo) {
    return check_here(req, pass, client, resume, body);
  }
  int free_slot = -1;
  for (int i = 0; i < PASS_QUEUE_LEN; i++) {
    if (s_items[i] && s_items[i]->client == client) {
      return busy(req);
    }
    if (!s_items[i]) {
      free_slot = i;
    }
  }
  int64_t now = esp_timer_get_time();
  int first = first_waiter(now);
  if (free_slot < 0 || (first >= 0 && s_waiters[first].client != client)) {
    keep_place(client, now);
    return busy(req);
  }
  item_t *it = calloc(1, sizeof(*it));
  if (!it || httpd_req_async_handler_begin(req, &it->req) != ESP_OK) {
    free(it);
    return busy(req);
  }
  if (first >= 0) {
    s_waiters[first].last_us = 0;  // its turn came
  }
  it->resume = resume;
  it->client = client;
  strlcpy(it->pass, pass, sizeof(it->pass));
  strlcpy(it->body, body ? body : "", sizeof(it->body));
  s_items[free_slot] = it;
  xQueueSend(s_fifo, &it, 0);  // never full: s_items holds as many
  return ESP_OK;
}

bool pass_queue_result(httpd_req_t *req, const char *pass, creds_check_t *result) {
  if (!s_resuming || s_resuming->req != req || !creds_ct_equal(pass, s_resuming->pass)) {
    return false;
  }
  *result = s_resuming->result;
  return true;
}
