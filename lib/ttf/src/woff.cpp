// **A face that travels compressed** (0.150.0, docs/46 D6): a `.woff` is an sfnt in a box —a header of 44 bytes, a
// directory of twenty bytes per table, and the tables themselves zlib-compressed— and this file is the box opened.
//
// What comes out is an sfnt of the shape the TrueType reader already knows: the offset table, one record of sixteen
// bytes per table, the tables padded to four bytes. So a face that a book carries inside itself goes through
// `ttf::open` exactly like a face of the disk, and nothing above this file has to know where the bytes came from
// (docs/46, D6).
//
// **WOFF2 is not this**: it is Brotli and a different table model and this system has no Brotli, so a book that
// carries one is told so instead of being read wrongly.
#include <string.h>

#include "inflate/inflate.h"
#include "ttf/ttf.h"

namespace ttf {
namespace {

const uint32_t kWoffHeader = 44;   // the header of a woff 1.0
const uint32_t kWoffEntry = 20;    // tag, offset, compLength, origLength, origChecksum
const uint16_t kMaxWoffTables = 64;  // an sfnt of a page has ten; a hundred is a font of a machine and not of a book

uint32_t read32(const uint8_t* at) {
  return (static_cast<uint32_t>(at[0]) << 24) | (static_cast<uint32_t>(at[1]) << 16) |
         (static_cast<uint32_t>(at[2]) << 8) | static_cast<uint32_t>(at[3]);
}

uint16_t read16(const uint8_t* at) { return static_cast<uint16_t>((at[0] << 8) | at[1]); }

void write32(uint8_t* at, uint32_t value) {
  at[0] = static_cast<uint8_t>(value >> 24);
  at[1] = static_cast<uint8_t>(value >> 16);
  at[2] = static_cast<uint8_t>(value >> 8);
  at[3] = static_cast<uint8_t>(value);
}

void write16(uint8_t* at, uint16_t value) {
  at[0] = static_cast<uint8_t>(value >> 8);
  at[1] = static_cast<uint8_t>(value);
}

// The three numbers of an sfnt's offset table, which the reader uses to find a table by bisection: the size of the
// biggest power of two that fits the tables, its exponent, and what is left over.
void search_fields(uint16_t tables, uint16_t& range, uint16_t& selector, uint16_t& shift) {
  uint16_t power = 1;
  selector = 0;
  while (static_cast<uint16_t>(power * 2) <= tables) {
    power = static_cast<uint16_t>(power * 2);
    ++selector;
  }
  range = static_cast<uint16_t>(power * 16);
  shift = static_cast<uint16_t>(tables * 16 - range);
}

}  // namespace

bool is_woff(const uint8_t* data, uint32_t length) {
  return data != nullptr && length >= kWoffHeader && data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == 'F';
}

bool is_woff2(const uint8_t* data, uint32_t length) {
  return data != nullptr && length >= 4 && data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == '2';
}

// **How big the sfnt will be**, which is what the caller has to give: 0 when the bytes are not a woff, and the number
// the header itself promises —a woff that lies is found out table by table below.
uint32_t woff_sfnt_size(const uint8_t* data, uint32_t length) {
  if (!is_woff(data, length)) return 0;

  return read32(data + 16);  // totalSfntSize
}

// **The box opened**: the tables out and the sfnt put together. False when the bytes are not a woff, when a table does
// not come out at the size its record promises —which is a damaged font and not a font with a surprise— or when the
// sfnt does not fit the block of the caller.
bool woff_to_sfnt(const uint8_t* data, uint32_t length, uint8_t* out, uint32_t cap, uint32_t& written) {
  written = 0;
  if (!is_woff(data, length) || out == nullptr) return false;

  const uint32_t flavor = read32(data + 4);
  const uint16_t tables = read16(data + 12);
  if (tables == 0 || tables > kMaxWoffTables) return false;

  const uint32_t directory = kWoffHeader + static_cast<uint32_t>(tables) * kWoffEntry;
  const uint32_t header = 12u + static_cast<uint32_t>(tables) * 16u;
  if (directory > length || header > cap) return false;

  uint16_t range = 0;
  uint16_t selector = 0;
  uint16_t shift = 0;
  search_fields(tables, range, selector, shift);

  write32(out, flavor);           // an sfnt of one face is 0x00010000, and a woff keeps the flavor it had
  write16(out + 4, tables);
  write16(out + 6, range);
  write16(out + 8, selector);
  write16(out + 10, shift);

  uint32_t at = header;  // where the next table goes in the sfnt
  for (uint16_t i = 0; i < tables; ++i) {
    const uint8_t* entry = data + kWoffHeader + static_cast<uint32_t>(i) * kWoffEntry;
    const uint32_t offset = read32(entry + 4);
    const uint32_t packed = read32(entry + 8);
    const uint32_t plain = read32(entry + 12);
    const uint32_t sum = read32(entry + 16);
    if (plain == 0 || offset + packed > length) return false;
    if (at + ((plain + 3u) & ~3u) > cap) return false;

    uint8_t* record = out + 12 + static_cast<uint32_t>(i) * 16;
    memcpy(record, entry, 4);  // the tag
    write32(record + 4, sum);
    write32(record + 8, at);
    write32(record + 12, plain);

    if (packed == plain) {
      // **A table that pays for compressing is stored**: the woff says so by giving the same length twice (docs/46, D6).
      memcpy(out + at, data + offset, plain);
    } else {
      uint32_t got = 0;
      if (inflate::zlib(data + offset, packed, out + at, plain, got) != inflate::kOk || got != plain) return false;
    }
    // The padding of an sfnt is zeros and not whatever was in the block before.
    const uint32_t pad = ((plain + 3u) & ~3u) - plain;
    if (pad != 0) memset(out + at + plain, 0, pad);
    at += (plain + 3u) & ~3u;
  }

  written = at;
  return true;
}

}  // namespace ttf
