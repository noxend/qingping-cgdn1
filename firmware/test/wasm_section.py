"""A WebAssembly module with one custom section taken off, for the host tests that put
a different plugin.json back with plugins/sdk/embed_manifest.py."""

CUSTOM = 0


def read_leb(code, p):
    """The unsigned LEB128 number at p and the position after it."""
    n = shift = 0
    while True:
        b = code[p]
        p += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return n, p


def without_section(code, name=b"cgdn1.plugin"):
    """The module's bytes with every custom section called name left out."""
    out, p = bytearray(code[:8]), 8
    while p < len(code):
        size, body = read_leb(code, p + 1)
        drop = False
        if code[p] == CUSTOM:
            name_len, q = read_leb(code, body)
            drop = code[q : q + name_len] == name
        if not drop:
            out += code[p : body + size]
        p = body + size
    return bytes(out)
