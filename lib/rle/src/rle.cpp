// The run-length of this system: the reasons of the format are in `rle.h` (docs/30, docs/75).
#include "rle/rle.h"

namespace rle {

namespace {

const uint8_t kMagic0 = 'R';
const uint8_t kMagic1 = 'L';
const uint8_t kMagic2 = 'E';

// **The longest run a control byte can say**, which is what makes a repeat of a hundred and twenty-nine the ceiling:
// `257 - 128 = 129`, and one byte less than that is a hundred and twenty-eight.
const uint32_t kMaxRun = 129;
// **And the longest literal**, which is the other half of the same byte: `control + 1` with a control of a hundred and
// twenty-seven.
const uint32_t kMaxLiteral = 128;

void put_length(uint8_t* out, uint32_t len) {
  out[3] = static_cast<uint8_t>(len & 0xFFu);
  out[4] = static_cast<uint8_t>((len >> 8) & 0xFFu);
  out[5] = static_cast<uint8_t>((len >> 16) & 0xFFu);
  out[6] = static_cast<uint8_t>((len >> 24) & 0xFFu);
}

}  // namespace

uint32_t packed_capacity(uint32_t len) {
  if (len == 0) return 0;
  return kHeader + len + (len + kMaxLiteral - 1) / kMaxLiteral;
}

uint32_t pack(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap) {
  if (in == nullptr || out == nullptr || len == 0) return 0;
  if (cap < packed_capacity(len)) return 0;

  out[0] = kMagic0;
  out[1] = kMagic1;
  out[2] = kMagic2;
  put_length(out, len);

  uint32_t o = kHeader;
  uint32_t i = 0;

  while (i < len) {
    // **How many equal bytes there are here**, up to the ceiling of a run.
    uint32_t run = 1;
    while ((i + run < len) && (in[i + run] == in[i]) && (run < kMaxRun)) ++run;

    if (run >= 3) {
      // A repeat: the control and the byte.
      out[o] = static_cast<uint8_t>(257u - run);
      out[o + 1] = in[i];
      o += 2;
      i += run;
      continue;
    }

    // **A literal**: everything until a run of three starts or the ceiling of a literal is reached. Two equal bytes are
    // part of the literal and not a repeat, because a repeat of two costs the same two bytes and says less.
    uint32_t n = 1;
    while ((i + n < len) && (n < kMaxLiteral)) {
      uint32_t ahead = 1;
      while ((i + n + ahead < len) && (in[i + n + ahead] == in[i + n]) && (ahead < 3)) ++ahead;
      if (ahead >= 3) break;
      ++n;
    }

    out[o] = static_cast<uint8_t>(n - 1);
    ++o;
    for (uint32_t k = 0; k < n; ++k) out[o + k] = in[i + k];
    o += n;
    i += n;
  }

  return o;
}

uint32_t unpacked_size(const uint8_t* in, uint32_t len) {
  if (in == nullptr || len < kHeader) return 0;
  if (in[0] != kMagic0 || in[1] != kMagic1 || in[2] != kMagic2) return 0;

  const uint32_t want = static_cast<uint32_t>(in[3]) | (static_cast<uint32_t>(in[4]) << 8) |
                        (static_cast<uint32_t>(in[5]) << 16) | (static_cast<uint32_t>(in[6]) << 24);

  return want;
}

uint32_t unpack(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap) {
  const uint32_t want = unpacked_size(in, len);
  if (want == 0 || out == nullptr || cap < want) return 0;

  uint32_t i = kHeader;
  uint32_t o = 0;

  while ((i < len) && (o < want)) {
    const uint8_t control = in[i];
    ++i;

    if (control < 128u) {
      const uint32_t n = static_cast<uint32_t>(control) + 1u;
      if ((i + n > len) || (o + n > want)) return 0;
      for (uint32_t k = 0; k < n; ++k) out[o + k] = in[i + k];
      o += n;
      i += n;
    } else {
      if (i + 1 > len) return 0;
      const uint32_t n = 257u - static_cast<uint32_t>(control);
      if (o + n > want) return 0;
      for (uint32_t k = 0; k < n; ++k) out[o + k] = in[i];
      o += n;
      ++i;
    }
  }

  // **A stream that unpacks to something other than its header said is a stream that failed** and not half a picture.
  return (o == want) ? o : 0;
}

}  // namespace rle
