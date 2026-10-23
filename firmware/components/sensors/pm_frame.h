// The particulate sensor's 32-byte frame and the values the stock firmware shows from
// it. Pure C with no ESP-IDF include, so the host test can compile it. The layout and
// the stock's rules were read from the stock app image (journal entry pm25); the
// checksum is the one ESPHome's pmsa003i driver checks.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PM_FRAME_LEN 32

typedef struct {
  uint16_t pm25;    // µg/m³, atmospheric
  uint16_t pm10;    // µg/m³, atmospheric
  uint8_t version;  // byte 28
} pm_frame_t;

// True when the frame starts with "BM", declares 28 bytes and its checksum matches.
bool pm_frame_parse(const uint8_t frame[PM_FRAME_LEN], pm_frame_t *out);

// What the stock firmware shows for a frame: a sensor with version 0x80 as it reads;
// any other gets the stock's correction above 61 µg/m³ and 999 above 592. 0 to 999.
void pm_frame_shown(const pm_frame_t *f, uint16_t *pm25, uint16_t *pm10);
