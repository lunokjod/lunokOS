// **The run-length of this system**: the stream of runs packed and unpacked, with no board under it.
//
// What a cache of icons asks of a compressor is three things and this is where they are checked: a picture that is
// mostly paper packs to a fraction of itself, a blob that is not a stream of runs is refused with a word instead of a
// picture made of rubbish, and the ceiling of the packing is a promise the caller budgets with (`packed_capacity`), so
// a caller that made room for that much and got zero knows the stream is broken and not that it needed more memory
// (docs/30, docs/75).
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "rle/rle.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// A picture of this panel: nibbles, mostly paper, with a few dark cells —which is what an icon of the launcher is and
// what a run-length is for.
uint32_t a_picture(uint8_t* into, uint32_t capacity) {
  uint32_t n = 0;
  for (uint32_t row = 0; row < 48; ++row) {
    for (uint32_t col = 0; col < 24; ++col) {
      if (n >= capacity) return n;
      // Three rows of ink in twenty-four of paper: a mark in the middle of a cell.
      const bool ink = (row % 12) >= 4 && (row % 12) < 7 && (col % 8) >= 2 && (col % 8) < 6;
      into[n] = ink ? 0x00 : 0xFF;
      ++n;
    }
  }
  return n;
}

}  // namespace

// **Anada i tornada**: what is packed unpacks to the same bytes, and the header says how many there are —which is what
// lets a reader ask for the memory before unpacking instead of guessing (docs/75).
void test_a_picture_of_paper_goes_round_and_comes_back_whole(void) {
  uint8_t picture[48 * 24];
  const uint32_t len = a_picture(picture, sizeof(picture));
  TEST_ASSERT_EQUAL_UINT32(48u * 24u, len);

  uint8_t packed[1024];
  const uint32_t got = rle::pack(picture, len, packed, sizeof(packed));
  TEST_ASSERT_GREATER_THAN_UINT32(rle::kHeader, got);
  // **A picture of paper is a tenth of itself and not a half**: the run of twenty-four paper nibbles in a row is one
  // control byte and one byte. The number is loose on purpose —a different picture packs differently— and tight enough
  // to say the runs are being found.
  TEST_ASSERT_LESS_THAN_UINT32(len / 4u, got);
  TEST_ASSERT_EQUAL_UINT32(len, rle::unpacked_size(packed, got));

  uint8_t back[48 * 24];
  TEST_ASSERT_EQUAL_UINT32(len, rle::unpack(packed, got, back, sizeof(back)));
  TEST_ASSERT_EQUAL_MEMORY(picture, back, len);
}

// **El sostre és una promesa**: a blob of random bytes does not compress, and a caller that made room for
// `packed_capacity` gets a stream that still unpacks to the original —the format never grows past its ceiling— (docs/75).
void test_the_ceiling_of_the_packing_is_what_the_caller_budgets(void) {
  uint8_t noise[200];
  // A sequence with no three equal bytes in a row, which is the case that gives the worst ratio.
  for (uint32_t i = 0; i < sizeof(noise); ++i) noise[i] = static_cast<uint8_t>((i * 37u) % 251u);

  const uint32_t capacity = rle::packed_capacity(sizeof(noise));
  uint8_t packed[512];
  TEST_ASSERT_TRUE(capacity <= sizeof(packed));

  const uint32_t got = rle::pack(noise, sizeof(noise), packed, capacity);
  TEST_ASSERT_GREATER_THAN_UINT32(0, got);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(capacity, got);

  uint8_t back[200];
  TEST_ASSERT_EQUAL_UINT32(sizeof(noise), rle::unpack(packed, got, back, sizeof(back)));
  TEST_ASSERT_EQUAL_MEMORY(noise, back, sizeof(noise));

  // **I un buffer de menys no és mitja feina**: the packing refuses before writing anything, and the unpacking refuses a
  // stream that does not fit instead of cutting a picture in half (docs/75).
  TEST_ASSERT_EQUAL_UINT32(0, rle::pack(noise, sizeof(noise), packed, capacity - 1));
  TEST_ASSERT_EQUAL_UINT32(0, rle::unpack(packed, got, back, sizeof(back) - 1));
}

// **What is not a stream of runs is said**: an empty file, a text, a stream with the magic of another system —the cache
// of an app is read from a card, and a card is a place where anything can be (docs/30, docs/75).
void test_a_blob_that_is_not_a_stream_of_runs_is_refused(void) {
  const char* text = "this is a text and not a picture of runs";
  const uint32_t len = static_cast<uint32_t>(strlen(text));

  TEST_ASSERT_EQUAL_UINT32(0, rle::unpacked_size(reinterpret_cast<const uint8_t*>(text), len));
  uint8_t out[64];
  TEST_ASSERT_EQUAL_UINT32(0, rle::unpack(reinterpret_cast<const uint8_t*>(text), len, out, sizeof(out)));
  // **I un tros de stream**: the magic and a length, and then nothing.
  const uint8_t cut[7] = {'R', 'L', 'E', 0x10, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT32(16, rle::unpacked_size(cut, sizeof(cut)));
  TEST_ASSERT_EQUAL_UINT32(0, rle::unpack(cut, sizeof(cut), out, sizeof(out)));
  // **I un stream que diu que descomprimeix més del que pot**: the header promises sixteen and the run says two hundred.
  const uint8_t liar[9] = {'R', 'L', 'E', 0x10, 0x00, 0x00, 0x00, 0x80, 0xFF};
  TEST_ASSERT_EQUAL_UINT32(0, rle::unpack(liar, sizeof(liar), out, sizeof(out)));
}

// **An empty stream and a stream of one byte**, which are the two edges of the arithmetic of the format.
void test_the_edges_of_the_format(void) {
  uint8_t out[8];
  TEST_ASSERT_EQUAL_UINT32(0, rle::packed_capacity(0));
  TEST_ASSERT_EQUAL_UINT32(0, rle::pack(nullptr, 0, out, sizeof(out)));

  const uint8_t one = 0x42;
  uint8_t packed[16];
  const uint32_t got = rle::pack(&one, 1, packed, sizeof(packed));
  TEST_ASSERT_EQUAL_UINT32(rle::kHeader + 2, got);
  uint8_t back[1] = {0};
  TEST_ASSERT_EQUAL_UINT32(1, rle::unpack(packed, got, back, sizeof(back)));
  TEST_ASSERT_EQUAL_UINT8(0x42, back[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_picture_of_paper_goes_round_and_comes_back_whole);
  RUN_TEST(test_the_ceiling_of_the_packing_is_what_the_caller_budgets);
  RUN_TEST(test_a_blob_that_is_not_a_stream_of_runs_is_refused);
  RUN_TEST(test_the_edges_of_the_format);
  return UNITY_END();
}
