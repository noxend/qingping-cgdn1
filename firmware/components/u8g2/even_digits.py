#!/usr/bin/env python3
"""Evens out u8g2_font_logisoso42_tn in u8g2_fonts_subset.c.

As u8g2 ships it the font has two flaws on the panel:
- each dot of '.' and ':' is a 5 x 5 square with three corners cut and the fourth left
  on, a dot with a one-pixel tail;
- every digit is 27 px wide, the 21 px digits with 3 px on each side but the 11 px '1'
  with 8, and '-', ',', '.' and ':' have 5 or 6 px sides, so the gaps in a number are 6 px
  between most digits and 8 to 12 px next to a '1', a dot or a dash.
This rounds the dots and gives '1', '-', ',', '.' and ':' 3 px sides, so every gap is 6 px.
The cost: a number with a '1' is narrower than one without, so a centred clock shifts by
a few pixels when a '1' comes or goes. Other glyphs and the font's height stay as they
were. Running it again changes nothing.

The font format is u8g2's (github.com/olikraus/u8g2/wiki/u8g2fontformat): a 23-byte
header, then per glyph its encoding, the offset to the next glyph and a bit stream,
least significant bit first, of width, height, x, y and advance followed by runs of
background and foreground pixels.

Usage: python3 -I firmware/components/u8g2/even_digits.py
"""

import os
import re
import sys

FONT = "u8g2_font_logisoso42_tn"
SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "u8g2_fonts_subset.c")

DOT = [".###.", "#####", "#####", "#####", ".###."]
COLON = DOT + ["....."] * 14 + DOT
SIDE = 3  # the space left and right of each digit's ink, as in '0' to '9' but '1'

# A new picture (None keeps the old one); every glyph listed gets SIDE on both sides.
GLYPHS = {".": DOT, ":": COLON, "1": None, "-": None, ",": None}

HEADER = 23
UNICODE_POS = 21  # two bytes: the Unicode table's offset from the end of the header


def parse_literal(text):
    """The bytes of a C string literal's body."""
    out = bytearray()
    i = 0
    while i < len(text):
        if text[i] != "\\":
            out.append(ord(text[i]))
            i += 1
            continue
        j = i + 1
        k = j
        while k < len(text) and k < j + 3 and text[k] in "01234567":
            k += 1
        if k > j:
            out.append(int(text[j:k], 8))
            i = k
            continue
        out.append(ord({"n": "\n", "t": "\t"}.get(text[j], text[j])))
        i = j + 1
    return bytes(out)


def to_literal(data, width=88):
    """C string literals, one per line: printable ASCII as is, the rest as three-digit
    octal, so no escape can swallow the digit after it."""
    lines, line = [], ""
    for b in data:
        c = chr(b)
        token = c if 32 <= b < 127 and c not in '"\\?' else "\\%03o" % b
        if len(line) + len(token) > width:
            lines.append(line)
            line = ""
        line += token
    lines.append(line)
    return "\n".join('  "%s"' % line for line in lines)


class BitReader:
    def __init__(self, data, at):
        self.data, self.at, self.bit = data, at, 0

    def get(self, count):
        value = 0
        for k in range(count):
            value |= ((self.data[self.at] >> self.bit) & 1) << k
            self.bit += 1
            if self.bit == 8:
                self.bit, self.at = 0, self.at + 1
        return value


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, value, count):
        self.bits += [(value >> k) & 1 for k in range(count)]

    def bytes(self):
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            chunk = self.bits[i : i + 8]
            out.append(sum(bit << k for k, bit in enumerate(chunk)))
        return bytes(out)


def field_widths(font):
    """Bit widths: background run, foreground run, width, height, x, y, advance."""
    return font[2:9]


def decode(font, at):
    """A glyph's (width, height, x, y, advance, pixels) from its record at `at`."""
    b0, b1, bw, bh, bx, by, bd = field_widths(font)
    bits = BitReader(font, at + 2)
    w, h = bits.get(bw), bits.get(bh)
    x = bits.get(bx) - (1 << (bx - 1))
    y = bits.get(by) - (1 << (by - 1))
    dx = bits.get(bd) - (1 << (bd - 1))
    pixels = []
    while h and len(pixels) < w * h:
        zeros, ones = bits.get(b0), bits.get(b1)
        while True:
            pixels += [0] * zeros + [1] * ones
            if not bits.get(1):
                break
    return w, h, x, y, dx, pixels[: w * h]


def encode(font, encoding, w, h, x, y, dx, pixels):
    """A glyph record: one run pair per stretch of background then foreground pixels."""
    b0, b1, bw, bh, bx, by, bd = field_widths(font)
    bits = BitWriter()
    bits.put(w, bw)
    bits.put(h, bh)
    bits.put(x + (1 << (bx - 1)), bx)
    bits.put(y + (1 << (by - 1)), by)
    bits.put(dx + (1 << (bd - 1)), bd)
    runs, i = [], 0
    while i < len(pixels):
        zeros = ones = 0
        while i < len(pixels) and pixels[i] == 0 and zeros < (1 << b0) - 1:
            zeros, i = zeros + 1, i + 1
        while i < len(pixels) and pixels[i] == 1 and ones < (1 << b1) - 1:
            ones, i = ones + 1, i + 1
        runs.append((zeros, ones))
    for zeros, ones in runs:
        bits.put(zeros, b0)
        bits.put(ones, b1)
        bits.put(0, 1)
    body = bits.bytes()
    return bytes([encoding, len(body) + 2]) + body


def rewrite(font):
    out = bytearray(font[:HEADER])
    at = HEADER
    while font[at + 1] != 0:
        encoding, jump = font[at], font[at + 1]
        record = font[at : at + jump]
        if chr(encoding) in GLYPHS:
            rows = GLYPHS[chr(encoding)]
            w, h, x, y, dx, pixels = decode(font, at)
            if rows:
                if (w, h) != (len(rows[0]), len(rows)):
                    sys.exit(f"{chr(encoding)!r} is {w} x {h}, not the expected size")
                pixels = [1 if c == "#" else 0 for row in rows for c in row]
            record = encode(font, encoding, w, h, SIDE, y, w + 2 * SIDE, pixels)
        out += record
        at += jump
    shift = len(out) - at
    out += font[at:]
    unicode_pos = (font[UNICODE_POS] << 8 | font[UNICODE_POS + 1]) + shift
    out[UNICODE_POS : UNICODE_POS + 2] = bytes([unicode_pos >> 8, unicode_pos & 0xFF])
    return bytes(out)


def main():
    source = open(SOURCE, encoding="latin-1").read()
    pattern = re.compile(
        r"(const uint8_t %s\[)(\d+)(\][^=]*=\s*\n)((?:\s*\"(?:[^\"\\]|\\.)*\")+)(;)" % FONT
    )
    match = pattern.search(source)
    if not match:
        sys.exit(f"{FONT} not found in {SOURCE}")
    literal = "".join(re.findall(r"\"((?:[^\"\\]|\\.)*)\"", match.group(4)))
    font = parse_literal(literal)
    new = rewrite(font)
    for encoding, rows in GLYPHS.items():
        at = HEADER
        while new[at] != ord(encoding):
            at += new[at + 1]
        w, _, x, _, dx, pixels = decode(new, at)
        if rows and pixels != [1 if c == "#" else 0 for row in rows for c in row]:
            sys.exit(f"{encoding!r} does not read back as written")
        if (x, dx) != (SIDE, w + 2 * SIDE):
            sys.exit(f"{encoding!r} does not read back with its new sides")
    # The array's size counts the terminating NUL of the string.
    text = match.group(1) + str(len(new) + 1) + match.group(3) + to_literal(new) + match.group(5)
    source = source[: match.start()] + text + source[match.end() :]
    open(SOURCE, "w", encoding="latin-1").write(source)
    print(f"{FONT}: {len(font)} -> {len(new)} bytes")


if __name__ == "__main__":
    main()
