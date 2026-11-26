#!/usr/bin/env python3
"""The icon of the Handwrite app: a letter drawn by hand over its writing line.

The owner asked for the keyboard that writes what the hand draws (*«Has de crear un nou teclat per software: "Handwrite" … L'usuari
fa el traç lletra a lletra i el teclat va donant el caràcter»*), and the icon says the same as the page: **full amb una línia
d'escriure i una lletra a sobre**, in the greys of the rest of this system's icons —the ink of the glass for the frame and the
letter, the paper for the rest.

The letter is an `a` and not another one: it is the first of the alphabet with a bowl and a stem, so it reads as a letter and not as
a stain. At ninety-four pixels a wobbly stroke is a blob, so the drawing is clean and what makes it a hand is the proportion —a
bowl a little taller than round, and a stem that goes down past it— not a trembling line. An earlier design put the letter inside
the seven-by-seven grid of the model; a grid of seven cells is a bush at this size and the letter stopped being read, so the grid
went away.

    python3 tools/make_handwrite_icon.py            # el mestre de 448 a `apps/handwrite/res/icon-source.png`
    python3 tools/make_icon.py --png apps/handwrite/res/icon-source.png \\
                               --out apps/handwrite/res/icon.png

Needs Pillow, like every tool of the icons here.
"""

import argparse
import os

from PIL import Image, ImageDraw

INK = 31
PAPER = 255

SUPERSAMPLE = 4


def draw(size):
    unit = size * SUPERSAMPLE
    image = Image.new("L", (unit, unit), PAPER)
    pen = ImageDraw.Draw(image)

    # The sheet: a card with rounded corners and an ink border, which is the silhouette of every icon here.
    margin = unit * 9 // 100
    card = [margin, margin, unit - margin, unit - margin]
    radius = unit * 7 // 100
    pen.rounded_rectangle(card, radius=radius, outline=INK, width=max(2, unit * 4 // 100))
    inside = [margin + unit * 4 // 100, margin + unit * 4 // 100, unit - margin - unit * 4 // 100,
              unit - margin - unit * 4 // 100]
    pen.rounded_rectangle(inside, radius=radius // 2, fill=PAPER)

    # The line the person writes on, which is what makes this a place where someone writes and not an empty card (the owner:
    # *«Posa la línia per escriure centrat»*).
    line_y = inside[3] - unit * 6 // 100
    line_left = inside[0] + unit * 6 // 100
    line_right = inside[2] - unit * 6 // 100
    pen.line([(line_left, line_y), (line_right, line_y)], fill=INK, width=max(2, unit * 4 // 100))

    # The letter: a bowl that sits on the line and a stem on its right, in the ink of the frame. **It sits on the line and not
    # under it**: a stem that goes past the line is read as a `q`, and this icon has to be the `a` the keyboard writes.
    width = max(3, unit * 7 // 100)
    ry = unit * 22 // 100
    rx = unit * 17 // 100
    cx = (inside[0] + inside[2]) / 2.0 - unit * 4 // 100
    cy = line_y - ry - unit * 4 // 100
    pen.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], outline=INK, width=width)
    pen.line([(cx + rx, cy - ry), (cx + rx, line_y - unit * 4 // 100)], fill=INK, width=width)

    return image


def main():
    parser = argparse.ArgumentParser(description="The icon of the Handwrite app.")
    parser.add_argument("--out", default=os.path.join("apps", "handwrite", "res", "icon-source.png"))
    parser.add_argument("--size", type=int, default=448)
    args = parser.parse_args()

    image = draw(args.size)
    image = image.resize((args.size, args.size), Image.LANCZOS)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    image.save(args.out)
    print(f"{args.out}: {args.size}x{args.size}")


if __name__ == "__main__":
    main()
