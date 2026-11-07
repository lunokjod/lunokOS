// **A BMP, read the way a paint program writes one** (docs/30). The family is old enough to have two headers
// and four widths of pixel, so what is read is what a picture actually is: the DIB header (40 bytes of
// Windows, or the 12 of OS/2), no compression, and rows of 1, 4, 8, 16, 24 or 32 bits —**with the last row
// first**, which is the way a BMP is stored and the reason a picture read the other way is upside down—.
// RLE compression exists in the format and is not read: it is refused with `kUnsupported` instead of being
// drawn wrong (docs/30).
#include "img_private.h"

namespace img {
namespace detail {
namespace {

const uint32_t kCoreHeader = 12;  // BITMAPCOREHEADER, OS/2 1.x
const uint32_t kInfoHeader = 40;  // BITMAPINFOHEADER, the one every tool writes
const uint16_t kCompressionRgb = 0;
const uint16_t kCompressionBitfields = 3;

// What a BMP's header says, in one place, because the readers below ask it the same questions.
struct Header {
  uint32_t pixel_at = 0;  // where the rows begin
  int32_t width = 0;
  int32_t height = 0;
  bool top_down = false;  // a negative height is a picture stored from the top
  uint16_t depth = 0;
  uint16_t compression = 0;
  uint32_t palette_at = 0;  // where the colour table begins
  uint32_t colours = 0;     // how many entries it has
  uint32_t palette_size = 4;
  bool core = false;  // OS/2: three bytes a colour and no compression field
  // The channel order of a 16- or 32-bit picture, which the bit fields say and a plain BMP leaves as BGRA.
  uint32_t masks[3] = {0, 0, 0};
};

bool read_header(const uint8_t* data, uint32_t len, Header& out) {
  Cursor cursor{data, len, 0};
  uint16_t signature = 0;
  if (!cursor.u16le(signature) || signature != 0x4D42) return false;  // 'BM'
  uint32_t file_size = 0;
  if (!cursor.u32le(file_size)) return false;
  (void)file_size;  // the file is the block the caller gave: a header that lies about it is not a reason to
                    // read past the end
  if (!cursor.skip(4)) return false;  // the two reserved words
  if (!cursor.u32le(out.pixel_at)) return false;

  uint32_t header_size = 0;
  const uint32_t dib_at = cursor.at;  // the DIB header begins with its own size, which is the field we are about
                                      // to read: the palette begins after the whole header and not after its size
  if (!cursor.u32le(header_size)) return false;
  if (header_size != kCoreHeader && header_size != kInfoHeader && header_size != 108 &&
      header_size != 124) {
    return false;
  }
  int32_t width = 0;
  int32_t height = 0;
  if (header_size == kCoreHeader) {
    uint16_t small_width = 0;
    uint16_t small_height = 0;
    uint16_t planes = 0;
    if (!cursor.u16le(small_width) || !cursor.u16le(small_height) || !cursor.u16le(planes) ||
        !cursor.u16le(out.depth)) {
      return false;
    }
    width = small_width;
    height = small_height;
    out.core = true;
    out.palette_size = 3;
  } else {
    uint32_t raw_width = 0;
    uint32_t raw_height = 0;
    if (!cursor.u32le(raw_width) || !cursor.u32le(raw_height)) return false;
    width = static_cast<int32_t>(raw_width);
    height = static_cast<int32_t>(raw_height);
    uint16_t planes = 0;
    if (!cursor.u16le(planes) || !cursor.u16le(out.depth)) return false;
    if (!cursor.u16le(out.compression)) return false;
    uint32_t image_size = 0;
    if (!cursor.u32le(image_size)) return false;
    (void)image_size;  // a length the format lets a writer leave at zero
    uint32_t x_ppm = 0;
    uint32_t y_ppm = 0;
    if (!cursor.u32le(x_ppm) || !cursor.u32le(y_ppm)) return false;
    uint32_t used = 0;
    uint32_t important = 0;
    if (!cursor.u32le(used) || !cursor.u32le(important)) return false;
    (void)important;
    out.colours = used;
    if (out.compression == kCompressionBitfields) {
      // The masks are inside the bigger headers and after the 40 bytes of the everyday one (docs/30).
      const uint32_t masks_at = dib_at + ((header_size >= 52) ? 40 : header_size);
      if (masks_at + 12 > len) return false;
      Cursor masks{data, len, masks_at};
      if (!masks.u32le(out.masks[0]) || !masks.u32le(out.masks[1]) || !masks.u32le(out.masks[2])) {
        return false;
      }
    }
    out.palette_at = dib_at + header_size;
  }
  if (out.core) {
    out.compression = kCompressionRgb;
    out.palette_at = dib_at + kCoreHeader;
  } else if (out.compression != kCompressionRgb && out.compression != kCompressionBitfields) {
    return false;  // RLE, or a JPEG or a PNG inside a BMP: another decoder's business or nobody's
  }
  if (width <= 0 || height == 0) return false;
  if (out.depth != 1 && out.depth != 4 && out.depth != 8 && out.depth != 16 && out.depth != 24 &&
      out.depth != 32) {
    return false;
  }
  if (out.compression == kCompressionBitfields && out.depth != 16 && out.depth != 32) return false;
  // **A negative height is a picture that is stored from the top**: the format allows it and a picture read
  // the other way is upside down (docs/30).
  out.top_down = height < 0;
  out.width = width;
  out.height = (height < 0) ? -height : height;
  // A colour table has the entries the header says, or the ones the depth needs when it says nothing: a
  // picture that reads past its own table reads another part of the file and calls it a colour.
  if (out.depth <= 8) {
    const uint32_t most = 1u << out.depth;
    if (out.colours == 0 || out.colours > most) out.colours = most;
  } else {
    out.colours = 0;
  }
  return true;
}

// Where a stored row begins: a BMP keeps its rows padded to four bytes and, unless the height was negative,
// the last row is the first one in the file (docs/30).
uint32_t row_at(const Header& header, uint32_t y) {
  const uint32_t stride = ((static_cast<uint32_t>(header.width) * header.depth + 31) / 32) * 4;
  const uint32_t told = header.top_down ? y : static_cast<uint32_t>(header.height) - 1 - y;
  return header.pixel_at + told * stride;
}

// A colour of the table, as the three numbers the dither wants. OS/2 stores three and Windows four, and the
// fourth is not a colour of the picture (docs/30).
void table_colour(const Header& header, const uint8_t* data, uint32_t index, uint8_t& r, uint8_t& g,
                  uint8_t& b) {
  if (index >= header.colours) index = 0;
  const uint32_t at = header.palette_at + index * header.palette_size;
  r = data[at + 2];
  g = data[at + 1];
  b = data[at];
}

// One channel of a masked pixel, widened to eight bits (docs/30).
void masked_channel(uint32_t value, uint32_t mask, uint8_t& out) {
  uint32_t shift = 0;
  while (shift < 32 && ((mask >> shift) & 1u) == 0) ++shift;
  uint32_t width = 0;
  while (shift + width < 32 && ((mask >> (shift + width)) & 1u) != 0) ++width;
  if (width == 0) {
    out = 0;
    return;
  }
  const uint32_t raw = (value & mask) >> shift;
  const uint32_t top = (1u << width) - 1u;
  out = static_cast<uint8_t>((raw * 255u) / top);
}

// **The pixels of one row, whatever their width.** A BMP that ends early is `kTruncated` and not a read past
// the end (docs/70).
bool one_row(const Header& header, const uint8_t* data, uint32_t len, uint32_t y, Row& out) {
  const uint32_t at = row_at(header, y);
  const uint32_t stride = ((static_cast<uint32_t>(header.width) * header.depth + 31) / 32) * 4;
  if (at > len || stride > len - at) return false;
  const uint8_t* row = data + at;
  const uint32_t width = static_cast<uint32_t>(header.width);
  for (uint32_t x = 0; x < width; ++x) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    switch (header.depth) {
      case 1: {
        const uint8_t bit = static_cast<uint8_t>((row[x / 8] >> (7 - (x & 7))) & 1);
        table_colour(header, data, bit, r, g, b);
        break;
      }
      case 4: {
        const uint8_t nibble = (x & 1) == 0 ? (row[x / 2] >> 4) : (row[x / 2] & 0x0F);
        table_colour(header, data, nibble, r, g, b);
        break;
      }
      case 8:
        table_colour(header, data, row[x], r, g, b);
        break;
      case 16: {
        const uint16_t value = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
        if (header.masks[0] != 0) {
          masked_channel(value, header.masks[0], r);
          masked_channel(value, header.masks[1], g);
          masked_channel(value, header.masks[2], b);
        } else {
          // The 5-5-5 of the format when nobody said otherwise (docs/30).
          r = static_cast<uint8_t>(((value >> 10) & 0x1F) * 255u / 31u);
          g = static_cast<uint8_t>(((value >> 5) & 0x1F) * 255u / 31u);
          b = static_cast<uint8_t>((value & 0x1F) * 255u / 31u);
        }
        break;
      }
      case 24:
        b = row[x * 3];
        g = row[x * 3 + 1];
        r = row[x * 3 + 2];
        break;
      case 32: {
        const uint32_t value = static_cast<uint32_t>(row[x * 4]) |
                               (static_cast<uint32_t>(row[x * 4 + 1]) << 8) |
                               (static_cast<uint32_t>(row[x * 4 + 2]) << 16) |
                               (static_cast<uint32_t>(row[x * 4 + 3]) << 24);
        if (header.masks[0] != 0) {
          masked_channel(value, header.masks[0], r);
          masked_channel(value, header.masks[1], g);
          masked_channel(value, header.masks[2], b);
        } else {
          b = row[x * 4];
          g = row[x * 4 + 1];
          r = row[x * 4 + 2];
        }
        break;
      }
      default:
        return false;
    }
    row_put_rgb(out, r, g, b);
  }
  return true;
}

// The size check every reader here does before it puts a row on the stack.
Status sane(const Header& header) {
  if (header.width <= 0 || header.height <= 0) return kBadData;
  if (static_cast<uint32_t>(header.width) > kMaxSide || static_cast<uint32_t>(header.height) > kMaxSide) {
    return kUnsupported;
  }
  return kOk;
}

}  // namespace

Status bmp_info(const uint8_t* data, uint32_t len, Info& out) {
  Header header;
  if (!read_header(data, len, header)) return kUnknown;
  const Status status = sane(header);
  if (status != kOk) return status;
  out.format = "BMP";
  out.width = static_cast<uint16_t>(header.width);
  out.height = static_cast<uint16_t>(header.height);
  out.depth = static_cast<uint8_t>((header.depth <= 8) ? header.depth : 24);
  return kOk;
}

// **The row of the page the decoder builds is the block of the caller and not the stack** (0.118.0): two kilobytes of
// a task with seven is a fifth of what an app has, and the block is the extra memory of the board (docs/75). The file
// itself is still read where it lies.
uint32_t bmp_scratch(const uint8_t* data, uint32_t len) {
  (void)data;
  (void)len;
  return kRowBytes;
}

Status decode_bmp(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  if (scratch == nullptr || cap < kRowBytes) return kNoRoom;
  Header header;
  if (!read_header(data, len, header)) return kUnknown;
  const Status status = sane(header);
  if (status != kOk) return status;
  if (header.depth <= 8) {
    const uint32_t need = header.palette_at + header.colours * header.palette_size;
    if (need > len) return kTruncated;
  }
  // A row of the picture is made in the block of the caller —`(width + 1) / 2` bytes, which is the most a row of a
  // page can be— and the same block serves every row: a BMP is read in place and the row it builds is not on the
  // stack (0.118.0, docs/30, docs/75).
  uint8_t* packed = scratch;
  for (uint32_t y = 0; y < static_cast<uint32_t>(header.height); ++y) {
    Row row;
    row_start(row, packed, static_cast<uint16_t>(header.width), static_cast<uint16_t>(y));
    if (!one_row(header, data, len, y, row)) return kTruncated;
    row_end(row);
    sink.write(sink.ctx, static_cast<uint16_t>(y), packed);
  }
  return kOk;
}

}  // namespace detail
}  // namespace img
