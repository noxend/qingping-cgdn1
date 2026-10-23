# WAMR for this computer with the firmware's options (sdkconfig.defaults): the classic
# interpreter with instruction metering, no builtin libc, no WASI, no bulk memory,
# linear memory through the allocator, bounds checked in software as on the device,
# which has no guard pages. Builds the vmlib library. Included by test/wasm, test/upload,
# test/plugins and plugins/preview, after they set FW to the firmware folder.
# WAMR comes from the firmware's build when there is one, else from its upstream release
# at the version the firmware uses (2.4.0).
set(WAMR_ROOT_DIR ${FW}/managed_components/espressif__wasm-micro-runtime)
if(NOT EXISTS ${WAMR_ROOT_DIR})
  include(FetchContent)
  FetchContent_Declare(wamr
                       GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
                       GIT_TAG WAMR-2.4.0 GIT_SHALLOW TRUE)
  FetchContent_Populate(wamr)
  set(WAMR_ROOT_DIR ${wamr_SOURCE_DIR})
endif()

if(APPLE)
  set(WAMR_BUILD_PLATFORM darwin)
else()
  set(WAMR_BUILD_PLATFORM linux)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(WAMR_BUILD_TARGET AARCH64)
else()
  set(WAMR_BUILD_TARGET X86_64)
endif()
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_INSTRUCTION_METERING 1)
set(WAMR_BUILD_BULK_MEMORY 0)
set(WAMR_BUILD_ALLOC_WITH_USAGE 1)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
add_library(vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE})
