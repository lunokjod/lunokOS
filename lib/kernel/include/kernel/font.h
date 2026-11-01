// The glyphs of the system: a five by seven font, drawn with runs of pixels (docs/30).
//
// There is no font file and no scalable outline: a bar that says an hour and a title needs seventy
// characters and nothing else, and five bytes of bitmap per character fit in the flash without a
// decoder. **Uppercase only**: the lowercase letters of a title are drawn as their capital, which is
// what a title bar did for decades and what saves half the table (docs/30).
#ifndef LUNOK_KERNEL_FONT_H
#define LUNOK_KERNEL_FONT_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace font {

const uint8_t kGlyphWidth = 5;
const uint8_t kGlyphHeight = 7;

// How wide a text is with that scale, in pixels, counting the one-glyph separation between letters:
// the same number the bar uses to centre a title.
uint16_t width(const char* text, uint8_t scale);

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
uint16_t runs(const char* text, int32_t x, int32_t y, uint8_t scale, const Clip& clip, RunFn fn,
              void* user);

// Draws `text` on `surface` with its top-left corner at (x, y), each glyph pixel a `scale` square of
// `level`, and **nothing outside `clip`** (docs/50).
uint16_t draw(hal::ISurface& surface, uint16_t x, uint16_t y, const char* text, uint8_t scale,
              uint8_t level, const Clip& clip = no_clip());

}  // namespace font
}  // namespace kernel

#endif  // LUNOK_KERNEL_FONT_H
