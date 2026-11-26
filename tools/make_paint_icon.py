#!/usr/bin/env python3
"""El pinzell de l'app Paint, per a la seva icona.

L'amo el volia **el pinzell sol i amb estil** (*«l'icona es horrible, usa el pinzell sol, pero donali estil de les icones d'aquí,
que no quedi plana!»*): les icones de les apps d'aquest sistema no són una silueta d'una tinta —això és la marca d'una clau de la
targeta de la barra— sinó un dibuix amb **contorn fosc, dos o tres grisos i els detalls a dins** (el lector amb el llom i el
tancament, la calculadora amb la pantalla i les tecles). Aquest pinzell és el mateix joc de tons que aquells: el contorn del negre
del vidre, el mànec amb la meitat il·luminada, l'emplomallat amb els pèls separats i la pintura que en cau a la punta.

    python3 tools/make_paint_icon.py            # el mestre de 448 a `apps/paint/res/icon-source.png`
    python3 tools/make_icon.py --png apps/paint/res/icon-source.png \\
                               --out apps/paint/res/icon.png

Needs Pillow, like every tool of the icons here.
"""

import argparse
import math

from PIL import Image, ImageDraw

# **Els tons del joc de les icones d'aquest sistema** (docs/45, docs/92): the outline is the ink of the glass, the two greys a body
# and its light, and the paint is the darkest of the three so it reads as something that is not the brush.
INK = 31
DARK = 59
MID = 87
LIGHT = 156
PAPER = 255

SUPERSAMPLE = 4

# **Les proporcions, en centèsimes de la caixa del dibuix** (no s'inventen cada cop: són les d'un pinzell de pintor, el mànec és
# més llarg que l'emplomallat i la virolla és una banda curta entre els dos).
HANDLE_TOP = 4     # where the handle starts, from the top of the drawing
HANDLE_W = 15      # its width
FERRULE_TOP = 52   # where the collar starts
FERRULE_BOTTOM = 63
FERRULE_W = 21
BRISTLE_TOP = 63
BRISTLE_BOTTOM = 97
BRISTLE_W = 17


def draw(size):
    """El pinzell dret en un quadre de `size`, girat, amb el paper de fons."""
    unit = size * SUPERSAMPLE
    image = Image.new("L", (unit, unit), PAPER)
    pen = ImageDraw.Draw(image)

    # **El dibuix cap dins del quadre girat**: a 38 graus, el que surt de llarg és la diagonal, o sigui que el pinzell ocupa
    # setanta-dos per cent del quadre i la resta és l'aire que el gira sense tallar-lo.
    height = unit * 72 // 100
    top = (unit - height) // 2
    cx = unit // 2
    edge = max(1, unit // 180)
    y0 = top + height * HANDLE_TOP // 100
    y1 = top + height * FERRULE_TOP // 100
    fy1 = top + height * FERRULE_BOTTOM // 100
    by1 = top + height * BRISTLE_BOTTOM // 100
    hw = unit * HANDLE_W // 100
    fw = unit * FERRULE_W // 100
    bw = unit * BRISTLE_W // 100

    # **El mànec**, amb el cap rodó i la meitat del darrere en el gris fosc —que és el que li dona volum i el que fa que no sigui
    # una taca— i el forat de penjar-lo, que és el detall que diu «una eina» i no «un pal».
    pen.rounded_rectangle([cx - hw // 2, y0, cx + hw // 2, y1], radius=hw // 2, fill=LIGHT, outline=INK, width=edge)
    pen.rounded_rectangle([cx + hw // 14, y0 + hw // 2, cx + hw // 2 - edge, y1 - hw // 6], radius=hw // 3, fill=MID)
    hole_r = hw // 5
    pen.ellipse([cx - hole_r, y0 + hw // 2 - hole_r, cx + hole_r, y0 + hw // 2 + hole_r], fill=PAPER, outline=INK,
                width=max(1, unit // 220))

    # **La virolla**: a band with its two edges in the light grey and a rivet on the side, which is where the metal holds the wood.
    pen.rounded_rectangle([cx - fw // 2, y1, cx + fw // 2, fy1], radius=fw // 7, fill=MID, outline=INK, width=edge)
    for at in (0.3, 0.7):
        y = int(y1 + (fy1 - y1) * at)
        pen.line([cx - fw // 2 + edge, y, cx + fw // 2 - edge, y], fill=LIGHT, width=max(1, unit // 300))
    rivet = fw // 7
    pen.ellipse([cx - rivet, (y1 + fy1) // 2 - rivet, cx + rivet, (y1 + fy1) // 2 + rivet], fill=LIGHT, outline=INK,
                width=max(1, unit // 320))

    # **L'emplomallat**: estret a la virolla i rodó a la punta, i el cap de la punta és mig cercle centrat on s'acaben els dos
    # costats, o sigui que el contorn dels costats i el de la punta són una sola línia i no dues que es troben en un cantó (que és
    # el que dona un polígon sol, i es llegeix com una escombra).
    half_tip = bw * 34 // 100
    neck = by1 - half_tip
    pen.polygon([(cx - bw // 2, fy1), (cx + bw // 2, fy1), (cx + half_tip, neck), (cx - half_tip, neck)], fill=MID,
                outline=INK, width=edge)
    pen.ellipse([cx - half_tip, neck - half_tip, cx + half_tip, neck + half_tip], fill=MID, outline=INK, width=edge)
    for step in (-1, 1):
        pen.line([cx + step * bw // 7, fy1 + edge, cx + step * bw // 10, neck - half_tip // 2], fill=DARK,
                 width=max(1, unit // 260))

    # **I la pintura a la punta**, que és el que fa que sigui un pinzell de pintura i no una brotxa de barata: the last third of
    # the hairs is wet, and the paint is the darkest grey of the set. Una gota no hi és a posta —el dibuix que l'amo volia és el
    # pinzell i prou, i una bola al costat és una segona cosa dins d'una icona de noranta-quatre píxels—.
    pen.pieslice([cx - half_tip, neck - half_tip, cx + half_tip, neck + half_tip], 15, 165, fill=DARK, outline=INK,
                 width=max(1, unit // 240))
    pen.line([cx - half_tip // 2, neck - half_tip // 3, cx + half_tip // 2, neck - half_tip // 3], fill=DARK,
             width=max(1, unit // 200))

    # **El pinzell es gira i es torna a centrar dempeus**: the turned drawing is cropped to its own ink and put in the middle of
    # the box with the same air on every side, which is what makes it sit like the icons of the other apps and not like a thing
    # that fell in a corner (docs/45, docs/92).
    turned = image.rotate(38, resample=Image.BICUBIC, fillcolor=PAPER)
    box = Image.eval(turned, lambda v: 255 - v).getbbox()
    if box is not None:
        art = turned.crop(box)
        room = size * 88 // 100
        scale = room / max(art.size[0], art.size[1], 1)
        art = art.resize((max(1, int(art.size[0] * scale)), max(1, int(art.size[1] * scale))), Image.LANCZOS)
        out = Image.new("L", (size, size), PAPER)
        out.paste(art, ((size - art.size[0]) // 2, (size - art.size[1]) // 2))
        return out
    return turned.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=448, help="el costat del mestre, en píxels")
    ap.add_argument("--out", default="apps/paint/res/icon-source.png")
    args = ap.parse_args()
    draw(args.size).save(args.out)
    print("%s: %d x %d" % (args.out, args.size, args.size))


if __name__ == "__main__":
    main()
