# The host check of the standard plugins (run.sh builds and runs it): the device's
# plugin table, SDK bindings and manifest check over WAMR built with the firmware's
# options, drawing with the real text.c, gfx_blit.c and u8g2.
cmake_minimum_required(VERSION 3.16)
project(plugins_test C ASM)

set(FW ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${FW}/test/host_wamr.cmake)

file(GLOB U8G2_SRCS ${FW}/components/u8g2/*.c)
add_executable(harness
  harness.c
  host_sdk.c
  ${FW}/components/drivers/text.c
  ${FW}/components/ui/gfx_blit.c
  ${FW}/components/ui/menu.c
  ${FW}/components/qrcodegen/qrcodegen.c
  ${FW}/components/wasm_host/wasm_plugins.c
  ${FW}/components/wasm_host/wasm_module.c
  ${FW}/components/wasm_host/wasm_binary.c
  ${FW}/components/wasm_host/wasm_bindings.c
  ${FW}/components/wasm_host/json_scan.c
  ${FW}/components/wasm_host/manifest.c
  ${U8G2_SRCS})
target_include_directories(harness PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/stub
  ${FW}/test/wasm/stub
  ${FW}/components/plugin_sdk/include
  ${FW}/components/drivers
  ${FW}/components/ui
  ${FW}/components/qrcodegen
  ${FW}/components/u8g2
  ${FW}/components/wasm_host
  ${FW}/components/wasm_host/include)
target_compile_options(harness PRIVATE -w -finput-charset=UTF-8)
target_link_libraries(harness vmlib m)
