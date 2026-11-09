// **A PNG** (RFC 2083): the eight bytes of its signature, chunks with a length and a CRC, and a picture that
// is a zlib stream of filtered rows. What is read is what a page meets: every colour type and every depth the
// format allows (one, two, four, eight and sixteen bits; grey, RGB, palette, grey with alpha and RGB with
// alpha), every filter (none, sub, up, average, Paeth) —**and not the interlaced flavour**, which is refused
// with `kUnsupported` because half a picture drawn well is worse than a picture not drawn— (docs/30).
//
// The rows are inflated into the block the caller gave (`scratch_bytes` says how big) and unfiltered **in
// place**, which is why the whole picture has to fit there: a page that decodes a photograph of a few
// hundred kilobytes into a panel of sixteen levels does not keep two copies of it (docs/30, docs/75).
#include "img_private.h"

#include "inflate/inflate.h"

namespace img {
namespace detail {
namespace {

const uint32_t kHeader = 13;  // the thirteen bytes of an IHDR

struct Header {
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t depth = 0;
  uint8_t colour = 0;
  uint8_t interlace = 0;
  uint32_t channels = 0;  // samples of a pixel, before the palette
  uint32_t row_bytes = 0;  // what a row takes unfiltered, without the filter byte
  uint32_t idat_at = 0;
  uint32_t idat_len = 0;
  bool had_palette = false;
  bool had_trns = false;
  uint32_t palette_at = 0;
  uint32_t palette_len = 0;  // entries, not bytes
  uint32_t trns_at = 0;
  uint32_t trns_len = 0;
};

// The channels of a colour type (RFC 2083, 11.2.2), and the depths each one allows.
bool channels_of(uint8_t colour, uint8_t depth, uint32_t& channels) {
  switch (colour) {
    case 0:
      if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return false;
      channels = 1;
      return true;
    case 2:
      if (depth != 8 && depth != 16) return false;
      channels = 3;
      return true;
    case 3:
      if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return false;
      channels = 1;
      return true;
    case 4:
      if (depth != 8 && depth != 16) return false;
      channels = 2;
      return true;
    case 6:
      if (depth != 8 && depth != 16) return false;
      channels = 4;
      return true;
    default:
      return false;
  }
}

// **The chunks, in one walk**: the header, the palette, the transparency and where the compressed rows are.
// The chunks of `IDAT` that follow each other are one stream and are read as one; a picture whose rows are
// written in several runs of `IDAT` separated by other chunks is refused, because inflating it would need a
// second copy of the file (docs/30).
Status read_chunks(const uint8_t* data, uint32_t len, Header& out) {
  Cursor cursor{data, len, 8};  // past the signature
  bool saw_header = false;
  bool saw_idat = false;
  bool run_closed = false;  // a chunk that is not IDAT after the rows began
  while (cursor.has(8)) {
    uint32_t size = 0;
    if (!cursor.u32be(size)) return kTruncated;
    if (size > 0x7FFFFFFFu) return kBadData;
    const uint8_t* type = cursor.here();
    if (!cursor.skip(4) || !cursor.has(size + 4)) return kTruncated;
    const uint32_t body_at = cursor.at;
    const char* name = reinterpret_cast<const char*>(type);
    if (name[0] == 'I' && name[1] == 'H' && name[2] == 'D' && name[3] == 'R') {
      if (size != kHeader || saw_header) return kBadData;
      Cursor body{data, len, body_at};
      uint8_t depth = 0;
      uint8_t colour = 0;
      uint8_t compression = 0;
      uint8_t filter = 0;
      uint8_t interlace = 0;
      if (!body.u32be(out.width) || !body.u32be(out.height) || !body.u8(depth) || !body.u8(colour) ||
          !body.u8(compression) || !body.u8(filter) || !body.u8(interlace)) {
        return kTruncated;
      }
      if (out.width == 0 || out.height == 0) return kBadData;
      if (compression != 0 || filter != 0) return kBadData;  // the only ones the format defines
      if (!channels_of(colour, depth, out.channels)) return kBadData;
      out.depth = depth;
      out.colour = colour;
      out.interlace = interlace;
      out.row_bytes = static_cast<uint32_t>(
          (static_cast<uint64_t>(out.width) * out.channels * depth + 7) / 8);
      saw_header = true;
    } else if (name[0] == 'P' && name[1] == 'L' && name[2] == 'T' && name[3] == 'E') {
      if (size == 0 || size % 3 != 0 || size > 3 * 256) return kBadData;
      out.had_palette = true;
      out.palette_at = body_at;
      out.palette_len = size / 3;
    } else if (name[0] == 't' && name[1] == 'R' && name[2] == 'N' && name[3] == 'S') {
      out.had_trns = true;
      out.trns_at = body_at;
      out.trns_len = size;
    } else if (name[0] == 'I' && name[1] == 'D' && name[2] == 'A' && name[3] == 'T') {
      // **The rows of a picture are one zlib stream**: the chunks of IDAT that follow each other are read as
      // one, and a second run of them —after some other chunk— would need a copy of the file to inflate
      // (docs/30).
      if (run_closed) return kUnsupported;
      if (!saw_idat) {
        out.idat_at = body_at;
        out.idat_len = size;
        saw_idat = true;
      } else if (out.idat_at + out.idat_len == body_at) {
        out.idat_len += size;
      } else {
        return kUnsupported;
      }
    } else if (name[0] == 'I' && name[1] == 'E' && name[2] == 'N' && name[3] == 'D') {
      break;
    } else if ((name[0] & 0x20) == 0) {
      // A chunk a reader must understand to draw the picture (the four letters are upper case): refused
      // rather than guessed (RFC 2083, 4.1).
      return kUnsupported;
    }
    // Any chunk that is not IDAT after the rows began closes the run.
    if (!(name[0] == 'I' && name[1] == 'D' && name[2] == 'A' && name[3] == 'T') && saw_idat) {
      run_closed = true;
    }
    if (!cursor.skip(size + 4)) return kTruncated;  // the body and its CRC
  }
  if (!saw_header || out.idat_len == 0) return kTruncated;
  if (out.colour == 3 && !out.had_palette) return kBadData;
  if (out.interlace != 0) return kUnsupported;  // Adam7: refused with a reason, not drawn wrong (docs/30)
  return kOk;
}

// One sample of a row, at a depth that is not a whole number of bytes (RFC 2083, 7.2).
uint32_t sample_at(const uint8_t* row, uint32_t index, uint8_t depth) {
  switch (depth) {
    case 1: {
      const uint8_t byte = row[index / 8];
      return (byte >> (7 - (index & 7))) & 1u;
    }
    case 2: {
      const uint8_t byte = row[index / 4];
      return (byte >> (6 - 2 * (index & 3))) & 3u;
    }
    case 4: {
      const uint8_t byte = row[index / 2];
      return (index & 1) == 0 ? (byte >> 4) : (byte & 0x0F);
    }
    default:
      break;
  }
  return row[index];
}

// A sample widened to the eight bits a page has. A sixteen-bit sample keeps its high byte, which is what the
// eye keeps too (docs/30).
uint8_t to_byte(uint32_t value, uint8_t depth) {
  if (depth == 16) return static_cast<uint8_t>(value >> 8);
  if (depth == 8) return static_cast<uint8_t>(value);
  const uint32_t top = (1u << depth) - 1u;
  return static_cast<uint8_t>((value * 255u) / top);
}

// The five filters of the format, undone in place (RFC 2083, 6). `up` is the row above, already undone.
void unfilter(uint8_t filter, uint8_t* row, const uint8_t* up, uint32_t bytes, uint32_t bpp) {
  switch (filter) {
    case 0:
      break;
    case 1:
      for (uint32_t i = bpp; i < bytes; ++i) row[i] = static_cast<uint8_t>(row[i] + row[i - bpp]);
      break;
    case 2:
      if (up != nullptr) {
        for (uint32_t i = 0; i < bytes; ++i) row[i] = static_cast<uint8_t>(row[i] + up[i]);
      }
      break;
    case 3:
      for (uint32_t i = 0; i < bytes; ++i) {
        const uint32_t left = (i >= bpp) ? row[i - bpp] : 0;
        const uint32_t above = (up != nullptr) ? up[i] : 0;
        row[i] = static_cast<uint8_t>(row[i] + ((left + above) / 2));
      }
      break;
    case 4:
      for (uint32_t i = 0; i < bytes; ++i) {
        const int32_t a = (i >= bpp) ? row[i - bpp] : 0;
        const int32_t b = (up != nullptr) ? up[i] : 0;
        const int32_t c = (i >= bpp && up != nullptr) ? up[i - bpp] : 0;
        const int32_t p = a + b - c;
        const int32_t pa = p > a ? p - a : a - p;
        const int32_t pb = p > b ? p - b : b - p;
        const int32_t pc = p > c ? p - c : c - p;
        const int32_t predictor = (pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c);
        row[i] = static_cast<uint8_t>(row[i] + predictor);
      }
      break;
    default:
      break;  // a filter the format does not define: the row is drawn as it came, which is what a reader of an
              // unknown filter can do without lying about the rest of the picture
  }
}

// One row of the picture, from the unfiltered bytes to the packed row the glass takes.
void one_row(const Header& header, const uint8_t* data, const uint8_t* row, Row& out) {
  const uint32_t width = header.width;
  const uint8_t depth = header.depth;
  for (uint32_t x = 0; x < width; ++x) {
    const uint32_t first = x * header.channels;
    if (header.colour == 3) {
      // A palette picture: the sample is an index and the colour is in the table (docs/30).
      uint32_t index = sample_at(row, first, depth);
      if (index >= header.palette_len) index = 0;  // a picture that names a colour it does not carry
      const uint32_t at = header.palette_at + index * 3;
      uint8_t alpha = 255;
      if (header.had_trns && index < header.trns_len) {
        alpha = data[header.trns_at + index];
      }
      row_put_alpha(out, static_cast<uint8_t>(alpha / 17));
      if (alpha == 0) {
        row_put(out, 15);  // transparent is the paper
        continue;
      }
      const uint8_t r = data[at];
      const uint8_t g = data[at + 1];
      const uint8_t b = data[at + 2];
      if (alpha == 255) {
        row_put_rgb(out, r, g, b);
      } else {
        // **Over the paper**: an e-ink page is white and a picture with transparency is a picture on white
        // (docs/30, docs/50).
        const uint32_t inv = 255u - alpha;
        row_put_rgb(out, static_cast<uint8_t>((r * alpha + 255u * inv) / 255u),
                    static_cast<uint8_t>((g * alpha + 255u * inv) / 255u),
                    static_cast<uint8_t>((b * alpha + 255u * inv) / 255u));
      }
      continue;
    }
    if (depth == 16) {
      // Sixteen bits a sample: the bytes are big endian pairs and the high one is the eight bits kept
      // (docs/30).
      const uint32_t r = row[(first + 0) * 2];
      const uint32_t g = (header.channels >= 3) ? row[(first + 1) * 2] : r;
      const uint32_t b = (header.channels >= 3) ? row[(first + 2) * 2] : r;
      uint32_t alpha = 255;
      if (header.channels == 2) alpha = row[(first + 1) * 2];
      if (header.channels == 4) alpha = row[(first + 3) * 2];
      row_put_alpha(out, static_cast<uint8_t>(alpha / 17));
      if (alpha == 0) {
        row_put(out, 15);
        continue;
      }
      if (alpha == 255) {
        row_put_rgb(out, static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
      } else {
        const uint32_t inv = 255u - alpha;
        row_put_rgb(out, static_cast<uint8_t>((r * alpha + 255u * inv) / 255u),
                    static_cast<uint8_t>((g * alpha + 255u * inv) / 255u),
                    static_cast<uint8_t>((b * alpha + 255u * inv) / 255u));
      }
      continue;
    }
    const uint32_t r = sample_at(row, first + 0, depth);
    const uint32_t g = (header.channels >= 3) ? sample_at(row, first + 1, depth) : r;
    const uint32_t b = (header.channels >= 3) ? sample_at(row, first + 2, depth) : r;
    uint32_t alpha = 255;
    if (header.channels == 2) alpha = sample_at(row, first + 1, depth);
    if (header.channels == 4) alpha = sample_at(row, first + 3, depth);
    // **A colour key** is transparency said of one colour of a picture without an alpha channel (RFC 2083,
    // 11.3.2): the samples are compared before they are widened.
    if (header.had_trns && header.channels <= 3 && header.trns_len >= 2 * header.channels) {
      uint32_t key[3] = {0, 0, 0};
      bool same = true;
      for (uint32_t c = 0; c < header.channels; ++c) {
        key[c] = (static_cast<uint32_t>(data[header.trns_at + c * 2]) << 8) |
                 data[header.trns_at + c * 2 + 1];
        if (sample_at(row, first + c, depth) != key[c]) same = false;
      }
      if (same) alpha = 0;
    }
    const uint8_t r8 = to_byte(r, depth);
    const uint8_t g8 = to_byte(g, depth);
    const uint8_t b8 = to_byte(b, depth);
    const uint8_t a8 = (header.channels == 1 || header.channels == 3) ? static_cast<uint8_t>(alpha)
                                                                      : to_byte(alpha, depth);
    row_put_alpha(out, static_cast<uint8_t>(a8 / 17));
    if (a8 == 0) {
      row_put(out, 15);
      continue;
    }
    if (a8 == 255) {
      row_put_rgb(out, r8, g8, b8);
    } else {
      const uint32_t inv = 255u - a8;
      row_put_rgb(out, static_cast<uint8_t>((r8 * a8 + 255u * inv) / 255u),
                  static_cast<uint8_t>((g8 * a8 + 255u * inv) / 255u),
                  static_cast<uint8_t>((b8 * a8 + 255u * inv) / 255u));
    }
  }
}

}  // namespace

Status png_info(const uint8_t* data, uint32_t len, Info& out) {
  Header header;
  const Status read = read_chunks(data, len, header);
  if (read != kOk) return read;
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  out.format = "PNG";
  out.width = static_cast<uint16_t>(header.width);
  out.height = static_cast<uint16_t>(header.height);
  out.depth = static_cast<uint8_t>(header.depth * header.channels);
  return kOk;
}

// **The row of the page is the block of the caller and not the stack** (0.118.0): the picture is inflated at once and
// the row the decoder builds before it writes it —the widest a page can be— goes after it, aligned (docs/75).
uint32_t png_state_at(uint32_t inflated) { return aligned_at(inflated); }

uint32_t png_scratch(const uint8_t* data, uint32_t len) {
  Header header;
  if (read_chunks(data, len, header) != kOk) return 0;
  // **A row of a PNG costs its filter byte and its samples**, and the whole picture is inflated at once: the
  // number the caller has to give is the picture as the file describes it (docs/30, docs/75).
  const uint64_t inflated = static_cast<uint64_t>(header.row_bytes + 1) * header.height;
  // **Two rows and not one** (0.169.0): the colours of a row and, when the caller asks for the picture's transparency, the
  // mask of the same row. It is asked for here whether or not the mask is wanted, because `scratch_bytes` is what the caller
  // allocates before it knows what it will ask —2 KB of its block and not of the firmware's data— (docs/75).
  const uint64_t bytes = static_cast<uint64_t>(png_state_at(static_cast<uint32_t>(inflated))) + 2u * kRowBytes;
  if (bytes > 0xFFFFFFFFu) return 0;
  return static_cast<uint32_t>(bytes);
}

Status decode_png(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap,
                  const RowSink* mask) {
  Header header;
  const Status read = read_chunks(data, len, header);
  if (read != kOk) return read;
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  const uint32_t need = (header.row_bytes + 1) * header.height;
  // **Two rows after the state, asked for whether or not the mask is wanted** (0.169.0): the colours and the transparency
  // of the same row, one after the other, in the block the caller gave —a decoder's memory lives in the caller's scratch
  // and never in the static data of the firmware, which is what the DRAM of the Paper answered with an overflow of 1 064
  // bytes when the mask row was a `static` here— (docs/75). **The check is the number `png_scratch` asked for and not one
  // of its own** (0.169.2): a caller that allocates what it was told and gets a byte less has to be told `kNoRoom`, and
  // here the two rows were only counted when a mask was asked, so a block one byte short of the answer was accepted
  // (found by this suite, 9/11).
  if (scratch == nullptr || cap < png_state_at(need) + 2u * kRowBytes) return kNoRoom;
  uint32_t written = 0;
  const inflate::Status status =
      inflate::zlib(data + header.idat_at, header.idat_len, scratch, need, written);
  if (status == inflate::kNoRoom) return kNoRoom;
  if (status != inflate::kOk) return kBadData;
  if (written < need) return kTruncated;

  const uint32_t bpp = static_cast<uint32_t>((header.channels * header.depth + 7) / 8);
  const uint32_t stride = header.row_bytes + 1;
  uint8_t* packed = scratch + png_state_at(need);
  for (uint32_t y = 0; y < header.height; ++y) {
    uint8_t* filter = scratch + y * stride;
    uint8_t* row = filter + 1;
    const uint8_t* up = (y > 0) ? scratch + (y - 1) * stride + 1 : nullptr;
    unfilter(filter[0], row, up, header.row_bytes, bpp);
    Row out;
    // **The mask of this row, when the caller asked for one** (0.169.0): the same row of the picture, one nibble a pixel,
    // filled by `one_row` as it goes and given to the caller **before** the colours, so a reader that draws as it reads
    // has it in its hand when the pixels arrive (docs/30).
    uint8_t* mask_row = (mask != nullptr) ? (packed + kRowBytes) : nullptr;
    out.alpha = mask_row;
    row_start(out, packed, static_cast<uint16_t>(header.width), static_cast<uint16_t>(y));
    one_row(header, data, row, out);
    row_end(out);
    if (mask != nullptr) mask->write(mask->ctx, static_cast<uint16_t>(y), mask_row);
    sink.write(sink.ctx, static_cast<uint16_t>(y), packed);
  }
  return kOk;
}

}  // namespace detail
}  // namespace img
