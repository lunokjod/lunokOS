#!/usr/bin/env python3
"""An EPUB out of a text file, so the reader has a book to read.

The reader of this system is a whole pipeline —a zip, a tokenizer, a paginator and a page painted in a
canvas— and a pipeline needs a book: this tool makes a small, legal EPUB out of a text that is already in
the repository, and the book it makes is committed beside it so a board can carry it without Python.

**The format, which is the least an EPUB can be**: the `mimetype` first and **stored**, the container that
points at the OPF, the OPF with the manifest and the spine, and one XHTML per chapter. No CSS, no fonts and
no cover: the reader of this house ignores the CSS and has its own faces (docs/46, A2).

The source is a text file with chapters: a line that starts with `## ` opens one and gives its title, and
everything under it is the chapter. Blank lines separate paragraphs.

    python3 tools/make_epub.py --source tools/epub-sources/la-vaca-cega.txt \\
        --out data/epub/la-vaca-cega.epub --title "La vaca cega" --author "Joan Maragall"
"""

import argparse
import io
import os
import re
import sys
import zipfile

# **The bytes of an XHTML file of this house**, and what is escaped in it: a chapter of a book is text with
# `&`, `<` and `>` in it, and nothing else is our business.
ESCAPES = (("&", "&amp;"), ("<", "&lt;"), (">", "&gt;"))


def escape(text):
    for raw, safe in ESCAPES:
        text = text.replace(raw, safe)
    return text


def chapters_of(path):
    """The chapters of the source: (title, [paragraphs])."""
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    out = []
    title = None
    paragraphs = []
    block = []

    def close_block():
        if block:
            paragraphs.append(" ".join(block))
            del block[:]

    for line in lines:
        if line.startswith("## "):
            close_block()
            if title is not None:
                out.append((title, paragraphs))
            title = line[3:].strip()
            paragraphs = []
            continue
        if title is None:
            continue
        if line.strip() == "":
            close_block()
            continue
        block.append(line.strip())

    close_block()
    if title is not None:
        out.append((title, paragraphs))

    return out


def xhtml(title, paragraphs, figure=False):
    parts = []
    if figure:
        parts.append('<p><img src="img/plate.png" alt="figure"/></p>')
    parts.extend("<p>%s</p>" % escape(one) for one in paragraphs if one)
    body = "\n".join(parts)
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        "<head><title>%s</title></head>\n"
        "<body>\n<h1>%s</h1>\n%s\n</body>\n</html>\n" % (escape(title), escape(title), body)
    )


def container():
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
        "  <rootfiles>\n"
        '    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>\n'
        "  </rootfiles>\n"
        "</container>\n"
    )


def opf(title, author, names):
    items = "\n".join(
        '    <item id="c%d" href="%s" media-type="application/xhtml+xml"/>' % (i + 1, one)
        for i, one in enumerate(names)
    )
    spine = "\n".join('    <itemref idref="c%d"/>' % (i + 1) for i in range(len(names)))
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="book">\n'
        '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        '    <dc:title>%s</dc:title>\n'
        '    <dc:creator>%s</dc:creator>\n'
        '    <dc:language>ca</dc:language>\n'
        '    <dc:identifier id="book">lunok-book-%s</dc:identifier>\n'
        "  </metadata>\n"
        "  <manifest>\n%s\n  </manifest>\n"
        "  <spine>\n%s\n  </spine>\n"
        "</package>\n"
        % (escape(title), escape(author), re.sub(r"[^a-z0-9]+", "-", title.lower()), items, spine)
    )


def figure():
    """A plate for the book of the sample: a frame, a cross and a line, at the size of a figure.

    It is drawn here and not copied from anywhere, which is the only way a repository can carry a picture
    without carrying somebody's.
    """
    from PIL import Image, ImageDraw

    plate = Image.new("L", (240, 150), 255)
    pen = ImageDraw.Draw(plate)
    pen.rectangle((0, 0, 239, 149), outline=0)
    pen.rectangle((6, 6, 233, 143), outline=0)
    pen.line((20, 120, 120, 30), fill=0, width=3)
    pen.line((120, 30, 220, 120), fill=0, width=3)
    pen.ellipse((100, 60, 140, 100), outline=0, width=3)
    return plate


def main():
    parser = argparse.ArgumentParser(description="An EPUB out of a text file")
    parser.add_argument("--source", required=True, help="the text, with `## ` opening a chapter")
    parser.add_argument("--out", required=True, help="the .epub to write")
    parser.add_argument("--title", required=True)
    parser.add_argument("--author", default="unknown")
    parser.add_argument("--figure", action="store_true", help="put a plate at the front of the first chapter")
    args = parser.parse_args()

    chapters = chapters_of(args.source)
    if not chapters:
        sys.stderr.write("the source has no chapter: a line that starts with '## ' opens one\n")
        return 1

    # **The name of a file of a chapter is its number**, because a title is a sentence and a file name is not.
    names = ["ch%02d.xhtml" % (i + 1) for i in range(len(chapters))]

    directory = os.path.dirname(args.out)
    if directory:
        os.makedirs(directory, exist_ok=True)

    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as book:
        # The `mimetype` is the first entry and it is **stored**: a reader looks for it without ungzipping.
        book.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", zipfile.ZIP_STORED)
        book.writestr("META-INF/container.xml", container())
        book.writestr("OEBPS/content.opf", opf(args.title, args.author, names))
        # **The picture of the sample is drawn here**, and the chapter that holds it says so with an `<img>`,
        # which is the only thing the reader of this house takes from a picture (docs/46, A2).
        if args.figure:
            plate = io.BytesIO()
            figure().save(plate, format="PNG")
            book.writestr("OEBPS/img/plate.png", plate.getvalue())

        for index, (name, (title, paragraphs)) in enumerate(zip(names, chapters)):
            book.writestr("OEBPS/" + name, xhtml(title, paragraphs, args.figure and index == 0))

    size = os.path.getsize(args.out)
    words = sum(len(" ".join(p).split()) for _, p in chapters)
    print("%s: %d chapters, %d words, %d bytes" % (args.out, len(chapters), words, size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
