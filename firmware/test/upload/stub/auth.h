// Host stand-in: the harness says whether a request carries the password.
#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
bool auth_diag_allowed(httpd_req_t *req);
