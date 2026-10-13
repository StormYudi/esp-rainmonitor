#!/usr/bin/env python3
"""Rasterize Montserrat into 4bpp anti-aliased C font tables."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ASCII = [chr(i) for i in range(32, 127)]
CLOCK = list("-0123456789:")
DIGITS = list(" 0123456789%.-/")


def pack_4bpp(pixels: list[int], width: int, height: int) -> bytes:
    out = bytearray()
    nibble = None
    for y in range(height):
        for x in range(width):
            a = pixels[y * width + x] >> 4
            if nibble is None:
                nibble = a << 4
            else:
                out.append(nibble | a)
                nibble = None
        if nibble is not None:
            out.append(nibble)
            nibble = None
    return bytes(out)


def raster_glyph(font: ImageFont.FreeTypeFont, ch: str) -> dict:
    bbox = font.getbbox(ch)
    if bbox is None:
        bbox = (0, 0, 1, 1)
    left, top, right, bottom = bbox
    width = max(1, right - left)
    height = max(1, bottom - top)
    img = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(img)
    draw.text((-left, -top), ch, font=font, fill=255)
    pixels = list(img.getdata())
    advance = int(round(font.getlength(ch)))
    return {
        "ch": ch,
        "width": width,
        "height": height,
        "xoff": left,
        "yoff": top,
        "advance": max(1, advance),
        "bitmap": pack_4bpp(pixels, width, height),
    }


def emit_font(name: str, px: int, ttf: Path, chars: list[str]) -> str:
    face = ImageFont.truetype(str(ttf), px)
    ascent, descent = face.getmetrics()
    line_height = ascent + descent + 2
    glyphs = [raster_glyph(face, ch) for ch in chars]
    first = min(ord(ch) for ch in chars)
    last = max(ord(ch) for ch in chars)
    # Fill holes so first..last is dense
    by_code = {ord(g["ch"]): g for g in glyphs}
    space = by_code.get(32, glyphs[0])
    dense = []
    for code in range(first, last + 1):
        dense.append(by_code.get(code, {**space, "ch": chr(code), "advance": space["advance"]}))

    bits = []
    decls = []
    for g in dense:
        ident = f"{name}_{ord(g['ch'])}"
        payload = ", ".join(f"0x{b:02X}" for b in g["bitmap"]) or "0x00"
        bits.append(f"static const uint8_t {ident}[] = {{{payload}}};")
        decls.append(
            f"    {{{g['width']}, {g['height']}, {g['xoff']}, {g['yoff']}, {g['advance']}, {ident}}},"
        )

    return "\n".join(
        [
            *bits,
            f"static const font_glyph_t {name}_glyphs[] = {{",
            *decls,
            "};",
            f"const font_t {name} = {{",
            f"    .first = {first},",
            f"    .last = {last},",
            f"    .line_height = {line_height},",
            f"    .glyphs = {name}_glyphs,",
            "};",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--medium", required=True)
    parser.add_argument("--semibold", required=True)
    parser.add_argument("--bold", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    chunks = [
        "/* Auto-generated Montserrat 4bpp glyphs. SIL Open Font License 1.1. */",
        '#include "font.h"',
        "",
        emit_font("FONT_16", 16, Path(args.medium), ASCII),
        emit_font("FONT_20", 20, Path(args.semibold), ASCII),
        emit_font("FONT_26", 26, Path(args.semibold), ASCII),
        emit_font("FONT_32", 32, Path(args.semibold), CLOCK),
        emit_font("FONT_56", 56, Path(args.bold), DIGITS),
    ]
    Path(args.out).write_text("\n".join(chunks) + "\n", encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
