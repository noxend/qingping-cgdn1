// The setup AP's DNS answer: every A question gets the AP's own address. A phone
// that joins the hotspot then finds its captive-portal check (captive.apple.com,
// connectivitycheck.gstatic.com) answered by the device, and opens the setup page
// by itself.
#pragma once

#include <stdint.h>

// The reply to one DNS query of `len` bytes, written to `out`: its length, or 0 when
// the packet gets no reply (not a standard query, more than one question, or
// malformed). An A/IN question gets `ip` (network order); any other type gets an
// empty answer, so the client does not wait for one.
int dns_answer(const uint8_t *query, int len, uint32_t ip, uint8_t *out, int cap);
