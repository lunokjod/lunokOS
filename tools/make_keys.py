#!/usr/bin/env python3
"""The four glyphs the time clock draws, as a face this system can load.

The app of PaperOS carried `keys.pof`, a face of its own folder with four marks of the **FontAwesome family** —the
same family as the icons of that system—: play (U+F04B), pause (U+F04C), stop (U+F04D) and an eye (U+F06E). The
`pof` was not drawn by hand: `tools/fontgen.py` of that project rasterised those four characters of
`FontAwesome.otf` into a one bit a pixel table (README, 27/10, second part).

This system reads a lunok face, a BDF, a PCF or a TrueType, so the port of the app drew **words** where the original
drew marks. The path taken here is therefore the original's: the **same characters of the same font**, whose outlines
are taken out of `FontAwesome.otf` and written into a TrueType of four glyphs and nothing else. Nothing is drawn by
this file —it carries no shape of its own—, so the day the marks want another weight it is another font file and not
a table of coordinates to fix.

    python3 tools/make_keys.py --out apps/timeclock/res/keys.ttf
"""

import argparse
import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

# **Where the marks live, and why not where the original had them**: a text of this system is one byte a character, so a
# codepoint of the private use area (U+F04B, which is what `keys.pof` of PaperOS used) cannot be written in a Lua string.
# The four marks are mapped to the four first control codes —`\1`, `\2`, `\3`, `\4`— which no page ever types and which a
# face can carry: the app writes `string.char(1)` and the face draws the triangle (docs/30).
#
# Each one names the glyph of FontAwesome it is, the code the face answers to, and the codepoint of the original (kept
# so the correspondence with `keys.pof` can be read and checked).
GLYPHS = [
    ("play", "play", 0x01, 0xF04B),
    ("pause", "pause", 0x02, 0xF04C),
    ("stop", "stop", 0x03, 0xF04D),
    ("eye", "eye_open", 0x04, 0xF06E),
    ("back", "arrow_left", 0x05, 0xF060),
]

DEFAULT_SOURCE = "/usr/share/fonts-font-awesome/fonts/FontAwesome.otf"
MAX_ERROR = 1.0    # font units; the shapes are read at some 40 px and this is under a tenth of a pixel there


def build(source, out_path):
    if not os.path.exists(source):
        raise SystemExit("%s is not here: apt install fonts-font-awesome" % source)
    src = TTFont(source)
    upem = src["head"].unitsPerEm
    glyph_set = src.getGlyphSet()
    hmtx = src["hmtx"]

    order = [".notdef"] + [name for name, _, _, _ in GLYPHS]
    glyphs = {}
    metrics = {}
    cmap = {}

    # A face of four marks needs a `.notdef` because every font has one; it draws nothing and takes half an em.
    glyphs[".notdef"] = TTGlyphPen(None).glyph()
    metrics[".notdef"] = (upem // 2, 0)

    for name, source_name, code, _codepoint in GLYPHS:
        if source_name not in glyph_set:
            raise SystemExit("%s has no glyph %s" % (source, source_name))
        # The outlines of the original are cubic (a CFF), and a TrueType of this system reads quadratic curves: the pen
        # in the middle turns one into the other with an error under a tenth of a pixel at the size the app draws them.
        pen = TTGlyphPen(glyph_set)
        glyph_set[source_name].draw(Cu2QuPen(pen, MAX_ERROR, reverse_direction=True))
        glyphs[name] = pen.glyph()
        metrics[name] = hmtx[source_name]
        cmap[code] = name

    builder = FontBuilder(upem, isTTF=True)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=src["hhea"].ascent, descent=src["hhea"].descent)
    builder.setupNameTable({
        "familyName": "lunokOS keys",
        "styleName": "Regular",
        "uniqueFontIdentifier": "lunokOS keys Regular 1.0",
        "fullName": "lunokOS keys Regular",
        "psName": "lunokOSKeys-Regular",
    })
    builder.setupOS2(sTypoAscender=src["hhea"].ascent, sTypoDescender=src["hhea"].descent)
    builder.setupPost()
    builder.setupMaxp()
    builder.save(out_path)
    # **The file is read back, because what this tool promises is the file and not the object it built**: a code with
    # no glyph, or a glyph with no width, is a key that draws nothing on the glass, and both are mistakes the writer
    # above can make without saying a word about them.
    check(out_path)
    return out_path


def check(path):
    made = TTFont(path)
    cmap = made.getBestCmap()
    for name, _source, code, codepoint in GLYPHS:
        glyph = cmap.get(code)
        if glyph is None:
            raise SystemExit("%s: the code %d has no glyph (the U+%04X of the original)" % (path, code, codepoint))
        advance, _left = made["hmtx"][glyph]
        contours = getattr(made["glyf"][glyph], "numberOfContours", 0)
        if advance <= 0 or contours <= 0:
            raise SystemExit("%s: %s is empty or has no width" % (path, name))
        print("  %d %-5s glyph %-10s %4d units wide, %d contour(s), from U+%04X"
              % (code, name, glyph, advance, contours, codepoint))


def main():
    parser = argparse.ArgumentParser(description="The four marks of the time clock, taken from FontAwesome.")
    parser.add_argument("--source", default=DEFAULT_SOURCE, help="the font the marks of the original come from")
    parser.add_argument("--out", default="apps/timeclock/res/keys.ttf")
    args = parser.parse_args()
    made = build(args.source, args.out)
    print("%s: %d glyphs, %d bytes" % (made, len(GLYPHS) + 1, os.path.getsize(made)))


if __name__ == "__main__":
    main()
