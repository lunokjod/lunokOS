// **A JPEG of the kind a camera and a phone write**: baseline sequential Huffman (and the extended sequential
// flavour, which is the same reader), one or three components, every sampling factor, restart markers. What
// is **not** read is refused with `kUnsupported` and not drawn wrong: the progressive flavour (SOF2, whose
// reader is a different decoder), the arithmetic coding of JPEG, and the four components of a CMYK file
// (docs/30).
//
// The picture is decoded **a band of MCUs at a time**: eight or sixteen rows of every component in the block
// the caller gave, and then those rows are turned into grey and written to the sink, which is what lets a
// photograph of a megapixel be read on a board whose heap is smaller than the picture (docs/30, docs/75).
#include <math.h>

#include "img_private.h"

namespace img {
namespace detail {
namespace {

const uint32_t kMaxComponents = 3;
const uint32_t kMaxBlocks = 10;  // ten blocks of eight by eight is the biggest MCU a JPEG may have

// The two tables of a Huffman-coded file: the codes and their lengths, in the order the file gave them
// (ITU T.81, B.2.4.2).
struct Huffman {
  uint8_t value[256];
  uint16_t count = 0;
  // The canonical table of the format, kept so a code is found in one step: the first code of each length,
  // where its values begin, and how many there are (ITU T.81, C.2).
  uint16_t first_code[17];
  uint16_t first_index[17];
  uint16_t of_length[17];
};

struct Component {
  uint8_t id = 0;
  uint8_t h = 1;
  uint8_t v = 1;
  uint8_t quant = 0;
  uint8_t dc = 0;
  uint8_t ac = 0;
  int32_t predictor = 0;
};

struct Header {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t components = 0;
  Component comp[kMaxComponents];
  uint32_t max_h = 1;
  uint32_t max_v = 1;
  uint16_t quant[4][64];
  bool have_quant[4] = {false, false, false, false};
  Huffman dc[4];
  Huffman ac[4];
  bool have_dc[4] = {false, false, false, false};
  bool have_ac[4] = {false, false, false, false};
  uint32_t restart_interval = 0;
  uint32_t entropy_at = 0;  // where the bits of the first scan start
};

// **A Huffman table, made canonical** (ITU T.81, C.2): the file gives the lengths and the values, and the codes
// are the ones that follow from them in order.
bool build_huffman(Huffman& table, const uint8_t* counts, const uint8_t* values, uint32_t values_len) {
  uint32_t total = 0;
  for (uint32_t i = 0; i < 16; ++i) total += counts[i];
  if (total > 256 || total != values_len) return false;
  uint32_t code = 0;
  uint32_t at = 0;
  table.count = static_cast<uint16_t>(total);
  for (uint32_t length = 1; length <= 16; ++length) {
    table.first_code[length] = static_cast<uint16_t>(code);
    table.first_index[length] = static_cast<uint16_t>(at);
    table.of_length[length] = counts[length - 1];
    for (uint32_t i = 0; i < counts[length - 1]; ++i) {
      table.value[at] = values[at];
      ++at;
    }
    code = (code + counts[length - 1]) << 1;
  }
  return true;
}

// **The bits of the scan**, with the stuffing of the format: a zero after a FF is a byte of the stream and any
// other byte after a FF is a marker that ends it (ITU T.81, B.1.1.5 and B.2.3).
struct Bits {
  const uint8_t* data = nullptr;
  uint32_t len = 0;
  uint32_t at = 0;
  uint32_t bits = 0;
  uint32_t count = 0;
  bool ended = false;

  void fill() {
    while (count <= 24) {
      uint8_t byte = 0;
      if (at < len) {
        byte = data[at];
        if (byte == 0xFF) {
          if (at + 1 < len && data[at + 1] == 0x00) {
            at += 2;  // the stuffing: this FF is a byte of the stream
          } else {
            ended = true;  // a marker: the scan stops here
            byte = 0;
          }
        } else {
          ++at;
        }
      } else {
        ended = true;
      }
      bits = (bits << 8) | byte;
      count += 8;
    }
  }

  uint32_t take(uint32_t how_many) {
    if (how_many == 0) return 0;
    if (count < how_many) fill();
    if (count < how_many) return 0;
    count -= how_many;
    return (bits >> count) & ((1u << how_many) - 1u);
  }

  uint32_t bit() { return take(1); }

  // **A code of the table, found in one step per bit and not in one per entry**: a photograph of a page is
  // eight thousand blocks and a scan of the table for every bit is a decoder that takes a minute
  // (ITU T.81, F.2.2.3).
  bool decode(const Huffman& table, uint16_t& value) {
    uint32_t code = 0;
    for (uint32_t length = 1; length <= 16; ++length) {
      code = (code << 1) | bit();
      const uint32_t first = table.first_code[length];
      if (code >= first && code - first < table.of_length[length]) {
        value = table.value[table.first_index[length] + (code - first)];
        return true;
      }
    }
    return false;
  }

  // How many bits to skip to be on the next byte boundary, which is what a restart does (ITU T.81, B.2.3).
  void align() { count -= count % 8; }
};

// **The inverse transform**: the cosine of the eight points, in the float the chip has. A block of a JPEG is
// sixty-four numbers and a photograph of a page is eight thousand of them; the levels of the glass are
// sixteen, and the difference between a float and a fixed point at this size is far below one of them
// (docs/30, docs/85).
void idct(const int32_t* in, uint8_t* out, uint32_t stride) {
  static float cosines[8][8];
  static bool made = false;
  if (!made) {
    for (uint32_t u = 0; u < 8; ++u) {
      for (uint32_t x = 0; x < 8; ++x) {
        cosines[u][x] = static_cast<float>(cos(((2.0 * x + 1.0) * u * 3.14159265358979323846) / 16.0));
      }
    }
    made = true;
  }
  static const float kScale0 = 0.35355339059327373f;  // 1 / (2 * sqrt(2))
  static const float kScale = 0.5f;                   // 1 / 2

  float rows[64];
  for (uint32_t y = 0; y < 8; ++y) {
    for (uint32_t x = 0; x < 8; ++x) {
      float sum = 0;
      for (uint32_t u = 0; u < 8; ++u) {
        const float value = static_cast<float>(in[y * 8 + u]);
        if (value == 0) continue;
        sum += ((u == 0) ? kScale0 : kScale) * value * cosines[u][x];
      }
      rows[y * 8 + x] = sum;
    }
  }
  for (uint32_t x = 0; x < 8; ++x) {
    for (uint32_t y = 0; y < 8; ++y) {
      float sum = 0;
      for (uint32_t v = 0; v < 8; ++v) {
        const float value = rows[v * 8 + x];
        if (value == 0) continue;
        sum += ((v == 0) ? kScale0 : kScale) * value * cosines[v][y];
      }
      // **The level shift of the format**: a sample is stored centred on zero and the paper is 128
      // (ITU T.81, A.1).
      int32_t sample = static_cast<int32_t>(sum + 128.5f);
      if (sample < 0) sample = 0;
      if (sample > 255) sample = 255;
      out[y * stride + x] = static_cast<uint8_t>(sample);
    }
  }
}

// One block of one component: the DC difference and the AC runs of the format (ITU T.81, A.2.2 and F.1.2.2).
bool one_block(Bits& bits, const Huffman& dc, const Huffman& ac, const uint16_t* quant, int32_t& predictor,
               int32_t* block) {
  for (uint32_t i = 0; i < 64; ++i) block[i] = 0;
  uint16_t size = 0;
  if (!bits.decode(dc, size)) return false;
  if (size > 16) return false;
  int32_t difference = 0;
  if (size > 0) {
    const uint32_t raw = bits.take(size);
    // A difference is stored with its sign in the number itself (ITU T.81, F.1.2.1.1).
    difference = (raw < (1u << (size - 1))) ? static_cast<int32_t>(raw) - static_cast<int32_t>((1u << size) - 1)
                                            : static_cast<int32_t>(raw);
  }
  predictor += difference;
  block[0] = predictor * quant[0];
  uint32_t at = 1;
  while (at < 64) {
    uint16_t run_and_size = 0;
    if (!bits.decode(ac, run_and_size)) return false;
    const uint32_t run = run_and_size >> 4;
    const uint32_t bits_of_size = run_and_size & 0x0F;
    if (bits_of_size == 0) {
      if (run == 0) break;  // the end of the block
      if (run == 15) {
        at += 16;  // sixteen zeros (ZRL)
        continue;
      }
      return false;
    }
    at += run;
    if (at >= 64) return false;
    const uint32_t raw = bits.take(bits_of_size);
    int32_t value = (raw < (1u << (bits_of_size - 1)))
                        ? static_cast<int32_t>(raw) - static_cast<int32_t>((1u << bits_of_size) - 1)
                        : static_cast<int32_t>(raw);
    value *= quant[at];
    // The coefficients of a block are in zigzag order and the block is stored row by row (ITU T.81, A.3.6).
    static const uint8_t kZigZag[64] = {
        0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
        41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
        30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
    block[kZigZag[at]] = value;
    ++at;
  }
  return true;
}

// The header of the file, walked marker by marker until the first scan: the numbers a picture is made of
// (ITU T.81, B.2).
Status read_header(const uint8_t* data, uint32_t len, Header& out) {
  Cursor cursor{data, len, 2};  // past the SOI
  while (true) {
    uint8_t marker = 0;
    if (!cursor.u8(marker)) return kTruncated;
    if (marker != 0xFF) return kBadData;  // a JPEG is a run of markers and each one begins with FF
    uint8_t kind = 0;
    do {
      if (!cursor.u8(kind)) return kTruncated;
    } while (kind == 0xFF);  // the padding a writer may put between markers
    if (kind == 0xD8 || kind == 0x01 || (kind >= 0xD0 && kind <= 0xD7)) continue;  // markers with no length
    if (kind == 0xD9) return kTruncated;                                           // the end before a scan
    uint16_t length = 0;
    if (!cursor.u16be(length) || length < 2) return kTruncated;
    const uint32_t body_at = cursor.at;
    const uint32_t body = length - 2u;
    if (!cursor.has(body)) return kTruncated;
    switch (kind) {
      case 0xC0:  // baseline
      case 0xC1: {  // extended sequential
        Cursor body_cursor{data, len, body_at};
        uint8_t depth = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        uint8_t components = 0;
        if (!body_cursor.u8(depth) || !body_cursor.u16be(height) || !body_cursor.u16be(width) ||
            !body_cursor.u8(components)) {
          return kBadData;
        }
        if (depth != 8) return kUnsupported;  // twelve-bit samples are another decoder
        if (components == 0 || components > kMaxComponents + 1) return kBadData;
        if (components > kMaxComponents) return kUnsupported;  // CMYK or YCCK
        out.width = width;
        out.height = height;
        out.components = components;
        for (uint32_t i = 0; i < components; ++i) {
          uint8_t id = 0;
          uint8_t sampling = 0;
          uint8_t quant = 0;
          if (!body_cursor.u8(id) || !body_cursor.u8(sampling) || !body_cursor.u8(quant)) return kBadData;
          out.comp[i].id = id;
          out.comp[i].h = static_cast<uint8_t>(sampling >> 4);
          out.comp[i].v = static_cast<uint8_t>(sampling & 0x0F);
          if (out.comp[i].h == 0 || out.comp[i].h > 4 || out.comp[i].v == 0 || out.comp[i].v > 4) {
            return kBadData;
          }
          if (quant > 3) return kBadData;
          out.comp[i].quant = quant;
          if (out.comp[i].h > out.max_h) out.max_h = out.comp[i].h;
          if (out.comp[i].v > out.max_v) out.max_v = out.comp[i].v;
        }
        if (out.width == 0 || out.height == 0) return kBadData;
        break;
      }
      case 0xC2:
        return kUnsupported;  // progressive: another decoder (docs/30)
      case 0xC9:
      case 0xCA:
      case 0xCB:
        return kUnsupported;  // arithmetic coding
      case 0xC3:
      case 0xC5:
      case 0xC6:
      case 0xC7:
      case 0xCD:
      case 0xCE:
      case 0xCF:
        return kUnsupported;  // lossless and hierarchical, which no camera writes
      case 0xC4: {
        Cursor body_cursor{data, len, body_at};
        const uint32_t end = body_at + body;
        while (body_cursor.at < end) {
          uint8_t slot = 0;
          if (!body_cursor.u8(slot)) return kBadData;
          const uint8_t table_class = static_cast<uint8_t>(slot >> 4);
          const uint8_t table_id = static_cast<uint8_t>(slot & 0x0F);
          if (table_id > 3 || table_class > 1) return kBadData;
          uint8_t counts[16];
          uint32_t total = 0;
          for (uint32_t i = 0; i < 16; ++i) {
            if (!body_cursor.u8(counts[i])) return kBadData;
            total += counts[i];
          }
          if (body_cursor.at + total > end) return kBadData;
          const uint8_t* values = data + body_cursor.at;
          if (!body_cursor.skip(total)) return kBadData;
          Huffman& table = (table_class == 0) ? out.dc[table_id] : out.ac[table_id];
          if (!build_huffman(table, counts, values, total)) return kBadData;
          ((table_class == 0) ? out.have_dc[table_id] : out.have_ac[table_id]) = true;
        }
        break;
      }
      case 0xDB: {
        Cursor body_cursor{data, len, body_at};
        const uint32_t end = body_at + body;
        while (body_cursor.at < end) {
          uint8_t slot = 0;
          if (!body_cursor.u8(slot)) return kBadData;
          const uint32_t precision = slot >> 4;
          const uint8_t table_id = static_cast<uint8_t>(slot & 0x0F);
          if (table_id > 3 || precision > 1) return kBadData;
          const uint32_t bytes = (precision == 0) ? 64 : 128;
          if (body_cursor.at + bytes > end) return kBadData;
          for (uint32_t i = 0; i < 64; ++i) {
            if (precision == 0) {
              out.quant[table_id][i] = data[body_cursor.at + i];
            } else {
              out.quant[table_id][i] = static_cast<uint16_t>((data[body_cursor.at + i * 2] << 8) |
                                                             data[body_cursor.at + i * 2 + 1]);
            }
          }
          body_cursor.skip(bytes);
          out.have_quant[table_id] = true;
        }
        break;
      }
      case 0xDD: {
        if (body != 2) return kBadData;
        uint16_t interval = 0;
        Cursor body_cursor{data, len, body_at};
        if (!body_cursor.u16be(interval)) return kBadData;
        out.restart_interval = interval;
        break;
      }
      case 0xDA: {  // the first scan: what the reader below decodes
        if (out.components == 0) return kBadData;
        Cursor body_cursor{data, len, body_at};
        uint8_t components = 0;
        if (!body_cursor.u8(components) || components != out.components) {
          return kUnsupported;  // a scan of some of the components: a multi-scan file, another reader
        }
        for (uint32_t i = 0; i < components; ++i) {
          uint8_t id = 0;
          uint8_t tables = 0;
          if (!body_cursor.u8(id) || !body_cursor.u8(tables)) return kBadData;
          Component* found = nullptr;
          for (uint32_t c = 0; c < out.components; ++c) {
            if (out.comp[c].id == id) found = &out.comp[c];
          }
          if (found == nullptr) return kBadData;
          found->dc = static_cast<uint8_t>(tables >> 4);
          found->ac = static_cast<uint8_t>(tables & 0x0F);
        }
        uint8_t spectral_start = 0;
        uint8_t spectral_end = 0;
        uint8_t approximation = 0;
        if (!body_cursor.u8(spectral_start) || !body_cursor.u8(spectral_end) ||
            !body_cursor.u8(approximation)) {
          return kBadData;
        }
        if (spectral_start != 0 || spectral_end != 63 || approximation != 0) return kUnsupported;
        out.entropy_at = body_cursor.at;
        for (uint32_t c = 0; c < out.components; ++c) {
          if (!out.have_dc[out.comp[c].dc] || !out.have_ac[out.comp[c].ac]) return kBadData;
          if (!out.have_quant[out.comp[c].quant]) return kBadData;
        }
        return kOk;
      }
      default:
        break;  // APPn, COM and the rest: walked past, because a picture is not a note
    }
    cursor.at = body_at + body;
  }
}

uint32_t mcus_across(const Header& header) {
  const uint32_t block = header.max_h * 8;
  return (header.width + block - 1) / block;
}

uint32_t bands_down(const Header& header) { return (header.height + header.max_v * 8 - 1) / (header.max_v * 8); }

}  // namespace

Status jpeg_info(const uint8_t* data, uint32_t len, Info& out) {
  Header header;
  const Status status = read_header(data, len, header);
  if (status != kOk) return status;
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  out.format = "JPEG";
  out.width = static_cast<uint16_t>(header.width);
  out.height = static_cast<uint16_t>(header.height);
  out.depth = static_cast<uint8_t>(header.components == 1 ? 8 : 24);
  return kOk;
}

uint32_t jpeg_scratch(const uint8_t* data, uint32_t len) {
  Header header;
  if (read_header(data, len, header) != kOk) return 0;
  // Three planes of a band of MCUs: the widest the components can be and eight rows of each one, which for a
  // photograph of a page is a few tens of kilobytes (docs/30, docs/75).
  const uint64_t plane = static_cast<uint64_t>(mcus_across(header) * header.max_h * 8) * (header.max_v * 8);
  const uint64_t bytes = plane * 3;
  return (bytes > 0xFFFFFFFFu) ? 0 : static_cast<uint32_t>(bytes);
}

Status decode_jpeg(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  Header header;
  const Status status = read_header(data, len, header);
  if (status != kOk) return status;
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  if (header.components != 1 && header.components != 3) return kUnsupported;

  const uint32_t mcus = mcus_across(header);
  const uint32_t plane_width = mcus * header.max_h * 8;
  const uint32_t band_rows = header.max_v * 8;
  const uint32_t plane_bytes = plane_width * band_rows;
  if (scratch == nullptr || cap < plane_bytes * 3) return kNoRoom;
  uint8_t* planes[kMaxComponents] = {scratch, scratch + plane_bytes, scratch + plane_bytes * 2};

  Bits bits;
  bits.data = data;
  bits.len = len;
  bits.at = header.entropy_at;
  int32_t block[64];
  uint32_t mcu_index = 0;
  uint8_t packed[(kMaxSide + 1) / 2];

  for (uint32_t band = 0; band < bands_down(header); ++band) {
    // The eight (or sixteen) rows of every component, block by block.
    for (uint32_t band_y = 0; band_y < band_rows; ++band_y) {
      for (uint32_t x = 0; x < plane_width; ++x) {
        for (uint32_t c = 0; c < header.components; ++c) planes[c][band_y * plane_width + x] = 128;
      }
    }
    for (uint32_t mx = 0; mx < mcus; ++mx) {
      for (uint32_t c = 0; c < header.components; ++c) {
        Component& component = header.comp[c];
        for (uint32_t by = 0; by < component.v; ++by) {
          for (uint32_t bx = 0; bx < component.h; ++bx) {
            if (!one_block(bits, header.dc[component.dc], header.ac[component.ac], header.quant[component.quant],
                           component.predictor, block)) {
              return kTruncated;
            }
            const uint32_t left = (mx * component.h + bx) * 8;
            const uint32_t top = by * 8;
            idct(block, planes[c] + top * plane_width + left, plane_width);
          }
        }
      }
      ++mcu_index;
      if (header.restart_interval != 0 && mcu_index % header.restart_interval == 0) {
        // A restart marker: the bits go back to a byte and the difference of every component starts again
        // (ITU T.81, B.2.3).
        bits.align();
        bits.count = 0;
        bits.bits = 0;
        while (bits.at + 1 < bits.len && !(data[bits.at] == 0xFF && data[bits.at + 1] >= 0xD0 &&
                                           data[bits.at + 1] <= 0xD7)) {
          ++bits.at;
        }
        if (bits.at + 1 < bits.len) bits.at += 2;
        for (uint32_t c = 0; c < header.components; ++c) header.comp[c].predictor = 0;
      }
    }
    // The band turned into rows of the page.
    for (uint32_t row = 0; row < band_rows; ++row) {
      const uint32_t y = band * band_rows + row;
      if (y >= header.height) break;
      Row out;
      row_start(out, packed, static_cast<uint16_t>(header.width), static_cast<uint16_t>(y));
      for (uint32_t x = 0; x < header.width; ++x) {
        if (header.components == 1) {
          const uint8_t grey = planes[0][row * plane_width + x];
          row_put_grey(out, grey);
          continue;
        }
        // Every component is sampled at its own rate: the sample of a pixel is the one it lands on
        // (ITU T.81, A.2.3).
        const uint32_t cx = x * header.comp[1].h / header.max_h;
        const uint32_t cy = row * header.comp[1].v / header.max_v;
        const int32_t luma = planes[0][row * plane_width + x];
        const int32_t cb = static_cast<int32_t>(planes[1][cy * plane_width + cx]) - 128;
        const int32_t cr = static_cast<int32_t>(planes[2][cy * plane_width + cx]) - 128;
        int32_t r = luma + ((91881 * cr) >> 16);
        int32_t g = luma - ((22554 * cb + 46802 * cr) >> 16);
        int32_t b = luma + ((116130 * cb) >> 16);
        r = (r < 0) ? 0 : (r > 255 ? 255 : r);
        g = (g < 0) ? 0 : (g > 255 ? 255 : g);
        b = (b < 0) ? 0 : (b > 255 ? 255 : b);
        row_put_rgb(out, static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
      }
      row_end(out);
      sink.write(sink.ctx, static_cast<uint16_t>(y), packed);
    }
  }
  return kOk;
}

}  // namespace detail
}  // namespace img
