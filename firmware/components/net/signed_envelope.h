// The envelope both release channels are published in (the web manifest and the
// firmware manifest): {"payload": base64(P), "sig": base64(DER ECDSA P-256 over P)},
// signed by tools/publish_*.py with the key whose public half is built into the
// firmware (web_manifest_pub.pem). Each channel then checks P's own shape, and the
// shapes differ, so a payload signed for one channel is refused by the other.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

#define ENVELOPE_PAYLOAD_MAX 1536

// P as a NUL-terminated string the caller frees, or NULL when the body is not an
// envelope or its signature does not verify (logged under the caller's tag).
char *signed_envelope_open(const char *body, const char *tag);

// A path under a channel's origin: letters, digits and . _ - /, no "..", no leading
// slash. Both channels put what they accept into a URL or into HTML.
bool envelope_path_ok(const char *s);

// The payload checks both channels share: a fixed field holds its terminator, and a
// JSON string is copied only when it fits whole.
bool envelope_terminated(const char *s, size_t cap);
bool envelope_copy_str(char *dst, size_t cap, const cJSON *v);
