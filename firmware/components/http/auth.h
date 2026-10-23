// Request authentication shared by the HTTP modules.
//
// A browser signs in once on POST /login and then carries a session cookie (session.h).
// A tool sends the config password with every request instead, as
// "Authorization: Bearer <password>". Either one opens every endpoint.
#pragma once

#include <stdbool.h>
#include "esp_http_server.h"
#include "creds.h"
#include "session.h"

// Holds the longest config password and its NUL.
#define AUTH_PASS_BUF (CREDS_PASS_MAX + 1)

// The client's IPv4 address, 0 when unknown: the key of the wrong-password wait.
uint32_t auth_client(httpd_req_t *req);

// True when the request carries a valid session or the config password and comes
// from no other site's page. Otherwise the request has been answered: 403 for
// another site's page or while no password is set, 401 without a session or for a
// wrong password, 429 within a second of a wrong one.
bool auth_request(httpd_req_t *req);
// The same check without answering the request: for open endpoints that show more
// to a signed-in caller.
bool auth_signed_in(httpd_req_t *req);
// True when the request's Bearer password can be told only by PBKDF2, with it in
// pass: the wrappers of util.c hand such a request to pass_queue before the handler
// runs, so the two checks above never hash on the server task.
bool auth_bearer_needs_hash(httpd_req_t *req, char pass[AUTH_PASS_BUF]);
// Answers the request for a failed password check; true for CREDS_OK, with nothing
// sent.
bool auth_answer(httpd_req_t *req, creds_check_t result);

// True when the request has no Origin header (not a browser, or a same-origin GET)
// or names this device's own page as the origin. False means it was answered with 403.
bool auth_origin_ok(httpd_req_t *req);

// The session token from the request's cookie into out ("" for none).
void auth_session_token(httpd_req_t *req, char out[SESSION_TOKEN_BUF]);
// Sets the session cookie on the answer, or clears it for a NULL token. The header
// text lives in a static buffer until the answer is sent: one HTTP task.
void auth_set_cookie(httpd_req_t *req, const char *token);

// The diagnostics and test endpoints (/touch, /oled, /gpio, /i2c).
// They need a session or the password, unless DEV_OPEN_DIAGNOSTICS is 1 in secrets.h
// (development only: it lets anyone on the network drive the panel, the LED and the
// pins).
bool auth_diag_allowed(httpd_req_t *req);
bool auth_diag_request(httpd_req_t *req);

// The names this server answers to: the mDNS host (without ".local") and the setup
// AP's address. The station's own IP is read at request time.
void auth_set_hosts(const char *mdns_host, const char *ap_ip);
// True when the request's Host header names this device (with or without a port).
// A page on the internet whose DNS record is flipped to this device's address
// (DNS rebinding) arrives with its own name in Host, and is refused; otherwise it
// would be same-origin with the device in the browser and could call every
// endpoint. False means the request was already answered: 421, or the redirect below.
bool auth_host_ok(httpd_req_t *req);
// A setup AP client asking for another host (a phone's captive-portal check, which
// the AP's DNS sends here) gets a redirect to the setup page, and true. A redirect,
// never the page itself under that name, so the rebinding guard above still holds.
bool auth_captive_redirect(httpd_req_t *req);
