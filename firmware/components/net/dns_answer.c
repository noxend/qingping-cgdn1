#include "dns_answer.h"

#include <stdbool.h>
#include <string.h>

#define HEADER_LEN 12
#define TYPE_A 1
#define CLASS_IN 1
#define TTL_S 60
#define ANSWER_LEN 16  // name pointer, type, class, ttl, length, address

static uint16_t get16(const uint8_t *p) {
  return (uint16_t) ((p[0] << 8) | p[1]);
}

static uint8_t *put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t) (v >> 8);
  p[1] = (uint8_t) v;
  return p + 2;
}

// The end of the question's name (past its zero label), or 0 when it runs past the
// packet or uses a pointer, which a query's only name never needs.
static int name_end(const uint8_t *q, int len) {
  int at = HEADER_LEN;
  while (at < len) {
    uint8_t label = q[at];
    if (label == 0) {
      return at + 1;
    }
    if (label > 63) {
      return 0;
    }
    at += 1 + label;
  }
  return 0;
}

int dns_answer(const uint8_t *query, int len, uint32_t ip, uint8_t *out, int cap) {
  if (len < HEADER_LEN) {
    return 0;
  }
  bool is_query = (query[2] & 0x80) == 0;
  bool standard = (query[2] & 0x78) == 0;
  if (!is_query || !standard || get16(query + 4) != 1) {
    return 0;
  }
  int end = name_end(query, len);
  if (end == 0 || end + 4 > len) {
    return 0;
  }
  int question_end = end + 4;
  if (question_end + ANSWER_LEN > cap) {
    return 0;
  }
  bool a_record = get16(query + end) == TYPE_A && get16(query + end + 2) == CLASS_IN;

  // The header and the question as asked; anything after it (an EDNS record) is dropped.
  memcpy(out, query, question_end);
  out[2] = (uint8_t) (0x80 | 0x04 | (query[2] & 0x01));  // response, authoritative, RD as asked
  out[3] = 0x80;                                         // recursion available, no error
  put16(out + 6, a_record ? 1 : 0);
  put16(out + 8, 0);
  put16(out + 10, 0);
  if (!a_record) {
    return question_end;
  }
  uint8_t *p = out + question_end;
  p = put16(p, 0xC000 | HEADER_LEN);  // the name is the question's
  p = put16(p, TYPE_A);
  p = put16(p, CLASS_IN);
  p = put16(p, 0);
  p = put16(p, TTL_S);
  p = put16(p, 4);
  memcpy(p, &ip, 4);
  return question_end + ANSWER_LEN;
}
