// **One page of a book, composed** (docs/46, A3): the second half of the reader.
//
// The tokenizer gives words, blocks and pictures; this puts them on the paper —measures every word with the
// face of its block, wraps it into a line, gives a floated picture a band for the words to flow around, and
// says where every one of them goes—.
//
// **A page is a PLAN and not a picture** (0.130.0), and that is the one decision of this file that the paper
// project did differently. There, the layout painted into a canvas as it went, because its canvas was a
// `Canvas4` of one bit and a glyph was a bitmap it blitted. Here the painting of a text is `font::runs` and
// `ttf`, which live in the kernel and know about faces, scales and ink, so the paginator computes **where**
// and the kernel does **what**: a page comes out as a list of items —a word at (x, y) with a face, a size and
// an ink, a rule, a picture in its box— and whoever shows it walks the list. Three things come from that:
//
//   - **measuring a page is nothing**: it is the same list, walked by nobody (docs/46, B4);
//   - **the page can be tested without a screen**, with a ruler of a face that says every letter is six
//     pixels wide, which is how the arithmetic of a line and of a table is proved on the computer;
//   - and the memory is the caller's, like everything else in this system: the list, and **the words
//     themselves**, because a word of a chapter is a slice of a buffer that is not ended by a zero.
//
// The contract with the caller is the paper's, and it is a byte: a page is laid out **from the byte it is
// given**, and when it does not fit it says the byte the next page starts at. That is the position of a
// reader (docs/46, §1).
#ifndef LUNOK_EPUB_PAGE_H
#define LUNOK_EPUB_PAGE_H

#include <stdint.h>

#include "epub/html.h"

namespace epub {

// **The three roles a face has in a book** —the words, the titles and the caption of a figure— and not three
// faces: which typeface is behind each one is the caller's (docs/46, C13).
enum Face : uint8_t {
  kFaceBody = 0,
  kFaceTitle,
  kFaceSmall,
};

// **The ink of one word**, in three bits, taken from the token so that whoever paints does not have to look at
// the chapter again: a bold is `1 << 0`, an italic `1 << 1`, and the step off the baseline is `1 << 2` up (a
// `sup`) and `2 << 2` down (a `sub`) (docs/46, A4).
enum InkBit : uint8_t {
  kInkBold = 1u << 0,
  kInkItalic = 1u << 1,
  kInkRiseMask = 3u << 2,
  kInkRiseUp = 1u << 2,
  kInkRiseDown = 2u << 2,
};

// **One thing a page puts on the paper.**
//
// `text` is an **offset inside the page's own pool of words** and not a pointer, because the pool is the
// caller's and a page may be walked after the layout returned: a word is a few bytes and a copy of them costs
// less than a rule about who lives longer.
struct Item {
  enum Kind : uint8_t {
    kWord = 0,   // a word, or a bullet and its word
    kRule,       // a hairline across the page, or under a header of a table
    kPicture,    // a box where a picture goes; the href is in the pool, ended by a zero
  };

  Kind kind = kWord;
  int32_t text = 0;  // an offset into `Sheet::words`
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;  // a picture: the width of its box. A rule: how far it goes.
  int16_t h = 0;  // a picture: the height of its box. A rule: one row.
  uint8_t face = kFaceBody;
  uint8_t scale = 0;
  uint8_t ink = 0;  // `InkBit`
};

// **The blocks a caller gives the page** —the list of items and the pool of the words— and nothing else: the
// paginator does not allocate, and it stops the page at a word when either of the two is full, saying the byte
// of that word as the start of the next one (docs/46, A7).
struct Sheet {
  Item* items = nullptr;
  int32_t items_cap = 0;
  char* words = nullptr;
  int32_t words_cap = 0;
};

// **What a page says of itself when it is done.** `count` items were put in the sheet; when `ended` the
// chapter is over; otherwise `next` is the byte the next page starts at; `used` is the paper it took.
struct Result {
  int16_t count = 0;
  bool ended = false;
  bool has_picture = false;
  int32_t next = 0;
  int32_t used = 0;
};

// **How the book looks**: the size of its three roles and the inset of a quotation, a figure or a definition.
// The sizes are the steps of a face (`scale`), which is what this system has instead of a size in points.
struct Style {
  uint8_t body_scale = 4;
  uint8_t title_scale = 5;
  uint8_t small_scale = 3;
  // The inset of a `blockquote`, a `figure` and a `dd`, in pixels: one finger of air, which is the paper's.
  int16_t indent = 24;
};

// **What a page asks of a face**: how wide a text is and how tall a line is. The text is ended by a zero
// because it is the page's own copy, and the face and the step are what the caller turns into one of its
// typefaces (docs/30).
struct Metrics {
  void* ctx = nullptr;
  int32_t (*text_width)(void* ctx, uint8_t face, uint8_t scale, const char* text) = nullptr;
  int32_t (*line_height)(void* ctx, uint8_t face, uint8_t scale) = nullptr;
};

// **What a page asks of the pictures of a book**: the box a picture takes inside a box, which is the one
// question that needs the file decoded. A page that has no pictures takes null here.
struct Pictures {
  void* ctx = nullptr;
  bool (*measure)(void* ctx, const char* href, int32_t max_w, int32_t max_h, int32_t& w, int32_t& h) = nullptr;
};

// The share of the width a floated picture may take, in percent, and the air between a picture and the words.
const int16_t kFloatShare = 45;
const int16_t kPictureGap = 10;
// The columns of a table this page knows how to draw, and how far it looks ahead for them: a table of a book is
// small, and a chapter that is one huge table must not be walked to its end twice for every page.
const int16_t kMaxColumns = 6;
const int16_t kProbeTokens = 4000;

// **Composes one page** from the byte the tokenizer is at. Every word goes into the sheet with its place, and
// whatever does not fit is the next page's, saying its byte.
//
// False only when a page cannot be composed at all —a sheet with no room, or a box of less than sixteen pixels
// in either direction—: then the caller has nothing to show and must not paint.
bool layout(Tokens& tokens, int32_t width, int32_t height, const Style& style, const Metrics& metrics,
            const Pictures* pictures, const Sheet& sheet, Result& result);

}  // namespace epub

#endif  // LUNOK_EPUB_PAGE_H
