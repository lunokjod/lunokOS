// **A book open** (docs/46, A5): the file, its table of entries, and the one chapter that is in memory.
//
// The reader of a book is three pieces that already exist —the zip container (`lib/zip`), the tokenizer and the
// paginator (`lib/epub`)— and this is the one that holds them together and gives them a file: it opens a `.epub`
// from a mount, reads its central directory, inflates a chapter into the extra memory of the board and lays out a
// page of it from the byte the reader was at.
//
// **One book at a time**, like the zip and for the same reason: the blocks are the board's and the reader of this
// system reads one book. A second `open` closes the first.
//
// **The blocks are the book's and they are counted**: a chapter up to half a megabyte, its compressed stream
// beside it, the table of entries and the sheet of a page. They come from `pal::extra_memory()` —the PSRAM of the
// Paper— and they leave when the book is closed, so a book that cannot be opened leaves the memory as it found it
// (docs/46, A7).
#ifndef LUNOK_KERNEL_BOOK_H
#define LUNOK_KERNEL_BOOK_H

#include <stdint.h>

#include "epub/page.h"
#include "zip/zip.h"

namespace kernel {

namespace font {
struct Face;
}

namespace book {

// **The ceilings of one book**, in a header because they are the size of the blocks the book takes: a chapter of a
// book is a few hundred kilobytes and a file of a book is a few dozen entries, so these are ceilings and not
// promises, and a book that does not fit says so instead of taking the whole of the extra memory.
const uint32_t kChapterBytes = 512u * 1024u;
const uint16_t kEntries = 128;
const int16_t kItems = 256;   // the words and the rules of one page
const int16_t kWords = 4096;  // the letters and the hrefs of one page
// **The blocks of a picture**, asked for the first time a book has one: the file as it comes out of the zip and the
// pixels in the box the page leaves for it. A page of the Paper is 540 × 912 at four bits a pixel, which is the size
// of the second one, and a picture of a book that does not fit here is not a picture of a page (docs/46, A5).
const uint32_t kPictureFileBytes = 256u * 1024u;
const uint32_t kPicturePixels = 540u * 960u / 2u;

// **The three faces a page of a book is drawn with** (docs/46, C13): the words, the titles and the caption of a
// figure. A null is the face of the system, so a book that was given no face of its own still reads.
struct Fonts {
  const font::Face* body = nullptr;
  const font::Face* title = nullptr;
  const font::Face* small = nullptr;
};

// **What a page of the book says when it is laid out**, and the two blocks where the items and the words are:
// they stay valid until the next page or the close.
struct Page {
  int16_t count = 0;
  bool ended = false;
  bool has_picture = false;
  int32_t next = 0;
  int32_t used = 0;
  const epub::Item* items = nullptr;
  const char* words = nullptr;
};

// **Opens the book at an absolute path** —`/littlefs/epub/a.epub`, `/sd/epub/a.epub`— and reads its table of
// entries. nullptr is a book open; anything else is the reason it is not, said in the words of a person.
const char* open(const char* path);
void close();
bool is_open();

// The entries of the book, by index or by name. `find` is -1 when the book does not carry that name.
int32_t entries();
const zip::Entry* entry(int32_t index);
int32_t find(const char* name);

// **The books of the machine**, which are the `.epub` of `/epub` on the flash and on the card —the folders this
// system keeps a book in— (docs/46, C1). The reader asks its own door and not the file system of the app: the folder
// is the system's, and an app that could list the flash would be an app with a key to every room.
int32_t books();
const char* book_at(int32_t index);

// **The first entry that looks like a chapter**, by the order of `epub::Tokens` of docs/46 C2 —an `.xhtml`,
// `.html` or `.htm` that is not inside `META-INF`—, or -1. It is what a reader opens when it cannot read the OPF.
int32_t first_chapter();

// **The chapter, inflated into the block of the book**, and its bytes. nullptr is a name the book does not carry
// or a chapter that does not fit.
const char* chapter(const char* name, int32_t& bytes);
const char* chapter_at(int32_t index, int32_t& bytes);

// **A small file of the book as it comes out of the zip** (0.142.0, docs/46 B6): the container that names the OPF,
// the OPF itself, the index and a stylesheet are kilobytes, and an app reads them whole to know the title of the
// book, the order of the spine and the cover. `data` is a block of the book and stays valid until the next read or
// the close, which is what the door of Lua wants to copy it once. False is a name the book does not carry or a
// file too big for the block.
const uint32_t kMetaBytes = 64u * 1024u;
bool read(const char* name, const char** data, uint32_t& size);

// **The cache of a book, written beside the book** (0.146.0, docs/46 C11): the picture `name` —the cover— scaled to
// `max` pixels on its longest side and written as an `ICS1` next to the `.epub`, hidden and with the name of the book
// inside it, so whoever lists the folder has the face of the book without opening it. **A cache that is already there
// is not written again** and a flash with no room is not written on: both of those are `true`, because there is nothing
// to come back to. `false` is a picture that cannot be read or a write that did not finish.
const int32_t kThumbMax = 256;
bool thumbnail(const char* name, int32_t max);

// **The face a book carries inside itself** (0.150.0, docs/46 D6): the bytes of a TrueType font, either because the
// file of the book is one or because it was a `.woff` and the box has been opened. `data` is a block of the book and
// stays valid until the next call that reads one of its files; `why` says what went wrong when the answer is false —the
// book carries no such file, a `wOF2` (Brotli, which this system does not have), a face of more than 128 KB or a table
// that does not come out of its compression. What the face is *made of* is `ui.load_font`'s business: here there are
// bytes and nothing else (docs/46, D6).
const uint32_t kFontFileBytes = 128u * 1024u;
const uint32_t kFontSfntBytes = 512u * 1024u;
bool font(const char* name, const uint8_t** data, uint32_t& size, const char*& why);

// **One page of the chapter that is open**, laid out from a byte into a box of that size. A page is a plan (see
// `epub/page.h`) and it is not painted here: whoever composes it walks the items.
bool page(int32_t from, int32_t width, int32_t height, const epub::Style& style, const Fonts& fonts, Page& out);

// **The table the door of Lua asks for** (0.132.0): the calls of a book as the interpreter wants them, and a page
// that is both laid out and painted in the canvas of an app, which is the one call of this file that needs the UI.
void bind();

}  // namespace book
}  // namespace kernel

#endif  // LUNOK_KERNEL_BOOK_H
