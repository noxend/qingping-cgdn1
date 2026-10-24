// Checks the factory record parser on the flash of a real unit: what the stock
// dump holds at 0x3D2000 (read with xxd: MAC cc:b5:d1:31:c0:8d, serial
// S00115100DB006CA, lcd_id 1 at 0x3D205D), the same record with another lcd_id,
// and the same place once this firmware's NVS has written its pages there.
#include <stdio.h>
#include <string.h>
#include "factory_record.h"

#define STOCK_RECORD_AT 0x3D2000L

static int failures;

static void check(const char *name, bool ok) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) {
    failures++;
  }
}

static bool read_at(const char *path, long offset, uint8_t *out) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return false;
  }
  bool ok = fseek(f, offset, SEEK_SET) == 0;
  if (ok) {
    ok = fread(out, 1, FACTORY_RECORD_BYTES, f) == FACTORY_RECORD_BYTES;
  }
  fclose(f);
  if (!ok) {
    fprintf(stderr, "%s: no %d bytes at 0x%lx\n", path, FACTORY_RECORD_BYTES, offset);
  }
  return ok;
}

int main(int argc, char **argv) {
  uint8_t stock[FACTORY_RECORD_BYTES];
  uint8_t ours[FACTORY_RECORD_BYTES];
  if (argc != 3) {
    return 2;
  }
  bool read = read_at(argv[1], STOCK_RECORD_AT, stock);
  read = read && read_at(argv[2], FACTORY_RECORD_OFFSET, ours);
  if (!read) {
    return 2;
  }

  factory_record_t r;
  memset(&r, 0, sizeof(r));
  check("the stock record parses", factory_record_parse(stock, &r));
  const uint8_t mac[6] = {0xcc, 0xb5, 0xd1, 0x31, 0xc0, 0x8d};
  check("its MAC is the unit's", memcmp(r.mac, mac, 6) == 0);
  check("its serial is S00115100DB006CA", strcmp(r.serial, "S00115100DB006CA") == 0);
  check("its lcd_id is 1", r.lcd_id == 1);

  uint8_t other[FACTORY_RECORD_BYTES];
  memcpy(other, stock, sizeof(other));
  other[0x5D] = 0x00;
  check("lcd_id 0 parses as 0", factory_record_parse(other, &r) && r.lcd_id == 0);

  check("our NVS pages in its place do not parse", !factory_record_parse(ours, &r));

  memcpy(other, stock, sizeof(other));
  other[0] = 0xFF;
  check("without the 0x5A it does not parse", !factory_record_parse(other, &r));

  memcpy(other, stock, sizeof(other));
  memset(other + 1, 0xFF, 6);
  check("with an erased MAC it does not parse", !factory_record_parse(other, &r));

  memcpy(other, stock, sizeof(other));
  other[0x4A] = 0xFF;
  check("with an erased serial it does not parse", !factory_record_parse(other, &r));

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("ok\n");
  return 0;
}
