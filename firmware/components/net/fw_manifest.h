// The firmware release channel: https://cgdn1.swithy.com/firmware.json, published by
// tools/publish_fw.py in the signed envelope (signed_envelope.h) around
//   {"kind":"firmware","seq":<unix time>,"version":"0.27.6",
//    "url":"https://cgdn1.swithy.com/fw/0.27.6/cgdn1.bin","sha256":"<64 hex>",
//    "size":<bytes>,"notes":"..."}
// The device fetches it hourly, keeps the newest valid one, and offers it as an
// update when its version is above the running one. The manifest only says which
// image is new: the image itself still has to pass its own signature check in
// esp_ota_end, and fw_update.c also holds it to this size, hash and version.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "plugin_sdk.h"
#include "web_manifest.h"

// Bump on any change to fw_manifest_t; a stored blob of another layout is dropped.
#define FW_LAYOUT 1
// No manifest below this sequence number is accepted, whatever the device stored:
// raise it with a release to that release's seq, so an erased store cannot reopen
// the door to older manifests.
#define FW_MIN_SEQ 1792437000u
// Notes are shown as text in the web page and written into /status as they are, so
// they are bounded and may not hold quotes, backslashes or control characters.
#define FW_NOTES_MAX 240

typedef struct {
  uint32_t layout;
  uint32_t seq;
  uint32_t size;
  uint8_t sha256[32];
  char version[24];
  char url[128];
  char notes[FW_NOTES_MAX + 1];
} fw_manifest_t;

// Loads the last accepted manifest from settings. running is this firmware's
// version (FW_VERSION). Call after settings_init.
void fw_manifest_init(const char *running);

// UI loop, once a second: asks for the manifest once the station is online, then
// hourly (every five minutes after a failure).
void fw_manifest_tick(bool online);

// UI loop: the answer to that request; r->parsed is a checked fw_manifest_t or NULL.
void fw_manifest_net_done(const core_net_result_t *r);
// Fetches the manifest at the next tick instead of the next hour (the web page's
// "Check for updates"); fw_manifest_checking stays true until the answer is in.
// False while the station is offline, when there is nothing to fetch it over.
bool fw_manifest_check_now(void);
bool fw_manifest_checking(void);
// The latest fetch, scheduled or asked for, got no usable manifest.
bool fw_manifest_check_failed(void);

// Any task: copies the offered update into out. False when the newest manifest is
// not above the running version, or there is none.
bool fw_manifest_offer(fw_manifest_t *out);

// The same, only what /status shows of it, for a caller short of stack.
typedef struct {
  char version[24];
  char notes[FW_NOTES_MAX + 1];
} fw_offer_text_t;
bool fw_manifest_offer_text(fw_offer_text_t *out);

// Compares dotted version numbers field by field ("0.27.10" > "0.27.9"): below
// zero, zero or above zero, like strcmp.
int fw_version_cmp(const char *a, const char *b);
