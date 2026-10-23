// The network: the home Wi-Fi station, the setup access point, mDNS and the clock.
//
// The device runs an access point "CGDN1-setup-XXXX" on 10.47.0.1 while it has no
// home WiFi, for a while after it joins one, whenever that network is lost, and
// after a hold on the touch strip. The credentials entered on its setup page are
// kept in NVS and the station joins that network, retrying for ever with backoff.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// mDNS host, so the config page is reached as http://cgdn1.local/ with no fixed
// IP or router setup.
#define NET_MDNS_HOSTNAME "cgdn1"
#define NET_AP_IP "10.47.0.1"

// Both are the Wi-Fi driver's field sizes: a 32-byte SSID and a 64-hex-digit PSK fill
// their fields with no terminator, which the driver accepts.
#define NET_SSID_MAX 32
#define NET_PASS_MAX 64

// Loads the saved network, brings up the station and the setup AP, mDNS and SNTP
// (time zone Kyiv). Call once, after NVS init.
void net_start(void);

// The name the device answers to on the LAN, "cgdn1.local" (mDNS may have renamed it).
void net_host(char *out, size_t cap);
// Why the station cannot join the saved network: "password", "not_found", or "" when
// it is online, has no network saved, or the reason says nothing to the owner.
const char *net_join_problem(void);
// A home network is saved (it may still be out of reach).
bool net_has_network(void);
// The station holds an address from the home network.
bool net_online(void);
// That address as text ("192.168.1.20") into out; false while there is none.
bool net_ip(char *out, size_t cap);

// An HTTP request arrived on socket `sockfd`: when its client is on the setup AP,
// note that the AP was used (the rollback gate counts that as healthy).
void net_note_request(int sockfd);
// True for a client of the setup AP talking to the AP address.
bool net_is_ap_client(int sockfd);
// The client's IPv4 address (network order) on socket `sockfd`.
bool net_peer_ipv4(int sockfd, uint32_t *out);
// The setup AP's name, CGDN1-setup-XXXX.
void net_ap_ssid(char *out, size_t cap);
bool net_ap_hit(void);

bool net_ap_is_up(void);

// For GET /status.
typedef struct {
  bool have_ap;  // associated with the router: ssid, rssi and channel are filled
  uint8_t ssid[33];
  int rssi;
  int channel;
  int disc_count;   // how often the router was lost
  int disc_reason;  // why, the last time
  int disc_at_s;    // when, in seconds of uptime
  int ip_count;     // addresses received since boot
} net_stats_t;

void net_stats(net_stats_t *out);

// Nearby networks as {"nets":[...]}, rescanned at most every 20 s.
const char *net_scan_json(void);

// Saves the home network; the station joins it after the next boot.
esp_err_t net_save_creds(const char *ssid, const char *pass);
// The home network was saved just before this boot, so the screen shows where to open
// the device once it joins.
bool net_saved_before_boot(void);
// Clears the copy of the station settings the Wi-Fi driver keeps in NVS
// (esp_wifi_restore). For factory_reset; the device restarts right after.
void net_forget_driver_creds(void);
