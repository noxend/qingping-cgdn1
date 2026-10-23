// Checks the time zones against the host's zone database: for each key, the local time
// its POSIX string gives matches the named IANA zone every hour of 2026 and 2027, both
// daylight-saving switches included, and so does every other city the web interface's
// label names. Also which stored records are read back.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "time_zone.h"

static int failures;

static void check(const char *name, bool ok) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) {
    failures++;
  }
}

#define FROM 1767225600  // 2026-01-01 00:00 UTC
#define HOURS (2 * 365 * 24)

// The local day, hour, minute and daylight-saving flag of every hour from FROM, in one
// zone. TZ is set once per zone: the host reloads its zone file on every change.
static void local_hours(const char *tz, int *out) {
  setenv("TZ", tz, 1);
  tzset();
  for (int i = 0; i < HOURS; i++) {
    time_t at = FROM + (time_t) i * 3600;
    struct tm t;
    localtime_r(&at, &t);
    out[i] = ((t.tm_mday * 24 + t.tm_hour) * 60 + t.tm_min) * 2 + (t.tm_isdst > 0);
  }
}

static void matches(const char *key, const char *iana) {
  static int want[HOURS];
  static int got[HOURS];
  const char *posix = time_zone_posix(key);
  char name[96];
  snprintf(name, sizeof(name), "%s follows %s", key, iana);
  if (!posix || strlen(key) >= TIME_ZONE_KEY_MAX) {
    check(name, false);
    return;
  }
  local_hours(iana, want);
  local_hours(posix, got);
  for (int i = 0; i < HOURS; i++) {
    if (got[i] != want[i]) {
      printf("     %d hours after 2026-01-01 00:00 UTC\n", i);
      check(name, false);
      return;
    }
  }
  check(name, true);
}

// A city the zone's label in timeZones.js names besides the zone's own.
static void city(const char *key, const char *iana) {
  matches(key, iana);
}

int main(void) {
  matches("kyiv", "Europe/Kyiv");
  matches("helsinki", "Europe/Helsinki");
  matches("warsaw", "Europe/Warsaw");
  matches("london", "Europe/London");
  matches("istanbul", "Europe/Istanbul");
  matches("utc", "UTC");
  matches("dubai", "Asia/Dubai");
  matches("tokyo", "Asia/Tokyo");
  matches("sydney", "Australia/Sydney");
  matches("new_york", "America/New_York");
  matches("chicago", "America/Chicago");
  matches("denver", "America/Denver");
  matches("los_angeles", "America/Los_Angeles");

  city("helsinki", "Europe/Chisinau");
  city("helsinki", "Europe/Bucharest");
  city("helsinki", "Europe/Athens");
  city("warsaw", "Europe/Berlin");
  city("warsaw", "Europe/Paris");
  city("warsaw", "Europe/Rome");
  city("london", "Europe/Lisbon");
  city("new_york", "America/Toronto");

  check("the default is a zone", time_zone_posix(TIME_ZONE_DEFAULT) != NULL);
  check("an unknown key is refused", time_zone_posix("mars") == NULL);
  check("an empty key is refused", time_zone_posix("") == NULL);
  check("a key differing in case is refused", time_zone_posix("Kyiv") == NULL);

  check("a stored key is read back", time_zone_record_ok("warsaw", sizeof("warsaw")));
  check("a record without its terminator is refused", !time_zone_record_ok("warsaw", 6));
  check("an empty record is refused", !time_zone_record_ok("", 0));
  check("a stored unknown key is refused", !time_zone_record_ok("mars", sizeof("mars")));
  char long_record[TIME_ZONE_KEY_MAX + 1] = "kyiv";
  check("a record longer than any key is refused",
        !time_zone_record_ok(long_record, sizeof(long_record)));

  if (failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
