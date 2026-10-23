// The device's HTTP server and its own endpoints: the pages, /status, Wi-Fi setup,
// OTA, and the diagnostics. The screens register theirs through screen_http_register.
#pragma once

#include <stdbool.h>

// Starts the server and registers every endpoint. Does nothing once it is up, so it
// can be retried; false when the server could not be started.
bool api_start(void);
bool api_is_up(void);
