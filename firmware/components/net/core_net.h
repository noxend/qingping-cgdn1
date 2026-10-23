// Network worker behind core_net_get: one task, one request at a time, so one
// TLS session is in memory at once. Internal to the core.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "plugin_sdk.h"

// Turns a body into a small result on the worker, so the UI loop never holds the body:
// for the core's own requests (the web and firmware manifests). Returns a malloc'd
// result, or NULL when the body is no good.
typedef void *(*core_net_parse_fn)(const char *body, int len);

// The worker hands each finished request to this callback, which owns r->body
// and r->parsed from then on and must not return before the result is delivered:
// every request accepted by core_net_submit ends in exactly one call.
typedef void (*core_net_done_fn)(int owner, core_net_result_t *r);

// Owner ids of the requests the core makes for itself; plugins are 0..n-1.
#define NET_OWNER_CORE (-2)  // the web manifest
#define NET_OWNER_FW (-3)    // the firmware manifest

// The largest body any request may ask for, whatever the plugin passes: a body plus
// the TLS session must leave room for Wi-Fi buffers on a heap of about 96 KB.
#define CORE_NET_BODY_CAP (32 * 1024)

void core_net_start(core_net_done_fn done);
// max_bytes is clamped to CORE_NET_BODY_CAP. With a parser the body is handed to it
// on the worker and freed there; the result reaches the callback as r->parsed.
bool core_net_submit(int owner, const char *url, size_t max_bytes, core_net_parse_fn parse);

// A job runs on the worker in the queue's order instead of a fetch, so its own TLS
// session (a firmware download) never sits in memory beside a request's. It feeds
// the task watchdog itself; no done callback follows it.
typedef void (*core_net_job_fn)(void);
bool core_net_run(core_net_job_fn job);
