// The network worker's wall clock (core_net.c), for the code that reads from a
// connection on it: a request, and the firmware download.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_http_client.h"

// A wall clock for a connection the worker reads from: when `us` have passed, the
// connection's socket is shut and the blocked read returns. Starting it again moves
// the clock. Worker only, one connection at a time. The client is made with
// core_net_deadline_event as its event_handler, which is how the clock finds the socket.
esp_err_t core_net_deadline_event(esp_http_client_event_t *evt);
void core_net_deadline_start(int64_t us);
// Stops the clock; true when it ran out, so what was read is not a whole answer.
bool core_net_deadline_end(void);
