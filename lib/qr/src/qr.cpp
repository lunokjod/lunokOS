// QR encoding: every version of the standard (1 to 40 in the tables), every error
// correction level, and the three modes that carry text (ISO/IEC 18004).
//
// The mask is not chosen by taste: all eight are drawn and the one with the lowest
// penalty wins, which is what the standard requires. Drawing eight times costs a
// few passes over the symbol and saves the memory of eight matrices.
#include "qr/qr.h"

#include <stddef.h>
#include <string.h>

namespace qr {
namespace {

// Total codewords of each version, as the standard publishes them. The geometry
// has to agree with this table and a test checks it (docs/50).
const uint16_t kTotals[41] = {
    0,    26,   44,   70,   100,  134,  172,  196,  242,  292,  346,  404,  466,  532,
    581,  655,  733,  815,  901,  991,  1085, 1156, 1258, 1364, 1474, 1588, 1706, 1828,
    1921, 2051, 2185, 2323, 2465, 2611, 2761, 2876, 3034, 3196, 3362, 3532, 3706,
};

// Error correction codewords per block, and the number of blocks, for each
// version and level; the levels go in the order L, M, Q, H.
const uint8_t kEc[41][4] = {
    {0, 0, 0, 0},     {7, 10, 13, 17},  {10, 16, 22, 28}, {15, 26, 18, 22}, {20, 18, 26, 16},
    {26, 24, 18, 22}, {18, 16, 24, 28}, {20, 18, 18, 26}, {24, 22, 22, 26}, {30, 22, 20, 24},
    {18, 26, 24, 28}, {20, 30, 28, 24}, {24, 22, 26, 28}, {26, 22, 24, 22}, {30, 24, 20, 24},
    {22, 24, 30, 24}, {24, 28, 24, 30}, {28, 28, 28, 28}, {30, 26, 28, 28}, {28, 26, 26, 26},
    {28, 26, 30, 28}, {28, 26, 28, 30}, {28, 28, 30, 24}, {30, 28, 30, 30}, {30, 28, 30, 30},
    {26, 28, 30, 30}, {28, 28, 28, 30}, {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30},
    {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30},
    {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30}, {30, 28, 30, 30},
};

const uint8_t kBlocks[41][4] = {
    {0, 0, 0, 0},     {1, 1, 1, 1},     {1, 1, 1, 1},     {1, 1, 2, 2},     {1, 2, 2, 4},
    {1, 2, 4, 4},     {2, 4, 4, 4},     {2, 4, 6, 5},     {2, 4, 6, 6},     {2, 5, 8, 8},
    {4, 5, 8, 8},     {4, 5, 8, 11},    {4, 8, 10, 11},   {4, 9, 12, 16},   {4, 9, 16, 16},
    {6, 10, 12, 18},  {6, 10, 17, 16},  {6, 11, 16, 19},  {6, 13, 18, 21},  {7, 14, 21, 25},
    {8, 16, 20, 25},  {8, 17, 23, 25},  {9, 17, 23, 34},  {9, 18, 25, 30},  {10, 20, 27, 32},
    {12, 21, 29, 35}, {12, 23, 34, 37}, {12, 25, 34, 40}, {13, 26, 35, 42}, {14, 28, 38, 45},
    {15, 29, 40, 48}, {16, 31, 43, 51}, {17, 33, 45, 54}, {18, 35, 48, 57}, {19, 37, 51, 60},
    {19, 38, 53, 63}, {20, 40, 56, 66}, {21, 43, 59, 70}, {22, 45, 62, 74}, {24, 47, 65, 77},
    {25, 49, 68, 81},
};

// Where the alignment patterns go. Version 1 has none and 0 means "no more".
const uint8_t kAlign[41][7] = {
    {0},                 {0},                 {6, 18},             {6, 22},
    {6, 26},             {6, 30},             {6, 34},             {6, 22, 38},
    {6, 24, 42},         {6, 26, 46},         {6, 28, 50},         {6, 30, 54},
    {6, 32, 58},         {6, 34, 62},         {6, 26, 46, 66},     {6, 26, 48, 70},
    {6, 26, 50, 74},     {6, 30, 54, 78},     {6, 30, 56, 82},     {6, 30, 58, 86},
    {6, 34, 62, 90},     {6, 28, 50, 72, 94}, {6, 26, 50, 74, 98}, {6, 30, 54, 78, 102},
    {6, 28, 54, 80, 106}, {6, 32, 58, 84, 110}, {6, 30, 58, 86, 114}, {6, 34, 62, 90, 118},
    {6, 26, 50, 74, 98, 122}, {6, 30, 54, 78, 102, 126}, {6, 26, 52, 78, 104, 130},
    {6, 30, 56, 82, 108, 134}, {6, 34, 60, 86, 112, 138}, {6, 30, 58, 86, 114, 142},
    {6, 34, 62, 90, 118, 146}, {6, 30, 54, 78, 102, 126, 150}, {6, 24, 50, 76, 102, 128, 154},
    {6, 28, 54, 80, 106, 132, 158}, {6, 32, 58, 84, 110, 136, 162}, {6, 26, 54, 82, 110, 138, 166},
    {6, 30, 58, 86, 114, 142, 170},
};

// The most blocks and the most error correction codewords per block that the
// table asks for between version 1 and version 20, which is what the working
// buffers are sized for. A test walks the table and checks that it never goes
// past them: if the version limit is raised, that test says so first (docs/75).
const uint32_t kMaxBlocks = 25;  // version 19 and 20 at level H
const uint32_t kMaxEc = 30;      // version 11 and up at level L

// The largest symbol this build draws. The tables go further, but every module is
// a byte in the caller's buffer and the working set is static: version 20 is 97
// modules a side and about 700 bytes of text at level M, which is more than this
// system prints or an app can hold in its 64 KB (docs/75).
const uint32_t kMaxVersion = 20;
const uint32_t kMaxSide = 17 + 4 * kMaxVersion;
const uint32_t kMaxModules = kMaxSide * kMaxSide;

// Function modules (finders, timing, alignment, format and version blocks) are
// not data and are never masked. A bitmap of the whole symbol holds that.
uint8_t g_fn[(kMaxModules + 7) / 8];
uint8_t g_data[1200];  // the data codewords of the biggest version allowed
uint8_t g_cw[1200];    // data and error correction, interleaved

uint8_t g_exp[512];
uint8_t g_log[256];

void init_gf() {
  static bool done = false;
  if (done) return;
  done = true;
  uint16_t x = 1;
  for (uint32_t i = 0; i < 255; ++i) {
    g_exp[i] = static_cast<uint8_t>(x);
    g_log[x] = static_cast<uint8_t>(i);
    x <<= 1;
    if ((x & 0x100) != 0) x ^= 0x11d;
  }
  for (uint32_t i = 255; i < 512; ++i) g_exp[i] = g_exp[i - 255];
}

uint8_t mul(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return 0;
  return g_exp[g_log[a] + g_log[b]];
}

void rs_divisor(uint32_t ec_len, uint8_t* out) {
  memset(out, 0, ec_len);
  out[ec_len - 1] = 1;
  uint8_t root = 1;
  for (uint32_t i = 0; i < ec_len; ++i) {
    for (uint32_t j = 0; j < ec_len; ++j) {
      out[j] = mul(out[j], root);
      if (j + 1 < ec_len) out[j] ^= out[j + 1];
    }
    root = mul(root, 0x02);
  }
}

void rs_ecc(const uint8_t* data, uint32_t len, uint8_t* out, uint32_t ec_len) {
  uint8_t divisor[64];
  rs_divisor(ec_len, divisor);
  memset(out, 0, ec_len);
  for (uint32_t i = 0; i < len; ++i) {
    const uint8_t factor = static_cast<uint8_t>(data[i] ^ out[0]);
    memmove(out, out + 1, ec_len - 1);
    out[ec_len - 1] = 0;
    for (uint32_t j = 0; j < ec_len; ++j) out[j] ^= mul(divisor[j], factor);
  }
}

// How many codewords of data the version and level leave for the text.
uint32_t data_codewords(uint32_t version, Level level) {
  const uint32_t l = static_cast<uint32_t>(level);
  const uint32_t ec = static_cast<uint32_t>(kEc[version][l]) * kBlocks[version][l];
  return kTotals[version] - ec;
}

uint32_t count_bits(uint32_t version, Mode mode) {
  const bool small = version <= 9;
  const bool medium = version <= 26;
  switch (mode) {
    case Mode::kNumeric: return small ? 10 : (medium ? 12 : 14);
    case Mode::kAlnum: return small ? 9 : (medium ? 11 : 13);
    case Mode::kByte: return small ? 8 : 16;
    default: return 8;
  }
}

uint32_t mode_indicator(Mode mode) {
  switch (mode) {
    case Mode::kNumeric: return 0x1;
    case Mode::kAlnum: return 0x2;
    case Mode::kByte: return 0x4;
    default: return 0x4;
  }
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int32_t alnum_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
  if (c == ' ') return 36;
  if (c == '$') return 37;
  if (c == '%') return 38;
  if (c == '*') return 39;
  if (c == '+') return 40;
  if (c == '-') return 41;
  if (c == '.') return 42;
  if (c == '/') return 43;
  if (c == ':') return 44;
  return -1;
}

// The densest mode that can carry the text, which is what "auto" means.
Mode detect_mode(const char* text, uint32_t len) {
  bool digits = true;
  bool alnum = true;
  for (uint32_t i = 0; i < len; ++i) {
    if (!is_digit(text[i])) digits = false;
    if (alnum_value(text[i]) < 0) alnum = false;
  }
  if (digits) return Mode::kNumeric;
  if (alnum) return Mode::kAlnum;
  return Mode::kByte;
}

bool mode_carries(Mode mode, const char* text, uint32_t len) {
  for (uint32_t i = 0; i < len; ++i) {
    if (mode == Mode::kNumeric && !is_digit(text[i])) return false;
    if (mode == Mode::kAlnum && alnum_value(text[i]) < 0) return false;
  }
  return true;
}

// Bits the text takes in a mode, without the header. Only the length is asked for —the mode decides how many bits
// a character costs— and the text itself is not read: it is passed so that the call says what it is measuring.
uint32_t data_bits(Mode mode, const char* /*text*/, uint32_t len) {
  if (mode == Mode::kNumeric) {
    const uint32_t groups = len / 3;
    const uint32_t rest = len % 3;
    return groups * 10 + ((rest == 2) ? 7 : (rest == 1 ? 4 : 0));
  }
  if (mode == Mode::kAlnum) {
    return (len / 2) * 11 + ((len % 2) != 0 ? 6 : 0);
  }
  return len * 8;
}

// A writer over a byte buffer, most significant bit first.
struct Bits {
  uint8_t* out;
  uint32_t cap;
  uint32_t at;

  void put(uint32_t value, uint32_t count) {
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
      if (at / 8 >= cap) return;
      const uint32_t bit = (value >> i) & 1;
      out[at / 8] = static_cast<uint8_t>(out[at / 8] | (bit << (7 - (at % 8))));
      ++at;
    }
  }
};

void write_payload(Mode mode, const char* text, uint32_t len, Bits& w) {
  if (mode == Mode::kNumeric) {
    for (uint32_t i = 0; i < len; i += 3) {
      const uint32_t take = (len - i >= 3) ? 3 : (len - i);
      uint32_t value = 0;
      for (uint32_t k = 0; k < take; ++k) value = value * 10 + static_cast<uint32_t>(text[i + k] - '0');
      w.put(value, (take == 3) ? 10 : (take == 2 ? 7 : 4));
    }
    return;
  }
  if (mode == Mode::kAlnum) {
    for (uint32_t i = 0; i < len; i += 2) {
      if (i + 1 < len) {
        const uint32_t value =
            static_cast<uint32_t>(alnum_value(text[i])) * 45 + static_cast<uint32_t>(alnum_value(text[i + 1]));
        w.put(value, 11);
      } else {
        w.put(static_cast<uint32_t>(alnum_value(text[i])), 6);
      }
    }
    return;
  }
  for (uint32_t i = 0; i < len; ++i) w.put(static_cast<uint8_t>(text[i]), 8);
}

// --- the symbol -------------------------------------------------------------

uint32_t idx(uint32_t side, uint32_t row, uint32_t col) { return row * side + col; }

void clear_fn(uint32_t side) {
  memset(g_fn, 0, (side * side + 7) / 8);
}

void mark(uint32_t side, uint32_t row, uint32_t col) {
  if (row >= side || col >= side) return;
  g_fn[idx(side, row, col) / 8] = static_cast<uint8_t>(g_fn[idx(side, row, col) / 8] |
                                                       (1u << (idx(side, row, col) % 8)));
}

bool is_fn(uint32_t side, uint32_t row, uint32_t col) {
  if (row >= side || col >= side) return true;
  return (g_fn[idx(side, row, col) / 8] & (1u << (idx(side, row, col) % 8))) != 0;
}

uint32_t format_bits(uint32_t level, uint32_t mask) {
  // The level code of the standard: L 01, M 00, Q 11, H 10.
  static const uint8_t kLevelCode[4] = {1, 0, 3, 2};
  const uint32_t data = (static_cast<uint32_t>(kLevelCode[level]) << 3) | mask;
  uint32_t rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
  return ((data << 10) | rem) ^ 0x5412;
}

uint32_t version_bits(uint32_t version) {
  uint32_t rem = version;
  for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ (((rem >> 11) & 1) * 0x1f25);
  return (version << 12) | rem;
}

bool masked(uint32_t mask, uint32_t row, uint32_t col) {
  switch (mask) {
    case 0: return ((row + col) % 2) == 0;
    case 1: return (row % 2) == 0;
    case 2: return (col % 3) == 0;
    case 3: return ((row + col) % 3) == 0;
    case 4: return (((row / 2) + (col / 3)) % 2) == 0;
    case 5: return (((row * col) % 2) + ((row * col) % 3)) == 0;
    case 6: return ((((row * col) % 2) + ((row * col) % 3)) % 2) == 0;
    case 7: return ((((row + col) % 2) + ((row * col) % 3)) % 2) == 0;
    default: return false;
  }
}

// Marks every function module of a version, and nothing else. The drawing below
// walks the same positions and puts colour in them.
void mark_patterns(uint32_t version) {
  const uint32_t side = 17 + 4 * version;
  clear_fn(side);

  // Finders with their separators: an 8x8 corner, three times.
  const uint32_t corners[3][2] = {{0, 0}, {0, side - 8}, {side - 8, 0}};
  for (uint32_t c = 0; c < 3; ++c) {
    for (uint32_t r = 0; r < 8; ++r) {
      for (uint32_t k = 0; k < 8; ++k) mark(side, corners[c][0] + r, corners[c][1] + k);
    }
  }
  // Timing lines, between the finders.
  for (uint32_t i = 8; i + 8 < side; ++i) {
    mark(side, 6, i);
    mark(side, i, 6);
  }
  // Format information and the always-dark module.
  for (uint32_t i = 0; i < 9; ++i) {
    mark(side, 8, i);
    mark(side, i, 8);
    if (i < 8) {
      mark(side, 8, side - 1 - i);
      mark(side, side - 1 - i, 8);
    }
  }
  mark(side, side - 8, 8);
  // Version information, from version 7 on.
  if (version >= 7) {
    for (uint32_t i = 0; i < 6; ++i) {
      for (uint32_t k = 0; k < 3; ++k) {
        mark(side, i, side - 11 + k);
        mark(side, side - 11 + k, i);
      }
    }
  }
  // Alignment patterns. The three that would sit on a finder are not drawn at
  // all, so their modules are data like any other: the same rule has to hold here
  // and in the drawing, or the count of data modules stops matching the standard.
  for (uint32_t i = 0; i < 7 && kAlign[version][i] != 0; ++i) {
    for (uint32_t j = 0; j < 7 && kAlign[version][j] != 0; ++j) {
      const uint32_t row = kAlign[version][i];
      const uint32_t col = kAlign[version][j];
      const bool corner = (row < 9 && col < 9) || (row < 9 && col + 9 > side) ||
                          (row + 9 > side && col < 9);
      if (corner) continue;
      for (int32_t r = -2; r <= 2; ++r) {
        for (int32_t k = -2; k <= 2; ++k) {
          mark(side, static_cast<uint32_t>(static_cast<int32_t>(row) + r),
               static_cast<uint32_t>(static_cast<int32_t>(col) + k));
        }
      }
    }
  }
}

uint32_t count_function_modules(uint32_t version) {
  mark_patterns(version);
  const uint32_t side = 17 + 4 * version;
  uint32_t count = 0;
  for (uint32_t i = 0; i < side * side; ++i) {
    if ((g_fn[i / 8] & (1u << (i % 8))) != 0) ++count;
  }
  return count;
}

// The value of every function module, plus the data, masked with `mask`.
void draw(uint32_t version, Level level, const uint8_t* cw, uint32_t cw_len, uint32_t mask,
          uint8_t* out) {
  const uint32_t side = 17 + 4 * version;
  memset(out, 0, side * side);
  const uint32_t level_index = static_cast<uint32_t>(level);

  // Finders: a ring of 7, a light ring inside and a 3x3 core.
  const uint32_t corners[3][2] = {{0, 0}, {0, side - 7}, {side - 7, 0}};
  for (uint32_t c = 0; c < 3; ++c) {
    for (uint32_t r = 0; r < 7; ++r) {
      for (uint32_t k = 0; k < 7; ++k) {
        const bool ring = (r == 0 || r == 6 || k == 0 || k == 6);
        const bool core = (r >= 2 && r <= 4 && k >= 2 && k <= 4);
        out[idx(side, corners[c][0] + r, corners[c][1] + k)] = (ring || core) ? 1 : 0;
      }
    }
  }
  // Timing lines.
  for (uint32_t i = 8; i + 8 < side; ++i) {
    out[idx(side, 6, i)] = (i % 2) == 0 ? 1 : 0;
    out[idx(side, i, 6)] = (i % 2) == 0 ? 1 : 0;
  }
  // Alignment patterns.
  for (uint32_t i = 0; i < 7 && kAlign[version][i] != 0; ++i) {
    for (uint32_t j = 0; j < 7 && kAlign[version][j] != 0; ++j) {
      const uint32_t row = kAlign[version][i];
      const uint32_t col = kAlign[version][j];
      const bool skip = (row < 9 && col < 9) || (row < 9 && col + 9 > side) ||
                        (row + 9 > side && col < 9);
      if (skip) continue;
      for (int32_t r = -2; r <= 2; ++r) {
        for (int32_t k = -2; k <= 2; ++k) {
          const bool dark = (r == -2 || r == 2 || k == -2 || k == 2) || (r == 0 && k == 0);
          out[idx(side, static_cast<uint32_t>(static_cast<int32_t>(row) + r),
                  static_cast<uint32_t>(static_cast<int32_t>(col) + k))] = dark ? 1 : 0;
        }
      }
    }
  }
  // Format information, twice.
  const uint32_t format = format_bits(level_index, mask);
  for (uint32_t i = 0; i < 15; ++i) {
    const bool dark = ((format >> i) & 1) != 0;
    if (i < 6) {
      out[idx(side, 8, i)] = dark ? 1 : 0;
    } else if (i < 8) {
      out[idx(side, 8, i + 1)] = dark ? 1 : 0;
    } else if (i == 8) {
      out[idx(side, 7, 8)] = dark ? 1 : 0;  // row 6 is the timing line
    } else {
      out[idx(side, 14 - i, 8)] = dark ? 1 : 0;
    }
    if (i < 8) {
      out[idx(side, 8, side - 1 - i)] = dark ? 1 : 0;
    } else {
      out[idx(side, side - 15 + i, 8)] = dark ? 1 : 0;
    }
  }
  out[idx(side, side - 8, 8)] = 1;  // always dark

  // Version information, from version 7 on.
  if (version >= 7) {
    const uint32_t bits = version_bits(version);
    for (uint32_t i = 0; i < 18; ++i) {
      const bool dark = ((bits >> i) & 1) != 0;
      const uint32_t a = i / 3;
      const uint32_t b = i % 3;
      out[idx(side, b + side - 11, a)] = dark ? 1 : 0;
      out[idx(side, a, b + side - 11)] = dark ? 1 : 0;
    }
  }

  // The data, in the zigzag of the standard, masked module by module.
  uint32_t bit = 0;
  const uint32_t total_bits = cw_len * 8;
  for (int32_t right = static_cast<int32_t>(side) - 1; right >= 1; right -= 2) {
    if (right == 6) --right;
    for (int32_t vert = 0; vert < static_cast<int32_t>(side); ++vert) {
      const bool upward = ((right + 1) & 2) == 0;
      const int32_t row = upward ? (static_cast<int32_t>(side) - 1 - vert) : vert;
      for (int32_t k = 0; k < 2; ++k) {
        const int32_t col = right - k;
        if (col < 0) continue;
        const uint32_t i = idx(side, static_cast<uint32_t>(row), static_cast<uint32_t>(col));
        if (is_fn(side, static_cast<uint32_t>(row), static_cast<uint32_t>(col))) continue;
        bool dark = (bit < total_bits) && (((cw[bit / 8] >> (7 - (bit % 8))) & 1) != 0);
        if (masked(mask, static_cast<uint32_t>(row), static_cast<uint32_t>(col))) dark = !dark;
        out[i] = dark ? 1 : 0;
        ++bit;
      }
    }
  }
}

uint32_t penalty(const uint8_t* m, uint32_t side) {
  uint32_t score = 0;
  for (uint32_t i = 0; i < side; ++i) {
    uint32_t run_row = 1;
    uint32_t run_col = 1;
    for (uint32_t j = 1; j < side; ++j) {
      run_row = (m[i * side + j] == m[i * side + j - 1]) ? run_row + 1 : 1;
      if (run_row == 5) score += 3;
      else if (run_row > 5) score += 1;
      run_col = (m[j * side + i] == m[(j - 1) * side + i]) ? run_col + 1 : 1;
      if (run_col == 5) score += 3;
      else if (run_col > 5) score += 1;
    }
  }
  for (uint32_t row = 0; row + 1 < side; ++row) {
    for (uint32_t col = 0; col + 1 < side; ++col) {
      const uint8_t a = m[row * side + col];
      if (a == m[row * side + col + 1] && a == m[(row + 1) * side + col] &&
          a == m[(row + 1) * side + col + 1]) {
        score += 3;
      }
    }
  }
  static const uint8_t kPattern[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
  static const uint8_t kReversed[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
  for (uint32_t i = 0; i < side; ++i) {
    for (uint32_t j = 0; j + 11 <= side; ++j) {
      bool row_match = true;
      bool row_rev = true;
      bool col_match = true;
      bool col_rev = true;
      for (uint32_t k = 0; k < 11; ++k) {
        const uint8_t rv = m[i * side + j + k];
        const uint8_t cv = m[(j + k) * side + i];
        if (rv != kPattern[k]) row_match = false;
        if (rv != kReversed[k]) row_rev = false;
        if (cv != kPattern[k]) col_match = false;
        if (cv != kReversed[k]) col_rev = false;
      }
      if (row_match) score += 40;
      if (row_rev) score += 40;
      if (col_match) score += 40;
      if (col_rev) score += 40;
    }
  }
  uint32_t dark = 0;
  for (uint32_t i = 0; i < side * side; ++i) dark += m[i];
  const uint32_t percent = (dark * 100) / (side * side);
  const uint32_t deviation = (percent > 50) ? (percent - 50) : (50 - percent);
  return score + (deviation / 5) * 10;
}

// Interleaves the data blocks and their error correction, which is the order the
// codewords go into the symbol.
uint32_t build_codewords(uint32_t version, Level level, uint32_t data_len) {
  const uint32_t l = static_cast<uint32_t>(level);
  const uint32_t ec_len = kEc[version][l];
  const uint32_t blocks = kBlocks[version][l];
  const uint32_t data = data_codewords(version, level);
  const uint32_t short_len = data / blocks;
  const uint32_t long_blocks = data % blocks;  // the last ones carry one more
  const uint32_t short_blocks = blocks - long_blocks;

  // Static and not on the stack: this is the buffer that blew the stack canary of
  // the loop task on the board (4 KB of a 8 KB stack, measured 30/10). One
  // encoder runs at a time, so a second copy would buy nothing.
  static uint8_t ec[kMaxBlocks][kMaxEc];
  static uint32_t starts[kMaxBlocks];
  static uint32_t sizes[kMaxBlocks];
  uint32_t offset = 0;
  for (uint32_t b = 0; b < blocks; ++b) {
    sizes[b] = short_len + ((b >= short_blocks) ? 1 : 0);
    starts[b] = offset;
    rs_ecc(g_data + offset, sizes[b], ec[b], ec_len);
    offset += sizes[b];
  }
  (void)data_len;

  uint32_t at = 0;
  for (uint32_t i = 0; i <= short_len; ++i) {
    for (uint32_t b = 0; b < blocks; ++b) {
      if (i < sizes[b]) g_cw[at++] = g_data[starts[b] + i];
    }
  }
  for (uint32_t i = 0; i < ec_len; ++i) {
    for (uint32_t b = 0; b < blocks; ++b) g_cw[at++] = ec[b][i];
  }
  return at;
}

}  // namespace

uint32_t data_modules(uint32_t version) {
  if (version < 1 || version > kMaxVersion) return 0;
  const uint32_t side = 17 + 4 * version;
  return side * side - count_function_modules(version);
}

uint32_t side_for(const char* text, Level level, Mode mode) {
  if (text == nullptr) return 0;
  const uint32_t len = static_cast<uint32_t>(strlen(text));
  if (len == 0) return 0;
  const Mode used = (mode == Mode::kAuto) ? detect_mode(text, len) : mode;
  if (used != Mode::kAuto && !mode_carries(used, text, len)) return 0;

  for (uint32_t version = 1; version <= kMaxVersion; ++version) {
    const uint32_t header = 4 + count_bits(version, used);
    const uint32_t capacity = data_codewords(version, level) * 8;
    if (header + data_bits(used, text, len) <= capacity) return 17 + 4 * version;
  }
  return 0;
}

bool encode(const char* text, Level level, Mode mode, uint8_t* out, uint32_t out_cap, Info& info) {
  if (text == nullptr || out == nullptr) return false;
  const uint32_t len = static_cast<uint32_t>(strlen(text));
  if (len == 0) return false;

  const Mode used = (mode == Mode::kAuto) ? detect_mode(text, len) : mode;
  if (used == Mode::kAuto || !mode_carries(used, text, len)) return false;

  uint32_t version = 0;
  for (uint32_t v = 1; v <= kMaxVersion; ++v) {
    const uint32_t header = 4 + count_bits(v, used);
    if (header + data_bits(used, text, len) <= data_codewords(v, level) * 8) {
      version = v;
      break;
    }
  }
  if (version == 0) return false;

  const uint32_t side = 17 + 4 * version;
  if (out_cap < side * side) return false;

  init_gf();

  // The data codewords: the mode, the length, the text, the terminator and the
  // padding the standard asks for.
  const uint32_t capacity = data_codewords(version, level);
  memset(g_data, 0, sizeof(g_data));
  Bits w = {g_data, sizeof(g_data), 0};
  w.put(mode_indicator(used), 4);
  w.put(len, count_bits(version, used));
  write_payload(used, text, len, w);
  const uint32_t capacity_bits = capacity * 8;
  const uint32_t left = (capacity_bits > w.at) ? (capacity_bits - w.at) : 0;
  w.put(0, (left < 4) ? left : 4);
  if (w.at % 8 != 0) w.put(0, 8 - (w.at % 8));
  uint8_t pad = 0xEC;
  while (w.at + 8 <= capacity_bits) {
    w.put(pad, 8);
    pad = (pad == 0xEC) ? 0x11 : 0xEC;
  }

  const uint32_t cw_len = build_codewords(version, level, capacity);
  mark_patterns(version);

  // The standard's mask: all eight are drawn and the one with the lowest penalty
  // is the one that stays.
  uint32_t best_mask = 0;
  uint32_t best_score = 0xFFFFFFFF;
  for (uint32_t mask = 0; mask < 8; ++mask) {
    draw(version, level, g_cw, cw_len, mask, out);
    const uint32_t score = penalty(out, side);
    if (score < best_score) {
      best_score = score;
      best_mask = mask;
    }
  }
  draw(version, level, g_cw, cw_len, best_mask, out);

  info.side = side;
  info.version = version;
  info.mask = best_mask;
  info.level = level;
  info.mode = used;
  return true;
}

}  // namespace qr
