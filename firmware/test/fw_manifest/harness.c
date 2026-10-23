#include "fw_manifest.c"  // copied next to the stubs by run.sh

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

// Parses the file as the network worker would and, when accepted, hands it to the
// UI loop's handler.
static int check(const char *file, bool want_ok) {
  char *b = slurp(file);
  fw_manifest_t *m = b ? parse(b, (int) strlen(b)) : NULL;
  bool ok = m != NULL;
  printf("%-16s %s (want %s)%s%s\n", file, ok ? "accepted" : "refused",
         want_ok ? "accept" : "refuse", ok ? " version " : "", ok ? m->version : "");
  int bad = ok != want_ok;
  if (ok) {
    core_net_result_t r = {.status = 200, .parsed = m};
    fw_manifest_net_done(&r);
  }
  free(m);
  free(b);
  return bad;
}

static int offered(const char *want) {
  fw_manifest_t m;
  bool on = fw_manifest_offer(&m);
  printf("offered: %s (want %s)\n", on ? m.version : "none", want ? want : "none");
  return want ? !on || strcmp(m.version, want) != 0 : on;
}

static int cmp(const char *a, const char *b, int want) {
  int got = fw_version_cmp(a, b);
  got = got < 0 ? -1 : got > 0;
  printf("cmp %-8s %-8s %2d (want %2d)\n", a, b, got, want);
  return got != want;
}

int main(void) {
  int bad = 0;
  bad += cmp("0.27.10", "0.27.9", 1);
  bad += cmp("1", "0.99.99", 1);
  bad += cmp("0.27", "0.27.0", 0);
  bad += cmp("0.27.5", "0.27.6", -1);
  // A pre-release suffix after equal numbers is the lower version (semver), so the
  // release is offered to a device running its release candidate.
  bad += cmp("1.0.0", "1.0.0-rc1", 1);
  bad += cmp("1.0.0-rc1", "1.0.0", -1);
  bad += cmp("1.0.0-rc2", "1.0.0-rc1", 1);
  bad += cmp("1.0.1", "1.0.0-rc1", 1);
  bad += cmp("0.31.2", "1.0.0-rc1", -1);

  fw_manifest_init("0.27.5");
  bad += offered(NULL);
  bad += check("webchannel.json", false);
  bad += check("tampered.json", false);
  bad += check("otherkey.json", false);
  bad += check("offpath.json", false);
  bad += check("quote.json", false);
  bad += check("real.json", true);
  bad += offered("0.27.6");
  int before = stores;
  bad += check("older.json", true); /* signature fine, but seq lower: parsed, not kept */
  bad += stores != before;
  bad += offered("0.27.6");
  bad += check("same.json", true); /* newer seq, the running version: kept, nothing offered */
  bad += offered(NULL);
  printf(bad ? "FAIL %d\n" : "ALL PASS\n", bad);
  return bad != 0;
}
