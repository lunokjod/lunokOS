// **The netpbm family**: `PBM` (two colours), `PGM` (grey) and `PPM` (colour), in the plain flavour (`P1`,
// `P2`, `P3`, which is text and easy to read with a person's eyes) and the raw one (`P4`, `P5`, `P6`, which
// is what a script writes). It is the format a tool writes in one line and the one a test uses to say
// exactly which pixels a picture has, so it lives here next to the decoders of the big families: a picture
// is a picture whatever wrote it (docs/30).
#include "img_private.h"

namespace img {
namespace detail {
namespace {

// **A header of netpbm is words separated by whitespace and comments**, and the comments start with '#' and
// run to the end of the line: the one thing a reader of this format gets wrong is a '#' inside a number
// (docs/30).
struct Words {
  const uint8_t* data;
  uint32_t len;
  uint32_t at;

  bool next(uint32_t& value) {
    while (at < len) {
      const uint8_t c = data[at];
      if (c == '#') {
        while (at < len && data[at] != '\n') ++at;
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++at;
        continue;
      }
      break;
    }
    if (at >= len) return false;
    uint32_t made = 0;
    bool any = false;
    while (at < len) {
      const uint8_t c = data[at];
      if (c < '0' || c > '9') break;
      made = made * 10 + static_cast<uint32_t>(c - '0');
      any = true;
      ++at;
    }
    value = made;
    return any;
  }
};

struct Header {
  uint8_t kind = 0;  // 1..6, the digit of the signature
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t most = 255;  // the greatest value of a sample, for P2, P3, P5 and P6
};

bool read_header(const uint8_t* data, uint32_t len, Header& out, uint32_t& samples_at) {
  if (len < 2 || data[0] != 'P' || data[1] < '1' || data[1] > '6') return false;
  out.kind = static_cast<uint8_t>(data[1] - '0');
  Words words{data, len, 2};
  if (!words.next(out.width) || !words.next(out.height)) return false;
  if (out.kind == 2 || out.kind == 3 || out.kind == 5 || out.kind == 6) {
    if (!words.next(out.most) || out.most == 0 || out.most > 65535) return false;
  }
  if (out.width == 0 || out.height == 0) return false;
  // The text flavours keep a single whitespace after the last word and the raw ones keep exactly one: the
  // samples begin after it (docs/30).
  if (out.kind >= 4) {
    if (words.at >= len) return false;
    if (data[words.at] != ' ' && data[words.at] != '\n' && data[words.at] != '\r' &&
        data[words.at] != '\t') {
      return false;
    }
    ++words.at;
  }
  samples_at = words.at;
  return true;
}

Status sane(const Header& header) {
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  return kOk;
}

}  // namespace

Status pnm_info(const uint8_t* data, uint32_t len, Info& out) {
  Header header;
  uint32_t samples_at = 0;
  if (!read_header(data, len, header, samples_at)) return kUnknown;
  const Status status = sane(header);
  if (status != kOk) return status;
  switch (header.kind) {
    case 1:
    case 4:
      out.format = "PBM";
      out.depth = 1;
      break;
    case 2:
    case 5:
      out.format = "PGM";
      out.depth = 8;
      break;
    default:
      out.format = "PPM";
      out.depth = 24;
      break;
  }
  out.width = static_cast<uint16_t>(header.width);
  out.height = static_cast<uint16_t>(header.height);
  return kOk;
}

// **The row of the page is the block of the caller and not the stack** (0.118.0): the file is still read where it lies
// and what the decoder builds is the row it hands to the sink (docs/75).
uint32_t pnm_scratch(const uint8_t* data, uint32_t len) {
  (void)data;
  (void)len;
  return kRowBytes;
}

Status decode_pnm(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  if (scratch == nullptr || cap < kRowBytes) return kNoRoom;
  Header header;
  uint32_t at = 0;
  if (!read_header(data, len, header, at)) return kUnknown;
  const Status status = sane(header);
  if (status != kOk) return status;

  uint8_t* packed = scratch;
  Words words{data, len, at};
  // A sample of a raw file is one byte, or two when the greatest value needs them: the format says big endian
  // and the reader widens both to the eight bits a page has (docs/30).
  const uint32_t wide = (header.most > 255) ? 2 : 1;
  for (uint32_t y = 0; y < header.height; ++y) {
    Row row;
    row_start(row, packed, static_cast<uint16_t>(header.width), static_cast<uint16_t>(y));
    for (uint32_t x = 0; x < header.width; ++x) {
      uint32_t r = 0;
      uint32_t g = 0;
      uint32_t b = 0;
      if (header.kind <= 3) {
        if (header.kind == 1) {
          // A bit: 1 is black and 0 is white, which is the opposite of every other format here (docs/30).
          uint32_t bit = 0;
          if (!words.next(bit)) return kTruncated;
          const uint8_t level = (bit != 0) ? 0 : 255;
          r = g = b = level;
        } else if (header.kind == 2) {
          if (!words.next(r)) return kTruncated;
          r = r * 255u / header.most;
          g = b = r;
        } else {
          if (!words.next(r) || !words.next(g) || !words.next(b)) return kTruncated;
          r = r * 255u / header.most;
          g = g * 255u / header.most;
          b = b * 255u / header.most;
        }
      } else if (header.kind == 4) {
        // Eight pixels are packed into one byte of a PBM and the bits are ones and zeros, high bit first.
        if (at >= len || at + x / 8 >= len) return kTruncated;
        const uint8_t bit = static_cast<uint8_t>((data[at + x / 8] >> (7 - (x & 7))) & 1);
        const uint8_t level = (bit != 0) ? 0 : 255;
        r = g = b = level;
      } else {
        const uint32_t need = (header.kind == 6) ? 3 : 1;
        if (at > len || (x * need + need) * wide > len - at) return kTruncated;
        const uint8_t* sample = data + at + (x * need) * wide;
        uint32_t values[3] = {0, 0, 0};
        for (uint32_t c = 0; c < need; ++c) {
          const uint32_t value = (wide == 2)
                                     ? (static_cast<uint32_t>(sample[c * 2]) << 8) | sample[c * 2 + 1]
                                     : sample[c];
          values[c] = value * 255u / header.most;
        }
        r = values[0];
        g = (need == 3) ? values[1] : values[0];
        b = (need == 3) ? values[2] : values[0];
      }
      row_put_rgb(row, static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
    }
    // A raw row moves the cursor and a text one does it word by word: they are two loops because a P4 without
    // a row and a P5 without a byte are two different mistakes and both are `kTruncated` (docs/30).
    if (header.kind >= 4) {
      const uint32_t bytes_per_row = (header.kind == 4) ? (header.width + 7) / 8
                                                        : header.width * wide * ((header.kind == 6) ? 3 : 1);
      // The rows of a raw file are followed by nothing: each one is exactly the bytes of its samples.
      at += bytes_per_row;
      if (at > len) return kTruncated;
    }
    row_end(row);
    sink.write(sink.ctx, static_cast<uint16_t>(y), packed);
  }
  return kOk;
}

}  // namespace detail
}  // namespace img
