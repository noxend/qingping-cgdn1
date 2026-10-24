#include "net.h"
#include "setup_ap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "mdns.h"
#include "nvs.h"
#include "creds.h"
#include "dns_answer.h"
#include "factory_reset.h"
#include "json_escape.h"

#define NVS_NS FACTORY_WIFI_NS

// Setup AP subnet. Deliberately unusual so it does not collide with the home
// LAN (192.168.4.0/24, the ESP-IDF default, is used by some routers).
#define AP_IP_HOST 0x0A2F0001u
#define AP_MASK_HOST 0xFFFFFF00u

#define AP_LINGER_US (5LL * 60 * 1000 * 1000)  // AP stays this long after the station joins
#define AP_HOLD_US (10LL * 60 * 1000 * 1000)   // and this long after a hold on the strip

#define BIT_STA_IP BIT0
#define BIT_AP_HIT BIT1

static const char *TAG = "cgdn1";
static EventGroupHandle_t s_ev;
static bool s_have_creds = false;
static int s_backoff_s = 5;
// What the station last went through, for GET /status: why it lost the router, how
// often, and when (seconds of uptime).
static volatile int s_disc_reason;
static volatile int s_disc_count;
static volatile int s_disc_at_s;
static volatile int s_ip_count;
static esp_timer_handle_t s_reconnect_timer;

// ---- the setup AP's clients -------------------------------------------------

static bool sockaddr_ipv4(const struct sockaddr_storage *ss, uint32_t *out) {
  if (ss->ss_family == AF_INET) {
    *out = ((const struct sockaddr_in *) ss)->sin_addr.s_addr;
    return true;
  }
  if (ss->ss_family == AF_INET6) {
    static const uint8_t v4mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    const uint8_t *a = ((const struct sockaddr_in6 *) ss)->sin6_addr.s6_addr;
    if (memcmp(a, v4mapped, sizeof(v4mapped)) != 0) {
      return false;
    }
    memcpy(out, a + 12, 4);
    return true;
  }
  return false;
}

// True only for a client inside the setup AP subnet talking to the AP address.
// Checking the peer, not just the local address, keeps a home-LAN host that
// routes to 10.47.0.1 through the station interface out.
bool net_is_ap_client(int fd) {
  if (fd < 0) {
    return false;
  }
  struct sockaddr_storage local;
  struct sockaddr_storage peer;
  socklen_t llen = sizeof(local);
  socklen_t plen = sizeof(peer);
  if (getsockname(fd, (struct sockaddr *) &local, &llen) != 0 ||
      getpeername(fd, (struct sockaddr *) &peer, &plen) != 0) {
    return false;
  }
  uint32_t l = 0;
  uint32_t p = 0;
  if (!sockaddr_ipv4(&local, &l) || !sockaddr_ipv4(&peer, &p)) {
    return false;
  }
  uint32_t net = htonl(AP_IP_HOST & AP_MASK_HOST);
  uint32_t mask = htonl(AP_MASK_HOST);
  return l == htonl(AP_IP_HOST) && (p & mask) == net;
}

bool net_peer_ipv4(int fd, uint32_t *out) {
  struct sockaddr_storage peer;
  socklen_t plen = sizeof(peer);
  if (fd < 0 || getpeername(fd, (struct sockaddr *) &peer, &plen) != 0) {
    return false;
  }
  return sockaddr_ipv4(&peer, out);
}

void net_note_request(int sockfd) {
  if (net_is_ap_client(sockfd)) {
    xEventGroupSetBits(s_ev, BIT_AP_HIT);
  }
}

// ---- NVS: stored WiFi credentials -----------------------------------------
static bool creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  bool ok = nvs_get_str(h, "ssid", ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(h, "pass", pass, &pass_len) == ESP_OK && ssid[0] != '\0';
  nvs_close(h);
  return ok;
}

// Set by a save, read at the next boot: the restart that follows a save keeps .noinit
// (memory-types guide); after a power cycle it holds noise, which the magic rejects.
#define SAVED_MAGIC 0x5A7EC0DEu
static __NOINIT_ATTR uint32_t s_saved_magic;
static bool s_saved_before_boot;

esp_err_t net_save_creds(const char *ssid, const char *pass) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_set_str(h, "ssid", ssid);
  if (err == ESP_OK) {
    err = nvs_set_str(h, "pass", pass);
  }
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err == ESP_OK) {
    s_saved_magic = SAVED_MAGIC;
  }
  return err;
}

// ---- Wi-Fi scan -------------------------------------------------------------
// Scan nearby networks (blocking). Returns the count written to recs.
static int wifi_scan(wifi_ap_record_t *recs, int max) {
  wifi_scan_config_t sc = {0};
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
    return 0;
  }
  uint16_t n = (uint16_t) max;
  if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
    return 0;
  }
  return n;
}

// Cached network scan as a JSON array. Cache bounds how often we scan, so a
// flood of requests cannot DoS the device or keep knocking STA off-channel.
static char s_scan_json[1200];
static int64_t s_scan_at = 0;

const char *net_scan_json(void) {
  int64_t now = esp_timer_get_time();
  if (s_scan_at != 0 && now - s_scan_at < 20LL * 1000 * 1000 && s_scan_json[0]) {
    return s_scan_json;
  }
  wifi_ap_record_t recs[20];
  int cnt = wifi_scan(recs, 20);
  int n = snprintf(s_scan_json, sizeof(s_scan_json), "{\"nets\":[");
  char seen[20][NET_SSID_MAX + 1];
  int nseen = 0;
  char je[NET_SSID_MAX * JSON_ESCAPE_MAX + 1];
  for (int i = 0; i < cnt && nseen < 20; i++) {
    const char *s = (const char *) recs[i].ssid;
    if (s[0] == '\0') {
      continue;
    }
    bool dup = false;
    for (int j = 0; j < nseen; j++) {
      if (strcmp(seen[j], s) == 0) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    strncpy(seen[nseen], s, NET_SSID_MAX);
    seen[nseen][NET_SSID_MAX] = '\0';
    json_escape(je, sizeof(je), s);
    n += snprintf(s_scan_json + n, sizeof(s_scan_json) - n, "%s\"%s\"", nseen ? "," : "", je);
    nseen++;
    if (n >= (int) sizeof(s_scan_json) - 260) {
      break;
    }
  }
  snprintf(s_scan_json + n, sizeof(s_scan_json) - n, "]}");
  s_scan_at = now;
  return s_scan_json;
}

// ---- WiFi -----------------------------------------------------------------
static void schedule_reconnect(void);
static void ap_want(bool on, int64_t delay_us);
static volatile int64_t s_ap_linger_us;

// esp_wifi_connect can fail at once (no event follows), so a failed call must
// schedule the next try itself or the station stays down until a reboot.
static void connect_or_retry(void) {
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "wifi connect: %s", esp_err_to_name(err));
    schedule_reconnect();
  }
}

static void reconnect_cb(void *arg) {
  connect_or_retry();
}

// Never give up on the home WiFi: retry with backoff (5 s doubling to 60 s),
// which also limits the channel scans that disturb the setup AP.
static void schedule_reconnect(void) {
  if (!s_have_creds) {
    return;
  }
  if (esp_timer_start_once(s_reconnect_timer, (uint64_t) s_backoff_s * 1000000) == ESP_OK) {
    ESP_LOGW(TAG, "wifi down, retry in %d s", s_backoff_s);
    s_backoff_s = s_backoff_s >= 30 ? 60 : s_backoff_s * 2;
  }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (s_have_creds) {
      connect_or_retry();
    }
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *d = data;
    s_disc_reason = d->reason;
    s_disc_count++;
    s_disc_at_s = (int) (esp_timer_get_time() / 1000000);
    xEventGroupClearBits(s_ev, BIT_STA_IP);
    schedule_reconnect();
    ap_want(true, 0);  // the home network is gone: the AP is the way back in
  } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
    // Still associated, but the lease lapsed: without an address the LAN cannot
    // reach the device, so the AP is the way in again.
    ESP_LOGW(TAG, "lost ip");
    xEventGroupClearBits(s_ev, BIT_STA_IP);
    ap_want(true, 0);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *e = (ip_event_got_ip_t *) data;
    ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&e->ip_info.ip));
    s_backoff_s = 5;
    s_ip_count++;
    xEventGroupSetBits(s_ev, BIT_STA_IP);
    if (s_ap_linger_us == 0) {
      ap_want(false, AP_LINGER_US);  // a setup session over the AP can still finish
    } else {
      ap_want(true, 0);  // a hold is pending: let the timer start its countdown now
    }
  }
}

// ---- setup AP lifetime ------------------------------------------------------
// The AP runs while the device has no home network, for a while after it joins
// one (so a setup session can finish), and on request from the touch strip. Mode
// changes run on the timer task, never inside a Wi-Fi event handler.
static esp_timer_handle_t s_ap_timer;
static volatile bool s_ap_target = true;
// s_ap_linger_us (declared above): after turning on, turn off again this much later.

static void ap_apply_cb(void *arg) {
  wifi_mode_t mode = WIFI_MODE_NULL;
  esp_wifi_get_mode(&mode);
  wifi_mode_t want = s_ap_target ? WIFI_MODE_APSTA : WIFI_MODE_STA;
  if (mode != want) {
    esp_err_t err = esp_wifi_set_mode(want);
    ESP_LOGI(TAG, "setup AP %s: %s", s_ap_target ? "on" : "off", esp_err_to_name(err));
  }
  if (s_ap_target && s_ap_linger_us > 0 && (xEventGroupGetBits(s_ev) & BIT_STA_IP)) {
    uint64_t linger = (uint64_t) s_ap_linger_us;
    s_ap_linger_us = 0;
    s_ap_target = false;
    // An ap_want from another task may have re-armed the timer meanwhile; then
    // start_once refuses and restart keeps the countdown instead of a 1 ms fire.
    if (esp_timer_start_once(s_ap_timer, linger) != ESP_OK) {
      esp_timer_restart(s_ap_timer, linger);
    }
  }
}

// Asks for the AP to be on or off, after `delay_us` (0 = as soon as the timer task
// runs). A later call replaces an earlier pending one.
static void ap_want(bool on, int64_t delay_us) {
  s_ap_target = on;
  if (!s_ap_timer) {
    return;
  }
  esp_timer_stop(s_ap_timer);
  esp_timer_start_once(s_ap_timer, delay_us > 0 ? (uint64_t) delay_us : 1000);
}

void net_forget_driver_creds(void) {
  esp_err_t err = esp_wifi_restore();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_restore: %s", esp_err_to_name(err));
  }
}

// The AP name carries the MAC suffix so several units can be told apart.
void net_ap_ssid(char *out, size_t cap) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(out, cap, "CGDN1-setup-%02X%02X", mac[4], mac[5]);
}

bool net_ip(char *out, size_t cap) {
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  if (!net_online() || !sta || esp_netif_get_ip_info(sta, &ip) != ESP_OK) {
    return false;
  }
  snprintf(out, cap, IPSTR, IP2STR(&ip.ip));
  return true;
}

void setup_ap_show(void) {
  if (!s_ev) {
    return;  // before net_start
  }
  s_ap_linger_us = AP_HOLD_US;  // off again later, if the station is still connected then
  ap_want(true, 0);
}

static void ap_set_subnet(esp_netif_t *ap) {
  esp_netif_ip_info_t ip = {0};
  ip.ip.addr = htonl(AP_IP_HOST);
  ip.gw.addr = htonl(AP_IP_HOST);
  ip.netmask.addr = htonl(AP_MASK_HOST);
  esp_netif_dhcps_stop(ap);
  ESP_ERROR_CHECK(esp_netif_set_ip_info(ap, &ip));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
}

// ---- the setup AP's DNS ---------------------------------------------------------
// The DHCP offer names the AP as the DNS server (LWIP_DHCPS_ADD_DNS), and every name
// resolves to it: the phone's captive-portal check reaches this server, auth.c
// redirects it to the setup page, and the phone opens that page by itself. A raw
// lwIP pcb, so it takes none of the ten sockets and no task. DHCP option 114 is not
// offered: RFC 8910 wants an HTTPS URI there, which the device cannot serve.
#define DNS_PORT 53
#define DNS_REPLY_MAX 300

static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                     u16_t port) {
  uint32_t peer = IP_IS_V4(addr) ? ip_2_ip4(addr)->addr : 0;
  bool from_ap = (peer & htonl(AP_MASK_HOST)) == htonl(AP_IP_HOST & AP_MASK_HOST);
  if (from_ap && p->len == p->tot_len) {
    struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, DNS_REPLY_MAX, PBUF_RAM);
    if (reply) {
      int n = dns_answer(p->payload, p->len, htonl(AP_IP_HOST), reply->payload, DNS_REPLY_MAX);
      if (n > 0) {
        pbuf_realloc(reply, n);
        udp_sendto(pcb, reply, addr, port);
      }
      pbuf_free(reply);
    }
  }
  pbuf_free(p);
}

// Runs in the TCP/IP thread (esp_netif_tcpip_exec), as the raw API needs.
static esp_err_t dns_open(void *ctx) {
  struct udp_pcb *pcb = udp_new();
  if (!pcb) {
    return ESP_ERR_NO_MEM;
  }
  ip_addr_t ap = IPADDR4_INIT(htonl(AP_IP_HOST));
  if (udp_bind(pcb, &ap, DNS_PORT) != ERR_OK) {
    udp_remove(pcb);
    return ESP_FAIL;
  }
  udp_recv(pcb, dns_recv, NULL);
  return ESP_OK;
}

static void wifi_start(const char *ssid, const char *pass) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();
  ap_set_subnet(ap_netif);

  wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&ic));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_wifi, NULL));

  const esp_timer_create_args_t ta = {.callback = reconnect_cb, .name = "wifi_retry"};
  ESP_ERROR_CHECK(esp_timer_create(&ta, &s_reconnect_timer));
  const esp_timer_create_args_t aa = {.callback = ap_apply_cb, .name = "setup_ap"};
  ESP_ERROR_CHECK(esp_timer_create(&aa, &s_ap_timer));

  wifi_config_t ap = {0};
  net_ap_ssid((char *) ap.ap.ssid, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen((char *) ap.ap.ssid);
  creds_ap_pass((char *) ap.ap.password, sizeof(ap.ap.password));
  ap.ap.max_connection = 2;
  // WPA3 where the phone can, WPA2 otherwise; PMF required is what WPA3 on a SoftAP
  // needs (wifi-security guide), and it also closes the deauth-and-capture route to
  // an offline dictionary attack on the WPA2 handshake.
  ap.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
  ap.ap.pmf_cfg.required = true;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
  if (s_have_creds) {
    wifi_config_t sta = {0};
    memcpy(sta.sta.ssid, ssid, strnlen(ssid, sizeof(sta.sta.ssid)));
    memcpy(sta.sta.password, pass, strnlen(pass, sizeof(sta.sta.password)));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
  }
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_err_t dns = esp_netif_tcpip_exec(dns_open, NULL);
  if (dns != ESP_OK) {
    ESP_LOGW(TAG, "setup AP dns: %s", esp_err_to_name(dns));  // the page still opens by address
  }
  ESP_LOGI(TAG, "AP '%s' up at " NET_AP_IP "; %s", ap.ap.ssid,
           s_have_creds ? "joining saved WiFi" : "no WiFi saved, open the setup page");
}

// Advertise the host and an _http._tcp record on both interfaces. A convenience
// only: on any failure the device still works by IP, so nothing here aborts or
// gates the rollback.
static void mdns_start(void) {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mdns init failed: %s", esp_err_to_name(err));
    return;
  }
  mdns_hostname_set(NET_MDNS_HOSTNAME);
  mdns_instance_name_set("CGDN1 Air Monitor");
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  ESP_LOGI(TAG, "mdns up: http://" NET_MDNS_HOSTNAME ".local/");
}

// ---- time ---------------------------------------------------------------------
// The core owns the clock: one time zone for the device and an NTP client started
// at boot, which retries on its own until the station is up. Plugins read localtime.
#define TZ_KYIV "EET-2EEST,M3.5.0/3,M10.5.0/4"  // Ukraine, with the EU daylight-saving rules
#define NTP_SERVER "pool.ntp.org"

static void time_start(void) {
  setenv("TZ", TZ_KYIV, 1);
  tzset();
  esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
  esp_err_t err = esp_netif_sntp_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "sntp init: %s", esp_err_to_name(err));
  }
}

// ---- state for the rest of the firmware -------------------------------------

void net_start(void) {
  s_saved_before_boot = s_saved_magic == SAVED_MAGIC;
  s_saved_magic = 0;
  s_ev = xEventGroupCreate();
  char ssid[NET_SSID_MAX + 1] = "";
  char pass[NET_PASS_MAX + 1] = "";
  s_have_creds = creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
  wifi_start(ssid, pass);
  mdns_start();
  time_start();
}

void net_host(char *out, size_t cap) {
  char name[MDNS_NAME_BUF_LEN];
  if (mdns_hostname_get(name) != ESP_OK) {
    snprintf(name, sizeof(name), "%s", NET_MDNS_HOSTNAME);
  }
  snprintf(out, cap, "%s.local", name);
}

const char *net_join_problem(void) {
  if (!s_have_creds || net_online() || s_disc_count == 0) {
    return "";
  }
  switch (s_disc_reason) {
  case WIFI_REASON_MIC_FAILURE:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
    return "password";
  case WIFI_REASON_NO_AP_FOUND:
  case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
  case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
  case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
    return "not_found";
  default:
    return "";
  }
}

bool net_saved_before_boot(void) {
  return s_saved_before_boot;
}

bool net_has_network(void) {
  return s_have_creds;
}

bool net_online(void) {
  return (xEventGroupGetBits(s_ev) & BIT_STA_IP) != 0;
}

bool net_ap_hit(void) {
  return (xEventGroupGetBits(s_ev) & BIT_AP_HIT) != 0;
}

bool net_ap_is_up(void) {
  wifi_mode_t mode = WIFI_MODE_NULL;
  return esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_AP);
}

void net_stats(net_stats_t *out) {
  wifi_ap_record_t ap = {0};
  out->have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  memcpy(out->ssid, ap.ssid, sizeof(out->ssid));
  out->rssi = out->have_ap ? ap.rssi : 0;
  out->channel = out->have_ap ? ap.primary : 0;
  out->disc_count = s_disc_count;
  out->disc_reason = s_disc_reason;
  out->disc_at_s = s_disc_at_s;
  out->ip_count = s_ip_count;
}
