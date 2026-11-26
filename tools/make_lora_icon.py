#!/usr/bin/env python3
"""La icona de l'app LoRa: una antena que escolta.

L'amo va demanar un **monitor i sniffer de LoRa** (*«Fer una app Monitor/Sniffer LORA. Ha de buscar senyals»*) i la icona ha de
dir el mateix que la pàgina: una antena i les ones que arriben, i **cap fletxa cap enfora** —aquesta ràdio no parla (docs/30,
docs/50). El joc de tons és el de la resta d'icones d'aquest sistema: el contorn del negre del vidre, dos grisos per al cos i la
seva llum, i un to més fosc per als detalls (docs/45, docs/92).

    python3 tools/make_lora_icon.py            # el mestre de 448 a `apps/lora/res/icon-source.png`
    python3 tools/make_icon.py --png apps/lora/res/icon-source.png \\
                               --out apps/lora/res/icon.png

Needs Pillow, like every tool of the icons here.
"""

import argparse

from PIL import Image, ImageDraw

INK = 31
DARK = 59
MID = 87
LIGHT = 156
PAPER = 255

SUPERSAMPLE = 4


def draw(size):
    unit = size * SUPERSAMPLE
    image = Image.new("L", (unit, unit), PAPER)
    pen = ImageDraw.Draw(image)
    edge = max(1, unit // 150)

    # **El màstil**: una columna que no arriba ni a dalt ni a baix de tot, amb la meitat dreta més fosca, que és el que fa que
    # es llegeixi com un tub i no com una barra.
    mast_w = unit * 11 // 100
    cx = unit * 38 // 100
    top = unit * 14 // 100
    bottom = unit * 84 // 100
    pen.rounded_rectangle([cx - mast_w // 2, top, cx + mast_w // 2, bottom], radius=mast_w // 2, fill=LIGHT, outline=INK,
                          width=edge)
    pen.rectangle([cx + mast_w // 10, top + mast_w // 2, cx + mast_w // 2 - edge, bottom - mast_w // 2], fill=MID)

    # **La base**: una peanya ampla que la planta a terra, amb una línia clara a sobre.
    base_w = unit * 30 // 100
    base_h = unit * 7 // 100
    pen.rounded_rectangle([cx - base_w // 2, bottom - base_h // 2, cx + base_w // 2, bottom + base_h // 2], radius=base_h // 2,
                          fill=MID, outline=INK, width=edge)
    pen.line([cx - base_w // 2 + edge * 2, bottom - base_h // 6, cx + base_w // 2 - edge * 2, bottom - base_h // 6], fill=LIGHT,
             width=max(1, unit // 320))

    # **La punta**, que és el detall que diu «una antena»: un disc petit al capdamunt amb una llum.
    tip = mast_w // 2 + unit * 1 // 100
    pen.ellipse([cx - tip, top - tip, cx + tip, top + tip], fill=DARK, outline=INK, width=edge)
    pen.ellipse([cx - tip // 3, top - tip // 2, cx + tip // 6, top + tip // 6], fill=LIGHT)

    # **Les ones que arriben**: tres arcs concèntrics a la dreta, cada un amb el seu contorn, i el més a prop amb un segon arc de
    # llum a dins. Els arcs no tanquen el cercle —seria un dibuix esportiu i no una ràdio— i comencen i acaben on una antena
    # dibuixa el seu camp (docs/45).
    for i, radius in enumerate((unit * 17 // 100, unit * 27 // 100, unit * 37 // 100)):
        box = [cx - radius, top - radius + unit * 2 // 100, cx + radius, top - radius + unit * 2 // 100 + 2 * radius]
        pen.arc(box, -55, 55, fill=INK, width=max(edge, unit // 90))
        if i == 0:
            inner = radius - unit * 2 // 100
            pen.arc([cx - inner, top - inner + unit * 2 // 100, cx + inner, top - inner + unit * 2 // 100 + 2 * inner], -50, 50,
                    fill=LIGHT, width=max(1, unit // 220))

    # **Una segona font de senyal a l'esquerra**: dos arcs curts, que és el que fa que el dibuix no estigui caigut cap a un costat.
    for radius in (unit * 15 // 100, unit * 24 // 100):
        box = [cx - radius, top - radius + unit * 2 // 100, cx + radius, top - radius + unit * 2 // 100 + 2 * radius]
        pen.arc(box, 145, 215, fill=MID, width=max(1, unit // 130))

    # **I el dibuix es retalla a la seva tinta i es centra**: el mateix air a cada costat que les altres icones d'aquesta casa,
    # o l'antena cauria en un racó de la cel·la (docs/45, docs/92).
    box = Image.eval(image, lambda v: 255 - v).getbbox()
    if box is not None:
        art = image.crop(box)
        room = size * 88 // 100
        scale = room / max(art.size[0], art.size[1], 1)
        art = art.resize((max(1, int(art.size[0] * scale)), max(1, int(art.size[1] * scale))), Image.LANCZOS)
        out = Image.new("L", (size, size), PAPER)
        out.paste(art, ((size - art.size[0]) // 2, (size - art.size[1]) // 2))
        return out
    return image.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=448, help="el costat del mestre, en píxels")
    ap.add_argument("--out", default="apps/lora/res/icon-source.png")
    args = ap.parse_args()
    draw(args.size).save(args.out)
    print("%s: %d x %d" % (args.out, args.size, args.size))


if __name__ == "__main__":
    main()
