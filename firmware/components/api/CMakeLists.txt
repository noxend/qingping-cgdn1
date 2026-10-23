# The device's HTTP server and its own endpoints. It sits on top: it reads every
# layer for /status and the diagnostics. test_hooks.h is where a test build adds more.
idf_component_register(SRCS "server.c" "pages.c" "status.c" "login.c" "wifi_setup.c" "ota.c" "update.c" "device.c"
                            "sensors_http.c" "test_hooks.c"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "private"
                       EMBED_FILES "app.html.gz"
                       EMBED_TXTFILES "shell.js"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES http net ui drivers sensors system secrets app_update
                                     json esp_timer lwip
                                     wasm_host)
