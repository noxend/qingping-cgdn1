# Screen plugins: WebAssembly modules run by WAMR's classic interpreter with every
# allocation in PSRAM, and the standard plugins the image carries.
idf_component_register(SRCS "wasm_host.c" "wasm_plugins.c" "wasm_store.c" "wasm_module.c"
                            "wasm_bindings.c" "wasm_binary.c" "json_scan.c" "manifest.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES plugin_sdk esp_http_server
                       PRIV_REQUIRES espressif__wasm-micro-runtime net http system esp_psram
                                     nvs_flash esp_timer mbedtls json)

# The standard plugins, built from plugins/standard/<id> by plugins/build.sh (wasi-sdk)
# and embedded as _binary_<id>_wasm_start/_end, the names wasm_host.c's STANDARD table
# links against. The order is the install order of wasm_host.c; a folder under
# plugins/standard that is not in the list stops the configure.
set(PLUGINS "${CMAKE_CURRENT_LIST_DIR}/../../../plugins")
set(STANDARD_PLUGINS alerts clock humidity temperature co2)
file(GLOB folders LIST_DIRECTORIES true CONFIGURE_DEPENDS RELATIVE "${PLUGINS}/standard"
     "${PLUGINS}/standard/*")
foreach(folder ${folders})
  if(IS_DIRECTORY "${PLUGINS}/standard/${folder}" AND NOT folder IN_LIST STANDARD_PLUGINS)
    message(FATAL_ERROR "plugins/standard/${folder} is not in STANDARD_PLUGINS here "
                        "nor in the STANDARD table of wasm_host.c")
  endif()
endforeach()
file(GLOB SDK_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${PLUGINS}/sdk/*")
set(outdir "${CMAKE_CURRENT_BINARY_DIR}/standard")
foreach(id ${STANDARD_PLUGINS})
  set(src "${PLUGINS}/standard/${id}")
  set(out "${outdir}/${id}.wasm")
  file(GLOB sources CONFIGURE_DEPENDS "${src}/*.c" "${src}/*.h" "${src}/*.json" "${src}/*.bin")
  add_custom_command(OUTPUT "${out}"
                     COMMAND "${CMAKE_COMMAND}" -E make_directory "${outdir}"
                     COMMAND "${PLUGINS}/build.sh" "${src}" "${out}"
                     DEPENDS ${sources} ${SDK_FILES} "${PLUGINS}/build.sh"
                     VERBATIM)
  target_add_binary_data(${COMPONENT_LIB} "${out}" BINARY)
endforeach()
