# The preview of a plugin on this computer (preview.sh builds and runs it): the
# device's own sources for the manifest check, the loader, the plugin table, the SDK
# bindings and the drawing, over WAMR built with the firmware's options (test/host_wamr.cmake).
cmake_minimum_required(VERSION 3.16)
project(plugin_preview C ASM)

set(FW ${CMAKE_CURRENT_LIST_DIR}/../../firmware)

include(${FW}/test/host_wamr.cmake)

set(TESTS ${FW}/test)
file(GLOB U8G2_SRCS ${FW}/components/u8g2/*.c)
add_executable(preview
  preview.c
  ${TESTS}/plugins/host_sdk.c
  ${FW}/components/drivers/text.c
  ${FW}/components/ui/gfx_blit.c
  ${FW}/components/wasm_host/wasm_module.c
  ${FW}/components/wasm_host/wasm_binary.c
  ${FW}/components/wasm_host/wasm_bindings.c
  ${FW}/components/wasm_host/wasm_plugins.c
  ${FW}/components/wasm_host/json_scan.c
  ${FW}/components/wasm_host/manifest.c
  ${U8G2_SRCS})
target_include_directories(preview PRIVATE
  ${TESTS}/wasm/stub
  ${TESTS}/plugins/stub
  ${FW}/components/plugin_sdk/include
  ${FW}/components/drivers
  ${FW}/components/u8g2
  ${FW}/components/wasm_host
  ${FW}/components/wasm_host/include)
target_compile_options(preview PRIVATE -w -finput-charset=UTF-8)
target_link_libraries(preview vmlib m)
