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

// Draws `text` on `surface` with its top-left corner at (x, y), each glyph pixel a `scale` square of
// `level`. It calls `fill_rect` once per horizontal run, not once per pixel: a glyph is a handful of
// rectangles and the panel's damage is the whole text with no gaps (docs/50). Returns the width.
uint16_t draw(hal::ISurface& surface, uint16_t x, uint16_t y, const char* text, uint8_t scale,
              uint8_t level);

}  // namespace font
}  // namespace kernel

#endif  // LUNOK_KERNEL_FONT_H
