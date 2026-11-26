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
    # **I la setena, que és la que hi ha en aquesta màquina** (0.189.139): the codes of the house and of the other five marks are
    # the same ones in the seventh family, so a checkout that only carries this one draws the same card (docs/45).
    "/home/jrubio/code/TebimaDevices/asset/fontawesome-free-7.2.0-desktop/otfs/Font Awesome 7 Free-Solid-900.otf",
)

# **Cada marca és un glif del clipart i una frase que diu què vol dir.** El codi
# és el de Font Awesome 5 i el nom és el de la família; el comment de cada línia
# és el que es veu quan el dibuix s'obre en un editor (docs/45).
MARKS = {
    # **El xevró i no la fletxa** (0.189.33, l'amo: *«l'icona es horrible, busca'n una de millor»*): the `arrow-left` of the
    # set is a shaft of two thin strokes, and a thin diagonal is the first thing an e-ink panel with four levels turns into a
    # staircase. The solid chevron is one shape of a single weight, it is read at forty pixels as well as at a hundred and
    # sixty, and it says «back» without a tail to lose (docs/45, docs/92).
    "back": (0xF104, "angle-left: the way back"),
    "close": (0xF00D, "times: the way out"),
    "settings": (0xF013, "cog: the things of the system"),
    "reboot": (0xF01E, "redo: a circle that starts again"),
    "poweroff": (0xF011, "power-off: the switch"),
    # **La lluna** (0.189.25): the board is still on, drawing current, and asleep until a key is pressed —which is neither the
    # switch of the power off nor the circle of the restart— (docs/45, docs/50).
    "deepsleep": (0xF186, "moon: the board sleeps with its keys awake"),
    # **La casa** (0.189.139, l'amo: *«un icona d'una casa per arrencar HOME APP»*): the way to the page of the start, which is
    # the one the board comes back to when the app in front goes and nothing asked for another one. `U+F015` has been the house
    # of this family since the fifth version and it is still the same address in the seventh, so the mark of the card does not
    # move the day the font does (docs/45, docs/50).
    "home": (0xF015, "house: the page of the start"),
    # **El candau** (0.189.150, l'amo: *«L'app "Lock" també ha de sortir, a la línia d'icones de Home, Settings, Back i Close»*):
    # the key of the card that covers the glass, and the same address in the fifth family and in the seventh. The app
    # `device_lock` draws its own padlock with the primitives of a page (`tools/make_lock_icon.py`), which is the picture of an
    # icon of the launcher; a key of the card carries a mark of the clipart like the other seven (docs/45, docs/50).
    "lock": (0xF023, "lock: the screen is covered"),
    # **Les tres de la ràdio** (0.190.219, l'amo: *«Els botons aquests nous no se d'on els has tret pero no tenen res en comú. Busca al
    # fontawsome»*): the Bluetooth, the wifi and the mesh are glyphs of the same clipart as the other eight, o sigui que les nou marques
    # de la targeta surten de la mateixa família i es reconeixen com un joc. **La del Bluetooth és a la família `brands` i no a `solid`**
    # —Font Awesome té les marques comercials a part— i per això una entrada pot dir la seva família; les altres dues són `solid` i prou
    # (docs/45, docs/92).
    # **La runa sola i no la insígnia** (0.190.219): `U+F293` és el Bluetooth dins d'un quadrat rodó —la marca de l'empresa— i a la targeta
    # quedava com una taca fosca amb una runa blanca a dins, que no té res a veure amb la resta. `U+F294` és **la runa i prou**, o sigui la
    # mateixa silueta que el pinyó, la casa i el candau (docs/45, docs/92).
    "bluetooth": (0xF294, "bluetooth-b: the rune of the radio", "brands"),
    "wifi": (0xF1EB, "wifi: the fan of the network"),
    # **`share-alt` i no `network-wired`** (0.190.219): el primer era tres pantalles en un arbre —una xarxa amb jerarquia, que és el que és
    # una LAN i el que **no** és un mesh—, i a la targeta es llegia com un org-chart. `U+F1E0` és **tres nodes enllaçats entre ells**, sense
    # cap a dalt: és la forma d'una xarxa de parells, que és el que fa un mesh de Bluetooth, i pesa el mateix que la resta del joc
    # (docs/45, docs/92). Els altres que s'hi van mirar —`project-diagram`, `sitemap`, `broadcast-tower`, `connectdevelop`— o són arbres o
    # són massa dibuix per a una clau de setze mil·límetres.
    "mesh": (0xF1E0, "share-alt: three nodes joined to each other"),
    # **Les dues del joc** (0.192.26, l'amo: *«Substitueix els botons de New i Pause per icones corresponents»*): the pause and the
    # play of the three games of this system —Tetris, Snake i Mines—, que fins ara deien una paraula i es menjaven la meitat de la
    # franja del marcador. `U+F04C` és les dues barres i `U+F04B` el triangle, que és el parell que tot el món llegeix; la represa
    # és el triangle i no un altre dibuix, perquè el que el botó fa quan la partida està aturada és precisament tornar-hi (docs/45).
    "pause": (0xF04C, "pause: the game stops where it is"),
    "play": (0xF04B, "play: the game goes on"),
    # **El gràfic de la pàgina de les estadístiques** (0.192.36, l'amo: *«Revisa l'app nativa "Stats" del antic PaperOS … copia la seva
    # estètica»*): `chart-line` és la línia que puja dins d'uns eixos, que és el que la pàgina fa —el cost de cada comptador— i el que la
    # icona d'una app de números ha de dir en un quadrat de noranta-quatre. L'antic sistema la tenia oculta i s'obria per nom, o sigui que
    # no en tenia cap de dibuix; aquesta és nova (docs/45, docs/92).
    "stats": (0xF201, "chart-line: what every meter has cost"),
    # **Les dues de l'app de dibuix** (0.192.47, l'amo: *«Fes un cubell de pintura i un pinzell gros com a icona, usant l'estètica
    # predominant en les icones, posa-li detalls»*): el cubell és `fill-drip` —un poal que aboca la pintura i la gota que cau, que és
    # el dibuix que tothom llegeix com «omple això»— i el pinzell és `paint-brush`, el pinzell de pintor amb el mànec i la punta, que
    # és el traç i no la ploma d'escriure. Són marques del sistema i no dibuixos de l'app: la barra de l'app les crida pel nom com les
    # altres tretze, o sigui que el joc sencer de la targeta i les eines d'una app són els mateixos disset glifs (docs/45, docs/92).
    "bucket": (0xF576, "fill-drip: the bucket pours, and the drip says fill"),
    "brush": (0xF1FC, "paint-brush: the thick stroke of a painter"),
}


# **The second family** (0.190.219): Font Awesome has the commercial marks in a font of their own, o sigui que el Bluetooth no és a
# `fa-solid-900.ttf` i buscar-lo allà és un glif que no hi és. Cada camí es prova amb el seu nom i la marca diu de quina família és.
FONTS_BRANDS = (
    os.path.join(HERE, "fontawesome", "fa-brands-400.ttf"),
    "/home/sharek/mnt/node0_bak/node0/public/external/fontawesome-free-5.15.4-web/webfonts/fa-brands-400.ttf",
    "/usr/share/fonts/truetype/fontawesome/fa-brands-400.ttf",
)


def the_font(family: str = "solid") -> str:
    for path in (FONTS_BRANDS if family == "brands" else FONTS):
        if os.path.isfile(path):
            return path
    name = "fa-brands-400.ttf" if family == "brands" else "fa-solid-900.ttf"
    sys.exit(f"the clipart of the {family} family is not here: put {name} in tools/fontawesome/ "
             "(Font Awesome Free 5.15.4, SIL OFL 1.1)")


def glyph(code: int, px: int, family: str = "solid") -> Image.Image:
    """**La tinta del glif i prou**, retallada a la seva caixa. A symbol of a
    clipart family is not centred in the box of its font, and two marks put side
    by side with their boxes centred come out at two optical sizes (docs/45)."""
    face = ImageFont.truetype(the_font(family), px)
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
    code, _, *family = MARKS[name]
    art = glyph(code, 400, family[0] if family else "solid")
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
    # **`--only <name>` torna a fer una sola marca** (0.189.139): a new mark is added without regenerating the six that are
    # already on the disk and looked at by the owner —a font of another version would change their drawing without anybody
    # asking— (docs/45).
    parser.add_argument("--only", default=None, help="one mark of the set, and only it")
    args = parser.parse_args(argv)
    if args.only is not None and args.only not in MARKS:
        sys.exit(f"{args.only} is not a mark of the card: {', '.join(MARKS)}")

    for name in ([args.only] if args.only is not None else list(MARKS)):
        master = save(draw(name), os.path.join(args.masters, name + ".png"), SIZE)
        icon = save(draw(name), os.path.join(args.out, name + ".png"), args.size)
        print(f"{master}\n{icon}")
    if args.preview:
        print(contact_sheet("/tmp/iconart-sheet.png"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
