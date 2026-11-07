// **The text of the system, and the faces it can be drawn with** (docs/30).
//
// The system once had one font: a five by seven table in the flash, uppercase only, because a bar that says
// an hour and a title needs seventy characters and nothing else. That font is still here and it is still the
// one the bar uses, but it is now **one face of a kind**: a face is a table of glyphs and the one function
// that knows how its bits are laid out, so a five by seven packed as columns and an eight by eight read from
// a file are the same thing to everything that draws a text (docs/30).
//
// The file is a **portable font**: plain text, one glyph per block and `#` for ink, so it is authored in any
// editor on any machine and needs no decoder:
//
//     ; a lunok font, one glyph per block
//     lunok-font 1
//     width 5
//     height 7
//     gap 1
//     char 65
//     .###.
//     #...#
//     #####
//     #...#
//     #...#
//     #...#
//     #...#
//
// A face carries the glyphs it carries: a code it does not carry is drawn as a gap, and a file with four
// letters is a legal face (docs/30).
#ifndef LUNOK_KERNEL_FONT_H
#define LUNOK_KERNEL_FONT_H

#include <stdint.h>

#include "hal/hal.h"

// **A face of outlines is a TrueType file**, and its reader is a library of its own: this header points at it
// without knowing what is inside (docs/30).
namespace ttf {
struct Face;
}

namespace kernel {
namespace font {

const uint8_t kGlyphWidth = 5;
const uint8_t kGlyphHeight = 7;

// **How big a glyph of a face can be**: sixteen columns by sixteen rows, which is what a body of sixteen
// pixels of a rounded font needs to be legible, and it was eight until 0.55.0 —eight was all a five by seven
// doubled ever asked (docs/30).
const uint8_t kMaxGlyphWidth = 16;
const uint8_t kMaxGlyphHeight = 16;
const uint8_t kMaxGlyphs = 96;  // 0x20 to 0x7F

// **Two bytes for a row of a glyph**: sixteen columns do not fit in one. The block of a face is a row-major
// table —`index * kMaxGlyphHeight + row`, and then these bytes— and **the leftmost pixel is the highest bit of
// the first of them**, with the second byte holding the columns eight to fifteen (docs/30).
const uint8_t kGlyphRowBytes = 2;

// The block of glyphs an app hands over for a face it loads from a file, in bytes (96 glyphs of sixteen rows).
const uint32_t kFaceBytes =
    static_cast<uint32_t>(kMaxGlyphs) * kMaxGlyphHeight * kGlyphRowBytes;

// **A face**: the size of a glyph, the space between two of them, the codes it carries, its bits, and the
// one function that knows how those bits are laid out (docs/30).
struct Face {
  uint8_t width;
  uint8_t height;
  uint8_t gap;
  uint8_t first;  // the code of the first glyph of the table
  uint8_t count;  // how many glyphs the table holds, in order
  const uint8_t* bits;
  bool (*lit)(const uint8_t* bits, uint8_t index, uint8_t x, uint8_t y);
  // **How a character becomes a code of this face, or 0 for a gap**: the face of the system folds a
  // lowercase letter into its capital and knows nothing outside its table, and a face from a file carries
  // exactly the codes it was given (docs/30).
  uint8_t (*code)(uint8_t c);
  // **Null when the face is a table of bits and not a font of outlines**: then `bits` is null too, the size is
  // what the file says of itself, and every glyph is drawn at the size it is asked for instead of being read
  // from a block (docs/30). `width`, `height`, `count` and `has()` say nothing for such a face: a text is laid
  // out with the advance of each glyph and not with a cell.
  const ttf::Face* outline;
};

// The face of the system, which is what the bar and every text without a font of its own are drawn with.
const Face& system_face();

// **The face the whole system draws with** (0.96.0): the bar, every control, the prompt and any text an app does not give
// a face of its own. It starts as the five by seven table of the flash —5 px wide, 7 tall, and **14 px of line at
// `scale` 2, which is 1,5 mm on this 235 ppi glass**: a stamp and not a text— and the kernel points it at the bold face of
// the disk at boot, so the glass is one typeface at the sizes of the paper project (43 px of line for the text) instead
// of a table of pixels multiplied by a number (docs/30, docs/92).
//
// A null puts the table of the flash back, which is what a board with no font on its disk keeps (docs/70).
void set_system_face(const Face* face);

// Whether a face carries that code: `false` is a gap and not an error, because a face may be partial.
inline bool has(const Face& face, uint8_t code) {
  return code >= face.first && static_cast<uint8_t>(code - face.first) < face.count;
}

// **A face read from a text file** (`.lfont`), which is the portable font of this system (docs/30). The
// caller gives the block for the glyphs and gets back the reason a file is not a face, or nullptr when it is
// one and `out` points at `bits`.
const char* load(const char* text, uint32_t length, uint8_t* bits, Face& out);

// **A face read from a BDF**, the bitmap distribution format: plain text, one glyph per block and the rows in
// hexadecimal, which is what X11 and the old readers wrote and what anybody can produce with an editor
// (docs/30). The same block and the same `Face` come out as from a `.lfont`, so nothing that draws a text
// knows which of the two it got; the glyphs are up to eight by eight, and a BDF that is bigger is refused
// with the reason.
const char* load_bdf(const char* text, uint32_t length, uint8_t* bits, Face& out);

// **A face read from a PCF**, which is a BDF compiled: the same tables in binary —the metrics, the bitmaps
// and the encodings— with the numbers of a table in the byte order that the table itself says. It is what
// `bdftopcf` writes and what an X server served, and it is the smallest real binary format there is. The
// same block and the same `Face` come out as from the other two readers; the glyphs are up to eight by
// eight, the codes are printable ascii, and what this reader does not know —two-byte codes, a scan unit, the
// bits the other way round— is refused with the reason (docs/30).
const char* load_pcf(const char* text, uint32_t length, uint8_t* bits, Face& out);

// **A face of outlines** (docs/30): the file is TrueType, `storage` is where this library keeps what it found
// in it, and the face points at that storage — so both have to live as long as the face does. There is no
// block of bits: a glyph is drawn when a text is drawn, at the size that text asks for, and the pen moves by
// the advance of the file.
//
// **`kOutlineBody` is what one step of `scale` means, and the number is the paper project's** (0.97.2): its dense
// text is **24 px of body** (`ui24b`), its text is **36** (`ui36b`) and its title is **48** (`ui48b`), because a text
// of 43 px of line is the one a person reads on this glass without glasses. With a step of nine, `scale` 4 is
// **36 px of body** —the text of every page— and `scale` 3 is 27, which is the dense size within a pixel. It was eight,
// and eight made `scale` 4 a body of thirty-two: the words of the whole system were **eleven per cent smaller** than
// the paper project's, which is what its owner saw on the glass before any drawing of a control (docs/92).
const uint8_t kOutlineBody = 9;

// **And `kOutlineLine` is the height of a line of one step**, which is not the body: a line of DejaVu Sans Bold is
// **1,19 times its body** —the paper project measured 43 px of line for a body of 36, and 29 for 24—, so a step of nine
// is a line of eleven. Everything that asks a face for the height of a line gets this, and a page that leaves room for
// a label leaves the room of a line (docs/30, docs/92).
const uint8_t kOutlineLine = 11;
const char* load_ttf(const char* text, uint32_t length, ttf::Face& storage, Face& out);

// **Whether these bytes are a font of outlines** and not text: a table directory of TrueType (`0x00010000`,
// `true`) or the other flavour (`OTTO`, which is CFF and is refused by the reader with its reason).
bool looks_like_ttf(const char* text, uint32_t length);

// **The loader an app gets**: it looks at what the file says of itself —`lunok-font 1`, `STARTFONT` or the
// four bytes a PCF opens with— and hands the bytes to the right reader, so `ui.load_font` takes any of the
// three and an app does not care which (docs/30). nullptr means the face is loaded and `out` is ready.
const char* load_any(const char* text, uint32_t length, uint8_t* bits, Face& out);

// How wide a text is with that face and that scale, in pixels, counting the one-glyph separation between
// letters: the same number the bar uses to centre a title.
uint16_t width(const Face& face, const char* text, uint8_t scale);
uint16_t width(const char* text, uint8_t scale);  // with the face of the system

// **The box a text is not allowed out of**, in the surface' own pixels. It is what a label is: a text
// inside a border, and the border is what cuts it (docs/30, docs/90). `x1` and `y1` are one past the
// last column and row that may be painted.
struct Clip {
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = 0;
  int32_t y1 = 0;
};

// The box that cuts nothing, for the callers that want a text anywhere. It is a function and not a
// constant because a `Clip` with values in the struct is not something C++11 lets you build with a
// braced list —and this code is compiled as C++11 on the board— (docs/50).
inline Clip no_clip() {
  Clip clip;
  clip.x1 = 0x7FFF;
  clip.y1 = 0x7FFF;
  return clip;
}

// **What a text is made of: rectangles.** Each glyph column that is lit is a run of pixels, and a run is
// a rectangle. Whoever draws a text gives this function something to do with each one —the glass, a
// canvas of an app, a mask— and the walking of the glyphs is written once for all of them (docs/90).
typedef void (*RunFn)(void* user, int32_t x, int32_t y, int32_t w, int32_t h);

// Walks `text` from its top-left corner and calls `fn` once per run, **cut at `clip`**: a run that hangs
// out of the box loses the part that is outside and nothing else (docs/30). Returns the width of the
// text, clipped or not, because that is what centring it needs.
uint16_t runs(const Face& face, const char* text, int32_t x, int32_t y, uint8_t scale, const Clip& clip,
              RunFn fn, void* user);
uint16_t runs(const char* text, int32_t x, int32_t y, uint8_t scale, const Clip& clip, RunFn fn,
              void* user);  // with the face of the system

// Draws `text` on `surface` with its top-left corner at (x, y), each glyph pixel a `scale` square of
// `level`, and **nothing outside `clip`** (docs/50).
uint16_t draw(hal::ISurface& surface, uint16_t x, uint16_t y, const char* text, uint8_t scale,
              uint8_t level, const Clip& clip = no_clip());

}  // namespace font
}  // namespace kernel

#endif  // LUNOK_KERNEL_FONT_H
