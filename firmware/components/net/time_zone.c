#include "time_zone.h"

#include <string.h>

typedef struct {
  const char *key;
  const char *posix;
} time_zone_t;

// The keys match web/src/lib/timeZones.js, which names each zone.
static const time_zone_t ZONES[] = {
    {"kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"london", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"istanbul", "TRT-3"},
    {"utc", "UTC0"},
    {"dubai", "GST-4"},
    {"tokyo", "JST-9"},
    {"sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"new_york", "EST5EDT,M3.2.0,M11.1.0"},
    {"chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"los_angeles", "PST8PDT,M3.2.0,M11.1.0"},
};

const char *time_zone_posix(const char *key) {
  for (size_t i = 0; i < sizeof(ZONES) / sizeof(ZONES[0]); i++) {
    if (strcmp(ZONES[i].key, key) == 0) {
      return ZONES[i].posix;
    }
  }
  return NULL;
}

bool time_zone_record_ok(const char *record, size_t len) {
  if (len == 0 || len > TIME_ZONE_KEY_MAX || record[len - 1] != '\0') {
    return false;
  }
  return time_zone_posix(record) != NULL;
}
