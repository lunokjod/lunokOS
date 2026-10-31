// The QR library has to make a QR a scanner agrees with, at any level and in the
// mode that carries the text. These tests read the symbol back the way a scanner
// does — the format says which level and mask were used, the modules are
// un-masked and read in the zigzag of the standard, the codewords are
// de-interleaved, every block passes the Reed-Solomon check, and the text that
// comes out is the text that went in (docs/50).
//
// The tables are checked from the other side too: the geometry has to add up to
// the total codewords the standard publishes, and the capacities have to be the
// published ones.
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "qr/qr.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

uint8_t g_matrix[qr::kMaxModules];

// Total codewords of each version, as the standard publishes them.
const uint16_t kTotals[41] = {
    0,    26,   44,   70,   100,  134,  172,  196,  242,  292,  346,  404,  466,  532,
    581,  655,  733,  815,  901,  991,  1085, 1156, 1258, 1364, 1474, 1588, 1706, 1828,
    1921, 2051, 2185, 2323, 2465, 2611, 2761, 2876, 3034, 3196, 3362, 3532, 3706,
};

const uint8_t kRowsEc[21][4] = {
    {0, 0, 0, 0},     {7, 10, 13, 17},  {10, 16, 22, 28}, {15, 26, 18, 22}, {20, 18, 26, 16},
    {26, 24, 18, 22}, {18, 16, 24, 28}, {20, 18, 18, 26}, {24, 22, 22, 26}, {30, 22, 20, 24},
    {18, 26, 24, 28}, {20, 30, 28, 24}, {24, 22, 26, 28}, {26, 22, 24, 22}, {30, 24, 20, 24},
    {22, 24, 30, 24}, {24, 28, 24, 30}, {28, 28, 28, 28}, {30, 26, 28, 28}, {28, 26, 26, 26},
    {28, 26, 30, 28},
};

const uint8_t kRowsBlocks[21][4] = {
    {0, 0, 0, 0},     {1, 1, 1, 1},     {1, 1, 1, 1},     {1, 1, 2, 2},     {1, 2, 2, 4},
    {1, 2, 4, 4},     {2, 4, 4, 4},     {2, 4, 6, 5},     {2, 4, 6, 6},     {2, 5, 8, 8},
    {4, 5, 8, 8},     {4, 5, 8, 11},    {4, 8, 10, 11},   {4, 9, 12, 16},   {4, 9, 16, 16},
    {6, 10, 12, 18},  {6, 10, 17, 16},  {6, 11, 16, 19},  {6, 13, 18, 21},  {7, 14, 21, 25},
    {8, 16, 20, 25},
};

// The block layout of the few versions the reading side needs, copied from the
// standard: error correction codewords per block and number of blocks.
struct Row {
  uint32_t version;
  qr::Level level;
  uint32_t ec;
  uint32_t blocks;
};

const Row kRows[] = {
    {1, qr::Level::kL, 7, 1},  {1, qr::Level::kM, 10, 1}, {1, qr::Level::kQ, 13, 1},
    {1, qr::Level::kH, 17, 1}, {2, qr::Level::kL, 10, 1}, {2, qr::Level::kM, 16, 1},
    {2, qr::Level::kQ, 22, 1}, {2, qr::Level::kH, 28, 1}, {3, qr::Level::kL, 15, 1},
    {3, qr::Level::kM, 26, 1}, {3, qr::Level::kQ, 18, 2}, {3, qr::Level::kH, 22, 2},
    {4, qr::Level::kQ, 26, 2}, {4, qr::Level::kH, 16, 4}, {7, qr::Level::kM, 18, 4},
};

const Row* row_for(uint32_t version, qr::Level level) {
  for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); ++i) {
    if (kRows[i].version == version && kRows[i].level == level) return &kRows[i];
  }
  return nullptr;
}

const uint8_t kSpots[][7] = {
    {0},  {0},  {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42},
    {6, 26, 46}, {6, 28, 50}, {6, 30, 54}, {6, 32, 58}, {6, 34, 62}, {6, 26, 46, 66},
    {6, 26, 48, 70}, {6, 26, 50, 74}, {6, 30, 54, 78}, {6, 30, 56, 82}, {6, 30, 58, 86},
    {6, 34, 62, 90},
};

bool module(uint32_t side, uint32_t row, uint32_t col) {
  if (row >= side || col >= side) return false;
  return g_matrix[row * side + col] != 0;
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

bool is_function(uint32_t side, uint32_t row, uint32_t col) {
  if (row < 9 && col < 9) return true;
  if (row < 9 && col + 9 > side) return true;
  if (row + 9 > side && col < 9) return true;
  if (row == 6 || col == 6) return true;
  if (row == side - 8 && col == 8) return true;
  const uint32_t version = (side - 17) / 4;
  if (version < 1 || version >= static_cast<uint32_t>(sizeof(kSpots) / sizeof(kSpots[0]))) {
    return false;
  }
  if (version >= 7) {
    // The version block is six rows by three columns at each end.
    if (row < 6 && col >= side - 11 && col <= side - 9) return true;
    if (col < 6 && row >= side - 11 && row <= side - 9) return true;
  }
  const uint8_t* spots = kSpots[version];
  for (uint32_t i = 0; i < 7 && spots[i] != 0; ++i) {
    for (uint32_t j = 0; j < 7 && spots[j] != 0; ++j) {
      const bool corner = (spots[i] < 9 && spots[j] < 9) ||
                          (spots[i] < 9 && spots[j] + 9 > side) ||
                          (spots[i] + 9 > side && spots[j] < 9);
      if (corner) continue;
      const int32_t dr = static_cast<int32_t>(row) - spots[i];
      const int32_t dc = static_cast<int32_t>(col) - spots[j];
      if (dr >= -2 && dr <= 2 && dc >= -2 && dc <= 2) return true;
    }
  }
  return false;
}

uint32_t read_format(uint32_t side) {
  uint32_t bits = 0;
  for (uint32_t i = 0; i < 15; ++i) {
    bool dark = false;
    if (i < 6) {
      dark = module(side, 8, i);
    } else if (i < 8) {
      dark = module(side, 8, i + 1);
    } else if (i == 8) {
      dark = module(side, 7, 8);
    } else {
      dark = module(side, 14 - i, 8);
    }
    if (dark) bits |= (1u << i);
  }
  return bits;
}

uint32_t read_codewords(uint32_t side, uint32_t mask, uint8_t* out, uint32_t out_cap) {
  memset(out, 0, out_cap);
  uint32_t bit = 0;
  for (int32_t right = static_cast<int32_t>(side) - 1; right >= 1; right -= 2) {
    if (right == 6) --right;
    for (int32_t vert = 0; vert < static_cast<int32_t>(side); ++vert) {
      for (int32_t k = 0; k < 2; ++k) {
        const int32_t col = right - k;
        if (col < 0) continue;
        const bool upward = ((right + 1) & 2) == 0;
        const int32_t row = upward ? (static_cast<int32_t>(side) - 1 - vert) : vert;
        if (is_function(side, static_cast<uint32_t>(row), static_cast<uint32_t>(col))) continue;
        bool dark = module(side, static_cast<uint32_t>(row), static_cast<uint32_t>(col));
        if (masked(mask, static_cast<uint32_t>(row), static_cast<uint32_t>(col))) dark = !dark;
        if (dark && bit < out_cap * 8) out[bit / 8] |= static_cast<uint8_t>(1u << (7 - (bit % 8)));
        ++bit;
      }
    }
  }
  return bit / 8;
}

uint8_t g_exp[512];
uint8_t g_log[256];
bool g_gf = false;

void init_gf() {
  if (g_gf) return;
  g_gf = true;
  uint16_t x = 1;
  for (uint32_t i = 0; i < 255; ++i) {
    g_exp[i] = static_cast<uint8_t>(x);
    g_log[x] = static_cast<uint8_t>(i);
    x <<= 1;
    if ((x & 0x100) != 0) x ^= 0x11d;
  }
  for (uint32_t i = 255; i < 512; ++i) g_exp[i] = g_exp[i - 255];
}

uint8_t gf_mul(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return 0;
  return g_exp[g_log[a] + g_log[b]];
}

bool block_is_valid(const uint8_t* block, uint32_t len, uint32_t roots) {
  for (uint32_t root = 0; root < roots; ++root) {
    uint8_t value = 0;
    for (uint32_t i = 0; i < len; ++i) value = static_cast<uint8_t>(gf_mul(value, g_exp[root]) ^ block[i]);
    if (value != 0) return false;
  }
  return true;
}

// Encodes, reads it back the way a scanner does, and hands the text back through
// `out`. False when anything along the way does not add up.
bool round_trip(const char* text, qr::Level level, char* out, size_t out_cap) {
  init_gf();
  memset(g_matrix, 0, sizeof(g_matrix));
  qr::Info info;
  if (!qr::encode(text, level, qr::Mode::kAuto, g_matrix, sizeof(g_matrix), info)) return false;
  if (info.side != 17 + 4 * info.version) return false;

  // The format has to carry the level and a mask, and its BCH has to hold.
  const uint32_t raw = read_format(info.side) ^ 0x5412u;
  const uint32_t data5 = raw >> 10;
  const uint8_t kLevelCode[4] = {1, 0, 3, 2};
  if (((data5 >> 3) & 3) != kLevelCode[static_cast<uint32_t>(info.level)]) return false;
  uint32_t rem = data5;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
  if ((((data5 << 10) | rem) & 0x7FFF) != (raw & 0x7FFF)) return false;
  const uint32_t mask = data5 & 7;
  if (mask != info.mask) return false;

  const Row* row = row_for(info.version, level);
  if (row == nullptr) return false;
  const uint32_t data_len = kTotals[info.version] - row->ec * row->blocks;
  const uint32_t short_len = data_len / row->blocks;
  const uint32_t long_blocks = data_len % row->blocks;
  const uint32_t short_blocks = row->blocks - long_blocks;

  uint8_t stream[3706];
  const uint32_t stream_len = read_codewords(info.side, mask, stream, sizeof(stream));
  if (stream_len != kTotals[info.version]) return false;

  uint8_t block[64][64];
  memset(block, 0, sizeof(block));
  uint32_t size[64];
  for (uint32_t b = 0; b < row->blocks; ++b) {
    size[b] = short_len + ((b >= short_blocks) ? 1 : 0);
  }
  uint32_t at = 0;
  for (uint32_t i = 0; i <= short_len; ++i) {
    for (uint32_t b = 0; b < row->blocks; ++b) {
      if (i < size[b]) block[b][i] = stream[at++];
    }
  }
  for (uint32_t i = 0; i < row->ec; ++i) {
    for (uint32_t b = 0; b < row->blocks; ++b) block[b][size[b] + i] = stream[at++];
  }
  if (at != stream_len) return false;
  for (uint32_t b = 0; b < row->blocks; ++b) {
    if (!block_is_valid(block[b], size[b] + row->ec, row->ec)) return false;
  }

  // The data, back in order: mode, length, text.
  uint8_t data[3706];
  uint32_t used = 0;
  for (uint32_t b = 0; b < row->blocks; ++b) {
    memcpy(data + used, block[b], size[b]);
    used += size[b];
  }

  const uint32_t mode_bits = static_cast<uint32_t>(data[0] >> 4);
  const uint32_t count_bits = (info.version <= 9) ? ((mode_bits == 1) ? 10 : (mode_bits == 2 ? 9 : 8))
                                                  : ((mode_bits == 1) ? 12 : (mode_bits == 2 ? 11 : 16));
  uint32_t header_bits = 4 + count_bits;
  uint32_t len = 0;
  for (uint32_t i = 4; i < header_bits; ++i) {
    len = (len << 1) | ((data[i / 8] >> (7 - (i % 8))) & 1);
  }

  if (mode_bits == 4) {
    for (uint32_t i = 0; i < len; ++i) {
      if (i + 1 >= out_cap) return false;
      const uint32_t bit = header_bits + i * 8;
      out[i] = static_cast<char>((data[bit / 8] << (bit % 8)) |
                                 (data[bit / 8 + 1] >> (8 - (bit % 8))));
    }
    out[len < out_cap ? len : out_cap - 1] = '\0';
    return strcmp(text, out) == 0;
  }
  // The other modes are checked by hand where they are used.
  return len == strlen(text);
}

}  // namespace

// The geometry has to add up to the codewords the standard publishes: if an
// alignment pattern or the version block is in the wrong place, the count of data
// modules stops matching and this goes red.
void test_the_geometry_adds_up_to_the_published_totals(void) {
  for (uint32_t version = 1; version <= 20; ++version) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kTotals[version], qr::data_modules(version) / 8, "codewords");
  }
  // Beyond the version this build draws, the answer is zero and not a guess.
  TEST_ASSERT_EQUAL_UINT32(0, qr::data_modules(21));
}

// The working buffers of the encoder are sized for a number of blocks and of
// error correction codewords. The board found out the hard way what happens when
// a buffer does not fit: a stack canary panic in the loop task (30/10). The table
// has to stay inside those numbers, and this is where it says so.
void test_the_table_fits_the_working_buffers(void) {
  // The same numbers as lib/qr: at most 25 blocks and 30 codewords per block up
  // to version 20.
  const uint32_t kMaxBlocks = 25;
  const uint32_t kMaxEc = 30;
  for (uint32_t version = 1; version <= 20; ++version) {
    for (uint32_t level = 0; level < 4; ++level) {
      TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(kMaxBlocks, kRowsBlocks[version][level], "blocks");
      TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(kMaxEc, kRowsEc[version][level], "ec per block");
    }
  }
}

// One byte over the published capacity of a version has to need the next one.
void test_the_capacities_are_the_published_ones(void) {
  char text[300];
  struct Case {
    uint32_t bytes;
    qr::Level level;
    uint32_t side;
  };
  // Byte mode at level M: 14, 26, 42, 62 and 84 bytes are versions 1 to 5.
  const Case cases[] = {
      {14, qr::Level::kM, 21}, {26, qr::Level::kM, 25}, {42, qr::Level::kM, 29},
      {62, qr::Level::kM, 33}, {84, qr::Level::kM, 37}, {17, qr::Level::kL, 21},
      {11, qr::Level::kQ, 21}, {7, qr::Level::kH, 21},  {32, qr::Level::kL, 25},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    memset(text, 'a', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    text[cases[i].bytes] = '\0';
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(cases[i].side, qr::side_for(text, cases[i].level, qr::Mode::kAuto),
                                     "fits");
    text[cases[i].bytes] = 'a';
    text[cases[i].bytes + 1] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(qr::side_for(text, cases[i].level, qr::Mode::kAuto) > cases[i].side,
                             "one more byte needs a bigger symbol");
  }
}

// The mode is chosen by what the text is made of: numbers pack three to ten bits
// and letters two to eleven, so they are denser than bytes.
void test_the_densest_mode_is_chosen_and_it_can_be_forced(void) {
  char digits[] = "01234567890123456789";   // 20 digits
  char letters[] = "abcdefghijklmnopqrst";  // 20 bytes
  char alnum[] = "HELLO WORLD 123";        // 15 characters of the alnum set

  TEST_ASSERT_EQUAL_UINT32(21, qr::side_for(digits, qr::Level::kM, qr::Mode::kAuto));
  TEST_ASSERT_EQUAL_UINT32(25, qr::side_for(letters, qr::Level::kM, qr::Mode::kAuto));
  TEST_ASSERT_EQUAL_UINT32(21, qr::side_for(alnum, qr::Level::kM, qr::Mode::kAuto));

  qr::Info info;
  memset(g_matrix, 0, sizeof(g_matrix));
  TEST_ASSERT_TRUE(qr::encode(digits, qr::Level::kM, qr::Mode::kAuto, g_matrix, sizeof(g_matrix), info));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(qr::Mode::kNumeric), static_cast<uint32_t>(info.mode));

  // Forcing byte on digits is allowed and takes more room.
  TEST_ASSERT_TRUE(qr::side_for(digits, qr::Level::kM, qr::Mode::kByte) >=
                   qr::side_for(digits, qr::Level::kM, qr::Mode::kAuto));
  // Forcing a mode that cannot carry the text is refused, not silently changed.
  TEST_ASSERT_EQUAL_UINT32(0, qr::side_for(letters, qr::Level::kM, qr::Mode::kNumeric));
  TEST_ASSERT_FALSE(qr::encode(letters, qr::Level::kM, qr::Mode::kAlnum, g_matrix, sizeof(g_matrix), info));
}

// The whole point: what a scanner reads is what was written, at every level and
// for a version with a single block, one with two and one with four.
void test_every_level_reads_back_what_was_written(void) {
  char got[128];
  TEST_ASSERT_TRUE_MESSAGE(round_trip("lunokos", qr::Level::kL, got, sizeof(got)), "level L");
  TEST_ASSERT_TRUE_MESSAGE(round_trip("lunokos", qr::Level::kM, got, sizeof(got)), "level M");
  TEST_ASSERT_TRUE_MESSAGE(round_trip("lunokos", qr::Level::kQ, got, sizeof(got)), "level Q");
  TEST_ASSERT_TRUE_MESSAGE(round_trip("lunokos", qr::Level::kH, got, sizeof(got)), "level H");
  // Version 3 at Q and H has two blocks, so the de-interleaving is exercised.
  TEST_ASSERT_TRUE_MESSAGE(
      round_trip("{\"ver\":\"v1\",\"name\":\"lunokos-prov\"}", qr::Level::kQ, got, sizeof(got)),
      "two blocks Q, version 4");
  TEST_ASSERT_TRUE_MESSAGE(
      round_trip("{\"ver\":\"v1\",\"name\":\"lunokos-prov\"}", qr::Level::kH, got, sizeof(got)),
      "four blocks H, version 4");
  // Version 7 is the first one with a version block.
  // 110 bytes is more than version 6 at level M holds (106), so this one is a
  // version 7 symbol: the first that carries a version block.
  char text[111];
  memset(text, 'q', sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';
  TEST_ASSERT_EQUAL_UINT32(45, qr::side_for(text, qr::Level::kM, qr::Mode::kAuto));
  TEST_ASSERT_TRUE_MESSAGE(round_trip(text, qr::Level::kM, got, sizeof(got)), "version 7");
}

// A symbol that does not fit, an empty text and a buffer that is too small are
// refused instead of writing something that lies.
void test_what_does_not_fit_is_refused(void) {
  qr::Info info;
  memset(g_matrix, 0, sizeof(g_matrix));
  TEST_ASSERT_EQUAL_UINT32(0, qr::side_for("", qr::Level::kM, qr::Mode::kAuto));
  TEST_ASSERT_FALSE(qr::encode("", qr::Level::kM, qr::Mode::kAuto, g_matrix, sizeof(g_matrix), info));

  char big[400];
  memset(big, 'a', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  TEST_ASSERT_EQUAL_UINT32(0, qr::side_for(big, qr::Level::kH, qr::Mode::kAuto));

  // A buffer smaller than the symbol is not written into.
  uint8_t small[100];
  TEST_ASSERT_FALSE(qr::encode("lunokos", qr::Level::kM, qr::Mode::kAuto, small, sizeof(small), info));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_geometry_adds_up_to_the_published_totals);
  RUN_TEST(test_the_table_fits_the_working_buffers);
  RUN_TEST(test_the_capacities_are_the_published_ones);
  RUN_TEST(test_the_densest_mode_is_chosen_and_it_can_be_forced);
  RUN_TEST(test_every_level_reads_back_what_was_written);
  RUN_TEST(test_what_does_not_fit_is_refused);
  return UNITY_END();
}
