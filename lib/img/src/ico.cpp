// **An ICO** (and a CUR, which is the same directory with a hotspot): a little table of pictures and, behind
// each entry, **a BMP without its file header or a whole PNG** (docs/30). A page draws the biggest one, which
// is what an icon of a window and a picture of a page have in common.
//
// The BMP of an icon is not quite the BMP of a file: its height in the header is **twice** the real one,
// because the colour rows are followed by the mask of one bit a pixel, and its 32 bits a pixel carry an alpha
// channel that an icon uses and a file of the older kind does not. The mask is not read: a pixel with an alpha
// of zero is the paper, and the mask is the same thing said in one bit (docs/30).
#include "img_private.h"

namespace img {
namespace detail {
namespace {

const uint32_t kEntry = 16;
const uint32_t kMaxEntries = 64;

struct Entry {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t bytes = 0;
  uint32_t at = 0;
  uint32_t area = 0;
};

// The directory: how many pictures and where each one is. The size of an entry is a byte and a zero is 256,
// which is the one thing about an ICO that surprises everybody (docs/30).
bool read_directory(const uint8_t* data, uint32_t len, Entry& best) {
  Cursor cursor{data, len, 0};
  if (!cursor.skip(4)) return false;  // the reserved word and the type, which the dispatcher has seen
  uint16_t count = 0;
  if (!cursor.u16le(count)) return false;
  if (count == 0 || count > kMaxEntries) return false;
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t colours = 0;
    uint8_t reserved = 0;
    uint16_t planes = 0;
    uint16_t bits = 0;
    uint32_t bytes = 0;
    uint32_t at = 0;
    if (!cursor.u8(width) || !cursor.u8(height) || !cursor.u8(colours) || !cursor.u8(reserved) ||
        !cursor.u16le(planes) || !cursor.u16le(bits) || !cursor.u32le(bytes) || !cursor.u32le(at)) {
      return false;
    }
    (void)colours;
    (void)reserved;
    (void)planes;
    (void)bits;
    if (at > len || bytes > len - at) continue;  // an entry that is not in the file is not a picture
    const uint32_t real_width = (width == 0) ? 256 : width;
    const uint32_t real_height = (height == 0) ? 256 : height;
    const uint32_t area = real_width * real_height;
    if (i == 0 || area > best.area) {
      best.width = real_width;
      best.height = real_height;
      best.bytes = bytes;
      best.at = at;
      best.area = area;
    }
  }
  return best.bytes != 0;
}

bool is_png_entry(const uint8_t* data, const Entry& entry) {
  static const uint8_t kPngMark[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (entry.bytes < sizeof(kPngMark)) return false;
  for (uint32_t i = 0; i < sizeof(kPngMark); ++i) {
    if (data[entry.at + i] != kPngMark[i]) return false;
  }
  return true;
}

// **The BMP of an icon**: the header of forty bytes, the colours of a palette when there is one, and the rows
// from the bottom, with the alpha of a 32-bit picture used as transparency (docs/30).
struct Dib {
  uint32_t width = 0;
  uint32_t height = 0;  // the real one, which is half of what the header says
  uint16_t depth = 0;
  uint32_t palette_at = 0;
  uint32_t colours = 0;
  uint32_t rows_at = 0;
};

bool read_dib(const uint8_t* data, const Entry& entry, Dib& out) {
  if (entry.bytes < 40) return false;
  Cursor dib{data, entry.at + entry.bytes, entry.at};
  uint32_t header_size = 0;
  int32_t width = 0;
  int32_t height = 0;
  uint16_t planes = 0;
  uint16_t depth = 0;
  uint32_t compression = 0;
  if (!dib.u32le(header_size)) return false;
  if (header_size < 40 || header_size > entry.bytes) return false;
  uint32_t raw_width = 0;
  uint32_t raw_height = 0;
  if (!dib.u32le(raw_width) || !dib.u32le(raw_height)) return false;
  width = static_cast<int32_t>(raw_width);
  height = static_cast<int32_t>(raw_height);
  if (!dib.u16le(planes) || !dib.u16le(depth) || !dib.u32le(compression)) return false;
  if (width <= 0 || height <= 0 || compression != 0) return false;
  if (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32) return false;
  out.width = static_cast<uint32_t>(width);
  // The header counts the colour rows **and the mask** (docs/30).
  out.height = static_cast<uint32_t>(height) / 2;
  if (out.height == 0) return false;
  out.depth = depth;
  out.palette_at = entry.at + header_size;
  out.colours = 0;
  if (depth <= 8) {
    uint32_t used = 0;
    Cursor palette_count{data, entry.at + entry.bytes, entry.at + 32};
    if (!palette_count.u32le(used)) return false;
    out.colours = (used == 0 || used > (1u << depth)) ? (1u << depth) : used;
    if (out.palette_at + out.colours * 4 > entry.at + entry.bytes) return false;
  }
  out.rows_at = out.palette_at + out.colours * 4;
  return true;
}

void dib_colour(const uint8_t* data, const Dib& dib, uint32_t index, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (index >= dib.colours) index = 0;
  const uint32_t at = dib.palette_at + index * 4;
  b = data[at];
  g = data[at + 1];
  r = data[at + 2];
}

}  // namespace

Status ico_info(const uint8_t* data, uint32_t len, Info& out) {
  Entry entry;
  if (!read_directory(data, len, entry)) return kUnknown;
  if (is_png_entry(data, entry)) {
    Info inner;
    const Status status = png_info(data + entry.at, entry.bytes, inner);
    if (status != kOk) return status;
    out = inner;
    out.format = "ICO";
    return kOk;
  }
  Dib dib;
  if (!read_dib(data, entry, dib)) return kBadData;
  if (dib.width > kMaxSide || dib.height > kMaxSide) return kUnsupported;
  out.format = "ICO";
  out.width = static_cast<uint16_t>(dib.width);
  out.height = static_cast<uint16_t>(dib.height);
  out.depth = static_cast<uint8_t>((dib.depth <= 8) ? dib.depth : 24);
  return kOk;
}

uint32_t ico_scratch(const uint8_t* data, uint32_t len) {
  Entry entry;
  if (!read_directory(data, len, entry)) return 0;
  // A PNG inside an icon needs what a PNG needs; a BMP inside one is read in place (docs/30).
  if (is_png_entry(data, entry)) return png_scratch(data + entry.at, entry.bytes);
  return 0;
}

Status decode_ico(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  Entry entry;
  if (!read_directory(data, len, entry)) return kUnknown;
  if (is_png_entry(data, entry)) return decode_png(data + entry.at, entry.bytes, sink, scratch, cap);
  (void)scratch;
  (void)cap;
  Dib dib;
  if (!read_dib(data, entry, dib)) return kBadData;
  if (dib.width > kMaxSide || dib.height > kMaxSide) return kUnsupported;
  const uint32_t stride = ((dib.width * dib.depth + 31) / 32) * 4;
  if (dib.rows_at > len || stride * dib.height > len - dib.rows_at) return kTruncated;
  uint8_t packed[(kMaxSide + 1) / 2];
  for (uint32_t y = 0; y < dib.height; ++y) {
    // An icon is stored from the bottom like a BMP (docs/30).
    const uint8_t* row = data + dib.rows_at + (dib.height - 1 - y) * stride;
    Row out;
    row_start(out, packed, static_cast<uint16_t>(dib.width), static_cast<uint16_t>(y));
    for (uint32_t x = 0; x < dib.width; ++x) {
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      uint8_t alpha = 255;
      switch (dib.depth) {
        case 1: {
          const uint8_t bit = static_cast<uint8_t>((row[x / 8] >> (7 - (x & 7))) & 1);
          dib_colour(data, dib, bit, r, g, b);
          break;
        }
        case 4: {
          const uint8_t nibble = (x & 1) == 0 ? (row[x / 2] >> 4) : (row[x / 2] & 0x0F);
          dib_colour(data, dib, nibble, r, g, b);
          break;
        }
        case 8:
          dib_colour(data, dib, row[x], r, g, b);
          break;
        case 24:
          b = row[x * 3];
          g = row[x * 3 + 1];
          r = row[x * 3 + 2];
          break;
        case 32:
          b = row[x * 4];
          g = row[x * 4 + 1];
          r = row[x * 4 + 2];
          alpha = row[x * 4 + 3];
          break;
        default:
          return kUnsupported;
      }
      if (alpha == 0) {
        row_put(out, 15);  // an icon on the paper, and the paper is white (docs/30)
        continue;
      }
      if (alpha == 255) {
        row_put_rgb(out, r, g, b);
        continue;
      }
      const uint32_t inv = 255u - alpha;
      row_put_rgb(out, static_cast<uint8_t>((r * alpha + 255u * inv) / 255u),
                  static_cast<uint8_t>((g * alpha + 255u * inv) / 255u),
                  static_cast<uint8_t>((b * alpha + 255u * inv) / 255u));
    }
    row_end(out);
    sink.write(sink.ctx, static_cast<uint16_t>(y), packed);
  }
  return kOk;
}

}  // namespace detail
}  // namespace img
