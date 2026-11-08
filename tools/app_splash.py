#!/usr/bin/env python3
# =============================================================================
#  lunokOS :: tools/app_splash.py
# =============================================================================
#  **The splash of an app, drawn out of its own icon**: the picture the system
#  puts on the glass, clean and in the middle, while the app is starting.
#
#  WHY IT EXISTS
#  -------------
#  The splash is a file of the app's folder that the manifest names
#  (`"splash": "res/splash.png"`), and an app that brings a hand-drawn one is an
#  app nobody can regenerate. What is nice in a listing is the icon, so the
#  splash is made of the icon and NOT of a frame with three lines of text in it.
#
#  It does NOT have to be the size of the glass: the system cleans the paper and
#  centres it, so a card in the middle of the screen is what it is meant to be
#  (docs/40).
#
#  **This is PaperOS's `tools/splashgen.py`, brought over with the reader**
#  (0.151.17): the icon and the splash of the EPUB app of the paper project are
#  the same artwork, and the recipe that composed them is this file. The only
#  change to the drawing is the foot, which is the name of the system it runs
#  on; `--foot PaperOS` gives the original back, byte for byte.
#
#  Use:
#     python3 tools/app_splash.py --icon apps/reader/res/icon.png --title EPUB \
#         --foot lunokOS --out apps/reader/res/splash.png
# =============================================================================

import argparse
import io

from PIL import Image, ImageDraw, ImageFont

SERIF_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
SERIF = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
SANS_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

W, H = 420, 520          # the card, not the glass
ICON = 232               # how much room the icon gets


def spaced(text, gap=" "):
    """`l o a d i n g . . .`: on e-ink a spaced line reads as a label."""
    return gap.join(list(text))


def draw_splash(icon_path, title, loading, foot):
    img = Image.new("L", (W, H), 255)

    if icon_path:
        icon = Image.open(icon_path).convert("L")

        if icon.size != (ICON, ICON):
            icon = icon.resize((ICON, ICON), Image.LANCZOS)

        img.paste(icon, ((W - ICON) // 2, 46))

        d = ImageDraw.Draw(img)
        d.line([(W - ICON) // 2 + 24, 46 + ICON + 26,
                (W + ICON) // 2 - 24, 46 + ICON + 26], fill=140, width=2)

    d = ImageDraw.Draw(img)

    big = ImageFont.truetype(SERIF_BOLD, 46)
    tw = d.textlength(title, font=big)
    d.text(((W - tw) / 2, 46 + ICON + 46), title, font=big, fill=0)

    if loading:
        small = ImageFont.truetype(SANS_BOLD, 17)
        text = spaced(loading)
        lw = d.textlength(text, font=small)
        d.text(((W - lw) / 2, 46 + ICON + 122), text, font=small, fill=90)

    if foot:
        tiny = ImageFont.truetype(SERIF, 13)
        fw = d.textlength(foot, font=tiny)
        d.text(((W - fw) / 2, H - 40), foot, font=tiny, fill=150)

    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--icon", default="", help="the PNG the icon came from")
    ap.add_argument("--title", required=True)
    ap.add_argument("--loading", default="loading...")
    ap.add_argument("--foot", default="PaperOS")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    img = draw_splash(args.icon, args.title, args.loading, args.foot)

    out = io.BytesIO()
    img.save(out, format="PNG", optimize=True)
    data = out.getvalue()

    with open(args.out, "wb") as f:
        f.write(data)

    print("%s  %d x %d  %d B" % (args.out, W, H, len(data)))


if __name__ == "__main__":
    main()
