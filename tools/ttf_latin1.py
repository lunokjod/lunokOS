#!/usr/bin/env python3
"""A face with the letters of Latin-1, and the pieces of its glyphs flattened.

**What this is for.** The faces of this system are TrueType files, and the reader of the board
(`lib/ttf`) reads a glyph that is a list of contours. A font like DejaVu writes `e acute` as a
**glyph made of pieces** — the `e` and the accent, each one its own glyph, with an offset — and the
reader refuses that by name ("a glyph made of pieces"). A book in Catalan is nothing but accented
letters, so a face that this system can use for one has to have those glyphs written out.

Two things happen here and nothing else:

  1. **The subset**: the file keeps the printable ASCII, the letters of Latin-1 (`U+00A0` to
     `U+00FF`) and the punctuation a book uses — the dashes, the quotation marks, the ellipsis and
     the bullet — which is what an EPUB of this house needs and no more.
  2. **The flattening**: every glyph made of pieces is replaced by the contours of its pieces put
     where they belong, so the reader sees a plain glyph like any other.

The trick of the flattening is `DecomposingRecordingPen`: it walks a glyph and, when it finds a
piece, walks the piece instead of recording it, so what comes out is the drawing and not the recipe.
`TTGlyphPen` turns that drawing back into a glyph of a TrueType table.

    python3 tools/ttf_latin1.py --ttf /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf \\
        --out data/res/fonts/dejavuserif-latin1.ttf

The measurements this prints are the ones the reader cares about: how many glyphs, how many of them
were made of pieces, and the biggest glyph in points and contours —because `lib/ttf` refuses a glyph
of more than 256 points or of more than 64 contours, and a face that would be refused has to say so
here and not on the glass—.
"""

import argparse
import io
import logging
import os
import sys

try:
    from fontTools import subset
    from fontTools.pens.recordingPen import DecomposingRecordingPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import TTFont
except ImportError:  # pragma: no cover - the message is the point
    sys.stderr.write("fonttools is not installed: pip install fonttools\n")
    sys.exit(2)

# **The three sets.** `latin1` is the one a book needs; `ascii` is the one the system's own faces were, kept
# here so that one command makes either and the two are the same road; `signs` is the ascii plus the marks a
# page of this system writes that are not letters —the plus-minus, the times and the divide of a calculator's
# keypad, and the arrows with the backspace—, which is what a face of the system has to carry for an app to be
# able to paint the sign of a key instead of a word. The paper project's faces carried them for the same
# reason: `tools/fontgen.py` of PaperOS cooked `0x2190` to `0x2193` with the letters (docs/30, docs/92 §34).
ASCII = "U+0020-007E"
LATIN1 = "U+0020-00FF,U+2013,U+2014,U+2018-201D,U+2026,U+2022,U+00A0"
SIGNS = ASCII + ",U+00B1,U+00D7,U+00F7,U+2190-2193,U+232B"

# What `lib/ttf` accepts, and the numbers are the reader's: a glyph of more points or contours than
# this is refused with `kBadData` and the page would lose it.
MAX_POINTS = 256
MAX_CONTOURS = 64


def flatten(font):
    """Every glyph made of pieces becomes the contours of its pieces."""
    glyf = font["glyf"]
    glyph_set = font.getGlyphSet()

    pieces = [name for name in font.getGlyphOrder() if glyf[name].isComposite()]

    for name in pieces:
        recorded = DecomposingRecordingPen(glyph_set)
        glyf[name].draw(recorded, glyf)
        pen = TTGlyphPen(None)
        recorded.replay(pen)
        glyf[name] = pen.glyph()

    return len(pieces)


def measure(font):
    """The biggest glyph of the file, in the two numbers the reader limits."""
    glyf = font["glyf"]
    points = 0
    contours = 0

    for name in font.getGlyphOrder():
        glyph = glyf[name]
        if glyph.numberOfContours < 0:
            return None  # still made of pieces: the reader would refuse it
        if glyph.numberOfContours > 0:
            points = max(points, len(glyph.coordinates))
            contours = max(contours, glyph.numberOfContours)

    return points, contours


def main():
    parser = argparse.ArgumentParser(description="A Latin-1 face with its pieces flattened")
    parser.add_argument("--ttf", required=True, help="the font to read")
    parser.add_argument("--out", help="where the face is written (not needed with --check)")
    parser.add_argument("--set", choices=("latin1", "ascii", "signs"), default="latin1",
                        help="which characters the face keeps (default: latin1)")
    parser.add_argument("--check", action="store_true",
                        help="do not write: say whether the file could be made")
    args = parser.parse_args()

    if not args.check and not args.out:
        parser.error("--out is needed to write the face")

    # 1. The subset, with the same recipe the project already uses for the faces it ships.
    kept = {"latin1": LATIN1, "ascii": ASCII, "signs": SIGNS}[args.set]

    # The subsetter says out loud which tables it does not know how to cut —a table of a timestamp,
    # in this font—, and that is not this tool's business: the face is tried on the glass.
    logging.getLogger("fontTools.subset").setLevel(logging.ERROR)

    # **The same font in gives the same face out**: the timestamp of the `head` table would make
    # every run of this tool a different file, and a file that changes without changing is a file
    # nobody can check.
    font = TTFont(args.ttf, recalcTimestamp=False)
    font["head"].modified = 0
    options = subset.Options()
    options.drop_tables = []
    options.layout_features = []
    # **The hints go.** `lib/ttf` reads the contours and never runs an instruction of a glyph, so
    # what the hints would say is a third of the file and none of it would be read.
    options.hinting = False
    options.notdef_outline = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=subset.parse_unicodes(kept))
    subsetter.subset(font)

    pieces = flatten(font)

    after = measure(font)
    if after is None:
        sys.stderr.write("a glyph is still made of pieces after the flattening\n")
        return 1

    points, contours = after
    if points > MAX_POINTS or contours > MAX_CONTOURS:
        sys.stderr.write("a glyph of %d points and %d contours does not fit the reader\n" % (points, contours))
        return 1

    if args.check:
        print("%s: %d glyphs, %d of them made of pieces, %d points and %d contours at most"
              % (os.path.basename(args.ttf), len(font.getGlyphOrder()), pieces, points, contours))
        return 0

    buffer = io.BytesIO()
    font.save(buffer)
    data = buffer.getvalue()

    with open(args.out, "wb") as handle:
        handle.write(data)

    print("%s: %d glyphs, %d of them made of pieces, %d points and %d contours at most, %d bytes"
          % (args.out, len(font.getGlyphOrder()), pieces, points, contours, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
