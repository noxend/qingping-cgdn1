// Config password checks that need PBKDF2 (0.48 s each on this chip) run on a task of
// their own, so the one server task keeps answering session cookies and /status while
// a flood of wrong passwords is hashed. The request is handed over with
// httpd_req_async_handler_begin and its handler goes on, on the server task again
// (httpd_queue_work), once the hash is done: handlers never run on two tasks.
#pragma once

#include "esp_http_server.h"
#include "creds.h"

// Checks waiting or running at once, in order. Each holds its connection open and
// the server has 7 (max_open_sockets), so 3 stay for everything else.
#define PASS_QUEUE_LEN 4
// The most request body a handler can hand over with its request: the setup form.
#define PASS_QUEUE_BODY_MAX 511

// Runs on the server task with the request's copy once its password is checked:
// `result` is creds_config_check's, `body` what the handler gave pass_queue_add
// ("" for none). It returns what the handler would: an error closes the connection.
typedef esp_err_t (*pass_resume_t)(httpd_req_t *req, creds_check_t result, const char *body);

// Starts the hash task; before the server takes requests.
void pass_queue_start(void);

// Hands req over for creds_config_check(pass, client), to run resume after it.
// ESP_OK: queued, and the handler returns ESP_OK at once with nothing sent. ESP_FAIL:
// answered here with 503 and Retry-After 1, and the handler returns ESP_FAIL: this
// client has a check in the queue already, the queue is full, another address was
// refused before this one and has not had its turn, or no memory. A refused address
// keeps its place in line while it asks again within 5 s, for up to 16 addresses.
// Without the hash task (it could not start) the check runs here and resume at once.
esp_err_t pass_queue_add(httpd_req_t *req, const char *pass, uint32_t client, pass_resume_t resume,
                         const char *body);

// True while req is the copy a resume is running with and its check was of pass:
// *result is that check's result.
bool pass_queue_result(httpd_req_t *req, const char *pass, creds_check_t *result);
