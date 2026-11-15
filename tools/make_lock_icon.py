#!/usr/bin/env python3
"""El candau de la vista «Device Lock», per a la icona de l'app.

La vista el dibuixa amb les primitives del canvas (`apps/device_lock/device_lock.lua`), i una icona és el mateix
dibuix a una altra mida: **les proporcions són un sol joc de números** —les de `OLD/PaperOS/src/apps/lock_app.cpp`,
que és d'on ve la vista— i aquesta eina les passa a un PNG de 448 com els altres del menú.

    python3 tools/make_lock_icon.py

Escriu `apps/device_lock/res/icon.png`.
"""
import argparse

from PIL import Image, ImageDraw

# **Les proporcions del candau**, copiades de PaperOS i no inventades (docs/45): el cos és tan alt com ample, la nansa
# fa dues terceres parts de l'amplada del cos amb dues cames que hi entren, el pany té la seva placa, i la tinta és només
# el forat i els detalls —el cos és un contorn i no una taca—.
BODY_W = 34     # % del costat curt de la vista
BODY_H = 95     # % de l'amplada del cos
LIFT = 45       # quant de la nansa queda per sobre del cos
SHACKLE = 62    # amplada exterior de l'anell, del cos
RING = 12       # gruix d'aquest anell
STROKE = 6      # el traç del cos
CORNER = 14     # el cantó del cos
PLATE = 26      # radi de la placa del forat
PLATE_THICK = 4
KEY = 8         # radi del forat
RIVET = 4       # radi d'un rebló al peu

SUPERSAMPLE = 4


def draw(size):
    """El candau en un quadre de `size`, centrat, amb el paper de fons."""
    image = Image.new("L", (size * SUPERSAMPLE, size * SUPERSAMPLE), 255)
    pen = ImageDraw.Draw(image)
    unit = size * SUPERSAMPLE

    body_w = unit * BODY_W // 100
    body_h = body_w * BODY_H // 100
    lift = body_w * LIFT // 100
    icon_h = lift + body_h

    left = (unit - body_w) // 2
    top = (unit - icon_h) // 2
    body_top = top + lift

    # **La nansa: un anell amb dues cames que entren al cos**, i el cos es pinta després, o sigui que les cames queden
    # tapades pel paper de dins del cos, que és el que fa un candau de debò.
    hoop_w = body_w * SHACKLE // 100
    hoop_t = body_w * RING // 100
    hoop_r = hoop_w // 2
    hoop_x = left + (body_w - hoop_w) // 2
    hoop_bottom = body_top + hoop_r
    pen.rounded_rectangle([hoop_x, top, hoop_x + hoop_w - 1, hoop_bottom], radius=hoop_r, fill=0)
    pen.rounded_rectangle([hoop_x + hoop_t, top + hoop_t, hoop_x + hoop_w - 1 - hoop_t, hoop_bottom],
                          radius=hoop_r - hoop_t, fill=255)

    corner = body_w * CORNER // 100
    stroke = body_w * STROKE // 100
    pen.rounded_rectangle([left, body_top, left + body_w - 1, body_top + body_h - 1], radius=corner, fill=0)
    pen.rounded_rectangle([left + stroke, body_top + stroke, left + body_w - 1 - stroke,
                           body_top + body_h - 1 - stroke], radius=corner - stroke, fill=255)

    # **El pany**: la placa, el forat, la ranura i l'extrem rodó, que és tot el negre massís del mig.
    plate_r = body_w * PLATE // 100
    plate_t = body_w * PLATE_THICK // 100
    key_r = body_w * KEY // 100
    cx = left + body_w // 2
    face_y = body_top + body_h // 2
    hole_y = face_y - plate_r * 30 // 100
    slot_w = key_r * 14 // 10
    slot_y = face_y + plate_r * 45 // 100

    pen.ellipse([cx - plate_r, face_y - plate_r, cx + plate_r, face_y + plate_r], fill=0)
    pen.ellipse([cx - plate_r + plate_t, face_y - plate_r + plate_t, cx + plate_r - plate_t,
                 face_y + plate_r - plate_t], fill=255)
    pen.ellipse([cx - key_r, hole_y - key_r, cx + key_r, hole_y + key_r], fill=0)
    pen.rectangle([cx - slot_w // 2, hole_y, cx + slot_w // 2, slot_y], fill=0)
    pen.ellipse([cx - slot_w // 2, slot_y - slot_w // 2, cx + slot_w // 2, slot_y + slot_w // 2], fill=0)

    # **I els dos reblons del peu.**
    rivet_r = body_w * RIVET // 100
    rivet_y = body_top + body_h - stroke - rivet_r * 2
    for side in (22, 78):
        rx = left + body_w * side // 100
        pen.ellipse([rx - rivet_r, rivet_y - rivet_r, rx + rivet_r, rivet_y + rivet_r], fill=0)

    return image.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=448, help="el costat de la icona, en píxels")
    ap.add_argument("--out", default="apps/device_lock/res/icon.png")
    args = ap.parse_args()

    draw(args.size).save(args.out)
    print("%s: %d x %d" % (args.out, args.size, args.size))


if __name__ == "__main__":
    main()
