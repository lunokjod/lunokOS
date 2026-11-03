// **What the decoders share**: a cursor that never walks off the file, a row that is being made in the
// packing the glass takes, and the two conversions every colour format needs (a grey value and a level).
// Nothing here is part of the library's face: `img.h` is (docs/30).
#ifndef LUNOK_IMG_PRIVATE_H
#define LUNOK_IMG_PRIVATE_H

#include <stdint.h>

#include "img/img.h"

namespace img {
namespace detail {

// A cursor that refuses to walk past the end: every read says whether it could, and a decoder that gets a
// false says `kTruncated` and stops. No pointer arithmetic outside the block, ever (docs/70).
struct Cursor {
  Cursor(const uint8_t* bytes, uint32_t bytes_len, uint32_t start) : p(bytes), len(bytes_len), at(start) {}
  const uint8_t* p;
  uint32_t len;
  uint32_t at;

  bool has(uint32_t bytes) const { return at <= len && bytes <= len - at; }
  const uint8_t* here() const { return p + at; }
  bool skip(uint32_t bytes) {
    if (!has(bytes)) return false;
    at += bytes;
    return true;
  }
  bool u8(uint8_t& v) {
    if (!has(1)) return false;
    v = p[at++];
    return true;
  }
  bool u16le(uint16_t& v) {
    if (!has(2)) return false;
    v = static_cast<uint16_t>(p[at] | (p[at + 1] << 8));
    at += 2;
    return true;
  }
  bool u16be(uint16_t& v) {
    if (!has(2)) return false;
    v = static_cast<uint16_t>((p[at] << 8) | p[at + 1]);
    at += 2;
    return true;
  }
  bool u32le(uint32_t& v) {
    if (!has(4)) return false;
    v = static_cast<uint32_t>(p[at]) | (static_cast<uint32_t>(p[at + 1]) << 8) |
        (static_cast<uint32_t>(p[at + 2]) << 16) | (static_cast<uint32_t>(p[at + 3]) << 24);
    at += 4;
    return true;
  }
  bool u32be(uint32_t& v) {
    if (!has(4)) return false;
    v = (static_cast<uint32_t>(p[at]) << 24) | (static_cast<uint32_t>(p[at + 1]) << 16) |
        (static_cast<uint32_t>(p[at + 2]) << 8) | static_cast<uint32_t>(p[at + 3]);
    at += 4;
    return true;
  }
};

// **A row being made**: `(width + 1) / 2` bytes, the left pixel of a pair in the high nibble — the packing
// of the glass, of a canvas and of a glyph's blob, so a picture drawn on a page is a copy (docs/50).
struct Row {
  uint8_t* bytes;
  uint16_t width;
  uint16_t at;
  uint16_t y;  // which row of the picture this is, for the dither
};

void row_start(Row& row, uint8_t* bytes, uint16_t width, uint16_t y);
// One pixel, already a level of the sixteen the panel has (docs/50).
void row_put(Row& row, uint8_t level);
// One pixel of colour, dithered to a level (docs/30).
void row_put_rgb(Row& row, uint8_t r, uint8_t g, uint8_t b);
// One pixel of grey, dithered to a level.
void row_put_grey(Row& row, uint8_t grey);
// The last nibble of an odd row is zero, so a row is a whole number of bytes (docs/30).
void row_end(Row& row);

// The luminance of the sRGB primaries, in 0..255 (docs/30).
uint8_t grey_of(uint8_t r, uint8_t g, uint8_t b);

// **Grey to a level of the sixteen, with an ordered dither**: a photograph in sixteen levels without one is
// a poster, and a dither is four bytes of table and two shifts (docs/30, docs/85).
uint8_t level_of(uint8_t grey, uint16_t x, uint16_t y);

// **The four decoders and what each one needs**: every one of them answers `kUnknown` when the signature is
// not its own, so the dispatcher tries them in order and never has to know more than the first bytes.
Status decode_bmp(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_png(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_gif(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_jpeg(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_pnm(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_ico(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);

uint32_t bmp_scratch(const uint8_t* data, uint32_t len);
uint32_t png_scratch(const uint8_t* data, uint32_t len);
uint32_t gif_scratch(const uint8_t* data, uint32_t len);
uint32_t jpeg_scratch(const uint8_t* data, uint32_t len);
uint32_t pnm_scratch(const uint8_t* data, uint32_t len);
uint32_t ico_scratch(const uint8_t* data, uint32_t len);

Status bmp_info(const uint8_t* data, uint32_t len, Info& out);
Status png_info(const uint8_t* data, uint32_t len, Info& out);
Status gif_info(const uint8_t* data, uint32_t len, Info& out);
Status jpeg_info(const uint8_t* data, uint32_t len, Info& out);
Status pnm_info(const uint8_t* data, uint32_t len, Info& out);
Status ico_info(const uint8_t* data, uint32_t len, Info& out);

// A picture that is wider or taller than a page can be is not a picture this board draws, and the sum of
// the two is a number that has to fit in the arithmetic of the decoders: 4096 is the ceiling of a page and
// of a decoder's own numbers (docs/30, docs/75).
const uint32_t kMaxSide = 4096;
// How many bytes a row of `width` pixels takes (docs/30).
inline uint32_t row_bytes(uint32_t width) { return (width + 1) / 2; }

}  // namespace detail
}  // namespace img

#endif  // LUNOK_IMG_PRIVATE_H
