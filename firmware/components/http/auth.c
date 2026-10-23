#include "auth.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_netif.h"
#include "mdns.h"
#include "creds.h"
#include "dev_switches.h"
#include "net.h"
#include "pass_queue.h"

static const char *s_mdns_host = "";
static const char *s_ap_ip = "";

void auth_set_hosts(const char *mdns_host, const char *ap_ip) {
  s_mdns_host = mdns_host;
  s_ap_ip = ap_ip;
}

// The station's current address as text, or "" when it has none.
static void sta_ip_str(char *out, size_t cap) {
  out[0] = '\0';
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
    snprintf(out, cap, IPSTR, IP2STR(&ip.ip));
  }
}

static bool host_is_ours(httpd_req_t *req) {
  char host[80] = {0};
  if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
    return false;
  }
  char *colon = strchr(host, ':');
  if (colon) {
    *colon = '\0';
  }
  // The live mDNS name: on a conflict the responder renames itself (cgdn1-2) and
  // keeps answering under that name, so the compile-time one is only the fallback.
  char name[MDNS_NAME_BUF_LEN];
  if (mdns_hostname_get(name) != ESP_OK) {
    snprintf(name, sizeof(name), "%s", s_mdns_host);
  }
  char mdns[MDNS_NAME_BUF_LEN + 8];
  snprintf(mdns, sizeof(mdns), "%s.local", name);
  char sta[16];
  sta_ip_str(sta, sizeof(sta));
  return host[0] && (strcasecmp(host, mdns) == 0 || strcmp(host, s_ap_ip) == 0 ||
                     (sta[0] && strcmp(host, sta) == 0));
}

bool auth_captive_redirect(httpd_req_t *req) {
  if (host_is_ours(req) || !net_is_ap_client(httpd_req_to_sockfd(req))) {
    return false;
  }
  char location[32];
  snprintf(location, sizeof(location), "http://%s/", s_ap_ip);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", location);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  // iOS opens its sign-in sheet only for a reply with a body.
  httpd_resp_sendstr(req, "CGDN1 setup");
  return true;
}

bool auth_host_ok(httpd_req_t *req) {
  if (host_is_ours(req)) {
    return true;
  }
  if (!auth_captive_redirect(req)) {
    httpd_resp_set_status(req, "421 Misdirected Request");
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_sendstr(req, "{\"error\":\"wrong host\"}");
  }
  return false;
}

uint32_t auth_client(httpd_req_t *req) {
  uint32_t ip = 0;
  net_peer_ipv4(httpd_req_to_sockfd(req), &ip);
  return ip;
}

bool auth_origin_ok(httpd_req_t *req) {
  char origin[96] = {0};
  esp_err_t e = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
  if (e == ESP_ERR_NOT_FOUND) {
    return true;
  }
  char host[80] = {0};
  bool ok = false;
  if (e == ESP_OK && httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
    char own[96];
    snprintf(own, sizeof(own), "http://%s", host);
    ok = strcasecmp(origin, own) == 0;
  }
  if (!ok) {
    http_error(req, HTTPD_403_FORBIDDEN, "a page of another site cannot use this device");
  }
  return ok;
}

void auth_session_token(httpd_req_t *req, char out[SESSION_TOKEN_BUF]) {
  size_t len = SESSION_TOKEN_BUF;
  if (httpd_req_get_cookie_val(req, SESSION_COOKIE, out, &len) != ESP_OK) {
    out[0] = '\0';
  }
}

void auth_set_cookie(httpd_req_t *req, const char *token) {
  static char cookie[160];
  snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict",
           SESSION_COOKIE, token ? token : "", token ? SESSION_MAX_AGE_S : 0);
  httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

// The password of an "Authorization: Bearer" header into pass. False without one,
// with *missing set when the request has no Authorization header at all.
static bool bearer_pass(httpd_req_t *req, char pass[AUTH_PASS_BUF], bool *missing) {
  static const char BEARER[] = "Bearer ";
  char header[sizeof(BEARER) + AUTH_PASS_BUF] = {0};
  esp_err_t e = httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header));
  *missing = e == ESP_ERR_NOT_FOUND;
  if (e != ESP_OK || strncmp(header, BEARER, sizeof(BEARER) - 1) != 0) {
    return false;  // none, too long for the buffer, or not Bearer
  }
  const char *value = header + sizeof(BEARER) - 1;
  if (strlen(value) > CREDS_PASS_MAX) {
    return false;  // one character too long: refused, not cut to a password
  }
  strlcpy(pass, value, AUTH_PASS_BUF);
  return true;
}

// True for a valid session cookie; the cookie goes out again when its expiry moved.
static bool session_ok(httpd_req_t *req) {
  char token[SESSION_TOKEN_BUF];
  auth_session_token(req, token);
  if (!token[0]) {
    return false;
  }
  session_result_t r = session_check(token);
  if (r == SESSION_EXTENDED) {
    auth_set_cookie(req, token);
  }
  return r != SESSION_NONE;
}

// CREDS_OK for a valid session or the right password, else why not. *missing is
// set when the request carried neither.
static creds_check_t credential(httpd_req_t *req, bool *missing) {
  *missing = false;
  if (session_ok(req)) {
    return CREDS_OK;
  }
  if (!creds_config_is_set()) {
    return CREDS_NOT_SET;
  }
  char pass[AUTH_PASS_BUF];
  if (!bearer_pass(req, pass, missing)) {
    return CREDS_WRONG;
  }
  creds_check_t known;
  if (pass_queue_result(req, pass, &known)) {
    return known;
  }
  creds_check_t result = creds_config_try(pass, auth_client(req));
  // The wrapper queued what needs a hash; a wait that ended in between is a retry.
  return result == CREDS_NEEDS_HASH ? CREDS_BUSY : result;
}

bool auth_bearer_needs_hash(httpd_req_t *req, char pass[AUTH_PASS_BUF]) {
  creds_check_t known;
  bool missing;
  if (!bearer_pass(req, pass, &missing) || pass_queue_result(req, pass, &known)) {
    return false;
  }
  // credential() takes a session before the password, and so does this: a valid
  // cookie with a stale header is not held up by a hash.
  return !session_ok(req) && creds_config_try(pass, auth_client(req)) == CREDS_NEEDS_HASH;
}

bool auth_signed_in(httpd_req_t *req) {
  bool missing;
  return credential(req, &missing) == CREDS_OK;
}

bool auth_request(httpd_req_t *req) {
  if (!auth_origin_ok(req)) {
    return false;
  }
  bool missing;
  creds_check_t result = credential(req, &missing);
  if (missing) {
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
    http_error(req, HTTPD_401_UNAUTHORIZED, "sign in first");
    return false;
  }
  return auth_answer(req, result);
}

bool auth_diag_allowed(httpd_req_t *req) {
  return DEV_OPEN_DIAGNOSTICS || auth_signed_in(req);
}

bool auth_diag_request(httpd_req_t *req) {
  return DEV_OPEN_DIAGNOSTICS || auth_request(req);
}

bool auth_answer(httpd_req_t *req, creds_check_t result) {
  switch (result) {
  case CREDS_OK:
    return true;
  case CREDS_NOT_SET:
    http_error(req, HTTPD_403_FORBIDDEN, "no config password yet: set one on the setup page");
    return false;
  case CREDS_BUSY:
    httpd_resp_set_hdr(req, "Retry-After", "1");
    http_error(req, HTTP_429_TOO_MANY, "wrong password, wait a second");
    return false;
  default:
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
    http_error(req, HTTPD_401_UNAUTHORIZED, "wrong config password");
    return false;
  }
}
