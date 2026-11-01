// The five by seven glyphs and the two functions that put them on a surface (docs/30).
#include "kernel/font.h"

#include <stddef.h>

namespace kernel {
namespace font {
namespace {

// Glyphs for the characters 0x20 to 0x5A —space, punctuation, digits and capitals—, five columns each
// and one byte per column: **bit 0 is the top row and bit 6 the bottom one**, so a column is read
// downwards from the least significant bit. Everything outside that range is drawn as a capital or as
// a space, which is what keeps the table at 295 bytes (docs/30).
struct Glyph {
  uint8_t column[5];
};

// clang-format off
const Glyph kGlyphs[] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00}},  // ' '
    {{0x00, 0x00, 0x5F, 0x00, 0x00}},  // '!'
    {{0x00, 0x07, 0x00, 0x07, 0x00}},  // '"'
    {{0x14, 0x7F, 0x14, 0x7F, 0x14}},  // '#'
    {{0x24, 0x2A, 0x7F, 0x2A, 0x12}},  // '$'
    {{0x23, 0x13, 0x08, 0x64, 0x62}},  // '%'
    {{0x36, 0x49, 0x55, 0x22, 0x50}},  // '&'
    {{0x00, 0x05, 0x03, 0x00, 0x00}},  // '\''
    {{0x00, 0x1C, 0x22, 0x41, 0x00}},  // '('
    {{0x00, 0x41, 0x22, 0x1C, 0x00}},  // ')'
    {{0x08, 0x2A, 0x1C, 0x2A, 0x08}},  // '*'
    {{0x08, 0x08, 0x3E, 0x08, 0x08}},  // '+'
    {{0x00, 0x50, 0x30, 0x00, 0x00}},  // ','
    {{0x08, 0x08, 0x08, 0x08, 0x08}},  // '-'
    {{0x00, 0x60, 0x60, 0x00, 0x00}},  // '.'
    {{0x20, 0x10, 0x08, 0x04, 0x02}},  // '/'
    {{0x3E, 0x51, 0x49, 0x45, 0x3E}},  // '0'
    {{0x00, 0x42, 0x7F, 0x40, 0x00}},  // '1'
    {{0x42, 0x61, 0x51, 0x49, 0x46}},  // '2'
    {{0x21, 0x41, 0x45, 0x4B, 0x31}},  // '3'
    {{0x18, 0x14, 0x12, 0x7F, 0x10}},  // '4'
    {{0x27, 0x45, 0x45, 0x45, 0x39}},  // '5'
    {{0x3C, 0x4A, 0x49, 0x49, 0x30}},  // '6'
    {{0x01, 0x71, 0x09, 0x05, 0x03}},  // '7'
    {{0x36, 0x49, 0x49, 0x49, 0x36}},  // '8'
    {{0x06, 0x49, 0x49, 0x29, 0x1E}},  // '9'
    {{0x00, 0x36, 0x36, 0x00, 0x00}},  // ':'
    {{0x00, 0x56, 0x36, 0x00, 0x00}},  // ';'
    {{0x08, 0x14, 0x22, 0x41, 0x00}},  // '<'
    {{0x14, 0x14, 0x14, 0x14, 0x14}},  // '='
    {{0x00, 0x41, 0x22, 0x14, 0x08}},  // '>'
    {{0x02, 0x01, 0x51, 0x09, 0x06}},  // '?'
    {{0x32, 0x49, 0x79, 0x41, 0x3E}},  // '@'
    {{0x7E, 0x11, 0x11, 0x11, 0x7E}},  // 'A'
    {{0x7F, 0x49, 0x49, 0x49, 0x36}},  // 'B'
    {{0x3E, 0x41, 0x41, 0x41, 0x22}},  // 'C'
    {{0x7F, 0x41, 0x41, 0x22, 0x1C}},  // 'D'
    {{0x7F, 0x49, 0x49, 0x49, 0x41}},  // 'E'
    {{0x7F, 0x09, 0x09, 0x09, 0x01}},  // 'F'
    {{0x3E, 0x41, 0x49, 0x49, 0x7A}},  // 'G'
    {{0x7F, 0x08, 0x08, 0x08, 0x7F}},  // 'H'
    {{0x00, 0x41, 0x7F, 0x41, 0x00}},  // 'I'
    {{0x20, 0x40, 0x41, 0x3F, 0x01}},  // 'J'
    {{0x7F, 0x08, 0x14, 0x22, 0x41}},  // 'K'
    {{0x7F, 0x40, 0x40, 0x40, 0x40}},  // 'L'
    {{0x7F, 0x02, 0x0C, 0x02, 0x7F}},  // 'M'
    {{0x7F, 0x04, 0x08, 0x10, 0x7F}},  // 'N'
    {{0x3E, 0x41, 0x41, 0x41, 0x3E}},  // 'O'
    {{0x7F, 0x09, 0x09, 0x09, 0x06}},  // 'P'
    {{0x3E, 0x41, 0x51, 0x21, 0x5E}},  // 'Q'
    {{0x7F, 0x09, 0x19, 0x29, 0x46}},  // 'R'
    {{0x46, 0x49, 0x49, 0x49, 0x31}},  // 'S'
    {{0x01, 0x01, 0x7F, 0x01, 0x01}},  // 'T'
    {{0x3F, 0x40, 0x40, 0x40, 0x3F}},  // 'U'
    {{0x1F, 0x20, 0x40, 0x20, 0x1F}},  // 'V'
    {{0x3F, 0x40, 0x38, 0x40, 0x3F}},  // 'W'
    {{0x63, 0x14, 0x08, 0x14, 0x63}},  // 'X'
    {{0x07, 0x08, 0x70, 0x08, 0x07}},  // 'Y'
    {{0x61, 0x51, 0x49, 0x45, 0x43}},  // 'Z'
};
// clang-format on

const char kFirst = ' ';
const char kLast = 'Z';
const uint16_t kCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

// The glyph of a character: a lowercase letter is its capital, and anything the table does not carry is
// a space — a bar with a box in it says less than a bar with a gap (docs/30).
const Glyph& glyph_for(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  if (c < kFirst || c > kLast) return kGlyphs[0];
  return kGlyphs[static_cast<uint8_t>(c - kFirst)];
}

}  // namespace

uint16_t width(const char* text, uint8_t scale) {
  if (text == nullptr || text[0] == '\0') return 0;
  const uint16_t step = static_cast<uint16_t>((kGlyphWidth + 1) * scale);
  uint16_t chars = 0;
  for (const char* at = text; *at != '\0'; ++at) ++chars;
  // The last glyph has no separation after it.
  return static_cast<uint16_t>(chars * step - scale);
}

uint16_t runs(const char* text, int32_t x, int32_t y, uint8_t scale, const Clip& clip, RunFn fn,
              void* user) {
  if (text == nullptr || scale == 0 || fn == nullptr) return 0;
  const int32_t step = (kGlyphWidth + 1) * scale;
  int32_t at_x = x;
  for (const char* at = text; *at != '\0'; ++at) {
    const Glyph& glyph = glyph_for(*at);
    for (uint8_t row = 0; row < kGlyphHeight; ++row) {
      uint8_t column = 0;
      while (column < kGlyphWidth) {
        const bool on = ((glyph.column[column] >> row) & 1u) != 0;
        if (!on) {
          ++column;
          continue;
        }
        // One rectangle per run of lit columns: a glyph is a few rectangles and not thirty-five.
        uint8_t run = column;
        while (run < kGlyphWidth && (((glyph.column[run] >> row) & 1u) != 0)) ++run;
        // **The run is cut against the border of the box**, column by column, so a letter that hangs out
        // of a label loses the part that is outside and nothing else (docs/30).
        int32_t left = at_x + static_cast<int32_t>(column) * scale;
        int32_t right = at_x + static_cast<int32_t>(run) * scale;
        const int32_t top = y + static_cast<int32_t>(row) * scale;
        const int32_t bottom = top + scale;
        if (left < clip.x0) left = clip.x0;
        if (right > clip.x1) right = clip.x1;
        if (left < right && top >= clip.y0 && bottom <= clip.y1) {
          fn(user, left, top, right - left, scale);
        }
        column = run;
      }
    }
    at_x += step;
  }
  return width(text, scale);
}

namespace {

// The sink of a surface: a run is one `fill_rect` of the level the caller asked for.
struct SurfaceSink {
  hal::ISurface* surface;
  uint8_t level;
};

void surface_run(void* user, int32_t x, int32_t y, int32_t w, int32_t h) {
  SurfaceSink* sink = static_cast<SurfaceSink*>(user);
  sink->surface->fill_rect(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                           static_cast<uint16_t>(w), static_cast<uint16_t>(h), sink->level);
}

}  // namespace

uint16_t draw(hal::ISurface& surface, uint16_t x, uint16_t y, const char* text, uint8_t scale,
              uint8_t level, const Clip& clip) {
  SurfaceSink sink = {&surface, level};
  return runs(text, x, y, scale, clip, surface_run, &sink);
}

}  // namespace font
}  // namespace kernel
