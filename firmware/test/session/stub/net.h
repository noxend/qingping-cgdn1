#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define NET_AP_IP "10.47.0.1"
#define NET_SSID_MAX 32
#define NET_PASS_MAX 64
bool net_peer_ipv4(int sockfd, uint32_t *out);
bool net_is_ap_client(int sockfd);
const char *net_scan_json(void);
esp_err_t net_save_creds(const char *ssid, const char *pass);
