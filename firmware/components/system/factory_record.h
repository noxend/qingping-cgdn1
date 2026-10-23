// The record Qingping's factory writes at flash 0x3D2000: the unit's MAC, its serial
// number and lcd_id, the byte the stock firmware picks the panel's init by. It lies
// inside our nvs_cfg partition, which reuses that flash, so the first boot keeps a
// copy on the default NVS partition before nvs_cfg is mounted, and a factory reset
// (which erases nvs_cfg) does not lose it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Where the record sits in nvs_cfg (0x3D0000 + 0x2000), and the bytes the stock
// firmware reads of it, 0x3D2000..0x3D205D.
#define FACTORY_RECORD_OFFSET 0x2000
#define FACTORY_RECORD_BYTES 0x5E

#define FACTORY_SERIAL_MAX 16

typedef struct {
  uint8_t mac[6];
  char serial[FACTORY_SERIAL_MAX + 1];
  uint8_t lcd_id;
} factory_record_t;

// Fills `out` from the raw record when it has the factory's shape: 0x5A first, a
// MAC that is not blank, and a serial of letters and digits. False for anything
// else, such as erased flash or the NVS pages that replace the record. Pure C, so
// the host test compiles it.
bool factory_record_parse(const uint8_t raw[FACTORY_RECORD_BYTES], factory_record_t *out);

// Reads the record from flash and keeps a copy when it is still there, else takes
// the copy kept on an earlier boot. Call after the default NVS partition is mounted
// and before nvs_cfg is.
void factory_record_load(void);

// The record factory_record_load found; false when there was none in flash and no
// copy, as on a unit that ran an earlier version of this firmware.
bool factory_record_get(factory_record_t *out);
