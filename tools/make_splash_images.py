#!/usr/bin/env python3
"""Draws the pictures of the splash moments of the system (docs/50, docs/92).

A moment looks for `/res/splash/<word>.png` (and `.bmp`, `.jpg`), the system
reads the file with `lib/img`, washes the whole glass and paints the picture in
the middle **without scaling it**, so the size written here is the size a person
sees. Every picture of this script is 460 x 200 and greyscale, which is what the
other moments of the disk are.

The logo is a moon with the name on it: **"lunok" over the disc in ink with a
paper edge**, because the disc is brighter than the paper and ink reads on it,
and **"OS" outside it in paper with an ink edge**, because a paper letter on
paper needs the edge to be a letter. The same wordmark, read two ways, and that
is the whole idea of the mark.

**The two logos this script used to write are gone** (0.178.5, the owner: *«boot-alt.png i
boot.png no han d'estar a la llista del boot»*): the picture of the boot is not one drawing
but the drawings the disk carries —`boot-2.bmp`, `boot-3.bmp`, `boot-5.bmp`, which are
pages of a wireframe and not this script's work— and one of them is drawn at random on
every switch-on (`splash::picture_of`, docs/30, docs/50). The two PNGs were the Moon
photograph of NASA's `as11-44-6667` and the same moon as clipart, and they are in the
history of the project if they are wanted again: `git show 9dfa038:data/res/splash/boot.png`.

`reboot.png` is in the plain style of `poweroff.png` and `charging.png` —paper,
the word, a rule and the line under it— and it is written here so all the
pictures of this script are made by one command.

Usage: `python3 tools/make_splash_images.py`, which writes into `data/res/splash`.
"""

from __future__ import annotations

import os
import sys
import urllib.request

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "data", "res", "splash")

WIDTH = 460
HEIGHT = 200
# **The face of the system is a bold sans** (docs/92): the splash of the moments
# has always been drawn with it, and the mark uses the same one so the logo and
# the words under it are one hand.
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

# The mark is one word with two inks: the half that is over the moon and the half
# that is not.
INK = 0
PAPER = 255
# **How thick the edge of a letter is** (in pixels): the letters of the mark are
# a stem of nine or ten pixels at this size, and an edge of three leaves a core
# of three or four, which is the least that still reads as paper on paper. An
# edge of four eats the letter and it comes out solid ink.
EDGE = 3
# The radius of the disc, in the pixels of the picture: the mark is 200 tall and
# the moon takes most of it, because it is the picture and not a decoration.
RADIUS = 88.0

# **The photograph**, and where it is kept: `~/.cache` and not the repository,
# because it is a hundred and sixty kilobytes of somebody else's picture and what
# this project carries is the ten kilobytes of the mark it made out of it
# (docs/50).
PHOTO_URL = "https://images-assets.nasa.gov/image/as11-44-6667/as11-44-6667~medium.jpg"
PHOTO_CACHE = os.path.join(os.path.expanduser("~"), ".cache", "lunokOS",
                           "moon-as11-44-6667.jpg")


def font(path: str, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(path, size)


def text_width(draw: ImageDraw.ImageDraw, text: str, face: ImageFont.FreeTypeFont) -> float:
    return draw.textlength(text, font=face)


def wordmark(draw: ImageDraw.ImageDraw, face: ImageFont.FreeTypeFont, centre_y: int,
             right_of_lunok: int) -> None:
    """Writes "lunok" and "OS" as one word: the join is `right_of_lunok`.

    The letters are the same face and the same baseline, so the two halves line
    up; what changes is the ink, which is the idea of the mark.
    """
    w_lunok = text_width(draw, "lunok", face)
    ascent, descent = face.getmetrics()
    baseline = centre_y + (ascent - descent) // 2
    x0 = right_of_lunok - w_lunok
    # **Ink with a paper edge over the moon**: the disc is a bright grey and the
    # white edge is what keeps the letter a letter where a crater is dark.
    draw.text((x0, baseline), "lunok", font=face, fill=INK, anchor="ls",
              stroke_width=EDGE, stroke_fill=PAPER)
    # **Paper with an ink edge outside it**: white on paper is nothing without
    # the edge, and the edge is what the eye reads as the letter.
    draw.text((right_of_lunok, baseline), "OS", font=face, fill=PAPER, anchor="ls",
              stroke_width=EDGE, stroke_fill=INK)


def fits_the_disc(draw: ImageDraw.ImageDraw, face: ImageFont.FreeTypeFont, radius: int) -> bool:
    """Whether "lunok" fits inside the disc at the height it is written.

    A chord of a circle is what a line of letters has to live in: the letters are
    `height` tall and the disc is `radius`, so the room at the top and the bottom
    of that band is not the diameter but `2 * sqrt(r^2 - (h / 2)^2)`.
    """
    ascent, descent = face.getmetrics()
    half = (ascent + descent) / 2.0
    if half >= radius:
        return False
    chord = 2.0 * np.sqrt(radius * radius - half * half)
    return text_width(draw, "lunok", face) + 8.0 <= chord


def photo(refresh: bool = False) -> Image.Image:
    """The Moon of the Apollo 11 photograph, cropped to its disc.

    The photograph is a full Moon in a black sky. **The sky is not the picture**:
    a circle cut to the brightness of the disc is what is put on paper, so that
    the splash is a moon on the paper of this system and not a black square in
    the middle of the glass, which is what a picture with its own background
    would be (docs/50, docs/92).
    """
    if refresh or not os.path.exists(PHOTO_CACHE):
        os.makedirs(os.path.dirname(PHOTO_CACHE), exist_ok=True)
        print(f"fetching {PHOTO_URL}")
        with urllib.request.urlopen(PHOTO_URL, timeout=60) as answer:
            data = answer.read()
        with open(PHOTO_CACHE, "wb") as out:
            out.write(data)
    moon = Image.open(PHOTO_CACHE).convert("L")
    levels = np.asarray(moon).astype(np.float64)
    lit = levels > 40.0  # the sky of the photograph is black and the Moon is not
    ys, xs = np.nonzero(lit)
    if len(xs) == 0:
        raise SystemExit("the photograph has no moon in it")
    left, right = int(xs.min()), int(xs.max())
    top, bottom = int(ys.min()), int(ys.max())
    centre_x = (left + right) / 2.0
    centre_y = (top + bottom) / 2.0
    radius = max(right - left, bottom - top) / 2.0
    disc = moon.crop((int(centre_x - radius), int(centre_y - radius),
                      int(centre_x + radius), int(centre_y + radius)))
    # **The tone of a scan is not the tone of a glass**: the maria are dark and
    # the highlands are bright, and what the panel wants is that difference and
    # nothing of the tone of a film.
    return ImageOps.autocontrast(disc, cutoff=1)


def circle(size: int, inset: float = 1.5) -> Image.Image:
    """A soft circle to paste a disc with: four times the size and shrunk."""
    scale = 4
    mask = Image.new("L", (size * scale, size * scale), 0)
    edge = inset * scale
    ImageDraw.Draw(mask).ellipse((edge, edge, size * scale - 1 - edge, size * scale - 1 - edge),
                                 fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def clipart_moon(cx: float, cy: float, radius: float) -> Image.Image:
    """The same moon as a drawing: a flat disc, an outline, circles and stars."""
    picture = Image.new("L", (WIDTH, HEIGHT), PAPER)
    draw = ImageDraw.Draw(picture)
    box = (cx - radius, cy - radius, cx + radius, cy + radius)
    draw.ellipse(box, fill=236, outline=INK, width=6)
    # A crater is a circle with an edge: the whole of the clipart is this.
    # **The band the word goes through stays empty**: a crater under a letter is
    # a shape the eye reads as part of the letter, and the mark is the word.
    for ox, oy, oradius in ((-0.30, -0.52, 0.16), (-0.02, -0.62, 0.11), (-0.55, -0.42, 0.09),
                            (0.24, -0.50, 0.13), (0.48, -0.34, 0.08), (-0.18, 0.52, 0.15),
                            (0.14, 0.62, 0.10), (-0.50, 0.44, 0.11), (0.44, 0.46, 0.09)):
        kr = oradius * radius
        kx = cx + ox * radius
        ky = cy + oy * radius
        # **A crater of a drawing is a circle of a slightly darker tone with an
        # edge**: paper inside would be a hole in the moon.
        draw.ellipse((kx - kr, ky - kr, kx + kr, ky + kr), fill=214, outline=INK, width=4)
    # **The stars of a drawing**: four points and not a cross, which is what a
    # clipart sky has.
    for sx, sy, size in ((40, 44, 10), (58, 156, 7), (410, 62, 9), (430, 148, 6)):
        waist = size * 0.26
        points = [(sx, sy - size), (sx + waist, sy - waist), (sx + size, sy),
                  (sx + waist, sy + waist), (sx, sy + size), (sx - waist, sy + waist),
                  (sx - size, sy), (sx - waist, sy - waist)]
        draw.polygon(points, fill=INK)
    return picture


def logo(clipart: bool, refresh: bool = False) -> Image.Image:
    """The mark: the moon and the name written across it."""
    centre_y = HEIGHT // 2
    # The face is the largest one whose half of the word fits the disc: the
    # letters have to be inside the moon and out of its edge.
    measure = ImageDraw.Draw(Image.new("L", (WIDTH, HEIGHT), PAPER))
    size = 60
    while size > 24:
        face = font(FONT_BOLD, size)
        if fits_the_disc(measure, face, int(RADIUS)):
            break
        size -= 1
    face = font(FONT_BOLD, size)
    w_lunok = text_width(measure, "lunok", face)
    w_os = text_width(measure, "OS", face)
    total = w_lunok + w_os
    # The limb falls just after the "k", which is what puts "OS" outside the
    # moon: the join of the word is the edge of the disc, with a gap of nine
    # pixels so that the ink of the "k" and the edge of the disc are two things.
    right_of_lunok = (WIDTH - total) // 2 + int(round(w_lunok)) + 9
    centre_x = right_of_lunok - RADIUS
    if clipart:
        picture = clipart_moon(centre_x, centre_y, RADIUS)
    else:
        picture = Image.new("L", (WIDTH, HEIGHT), PAPER)
        diameter = int(RADIUS * 2)
        disc = photo(refresh).resize((diameter, diameter), Image.LANCZOS)
        picture.paste(disc, (int(centre_x - RADIUS), int(centre_y - RADIUS)), circle(diameter))
    draw = ImageDraw.Draw(picture)
    wordmark(draw, face, centre_y, right_of_lunok)
    return picture


def plain(title: str, subtitle: str) -> Image.Image:
    """The style of `poweroff.png` and `charging.png`: the word, a rule, a line."""
    picture = Image.new("L", (WIDTH, HEIGHT), PAPER)
    draw = ImageDraw.Draw(picture)
    big = font(FONT_BOLD, 54)
    small = font(FONT, 26)
    draw.text((WIDTH // 2, 58), title, font=big, fill=INK, anchor="ms")
    rule = 70
    draw.line((WIDTH // 2 - 130, 96, WIDTH // 2 + 130, 96), fill=INK, width=2)
    draw.text((WIDTH // 2, 96 + rule // 2 + 4), subtitle, font=small, fill=INK, anchor="ms")
    return picture


def save(picture: Image.Image, name: str) -> None:
    path = os.path.join(OUT, name)
    picture.convert("L").save(path, "PNG", optimize=True)
    print(f"{path} {picture.width}x{picture.height}")


def main(argv: list[str]) -> int:
    if not os.path.isdir(OUT):
        print(f"there is no {OUT}", file=sys.stderr)
        return 1
    # **`--refresh` is still accepted and does nothing**: it was the flag of the photograph of the Moon, and the boot
    # is drawn with the drawings of the disk now (0.178.5), so a script of the owner that still carries it keeps running.
    del argv
    save(plain("reboot", "restarting"), "reboot.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
