#include "manifest.h"

#include <stdlib.h>
#include <string.h>
#include "json_scan.h"

// The longest text of each kind, in bytes of UTF-8, per language.
#define NAME_MAX 47
#define DESCRIPTION_MAX 255
#define LABEL_MAX 63
#define TEXT_VALUE_MAX 63  // a text setting's default
#define PARAM_KEY_MAX 15
#define OPTION_KEY_MAX 23
#define PARAMS_MAX 16
#define OPTIONS_MAX 64
// The deepest a manifest nests: the format needs six (the object, params, a setting,
// its options, an option, its label). The check below recurses once per level, on the
// HTTP task's stack.
#define DEPTH_MAX 8

// The categories the web page has words for (web/src/i18n, "category.<key>").
static const char *const CATEGORIES[] = {
    "climate", "time", "weather", "security", "finance",  "transport",
    "news",    "home", "fun",     "tools",    "examples", "other",
};

static const char *const TOP_FIELDS[] = {
    "id", "name", "description", "category", "version", "sdk", "hosts", "params", NULL,
};
static const char *const PARAM_FIELDS[] = {"key", "label", "type", "def", "options", NULL};
static const char *const OPTION_FIELDS[] = {"key", "label", NULL};
static const char *const TYPES[] = {"select", "text", "number", "toggle", NULL};

static bool in_list(const char *s, const char *const *list) {
  for (; *list; list++) {
    if (strcmp(s, *list) == 0) {
      return true;
    }
  }
  return false;
}

// 1 to max of a-z, 0-9 and _.
static bool key_ok(const char *s, int max) {
  int n = (int) strlen(s);
  if (n == 0 || n > max) {
    return false;
  }
  for (const char *p = s; *p; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool manifest_id_ok(const char *id) {
  return key_ok(id, MANIFEST_ID_MAX - 1) && strcmp(id, "sys") != 0;
}

static bool host_ok(const char *h) {
  if (!h[0]) {
    return false;
  }
  for (const char *p = h; *p; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// An optional sign and one to nine digits: fits an int, which is what a plugin reading
// it with atoi expects.
static bool number_ok(const char *v) {
  size_t i = v[0] == '-' ? 1 : 0;
  size_t digits = strlen(v) - i;
  if (digits < 1 || digits > 9) {
    return false;
  }
  for (; v[i]; i++) {
    if (v[i] < '0' || v[i] > '9') {
      return false;
    }
  }
  return true;
}

// A non-empty string of at most max bytes, decoded into out (max + 1 bytes).
static bool string_of(const char *js, int len, int at, char *out, int max) {
  return json_type(js, len, at) == JSON_STRING && json_string(js, len, at, out, max + 1) > 0;
}

// Whether the key of the member at m comes again later in its object; a key too long to
// compare counts as repeated (no field has one). Not inlined: its buffer stays out of
// keys_unique's recursion.
__attribute__((noinline)) static bool key_repeats(const char *js, int len, int m) {
  char key[JSON_COMPARE_MAX + 1];
  if (json_string(js, len, m, key, sizeof(key)) < 0) {
    return true;
  }
  for (int later = json_next(js, len, m); later >= 0; later = json_next(js, len, later)) {
    if (json_string_eq(js, len, later, key)) {
      return true;
    }
  }
  return false;
}

// Every object in the value has each key once, and it nests at most DEPTH_MAX deep. JSON
// leaves repeated keys to the reader, and readers differ (the page's takes the last, this
// file the first): the user could be shown one host list and the device keep another.
static bool keys_unique(const char *js, int len, int at, int depth) {
  json_type_t t = json_type(js, len, at);
  if (t != JSON_OBJECT && t != JSON_ARRAY) {
    return true;
  }
  if (depth == DEPTH_MAX) {
    return false;
  }
  for (int e = json_first(js, len, at); e >= 0; e = json_next(js, len, e)) {
    if (t == JSON_OBJECT && key_repeats(js, len, e)) {
      return false;
    }
    int value = t == JSON_OBJECT ? json_member_value(js, len, e) : e;
    if (!keys_unique(js, len, value, depth + 1)) {
      return false;
    }
  }
  return true;
}

// The object's keys are all in `allowed`.
static bool fields_known(const char *js, int len, int obj, const char *const *allowed) {
  char key[32];
  for (int m = json_first(js, len, obj); m >= 0; m = json_next(js, len, m)) {
    if (json_string(js, len, m, key, sizeof(key)) < 0 || !in_list(key, allowed)) {
      return false;
    }
  }
  return true;
}

// {"en": "...", "uk": "..."}: language codes of two letters, each text 1 to max bytes,
// English required. With out, the English text goes there (max + 1 bytes).
static bool text_ok(const char *js, int len, int at, int max, char *out) {
  if (json_type(js, len, at) != JSON_OBJECT) {
    return false;
  }
  char lang[4];
  char text[DESCRIPTION_MAX + 1];
  bool english = false;
  for (int m = json_first(js, len, at); m >= 0; m = json_next(js, len, m)) {
    bool code = json_string(js, len, m, lang, sizeof(lang)) == 2 && lang[0] >= 'a' &&
                lang[0] <= 'z' && lang[1] >= 'a' && lang[1] <= 'z';
    if (!code || !string_of(js, len, json_member_value(js, len, m), text, max)) {
      return false;
    }
    if (strcmp(lang, "en") == 0) {
      english = true;
      if (out) {
        strcpy(out, text);
      }
    }
  }
  return english;
}

static bool hosts_ok(const char *js, int len, int at, manifest_t *m) {
  if (json_type(js, len, at) != JSON_ARRAY) {
    return false;
  }
  for (int h = json_first(js, len, at); h >= 0; h = json_next(js, len, h)) {
    if (m->host_count == MANIFEST_HOSTS_MAX) {
      return false;
    }
    char *dst = m->hosts[m->host_count++];
    if (!string_of(js, len, h, dst, sizeof(m->hosts[0]) - 1) || !host_ok(dst)) {
      return false;
    }
  }
  return true;
}

// A select's options: 1 to OPTIONS_MAX of {key, label}, keys unique, def among them.
static bool options_ok(const char *js, int len, int at, const char *def) {
  if (json_type(js, len, at) != JSON_ARRAY) {
    return false;
  }
  int n = 0;
  bool has_def = false;
  for (int o = json_first(js, len, at); o >= 0; o = json_next(js, len, o)) {
    char key[OPTION_KEY_MAX + 1];
    bool ok = n++ < OPTIONS_MAX && json_type(js, len, o) == JSON_OBJECT &&
              fields_known(js, len, o, OPTION_FIELDS) &&
              string_of(js, len, json_find(js, len, o, "key"), key, OPTION_KEY_MAX) &&
              key_ok(key, OPTION_KEY_MAX) &&
              text_ok(js, len, json_find(js, len, o, "label"), LABEL_MAX, NULL);
    if (!ok) {
      return false;
    }
    for (int earlier = json_first(js, len, at); earlier >= 0 && earlier < o;
         earlier = json_next(js, len, earlier)) {
      if (json_string_eq(js, len, json_find(js, len, earlier, "key"), key)) {
        return false;
      }
    }
    has_def = has_def || strcmp(key, def) == 0;
  }
  return has_def;
}

static bool param_ok(const char *js, int len, int p, int params) {
  char key[PARAM_KEY_MAX + 1];
  char type[8];
  char def[TEXT_VALUE_MAX + 1];
  bool ok = json_type(js, len, p) == JSON_OBJECT && fields_known(js, len, p, PARAM_FIELDS) &&
            string_of(js, len, json_find(js, len, p, "key"), key, PARAM_KEY_MAX) &&
            key_ok(key, PARAM_KEY_MAX) &&
            text_ok(js, len, json_find(js, len, p, "label"), LABEL_MAX, NULL) &&
            string_of(js, len, json_find(js, len, p, "type"), type, sizeof(type) - 1) &&
            in_list(type, TYPES);
  // A text setting may default to the empty string; json_string gives 0 for it.
  int def_at = json_find(js, len, p, "def");
  ok = ok && json_type(js, len, def_at) == JSON_STRING &&
       json_string(js, len, def_at, def, sizeof(def)) >= 0;
  if (!ok) {
    return false;
  }
  for (int earlier = json_first(js, len, params); earlier >= 0 && earlier < p;
       earlier = json_next(js, len, earlier)) {
    if (json_string_eq(js, len, json_find(js, len, earlier, "key"), key)) {
      return false;
    }
  }
  int options = json_find(js, len, p, "options");
  if (strcmp(type, "select") == 0) {
    return options_ok(js, len, options, def);
  }
  if (options >= 0) {
    return false;
  }
  if (strcmp(type, "toggle") == 0) {
    return strcmp(def, "0") == 0 || strcmp(def, "1") == 0;
  }
  if (strcmp(type, "number") == 0) {
    return number_ok(def);
  }
  return true;
}

static bool params_ok(const char *js, int len, int at) {
  if (json_type(js, len, at) != JSON_ARRAY) {
    return false;
  }
  int n = 0;
  for (int p = json_first(js, len, at); p >= 0; p = json_next(js, len, p)) {
    if (n++ == PARAMS_MAX || !param_ok(js, len, p, at)) {
      return false;
    }
  }
  return true;
}

const char *manifest_parse(const char *js, int len, manifest_t *m) {
  memset(m, 0, sizeof(*m));
  if (len > MANIFEST_MAX || !json_valid(js, len) || json_type(js, len, 0) != JSON_OBJECT ||
      !keys_unique(js, len, 0, 0)) {
    return "json";
  }
  int sdk = json_find(js, len, 0, "sdk");
  char number[8];
  int sdk_len = json_end(js, len, sdk) - json_skip_space(js, len, sdk);
  if (json_type(js, len, sdk) != JSON_NUMBER || sdk_len <= 0 || sdk_len >= (int) sizeof(number)) {
    return "sdk";
  }
  memcpy(number, js + json_skip_space(js, len, sdk), sdk_len);
  number[sdk_len] = '\0';
  if (!number_ok(number) || atoi(number) > MANIFEST_SDK) {
    return "sdk";
  }
  if (atoi(number) < MANIFEST_SDK) {
    return "old_sdk";
  }
  m->sdk = MANIFEST_SDK;
  if (!fields_known(js, len, 0, TOP_FIELDS)) {
    return "fields";
  }
  if (!string_of(js, len, json_find(js, len, 0, "id"), m->id, sizeof(m->id) - 1) ||
      !manifest_id_ok(m->id)) {
    return "id";
  }
  if (!text_ok(js, len, json_find(js, len, 0, "name"), NAME_MAX, m->name)) {
    return "name";
  }
  if (!text_ok(js, len, json_find(js, len, 0, "description"), DESCRIPTION_MAX, NULL)) {
    return "description";
  }
  int category = json_find(js, len, 0, "category");
  if (!string_of(js, len, category, m->category, sizeof(m->category) - 1)) {
    return "category";
  }
  bool known = false;
  for (size_t i = 0; i < sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); i++) {
    known = known || strcmp(m->category, CATEGORIES[i]) == 0;
  }
  if (!known) {
    return "category";
  }
  if (!string_of(js, len, json_find(js, len, 0, "version"), m->version, sizeof(m->version) - 1)) {
    return "version";
  }
  if (!hosts_ok(js, len, json_find(js, len, 0, "hosts"), m)) {
    return "hosts";
  }
  if (!params_ok(js, len, json_find(js, len, 0, "params"))) {
    return "params";
  }
  return NULL;
}

// ---- settings ------------------------------------------------------------------------------

// The setting `key` of a checked manifest, or -1.
static int param_of(const char *js, int len, const char *key) {
  int params = json_find(js, len, 0, "params");
  for (int p = json_first(js, len, params); p >= 0; p = json_next(js, len, p)) {
    if (json_string_eq(js, len, json_find(js, len, p, "key"), key)) {
      return p;
    }
  }
  return -1;
}

// The option of select p whose key is value, or -1.
static int option_of(const char *js, int len, int p, const char *value) {
  int options = json_find(js, len, p, "options");
  for (int o = json_first(js, len, options); o >= 0; o = json_next(js, len, o)) {
    if (json_string_eq(js, len, json_find(js, len, o, "key"), value)) {
      return o;
    }
  }
  return -1;
}

bool manifest_value_ok(const char *js, int len, const char *key, const char *value) {
  int p = param_of(js, len, key);
  if (p < 0) {
    return false;
  }
  int type = json_find(js, len, p, "type");
  if (json_string_eq(js, len, type, "select")) {
    return option_of(js, len, p, value) >= 0;
  }
  if (json_string_eq(js, len, type, "toggle")) {
    return strcmp(value, "0") == 0 || strcmp(value, "1") == 0;
  }
  if (json_string_eq(js, len, type, "number")) {
    return number_ok(value);
  }
  return strlen(value) <= TEXT_VALUE_MAX;
}

bool manifest_key_for_label(const char *js, int len, const char *key, const char *value, char *out,
                            int cap) {
  int p = param_of(js, len, key);
  if (p < 0 || !json_string_eq(js, len, json_find(js, len, p, "type"), "select") ||
      option_of(js, len, p, value) >= 0) {
    return false;
  }
  int options = json_find(js, len, p, "options");
  for (int o = json_first(js, len, options); o >= 0; o = json_next(js, len, o)) {
    int label = json_find(js, len, o, "label");
    for (int t = json_first(js, len, label); t >= 0; t = json_next(js, len, t)) {
      if (json_string_eq(js, len, json_member_value(js, len, t), value)) {
        return json_string(js, len, json_find(js, len, o, "key"), out, cap) > 0;
      }
    }
  }
  return false;
}

void manifest_selects(const char *js, int len, bool (*fn)(const char *key, void *ctx), void *ctx) {
  int params = json_find(js, len, 0, "params");
  for (int p = json_first(js, len, params); p >= 0; p = json_next(js, len, p)) {
    char key[PARAM_KEY_MAX + 1];
    bool select = json_string_eq(js, len, json_find(js, len, p, "type"), "select");
    if (select && json_string(js, len, json_find(js, len, p, "key"), key, sizeof(key)) > 0 &&
        !fn(key, ctx)) {
      return;
    }
  }
}
