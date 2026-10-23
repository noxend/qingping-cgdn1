# Header only: the contract between the core and the plugins. The functions it
# declares are implemented in ui (sdk_impl.c, screen_manager.c).
idf_component_register(INCLUDE_DIRS "include" REQUIRES esp_common)
