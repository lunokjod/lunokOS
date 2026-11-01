// Deflate, read the other way (docs/70): see `inflate.h` for the shape of the call.
#include "inflate/inflate.h"

#include <stddef.h>
#include <string.h>

namespace inflate {
namespace {

// ---- the bits of the stream ----
//
// Deflate packs from the least significant bit up, and the codes of the Huffman tables are read most
// significant bit first inside those bits: the two directions are why this reader has both `take` and the
// table walk below (RFC 1951, 3.1.1).
struct Bits {
  const uint8_t* in;
  uint32_t len;
  uint32_t at;
  uint32_t bit;
  bool over;  // the stream ended in the middle of something
};

bool take(Bits& bits, uint32_t need, uint32_t& out) {
  if (need == 0) {
    out = 0;
    return true;
  }
  uint32_t value = 0;
  for (uint32_t i = 0; i < need; ++i) {
    if (bits.at >= bits.len) {
      bits.over = true;
      return false;
    }
    const uint32_t one = (bits.in[bits.at] >> bits.bit) & 1u;
    value |= one << i;
    if (++bits.bit == 8) {
      bits.bit = 0;
      ++bits.at;
    }
  }
  out = value;
  return true;
}

void align_to_byte(Bits& bits) {
  if (bits.bit != 0) {
    bits.bit = 0;
    ++bits.at;
  }
}

// ---- the tables ----
//
// A canonical Huffman table, held as the number of codes of each length and the symbols in code order: what
// `build` fills from the lengths the stream carries, and what `decode` walks one bit at a time. A table of
// sixteen rows is what the format promises, so no row can be longer than that and nothing here counts
// further (RFC 1951, 3.2.2).
const uint8_t kMaxBits = 15;
const uint16_t kMaxSymbols = 288;

// **The tables are static and not on the stack**: a table is 608 bytes and a block needs three of them, and
// the stack of this system's task is 8 KB —a buffer of that size on it is how a board crashes with a canary
// warning— (docs/50, docs/75). One stream is read at a time, like every other reader here.
struct Table {
  uint16_t count[kMaxBits + 1];
  uint16_t symbol[kMaxSymbols];
};

Table g_literals;
Table g_distances;
Table g_lengths;
uint8_t g_code_lengths[kMaxSymbols + 30];
uint8_t g_lengths_of_a_fixed[kMaxSymbols];

bool build(Table& table, const uint8_t* lengths, uint16_t size) {
  memset(table.count, 0, sizeof(table.count));
  for (uint16_t i = 0; i < size; ++i) {
    if (lengths[i] > kMaxBits) return false;
    ++table.count[lengths[i]];
  }
  if (table.count[0] == size) return true;  // no codes at all, which is legal for a distance table
  // **A table that is over-subscribed cannot be decoded** and one that is incomplete is a stream that is
  // missing codes; both are broken streams and not something to guess about (RFC 1951, 3.2.7).
  int32_t left = 1;
  for (uint8_t bits = 1; bits <= kMaxBits; ++bits) {
    left = left * 2 - static_cast<int32_t>(table.count[bits]);
    if (left < 0) return false;
  }
  uint16_t offsets[kMaxBits + 1];
  offsets[1] = 0;
  for (uint8_t bits = 1; bits < kMaxBits; ++bits) {
    offsets[bits + 1] = static_cast<uint16_t>(offsets[bits] + table.count[bits]);
  }
  for (uint16_t symbol = 0; symbol < size; ++symbol) {
    if (lengths[symbol] == 0) continue;
    table.symbol[offsets[lengths[symbol]]++] = symbol;
  }
  return true;
}

// -1 is a code the table does not contain, which is a broken stream (docs/70).
int32_t decode(Bits& bits, const Table& table) {
  int32_t code = 0;
  int32_t first = 0;
  int32_t index = 0;
  for (uint8_t len = 1; len <= kMaxBits; ++len) {
    uint32_t bit = 0;
    if (!take(bits, 1, bit)) return -1;
    code |= static_cast<int32_t>(bit);
    const int32_t count = static_cast<int32_t>(table.count[len]);
    if (code - first < count) return table.symbol[index + (code - first)];
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  return -1;
}

// ---- the two alphabets of a compressed block ----
//
// Length 3 to 258 and the distance back, each in a code of its own plus some extra bits: the numbers are the
// ones of the format and the only thing worth saying is that the last length has no extra bits (RFC 1951,
// 3.2.5).
const uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19,  23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistanceBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,    25,
                                    33,   49,   65,   97,   129,  193,   257,   385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

Status codes(Bits& bits, const Table& literals, const Table& distances, uint8_t* out, uint32_t cap,
             uint32_t& written) {
  for (;;) {
    const int32_t symbol = decode(bits, literals);
    if (symbol < 0) return kBadData;
    if (symbol < 256) {
      if (written >= cap) return kNoRoom;
      out[written++] = static_cast<uint8_t>(symbol);
      continue;
    }
    if (symbol == 256) return kOk;  // the end of the block
    const int32_t length_code = symbol - 257;
    if (length_code >= 29) return kBadData;
    uint32_t extra = 0;
    if (!take(bits, kLengthExtra[length_code], extra)) return kTruncated;
    const uint32_t length = kLengthBase[length_code] + extra;

    const int32_t distance_symbol = decode(bits, distances);
    if (distance_symbol < 0 || distance_symbol >= 30) return kBadData;
    uint32_t far = 0;
    if (!take(bits, kDistanceExtra[distance_symbol], far)) return kTruncated;
    const uint32_t distance = kDistanceBase[distance_symbol] + far;
    // **A distance may not reach past the start of the output**: the window of a deflate stream is the data
    // already written, and a stream that asks for more is a stream that was never written by a compressor
    // (docs/70).
    if (distance > written) return kBadData;
    if (written + length > cap) return kNoRoom;
    // The copy is byte by byte on purpose: an overlapping copy is legal here and is how a run of the same
    // byte is written at all (RFC 1951, 3.2.3).
    for (uint32_t i = 0; i < length; ++i) {
      out[written] = out[written - distance];
      ++written;
    }
  }
}

Status stored(Bits& bits, uint8_t* out, uint32_t cap, uint32_t& written) {
  align_to_byte(bits);
  uint32_t length = 0;
  uint32_t not_length = 0;
  if (!take(bits, 16, length) || !take(bits, 16, not_length)) return kTruncated;
  if ((length ^ 0xFFFFu) != not_length) return kBadData;
  if (written + length > cap) return kNoRoom;
  if (bits.at + length > bits.len) return kTruncated;
  memcpy(out + written, bits.in + bits.at, length);
  bits.at += length;
  written += length;
  return kOk;
}

Status fixed_block(Bits& bits, uint8_t* out, uint32_t cap, uint32_t& written) {
  // The tables of a fixed block are the ones in the format: 8, 9, 7 and 8 bits for the literals and five
  // bits for every distance (RFC 1951, 3.2.6).
  uint8_t* lengths = g_lengths_of_a_fixed;
  for (uint16_t i = 0; i < 144; ++i) lengths[i] = 8;
  for (uint16_t i = 144; i < 256; ++i) lengths[i] = 9;
  for (uint16_t i = 256; i < 280; ++i) lengths[i] = 7;
  for (uint16_t i = 280; i < 288; ++i) lengths[i] = 8;
  if (!build(g_literals, lengths, 288)) return kBadData;
  uint8_t distance_lengths[30];
  for (uint16_t i = 0; i < 30; ++i) distance_lengths[i] = 5;
  if (!build(g_distances, distance_lengths, 30)) return kBadData;
  return codes(bits, g_literals, g_distances, out, cap, written);
}

Status dynamic_block(Bits& bits, uint8_t* out, uint32_t cap, uint32_t& written) {
  uint32_t count_literals = 0;
  uint32_t count_distances = 0;
  uint32_t count_lengths = 0;
  if (!take(bits, 5, count_literals) || !take(bits, 5, count_distances) || !take(bits, 4, count_lengths)) {
    return kTruncated;
  }
  const uint16_t literals = static_cast<uint16_t>(257 + count_literals);
  const uint16_t distances = static_cast<uint16_t>(1 + count_distances);
  const uint16_t lengths_given = static_cast<uint16_t>(4 + count_lengths);
  if (literals > 286 || distances > 30) return kBadData;

  // The order the code lengths themselves are given in: it is the format's, and it puts the lengths that
  // are almost always zero at the end so that the stream can stop early (RFC 1951, 3.2.7).
  const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  uint8_t code_lengths[19];
  memset(code_lengths, 0, sizeof(code_lengths));
  for (uint16_t i = 0; i < lengths_given; ++i) {
    uint32_t value = 0;
    if (!take(bits, 3, value)) return kTruncated;
    code_lengths[order[i]] = static_cast<uint8_t>(value);
  }
  if (!build(g_lengths, code_lengths, 19)) return kBadData;

  uint8_t* all = g_code_lengths;
  memset(all, 0, kMaxSymbols + 30);
  const uint16_t total = static_cast<uint16_t>(literals + distances);
  uint16_t at = 0;
  while (at < total) {
    const int32_t symbol = decode(bits, g_lengths);
    if (symbol < 0) return kBadData;
    if (symbol < 16) {
      all[at++] = static_cast<uint8_t>(symbol);
      continue;
    }
    uint8_t value = 0;
    uint32_t repeat = 0;
    if (symbol == 16) {
      if (at == 0) return kBadData;  // nothing to repeat
      value = all[at - 1];
      if (!take(bits, 2, repeat)) return kTruncated;
      repeat += 3;
    } else if (symbol == 17) {
      if (!take(bits, 3, repeat)) return kTruncated;
      repeat += 3;
    } else if (symbol == 18) {
      if (!take(bits, 7, repeat)) return kTruncated;
      repeat += 11;
    } else {
      return kBadData;
    }
    if (at + repeat > total) return kBadData;
    for (uint32_t i = 0; i < repeat; ++i) all[at++] = value;
  }
  if (!build(g_literals, all, literals)) return kBadData;
  if (!build(g_distances, all + literals, distances)) return kBadData;
  // A dynamic block with no end-of-block code is a block that never says it is over.
  if (all[256] == 0) return kBadData;
  return codes(bits, g_literals, g_distances, out, cap, written);
}

}  // namespace

Status raw(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t cap, uint32_t& written) {
  written = 0;
  if (in == nullptr || out == nullptr) return kBadData;
  Bits bits = {in, in_len, 0, 0, false};
  for (;;) {
    uint32_t last = 0;
    uint32_t kind = 0;
    if (!take(bits, 1, last) || !take(bits, 2, kind)) return kTruncated;
    Status status = kOk;
    if (kind == 0) {
      status = stored(bits, out, cap, written);
    } else if (kind == 1) {
      status = fixed_block(bits, out, cap, written);
    } else if (kind == 2) {
      status = dynamic_block(bits, out, cap, written);
    } else {
      return kBadData;  // the fourth kind is not a kind
    }
    if (status != kOk) return status;
    if (last != 0) return kOk;
  }
}

Status zlib(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t cap, uint32_t& written) {
  written = 0;
  // Two header bytes: the method has to be deflate, and the rest is a window size this reader does not need
  // because the window is the output itself (RFC 1950, 2.2).
  if (in == nullptr || in_len < 6) return kTruncated;
  if ((in[0] & 0x0F) != 8) return kBadData;
  if (((static_cast<uint32_t>(in[0]) << 8) | in[1]) % 31 != 0) return kBadData;
  if ((in[1] & 0x20) != 0) return kBadData;  // a preset dictionary is not something a woff uses
  const Status status = raw(in + 2, in_len - 2, out, cap, written);
  if (status != kOk) return status;
  // The four bytes are the checksum of what came out, big-endian, and a mismatch is not a warning: the
  // bytes are wrong and there is nothing to hand over (docs/70).
  const uint32_t expected = (static_cast<uint32_t>(in[in_len - 4]) << 24) |
                            (static_cast<uint32_t>(in[in_len - 3]) << 16) |
                            (static_cast<uint32_t>(in[in_len - 2]) << 8) |
                            static_cast<uint32_t>(in[in_len - 1]);
  if (adler32(out, written) != expected) return kAdler;
  return kOk;
}

uint32_t adler32(const uint8_t* data, uint32_t len, uint32_t seed) {
  const uint32_t kMod = 65521;
  uint32_t a = seed & 0xFFFFu;
  uint32_t b = (seed >> 16) & 0xFFFFu;
  for (uint32_t i = 0; i < len; ++i) {
    a = (a + data[i]) % kMod;
    b = (b + a) % kMod;
  }
  return ((b & 0xFFFFu) << 16) | (a & 0xFFFFu);
}

uint32_t crc32(const uint8_t* data, uint32_t len, uint32_t seed) {
  // **Sixteen rows and not two hundred and fifty-six**: the nibble table is the whole of the polynomial in
  // a quarter of the flash, and a zip name is short (docs/70).
  static const uint32_t kNibble[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
                                       0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                       0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                       0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
  uint32_t crc = ~seed;
  for (uint32_t i = 0; i < len; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ kNibble[crc & 0x0F];
    crc = (crc >> 4) ^ kNibble[crc & 0x0F];
  }
  return ~crc;
}

}  // namespace inflate
