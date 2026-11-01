// The portable font of this system: a face read from a text file, and the runs it makes when a text is
// drawn with it (docs/30).
//
// A face is the table of glyphs a text is drawn with, and the system's own font is one of them, so what is
// tried here is the file format and the walk of the glyphs —not a picture—: the bits of a glyph where the
// face says they are, the width a text takes, the clipping of a run at the border of a label, and every way
// a file stops being a face.
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "kernel/font.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// A block big enough for any face, as the kernel hands to `ui::font_load`.
uint8_t g_bits[kernel::font::kFaceBytes];

const char* kDigits =
    "; two digits and a gap\n"
    "lunok-font 1\n"
    "width 3\n"
    "height 5\n"
    "gap 1\n"
    "char 48\n"
    "###\n"
    "#.#\n"
    "#.#\n"
    "#.#\n"
    "###\n"
    "char 49\n"
    ".#.\n"
    "##.\n"
    ".#.\n"
    ".#.\n"
    "###\n";

kernel::font::Face load(const char* text) {
  kernel::font::Face face = kernel::font::system_face();
  const char* why = kernel::font::load(text, static_cast<uint32_t>(strlen(text)), g_bits, face);
  TEST_ASSERT_NULL_MESSAGE(why, why);
  return face;
}

// The runs of a text, kept as they come: a rectangle each, and how many there were.
struct Runs {
  int32_t x[64];
  int32_t y[64];
  int32_t w[64];
  int32_t h[64];
  uint32_t count;
};

void keep_run(void* user, int32_t x, int32_t y, int32_t w, int32_t h) {
  Runs* runs = static_cast<Runs*>(user);
  if (runs->count >= 64) return;
  runs->x[runs->count] = x;
  runs->y[runs->count] = y;
  runs->w[runs->count] = w;
  runs->h[runs->count] = h;
  ++runs->count;
}

// The bytes of a file of the repository, read from the machine the suite runs on —`test_apps` reads its apps
// the same way— so the vector of a compiled font is the one `bdftopcf` wrote and not a copy of it.
uint32_t read_file(const char* path, char* out, uint32_t cap) {
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return 0;
  const size_t got = fread(out, 1, cap, file);
  fclose(file);
  return static_cast<uint32_t>(got);
}

// The PCF the app `font-pcf` ships with, read once: it is the vector of the two cases of the binary format.
const char* pcf_file(uint32_t& length) {
  static char bytes[4096];
  static uint32_t stored = 0;
  if (stored == 0) {
    stored = read_file("data/apps/font-pcf/res/abcdef-8x8.pcf", bytes, sizeof(bytes));
  }
  length = stored;
  return bytes;
}

// The offset of a table of a pcf, walking the directory the way the reader does: the directory is always
// little-endian, whatever the tables say of themselves.
int32_t table_of(const char* pcf, uint32_t type) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(pcf);
  if (p == nullptr) return -1;
  const auto u32 = [p](uint32_t at) {
    return static_cast<uint32_t>(p[at]) | (static_cast<uint32_t>(p[at + 1]) << 8) |
           (static_cast<uint32_t>(p[at + 2]) << 16) | (static_cast<uint32_t>(p[at + 3]) << 24);
  };
  const uint32_t tables = u32(4);
  if (tables == 0 || tables > 64) return -1;
  for (uint32_t i = 0; i < tables; ++i) {
    const uint32_t at = 8 + i * 16;
    if (u32(at) == type) return static_cast<int32_t>(u32(at + 12));
  }
  return -1;
}

}  // namespace

// **A file that is a face is a face**: the size it says, the gap it says, the codes in order, and one bit
// per `#` where the block puts it (docs/30).
static void test_a_font_file_becomes_a_face() {
  const kernel::font::Face face = load(kDigits);
  TEST_ASSERT_EQUAL_UINT8(3, face.width);
  TEST_ASSERT_EQUAL_UINT8(5, face.height);
  TEST_ASSERT_EQUAL_UINT8(1, face.gap);
  TEST_ASSERT_EQUAL_UINT8(48, face.first);
  TEST_ASSERT_EQUAL_UINT8(2, face.count);
  TEST_ASSERT_NOT_NULL(face.bits);
  TEST_ASSERT_NOT_NULL(face.lit);

  // The '0' of the file: a box. Its top row is three columns of ink and its middle row is the two sides.
  TEST_ASSERT_TRUE(face.lit(face.bits, 0, 0, 0));
  TEST_ASSERT_TRUE(face.lit(face.bits, 0, 2, 0));
  TEST_ASSERT_TRUE(face.lit(face.bits, 0, 0, 2));
  TEST_ASSERT_FALSE_MESSAGE(face.lit(face.bits, 0, 1, 2), "the inside of the zero is not ink");
  TEST_ASSERT_TRUE_MESSAGE(face.lit(face.bits, 0, 1, 4), "the bottom of the zero is a bar of ink");
  // And the '1' is the second glyph, so the block is walked by index and not by code.
  TEST_ASSERT_FALSE(face.lit(face.bits, 1, 0, 0));
  TEST_ASSERT_TRUE(face.lit(face.bits, 1, 1, 0));
  TEST_ASSERT_TRUE(face.lit(face.bits, 1, 1, 4));
  // A code the face does not carry is a gap, not an error.
  TEST_ASSERT_TRUE(kernel::font::has(face, '0'));
  TEST_ASSERT_FALSE(kernel::font::has(face, '2'));
  TEST_ASSERT_FALSE(kernel::font::has(face, 'A'));
}

// **How wide a line is**: the glyph, the gap between two of them, and the scale, with no gap after the last
// one —the same number the bar uses to centre a title (docs/30).
static void test_a_line_is_as_wide_as_its_glyphs() {
  const kernel::font::Face face = load(kDigits);
  TEST_ASSERT_EQUAL_UINT16(0, kernel::font::width(face, "", 1));
  TEST_ASSERT_EQUAL_UINT16(3, kernel::font::width(face, "0", 1));
  TEST_ASSERT_EQUAL_UINT16(7, kernel::font::width(face, "01", 1));   // 3 + 1 + 3
  TEST_ASSERT_EQUAL_UINT16(11, kernel::font::width(face, "000", 1));  // 4 + 4 + 3: the gap goes before a glyph
  TEST_ASSERT_EQUAL_UINT16(14, kernel::font::width(face, "01", 2));  // (3 + 1) * 2 per glyph, less a gap of 2
  // **A code the face does not carry still takes its room**: the text advances, so a partial face does not
  // push the rest of a line (docs/30).
  TEST_ASSERT_EQUAL_UINT16(7, kernel::font::width(face, "AB", 1));
  // The font of the system is a face like any other, and its numbers are the ones the bar has always used.
  const kernel::font::Face& system = kernel::font::system_face();
  TEST_ASSERT_EQUAL_UINT8(5, system.width);
  TEST_ASSERT_EQUAL_UINT8(7, system.height);
  TEST_ASSERT_EQUAL_UINT16(5, kernel::font::width(system, "A", 1));
  TEST_ASSERT_EQUAL_UINT16(11, kernel::font::width(system, "AB", 1));
  // **A lowercase letter is its capital in the font of the system**, which is the one thing that face does
  // and a face from a file does not (docs/30).
  TEST_ASSERT_EQUAL_UINT16(5, kernel::font::width(system, "a", 1));
}

// **The glyphs are drawn as runs of pixels**, and a run that hangs out of the label loses the part that is
// outside and nothing else (docs/30).
static void test_a_text_is_runs_cut_at_the_label() {
  const kernel::font::Face face = load(kDigits);
  Runs runs = {};
  kernel::font::runs(face, "0", 10, 20, 1, kernel::font::no_clip(), keep_run, &runs);
  // The zero is a box: three rows... its top and bottom are one run each, and its three middle rows are two
  // runs each.
  TEST_ASSERT_EQUAL_UINT32(8, runs.count);
  TEST_ASSERT_EQUAL_INT32(10, runs.x[0]);
  TEST_ASSERT_EQUAL_INT32(20, runs.y[0]);
  TEST_ASSERT_EQUAL_INT32(3, runs.w[0]);
  TEST_ASSERT_EQUAL_INT32(1, runs.h[0]);

  // At scale 2 the same glyph is twice as big, and the runs are still runs and not ninety pixels.
  Runs twice = {};
  kernel::font::runs(face, "0", 0, 0, 2, kernel::font::no_clip(), keep_run, &twice);
  TEST_ASSERT_EQUAL_UINT32(8, twice.count);
  TEST_ASSERT_EQUAL_INT32(6, twice.w[0]);
  TEST_ASSERT_EQUAL_INT32(2, twice.h[0]);

  // **Cut at the box**: a text starting at x = 0 with a box of four columns keeps the first column and a
  // half of the glyph and nothing past column three.
  kernel::font::Clip clip;
  clip.x0 = 0;
  clip.y0 = 0;
  clip.x1 = 2;
  clip.y1 = 100;
  Runs cut = {};
  kernel::font::runs(face, "0", 0, 0, 1, clip, keep_run, &cut);
  TEST_ASSERT_TRUE(cut.count > 0);
  for (uint32_t i = 0; i < cut.count; ++i) {
    TEST_ASSERT_TRUE(cut.x[i] + cut.w[i] <= 2);
  }
  // And a text that starts past the box paints nothing at all.
  Runs outside = {};
  kernel::font::runs(face, "0", 40, 0, 1, clip, keep_run, &outside);
  TEST_ASSERT_EQUAL_UINT32(0, outside.count);
}

// **What is not a face is refused with a reason**, and the reason is what an app shows to whoever wrote the
// file (docs/30).
static void test_a_file_that_is_not_a_face_says_why() {
  kernel::font::Face face = kernel::font::system_face();
  const auto load_text = [&face](const char* text) {
    return kernel::font::load(text, static_cast<uint32_t>(strlen(text)), g_bits, face);
  };
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("lunok-font 2\nwidth 3\n"), "a version this system does not read");
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("width 3\nheight 5\n"), "a file with no magic");
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("lunok-font 1\nheight 5\nchar 48\n###\n"), "no width");
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("lunok-font 1\nwidth 9\nheight 5\n"), "a width of nine");
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("lunok-font 1\nwidth 3\nheight 5\n"), "a face with no glyph");
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("lunok-font 1\nwidth 3\nheight 5\ngap 9\n"), "a gap of nine");
  // **A glyph that is cut short**: the rows of the next glyph would be read as the rows of this one.
  TEST_ASSERT_NOT_NULL_MESSAGE(
      load_text("lunok-font 1\nwidth 3\nheight 5\nchar 48\n###\n###\nchar 49\n###\n###\n###\n"),
      "a glyph of two rows");
  // **A hole in the table**: the codes come in order, one by one, or the block and the codes disagree.
  TEST_ASSERT_NOT_NULL_MESSAGE(
      load_text("lunok-font 1\nwidth 3\nheight 5\nchar 48\n###\n###\n###\n###\n###\nchar 50\n###\n###\n###\n"
                "###\n###\n"),
      "a glyph 48 then 50");
  // A line that is not a directive is not ignored: a typo in a font is a font that does not work.
  TEST_ASSERT_NOT_NULL_MESSAGE(load_text("lunok-font 1\nwidht 3\n"), "a misspelt directive");
  // And nothing at all.
  TEST_ASSERT_NOT_NULL(load_text(""));
}

// **A BDF becomes the same face a `.lfont` does**, row of hex after row of hex: the eight by eight block,
// the codes in order, and the runs of a glyph that is drawn from it (docs/30).
static void test_a_bdf_becomes_a_face() {
  // Two glyphs of eight by eight: the A is a triangle over two legs, the B is a stack of three bumps.
  const char* kTwo = "STARTFONT 2.1\n"
                     "COMMENT a bdf with two glyphs\n"
                     "FONTBOUNDINGBOX 8 8 0 0\n"
                     "STARTPROPERTIES 1\n"
                     "FONT_ASCENT 8\n"
                     "ENDPROPERTIES\n"
                     "CHARS 2\n"
                     "STARTCHAR A\n"
                     "ENCODING 65\n"
                     "DWIDTH 8 0\n"
                     "BBX 8 8 0 0\n"
                     "BITMAP\n"
                     "18\n24\n42\n7E\n42\n42\n42\n00\n"
                     "ENDCHAR\n"
                     "STARTCHAR B\n"
                     "ENCODING 66\n"
                     "DWIDTH 8 0\n"
                     "BBX 8 8 0 0\n"
                     "BITMAP\n"
                     "7C\n42\n42\n7C\n42\n42\n7C\n00\n"
                     "ENDCHAR\n"
                     "ENDFONT\n";
  kernel::font::Face face = kernel::font::system_face();
  TEST_ASSERT_NULL_MESSAGE(kernel::font::load_bdf(kTwo, static_cast<uint32_t>(strlen(kTwo)), g_bits, face),
                           "a bdf of two glyphs");
  TEST_ASSERT_EQUAL_UINT8(8, face.width);
  TEST_ASSERT_EQUAL_UINT8(8, face.height);
  TEST_ASSERT_EQUAL_UINT8('A', face.first);
  TEST_ASSERT_EQUAL_UINT8(2, face.count);
  TEST_ASSERT_EQUAL_UINT8(1, face.gap);
  // **The rows are hexadecimal bytes with the leftmost pixel in the top bit**, so `0x18` is two pixels and
  // `0x7E` is six of them, in the row the file puts them.
  TEST_ASSERT_EQUAL_UINT8(0x18, g_bits[0 * kernel::font::kMaxGlyphHeight + 0]);
  TEST_ASSERT_EQUAL_UINT8(0x7E, g_bits[0 * kernel::font::kMaxGlyphHeight + 3]);
  TEST_ASSERT_EQUAL_UINT8(0x7C, g_bits[1 * kernel::font::kMaxGlyphHeight + 0]);
  // And it is a face like any other from there on: the width of a line, and the runs of a glyph.
  TEST_ASSERT_EQUAL_UINT16(17, kernel::font::width(face, "AB", 1));  // 8 + 1 + 8
  Runs runs = {};
  kernel::font::runs(face, "A", 10, 20, 1, kernel::font::no_clip(), keep_run, &runs);
  TEST_ASSERT_TRUE(runs.count > 0);
  TEST_ASSERT_EQUAL_INT32(13, runs.x[0]);  // the first row of the A starts at its third column
  TEST_ASSERT_EQUAL_INT32(20, runs.y[0]);
  TEST_ASSERT_EQUAL_INT32(2, runs.w[0]);
  TEST_ASSERT_EQUAL_INT32(1, runs.h[0]);

  // **`ENCODING -1 <n>`** is the code in the second table, and that is the one that counts.
  const char* kSecond = "STARTFONT 2.1\nFONTBOUNDINGBOX 3 5 0 0\nSTARTCHAR zero\nENCODING -1 48\n"
                        "BITMAP\nE0\nA0\nE0\nA0\nE0\nENDCHAR\nENDFONT\n";
  kernel::font::Face other = kernel::font::system_face();
  TEST_ASSERT_NULL(kernel::font::load_bdf(kSecond, static_cast<uint32_t>(strlen(kSecond)), g_bits, other));
  TEST_ASSERT_EQUAL_UINT8('0', other.first);
  TEST_ASSERT_EQUAL_UINT8(1, other.count);
  TEST_ASSERT_EQUAL_UINT8(3, other.width);
  TEST_ASSERT_EQUAL_UINT8(5, other.height);
  // `0xE0` with the leftmost pixel in the top bit is the first three columns of the five.
  TEST_ASSERT_EQUAL_UINT8(0xE0, g_bits[0]);
}

// **What is not a bdf is refused with a reason**, and a file that is one format or the other is read by the
// reader that its first line asks for (docs/30).
static void test_a_bdf_that_is_not_a_face_says_why() {
  kernel::font::Face face = kernel::font::system_face();
  const auto load_bdf = [&face](const char* text) {
    return kernel::font::load_bdf(text, static_cast<uint32_t>(strlen(text)), g_bits, face);
  };
  const char* kNine = "STARTFONT 2.1\nFONTBOUNDINGBOX 9 8 0 0\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kNine), "a bounding box of nine columns");
  const char* kTall = "STARTFONT 2.1\nFONTBOUNDINGBOX 8 9 0 0\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kTall), "a bounding box of nine rows");
  const char* kNoMagic = "FONTBOUNDINGBOX 8 8 0 0\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kNoMagic), "a bdf that does not start with STARTFONT");
  const char* kNoBox = "STARTFONT 2.1\nSTARTCHAR A\nENCODING 65\nBITMAP\n00\nENDCHAR\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kNoBox), "a bdf with no bounding box");
  const char* kLow = "STARTFONT 2.1\nFONTBOUNDINGBOX 8 8 0 0\nENCODING 10\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kLow), "a code of ten");
  const char* kGap = "STARTFONT 2.1\nFONTBOUNDINGBOX 8 8 0 0\nENCODING 65\nBBX 8 1 0 0\nBITMAP\n00\nENDCHAR\n"
                     "ENCODING 67\nBBX 8 1 0 0\nBITMAP\n00\nENDCHAR\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kGap), "a glyph 65 then 67");
  const char* kShort = "STARTFONT 2.1\nFONTBOUNDINGBOX 8 8 0 0\nENCODING 65\nBBX 8 4 0 0\nBITMAP\n00\nENDCHAR\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kShort), "a glyph with one row of the four it says");
  const char* kNotHex = "STARTFONT 2.1\nFONTBOUNDINGBOX 8 8 0 0\nENCODING 65\nBITMAP\nZZ\nENDCHAR\n";
  TEST_ASSERT_NOT_NULL_MESSAGE(load_bdf(kNotHex), "a row that is not hexadecimal");

  // **The first line decides the reader**: the two formats cannot be mistaken for one another.
  const char* kLunok = "lunok-font 1\nwidth 3\nheight 5\nchar 48\n###\n###\n###\n###\n###\n";
  kernel::font::Face made = kernel::font::system_face();
  TEST_ASSERT_NULL(kernel::font::load_any(kLunok, static_cast<uint32_t>(strlen(kLunok)), g_bits, made));
  TEST_ASSERT_EQUAL_UINT8(3, made.width);
  // A `.lfont` of this system opens with `;` comments and a bdf may open with `COMMENT`: both are skipped
  // before the line that says which format it is, or a face that reads fine would be refused by the sniffer.
  const char* kCommented = "; a face, with a comment first\n;\nlunok-font 1\nwidth 3\nheight 5\nchar 48\n"
                           "###\n###\n###\n###\n###\n";
  TEST_ASSERT_NULL(
      kernel::font::load_any(kCommented, static_cast<uint32_t>(strlen(kCommented)), g_bits, made));
  TEST_ASSERT_EQUAL_UINT8(3, made.width);
  const char* kBdf = "STARTFONT 2.1\nFONTBOUNDINGBOX 3 5 0 0\nSTARTCHAR zero\nENCODING 48\n"
                     "BITMAP\nE0\nA0\nE0\nA0\nE0\nENDCHAR\nENDFONT\n";
  TEST_ASSERT_NULL(kernel::font::load_any(kBdf, static_cast<uint32_t>(strlen(kBdf)), g_bits, made));
  TEST_ASSERT_EQUAL_UINT8(3, made.width);
  const char* kBdfCommented = "COMMENT a bdf, with a comment first\n" "STARTFONT 2.1\n"
                              "FONTBOUNDINGBOX 3 5 0 0\nSTARTCHAR zero\nENCODING 48\n"
                              "BITMAP\nE0\nA0\nE0\nA0\nE0\nENDCHAR\nENDFONT\n";
  TEST_ASSERT_NULL(
      kernel::font::load_any(kBdfCommented, static_cast<uint32_t>(strlen(kBdfCommented)), g_bits, made));
  TEST_ASSERT_EQUAL_UINT8(3, made.width);
  TEST_ASSERT_NOT_NULL(kernel::font::load_any("hello", 5, g_bits, made));
}

// **A PCF and the BDF it was compiled from give the same face**, byte for byte: that is what "compiled" is
// supposed to mean, and it is the one thing a reader of a binary format has to get exactly right (docs/30).
// The vector is the file the app `font-pcf` ships with, written by `bdftopcf` from the BDF of `font-bdf`.
static void test_a_pcf_becomes_the_same_face_as_its_bdf() {
  uint32_t pcf_length = 0;
  const char* pcf = pcf_file(pcf_length);
  TEST_ASSERT_GREATER_THAN(8, pcf_length);
  kernel::font::Face from_pcf = kernel::font::system_face();
  TEST_ASSERT_NULL_MESSAGE(kernel::font::load_pcf(pcf, pcf_length, g_bits, from_pcf), "a pcf of six glyphs");
  TEST_ASSERT_EQUAL_UINT8(8, from_pcf.width);
  TEST_ASSERT_EQUAL_UINT8(8, from_pcf.height);
  TEST_ASSERT_EQUAL_UINT8('A', from_pcf.first);
  TEST_ASSERT_EQUAL_UINT8(6, from_pcf.count);
  TEST_ASSERT_EQUAL_UINT8(1, from_pcf.gap);
  TEST_ASSERT_EQUAL_UINT16(53, kernel::font::width(from_pcf, "ABCDEF", 1));  // 6 * 8 + 5 gaps

  static char bdf[4096];
  const uint32_t bdf_length = read_file("data/apps/font-bdf/res/abcdef-8x8.bdf", bdf, sizeof(bdf));
  TEST_ASSERT_GREATER_THAN(0, bdf_length);
  static uint8_t bdf_bits[kernel::font::kFaceBytes];
  kernel::font::Face from_bdf = kernel::font::system_face();
  TEST_ASSERT_NULL(kernel::font::load_bdf(bdf, bdf_length, bdf_bits, from_bdf));
  TEST_ASSERT_EQUAL_UINT8(from_bdf.width, from_pcf.width);
  TEST_ASSERT_EQUAL_UINT8(from_bdf.height, from_pcf.height);
  TEST_ASSERT_EQUAL_UINT8(from_bdf.first, from_pcf.first);
  TEST_ASSERT_EQUAL_UINT8(from_bdf.count, from_pcf.count);
  for (uint32_t i = 0; i < from_pcf.count * kernel::font::kMaxGlyphHeight; ++i) {
    TEST_ASSERT_EQUAL_UINT8(bdf_bits[i], g_bits[i]);
  }
  // And the loader an app calls takes the file without being told what it is: a PCF opens with four bytes
  // that are not a line of text, so nothing can mistake it for a bdf (docs/30).
  kernel::font::Face any = kernel::font::system_face();
  TEST_ASSERT_NULL(kernel::font::load_any(pcf, pcf_length, g_bits, any));
  TEST_ASSERT_EQUAL_UINT8('A', any.first);
}

// **What is not a pcf, and what is one this reader does not know, is refused with the reason** (docs/30).
static void test_a_pcf_that_is_not_one_says_why() {
  uint32_t length = 0;
  const char* pcf = pcf_file(length);
  TEST_ASSERT_GREATER_THAN(8, length);
  kernel::font::Face face = kernel::font::system_face();
  TEST_ASSERT_NOT_NULL_MESSAGE(kernel::font::load_pcf("hello", 5, g_bits, face), "not a pcf");
  static char scratch[4096];
  // **A directory that is cut** is not read table by table to see how far it goes.
  memcpy(scratch, pcf, length);
  TEST_ASSERT_NOT_NULL(kernel::font::load_pcf(scratch, 10, g_bits, face));
  // **A table that starts past the end of the file.**
  memcpy(scratch, pcf, length);
  const int32_t bitmaps = table_of(scratch, 0x08);
  TEST_ASSERT_TRUE(bitmaps > 0);
  scratch[bitmaps + 12] = static_cast<char>(0xFF);
  scratch[bitmaps + 13] = static_cast<char>(0xFF);
  scratch[bitmaps + 14] = static_cast<char>(0xFF);
  scratch[bitmaps + 15] = static_cast<char>(0xFF);
  TEST_ASSERT_NOT_NULL(kernel::font::load_pcf(scratch, length, g_bits, face));
  // **The bits the other way round** would mirror every letter, so the file is refused instead (`0x0e` is
  // most-significant bits first; `0x06` is the same file with the bit the other way).
  memcpy(scratch, pcf, length);
  scratch[bitmaps] = static_cast<char>(0x06);
  TEST_ASSERT_NOT_NULL(kernel::font::load_pcf(scratch, length, g_bits, face));
  // **A two-byte encoding**, which is where the codes stop being ascii.
  const int32_t encodings = table_of(pcf, 0x20);
  TEST_ASSERT_TRUE(encodings > 0);
  memcpy(scratch, pcf, length);
  scratch[encodings + 9] = static_cast<char>(0x01);  // `min_byte1`, most significant byte first
  TEST_ASSERT_NOT_NULL(kernel::font::load_pcf(scratch, length, g_bits, face));
  // **A code that points past the glyphs**: the first entry of the encoding table says glyph 200 of six.
  memcpy(scratch, pcf, length);
  scratch[encodings + 14] = static_cast<char>(0x00);
  scratch[encodings + 15] = static_cast<char>(200);
  TEST_ASSERT_NOT_NULL(kernel::font::load_pcf(scratch, length, g_bits, face));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_font_file_becomes_a_face);
  RUN_TEST(test_a_line_is_as_wide_as_its_glyphs);
  RUN_TEST(test_a_text_is_runs_cut_at_the_label);
  RUN_TEST(test_a_file_that_is_not_a_face_says_why);
  RUN_TEST(test_a_bdf_becomes_a_face);
  RUN_TEST(test_a_bdf_that_is_not_a_face_says_why);
  RUN_TEST(test_a_pcf_becomes_the_same_face_as_its_bdf);
  RUN_TEST(test_a_pcf_that_is_not_one_says_why);
  return UNITY_END();
}
