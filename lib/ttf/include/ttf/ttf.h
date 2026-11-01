// **A TrueType font, read the way a page needs it** (docs/30): the tables a text is drawn with, the outline of
// a glyph, and that outline turned into rows of bits at the size it is asked for.
//
// It is written the way everything here is: **no allocation of its own and no table kept in step**. The whole
// file is the caller's bytes, the tables are found once and remembered as offsets into them, and the drawing
// happens into a block the caller hands over. A `.ttf` of a few kilobytes lives in the same place as any other
// face —the app's own block— and this reader never copies it.
//
// What is read is what a page needs: `head`, `hhea`, `hmtx`, `maxp`, `loca`, `glyf` and a `cmap` of format 4.
// **What is not read is refused and not guessed**: a font of the other flavour (`OTTO`, which is CFF), a
// glyph made of pieces, a code that the character map does not carry, and an outline with more edges than the
// block of edges this reader keeps.
#ifndef LUNOK_TTF_TTF_H
#define LUNOK_TTF_TTF_H

#include <stdint.h>

namespace ttf {

// **What came out of it** and, when nothing did, why. A plain enum like the rest of the HAL's, because the
// answer is asked for on nearly every line of the readers below.
enum Status : uint8_t {
  kOk = 0,
  kNotAFont,      // not a table directory this reader knows
  kOtherFlavour,  // `OTTO`: outlines in CFF, which is another format
  kNoTable,       // one of the seven tables a page needs is missing
  kBadData,       // a number that does not fit, a segment that goes backwards
  kNoRoom,        // the glyph does not fit in the block, or has more edges than there is room for
  kNoGlyph,       // that code is not in the character map
  kComposite,     // a glyph made of other glyphs, which is not read yet
};

// **The edges of a glyph that is being drawn**, flattened to straight lines: `kMaxEdges` of them, which is
// more than a letter of a real font needs at any size a page is read at and few enough to be a fixed cost
// —a `@` of DejaVu Sans is the worst of the printable ascii and it fits— (docs/75).
const uint32_t kMaxEdges = 384;

// **A face of outlines**: the file, where its tables are, and the numbers a text is laid out with. It is a
// view and not a copy, so the bytes have to outlive it.
struct Face {
  const uint8_t* data;
  uint32_t length;
  uint32_t cmap;
  uint32_t glyf;
  uint32_t loca;
  uint32_t hmtx;
  uint32_t maxp;
  uint16_t units_per_em;
  uint16_t glyphs;
  uint16_t metrics;  // how many glyphs have their own advance in `hmtx`
  int16_t ascent;
  int16_t descent;
  int16_t line_gap;
  bool long_loca;
  bool format4;  // the character map is the segment map and not a table of every code
};

// **The box of a glyph that has been drawn**, in pixels, with where its top-left corner sits from the pen:
// `left` is how far right of the pen the ink starts and `top` how far below the top of the line.
struct Bitmap {
  uint16_t width;
  uint16_t height;
  int16_t left;
  int16_t top;
};

// Reads the table directory and the seven tables a page needs. nullptr means `out` is a face.
const char* open(const uint8_t* data, uint32_t length, Face& out);

// The glyph a code asks for, through the character map. `kNoGlyph` when the code is not in the font.
Status glyph_of(const Face& face, uint8_t code, uint16_t& glyph);

// How far the pen moves after that glyph at `px` pixels of body, in pixels (at least one).
uint16_t advance(const Face& face, uint16_t glyph, uint16_t px);

// **The outline of a glyph, drawn**: `rows` is `height` rows of `row_bytes` bytes, most significant bit of the
// first byte is the leftmost pixel, and what is not ink is zero. The block has to hold `max_width` by
// `max_height`, or the answer is `kNoRoom` and nothing is written.
Status render(const Face& face, uint16_t glyph, uint16_t px, uint8_t* rows, uint16_t row_bytes,
              uint16_t max_width, uint16_t max_height, Bitmap& box);

// How many edges the last `render` flattened, for the tests and for the log of a page that took too long.
uint32_t edges_used();

}  // namespace ttf

#endif  // LUNOK_TTF_TTF_H
