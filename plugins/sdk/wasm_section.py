"""A WebAssembly module's custom sections, and the one called "cgdn1.plugin" that
carries a plugin's plugin.json: embed_manifest.py adds it, the device reads it
(components/wasm_host/wasm_binary.c), the host tests and web/mock_device.py take it apart."""

CUSTOM = 0
PLUGIN = b"cgdn1.plugin"
HEADER = b"\0asm\x01\0\0\0"


def write_leb(n):
    """n as unsigned LEB128."""
    out = bytearray()
    while True:
        byte, n = n & 0x7F, n >> 7
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def read_leb(code, at):
    """The unsigned LEB128 number at `at` and the position after it. The number is None
    when the module ends inside it."""
    value = shift = 0
    while at < len(code):
        byte = code[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at
    return None, at


def walk(code):
    """(start, end, name, payload) of each section: code[start:end] is all of it, name the
    custom section's name (None for any other section), code[payload:end] what follows
    the name. Stops at a section that runs past the end of the module."""
    at = len(HEADER)
    while at < len(code):
        size, body = read_leb(code, at + 1)
        if size is None or body + size > len(code):
            return
        end = body + size
        name, payload = None, body
        if code[at] == CUSTOM:
            n, name_at = read_leb(code, body)
            if n is not None:
                name, payload = bytes(code[name_at : name_at + n]), name_at + n
        yield at, end, name, payload
        at = end


def custom_section(code, name=PLUGIN):
    """What the custom section called name carries, or None when the module has none."""
    if code[: len(HEADER)] != HEADER:
        return None
    for _start, end, found, payload in walk(code):
        if found == name:
            return code[payload:end]
    return None


def without_section(code, name=PLUGIN):
    """The module's bytes with every custom section called name left out."""
    out = bytearray(code[: len(HEADER)])
    for start, end, found, _payload in walk(code):
        if found != name:
            out += code[start:end]
    return bytes(out)


def encode_section(payload, name=PLUGIN):
    """A custom section called name carrying payload, to append to a module."""
    body = write_leb(len(name)) + name + payload
    return bytes([CUSTOM]) + write_leb(len(body)) + body
