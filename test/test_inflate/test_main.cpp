// **Deflate, read the other way** (docs/70): a stream of bytes that a compressor wrote comes back as the
// bytes it came from, and everything that is not a stream is refused with the reason.
//
// The vectors here are real: the three compressed blocks were produced by zlib from the same sentence, so
// what is tried is this reader against a compressor that is not mine —the fixed-Huffman one, the dynamic one
// and the stored one are three different decoders inside the same function— and the two checksums are tried
// against the values everybody quotes (`adler32("Wikipedia")` and `crc32("123456789")`).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "inflate/inflate.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

const char kText[] = "the same word at six sizes, and the paper is not a screen";

// `huffman deflate` of kText, from zlib with the default strategy: a dynamic block.
const uint8_t kRaw[] = {
    0x0D, 0xC9, 0xC1, 0x09, 0xC0, 0x30, 0x0C, 0x03, 0xC0, 0x55, 0x34, 0x40, 0x97, 0x12, 0x8D,
    0xA0, 0x79, 0xD4, 0x09, 0x96, 0xA1, 0x25, 0xD3, 0x27, 0x8F, 0x7B, 0x5D, 0x3D, 0x82, 0xF9,
    0x0A, 0xDF, 0xC8, 0x06, 0x16, 0xDC, 0xFF, 0x63, 0xC9, 0x17, 0x18, 0x0D, 0x75, 0x7E, 0x72,
    0x2A, 0xD1, 0x8D, 0x18, 0x05, 0xC2, 0x77, 0x4A, 0xB1, 0x01};

// The same sentence in a **fixed-Huffman** block: the decoder of a fixed table is another path.
const uint8_t kFixed[] = {0x2B, 0xC9, 0x48, 0x55, 0x28, 0x4E, 0xCC, 0x4D, 0x55, 0x28, 0xCF, 0x2F, 0x4A, 0x51,
                          0x48, 0x2C, 0x51, 0x28, 0xCE, 0xAC, 0x00, 0xE2, 0xAA, 0xD4, 0x62, 0x1D, 0x85, 0xC4,
                          0xBC, 0x14, 0x85, 0x12, 0xA0, 0x7C, 0x41, 0x62, 0x41, 0x6A, 0x91, 0x42, 0x66, 0xB1,
                          0x42, 0x5E, 0x7E, 0x89, 0x42, 0xA2, 0x42, 0x71, 0x72, 0x51, 0x6A, 0x6A, 0x1E, 0x00};

// And in a **zlib** wrapper, which is what a woff holds: the two header bytes and the Adler-32 in front of
// and behind the same block.
const uint8_t kZlib[] = {
    0x78, 0xDA, 0x0D, 0xC9, 0xC1, 0x09, 0xC0, 0x30, 0x0C, 0x03, 0xC0, 0x55, 0x34, 0x40, 0x97, 0x12,
    0x8D, 0xA0, 0x79, 0xD4, 0x09, 0x96, 0xA1, 0x25, 0xD3, 0x27, 0x8F, 0x7B, 0x5D, 0x3D, 0x82, 0xF9,
    0x0A, 0xDF, 0xC8, 0x06, 0x16, 0xDC, 0xFF, 0x63, 0xC9, 0x17, 0x18, 0x0D, 0x75, 0x7E, 0x72, 0x2A,
    0xD1, 0x8D, 0x18, 0x05, 0xC2, 0x77, 0x4A, 0xB1, 0x01, 0x4C, 0x97, 0x14, 0x41};

// A stored block: `00` is not compressing at all, and the four bytes after it are the length and its complement.
const uint8_t kStored[] = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};

uint8_t g_out[512];

}  // namespace

// **The three kinds of block come back as the sentence they were**: dynamic, fixed and stored, each one a
// decoder of its own inside `raw` (docs/70).
static void test_a_stream_comes_back_as_what_it_was() {
  uint32_t written = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(inflate::Status::kOk),
                        static_cast<int>(inflate::raw(kRaw, sizeof(kRaw), g_out, sizeof(g_out), written)));
  TEST_ASSERT_EQUAL_UINT32(strlen(kText), written);
  TEST_ASSERT_EQUAL_MEMORY(kText, g_out, written);

  written = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(inflate::Status::kOk),
                        static_cast<int>(inflate::raw(kFixed, sizeof(kFixed), g_out, sizeof(g_out), written)));
  TEST_ASSERT_EQUAL_MEMORY(kText, g_out, written);

  written = 0;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kOk),
      static_cast<int>(inflate::raw(kStored, sizeof(kStored), g_out, sizeof(g_out), written)));
  TEST_ASSERT_EQUAL_UINT32(5, written);
  TEST_ASSERT_EQUAL_MEMORY("hello", g_out, 5);
}

// **A zlib stream has a checksum and it is checked**: the same bytes with the right trailer come out, and
// one bit changed in the trailer is a stream that is not handed over (docs/70).
static void test_the_zlib_wrapper_checks_itself() {
  uint32_t written = 0;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kOk),
      static_cast<int>(inflate::zlib(kZlib, sizeof(kZlib), g_out, sizeof(g_out), written)));
  TEST_ASSERT_EQUAL_MEMORY(kText, g_out, written);

  uint8_t broken[sizeof(kZlib)];
  memcpy(broken, kZlib, sizeof(kZlib));
  broken[sizeof(kZlib) - 1] ^= 0x01;
  written = 0;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kAdler),
      static_cast<int>(inflate::zlib(broken, sizeof(broken), g_out, sizeof(g_out), written)));

  // And a header that is not deflate is refused before anything is decompressed.
  uint8_t not_zlib[sizeof(kZlib)];
  memcpy(not_zlib, kZlib, sizeof(kZlib));
  not_zlib[0] = 0x77;  // method 7, which does not exist
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kBadData),
      static_cast<int>(inflate::zlib(not_zlib, sizeof(not_zlib), g_out, sizeof(g_out), written)));
}

// **What is not a stream is refused with the reason**, and not read past its end: a stream cut short, a
// block kind that does not exist, and an output too small to hold what the stream says.
static void test_a_stream_that_is_not_one_is_refused() {
  uint32_t written = 0;
  // Three bytes of the dynamic block are not a block.
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kTruncated),
      static_cast<int>(inflate::raw(kRaw, 3, g_out, sizeof(g_out), written)));
  // The fourth kind of block is not a kind: `111` in the first three bits.
  const uint8_t no_kind[] = {0x07};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kBadData),
      static_cast<int>(inflate::raw(no_kind, sizeof(no_kind), g_out, sizeof(g_out), written)));
  // A stored block whose length and complement do not agree.
  const uint8_t lying[] = {0x01, 0x05, 0x00, 0x00, 0x00, 'h', 'e', 'l', 'l', 'o'};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kBadData),
      static_cast<int>(inflate::raw(lying, sizeof(lying), g_out, sizeof(g_out), written)));
  // **And the sentence does not fit in ten bytes of output**: the reader says so instead of writing past the
  // block (docs/75).
  TEST_ASSERT_EQUAL_INT(static_cast<int>(inflate::Status::kNoRoom),
                        static_cast<int>(inflate::raw(kRaw, sizeof(kRaw), g_out, 10, written)));
  // **Nothing at all is not a stream**: a caller with no buffer has made a mistake, and that is not the same
  // thing as a stream that ended early —an empty *buffer* walks the reader to the end of its bits and comes
  // out `kTruncated`—, so the two answers are two words and not one (docs/70).
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kBadData),
      static_cast<int>(inflate::raw(nullptr, 0, g_out, sizeof(g_out), written)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(inflate::Status::kTruncated),
      static_cast<int>(inflate::raw(kRaw, 0, g_out, sizeof(g_out), written)));
}

// **The two checksums a container uses**, against the values everybody quotes: if these are right the table
// of the crc is right, and a zip that says its entry is good will be believed (docs/70).
static void test_the_two_checksums() {
  TEST_ASSERT_EQUAL_HEX32(0x11E60398u, inflate::adler32(reinterpret_cast<const uint8_t*>("Wikipedia"), 9));
  // Done in two pieces gives the one of the whole block: that is what `seed` is for.
  const uint8_t* text = reinterpret_cast<const uint8_t*>("Wikipedia");
  const uint32_t first = inflate::adler32(text, 4);
  TEST_ASSERT_EQUAL_HEX32(0x11E60398u, inflate::adler32(text + 4, 5, first));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, inflate::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9));
  const uint32_t crc_first = inflate::crc32(reinterpret_cast<const uint8_t*>("1234"), 4);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, inflate::crc32(reinterpret_cast<const uint8_t*>("56789"), 5, crc_first));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_stream_comes_back_as_what_it_was);
  RUN_TEST(test_the_zlib_wrapper_checks_itself);
  RUN_TEST(test_a_stream_that_is_not_one_is_refused);
  RUN_TEST(test_the_two_checksums);
  return UNITY_END();
}
