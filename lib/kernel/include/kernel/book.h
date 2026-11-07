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

// **One page of the chapter that is open**, laid out from a byte into a box of that size. A page is a plan (see
// `epub/page.h`) and it is not painted here: whoever composes it walks the items.
bool page(int32_t from, int32_t width, int32_t height, const epub::Style& style, const Fonts& fonts, Page& out);

// **The table the door of Lua asks for** (0.132.0): the calls of a book as the interpreter wants them, and a page
// that is both laid out and painted in the canvas of an app, which is the one call of this file that needs the UI.
void bind();

}  // namespace book
}  // namespace kernel

#endif  // LUNOK_KERNEL_BOOK_H
