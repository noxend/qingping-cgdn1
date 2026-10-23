// The hosted web interface (https://cgdn1.swithy.com) and the signed manifest that
// names its current build. The device fetches the manifest, checks its ECDSA
// signature with the public key built into the firmware (web_manifest_pub.pem),
// keeps the newest valid one, and hands the browser the file names and their
// sha384 hashes; the browser then refuses any file that does not match. So the
// interface updates without a firmware update, and a compromised server or tunnel
// cannot put other code on the page: it has no signing key.
//
// A manifest is accepted only when its sequence number is above the stored one
// (no replay of an older build), its base is under the pinned origin, and every
// name and hash is in the expected shape.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "plugin_sdk.h"

#define WEB_ORIGIN "https://cgdn1.swithy.com"
#define WEB_MANIFEST_URL WEB_ORIGIN "/manifest.json"
#define WEB_MAX_CSS 2
// No manifest below this sequence number is accepted, whatever the device stored:
// raise it with a release to the seq of the build that release ships with, so an
// erased store or an older layout cannot reopen the door to older signed builds.
// 1.0.0-rc7 signs in with a session; a build published before it has no sign-in page.
#define WEB_MIN_SEQ 1792663279u  // 2026-10-22, after web-2ec9bb7 (seq 1792558455)
// Bump on any change to web_manifest_t; a stored blob of another layout is dropped.
#define WEB_LAYOUT 1

typedef struct {
  uint32_t layout;
  uint32_t seq;
  char version[48];
  char base[96];
  char js[64];
  char js_sri[72];
  int css_n;
  char css[WEB_MAX_CSS][64];
  char css_sri[WEB_MAX_CSS][72];
} web_manifest_t;

// Loads the last accepted manifest from settings. Call after settings_init.
void web_manifest_init(void);

// UI loop, once a second: asks for the manifest once the station is online, then
// hourly (every five minutes after a failure).
void web_manifest_tick(bool online);

// UI loop: the answer to that request; r->parsed is a checked web_manifest_t or NULL.
void web_manifest_net_done(const core_net_result_t *r);

// Any task: copies the current manifest into out. False when there is none, and
// the device then serves the interface built into the firmware.
bool web_manifest_get(web_manifest_t *out);
