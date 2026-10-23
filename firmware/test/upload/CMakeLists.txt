# The host check of wasm_host.c (run.sh builds and runs it): the device's wasm_host.c with
# its plugin table, store, manifest check, loader and bindings, over WAMR built with the
# firmware's options as in test/wasm, the in-memory NVS of test/store, and the standard
# plugins embedded from DEFAULTS_ASM.
cmake_minimum_required(VERSION 3.16)
project(upload_test C ASM)

set(FW ${CMAKE_CURRENT_LIST_DIR}/../..)
set(IDF $ENV{IDF_PATH})
if(NOT IDF)
  set(IDF $ENV{HOME}/Library/Caches/esphome/idf/frameworks/5.5.5)
endif()
set(MBEDTLS_DIR ${IDF}/components/mbedtls/mbedtls)

include(${FW}/test/host_wamr.cmake)

set(TESTS ${FW}/test)
file(GLOB U8G2_SRCS ${FW}/components/u8g2/*.c)
add_executable(harness
  harness.c
  ${DEFAULTS_ASM}
  ${TESTS}/plugins/host_sdk.c
  ${TESTS}/store/fake_nvs.c
  ${TESTS}/store/fake_settings.c
  ${FW}/components/drivers/text.c
  ${FW}/components/ui/gfx_blit.c
  ${FW}/components/http/json_out.c
  ${FW}/components/wasm_host/wasm_host.c
  ${FW}/components/wasm_host/wasm_plugins.c
  ${FW}/components/wasm_host/wasm_store.c
  ${FW}/components/wasm_host/wasm_module.c
  ${FW}/components/wasm_host/wasm_binary.c
  ${FW}/components/wasm_host/wasm_bindings.c
  ${FW}/components/wasm_host/json_scan.c
  ${FW}/components/wasm_host/manifest.c
  ${IDF}/components/json/cJSON/cJSON.c
  ${MBEDTLS_DIR}/library/sha256.c
  ${MBEDTLS_DIR}/library/platform_util.c
  ${U8G2_SRCS})
# cJSON as the firmware builds it (firmware/CMakeLists.txt).
target_compile_definitions(harness PRIVATE CJSON_NESTING_LIMIT=8)
# This test's stubs first: they stand in for the server, auth and the heap.
target_include_directories(harness PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stub
  ${TESTS}/wasm/stub
  ${TESTS}/store/stub
  ${TESTS}/plugins/stub
  ${IDF}/components/json/cJSON
  ${MBEDTLS_DIR}/include
  ${MBEDTLS_DIR}/library
  ${FW}/components/system
  ${FW}/components/http
  ${FW}/components/plugin_sdk/include
  ${FW}/components/drivers
  ${FW}/components/u8g2
  ${FW}/components/wasm_host
  ${FW}/components/wasm_host/include)
target_compile_options(harness PRIVATE -w -finput-charset=UTF-8)
target_link_libraries(harness vmlib m)
