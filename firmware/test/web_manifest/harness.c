#include "web_manifest.c"  // copied next to the stubs by run.sh
#include <assert.h>

__asm__(".section __TEXT,__const\n"
        ".globl _binary_web_manifest_pub_pem_start\n_binary_web_manifest_pub_pem_start:\n"
        ".incbin \"pub.pem\"\n.byte 0\n"
        ".globl _binary_web_manifest_pub_pem_end\n_binary_web_manifest_pub_pem_end:\n");

static int stores;
esp_err_t settings_blob_get(const char *k, void *o, size_t *l) {
  return ESP_FAIL;
}
esp_err_t settings_blob_set(const char *k, const void *d, size_t l) {
  stores++;
  return ESP_OK;
}

static char *slurp(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) {
    return NULL;
  }
  static char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = 0;
  fclose(f);
  char *c = malloc(n + 1);
  memcpy(c, buf, n + 1);
  return c;
}

static int check(const char *file, bool want_ok) {
  char *b = slurp(file);
  web_manifest_t *m = b ? parse(b, (int) strlen(b)) : NULL;
  bool ok = m != NULL;
  printf("%-28s %s (want %s)%s%s\n", file, ok ? "accepted" : "refused",
         want_ok ? "accept" : "refuse", ok ? " version " : "", ok ? m->version : "");
  int bad = ok != want_ok;
  if (ok) {
    core_net_result_t r = {.status = 200, .parsed = m};
    web_manifest_net_done(&r);
  }
  free(b);
  return bad;
}

int main(int argc, char **argv) {
  int bad = 0;
  bad += check("real.json", true);
  bad += check("tampered.json", false);
  bad += check("otherkey.json", false);
  bad += check("badshape.json", false);
  bad += check("quote.json", false);
  int before = stores;
  bad += check("older.json", true); /* signature fine, but seq lower: parsed, not kept */
  web_manifest_t cur;
  web_manifest_get(&cur);
  printf("after the older one: current %s, stores %d -> %d\n", cur.version, before, stores);
  bad += strcmp(cur.version, "older") == 0 || stores != before;
  printf(bad ? "FAIL %d\n" : "ALL PASS\n", bad);
  return bad != 0;
}
