#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_netif_obj esp_netif_t;
typedef struct {
  struct {
    uint32_t addr;
  } ip;
} esp_netif_ip_info_t;
#define IPSTR "%d.%d.%d.%d"
#define IP2STR(a)                                                                                  \
  (int) ((a)->addr & 0xff), (int) (((a)->addr >> 8) & 0xff), (int) (((a)->addr >> 16) & 0xff),     \
      (int) (((a)->addr >> 24) & 0xff)
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
