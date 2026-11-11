#!/usr/bin/env python3
"""Turn a picture into the plane of the glass: four bits a pixel and no header.

    tools/e4pack.py data/res/splash/boot-2.bmp build/disk/m5-paper/res/splash/boot-2.e4
    tools/e4pack.py --page 540x912 data/res/splash build/disk/m5-paper/res/splash

**The plane is what the controller's own image is**: one byte for two pixels, the left one in the high nibble,
`width / 2` bytes a row and `height` rows, with no header, no palette and no compression. It is the one format a
splash can be read into and blitted with no arithmetic at all —one copy of a row per row— and it is **half the bytes
of the BMP of the same picture** (246 240 against 519 478 for 540 by 912 on the Paper).

**The arithmetic here is the decoder's and not a second one** (`lib/img/src/img.cpp`, docs/30): the grey of a pixel is
the 601 luminance `(77 r + 150 g + 29 b + 128) >> 8`, and the level is the four-by-four ordered dither
`(grey * 15 + bayer[y & 3][x & 3] * 16) / 255`. So a picture packed by this tool is the picture the system would have
decoded, pixel for pixel; what changes is that the panel no longer pays for it.

A picture that is not the size of the page is placed like `splash.cpp` places it: **the middle of it on the paper**,
which is the paper's white (level 15) around a picture that fits, and the middle cut out of one that does not. The
whole page is written, so the file always fits the glass it was packed for and `splash.cpp` refuses one that does not.

What it reads is the BMP a drawing program writes —eight bits with a palette, twenty-four or thirty-two, uncompressed—
because that is what the splashes of this disk are. PNG and JPEG are not read here: they stay in the image and the
system decodes them the way it always did (docs/50).
"""

import os
import struct
import sys

BAYER = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)


def grey_of(r, g, b):
    """The 601 luminance of the decoder, in integers."""
    return (77 * r + 150 * g + 29 * b + 128) >> 8


def level_of(grey, x, y):
    """The level of the decoder: the sixteen thresholds of the ordered dither, added and not rounded."""
    level = (grey * 15 + BAYER[y & 3][x & 3] * 16) // 255
    return 15 if level > 15 else level


def read_bmp(path):
    """The pixels of an uncompressed BMP, as (width, height, rows of (r, g, b)) top row first."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if compression != 0:
        raise ValueError("%s is compressed, and only an uncompressed BMP is read here" % path)
    if bits not in (8, 24, 32):
        raise ValueError("%s has %d bits a pixel, and eight, twenty-four or thirty-two are the ones read here"
                         % (path, bits))
    top_down = height < 0
    height = abs(height)
    palette = []
    if bits == 8:
        colours = struct.unpack_from("<I", data, 46)[0] or 256
        base = 14 + struct.unpack_from("<I", data, 14)[0]
        for index in range(colours):
            b, g, r = data[base + index * 4], data[base + index * 4 + 1], data[base + index * 4 + 2]
            palette.append((r, g, b))
    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for row in range(height):
        line = offset + row * stride
        pixels = []
        for column in range(width):
            if bits == 8:
                pixels.append(palette[data[line + column]])
            elif bits == 24:
                b, g, r = data[line + column * 3], data[line + column * 3 + 1], data[line + column * 3 + 2]
                pixels.append((r, g, b))
            else:
                b, g, r = data[line + column * 4], data[line + column * 4 + 1], data[line + column * 4 + 2]
                pixels.append((r, g, b))
        rows.append(pixels)
    if not top_down:
        rows.reverse()
    return width, height, rows


def pack(path, page_w, page_h):
    """The page of the glass as the controller has it: (bytes, width, height)."""
    width, height, rows = read_bmp(path)
    stride = (page_w + 1) // 2
    plane = bytearray(b"\xff" * (stride * page_h))  # the paper is white, level 15 everywhere
    at_x = (page_w - width) // 2
    at_y = (page_h - height) // 2
    for y in range(height):
        target = at_y + y
        if target < 0 or target >= page_h:
            continue
        for x in range(width):
            column = at_x + x
            if column < 0 or column >= page_w:
                continue
            level = level_of(grey_of(*rows[y][x]), x, y)
            at = target * stride + column // 2
            if column & 1:
                plane[at] = (plane[at] & 0xF0) | level
            else:
                plane[at] = (plane[at] & 0x0F) | (level << 4)
    return bytes(plane), page_w, page_h


def page_of(text):
    try:
        width, height = text.lower().split("x")
        return int(width), int(height)
    except (AttributeError, ValueError):
        raise SystemExit("e4pack: the page is <width>x<height>, as 540x912")


def main(argv):
    page_w, page_h = 540, 912
    args = []
    index = 1
    while index < len(argv):
        if argv[index] == "--page":
            index += 1
            if index >= len(argv):
                raise SystemExit("e4pack: --page wants a size, as 540x912")
            page_w, page_h = page_of(argv[index])
        else:
            args.append(argv[index])
        index += 1
    if len(args) == 2 and os.path.isdir(args[0]):
        source, target = args
        packed = 0
        for name in sorted(os.listdir(source)):
            if not name.lower().endswith(".bmp"):
                continue
            plane, width, height = pack(os.path.join(source, name), page_w, page_h)
            target_path = os.path.join(target, name[:-4] + ".e4")
            with open(target_path, "wb") as handle:
                handle.write(plane)
            packed += 1
            print("e4pack: %s (%dx%d) -> %s, %d B of a %dx%d page" %
                  (name, width, height, os.path.basename(target_path), len(plane), page_w, page_h))
        if packed == 0:
            raise SystemExit("e4pack: there is no .bmp in %s" % source)
        return 0
    if len(args) != 2:
        raise SystemExit("usage: e4pack.py [--page WxH] <picture.bmp> <plane.e4>\n"
                         "       e4pack.py [--page WxH] <folder> <folder>")
    plane, width, height = pack(args[0], page_w, page_h)
    with open(args[1], "wb") as handle:
        handle.write(plane)
    print("e4pack: %s -> %s, %d B of a %dx%d page" % (args[0], args[1], len(plane), width, height))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
