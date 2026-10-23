// The point where a test build adds its own endpoints. The image itself has none:
// api_test_hooks_register below is a weak default that registers nothing. A component
// linked with WHOLE_ARCHIVE that defines it (the private cgdn1-dev repository's
// test_hooks, built in with -DEXTRA_COMPONENT_DIRS) replaces it.
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// Registers the test endpoints on srv, with http_register or http_register_open.
// ESP_OK when it did, ESP_ERR_NOT_SUPPORTED when the build has none (the default); any
// other error stops the server as a failed registration does.
esp_err_t api_test_hooks_register(httpd_handle_t srv);
