// Checks of the plugin store and the manifest check against modules from the real build
// (run.sh). Each line names what a user of the store relies on; the last line counts
// failures.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "manifest.h"
#include "settings.h"
#include "wasm_binary.h"
#include "wasm_store.h"

void fake_nvs_reset(int pages);
bool fake_nvs_settings_fit(size_t len);
void fake_nvs_fail_next(const char *ns);
int old_install(const uint8_t *code, size_t len);

static int s_failures;

static void *no_memory(size_t size) {
  (void) size;
  return NULL;
}

static void check(bool ok, const char *what) {
  printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    s_failures++;
  }
}

static uint8_t *slurp(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("cannot read %s\n", path);
    exit(2);
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = malloc(n + 1);
  fread(buf, 1, n, f);
  buf[n] = '\0';
  fclose(f);
  if (len) {
    *len = (size_t) n;
  }
  return buf;
}

static void release(void *p) {
  free(p);
}

// The file's manifest as an upload reads it: NULL, or why it is refused ("section" when
// there is none).
static const char *parse_file(const char *path, manifest_t *m) {
  size_t len = 0;
  uint8_t *code = slurp(path, &len);
  uint32_t n = 0;
  const char *text = (const char *) wasm_custom_section(code, (uint32_t) len, MANIFEST_SECTION, &n);
  const char *why = text ? manifest_parse(text, (int) n, m) : "section";
  free(code);
  return why;
}

static bool refused(const char *path, const char *code) {
  manifest_t m;
  const char *why = parse_file(path, &m);
  bool no = why && strcmp(why, code) == 0;
  if (!no) {
    printf("  %s: %s\n", path, why ? why : "(taken)");
  }
  return no;
}

static bool is_listed(const char *id) {
  char (*ids)[STORE_ID_MAX] = NULL;
  int n = store_ids(&ids);
  bool found = false;
  for (int k = 0; k < n; k++) {
    found = found || strcmp(ids[k], id) == 0;
  }
  free(ids);
  return found;
}

static int installed_count(void) {
  char (*ids)[STORE_ID_MAX] = NULL;
  int n = store_ids(&ids);
  free(ids);
  return n;
}

// Keeps a module under the id its manifest gives, as the device's install does.
static store_err_t install(const char *path) {
  size_t len = 0;
  uint8_t *code = slurp(path, &len);
  manifest_t m;
  uint32_t n = 0;
  const char *text = (const char *) wasm_custom_section(code, (uint32_t) len, MANIFEST_SECTION, &n);
  store_err_t e = STORE_ERR_FLASH;
  if (text && !manifest_parse(text, (int) n, &m)) {
    e = store_save(m.id, code, (uint32_t) len);
  }
  free(code);
  return e;
}

static void check_manifest(void) {
  manifest_t m;
  check(parse_file("clock.wasm", &m) == NULL && strcmp(m.id, "clock") == 0 &&
            strcmp(m.name, "Clock") == 0 && strcmp(m.category, "time") == 0 &&
            m.sdk == MANIFEST_SDK && strcmp(m.version, "1.0.0") == 0 && m.host_count == 0,
        "a module built by build.sh reads with its id, English name, category, SDK");
  size_t len = 0;
  uint8_t *code = slurp("clock.wasm", &len);
  uint32_t n = 0;
  const char *text = (const char *) wasm_custom_section(code, (uint32_t) len, MANIFEST_SECTION, &n);
  check(manifest_value_ok(text, (int) n, "time_format", "12h") &&
            !manifest_value_ok(text, (int) n, "time_format", "12 годин (AM/PM)") &&
            manifest_value_ok(text, (int) n, "seconds", "1") &&
            !manifest_value_ok(text, (int) n, "seconds", "2") &&
            !manifest_value_ok(text, (int) n, "nope", "1"),
        "a select takes its options' keys, a toggle 0 or 1, an unknown key nothing");
  char key[32];
  bool uk =
      manifest_key_for_label(text, (int) n, "time_format", "12 годин (AM/PM)", key, sizeof(key)) &&
      strcmp(key, "12h") == 0;
  bool en = manifest_key_for_label(text, (int) n, "date_format", "YYYY-MM-DD", key, sizeof(key)) &&
            strcmp(key, "ymd") == 0;
  bool already = !manifest_key_for_label(text, (int) n, "time_format", "24h", key, sizeof(key));
  bool unknown =
      !manifest_key_for_label(text, (int) n, "time_format", "25 годин", key, sizeof(key));
  check(uk && en && already && unknown,
        "a stored label in either language names its option's key; a key needs nothing");
  free(code);
  check(parse_file("bank.wasm", &m) == NULL && m.host_count == 1 &&
            strcmp(m.hosts[0], "bank.gov.ua") == 0,
        "its hosts come from its plugin.json");
  check(parse_file("raw_ok.wasm", &m) == NULL, "(the same plugin.json written by hand reads too)");
  check(parse_file("textdef.wasm", &m) == NULL, "a text setting may default to empty");

  check(refused("bare.wasm", "section"), "a module without plugin.json is refused: section");
  check(refused("variant.json", "section"), "a file that is not WebAssembly: section");
  check(refused("notjson.wasm", "json"), "a plugin.json that is not JSON: json");
  check(refused("deep.wasm", "json"), "a plugin.json nested 40 deep: json");
  check(refused("deep31.wasm", "json"), "a plugin.json nested 31 deep, past the format's 8: json");
  check(refused("huge.wasm", "json"), "a plugin.json over 8 KB: json");
  check(refused("twice.wasm", "json"), "a plugin.json with two host lists: json");
  check(refused("case.wasm", "fields"), "a plugin.json with HOSTS beside hosts: fields");
  check(refused("author.wasm", "fields"), "a field the format does not have: fields");
  check(refused("v1.wasm", "old_sdk"), "the plugin.json of 1.0.0-rc7 (format 1): old_sdk");
  check(refused("sdk3.wasm", "sdk"), "a module for a newer SDK: sdk");
  check(refused("badid.wasm", "id"), "an id with capitals: id");
  check(refused("sysid.wasm", "id"), "the id \"sys\": id");
  check(refused("longid.wasm", "id"), "an id longer than an NVS key: id");
  check(refused("noname.wasm", "name"), "no name: name");
  check(refused("plainname.wasm", "name"), "a name as plain text, not per language: name");
  check(refused("noen.wasm", "name"), "a name without English: name");
  check(refused("badlang.wasm", "name"), "a language code of three letters: name");
  check(refused("longname.wasm", "name"), "a Ukrainian name over 47 bytes: name");
  check(refused("badcat.wasm", "category"), "a category not in the list: category");
  check(refused("fivehosts.wasm", "hosts"), "five hosts: hosts");
  check(refused("badhost.wasm", "hosts"), "a host that is not a host name: hosts");
  check(refused("stringopts.wasm", "params"), "options as plain strings (format 1): params");
  check(refused("baddef.wasm", "params"), "a default that is a label, not a key: params");
  check(refused("twinopts.wasm", "params"), "two options with one key: params");
  check(refused("twinparams.wasm", "params"), "two settings with one key: params");
  check(refused("manyparams.wasm", "params"), "17 settings: params");
  check(refused("badnumber.wasm", "params"), "a number setting defaulting to 1e3: params");
}

static void check_install(void) {
  fake_nvs_reset(48);
  store_init();
  size_t len = 0;
  uint8_t *code = slurp("clock.wasm", &len);
  uint8_t *flipped = slurp("clock_flipped.wasm", NULL);

  settings_ns_blob_set("cfg", "clock", "{\"seconds\":\"1\"}", 16);
  check(install("clock.wasm") == STORE_OK && is_listed("clock"), "install keeps it and lists it");
  uint8_t *read = NULL;
  uint32_t got = 0;
  store_err_t e = store_read("clock", &read, &got, malloc, release);
  check(e == STORE_OK && got == len && memcmp(read, code, len) == 0,
        "the kept module reads back byte for byte");
  free(read);
  uint32_t size = 0;
  check(store_size("clock", &size) == STORE_OK && size == len, "and its size is known");
  size_t len101 = 0;
  uint8_t *code101 = slurp("v101.wasm", &len101);
  check(install("v101.wasm") == STORE_OK && installed_count() == 1 &&
            store_read("clock", &read, &got, malloc, release) == STORE_OK && got == len101 &&
            memcmp(read, code101, len101) == 0,
        "another version of the same id replaces it in place");
  free(read);
  check(install("clock.wasm") == STORE_OK &&
            store_read("clock", &read, &got, malloc, release) == STORE_OK &&
            memcmp(read, code, len) == 0,
        "and the earlier one replaces that: uploads have no order");
  free(read);

  // An update whose record write fails keeps the installed version whole.
  fake_nvs_fail_next("plug_meta");
  e = store_save("clock", code101, (uint32_t) len101);
  check(e != STORE_OK && store_read("clock", &read, &got, malloc, release) == STORE_OK &&
            memcmp(read, code, len) == 0,
        "a failed update leaves the installed version as it was");
  free(read);
  free(code101);

  // Power lost after an update wrote its module and before its record: the other bank
  // holds a module no record names. The installed version still reads, and the boot
  // sweep erases the stray module.
  size_t n = 0;
  bool in_a = settings_ns_blob_get("plug_code_a", "clock", NULL, &n) == ESP_OK;
  const char *other = in_a ? "plug_code_b" : "plug_code_a";
  settings_ns_blob_set(other, "clock", flipped, len);
  check(store_read("clock", &read, &got, malloc, release) == STORE_OK &&
            memcmp(read, code, len) == 0,
        "a module cut off from its record does not replace the installed one");
  free(read);
  store_init();
  check(settings_ns_blob_get(other, "clock", NULL, &n) != ESP_OK,
        "the boot sweep erases a module no record names");

  // Corrupt the kept module: the read refuses it.
  const char *bank = in_a ? "plug_code_a" : "plug_code_b";
  settings_ns_blob_set(bank, "clock", flipped, len);
  check(store_read("clock", &read, &got, malloc, release) == STORE_ERR_HASH && !read,
        "a module damaged on flash is refused when it is read");
  settings_ns_blob_set(bank, "clock", code, len);
  check(store_read("clock", &read, &got, no_memory, release) == STORE_ERR_MEMORY && !read,
        "no memory to read it into is not a flash fault");

  check(store_remove("clock", true) == STORE_OK && !is_listed("clock"), "remove forgets it");
  check(settings_ns_blob_get("plug_code_a", "clock", NULL, &n) != ESP_OK &&
            settings_ns_blob_get("plug_code_b", "clock", NULL, &n) != ESP_OK &&
            settings_ns_blob_get("cfg", "clock", NULL, &n) != ESP_OK,
        "remove erases its module and its settings");
  check(store_read("clock", &read, &got, malloc, release) == STORE_ERR_NOT_FOUND,
        "a removed plugin reads as not installed");
  check(store_remove("clock", true) == STORE_ERR_NOT_FOUND, "removing it again says so");
  check(install("clock.wasm") == STORE_OK, "and it installs again");
  settings_ns_blob_set("cfg", "clock", "{}", 3);
  store_remove("clock", false);
  check(settings_ns_blob_get("cfg", "clock", NULL, &n) == ESP_OK,
        "removing without erasing settings keeps them");
  free(code);
  free(flipped);
}

// The record 1.0.0-rc7 wrote, by its own code, reads with this firmware after the boot
// turns it into this one's: the module is the one that release kept.
static void check_upgrade(void) {
  fake_nvs_reset(48);
  size_t len = 0;
  uint8_t *v1 = slurp("v1.wasm", &len);
  int written = old_install(v1, len);
  size_t meta = 0;
  settings_ns_blob_get("plug_meta", "clock_wasm", NULL, &meta);
  printf("  1.0.0-rc7 wrote a record of %zu B for clock_wasm\n", meta);
  store_init();
  uint8_t *read = NULL;
  uint32_t got = 0;
  store_err_t e = store_read("clock_wasm", &read, &got, malloc, release);
  check(written == 0 && e == STORE_OK && got == len && memcmp(read, v1, len) == 0,
        "a plugin 1.0.0-rc7 installed is still installed, its module whole");
  free(read);
  settings_ns_blob_get("plug_meta", "clock_wasm", NULL, &meta);
  check(meta < 64 && store_remove("clock_wasm", true) == STORE_OK && installed_count() == 0,
        "its record is this firmware's now, and it can be deleted");

  // A record the boot cannot read or convert names nothing for sure: its module stays.
  old_install(v1, len);
  uint32_t broken[2] = {1, 7};  // layout 1, a bank that does not exist
  settings_ns_blob_set("plug_meta", "clock_wasm", broken, sizeof(broken));
  store_init();
  size_t n = 0;
  check(settings_ns_blob_get("plug_code_a", "clock_wasm", NULL, &n) == ESP_OK && n == len,
        "a module whose record does not read is kept by the boot sweep");
  free(v1);
}

// A partition fills up: every install the check lets through is written, and the
// settings keep their reserve.
static void check_capacity(void) {
  fake_nvs_reset(48);
  store_init();
  int kept = 0;
  store_err_t last = STORE_OK;
  size_t len = 0;
  for (int n = 0; n < 64 && last == STORE_OK; n++) {
    char path[32];
    snprintf(path, sizeof(path), "copy%d.wasm", n);
    free(slurp(path, &len));
    last = install(path);
    if (last == STORE_OK) {
      kept++;
    }
  }
  printf("  %d plugins of %zu B fit in the 192 KB partition\n", kept, len);
  check(last == STORE_ERR_SPACE && kept > 8,
        "more than eight fit, then the storage refuses: space");
  check(installed_count() == kept, "every install the room check allowed was written");
  check(fake_nvs_settings_fit(STORE_RESERVE_BYTES - 1024), "the settings still have their reserve");
  store_space_t space;
  store_space(&space);
  size_t free_before = space.free;
  store_remove("clock0", true);
  store_space(&space);
  check(space.free > free_before, "removing one gives its room back at once");
  check(install("clock.wasm") == STORE_OK, "and another plugin fits again");
}

int main(void) {
  fake_nvs_reset(48);
  store_init();
  check_manifest();
  check_install();
  check_upgrade();
  check_capacity();
  printf("%d failed\n", s_failures);
  return s_failures ? 1 : 0;
}
