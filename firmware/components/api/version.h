// The firmware's version, shown in GET /status and the boot log; bump it on every
// flash so builds can be told apart.
#pragma once

#define FW_VERSION "1.0.0-rc21"
// The HTTP API contract the hosted web interface is written against: the JSON
// shapes of /status, /config, /screen, /sensors, /alerts and the JSON errors.
// Bump it on any change a published interface could trip over.
#define API_VERSION 2  // 2: sign-in, POST /login and a session cookie, or Authorization: Bearer
