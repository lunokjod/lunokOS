#!/usr/bin/env python3
"""Els dos estats d'una casella, presos del clipart de Font Awesome i escrits com una taula de bits.

El `ui.checkbox` d'aquest sistema era un quadrat de dues línies de marc amb una X a dins, i l'amo el va trobar
*«massa bàsic»*: el que volia és **la càpsula d'un interruptor** —*«enlloc d'un checkbox clàssic vull un en forma de
càpsula, una banda buida i l'altre negre. Si es posa "Si" es tot negre si es "No" surt farcit de blanc»*— i el
caràcter és el de Font Awesome, `U+F204 TOGGLE OFF` i `U+F205 TOGGLE ON`, que són exactament aquestes dues imatges.

**No es dibuixa res a mà**, i és la mateixa decisió que el despertador de la barra (0.189.58): la càpsula és una corba
composta i a trenta-quatre píxels el que en surti d'escriure coordenades no és la imatge de ningú. Les dues taules les
escriu aquesta eina i llegeix `lib/kernel/src/toggle_mark.cpp`.

    python3 tools/make_toggle_mark.py --font /tmp/fa-solid-900.ttf

La font de Font Awesome 6 no és al sistema (la que hi ha instal·lada és la 4, que encara no portava el toggle) i el
fitxer es baixa una vegada:

    curl -sL -o /tmp/fa-solid-900.ttf \\
        https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/webfonts/fa-solid-900.ttf

Escriu `lib/kernel/src/toggle_mark_art.h`, que no s'edita a mà.
"""
import argparse
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HEADER = """// **Generat per `tools/make_toggle_mark.py` i no s'edita a mà** (0.189.90): els dos estats de la casella del sistema,
// **`U+F204 TOGGLE OFF` i `U+F205 TOGGLE ON` de {font_name}**, rasteritzats a {height} píxels d'alçada de tinta i retallats
// a la seva tinta —{w} x {h} tots dos, que és el que fa que la fila no es mogui quan la casella es gira—. {licence}
// L'atribució viu aquí i a docs/50.
//
// **Per què una taula de bits i no un dibuix de línies**: la càpsula d'un interruptor té dos caps rodons, un marc d'un
// píxel i un botó que és un forat, i a la mida d'una fila el que surt d'escriure-ho amb arcs no és el clipart de ningú.
// Les dues taules són el caràcter i prou (docs/30, docs/90).
#ifndef LUNOK_KERNEL_TOGGLE_MARK_ART_H
#define LUNOK_KERNEL_TOGGLE_MARK_ART_H

#include <stdint.h>

namespace kernel {{
namespace toggle_mark {{
namespace art {{

// {w} x {h} píxels, una fila darrere l'altra, un bit per píxel i el bit de més a l'esquerra és el de l'esquerra. **El
// blanc del botó és un zero**: la imatge d'encesa és la càpsula amb el botó foradat, i el paper de la fila es veu pel
// forat, que és el que fa que el botó sigui blanc sense que la taula hagi de saber de quin color és la pàgina.
const uint8_t kToggleW = {w};
const uint8_t kToggleH = {h};

// Apagada: el marc de la càpsula i el botó a l'esquerra.
const uint8_t kOffBits[{nbytes}] = {{
{off_body}}};

// Encesa: la càpsula tota de tinta i el botó foradat a la dreta.
const uint8_t kOnBits[{nbytes}] = {{
{on_body}}};

}}  // namespace art
}}  // namespace toggle_mark
}}  // namespace kernel

#endif  // LUNOK_KERNEL_TOGGLE_MARK_ART_H
"""

# Els dos estats del mateix caràcter: el nom de la taula que s'escriu i el codepoint del clipart.
STATES = [("off", 0xF204), ("on", 0xF205)]


def ink_of(pixels):
    """La tinta d'un dibuix i el que la rodeja, que és el que es llença."""
    ys = np.where(pixels.any(axis=1))[0]
    xs = np.where(pixels.any(axis=0))[0]
    return pixels[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


# **Quatre vegades més gran i baixat, que és el que fa els caps rodons rodons**: a la mida de la fila un píxel és un
# vuitè del radi del botó, i el que surt de rasteritzar-ho d'un cop són escales. Es dibuixa a quatre vegades i es baixa
# amb la mitjana de cada bloc, i el llindar de després és el que decideix el píxel (docs/50, docs/90).
SUPERSAMPLE = 4


def render(font_path, code, size):
    big = size * SUPERSAMPLE
    face = ImageFont.truetype(font_path, big)
    canvas = Image.new("L", (big * 3, big * 3), 255)
    ImageDraw.Draw(canvas).text((big, big), chr(code), font=face, fill=0)
    ink = np.array(canvas) < 128
    h = ink.shape[0] // SUPERSAMPLE
    w = ink.shape[1] // SUPERSAMPLE
    ink = ink[:h * SUPERSAMPLE, :w * SUPERSAMPLE]
    ink = ink.reshape(h, SUPERSAMPLE, w, SUPERSAMPLE).mean(axis=(1, 3)) > 0.45
    # **El gruix el fa la mida i res més** (0.189.90, l'amo: *«res de bold»* i *«es veuen fils»*): the glyph is drawn
    # once, at the height asked for, and what thickens it is a smaller body asked for a bigger picture —the strokes of the
    # character grow with it—. **Engreixar-lo després, píxel a píxel, deixa fils**: a pixel of ink on each side closes the
    # gap between the knob and the band, and the two pixels that are left of it come out as a white thread inside the
    # black (seen on the emulator). The drawing is Font Awesome's and this tool only says how big it is (docs/50).
    return ink


def table(ink, w, h):
    """Una imatge posada a la caixa comuna, amb el blanc de sota a la dreta, i els seus bits."""
    placed = np.zeros((h, w), dtype=bool)
    placed[:ink.shape[0], :ink.shape[1]] = ink
    bits = bytearray((w * h + 7) // 8)
    for y in range(h):
        for x in range(w):
            if placed[y, x]:
                at = y * w + x
                bits[at // 8] |= 1 << (7 - at % 8)
    return bits


def body_of(bits):
    out = ""
    for i in range(0, len(bits), 12):
        out += "    " + ", ".join(f"0x{b:02X}" for b in bits[i:i + 12]) + ",\n"
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/tmp/fa-solid-900.ttf")
    ap.add_argument("--height", type=int, default=40, help="l'alçada de la tinta, en píxels")
    ap.add_argument("--font-name", default="Font Awesome 6 Free (Solid)")
    ap.add_argument("--licence",
                    default="Les icones són CC BY 4.0 i la font SIL OFL 1.1 (https://fontawesome.com).",
                    help="la llicència, per a l'atribució")
    ap.add_argument("--out", default="lib/kernel/src/toggle_mark_art.h")
    args = ap.parse_args()

    # **El cos es busca, no es demana**: el que mana és l'alçada de la TINTA, i el cos més petit que hi arriba és el que
    # deixa el dibuix més fi. Els dos estats es rasteritzen al mateix cos, o la fila es mouria en girar la casella.
    size = None
    for candidate in range(8, 96):
        if ink_of(render(args.font, STATES[0][1], candidate)).shape[0] >= args.height:
            size = candidate
            break
    if size is None:
        print("el clipart no ha arribat a l'alçada demanada", file=sys.stderr)
        return 1

    inks = {name: ink_of(render(args.font, code, size)) for name, code in STATES}
    for name, ink in inks.items():
        if ink.shape[0] != args.height:
            print(f"avís: {name} ha sortit de {ink.shape[0]} files i no de {args.height}", file=sys.stderr)
    # **Una sola caixa per als dos estats**: la imatge d'encesa és un píxel més ampla que la d'apagada, i una casella
    # que canvia de mida en girar-se mou la paraula que té al costat.
    h = max(ink.shape[0] for ink in inks.values())
    w = max(ink.shape[1] for ink in inks.values())
    bits = {name: table(ink, w, h) for name, ink in inks.items()}

    with open(args.out, "w") as f:
        f.write(HEADER.format(height=args.height, w=w, h=h, nbytes=len(bits["off"]),
                              off_body=body_of(bits["off"]), on_body=body_of(bits["on"]),
                              font_name=args.font_name, licence=args.licence))
    print(f"{args.out}: U+F204 i U+F205 a {w} x {h} (cos {size})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
