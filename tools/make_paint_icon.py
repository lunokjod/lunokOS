#!/usr/bin/env python3
"""La icona de l'app Paint: el pot de pintura amb la silueta en tinta i la cara en gris clar.

L'amo el va demanar després d'una pila de proves que no li van agradar (*«l'icona es horrible»*, *«això que dibuixes es un rasclet
per caca de gat»*, *«les taques de pintura semblen merda bonyeguda»*). Quan li vaig ensenyar cinc cliparts de Font Awesome —les
mateixes marques que porta la targeta del sistema— va triar el contrari d'un clipart, i ho va dir amb totes les lletres:
**«cap d'aquests: el pot que he dibuixat, però polit»**, i l'acabat que vol és el d'aquests cliparts: **silueta fosca i cara
clara**, o sigui contorn de tinta gruixut i el dibuix en el gris clar a dins.

Per això aquest pot no té ni mitges tintes ni lluentors: **el contorn és la silueta** —gruixut, com el d'una marca de la targeta—, la
cara del pot és el gris clar, i **el que és pintura és tinta pura**: la vora, tres regalims que pengen per fora, la pintura de dins i
la taca a terra. El dibuix és el d'un pot de debò, que vaig mirar en tres cliparts i en una foto: vora el·líptica, cos de cilindre en
perspectiva que s'estreta cap al cul, la costella del cul, **l'etiqueta del mig** (*«una etiqueta al mig el pot....»*), l'ansa de
filferro amb les seves dues orelles, i **les taques a terra enganxades i ovalades per la perspectiva** (*«les taques mes exagerades i
que es toquin formant fomes»*).

    python3 tools/make_paint_icon.py            # el mestre de 448 a `apps/paint/res/icon-source.png`
    python3 tools/make_icon.py --png apps/paint/res/icon-source.png \\
                               --out apps/paint/res/icon.png

Needs Pillow, like every tool of the icons here.
"""

import argparse
import math

from PIL import Image, ImageDraw

INK = 31
DARK = 59
MID = 87
LIGHT = 156
PAPER = 255

SUPERSAMPLE = 4

# **El contorn és la silueta** i per això és gruixut: un quatre per cent del quadre, que és el que tenen les marques de la targeta
# (i no el dos i mig de les icones de línia) — ho va demanar així: *«silueta fosca i cara clara»*.
EDGE = 40

# **El pot, en centèsimes del quadre.**
CAN_TOP = 30
CAN_BOTTOM = 83
CAN_HW = 27
CAN_FOOT_HW = 24
RIM_RY = 8.0
BAIL_HW = 28
BAIL_RY = 24       # lo alt que puja per damunt de les orelles

# **Els regalims** (desplaçament, llargada, amplada): cinc gotes de llargades molt diferents penjant de la vora —les dues més llargues
# baixen fins a l'etiqueta i la tapen, que és el que fa que sembli que la pintura ha corregut de debò (l'amo: *«mes gotes de pintura i
# mes llargues, algunes tapant l'etiqeta»*).
DRIPS = ((-16.0, 34.0, 5.4), (-6.0, 22.0, 6.2), (4.0, 41.0, 7.4), (13.0, 30.0, 6.0), (19.0, 16.0, 5.0))

# **Les taques a terra, ovalades i enganxades** (l'amo: *«les taques mes exagerades i que es toquin formant fomes»*, i després
# *«no tant exagerades... i la pintura va AL TERRA, SOTA EL CUB»*): a floor seen at an angle turns every stain into an ellipse —wide and
# flat—, i les el·lipses es toquen i es fonen en una sola taca. Es dibuixen **dues vegades**: primer totes amb el radi engruixit en
# tinta i al damunt les de debò en el to fosc, o sigui que la vora de tinta només surt per fora del conjunt (0.192.54). **I van
# darrere del pot**: la pintura és a terra i el pot hi seu a sobre, no pas un munt de pintura al davant del cul.
# (dx, dy, rx, ry) en centèsimes del quadre, comptades des del centre del cul del pot.
STAINS = ((-27.0, 1.5, 11.0, 4.2), (4.0, 3.0, 12.0, 4.4), (23.0, 1.0, 9.0, 3.6))


def draw(size):
    unit = size * SUPERSAMPLE
    image = Image.new("L", (unit, unit), PAPER)
    pen = ImageDraw.Draw(image)

    edge = max(3, unit * EDGE // 1000)
    scale = unit / 100.0
    cx = unit / 2.0

    def R(v):
        return v * scale

    top, bottom = R(CAN_TOP), R(CAN_BOTTOM)
    hw, foot, rim = R(CAN_HW), R(CAN_FOOT_HW), R(RIM_RY)

    # **La taca a terra, sota el pot**: the ellipses overlap and their union gets one ink edge, i es dibuixa **abans del cos** perquè
    # el pot hi seu a sobre i la pintura li surti pels costats — no al davant del cul, que és el que semblava una pila de fang
    # (l'amo: *«la pintura va AL TERRA, SOTA EL CUB»*, 0.192.54).
    for grow in (edge, 0):
        for where, down, rx, ry in STAINS:
            x, y = cx + R(where), bottom + R(down)
            pen.ellipse([x - R(rx) - grow, y - R(ry) - grow, x + R(rx) + grow, y + R(ry) + grow],
                        fill=INK if grow else DARK)

    # **L'ansa de filferro**, darrere del pot i **alta** (l'amo: *«la nança mes gran i alta, sobresurt molt poc»*): un arc gruixut com
    # tota la resta de la tinta, perquè en aquest acabat no hi ha línies primes — tot el que es veu és silueta.
    pen.arc([cx - R(BAIL_HW), top - R(BAIL_RY), cx + R(BAIL_HW), top + R(BAIL_RY)], 180, 360, fill=INK,
            width=int(R(5.2)))

    # **El cos**: a cylinder in perspective —narrower at the foot, which is what a 3/4 view does— closed in one path so the outline
    # is one line and not four that meet in the corners. The face is the light grey of the cliparts and there is no shading: in this
    # style the volume is the thick outline (l'amo: *«silueta fosca i cara clara»*).
    body = [(cx - hw, top), (cx + hw, top), (cx + foot, bottom)]
    for step in range(1, 13):
        a = math.radians(180 * step / 12)
        body.append((cx + foot * math.cos(a), bottom + R(4.5) * math.sin(a)))
    silhouette = Image.new("L", (unit, unit), 0)
    ImageDraw.Draw(silhouette).polygon(body, fill=255)
    image.paste(LIGHT, (0, 0), silhouette)
    # **I el camí es tanca**: without the last point back to the start the left side of the can had no ink at all and the pot
    # looked cut open on that side (vist a la icona de 448, 0.192.54).
    pen.line(body + [body[0]], fill=INK, width=edge, joint="curve")
    # the rib near the foot, which is what a paint can has and a plain bucket does not
    pen.arc([cx - foot - R(1), bottom - R(11), cx + foot + R(1), bottom - R(2)], 25, 155, fill=INK,
            width=max(3, unit // 180))

    # **L'etiqueta del pot** (l'amo: *«una etiqueta al mig el pot....»*): a paper band across the body with an ink outline, and its
    # top and bottom edges are arcs because the body is a cylinder —a straight rectangle would be a sticker on a flat thing—. És el
    # que fa que el pot es llegeixi com un pot de pintura de botiga i no com un cubell qualsevol.
    label_top, label_bottom, label_hw = R(52.0), R(71.0), R(19.0)
    # **Les dues vores són la mateixa corba i cap avall** (el mateix radi que el cul del pot, o l'etiqueta no és del mateix cilindre): the body is a cylinder seen a little from above, so the top edge of a band
    # around it bows **down** in the middle exactly like the bottom one does —the first version arched the top the other way and the
    # label came out like a barrel (l'amo: *«l'etiqueta te malament la perspectiva»*, 0.192.54)—.
    lid = []
    for step in range(0, 7):
        a = math.pi * step / 6
        lid.append((cx - label_hw + 2 * label_hw * step / 6, label_top + R(3.6) * math.sin(a)))
    for step in range(6, -1, -1):
        a = math.pi * step / 6
        edge = -R(0.8) if step in (0, 6) else 0.0   # el pot s'estreta cap avall, i l'etiqueta el segueix
        lid.append((cx - label_hw + 2 * label_hw * step / 6 + (edge if step == 0 else -edge), label_bottom + R(3.6) * math.sin(a)))
    pen.polygon(lid, fill=PAPER, outline=INK, width=max(3, unit // 150))

    # **La vora**: la lliurea en tinta, la paret de dins en el gris clar i **la pintura en tinta al mig**, amb el gruix de cada
    # anella triat perquè es llegeixin les tres coses a noranta-quatre píxels: el llavi, la paret i el líquid.
    pen.ellipse([cx - hw, top - rim, cx + hw, top + rim], fill=INK)
    pen.ellipse([cx - hw + R(4.4), top - rim + R(4.2), cx + hw - R(4.4), top + rim - R(4.2)], fill=DARK)
    # i una lluentor prima a la pintura, que és el que la fa líquida
    pen.arc([cx - R(14), top - R(6.4), cx + R(2), top + R(1.6)], 200, 330, fill=LIGHT,
            width=max(3, unit // 260))

    # **Els regalims**: surten del llavi de davant i baixen pel cos, amb el cap rodó. Comencen on acaba la pintura de dins, o sigui
    # que es llegeixen com la mateixa pintura que ha sortit, i no com dues peces enganxades a la vora (0.192.54).
    lip = top + R(RIM_RY) * 0.78
    for where, length, wide in DRIPS:
        x = cx + R(where)
        pen.rounded_rectangle([x - R(wide) / 2, lip - R(2), x + R(wide) / 2, top + R(length)], radius=R(wide) / 2,
                              fill=INK)

    # **Les dues orelles de l'ansa**, on el filferro entra al pot, dibuixades després de l'ansa perquè la tapin.
    for x in (cx - hw + R(1.0), cx + hw - R(1.0)):
        pen.ellipse([x - R(3.4), top - R(2.6), x + R(3.4), top + R(2.6)], fill=INK)

    # **Es retalla i es torna a centrar**: the drawing is cropped to its own ink and put in the middle of the box with the same air
    # on every side, which is what makes it sit like the icons of the other apps (docs/45, docs/92).
    box = Image.eval(image, lambda v: 255 - v).getbbox()
    if box is None:
        return image.resize((size, size), Image.LANCZOS)
    art = image.crop(box)
    room = size * 92 // 100
    fit = room / max(art.size[0], art.size[1], 1)
    art = art.resize((max(1, int(art.size[0] * fit)), max(1, int(art.size[1] * fit))), Image.LANCZOS)
    out = Image.new("L", (size, size), PAPER)
    out.paste(art, ((size - art.size[0]) // 2, (size - art.size[1]) // 2))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=448, help="el costat del mestre, en píxels")
    ap.add_argument("--out", default="apps/paint/res/icon-source.png")
    args = ap.parse_args()
    draw(args.size).save(args.out)
    print("%s: %d x %d" % (args.out, args.size, args.size))


if __name__ == "__main__":
    main()
