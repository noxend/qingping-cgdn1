// JSON read in place: a text in a buffer and offsets into it, no allocation. Every
// read is checked against the buffer's end, so a malformed or cut text gives -1, never
// a read past it. Used for plugin manifests and, through the SDK, by plugins on the
// bodies they fetch. No firmware headers: firmware/test/json runs it on the Mac.
#pragma once

#include <stdbool.h>

// Containers nest at most this deep; a deeper text is not valid here.
#define JSON_DEPTH_MAX 32
// json_enum and json_string_eq compare strings of at most this many bytes.
#define JSON_COMPARE_MAX 255

typedef enum {
  JSON_BAD,
  JSON_OBJECT,
  JSON_ARRAY,
  JSON_STRING,
  JSON_NUMBER,
  JSON_TRUE,
  JSON_FALSE,
  JSON_NULL,
} json_type_t;

// The offset of the value that starts at `at` or after whitespace there, or -1.
int json_skip_space(const char *js, int len, int at);

// The type of the value at `at`, from its first byte alone.
json_type_t json_type(const char *js, int len, int at);

// The offset just after the value at `at`, or -1 when it is not one whole valid value:
// the grammar of RFC 8259, strings in valid UTF-8 without unpaired surrogate escapes.
int json_end(const char *js, int len, int at);

// True when the buffer holds one valid value and nothing else but whitespace.
bool json_valid(const char *js, int len);

// The first element of the array or object at `at`, or -1 when it is empty, not a
// container, or not a whole valid value (cut short, say). An object's elements are its
// members, at their keys.
int json_first(const char *js, int len, int at);

// The element after the one at `at` in the same container, or -1 after the last.
int json_next(const char *js, int len, int at);

// The value of the member whose key starts at `at`, or -1.
int json_member_value(const char *js, int len, int at);

// The value of the member `key` of the object at `at`, or -1 when there is none or it is
// not a whole valid value. The first one when the key repeats.
int json_find(const char *js, int len, int at, const char *key);

// Copies the string at `at`, decoded, into out with a terminator. Its length in bytes,
// or -1 when the value is not a string, holds a NUL, or does not fit in cap.
int json_string(const char *js, int len, int at, char *out, int cap);

// Whether the string at `at`, decoded, is s.
bool json_string_eq(const char *js, int len, int at, const char *s);

// Which of a list of strings the string at `at` is: list holds them one after another,
// each ending in a NUL, and ends at list_len or at an empty string. The index, or -1.
int json_enum(const char *js, int len, int at, const char *list, int list_len);
