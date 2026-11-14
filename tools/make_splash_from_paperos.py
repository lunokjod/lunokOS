#!/usr/bin/env python3
"""Les dues pàgines del PaperOS que aquest sistema hereta, dibuixades exactament igual (0.183.1).

L'amo va demanar que el splash de bateria baixa i el de càrrega **siguin els del projecte
vell**, mirats i copiats. El que hi ha aquí és l'aritmètica d'aquelles dues pantalles, número
per número, i no una imitació feta a ull:

  · **bateria baixa** — `PaperOS/src/ui/power_screen.cpp`, `drawPowerOffScreen` amb
    `PowerOffNotice::BatteryLow` i `drawBatteryLowMark`, amb el tema de `src/ui/theme.cpp`
    (`padLarge` 18, `gap` 10) i les tres cares de `src/gfx/fonts` (`fontTitle` 57/45,
    `fontBody` 43/34, `fontSmall` 33/26, que són DejaVuSans-Bold a 48, 36 i 28 px).
  · **càrrega** — `PaperOS/src/apps/charging_app.cpp`, `ChargingApp::arrange` i `::onDraw`,
    amb el llamp de sis punts que ve de `src/ui/title_bar.cpp` (`fillBolt`).

Les imatges font que hi havia es queden a `tools/splash-sources/` (vegeu el README d'allà):
la captura del PaperOS per a la de bateria baixa; de la targeta de càrrega el PaperOS no en
té cap — el seu `tools/ui_preview.cpp` només dibuixa la barra amb el cable a dins— i per tant
la font d'aquella és el codi, que és el que aquest fitxer copia.

**L'única cosa que no és una còpia és el nom del sistema**: la pàgina de bateria baixa porta el
nom a la primera línia, i aquí diu `lunokOS` i no `PaperOS`, perquè aquesta placa no és l'altra.

Ús: `python3 tools/make_splash_from_paperos.py`, que escriu a `data/res/splash`.
"""

from __future__ import annotations

import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "data", "res", "splash")

# **La pàgina és el vidre sencer del Paper**: 540 x 960, que és el mateix que el d'aquesta placa.
WIDTH = 540
HEIGHT = 960

INK = 0
PAPER = 255
# `gray::Du4Light` de `src/core/palette.h`: el gris de les cel·les buides, que és un dels quatre
# nivells que qualsevol mode del panell pinta bé (0, 5, 10 i 15).
DU4_LIGHT = 170  # 10 de 16 nivells, en vuit bits

# La cara del sistema del Paper: `fontgen.py` la cou de DejaVuSans-Bold i el que en queda és el
# glif a 48, 36 i 28 px amb una alçada de línia i un ascens que són els de la capçalera del fitxer
# (`font_ui48b.h` diu 57 i 45).
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def grey(level16: int) -> int:
    """Un nivell de setze del panell, en vuit bits, que és el que aquests PNG porten."""
    return int(round(level16 * 255 / 15))


def face(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT_BOLD, size)


def text_width(draw: ImageDraw.ImageDraw, text: str, f: ImageFont.FreeTypeFont) -> int:
    return int(round(draw.textlength(text, font=f)))


def draw_text(draw: ImageDraw.ImageDraw, text: str, x: int, top: int, size: int, ascent: int) -> None:
    """`drawText` del Paper: `y` és el cim de la caixa de la línia i el glif penja de l'ascens."""
    draw.text((x, top + ascent), text, font=face(size), fill=INK, anchor="ls")


# --------------------------------------------------------------- les peces

def fill_triangle(draw: ImageDraw.ImageDraw, cx: int, top: int, half_w: int, h: int, level: int) -> None:
    """`fillTriangle`: una fila alhora, amb la meitat que creix, i prou ampla per no desaparèixer."""
    for y in range(h):
        half = half_w * y // h
        draw.rectangle((cx - half, top + y, cx - half + (2 * half + 1) - 1, top + y), fill=level)


def thick_line(draw: ImageDraw.ImageDraw, x0: int, y0: int, x1: int, y1: int, thickness: int, level: int) -> None:
    """`drawThickLine`: Bresenham, i a cada passa una caixa de `thickness` centrada al punt.

    **I `dy` és negatiu o zero, com al Paper** (`(y1 > y0) ? (y0 - y1) : (y1 - y0)`): la recta es
    camina amb `err = dx + dy` i dues comparacions, i amb un `dy` positiu les dues es compleixen
    sempre, la línia avança en diagonal i **no arriba mai al final** —un bucle que penja; em va
    passar el primer cop que el vaig portar.
    """
    half = thickness // 2
    dx = (x1 - x0) if x1 > x0 else (x0 - x1)
    dy = (y0 - y1) if y1 > y0 else (y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        draw.rectangle((x0 - half, y0 - half, x0 - half + thickness - 1, y0 - half + thickness - 1), fill=level)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def corner_inset(rad: int, i: int) -> int:
    dy = rad - 1 - i
    return rad - int((rad * rad - dy * dy) ** 0.5)


def fill_round_rect(draw: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int, radius: int, level: int) -> None:
    """`fillRoundRect`: la banda del mig sencera i, a cada fila de les cantonades, el retall del cercle."""
    rad = radius
    if rad > w // 2:
        rad = w // 2
    if rad > h // 2:
        rad = h // 2
    if rad <= 0:
        draw.rectangle((x, y, x + w - 1, y + h - 1), fill=level)
        return
    draw.rectangle((x, y + rad, x + w - 1, y + rad + (h - 2 * rad) - 1), fill=level)
    for i in range(rad):
        inset = corner_inset(rad, i)
        wide = w - 2 * inset
        if wide <= 0:
            continue
        draw.rectangle((x + inset, y + i, x + inset + wide - 1, y + i), fill=level)
        draw.rectangle((x + inset, y + h - 1 - i, x + inset + wide - 1, y + h - 1 - i), fill=level)


# ------------------------------------------------- bateria baixa (power_screen)

def battery_low_mark(draw: ImageDraw.ImageDraw, left: int, top: int, w: int, h: int) -> None:
    """`drawBatteryLowMark` amb la banda que li dona `drawPowerOffScreen`."""
    widest = w * 1000 // 955
    if h > widest:
        h = widest
    if h < 64:
        return

    stroke = 6
    nub_h = h * 6 // 100
    body_h = h - nub_h
    body_w = h * 58 // 100
    nub_w = body_w * 36 // 100

    tri_h = h * 52 // 100
    tri_w = h * 50 // 100
    over = h * 12 // 100

    group_w = body_w + tri_w - over
    x0 = left + (w - group_w) // 2
    y0 = top + (h - h) // 2

    body_x, body_y = x0, y0 + nub_h
    body_r, body_b = body_x + body_w, body_y + body_h

    # El terminal, amb dues files dins del cos perquè els dos siguin una peça.
    draw.rectangle((body_x + (body_w - nub_w) // 2, y0,
                    body_x + (body_w - nub_w) // 2 + nub_w - 1, y0 + (nub_h + 2) - 1), fill=INK)

    # La vora amb les quatre cantonades tallades a 45 graus.
    cut = stroke
    thick_line(draw, body_x + cut, body_y, body_r - cut, body_y, stroke, INK)
    thick_line(draw, body_x + cut, body_b, body_r - cut, body_b, stroke, INK)
    thick_line(draw, body_x, body_y + cut, body_x, body_b - cut, stroke, INK)
    thick_line(draw, body_r, body_y + cut, body_r, body_b - cut, stroke, INK)
    thick_line(draw, body_x, body_y + cut, body_x + cut, body_y, stroke, INK)
    thick_line(draw, body_r - cut, body_y, body_r, body_y + cut, stroke, INK)
    thick_line(draw, body_x, body_b - cut, body_x + cut, body_b, stroke, INK)
    thick_line(draw, body_r - cut, body_b, body_r, body_b - cut, stroke, INK)

    # Quatre cel·les i la de baix plena: una pila es buida des de dalt.
    inset = stroke + 4
    cell_gap = h // 33
    inner_h = body_h - 2 * inset
    cell_h = (inner_h - (4 - 1) * cell_gap) // 4
    for i in range(4):
        y = body_b - inset - (i + 1) * cell_h - i * cell_gap
        draw.rectangle((body_x + inset, y, body_x + inset + (body_w - 2 * inset) - 1, y + cell_h - 1),
                       fill=INK if i == 0 else DU4_LIGHT)

    # El triangle d'avis, amb la vora de paper que el fa una insígnia.
    tri_right = body_r + tri_w - over
    tri_bottom = body_b
    tri_top = tri_bottom - tri_h
    cx = tri_right - tri_w // 2

    fill_triangle(draw, cx, tri_top - stroke, tri_w // 2 + stroke, tri_h + 2 * stroke, PAPER)
    fill_triangle(draw, cx, tri_top, tri_w // 2, tri_h, INK)

    bar_w = tri_w // 7
    bar_h = tri_h * 34 // 100
    bar_x = cx - bar_w // 2
    bar_y = tri_top + tri_h * 34 // 100
    draw.rectangle((bar_x, bar_y, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=PAPER)
    dot_y = bar_y + bar_h + tri_h * 8 // 100
    draw.rectangle((bar_x, dot_y, bar_x + bar_w - 1, dot_y + bar_w - 1), fill=PAPER)


def battery_low_page() -> Image.Image:
    """`drawPowerOffScreen(BatteryLow)`: la insígnia i cinc línies, tot centrat com una peça."""
    pad_large = 18
    gap = 10
    # (text, px de la cara, ascens, alçada de línia) — `fontTitle`, `fontBody` i `fontSmall`.
    lines = [
        ("lunokOS", 48, 45, 57),
        ("The battery is running out.", 36, 34, 43),
        ("Plug in the USB cable:", 28, 26, 33),
        ("it will start again", 28, 26, 33),
        ("once it has power.", 28, 26, 33),
    ]

    total = sum(line_h + gap for (_, _, _, line_h) in lines) - gap
    mark_h = min(HEIGHT // 4, 200)
    mark_gap = pad_large
    top = (HEIGHT - (total + mark_h + mark_gap)) // 2

    picture = Image.new("L", (WIDTH, HEIGHT), PAPER)
    draw = ImageDraw.Draw(picture)

    battery_low_mark(draw, pad_large, top, WIDTH - 2 * pad_large, mark_h)

    y = top + mark_h + mark_gap
    for text, size, ascent, line_h in lines:
        width = text_width(draw, text, face(size))
        x = (WIDTH - width) // 2
        draw_text(draw, text, x if x > 0 else 0, y, size, ascent)
        y += line_h + gap

    return picture


# ---------------------------------------------------- càrrega (charging_app)

# El llamp de sis punts de `title_bar.cpp` (`fillBolt`), en percentatges de la seva caixa.
BOLT = ((70, 0), (22, 52), (48, 52), (30, 100), (78, 48), (52, 48))


def fill_bolt(draw: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int, level: int) -> None:
    """`fillBoltMark`: el polígon, fila a fila, amb la creuera de cada fila ordenada."""
    for row in range(h):
        py = ((2 * row + 1) * 100) // (2 * h)
        crossings = []
        for i in range(len(BOLT)):
            j = (i + 1) % len(BOLT)
            y1, y2 = BOLT[i][1], BOLT[j][1]
            if y1 == y2:
                continue
            if py < y1 and py < y2:
                continue
            if py >= y1 and py >= y2:
                continue
            t = ((py - y1) * 100) // (y2 - y1)
            crossings.append(BOLT[i][0] + (t * (BOLT[j][0] - BOLT[i][0])) // 100)
        crossings.sort()
        for k in range(0, len(crossings) - 1, 2):
            xa = x + (crossings[k] * w) // 100
            xb = x + (crossings[k + 1] * w) // 100
            if xb < xa:
                continue
            draw.rectangle((xa, y + row, xb, y + row), fill=level)


def charging_page() -> Image.Image:
    """`ChargingApp::onDraw` amb el que `arrange()` li deixa: el vidre sencer, sense barra."""
    picture = Image.new("L", (WIDTH, HEIGHT), PAPER)
    draw = ImageDraw.Draw(picture)

    unit = min(WIDTH, HEIGHT)
    body_w = unit * 40 // 100
    body_h = body_w * 48 // 100
    nub_w = body_w * 10 // 100

    wall = body_w * 7 // 100
    corner = body_w * 10 // 100
    nub_h = body_h * 32 // 100

    title_h = 57
    gap = 18 + 24
    block_h = body_h + gap + title_h
    y = (HEIGHT - block_h) // 2

    icon_w = body_w + nub_w
    body_x = (WIDTH - icon_w) // 2

    # El terminal primer: el cos el tapa on es troben, i la línia del mig és la seva paret.
    fill_round_rect(draw, body_x + body_w, y + (body_h - nub_h) // 2, nub_w, nub_h, nub_h // 3, INK)
    fill_round_rect(draw, body_x, y, body_w, body_h, corner, INK)

    inner_x, inner_y = body_x + wall, y + wall
    inner_w, inner_h = body_w - 2 * wall, body_h - 2 * wall
    inner_corner = corner - wall
    fill_round_rect(draw, inner_x, inner_y, inner_w, inner_h, inner_corner if inner_corner > 0 else 1, PAPER)

    bolt_w = inner_w * 45 // 100
    bolt_h = inner_h * 84 // 100
    fill_bolt(draw, inner_x + inner_w // 2 - bolt_w // 2, inner_y + inner_h // 2 - bolt_h // 2,
              bolt_w, bolt_h, INK)

    # La frase, centrada a la caixa que `arrange` deixa sota la bateria.
    text = "Charging..."
    text_y = y + body_h + gap
    draw_text(draw, text, (WIDTH - text_width(draw, text, face(48))) // 2, text_y, 48, 45)

    return picture


def the_drawing(picture: Image.Image) -> Image.Image:
    """**El dibuix i no el paper que el volta** (0.183.4, the owner: *«per qué l'splash charging i lowbatt
    són a pantalla completa? són blancs de fons!»*). The design is the whole glass of the Paper —540 x 960— because
    that is how the old project drew it, and the drawing is a mark and five lines in the middle of it: `splash::show`
    drops the uniform border and centres what is left, so the paper around it was a size written, read and decoded for
    nothing. **The file is the ink it carries**, trimmed by the same rule the system uses, and the glass does not
    change: what is painted was already the drawing, centred.
    """
    page = picture.getpixel((0, 0))
    ink = picture.point(lambda v: 255 if abs(v - page) > 2 else 0)
    box = ink.getbbox()
    if box is None:
        return picture
    return picture.crop(box)


def save(picture: Image.Image, name: str) -> None:
    path = os.path.join(OUT, name)
    drawing = the_drawing(picture)
    drawing.save(path, "PNG", optimize=True)
    print(f"{path} {drawing.width}x{drawing.height}")


def main() -> int:
    if not os.path.isdir(OUT):
        print(f"there is no {OUT}")
        return 1
    save(battery_low_page(), "lowbatt.png")
    save(charging_page(), "charging.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
