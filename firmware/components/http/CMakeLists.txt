# The base every HTTP endpoint goes through: the Host check, sign-in (sessions and
# the config password), JSON errors and bounded body reads. Endpoints live with
# their owners (ui, api).
idf_component_register(SRCS "auth.c" "session.c" "session_clock.c" "util.c" "json_out.c" "pass_queue.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_server system
                       PRIV_REQUIRES secrets esp_netif mdns net lwip mbedtls esp_timer)
