# The chip's own services: storage, crash log, stall watch, power latch, the
# watchdogs and the rollback gate. Knows nothing of the network or the screens.
idf_component_register(SRCS "settings.c" "crash_log.c" "stall_watch.c" "power_latch.c"
                            "rollback.c" "system.c" "factory_reset.c" "creds.c" "creds_rules.c"
                            "heap_large.c" "json_escape.c" "factory_record.c"
                            "factory_record_parse.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES app_update esp_driver_gpio esp_timer json nvs_flash secrets mbedtls
                                     bootloader_support esp_partition)
# The crash log wraps the panic handler to keep the last crash across the reboot.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
