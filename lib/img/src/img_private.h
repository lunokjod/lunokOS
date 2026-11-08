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

  // **The mask of the picture, when the caller asked for one** (0.169.0): one nibble a pixel, `15` for a pixel that is
  // there and `0` for one the picture leaves to whatever is under it. It is a row of the caller's and not of this library,
  // and it is `nullptr` for a caller that does not want it —which is every caller that does not ask, and that is how the
  // memory of a mask is paid only by an app that uses one— (docs/30, docs/75).
  uint8_t* alpha;
};

void row_start(Row& row, uint8_t* bytes, uint16_t width, uint16_t y);
// One pixel, already a level of the sixteen the panel has (docs/50).
void row_put(Row& row, uint8_t level);
// **The alpha of the pixel that is about to be put** (0.169.0), a level of the same sixteen: `row_put_alpha` says whether the
// pixel is there and `row_put` says what colour it has. It goes **before** the colour, because the colour is what moves the
// row on, and a mask nobody asked for is not written at all —one branch and no memory— (docs/30, docs/75).
void row_put_alpha(Row& row, uint8_t alpha);
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
// **`mask` is where the picture's own transparency is said** (0.169.0): a row of nibbles a pixel, `15` where the picture is
// and `0` where it leaves what is under it. A caller that does not ask gives `nullptr` and pays nothing (docs/30).
Status decode_png(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap,
                  const RowSink* mask = nullptr);
Status decode_gif(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_jpeg(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_pnm(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
Status decode_ico(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);
// **The cache of this system** (0.146.0, docs/46 C11): a picture that is already in the packing of the glass.
Status decode_ics(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);

uint32_t bmp_scratch(const uint8_t* data, uint32_t len);
uint32_t png_scratch(const uint8_t* data, uint32_t len);
uint32_t gif_scratch(const uint8_t* data, uint32_t len);
uint32_t jpeg_scratch(const uint8_t* data, uint32_t len);
uint32_t pnm_scratch(const uint8_t* data, uint32_t len);
uint32_t ico_scratch(const uint8_t* data, uint32_t len);
uint32_t ics_scratch(const uint8_t* data, uint32_t len);

Status bmp_info(const uint8_t* data, uint32_t len, Info& out);
Status png_info(const uint8_t* data, uint32_t len, Info& out);
Status gif_info(const uint8_t* data, uint32_t len, Info& out);
Status jpeg_info(const uint8_t* data, uint32_t len, Info& out);
Status pnm_info(const uint8_t* data, uint32_t len, Info& out);
Status ico_info(const uint8_t* data, uint32_t len, Info& out);
Status ics_info(const uint8_t* data, uint32_t len, Info& out);

// A picture that is wider or taller than a page can be is not a picture this board draws, and the sum of
// the two is a number that has to fit in the arithmetic of the decoders: 4096 is the ceiling of a page and
// of a decoder's own numbers (docs/30, docs/75).
const uint32_t kMaxSide = 4096;
// How many bytes a row of `width` pixels takes (docs/30).
inline uint32_t row_bytes(uint32_t width) { return (width + 1) / 2; }

// **The working state of a decoder comes out of the block the caller gave, and never out of the stack**
// (0.118.0): the table of a GIF is twelve kilobytes and the header of a JPEG is three and a half, and the
// task that runs an app has six or seven kilobytes of stack —`stack 7320 B free` is what the kernel prints
// before an app starts—, which is why the board answered a page of nine pictures with a stack overflow in
// `loopTask`. A decoder asks for what it needs in its `*_scratch`, puts its state after the pixels and
// hands the caller one number (docs/75, docs/85, docs/92).
//
// The row of a picture that a decoder builds before it writes it is the widest a page can be, and it is
// the same size for every family, so it is one constant and not a number per file.
const uint32_t kRowBytes = (kMaxSide + 1) / 2;

// The first byte of a block after `at` that a struct can start at: the pixels of a decoder are bytes and
// its tables are words, and a table at an odd address is a fault on a board and a slow read on a PC.
inline uint32_t aligned_at(uint32_t at) { return (at + 3u) & ~3u; }

}  // namespace detail
}  // namespace img

#endif  // LUNOK_IMG_PRIVATE_H
