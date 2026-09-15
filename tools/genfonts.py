#!python3
"""Generate src/ui_fonts.h for the Lineup timetracker UI.

Derived from LilyGo-EPD47-esp32s3/scripts/fontconvert.py, with two changes:
the Latin-1 supplement is included so French accents render, and several
faces/sizes are emitted into a single header.

    pip install freetype-py
    python tools/genfonts.py src/ui_fonts.h

The file is written directly rather than through shell redirection, which on
Windows PowerShell would produce UTF-16 and drown the compiler in "null
character ignored" warnings.

`size` is a point size rasterised at 150 dpi (the panel's approximate
density), so the em box is roughly size * 150/72 pixels.

The UI is all-caps monospace (Source Code Pro). Sizes are a step smaller
than the old Barlow Semi Condensed set because a fixed advance is wider.
"""

import argparse
import math
import os
import sys
import zlib
from collections import namedtuple

import freetype

# Latin-1 supplement gives us é è ê à â ç ô ù î ü and friends.
INTERVALS = [(0x20, 0x7E), (0xA0, 0xFF)]

WIN_FONTS = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts"),
    os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts"),
]

# name, font file, point size
FONTS = [
    ("UiMicro", "SourceCodePro-Medium.ttf", 8),
    ("UiSmall", "SourceCodePro-Medium.ttf", 10),
    ("UiBody", "SourceCodePro-Medium.ttf", 12),
    ("UiBodyBold", "SourceCodePro-Semibold.ttf", 12),
    ("UiTitle", "SourceCodePro-Semibold.ttf", 16),
    ("UiDisplay", "SourceCodePro-Bold.ttf", 24),
]

GlyphProps = namedtuple(
    "GlyphProps",
    ["width", "height", "advance_x", "left", "top", "compressed_size", "data_offset", "code_point"],
)


def find_font(filename):
    for directory in WIN_FONTS:
        candidate = os.path.join(directory, filename)
        if os.path.isfile(candidate):
            return candidate
    raise SystemExit(f"font not found: {filename} (looked in {WIN_FONTS})")


def norm_floor(val):
    return int(math.floor(val / (1 << 6)))


def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


def pack_bitmap(bitmap):
    """Pack an 8bpp grayscale bitmap into 4bpp, two pixels per byte.

    A byte never wraps across rows, so odd-width rows get a padding nibble.
    """
    pixels = bytearray()
    px = 0
    for i, v in enumerate(bitmap.buffer):
        x = i % bitmap.width
        if x % 2 == 0:
            px = v >> 4
        else:
            pixels.append(px | (v & 0xF0))
            px = 0
        if x == bitmap.width - 1 and bitmap.width % 2 > 0:
            pixels.append(px)
            px = 0
    return bytes(pixels)


def build_font(name, filename, size, out):
    face = freetype.Face(find_font(filename))
    face.set_char_size(size << 6, size << 6, 150, 150)

    glyph_data = bytearray()
    glyph_props = []
    raw_total = 0

    for first, last in INTERVALS:
        for code_point in range(first, last + 1):
            index = face.get_char_index(code_point)
            if index == 0:
                # Keep the glyph table dense: fall back to a blank advance so
                # interval offsets stay valid.
                index = face.get_char_index(ord(" "))
                print(f"{name}: missing U+{code_point:04X}, using space", file=sys.stderr)
            face.load_glyph(index, freetype.FT_LOAD_RENDER)

            packed = pack_bitmap(face.glyph.bitmap)
            raw_total += len(packed)
            compressed = zlib.compress(packed)

            glyph_props.append(
                GlyphProps(
                    width=face.glyph.bitmap.width,
                    height=face.glyph.bitmap.rows,
                    advance_x=norm_floor(face.glyph.advance.x),
                    left=face.glyph.bitmap_left,
                    top=face.glyph.bitmap_top,
                    compressed_size=len(compressed),
                    data_offset=len(glyph_data),
                    code_point=code_point,
                )
            )
            glyph_data.extend(compressed)

    # `|` is a better heuristic for the real descender than the face metric.
    face.load_glyph(face.get_char_index(ord("|")), freetype.FT_LOAD_RENDER)

    print(f"{name}: {len(glyph_props)} glyphs, {raw_total} -> {len(glyph_data)} bytes", file=sys.stderr)

    out.write(f"const uint8_t {name}Bitmaps[{len(glyph_data)}] PROGMEM = {{\n")
    for chunk in chunks(glyph_data, 16):
        out.write("    " + " ".join(f"0x{b:02X}," for b in chunk) + "\n")
    out.write("};\n")

    out.write(f"const GFXglyph {name}Glyphs[] PROGMEM = {{\n")
    for g in glyph_props:
        # The header stays pure ASCII so no compiler ever has to guess at its
        # encoding; anything outside printable ASCII is named instead.
        printable = 0x21 <= g.code_point <= 0x7E and g.code_point != 0x5C
        label = chr(g.code_point) if printable else f"U+{g.code_point:04X}"
        out.write("    { " + ", ".join(str(v) for v in g[:-1]) + " }," + f" // {label}\n")
    out.write("};\n")

    out.write(f"const UnicodeInterval {name}Intervals[] PROGMEM = {{\n")
    offset = 0
    for first, last in INTERVALS:
        out.write(f"    {{ 0x{first:X}, 0x{last:X}, 0x{offset:X} }},\n")
        offset += last - first + 1
    out.write("};\n")

    out.write(f"const GFXfont {name} PROGMEM = {{\n")
    out.write(f"    (uint8_t *){name}Bitmaps,\n")
    out.write(f"    (GFXglyph *){name}Glyphs,\n")
    out.write(f"    (UnicodeInterval *){name}Intervals,\n")
    out.write(f"    {len(INTERVALS)},\n")
    out.write("    1,\n")
    out.write(f"    {norm_ceil(face.size.height)},\n")
    out.write(f"    {norm_ceil(face.size.ascender)},\n")
    out.write(f"    {norm_floor(face.size.descender)},\n")
    out.write("};\n\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default="src/ui_fonts.h")
    args = parser.parse_args()

    with open(args.output, "w", encoding="ascii", newline="\n") as out:
        out.write("// Generated by tools/genfonts.py -- do not edit by hand.\n")
        out.write("// Source Code Pro, ASCII + Latin-1 supplement, zlib compressed.\n")
        out.write("#pragma once\n")
        out.write('#include "epd_driver.h"\n\n')
        for name, filename, size in FONTS:
            build_font(name, filename, size, out)

    print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
