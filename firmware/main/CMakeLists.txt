idf_component_register(SRCS "main.c")
# main depends on every component by default (build-system guide), so it lists none.
# `idf.py -DDEV_BUILD=1 build` allows the DEV_* switches in secrets.h; without it a
# build with either switch on fails, so a bench setting cannot ship. CMake keeps the
# value in its cache: pass -DDEV_BUILD=0 (or run fullclean) to go back.
if(DEV_BUILD)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE DEV_BUILD=1)
endif()
