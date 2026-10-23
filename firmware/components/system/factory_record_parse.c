#include "factory_record.h"

#include <string.h>

// Offsets in the record, from the stock dump of the maintainer's unit.
#define MAGIC 0x5A
#define AT_MAC 0x01
#define AT_SERIAL_LEN 0x4A
#define AT_SERIAL 0x4B
#define AT_LCD_ID 0x5D

static bool blank(const uint8_t *bytes, size_t len, uint8_t value) {
  for (size_t i = 0; i < len; i++) {
    if (bytes[i] != value) {
      return false;
    }
  }
  return true;
}

static bool alnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool factory_record_parse(const uint8_t raw[FACTORY_RECORD_BYTES], factory_record_t *out) {
  if (raw[0] != MAGIC) {
    return false;
  }
  const uint8_t *mac = raw + AT_MAC;
  if (blank(mac, 6, 0x00) || blank(mac, 6, 0xFF)) {
    return false;
  }
  int len = raw[AT_SERIAL_LEN];
  if (len < 1 || len > FACTORY_SERIAL_MAX) {
    return false;
  }
  for (int i = 0; i < len; i++) {
    if (!alnum((char) raw[AT_SERIAL + i])) {
      return false;
    }
  }
  memcpy(out->mac, mac, 6);
  memcpy(out->serial, raw + AT_SERIAL, len);
  out->serial[len] = '\0';
  out->lcd_id = raw[AT_LCD_ID];
  return true;
}
