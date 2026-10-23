cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# WAMR options the managed component reads as variables, with no Kconfig switch
# (components/wasm_host): the instruction limit per call; no bulk memory, whose
# memory.fill counts as one instruction however much it writes; and the linear
# memory through the host's allocator (PSRAM) instead of os_mmap (internal RAM).
set(WAMR_BUILD_INSTRUCTION_METERING 1 CACHE STRING "WAMR instruction metering")
set(WAMR_BUILD_BULK_MEMORY 0 CACHE STRING "WAMR bulk memory operations")
set(WAMR_BUILD_ALLOC_WITH_USAGE 1 CACHE STRING "WAMR linear memory through the allocator")
# cJSON recurses once per nesting level (default limit 1000), more than a task
# stack holds; screens parse untrusted API responses that are a few levels deep.
idf_build_set_property(COMPILE_DEFINITIONS "CJSON_NESTING_LIMIT=8" APPEND)
# The one build that moves a device from CONFIG_PASS in secrets.h to the hash in
# nvs_cfg (system/creds.c). It holds that password, so it is a DEV build, which
# publish_fw.mjs refuses; build it in its own directory:
# idf.py -B build-migrate -DDEV_BUILD=1 -DCGDN1_MIGRATE=1 build
if(CGDN1_MIGRATE)
  if(NOT DEV_BUILD)
    message(FATAL_ERROR "CGDN1_MIGRATE needs -DDEV_BUILD=1")
  endif()
  idf_build_set_property(COMPILE_DEFINITIONS "CGDN1_MIGRATE=1" APPEND)
endif()
# The image's own version (esp_app_desc_t.version) is FW_VERSION, so an update
# check can read what a downloaded image is before it boots.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/components/api/version.h" FW_VERSION_LINE
     REGEX "^#define FW_VERSION ")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PROJECT_VER "${FW_VERSION_LINE}")
# Read at configure time: a bump must reconfigure, or the image keeps the old version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/components/api/version.h")
project(cgdn1)
