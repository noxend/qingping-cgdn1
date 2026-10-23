# The host check of the WASM plugin path (run.sh builds and runs it). WAMR is built
# from the same managed component as the firmware, with the firmware's options:
# classic interpreter, instruction metering, no builtin libc, no bulk memory, linear
# memory through the allocator, software bounds checks.
cmake_minimum_required(VERSION 3.16)
project(wasm_host_test C ASM)

set(FW ${CMAKE_CURRENT_LIST_DIR}/../..)
set(IDF $ENV{IDF_PATH})
if(NOT IDF)
  set(IDF $ENV{HOME}/Library/Caches/esphome/idf/frameworks/5.5.5)
endif()
include(${FW}/test/host_wamr.cmake)

set(PLUGINS_TEST ${FW}/test/plugins)
file(GLOB U8G2_SRCS ${FW}/components/u8g2/*.c)
add_executable(harness
  harness.c
  ${PLUGINS_TEST}/host_sdk.c
  ${FW}/components/drivers/text.c
  ${FW}/components/ui/gfx_blit.c
  ${FW}/components/wasm_host/wasm_module.c
  ${FW}/components/wasm_host/wasm_binary.c
  ${FW}/components/wasm_host/wasm_bindings.c
  ${FW}/components/wasm_host/wasm_plugins.c
  ${FW}/components/wasm_host/json_scan.c
  ${FW}/components/wasm_host/manifest.c
  ${U8G2_SRCS})
target_include_directories(harness PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stub
  ${PLUGINS_TEST}/stub
  ${FW}/components/plugin_sdk/include
  ${FW}/components/drivers
  ${FW}/components/u8g2
  ${FW}/components/wasm_host
  ${FW}/components/wasm_host/include)
target_compile_options(harness PRIVATE -w -finput-charset=UTF-8)
target_link_libraries(harness vmlib m)
