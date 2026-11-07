#!/usr/bin/env python3
# =============================================================================
#  lunokOS :: tools/make_icon.py
# =============================================================================
#  The icon of an app: from the drawing a person edits to the file the system
#  reads.
#
#  The drawings of the paper project live in `~/code/PaperOS/tools/icons/` —one
#  PNG of 448 x 448 per app, kept apart from the app on purpose so that it can
#  be edited— and the icon it ships is a `.icn` of 94 x 94 in four bits, the
#  format of *its* panel. This system does not need that format: `ui.image`
#  reads a PNG of any size, so the icon here **is a PNG**, written at 94 x 94 —
#  the box a cell of the paper's start menu gives its icon, which is the only
#  size that was ever measured— and that is what the manifest names.
#
#  The master travels beside it (`res/icon-source.png`): it is the file a person
#  opens in an editor, and this tool is the step between the two.
#
#  Usage:
#     tools/make_icon.py --png apps/calculator/res/icon-source.png \
#                        --out apps/calculator/res/icon.png
#     tools/make_icon.py --png drawing.png --preview
#
#  Needs Pillow, like the paper's `tools/icongen.py`, and it does the same three
#  things to the drawing (compose the transparency over paper, take the greys,
#  resize with LANCZOS), so the icon here and the one there come from the same
#  pixels.
# =============================================================================

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is needed:  pip install Pillow")

# **The size of an icon is the paper project's number** (94 x 94): the box a
# cell of its start menu gives an icon, measured so that five rows fit a page of
# its launcher. Here there is no launcher yet, and when there is one it will be
# this system's page and not the other one's —but a drawing that is already the
# right size for an icon costs 9 KB and not 200 KB of pixels— (docs/45).
SIZE = 94

# The background the drawing is composed over: the board paints on white paper,
# and a transparency taken as black would be a scrawl with a black frame nobody
# drew.
PAPER = 255


def load_grey(path, size):
    """The PNG, in greys and at the size of an icon."""
    img = Image.open(path)

    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        paper = Image.new("RGBA", img.size, (PAPER, PAPER, PAPER, 255))
        img = Image.alpha_composite(paper, img)

    img = img.convert("L")

    # The size is the tool's job and not the app's: scaling a drawing on the
    # board would cost code and memory for something that is done once, here.
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)

    return img


def preview(img, size):
    """A text sample of it, to look at it without a board."""
    ramp = " .:-=+*#%@"
    px = img.load()

    print("  +" + "-" * size + "+")
    for y in range(size):
        line = "  |"
        for x in range(size):
            level = px[x, y] * 15 // 255
            line += ramp[(15 - level) * (len(ramp) - 1) // 15]
        print(line + "|")
    print("  +" + "-" * size + "+")


def main():
    parser = argparse.ArgumentParser(description="A PNG to the icon of an app")
    parser.add_argument("--png", required=True, help="the drawing, in greys or in colour")
    parser.add_argument("--out", help="the PNG to write")
    parser.add_argument("--size", type=int, default=SIZE, help="square size (94 by default)")
    parser.add_argument("--preview", action="store_true", help="print it as text")
    args = parser.parse_args()

    if not os.path.exists(args.png):
        sys.exit("There is no such file: %s" % args.png)

    img = load_grey(args.png, args.size)

    if args.preview:
        preview(img, args.size)

    if args.out:
        img.save(args.out, "PNG", optimize=True)
        print("%s: %d x %d, %d bytes" % (args.out, args.size, args.size, os.path.getsize(args.out)))


main()
