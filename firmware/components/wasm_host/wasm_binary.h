// A WebAssembly binary read without the runtime: its sections in order, and a custom
// section by name. Every length is checked against the end of the buffer, so a
// malformed binary is reported, never read past.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// An unsigned LEB128 number of at most 32 bits at *p; *p moves past it.
bool wasm_leb_read(const uint8_t **p, const uint8_t *end, uint32_t *out);

// Calls fn with each section's id and contents; false from fn stops the walk. False when
// the header is not WebAssembly 1 or a section runs past the end.
typedef bool (*wasm_section_fn)(uint8_t id, const uint8_t *body, uint32_t size, void *ctx);
bool wasm_sections(const uint8_t *code, uint32_t len, wasm_section_fn fn, void *ctx);

// The contents of the first custom section called name, after the name, or NULL.
const uint8_t *wasm_custom_section(const uint8_t *code, uint32_t len, const char *name,
                                   uint32_t *size);
