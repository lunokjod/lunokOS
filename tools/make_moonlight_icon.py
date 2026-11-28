#!/usr/bin/env python3
# =============================================================================
#  lunokOS :: tools/make_moonlight_icon.py
# =============================================================================
#  **El dibuix de la icona de la botiga, i cada pas que hi va portar** (0.200.0, l'amo: *«Guarda el master i si pot
#  ser els passos que has fet i totes les versions que hem fet a res/»*).
#
#  The icon is drawn here and not in an editor so that the recipe survives: each step of the conversation is a function
#  and each one writes its master into `apps/moonlight/res/icon/`, numbered in the order it was tried. The last one is
#  the drawing the app ships, and it is also written to `apps/moonlight/res/icon-source.png`, which is what
#  `tools/make_icon.py` turns into the 94 x 94 `res/icon.png` the manifest names.
#
#  The greys are the ones of every drawing of this project, measured on its masters (docs/45):
#
#      68   l'òxid de la tinta (el to dels contorns i del cos)
#     170   l'ombra
#     221   el gris clar dels farciments
#     255   el paper
#
#  Usage:
#     tools/make_moonlight_icon.py                     # every step and the master
#     tools/make_moonlight_icon.py --only final        # one of them
#     python3 tools/make_icon.py --png apps/moonlight/res/icon-source.png --out apps/moonlight/res/icon.png
#
#  Needs Pillow, like `tools/make_icon.py`.
# =============================================================================

import argparse
import math
import os
import sys

try:
    from PIL import Image, ImageChops, ImageDraw
except ImportError:
    sys.exit("Pillow is needed:  pip install Pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTO = os.path.join(ROOT, "apps/moonlight/res/icon")
MASTER = os.path.join(ROOT, "apps/moonlight/res/icon-source.png")
CLOUD = os.path.join(INTO, "0-nuvol.png")

SIZE = 448
K = 4                     # the drawing is done four times the size and comes down with LANCZOS
W = SIZE * K

INK = (68, 68, 68, 255)
SHADE = (170, 170, 170, 255)
LIGHT = (221, 221, 221, 255)
PAPER = (255, 255, 255, 255)
NONE = (255, 255, 255, 0)

TURN = 30                 # el gir de la lluna que l'amo va triar (*«tombada cap a l'altre banda»*)


def solid(colour):
    return Image.new("RGBA", (W, W), colour)


def mask():
    return Image.new("L", (W, W), 0)


def disc(m, cx, cy, r, fill=255):
    if r <= 0:
        return m
    ImageDraw.Draw(m).ellipse([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], fill=fill)
    return m


def star(cx, cy, outer, inner, points=5, turn=-math.pi / 2):
    out = []
    for i in range(points * 2):
        rad = outer if i % 2 == 0 else inner
        angle = turn + i * math.pi / points
        out.append(((cx + rad * math.cos(angle)) * K, (cy + rad * math.sin(angle)) * K))
    return out


def sparkle(cx, cy, outer, waist):
    """A four-pointed one: two crossing points with concave sides, which is the brightest thing in a night."""
    pts = [(cx, cy - outer), (cx + waist * 0.72, cy - waist * 0.72), (cx + outer, cy),
           (cx + waist * 0.72, cy + waist * 0.72), (cx, cy + outer),
           (cx - waist * 0.72, cy + waist * 0.72), (cx - outer, cy),
           (cx - waist * 0.72, cy - waist * 0.72)]
    return [(x * K, y * K) for x, y in pts]


def crescent(cx, cy, r, bite_off, bite_r):
    """The body of the moon: a disc with a second disc taken out of its side. The bite is what makes the horns."""
    body = mask()
    disc(body, cx, cy, r)
    disc(body, cx + bite_off, cy, bite_r, 0)
    return body


def two_stars(img):
    """**Els estels de la versió que es queda** (la que l'amo va dir bona): a five-pointed one with a lit face, and the
    four-pointed sparkle under it. The positions are part of the drawing and are not a parameter: they were moved three
    times until the moon was turned and the sky fitted."""
    draw = ImageDraw.Draw(img)
    draw.polygon(star(360, 92, 50, 21), fill=INK)
    draw.polygon(star(360, 92, 28, 12), fill=SHADE)
    draw.polygon(sparkle(360, 176, 24, 7), fill=INK)


def black_moon(holes):
    """**El primer dibuix** (l'amo: *«l'icona es massa básica»*). Amb `holes`, els cràters foradats, que és el que
    l'amo va rebutjar: *«No a que foradessis el dibuix xD»*."""
    img = Image.new("RGBA", (W, W), NONE)
    body = crescent(215, 255, 150, 105, 140)
    img.paste(solid(INK), (0, 0), body)
    if holes:
        craters = ImageDraw.Draw(img)
        for x, y, r in [(140, 258, 27), (116, 196, 16), (124, 320, 14), (170, 210, 10)]:
            craters.ellipse([(x - r) * K, (y - r) * K, (x + r) * K, (y + r) * K], fill=NONE)
    draw = ImageDraw.Draw(img)
    draw.polygon(star(352, 128, 62, 17), fill=INK)
    draw.polygon(star(370, 262, 32, 13), fill=INK)
    draw.polygon(sparkle(268, 356, 20, 6), fill=INK)
    return img


def metallic_moon(turn):
    """**La lluna metàl·lica** (l'amo: *«la que sembla metàl·lica anterior era millor, però l'has de tombar una mica»*):
    a light body, a dark rim painted as an inner stroke, two steps of shade on the side the light does not reach, and
    the craters in grey with their own shadow, which is what gives a flat drawing volume without one hole."""
    cx, cy, r, off, bite_r = 228, 246, 150, 105, 140
    bx = cx + off
    moon = Image.new("RGBA", (W, W), NONE)
    body = crescent(cx, cy, r, off, bite_r)
    moon.paste(solid(LIGHT), (0, 0), body)

    shade = mask()
    disc(shade, bx, cy, bite_r + 46)
    moon.paste(solid(SHADE), (0, 0), ImageChops.multiply(shade, body))
    deep = mask()
    disc(deep, bx, cy, bite_r + 18)
    moon.paste(solid(INK), (0, 0), ImageChops.multiply(deep, body))

    outer_ring = mask()
    disc(outer_ring, cx, cy, r)
    disc(outer_ring, cx, cy, r - 7, 0)
    inner_ring = mask()
    disc(inner_ring, bx, cy, bite_r + 7)
    disc(inner_ring, bx, cy, bite_r, 0)
    moon.paste(solid(INK), (0, 0), ImageChops.multiply(ImageChops.lighter(outer_ring, inner_ring), body))

    craters = Image.new("RGBA", (W, W), NONE)
    draw = ImageDraw.Draw(craters)
    for x, y, rr in [(cx - 75, cy + 3, 27), (cx - 54, cy - 62, 16), (cx - 50, cy + 65, 14),
                     (cx - 22, cy - 104, 10), (cx - 108, cy - 26, 9)]:
        draw.ellipse([(x + 3 - rr) * K, (y + 3 - rr) * K, (x + 3 + rr) * K, (y + 3 + rr) * K], fill=INK)
        draw.ellipse([(x - rr + 3) * K, (y - rr + 3) * K, (x + rr - 3) * K, (y + rr - 3) * K], fill=SHADE)
    moon.paste(craters, (0, 0), ImageChops.multiply(craters.getchannel("A"), body))

    return moon.rotate(turn, resample=Image.BICUBIC, center=(cx * K, cy * K), fillcolor=NONE)


def a_cloud(img, cx, cy, wide, tall, rim=8):
    """A cumulus of six lobes with a flat base, a dark rim and two bands of shade underneath: the shape asked for with
    *«una mica mes de gracia»*."""
    lobes = [(cx - wide * 0.36, cy + tall * 0.06, tall * 0.32),
             (cx - wide * 0.20, cy - tall * 0.14, tall * 0.44),
             (cx + wide * 0.00, cy - tall * 0.26, tall * 0.50),
             (cx + wide * 0.20, cy - tall * 0.06, tall * 0.42),
             (cx + wide * 0.36, cy + tall * 0.10, tall * 0.30),
             (cx + wide * 0.46, cy + tall * 0.20, tall * 0.18)]
    full, inner = mask(), mask()
    for x, y, r in lobes:
        disc(full, x, y, r)
        disc(inner, x, y, r - rim)
    left, right = cx - wide * 0.50, cx + wide * 0.52
    base = cy + tall * 0.36
    ImageDraw.Draw(full).rectangle([left * K, (cy + tall * 0.02) * K, right * K, base * K], fill=255)
    ImageDraw.Draw(inner).rectangle([(left + rim) * K, (cy + tall * 0.02) * K,
                                     (right - rim) * K, (base - rim) * K], fill=255)

    img.paste(solid(PAPER), (0, 0), full)
    for level, at in ((LIGHT, 0.12), (SHADE, 0.24)):
        band = mask()
        ImageDraw.Draw(band).rectangle([0, int((cy + tall * at) * K), W, W], fill=255)
        img.paste(solid(level), (0, 0), ImageChops.multiply(ImageChops.multiply(band, full), inner))
    img.paste(solid(INK), (0, 0), ImageChops.subtract(full, inner))


def with_clouds(img, big=(236, 348, 356, 128, 8), small=(396, 246, 128, 52, 7)):
    a_cloud(img, *big)
    a_cloud(img, *small)
    return img


def the_owner_cloud(moon):
    """**El núvol que l'amo va portar** (`0-nuvol.png`, 220 x 171 i amb alfa): the try that made him say *«aquestes dues
    no m'agraden»*, kept because a step that was refused is part of the recipe."""
    if not os.path.isfile(CLOUD):
        return None
    cloud = Image.open(CLOUD).convert("RGBA")
    grey = cloud.convert("L").convert("RGBA")
    grey.putalpha(cloud.getchannel("A"))
    img = Image.new("RGBA", (W, W), NONE)
    img.alpha_composite(moon)
    for cx, cy, wide in [(238, 352, 250), (352, 268, 120)]:
        tall = int(wide * cloud.size[1] / cloud.size[0])
        pic = grey.resize((wide * K, tall * K), Image.LANCZOS)
        img.alpha_composite(pic, (int((cx - wide / 2) * K), int((cy - tall / 2) * K)))
    return img


def steps():
    """Each step of the drawing, in the order the owner saw it."""
    out = []
    out.append(("1-negre", "el primer dibuix: la lluna i tres estels, tot en tinta", black_moon(False)))
    out.append(("2-forats", "els cràters foradats (rebutjat: «No a que foradessis el dibuix»)", black_moon(True)))

    grey = Image.new("RGBA", (W, W), NONE)
    grey.alpha_composite(metallic_moon(0))
    two_stars(grey)
    out.append(("3-gris", "la lluna metàl·lica: grisos i ombres, els cràters pintats", grey))

    from_owner = the_owner_cloud(metallic_moon(0))
    if from_owner is not None:
        two_stars(from_owner)
        out.append(("4-nuvol-de-lamo", "el núvol de l'amo, tal com el va portar (rebutjat: «no m'agraden»)", from_owner))

    drawn = with_clouds(Image.new("RGBA", (W, W), NONE).copy())
    base = metallic_moon(0)
    drawn.alpha_composite(base)
    two_stars(drawn)
    out.append(("5-nuvols-dibuixats", "els núvols dibuixats amb els grisos de casa", drawn))

    black_clouds = Image.new("RGBA", (W, W), NONE)
    black_clouds.alpha_composite(black_moon(False))
    with_clouds(black_clouds)
    two_stars(black_clouds)
    out.append(("6-negre-amb-nuvols", "la lluna negra amb els núvols a sobre (rebutjat)", black_clouds))

    turned = Image.new("RGBA", (W, W), NONE)
    turned.alpha_composite(metallic_moon(TURN))
    two_stars(turned)
    out.append(("7-tombada", "la metàl·lica girada %d graus («tombada cap a l'altre banda»)" % TURN, turned))

    final = Image.new("RGBA", (W, W), NONE)
    final.alpha_composite(metallic_moon(TURN))
    with_clouds(final)
    two_stars(final)
    out.append(("8-final", "la que es queda: la metàl·lica tombada i els núvols davant", final))
    return out


def main():
    parser = argparse.ArgumentParser(description="The drawing of the Moonlight icon and every step of it.")
    parser.add_argument("--only", help="one step by name (1-negre, 8-final, ...)")
    args = parser.parse_args()

    os.makedirs(INTO, exist_ok=True)
    for name, what, img in steps():
        if args.only is not None and args.only != name:
            continue
        # **El mestre va a 448 i els passos a 224** (0.200.0): el mestre és el que una persona obre en un editor i el que
        # `tools/make_icon.py` baixa a 94, i els passos són un record que viatja a la placa amb l'app —dos canals en lloc
        # de quatre i la meitat de píxels, que és un quart dels bytes— (docs/45, docs/70).
        full = img.resize((SIZE, SIZE), Image.LANCZOS)
        full.convert("LA").save(os.path.join(INTO, name + ".png"))
        print("%s.png  %s" % (name, what))
        if name == "8-final":
            full.save(MASTER)  # el mestre es queda amb alfa de color, que és el que llegeix `make_icon.py`
            print("master: apps/moonlight/res/icon-source.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
