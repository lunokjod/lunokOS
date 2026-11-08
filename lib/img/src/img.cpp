// **Which picture this is, and the handful of bytes every decoder shares.** The families are told apart by
// the first bytes —that is what a format is— and each decoder is its own file: `bmp.cpp`, `png.cpp`,
// `gif.cpp`, `jpeg.cpp`, `pnm.cpp`, `ico.cpp` (docs/30).
#include "img/img.h"

#include "img_private.h"

namespace img {
namespace {

bool starts_with(const uint8_t* data, uint32_t len, const char* text, uint32_t size) {
  if (len < size) return false;
  for (uint32_t i = 0; i < size; ++i) {
    if (data[i] != static_cast<uint8_t>(text[i])) return false;
  }
  return true;
}

// The eight bytes every PNG begins with (RFC 2083, 3.1), and the six of a GIF (89a, 17).
const uint8_t kPngMark[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

bool is_png(const uint8_t* data, uint32_t len) {
  if (len < sizeof(kPngMark)) return false;
  for (uint32_t i = 0; i < sizeof(kPngMark); ++i) {
    if (data[i] != kPngMark[i]) return false;
  }
  return true;
}

bool is_bmp(const uint8_t* data, uint32_t len) { return starts_with(data, len, "BM", 2); }

bool is_gif(const uint8_t* data, uint32_t len) {
  return starts_with(data, len, "GIF87a", 6) || starts_with(data, len, "GIF89a", 6);
}

bool is_jpeg(const uint8_t* data, uint32_t len) {
  return len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool is_ico(const uint8_t* data, uint32_t len) {
  return len >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00;
}

bool is_pnm(const uint8_t* data, uint32_t len) {
  return len >= 2 && data[0] == 'P' && data[1] >= '1' && data[1] <= '6';
}

// **The cache of this system** (0.146.0, docs/46 C11): four letters that are nobody else's, and the rows are already the
// packing of the glass.
bool is_ics(const uint8_t* data, uint32_t len) { return starts_with(data, len, "ICS1", 4); }

}  // namespace

namespace detail {

void row_start(Row& row, uint8_t* bytes, uint16_t width, uint16_t y) {
  row.bytes = bytes;
  row.width = width;
  row.at = 0;
  row.y = y;
  for (uint32_t i = 0; i < row_bytes(width); ++i) bytes[i] = 0;
}

void row_put(Row& row, uint8_t level) {
  if (row.bytes == nullptr || row.at >= row.width) return;
  const uint8_t value = static_cast<uint8_t>((level > 15) ? 15 : level);
  uint8_t& byte = row.bytes[row.at / 2];
  if ((row.at & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | (value << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | value);
  }
  ++row.at;
}

uint8_t grey_of(uint8_t r, uint8_t g, uint8_t b) {
  // The luminance of the sRGB primaries (0.2126, 0.7152, 0.0722 in linear light are not the numbers a
  // decoder has room for; the 601 ones are what every grey scale in this project uses) (docs/30).
  const uint32_t sum = 77u * r + 150u * g + 29u * b;
  return static_cast<uint8_t>((sum + 128u) >> 8);
}

uint8_t level_of(uint8_t grey, uint16_t x, uint16_t y) {
  // **An ordered dither of four by four**: the sixteeen thresholds are the corners of the sixteen levels,
  // so a flat area of one grey becomes a texture that reads as that grey and a photograph keeps its
  // gradients in a panel with sixteen of them (docs/30, docs/85).
  static const uint8_t kBayer[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  // **The threshold is the whole of the next step and it is added, not rounded**: a grey of zero has to be
  // black at every one of the sixteen places of the table and a grey of 255 has to be white at every one.
  // Rounding the sum made the black of a picture a texture of level one, which is a page of a photograph with
  // dust on it (found on the glass, 3/11).
  const uint32_t dithered = static_cast<uint32_t>(grey) * 15u + kBayer[y & 3][x & 3] * 16u;
  const uint32_t level = dithered / 255u;
  return static_cast<uint8_t>((level > 15) ? 15 : level);
}

void row_put_rgb(Row& row, uint8_t r, uint8_t g, uint8_t b) {
  row_put(row, level_of(grey_of(r, g, b), row.at, row.y));
}

void row_put_grey(Row& row, uint8_t grey) { row_put(row, level_of(grey, row.at, row.y)); }

void row_end(Row& row) {
  // **A row is a whole number of bytes.** An odd width leaves the last nibble of the last byte empty, and
  // it stays zero: the caller may copy a row entire and the pixel after the picture is black, which is
  // outside the picture and not a colour of it (docs/30).
  if (row.bytes != nullptr && (row.width & 1) != 0 && row.at >= row.width) {
    const uint32_t last = row_bytes(row.width) - 1;
    row.bytes[last] &= 0xF0;
  }
}

}  // namespace detail

uint32_t scratch_bytes(const uint8_t* data, uint32_t len) {
  if (data == nullptr || len == 0) return 0;
  if (is_png(data, len)) return detail::png_scratch(data, len);
  if (is_jpeg(data, len)) return detail::jpeg_scratch(data, len);
  if (is_gif(data, len)) return detail::gif_scratch(data, len);
  if (is_ico(data, len)) return detail::ico_scratch(data, len);
  if (is_bmp(data, len)) return detail::bmp_scratch(data, len);
  if (is_pnm(data, len)) return detail::pnm_scratch(data, len);
  if (is_ics(data, len)) return detail::ics_scratch(data, len);
  return 0;
}

Status read_info(const uint8_t* data, uint32_t len, Info& out) {
  out = Info();
  if (data == nullptr || len == 0) return kUnknown;
  // **A signature that matched and a body that did not is a broken picture and not an unknown one**: the
  // caller has already been told which family it is looking at, and a silent `kUnknown` there reads as "this
  // board cannot show that kind of file" (docs/30).
  Status status = kUnknown;
  bool matched = false;
  if (is_png(data, len)) {
    matched = true;
    status = detail::png_info(data, len, out);
  } else if (is_jpeg(data, len)) {
    matched = true;
    status = detail::jpeg_info(data, len, out);
  } else if (is_gif(data, len)) {
    matched = true;
    status = detail::gif_info(data, len, out);
  } else if (is_ico(data, len)) {
    matched = true;
    status = detail::ico_info(data, len, out);
  } else if (is_bmp(data, len)) {
    matched = true;
    status = detail::bmp_info(data, len, out);
  } else if (is_pnm(data, len)) {
    matched = true;
    status = detail::pnm_info(data, len, out);
  } else if (is_ics(data, len)) {
    matched = true;
    status = detail::ics_info(data, len, out);
  }
  if (!matched) return kUnknown;
  return (status == kUnknown) ? kBadData : status;
}

Status decode(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  if (data == nullptr || len == 0 || sink.write == nullptr) return kUnknown;
  // A picture with no scratch is a picture that is read where it lies; one that needs a block and does not
  // get it says `kNoRoom` from its own decoder, which is the only one that knows how much it wanted
  // (docs/75).
  Status status = kUnknown;
  bool matched = false;
  if (is_png(data, len)) {
    matched = true;
    status = detail::decode_png(data, len, sink, scratch, cap);
  } else if (is_jpeg(data, len)) {
    matched = true;
    status = detail::decode_jpeg(data, len, sink, scratch, cap);
  } else if (is_gif(data, len)) {
    matched = true;
    status = detail::decode_gif(data, len, sink, scratch, cap);
  } else if (is_ico(data, len)) {
    matched = true;
    status = detail::decode_ico(data, len, sink, scratch, cap);
  } else if (is_bmp(data, len)) {
    matched = true;
    status = detail::decode_bmp(data, len, sink, scratch, cap);
  } else if (is_pnm(data, len)) {
    matched = true;
    status = detail::decode_pnm(data, len, sink, scratch, cap);
  } else if (is_ics(data, len)) {
    matched = true;
    status = detail::decode_ics(data, len, sink, scratch, cap);
  }
  if (!matched) return kUnknown;
  return (status == kUnknown) ? kBadData : status;
}

}  // namespace img
