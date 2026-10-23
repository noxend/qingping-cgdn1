// The one rule for putting text inside a JSON string: quote and backslash get a
// backslash, control bytes become \u00XX, every other byte (UTF-8 included) stays.
#pragma once

// The longest form of one byte: \u00XX.
#define JSON_ESCAPE_MAX 6

// One byte in its JSON form, written to out; returns how many bytes that is.
int json_escape_byte(unsigned char c, char out[JSON_ESCAPE_MAX]);

// s as the inside of a JSON string, without the quotes, NUL-terminated. When it does
// not fit, it stops before the first byte whose escape would not, so out is never cut
// inside an escape. Returns the length written.
int json_escape(char *out, int cap, const char *s);
