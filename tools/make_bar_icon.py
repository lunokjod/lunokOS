#!/usr/bin/env python3
"""Extreu un glif d'una font d'icones i l'escriu com una taula de bits per a la barra del sistema.

Les icones de la barra son dibuixos de linies (`lib/kernel/src/bar_icons.cpp`) i aixo es el que es vol: una
corba que es pot llegir i corregir. Aixo es per a la que **no** es un dibuix de linies: el rellotge de dormitori
de l'alarma, que es una corba composta i que a vint pixels no te sentit tornar a dibuixar a ma (l'amo va veure
tres intents i cap no hi era).

    python3 tools/make_bar_icon.py --font /usr/share/fonts/truetype/noto/NotoColorEmoji.ttf \
        --char 23f0 --height 21 --name "U+23F0 ALARM CLOCK" --font-name "Noto Color Emoji" \
        --licence "La font es SIL OFL 1.1 i el codi Apache 2.0."

Tambe serveix per a una font de bitmaps d'una sola mida (Noto Color Emoji nomeś en te una): el dibuix es fa
allí i es baixa a l'alçada que es demana, que es el que fa llegible una emoji de cent pixels en vint.

Escriu `lib/kernel/src/bar_icons_art.h`, que es l'unic que llegeix aquesta eina i que no s'edita a ma.
"""
import argparse
import sys

from PIL import Image, ImageDraw, ImageFont

HEADER = """// **Generat per `tools/make_bar_icon.py` i no s'edita a ma** (0.189.58): el glif `{name}` (U+{code:04X}) de
// **{font_name}**, rasteritzat a {height} px d'alçada i retallat a la seva tinta —{w} x {h}—. {licence} L'atribució viu aquí
// i a docs/50.
//
// **Per que una taula de bits i no un dibuix de linies**: la resta d'icones de la barra són cinc línies i tres arcs que es
// corregeixen llegint-les, i aquesta és una corba composta que a vint píxels no es pot tornar a dibuixar millor que la font.
// Les mides les publica `ink_of()` com les de tots els altres dibuixos, així que la barra la col·loca igual.
#ifndef LUNOK_KERNEL_BAR_ICONS_ART_H
#define LUNOK_KERNEL_BAR_ICONS_ART_H

#include <stdint.h>

namespace kernel {{
namespace bar_icons {{
namespace art {{

// {w} x {h} píxels, una fila darrere l'altra, un bit per píxel i el bit de més a l'esquerra és el de l'esquerra.
const uint8_t kAlarmW = {w};
const uint8_t kAlarmH = {h};
const uint8_t kAlarmBits[{nbytes}] = {{
{body}}};

}}  // namespace art
}}  // namespace bar_icons
}}  // namespace kernel

#endif  // LUNOK_KERNEL_BAR_ICONS_ART_H
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf")
    ap.add_argument("--char", default="23f0", help="el codepoint que es vol, en hexadecimal (per defecte el rellotge de dormitori)")
    ap.add_argument("--height", type=int, default=21, help="l'alçada de la tinta, en píxels")
    ap.add_argument("--name", default="U+23F0 ALARM CLOCK", help="el nom del glif, per a l'atribució")
    ap.add_argument("--font-name", default="Noto Color Emoji", help="la font, per a l'atribució")
    ap.add_argument("--licence", default="La font és SIL OFL 1.1 i el codi Apache 2.0 (https://github.com/googlefonts/noto-emoji).",
                    help="la llicència, per a l'atribució")
    ap.add_argument("--out", default="lib/kernel/src/bar_icons_art.h")
    args = ap.parse_args()

    code = int(args.char, 16)
    import numpy as np

    def natural_size():
        """La mida única d'una font de bitmaps, o 0 si la font és escalable (Noto Color Emoji només té els seus 109 píxels i en
        refusa qualsevol altre; una font de contorns en dibuixa qualsevol)."""
        try:
            ImageFont.truetype(args.font, 40)
            return 0
        except OSError:
            pass
        for size in range(8, 300):
            try:
                ImageFont.truetype(args.font, size)
                return size
            except OSError:
                continue
        return 0

    bitcoin = natural_size()

    def render(size):
        face = ImageFont.truetype(args.font, size)
        canvas = Image.new("L", (size * 3, size * 3), 255)
        ImageDraw.Draw(canvas).text((size, size), chr(code), font=face, fill=0)
        return np.array(canvas) < 128

    def reduce_to(bits, height):
        """La meitat de cada bloc, que és el que fa llegible un dibuix de cent píxels posat en vint."""
        h, w = bits.shape
        scale = h / height
        out_w = max(1, int(round(w / scale)))
        out = np.zeros((height, out_w), dtype=bool)
        for y in range(height):
            for x in range(out_w):
                y0 = int(y * scale)
                y1 = max(int((y + 1) * scale), y0 + 1)
                x0 = int(x * scale)
                x1 = max(int((x + 1) * scale), x0 + 1)
                out[y, x] = bits[y0:y1, x0:x1].mean() > 0.45
        return out

    # **El cos es busca, no es demana**: el que mana és l'alçada de la TINTA (la campana ocupa tot el cos i el rellotge no), i
    # el cos més petit que arriba a l'alçada demanada és el que la deixa més fina.
    pixels = None
    for size in (range(bitcoin, bitcoin + 1) if bitcoin else range(8, 64)):
        candidate = render(size)
        ys = np.where(candidate.any(axis=1))[0]
        if len(ys) and (ys.max() - ys.min() + 1) >= args.height:
            if not bitcoin:
                # Una font escalable: el cos de sota, si la seva tinta encara hi arriba, és el que deixa el dibuix més fi.
                tiny = render(size - 1)
                ys_tiny = np.where(tiny.any(axis=1))[0]
                if len(ys_tiny) and (ys_tiny.max() - ys_tiny.min() + 1) == args.height:
                    candidate = tiny
            pixels = candidate
            break
    if pixels is None:
        print("el glif no ha arribat a l'alçada demanada", file=sys.stderr)
        return 1
    if bitcoin:
        # **Una font de bitmaps té una sola mida** i el dibuix es fa allí i es baixa aquí: el que mana és l'alçada de la tinta.
        ys = np.where(pixels.any(axis=1))[0]
        xs = np.where(pixels.any(axis=0))[0]
        pixels = reduce_to(pixels[ys.min():ys.max() + 1, xs.min():xs.max() + 1], args.height)
    rows = [np.where(pixels[y])[0] for y in range(pixels.shape[0])]
    rows = [r for r in rows if len(r)]
    y0 = next(y for y in range(pixels.shape[0]) if pixels[y].any())
    y1 = pixels.shape[0] - next(y for y in range(pixels.shape[0] - 1, -1, -1) if pixels[y].any()) - 1
    x0 = min(r.min() for r in rows)
    x1 = pixels.shape[1] - max(r.max() for r in rows) - 1
    ink = pixels[y0:pixels.shape[0] - y1, x0:pixels.shape[1] - x1]

    h, w = ink.shape
    if h != args.height:
        print(f"avís: la tinta ha sortit de {h} files i no de {args.height}", file=sys.stderr)

    bits = bytearray((w * h + 7) // 8)
    for y in range(h):
        for x in range(w):
            if ink[y, x]:
                i = y * w + x
                bits[i // 8] |= 1 << (7 - i % 8)

    body = ""
    for i in range(0, len(bits), 12):
        chunk = ", ".join(f"0x{b:02X}" for b in bits[i:i + 12])
        body += f"    {chunk},\n"

    with open(args.out, "w") as f:
        f.write(HEADER.format(name=args.name, code=code, height=args.height, w=w, h=h, body=body,
                              nbytes=len(bits), font_name=args.font_name, licence=args.licence))
    print(f"{args.out}: el glif U+{code:04X} a {w} x {h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
