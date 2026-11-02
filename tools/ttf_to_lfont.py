#!/usr/bin/env python3
"""Turn a TrueType font into a `.lfont`, the bitmap text face this system reads fastest.

    python3 tools/ttf_to_lfont.py <font.ttf> <pixels> <out.lfont> [--gap 1]

A `.lfont` is the cheapest face the kernel knows how to draw: a face of a file is a block of
`96 * kMaxGlyphHeight * kGlyphRowBytes` bytes —3072 with the sixteen rows of 0.55.0— and nothing is decoded when
a text is drawn, so a page of a bitmap face costs less than the same page of outlines and needs no rasteriser.

What it costs is that the size is **baked in**: what this writes is one size, chosen here, and scaling it up is
a multiply and not a drawing. That is the trade: a TrueType face is drawn at every size and read from the file
every time; a `.lfont` is one size and a table (docs/30).

The glyphs are rendered with Pillow from the ascender line down, so every letter sits on the same baseline, and
the cell is as wide as the widest advance of the printable ascii — a `lunok font` is a fixed-pitch table and
the faces of this system are laid out with one width and one gap.
"""
import argparse
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - the message is the point
    sys.exit("this needs Pillow: python3 -m pip install --user pillow")

FIRST = 0x20
LAST = 0x7F
MAX_SIDE = 16  # font.h: kMaxGlyphWidth and kMaxGlyphHeight
THRESHOLD = 128


def render_cell(font, char, width, height, ascent, stroke):
    """One glyph as a list of rows of `#` and `.`, on the baseline and **centred in the cell**.

    A `.lfont` is fixed pitch and a font of a machine is not, so a letter that is drawn from its own left
    bearing leaves the air of the widest one on its right and a word comes out full of holes. Centring every
    glyph in the cell is what makes a proportional font readable in a table that has one width (docs/30).
    """
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    ink = draw.textbbox((0, ascent), char, font=font, anchor="ls", stroke_width=stroke)
    offset = (width - (ink[2] - ink[0])) // 2 - ink[0]
    draw.text((offset, ascent), char, font=font, fill=255, anchor="ls", stroke_width=stroke)
    rows = []
    for y in range(height):
        row = ""
        for x in range(width):
            row += "#" if image.getpixel((x, y)) >= THRESHOLD else "."
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description="TrueType to the portable font of lunokOS")
    parser.add_argument("ttf")
    parser.add_argument("pixels", type=int, help="body size in pixels, which is what decides the ink")
    parser.add_argument("out")
    parser.add_argument("--gap", type=int, default=1, help="pixels between two glyphs (0 to 4)")
    parser.add_argument("--height", type=int, default=16, help="rows of the cell (up to 16)")
    parser.add_argument("--baseline", type=int, default=0,
                        help="row the baseline sits on (default: four rows of descender at the bottom)")
    parser.add_argument("--weight", default="SemiBold",
                        help="the instance of a variable font to use, or '' for the default")
    parser.add_argument("--stroke", type=int, default=1,
                        help="pixels of stroke added round every glyph (0 for a thin one)")
    args = parser.parse_args()

    font = ImageFont.truetype(args.ttf, args.pixels)
    if args.weight:
        try:
            font.set_variation_by_name(args.weight)
        except Exception:  # a static font has no instances, and that is not an error
            pass
    height = args.height
    ascent = args.baseline if args.baseline > 0 else max(1, height - 4)
    # The cell is the widest advance of the printable ascii: a lunok font has one width for every glyph.
    width = 0
    for code in range(FIRST, LAST + 1):
        advance = int(round(font.getlength(chr(code))))
        width = max(width, advance)
    if width > MAX_SIDE or height > MAX_SIDE:
        sys.exit(
            "the cell would be %dx%d and the kernel reads up to %dx%d: use a smaller size"
            % (width, height, MAX_SIDE, MAX_SIDE)
        )
    if args.gap < 0 or args.gap > 4:
        sys.exit("the gap is zero to four")

    lines = [
        "; a lunok font, made by tools/ttf_to_lfont.py",
        "; from: %s" % args.ttf,
        "; body: %d px, cell %dx%d, gap %d" % (args.pixels, width, height, args.gap),
        "lunok-font 1",
        "width %d" % width,
        "height %d" % height,
        "gap %d" % args.gap,
    ]
    for code in range(FIRST, LAST + 1):
        lines.append("char %d" % code)
        lines.extend(render_cell(font, chr(code), width, height, ascent, args.stroke))
    with open(args.out, "w", encoding="ascii") as handle:
        handle.write("\n".join(lines) + "\n")

    print("%s: %dx%d, %d glyphs, gap %d, %d bytes" % (args.out, width, height, LAST - FIRST + 1,
                                                      args.gap, sum(len(l) + 1 for l in lines)))


if __name__ == "__main__":
    main()
