// Checks the particulate frame: which frames are accepted, which words are read, and
// what the stock firmware would show for them. The frames are built to the layout in
// pm_frame.h (no frame from the device's sensor has been captured yet); the expected
// values are the stock's curve x / (0.874 - 0.000475 x) worked out by hand.
#include <stdio.h>
#include <string.h>
#include "pm_frame.h"

static int failures;

static void check(const char *name, bool ok) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) {
    failures++;
  }
}

static void put_word(uint8_t *frame, int i, uint16_t v) {
  frame[i] = v >> 8;
  frame[i + 1] = v & 0xFF;
}

// A valid frame with PM2.5 and PM10 at `pm25` and `pm10` µg/m³ atmospheric, the CF=1
// words set to other values so a read of the wrong word shows.
static void build(uint8_t *frame, uint16_t pm25, uint16_t pm10, uint8_t version) {
  memset(frame, 0, PM_FRAME_LEN);
  frame[0] = 0x42;
  frame[1] = 0x4D;
  put_word(frame, 2, 28);
  put_word(frame, 4, 1111);
  put_word(frame, 6, 2222);
  put_word(frame, 8, 3333);
  put_word(frame, 10, 4444);
  put_word(frame, 12, pm25);
  put_word(frame, 14, pm10);
  put_word(frame, 16, 5555);
  frame[28] = version;
  uint16_t sum = 0;
  for (int i = 0; i < PM_FRAME_LEN - 2; i++) {
    sum += frame[i];
  }
  put_word(frame, 30, sum);
}

static void refused(const char *name, const uint8_t *frame) {
  pm_frame_t f;
  check(name, !pm_frame_parse(frame, &f));
}

static void shown(uint16_t pm25, uint16_t pm10, uint8_t version, uint16_t want25, uint16_t want10) {
  uint8_t frame[PM_FRAME_LEN];
  build(frame, pm25, pm10, version);
  pm_frame_t f;
  uint16_t got25 = 0;
  uint16_t got10 = 0;
  bool ok = pm_frame_parse(frame, &f);
  if (ok) {
    pm_frame_shown(&f, &got25, &got10);
  }
  char name[96];
  snprintf(name, sizeof(name), "%u / %u, version 0x%02x, shows %u / %u (got %u / %u)", pm25, pm10,
           version, want25, want10, got25, got10);
  check(name, ok && got25 == want25 && got10 == want10);
}

int main(void) {
  uint8_t frame[PM_FRAME_LEN];

  build(frame, 12, 20, 0x91);
  pm_frame_t f;
  check("a valid frame is accepted", pm_frame_parse(frame, &f));
  check("and its atmospheric words are read", f.pm25 == 12 && f.pm10 == 20);
  check("and its version byte", f.version == 0x91);

  build(frame, 12, 20, 0x91);
  frame[0] = 0x41;
  refused("a wrong first byte is refused", frame);
  build(frame, 12, 20, 0x91);
  frame[1] = 0x4E;
  refused("a wrong second byte is refused", frame);
  build(frame, 12, 20, 0x91);
  frame[3] = 26;
  refused("a wrong length is refused", frame);
  build(frame, 12, 20, 0x91);
  frame[13] ^= 1;
  refused("a changed value with the old checksum is refused", frame);
  build(frame, 12, 20, 0x91);
  frame[31] ^= 1;
  refused("a wrong checksum is refused", frame);
  memset(frame, 0xFF, sizeof(frame));
  refused("a bus that reads all ones is refused", frame);
  memset(frame, 0, sizeof(frame));
  refused("a bus that reads all zeros is refused", frame);

  // Version 0x80: shown as read, capped at 999.
  shown(100, 150, 0x80, 100, 150);
  shown(700, 1200, 0x80, 700, 999);
  // Any other version: the stock's rules.
  shown(12, 20, 0x91, 12, 20);
  shown(61, 80, 0x91, 61, 80);
  shown(62, 62, 0x91, 73, 73);
  shown(100, 300, 0x91, 120, 410);
  shown(592, 700, 0x91, 998, 999);
  shown(593, 700, 0x91, 999, 999);
  shown(0, 20, 0x91, 0, 0);
  shown(12, 0, 0x91, 0, 0);
  shown(100, 2000, 0x91, 120, 999);

  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
