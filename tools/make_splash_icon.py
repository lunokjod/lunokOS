#!/usr/bin/env python3
"""La icona de l'app «Splash», que és la de les imatges que el sistema pinta.

**Per què se'n fa una de nova** (0.192.27, l'amo: *«Usa en aquesta nova app l'icona de l'app Splash i fes na nova per l'app
Splash (recorda que es per veure les imatges dels events de sistema) perqué no confondre al usuari»*): el marc amb les
muntanyes i el sol és el dibuix de tota la vida del visor d'imatges —el porta `tools/icons/ImageViewer.png` i és el que ara
té l'app `images`—, i el que ensenya l'app Splash no són fotos de ningú sinó **les imatges que el sistema pinta** en els seus
moments: l'arrencada, el corrent apagat, el reinici, el cable, el son, la bateria que s'ha acabat i l'error.

**El dibuix**: el mateix marc del visor —el contorn de tinta, el paper a dins i el peu gris— i, al mig, el símbol d'encesa
(el cercle amb una escletxa a dalt i el mànec) en lloc de les muntanyes. És el mateix joc de cinc tons que la resta de
`tools/icons/` i es reconeix al costat del visor sense confondre's: un és una foto, l'altre és la pantalla del sistema.

    python3 tools/make_splash_icon.py

Escriu `tools/icons/Splash.png`, i d'allà `tools/make_icon.py` en fa la icona de l'app.
"""
import argparse
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "icons", "Splash.png")

# **Els cinc tons de casa**, els mateixos que `tools/icons/ImageViewer.png`: paper, cos, ombra, detall i tinta (docs/45).
PAPER = 255
BODY = 221
SHADOW = 187
DETAIL = 170
INK = 68

SIZE = 448
SUPERSAMPLE = 4

# **El marc, mesurat al mestre del visor** i no inventat: la caixa és de 344 x 280 a 52,84 del dibuix de 448, el contorn fa
# setze píxels i les cantonades quaranta-quatre. El símbol d'encesa és el del sistema (`draw_icon`, la marca `poweroff`): un
# anell amb una escletxa a dalt i un mànec que hi entra, i a l'icone fa cent quaranta píxels de diàmetre.
FRAME = (52, 84, 396, 364)
STROKE = 16
CORNER = 44
GLYPH = 140
GLYPH_STROKE = 22


def ring(pen, cx, cy, radius, thickness, top_gap_deg, colour):
    """Un anell amb una escletxa a dalt, dibuixat com un pastís i no com dues corbes."""
    box = [cx - radius, cy - radius, cx + radius, cy + radius]
    start = -90 + top_gap_deg / 2
    pen.arc(box, start=start, end=start + 360 - top_gap_deg, fill=colour, width=thickness)


def draw(size):
    image = Image.new("L", (size * SUPERSAMPLE, size * SUPERSAMPLE), PAPER)
    pen = ImageDraw.Draw(image)
    unit = size * SUPERSAMPLE

    def at(value):
        return value * unit // SIZE

    left, top, right, bottom = (at(value) for value in FRAME)
    stroke = at(STROKE)
    corner = at(CORNER)

    # **El marc**: la tinta de fora, el cos i el paper de dins, i una ombra de dos píxels a baix que és el que fa que el
    # marc es llegeixi com una cosa que té gruix i no com un rectangle buit (el mateix truc del visor).
    pen.rounded_rectangle([left, top, right, bottom], radius=corner, fill=INK)
    pen.rounded_rectangle([left + stroke, top + stroke, right - stroke, bottom - stroke - at(6)],
                          radius=corner - stroke, fill=BODY)
    inner = stroke + at(18)
    pen.rounded_rectangle([left + inner, top + inner, right - inner, bottom - inner - at(6)],
                          radius=max(1, corner - inner), fill=PAPER)

    # **El símbol d'encesa al mig del paper**: el cercle amb l'escletxa a dalt, el mànec que hi entra i res més. El que
    # l'app ensenya són els moments en què el sistema encén, apaga o reinicia la placa.
    cx = (left + right) // 2
    cy = (top + bottom) // 2 - at(6)
    radius = at(GLYPH) // 2
    ring(pen, cx, cy + radius // 5, radius, at(GLYPH_STROKE), 62, INK)
    pen.rounded_rectangle([cx - at(GLYPH_STROKE) // 2, cy - radius, cx + at(GLYPH_STROKE) // 2, cy + radius // 5],
                          radius=at(GLYPH_STROKE) // 2, fill=INK)

    # **I el peu del marc en detall**, que és la línia de sota del visor: diu que el marc té fons.
    pen.rounded_rectangle([left + stroke, bottom - stroke - at(34), right - stroke, bottom - stroke - at(10)],
                          radius=at(8), fill=DETAIL)

    return image.resize((size, size), Image.LANCZOS)


def main(argv):
    parser = argparse.ArgumentParser(description="La icona de l'app Splash.")
    parser.add_argument("--out", default=OUT, help="where the 448 drawing goes")
    parser.add_argument("--preview", action="store_true", help="also write a copy at icon size")
    args = parser.parse_args(argv[1:])

    image = draw(SIZE)
    image.save(args.out)
    print(args.out)
    if args.preview:
        small = image.resize((94, 94), Image.LANCZOS)
        small.save("/tmp/splash-icon-94.png")
        print("/tmp/splash-icon-94.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(__import__("sys").argv))
