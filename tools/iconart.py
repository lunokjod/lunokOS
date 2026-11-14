#!/usr/bin/env python3
"""Les cinc marques de la targeta de la barra, **d'un clipart** i no de la mà.

L'amo ho va dir amb totes les lletres: *«usa un clipart, clarament no estàs
 dibuixant com ho feies l'altre dia a PaperOS»*. I tenia raó sobre d'on venien
aquelles icones: el projecte del paper **no les dibuixava a mà**, les treia de
la família **Font Awesome** —`tools/fontgen.py` en rasteritzava els caràcters—
i aquest repositori ja en porta una mostra a `tools/make_keys.py`, que fa les
cinc lletres del rellotge amb el mateix `fa-solid-900.ttf`.

Aquest fitxer fa el mateix per als cinc botons del panell de sistema:

  · **el dibuix és un glif del clipart** —`arrow-left`, `times`, `cog`,
    `redo` i `power-off`— i el fitxer no en dibuixa cap de propi;
  · **es rasteritza gran i es redueix d'una sola passada** (LANCZOS): 400 px de
    tinta que caben en el 80 % de la caixa de 448, i d'allà al costat de la
    clau (64 px). És el que fa que les vores portin els grisos del panell en
    lloc d'una escala de píxels;
  · **la tinta és un dels setze tons**, la 4 (68 de 255), i el paper la 15: la
    marca és una silueta i no demana cap color propi.

Llicències: el clipart és **Font Awesome Free 5.15.4**, amb els seus glifss
sota **SIL OFL 1.1** i les icones sota **CC BY 4.0** —el crèdit és al `README`
del projecte i a docs/45—.

Ús:
    python3 tools/iconart.py                  # els mestres i les icones del disc
    python3 tools/iconart.py --preview        # el full de contacte, per mirar-les
"""

from __future__ import annotations

import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.exit("Pillow is needed:  pip install Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
# **Els mestres** (448 × 448) són el que una persona edita i el que el
# repositori guarda, com els dibuixos de les apps a `tools/icons/` (docs/45).
MASTERS = os.path.join(HERE, "icons", "system")
# **I el que el sistema llegeix** és la icona al costat de la clau del panell.
ICONS = os.path.join(HERE, "..", "data", "res", "icons")

SIZE = 448   # la caixa del mestre: es rasteritza gran i es redueix un sol cop
KEY = 64     # el costat d'una clau de la targeta (panel.cpp, `kKeySide`)

# **Els setze grisos del panell amb un nom**: la marca és una silueta i en
# demana un de sol per a la tinta i el paper.
PAPER = 15
INK = 4


def grey(level: int) -> int:
    """Un to del panell (0..15) com el byte d'un PNG de vuit bits."""
    return level * 17


# **On viu el clipart**: Font Awesome 5 Free, la família de què el projecte del
# paper va treure les seves marques i de què aquest repositori ja en treu cinc a
# `tools/make_keys.py`. Es proven els camins coneguts i el primer que hi sigui és
# el que es fa servir; **si no n'hi ha cap el fitxer s'atura**: unes icones que
# canvien de dibuix en silenci el dia que falta una font són pitjor que un error.
FONTS = (
    os.path.join(HERE, "fontawesome", "fa-solid-900.ttf"),
    "/home/sharek/mnt/node0_bak/node0/public/external/fontawesome-free-5.15.4-web/webfonts/fa-solid-900.ttf",
    "/usr/share/fonts/truetype/fontawesome/fa-solid-900.ttf",
)

# **Cada marca és un glif del clipart i una frase que diu què vol dir.** El codi
# és el de Font Awesome 5 i el nom és el de la família; el comment de cada línia
# és el que es veu quan el dibuix s'obre en un editor (docs/45).
MARKS = {
    "back": (0xF060, "arrow-left: the way back"),
    "close": (0xF00D, "times: the way out"),
    "settings": (0xF013, "cog: the things of the system"),
    "reboot": (0xF01E, "redo: a circle that starts again"),
    "poweroff": (0xF011, "power-off: the switch"),
}


def the_font() -> str:
    for path in FONTS:
        if os.path.isfile(path):
            return path
    sys.exit("the clipart is not here: put fa-solid-900.ttf in tools/fontawesome/ "
             "(Font Awesome Free 5.15.4, SIL OFL 1.1)")


def glyph(code: int, px: int) -> Image.Image:
    """**La tinta del glif i prou**, retallada a la seva caixa. A symbol of a
    clipart family is not centred in the box of its font, and two marks put side
    by side with their boxes centred come out at two optical sizes (docs/45)."""
    face = ImageFont.truetype(the_font(), px)
    probe = Image.new("L", (px * 2, px * 2), 0)
    ImageDraw.Draw(probe).text((px, px), chr(code), font=face, fill=255, anchor="mm")
    box = probe.getbbox()
    if box is None:
        sys.exit(f"the font has no glyph for {code:#x}")
    return probe.crop(box)


def draw(name: str) -> Image.Image:
    """**El mestre de 448**: the ink of the glyph, fitted to eighty per cent of
    the box and centred on paper, in one tone. The twenty per cent that is left
    is the air a key of the card needs —a mark that fills its key is a key with a
    frame of ink— and the two ends of the fourteen levels are the paper of the
    card and its ink (docs/45, docs/92)."""
    code, _ = MARKS[name]
    art = glyph(code, 400)
    room = int(SIZE * 0.80)
    scale = min(room / art.width, room / art.height)
    art = art.resize((max(1, int(art.width * scale)), max(1, int(art.height * scale))), Image.LANCZOS)
    page = Image.new("L", (SIZE, SIZE), grey(PAPER))
    page.paste(grey(INK), ((SIZE - art.width) // 2, (SIZE - art.height) // 2), art)
    return page


def save(page: Image.Image, path: str, size: int) -> str:
    """**Una sola reducció** al costat que es demana: the 448 of the clipart and
    the 64 of the key come from one LANCZOS pass, which is what gives the edges
    their greys (docs/45)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    page.resize((size, size), Image.LANCZOS).save(path, "PNG", optimize=True)
    return path


def contact_sheet(path: str) -> str:
    """**El full de contacte**: the five marks at the size of the glass, side by
    side, so a person can see what the card of the bar will carry without a
    board in front of them (docs/85)."""
    names = list(MARKS)
    cell = KEY
    sheet = Image.new("L", (cell * len(names) + 16 * (len(names) + 1), cell + 32), grey(PAPER))
    for index, name in enumerate(names):
        icon = draw(name).resize((KEY, KEY), Image.LANCZOS)
        sheet.paste(icon, (16 + index * (cell + 16), 16))
    sheet.save(path, "PNG", optimize=True)
    return path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="The five marks of the card of the bar.")
    parser.add_argument("--masters", default=MASTERS, help="where the 448 drawings go")
    parser.add_argument("--out", default=ICONS, help="where the icons of the disk go")
    parser.add_argument("--size", type=int, default=KEY, help="the side of an icon of the disk")
    parser.add_argument("--preview", action="store_true", help="also write a contact sheet")
    args = parser.parse_args(argv)

    for name in MARKS:
        master = save(draw(name), os.path.join(args.masters, name + ".png"), SIZE)
        icon = save(draw(name), os.path.join(args.out, name + ".png"), args.size)
        print(f"{master}\n{icon}")
    if args.preview:
        print(contact_sheet("/tmp/iconart-sheet.png"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
