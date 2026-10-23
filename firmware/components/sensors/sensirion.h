// Helpers shared by the Sensirion sensors on I2C port 0 (SHT4x, SCD4x).
#pragma once

#include <stdint.h>

// CRC8 over one data word pair, as every Sensirion I2C sensor uses it:
// polynomial 0x31, init 0xFF, no reflection, final XOR 0.
static inline uint8_t sensirion_crc8(const uint8_t *d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) {
      c = (c & 0x80) ? (uint8_t) ((c << 1) ^ 0x31) : (uint8_t) (c << 1);
    }
  }
  return c;
}
