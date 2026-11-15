#!/usr/bin/env python3
"""La imatge del moment `sleep`: **un os bru que dorm**, i el fons de quadres que se li ha de treure.

The owner brought the drawing as a WebP **without an alpha channel but with the checkerboard drawn into its
pixels** —the grey and white squares that a PNG viewer shows as "transparent" and that a photograph of a
file does not have—: *«te un fons de falsa transparencia, fa la senefa de blanc i gris, com si fos un png i
no… m'han enganyat»*. An e-ink glass has no transparency at all: what the system paints is the picture in the
middle of a page, and the page is the colour of the picture's own border (`splash::background_of`,
docs/50, docs/75). So "transparent" here means **the drawing and nothing else**, on the paper the page will
be washed with, and that is what this script writes.

How the two squares go away, and why a flood fill and not a colour key:

- the two greys of the board are 255 and 239, so `ImageDraw.floodfill` from a corner with a tolerance of 20
  takes both and stops at the bear —whose browns are nowhere near white— in one call;
- **the air of the edge is eaten by one pixel** (`MinFilter(3)` over the filled area): the drawing was
  antialiased against the grey squares, and a pixel that is half bear and half 239 grey reads as a grey
  fringe on white paper. One pixel of a 900-pixel drawing is half of one when the picture is scaled to the
  glass;
- **and the watermark line of the source is cropped with the drawing**: the file carries `by cleanpng.com`
  under the bear's grass, which is not part of the drawing.

Usage: `python3 tools/make_bear_sleep.py`, which reads `tools/splash-sources/bear-sleeping-clipart-14.webp`
and writes `data/res/splash/sleep.png`. **The size is the size a person sees**: the system paints the picture
without scaling it, so the drawing is scaled here to the width of the page (docs/50).
"""

from __future__ import annotations

import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "splash-sources", "bear-sleeping-clipart-14.webp")
OUT = os.path.join(HERE, "..", "data", "res", "splash", "sleep.png")

PAPER = 255
# **Com de gran el dibuix al vidre** (the glass of the Paper is 540 x 960 and the picture is centred): 500 leaves twenty
# pixels of paper on each side, which is the air of a picture that is the page —the other moments are words on 460.
WIDTH = 500
# The two greys of the fake transparency, and how far a pixel can be from the paper and still be the board.
# **`floodfill` of PIL measures the distance of an RGB pixel as the sum of its three bands**, and the two squares of
# the board are 255 and 239: forty-eight apart, and not twenty. Brown is two hundred away, so a threshold of sixty
# takes the board whole and stops dead at the drawing.
TOLERANCE = 60
# **El peu de la imatge no és el dibuix**: under the bear's grass the file carries `by cleanpng.com` in dark grey,
# which the fill does not take (it is not the board) and which would come out as ink at the bottom of the picture.
# The bear's last row of ink is 819 and the line starts at 912, so the source is cut at 880.
SOURCE_BOTTOM = 880
# The air of paper kept around the drawing, so the border ring the kernel reads is paper and the page is white
# (`splash::background_of` decides the page from the ring of the picture, docs/50).
MARGIN = 24


def the_drawing() -> Image.Image:
    """The bear, and the paper under it, with the checkerboard gone."""
    source = Image.open(SOURCE).convert("RGB")
    source = source.crop((0, 0, source.width, min(SOURCE_BOTTOM, source.height)))

    # The board, as one region: from a corner, and through both of its greys. A pixel the fill does not reach is
    # the drawing — the fur, the muzzle, the grass — because a bear is not within twenty of white in any band.
    keyed = source.copy()
    ImageDraw.floodfill(keyed, (0, 0), (0, 0, 0), thresh=TOLERANCE)
    filled = keyed.convert("L").point(lambda v: 255 if v < 8 else 0)  # 255 where the board was

    # **One pixel of the air of the edge goes with the board**: the drawing was antialiased against the grey squares.
    board = filled.filter(ImageFilter.MaxFilter(3))
    alpha = board.point(lambda v: 0 if v > 127 else 255)

    drawing = source.convert("RGBA")
    drawing.putalpha(alpha)
    box = alpha.getbbox()
    if box is not None:
        drawing = drawing.crop(box)

    page = Image.new("RGBA", (drawing.width + 2 * MARGIN, drawing.height + 2 * MARGIN), (PAPER, PAPER, PAPER, 255))
    page.paste(drawing, (MARGIN, MARGIN), drawing)
    grey = page.convert("L")
    if grey.width > WIDTH:
        height = round(grey.height * WIDTH / grey.width)
        grey = grey.resize((WIDTH, height), Image.LANCZOS)
    return grey


def main() -> int:
    picture = the_drawing()
    picture.save(OUT, "PNG", optimize=True)
    ink = sum(1 for v in picture.tobytes() if v < 250)
    print(f"{OUT} {picture.width}x{picture.height}, {ink} pixel(s) of ink")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
