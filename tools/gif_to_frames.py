#!/usr/bin/env python3
"""Converteix un GIF animat en els frames que una app pot pintar, un PNG per frame.

El sistema llegeix un GIF i en pinta **el primer frame** (`lib/img/src/gif.cpp`: una pàgina d'e-ink mostra una
imatge i no una animacio), o sigui que una animacio al vidre es una sequencia de fitxers i un bucle a l'app.
Aixo es el que escriu aquesta eina:

    python3 tools/gif_to_frames.py tools/icons/system/loading_bounce_500x500.gif --out apps/loading/res/anim

Els PNG surten **de setze nivells de gris** perque el panell en te setze i el descodificador no hagi de fer la
conversio a cada frame, i amb el fons del GIF ja posat (una transparencia composta sobre paper).
"""
import argparse
import os

from PIL import Image, ImageSequence


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("gif")
    ap.add_argument("--out", required=True)
    ap.add_argument("--max", type=int, default=0, help="el costat més llarg, si es vol reduir (0 és la mida del GIF)")
    ap.add_argument("--prefix", default="f")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    gif = Image.open(args.gif)
    frames = 0
    total = 0
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
        # Setze nivells: el panell en té setze i el que es veu al vidre és aquest número i no el del fitxer.
        grey = grey.quantize(colors=16).convert("L")
        path = os.path.join(args.out, f"{args.prefix}{index + 1:02d}.png")
        grey.save(path, optimize=True)
        size = os.path.getsize(path)
        total += size
        frames += 1
    print(f"{args.out}: {frames} frames, {total} B ({total // 1024} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
