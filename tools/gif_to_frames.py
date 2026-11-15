#!/usr/bin/env python3
"""Converteix un GIF animat en els frames que una app pot pintar, un PNG per frame.

El sistema llegeix un GIF i en pinta **el primer frame** (`lib/img/src/gif.cpp`: una pàgina d'e-ink mostra una
imatge i no una animacio), o sigui que una animacio al vidre es una sequencia de fitxers i un bucle a l'app.
Aixo es el que escriu aquesta eina:

    python3 tools/gif_to_frames.py tools/icons/system/loading_bounce_500x500.gif --out apps/loading/res/anim

Els PNG surten amb el fons del GIF ja posat (una transparencia composta sobre paper) i amb **els nivells que el
dibuix te**: un GIF de blanc i negre surt d'un bit —que es el que vol una animacio, i el que fa que el PNG sigui
una desena part— i un de grisos, de setze. `--bits` ho forca.
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageSequence


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("gif")
    ap.add_argument("--out", required=True)
    ap.add_argument("--max", type=int, default=0, help="el costat més llarg, si es vol reduir (0 és la mida del GIF)")
    ap.add_argument("--prefix", default="f")
    ap.add_argument("--bits", type=int, choices=(1, 8), default=0,
                    help="1 o 8, forcat; per defecte el que el dibuix demana (dos nivells són un bit)")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    gif = Image.open(args.gif)
    frames = 0
    total = 0
    durations = []
    for index, frame in enumerate(ImageSequence.Iterator(gif)):
        # **El fons del GIF és paper**: a frame that carries a transparency is a frame that has to be put on top of the page,
        # and the page of this system is white (docs/30).
        flat = frame.convert("RGBA")
        paper = Image.new("RGBA", flat.size, (255, 255, 255, 255))
        paper.alpha_composite(flat)
        grey = paper.convert("L")
        if args.max > 0 and max(grey.size) > args.max:
            scale = args.max / max(grey.size)
            grey = grey.resize((max(1, int(grey.width * scale)), max(1, int(grey.height * scale))), Image.LANCZOS)
        # **Els nivells que el dibuix té i no els que el panell té**: a two-colour frame stored as sixteen levels is a file eight
        # times bigger and a decoder that dithers over a picture that was already black and white (0.189.60).
        bits = args.bits
        if bits == 0:
            spread = len(set(np.array(grey).flatten().tolist()))
            bits = 1 if spread <= 2 else 8
        if bits == 1:
            grey = grey.point(lambda value: 255 if value >= 128 else 0).convert("1")
        else:
            grey = grey.quantize(colors=16).convert("L")
        durations.append(frame.info.get("duration") or 0)
        path = os.path.join(args.out, f"{args.prefix}{index + 1:02d}.png")
        grey.save(path, optimize=True)
        size = os.path.getsize(path)
        total += size
        frames += 1
    # **El ritme és del GIF i no de l'app**: the duration of every frame is written next to the pictures so the loop asks
    # the file for its pace instead of carrying a number that somebody has to remember to change with the animation
    # (0.189.63). It is one line of text —the frames and the milliseconds of the first one— and a GIF that mixes durations
    # gets the first, with a line saying so.
    step = durations[0] if durations else 0
    if len(set(durations)) > 1:
        print(f"avis: el GIF barreja durades ({sorted(set(durations))}); el ritme serà el del primer frame", file=sys.stderr)
    with open(os.path.join(args.out, "anim.txt"), "w") as f:
        f.write(f"{frames} {step}\n")
    print(f"{args.out}: {frames} frames, {total} B ({total // 1024} KB), {step} ms per frame")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
