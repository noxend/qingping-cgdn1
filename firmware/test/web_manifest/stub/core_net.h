#pragma once
#include "plugin_sdk.h"
#define NET_OWNER_CORE (-2)
#define NET_OWNER_FW (-3)
static inline bool core_net_submit(int o, const char *u, size_t m, core_net_parse_fn p) {
  return true;
}
