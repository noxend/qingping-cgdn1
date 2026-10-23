# The screens: the UI loop that runs the plugins, the page carousel, the SDK's
# implementation and the screen endpoints (/config, /screen, /plugin/stats, /alerts).
idf_component_register(SRCS "screen_manager.c" "screen_http.c" "carousel.c" "sdk_impl.c"
                            "gfx_blit.c"
                            "night.c" "night_rules.c" "menu.c"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "private"
                       REQUIRES plugin_sdk drivers esp_http_server
                       PRIV_REQUIRES system net sensors http esp_timer json nvs_flash
                                     pthread wasm_host qrcodegen)
# gfx_blit.c holds a call only the WASM host's bindings make; nothing in ui references
# it, so without this the link leaves gfx_blit undefined (build-system guide, "Advanced
# Workaround: Undefined Symbols").
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u gfx_blit")
