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

namespace kernel {
namespace font {

const uint8_t kGlyphWidth = 5;
const uint8_t kGlyphHeight = 7;

// **How big a glyph of a face can be**: one byte per row, and the leftmost column is the highest bit, which
// is the order a text file is written in (docs/30).
const uint8_t kMaxGlyphWidth = 8;
const uint8_t kMaxGlyphHeight = 8;
const uint8_t kMaxGlyphs = 96;  // 0x20 to 0x7F

// The block of glyphs an app hands over for a face it loads from a file, in bytes (96 glyphs of eight rows).
const uint32_t kFaceBytes = static_cast<uint32_t>(kMaxGlyphs) * kMaxGlyphHeight;

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
};

// The face of the system, which is what the bar and every text without a font of its own are drawn with.
const Face& system_face();

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

// **The loader an app gets**: it looks at what the file says of itself —`lunok-font 1` or `STARTFONT`— and
// hands the bytes to the right reader, so `ui.load_font` takes either and an app does not care which
// (docs/30). nullptr means the face is loaded and `out` is ready.
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
