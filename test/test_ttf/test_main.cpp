// **A TrueType font read and drawn** (docs/30): the tables a text is laid out with, the glyph a code asks
// for, and the outline of that glyph turned into pixels at the size the caller names.
//
// The vector is a font this repository owns: `OLD/apps/font-ttf/res/lunok-8x8.ttf`, built by the script of
// docs/30 out of the same eight by eight letters that `font-bdf` and `font-pcf` draw. So the numbers of the
// test are not somebody's font: they are the letters of those two files, which are a picture that already has
// a test — an `A` is `18 24 42 7e 42 42 42 00` — and here the same `A` has to come out of curves and lines.
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "ttf/ttf.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// The bytes of a file of the repository, read from the machine the suite runs on.
uint32_t read_file(const char* path, uint8_t* out, uint32_t cap) {
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return 0;
  const size_t got = fread(out, 1, cap, file);
  fclose(file);
  return static_cast<uint32_t>(got);
}

uint8_t g_file[8192];
uint32_t g_length = 0;

// The font of the app `font-ttf`, read once.
ttf::Face the_face() {
  if (g_length == 0) g_length = read_file("OLD/apps/font-ttf/res/lunok-8x8.ttf", g_file, sizeof(g_file));
  ttf::Face face;
  const char* why = ttf::open(g_file, g_length, face);
  TEST_ASSERT_NULL_MESSAGE(why, why);
  return face;
}

// The eight rows of the `A` of `font-bdf` and of `font-pcf`, which is what a size of eight pixels has to give.
const uint8_t kRowsOfA[8] = {0x18, 0x24, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00};

}  // namespace

// **The tables say what the font is**: the size of its body, how many glyphs it has, and where its line sits.
static void test_a_font_says_what_it_is() {
  const ttf::Face face = the_face();
  TEST_ASSERT_EQUAL_UINT16(64, face.units_per_em);
  TEST_ASSERT_EQUAL_UINT16(7, face.glyphs);  // the six letters and `.notdef`
  TEST_ASSERT_EQUAL_INT16(64, face.ascent);
  TEST_ASSERT_EQUAL_INT16(0, face.descent);
  TEST_ASSERT_TRUE(face.format4);
  // **A code finds its glyph through the character map**, and a code the font does not carry says so instead
  // of drawing the first one (docs/30).
  uint16_t glyph = 0;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::glyph_of(face, 'A', glyph)));
  TEST_ASSERT_EQUAL_UINT16(1, glyph);
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::glyph_of(face, 'F', glyph)));
  TEST_ASSERT_EQUAL_UINT16(6, glyph);
  TEST_ASSERT_EQUAL_INT(ttf::kNoGlyph, static_cast<int>(ttf::glyph_of(face, 'Z', glyph)));
  TEST_ASSERT_EQUAL_INT(ttf::kNoGlyph, static_cast<int>(ttf::glyph_of(face, ' ', glyph)));
  // **The pen moves by the advance of the glyph**, in pixels, which is the one number a line is laid out with.
  TEST_ASSERT_EQUAL_UINT16(8, ttf::advance(face, 1, 8));
  TEST_ASSERT_EQUAL_UINT16(16, ttf::advance(face, 1, 16));
  TEST_ASSERT_EQUAL_UINT16(1, ttf::advance(face, 1, 1));  // never zero: a pen that does not move is a loop
}

// **An outline becomes the same pixels the bitmap face of the same letters has**: the `A` at eight pixels is
// the eight rows of `font-bdf`, column for column, and that is the whole proof that the curves and the winding
// rule are right (docs/30).
static void test_an_outline_becomes_the_pixels_of_its_bitmap() {
  const ttf::Face face = the_face();
  uint16_t glyph = 0;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::glyph_of(face, 'A', glyph)));
  uint8_t rows[8] = {0};
  ttf::Bitmap box;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::render(face, glyph, 8, rows, 1, 8, 8, box)));
  // The ink of this `A` starts one column in and does not reach the last row, so the box is the ink and the
  // pen knows where to put it: six wide, seven tall, one column right of the pen and eight pixels above the
  // baseline (docs/30).
  TEST_ASSERT_EQUAL_UINT16(6, box.width);
  TEST_ASSERT_EQUAL_UINT16(7, box.height);
  TEST_ASSERT_EQUAL_INT16(1, box.left);
  TEST_ASSERT_EQUAL_INT16(-8, box.top);
  TEST_ASSERT_GREATER_THAN(0, ttf::edges_used());
  for (uint32_t row = 0; row < box.height; ++row) {
    for (uint32_t column = 0; column < box.width; ++column) {
      const bool drawn = (rows[row] & (0x80u >> column)) != 0;
      // The column of the box is the column of the bitmap face plus what the pen had to walk in, which is
      // the `left` of the box.
      const bool wanted = ((kRowsOfA[row] >> (7 - (column + box.left))) & 1u) != 0;
      TEST_ASSERT_EQUAL_UINT8(wanted ? 1 : 0, drawn ? 1 : 0);
    }
  }
  // **Twice the size is twice the ink**, and the box grows with it: a face of outlines is not a picture that
  // is stretched, it is drawn again (docs/30).
  uint8_t big[2 * 16] = {0};
  ttf::Bitmap twice;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::render(face, glyph, 16, big, 2, 16, 16, twice)));
  TEST_ASSERT_EQUAL_UINT16(12, twice.width);
  TEST_ASSERT_EQUAL_UINT16(14, twice.height);
  TEST_ASSERT_EQUAL_INT16(2, twice.left);
  // The first row of the `A` is two pixels wide at eight and four at sixteen, four columns into a box that
  // is twelve wide —so it is one byte of rows and the second byte is empty—, and the row below it is the
  // other half of the doubled pixel.
  TEST_ASSERT_EQUAL_UINT8(0x0F, big[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, big[1]);
  TEST_ASSERT_EQUAL_UINT8(0x0F, big[2]);
}

// **A glyph with more curves than the block of edges holds is drawn with fewer pieces** (docs/30, docs/75):
// Lato Black's `g` has ninety-one points and at six pieces a curve it does not fit; the reader cuts at four
// and then three before it gives up, so the letter arrives whole —a corner is not a bug and a missing letter
// is—. This is the face of the buttons page, read from the disk of the repository.
static void test_a_glyph_with_too_many_curves_is_drawn_with_fewer_pieces() {
  static uint8_t big[24576];
  const uint32_t length = read_file("OLD/apps/buttons/res/lato-black.ttf", big, sizeof(big));
  TEST_ASSERT_GREATER_THAN(0, length);
  ttf::Face face;
  const char* why = ttf::open(big, length, face);
  TEST_ASSERT_NULL_MESSAGE(why, why);
  uint16_t glyph = 0;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::glyph_of(face, 'g', glyph)));
  static uint8_t rows[64 * 64];
  ttf::Bitmap box;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::render(face, glyph, 24, rows, 64, 64, 64, box)));
  TEST_ASSERT_GREATER_THAN(0, box.width);
  TEST_ASSERT_GREATER_THAN(0, box.height);
  // **Both sides of the letter are there**: the bowl of a `g` reaches the right of its box, and a reader
  // that stopped at the contour that did not fit would draw the stem and nothing else.
  uint16_t last = 0;
  for (uint16_t x = 0; x < box.width; ++x) {
    for (uint16_t y = 0; y < box.height; ++y) {
      if ((rows[y * 64 + (x >> 3)] & (0x80u >> (x & 7))) != 0) last = x;
    }
  }
  TEST_ASSERT_GREATER_THAN(static_cast<int>(box.width * 2 / 3), static_cast<int>(last));
}

// **What is not a font, or a font this reader does not read, is refused with the reason** (docs/30).
static void test_what_is_not_a_font_says_why() {
  ttf::Face face;
  const uint8_t too_short[4] = {0x00, 0x01, 0x00, 0x00};
  TEST_ASSERT_NOT_NULL(ttf::open(too_short, sizeof(too_short), face));
  // **`OTTO` is the other flavour**: outlines in CFF, which is a different format and another reader.
  uint8_t otto[12] = {'O', 'T', 'T', 'O', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_NOT_NULL(ttf::open(otto, sizeof(otto), face));
  static uint8_t scratch[8192];
  TEST_ASSERT_GREATER_THAN(0, g_length);
  memcpy(scratch, g_file, g_length);
  // A file that stops in the middle of its directory of tables is not read table by table to see how far it
  // goes, and one that says a table starts past its end is not believed.
  TEST_ASSERT_NOT_NULL(ttf::open(scratch, 30, face));
  memcpy(scratch, g_file, g_length);
  scratch[12 + 8] = 0x7F;  // the offset of the first table, four bytes big-endian
  scratch[12 + 9] = 0xFF;
  TEST_ASSERT_NOT_NULL(ttf::open(scratch, g_length, face));
  // And a glyph that does not fit the block of the caller is a `kNoRoom` and not a page drawn outside it.
  const ttf::Face good = the_face();
  uint16_t glyph = 0;
  TEST_ASSERT_EQUAL_INT(ttf::kOk, static_cast<int>(ttf::glyph_of(good, 'A', glyph)));
  uint8_t small[8] = {0};
  ttf::Bitmap box;
  TEST_ASSERT_EQUAL_INT(ttf::kNoRoom, static_cast<int>(ttf::render(good, glyph, 8, small, 1, 4, 8, box)));
  // A glyph of nothing —the space of a font— is not an error and not an outline either.
  TEST_ASSERT_EQUAL_INT(ttf::kNoGlyph, static_cast<int>(ttf::render(good, 0, 8, small, 1, 8, 8, box)));
}

// **The box a face travels in** (0.150.0, docs/46 D6): a `.woff` is an sfnt compressed table by table, and opening it
// gives the bytes the TrueType reader already reads —the offset table, one record of sixteen bytes per table, the tables
// padded to four bytes— whether a table was stored or compressed. The box below has two tables: `TEST` stored as it is
// and `DATA` compressed by the real zlib, which is what the face of a book is made of.
const uint8_t kWoffBox[] = {
    0x77, 0x4F, 0x46, 0x46, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x45, 0x53, 0x54,
    0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x44, 0x41, 0x54, 0x41, 0x00, 0x00, 0x00, 0x5C,
    0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00,
    0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x78, 0xDA, 0x33, 0x30,
    0x34, 0x32, 0x36, 0x31, 0x35, 0x33, 0xB7, 0xB0, 0x4C, 0x4C, 0x4A, 0x4E,
    0x49, 0x4D, 0x4B, 0xCF, 0xC8, 0xCC, 0x02, 0x00, 0x35, 0x07, 0x06, 0x05,
};

const uint8_t kWoffSfnt[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00,
    0x54, 0x45, 0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C,
    0x00, 0x00, 0x00, 0x08, 0x44, 0x41, 0x54, 0x41, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x14, 0x61, 0x62, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
};

void test_a_woff_becomes_the_sfnt_it_carries(void) {
  TEST_ASSERT_TRUE(ttf::is_woff(kWoffBox, sizeof(kWoffBox)));
  TEST_ASSERT_FALSE(ttf::is_woff2(kWoffBox, sizeof(kWoffBox)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kWoffSfnt), ttf::woff_sfnt_size(kWoffBox, sizeof(kWoffBox)));

  uint8_t out[128];
  uint32_t written = 0;
  TEST_ASSERT_TRUE(ttf::woff_to_sfnt(kWoffBox, sizeof(kWoffBox), out, sizeof(out), written));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kWoffSfnt), written);
  TEST_ASSERT_EQUAL_INT(0, memcmp(kWoffSfnt, out, written));

  // **And what comes out is an sfnt of one face**: the flavor the woff had and the two tables it carried.
  TEST_ASSERT_EQUAL_UINT8(0x00, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, out[3]);
  TEST_ASSERT_EQUAL_UINT8(2, out[5]);

  // A block that is too small is refused and nothing is written, a box cut in the middle is not a font, and bytes that
  // are not a woff say so instead of pretending.
  TEST_ASSERT_FALSE(ttf::woff_to_sfnt(kWoffBox, sizeof(kWoffBox), out, 40, written));
  TEST_ASSERT_EQUAL_UINT32(0, written);
  TEST_ASSERT_FALSE(ttf::woff_to_sfnt(kWoffBox, 60, out, sizeof(out), written));
  TEST_ASSERT_FALSE(ttf::is_woff(kWoffSfnt, sizeof(kWoffSfnt)));
  TEST_ASSERT_EQUAL_UINT32(0, ttf::woff_sfnt_size(kWoffSfnt, sizeof(kWoffSfnt)));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_font_says_what_it_is);
  RUN_TEST(test_an_outline_becomes_the_pixels_of_its_bitmap);
  RUN_TEST(test_a_glyph_with_too_many_curves_is_drawn_with_fewer_pieces);
  RUN_TEST(test_what_is_not_a_font_says_why);
  RUN_TEST(test_a_woff_becomes_the_sfnt_it_carries);
  return UNITY_END();
}
