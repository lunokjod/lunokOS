// **The cache of this system, which is a picture** (0.146.0, docs/46 C11): `ICS1`, the width and the height as two
// little-endian words —the width always even, because two pixels share a byte— and then the rows of the picture, the
// left pixel of a pair in the high nibble, which is the packing the glass, a canvas and every decoder of this library
// already speak.
//
// A cache written by one app is a picture for the next one, and that is the whole reason it is a family of this file and
// not a private trick of the reader: the thumbnail of a book and the picture of a page are the same bytes for whoever
// reads them (docs/30).
#include <string.h>

#include "img_private.h"

namespace img {
namespace detail {
namespace {

// "ICS1" and the two words of the size, which is what every row after them is read from.
const uint32_t kIcsHeader = 8;

}  // namespace

Status ics_info(const uint8_t* data, uint32_t len, Info& out) {
  if (data == nullptr || len < kIcsHeader || memcmp(data, "ICS1", 4) != 0) return kUnknown;

  const uint16_t width = static_cast<uint16_t>(data[4] | (data[5] << 8));
  const uint16_t height = static_cast<uint16_t>(data[6] | (data[7] << 8));
  if (width == 0 || height == 0) return kBadData;
  // **An odd width is a file of a writer that did not follow the rule**: two pixels are a byte, and a reader that
  // trusted the header would read every row after the first a nibble off (docs/46, C11).
  if ((width & 1u) != 0) return kBadData;
  if (width > kMaxSide || height > kMaxSide) return kUnsupported;

  // A file that does not carry its rows is a cache that was cut in the middle of the write.
  const uint32_t need = kIcsHeader + row_bytes(width) * height;
  if (len < need) return kTruncated;

  out.format = "ICS1";
  out.width = width;
  out.height = height;
  out.depth = 4;
  return kOk;
}

// **A cache is read where it lies**: its rows are already the bytes the sink wants, so this family asks for no block.
uint32_t ics_scratch(const uint8_t* data, uint32_t len) {
  (void)data;
  (void)len;
  return 0;
}

Status decode_ics(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  (void)scratch;
  (void)cap;

  Info info;
  const Status status = ics_info(data, len, info);
  if (status != kOk) return status;

  const uint32_t stride = row_bytes(info.width);
  for (uint16_t y = 0; y < info.height; ++y) {
    sink.write(sink.ctx, y, data + kIcsHeader + stride * y);
  }

  return kOk;
}

}  // namespace detail

// ---- and the other half: writing one (docs/46, C11) ----

uint32_t ics_bytes(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) return 0;

  const uint32_t even = static_cast<uint32_t>(width) + (width & 1u);
  return 8 + (even / 2) * height;
}

void ics_header(uint16_t width, uint16_t height, uint8_t out[8]) {
  if (out == nullptr) return;

  const uint32_t even = static_cast<uint32_t>(width) + (width & 1u);
  out[0] = 'I';
  out[1] = 'C';
  out[2] = 'S';
  out[3] = '1';
  out[4] = static_cast<uint8_t>(even & 0xFF);
  out[5] = static_cast<uint8_t>((even >> 8) & 0xFF);
  out[6] = static_cast<uint8_t>(height & 0xFF);
  out[7] = static_cast<uint8_t>((height >> 8) & 0xFF);
}

uint32_t ics_row(uint16_t width, const uint8_t* row, uint8_t* out, uint32_t cap) {
  if (row == nullptr || out == nullptr || width == 0) return 0;

  const uint32_t even = static_cast<uint32_t>(width) + (width & 1u);
  const uint32_t line = even / 2;
  if (cap < line) return 0;

  memcpy(out, row, detail::row_bytes(width));
  // **An odd picture loses the last half of its last byte and it is PAPER** (0.146.0): two pixels are a byte and there
  // is no pixel behind the odd last column, and a black stripe down the side of a thumbnail is not a picture of anything
  // (docs/46, C11).
  if ((width & 1u) != 0) out[line - 1] = static_cast<uint8_t>((out[line - 1] & 0xF0) | 0x0F);

  return line;
}

}  // namespace img
