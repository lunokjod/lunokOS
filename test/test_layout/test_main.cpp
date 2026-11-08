// **One page of a book, composed** (docs/46, A3): the words with their place, the air of a block, the inset of a
// quotation, the middle of a caption, the grid of a table and the band of a floated picture.
//
// **A face of the test rules every letter six pixels and every line ten**, times the step. That is the whole
// reason a page is composed as a list of items and not painted: with numbers like these the arithmetic of a page
// can be done with a pencil, and what a case asserts is where a word lands and not how a glyph looks.
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "epub/page.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// The face of the test: a letter is six pixels, a space is six, and a line is ten, all of them times the step.
int32_t ruler_width(void* ctx, uint8_t face, uint8_t scale, const char* text) {
  (void)ctx;
  (void)face;
  int32_t len = 0;
  while (text[len] != 0) ++len;
  return len * 6 * scale;
}

int32_t ruler_line(void* ctx, uint8_t face, uint8_t scale) {
  (void)ctx;
  (void)face;
  return 10 * scale;
}

// The metrics a page asks of its machine, which are the face of the test for every case.
const epub::Metrics& metrics() {
  static epub::Metrics built;
  built.ctx = nullptr;
  built.text_width = ruler_width;
  built.line_height = ruler_line;
  return built;
}

// **The step of the page of these cases is one** (see the top of the file): `epub::Style` keeps the steps the
// reader of the paper wants —a body of four, a title of five and a note of three— and every case here was
// written with a pencil on a step of one, where a letter is six pixels and a line ten. The style is said once
// here and passed everywhere instead of being left to a default that belongs to the reader: eight cases were
// asking for a page of step one and being given a page of step four (found red on the 8/11).
const epub::Style& step_one() {
  static epub::Style built;
  built.body_scale = 1;
  built.title_scale = 2;
  built.small_scale = 1;
  built.indent = 24;
  return built;
}

// A picture of the test: it asks for a size and the box of the page cuts it, which is what a book's picture does
// when it is bigger than the paper.
struct Picture {
  int32_t w;
  int32_t h;
};

bool picture_measure(void* ctx, const char* href, int32_t max_w, int32_t max_h, int32_t& w, int32_t& h) {
  (void)href;
  const Picture* size = static_cast<const Picture*>(ctx);

  w = (size->w > max_w) ? max_w : size->w;
  h = (size->h > max_h) ? max_h : size->h;

  return w > 0 && h > 0;
}

// **The sheet of a page**: the list of items and the pool of words, which in a running system are the book's and
// the memory it reserved (docs/46, A7).
struct Block {
  static const int32_t kItems = 64;
  static const int32_t kWords = 1024;

  epub::Item items[kItems];
  char words[kWords];
  epub::Sheet sheet;
  epub::Result result;

  Block() {
    sheet.items = items;
    sheet.items_cap = kItems;
    sheet.words = words;
    sheet.words_cap = kWords;
  }
};

const char* text_of(const Block& block, int32_t index) { return block.words + block.items[index].text; }

// Lays one page out of the chapter, from the first byte.
void lay_out(const char* chapter, int32_t width, int32_t height, Block& block,
             const epub::Style& style = step_one(), const epub::Pictures* pictures = nullptr,
             int32_t from = 0) {
  epub::Tokens tokens(chapter, static_cast<int32_t>(strlen(chapter)), from);
  const bool composed = epub::layout(tokens, width, height, style, metrics(), pictures, block.sheet,
                                     block.result);
  if (!composed) {
    TEST_FAIL_MESSAGE("the page could not be composed");
  }
}

}  // namespace

// **A page is the words of a paragraph with a place each**, and the first one opens the block (docs/46, A3).
static void test_a_page_is_words_with_a_place() {
  Block block;
  lay_out("<p>one two</p>", 200, 200, block);

  TEST_ASSERT_EQUAL_INT16(2, block.result.count);
  TEST_ASSERT_EQUAL_STRING("one", text_of(block, 0));
  TEST_ASSERT_EQUAL_INT16(0, block.items[0].x);
  TEST_ASSERT_EQUAL_INT16(0, block.items[0].y);
  // `one` is three letters of six pixels and the space is another six: the second word starts at twenty-four.
  TEST_ASSERT_EQUAL_STRING("two", text_of(block, 1));
  TEST_ASSERT_EQUAL_INT16(24, block.items[1].x);
  TEST_ASSERT_EQUAL_INT16(0, block.items[1].y);
  // And the chapter is over: a page that ended says so, and there is no next byte.
  TEST_ASSERT_TRUE(block.result.ended);
  TEST_ASSERT_EQUAL_INT32(0, block.result.next);
  TEST_ASSERT_EQUAL_INT32(10, block.result.used);
  TEST_ASSERT_EQUAL_UINT8(epub::kFaceBody, block.items[0].face);
  TEST_ASSERT_EQUAL_UINT8(1, block.items[0].scale);
}

// **A line wraps where the word does not fit**, and the pen goes down one line (docs/46, A3).
static void test_a_line_wraps_where_the_word_does_not_fit() {
  Block block;
  lay_out("<p>aa bb cc</p>", 40, 100, block);

  TEST_ASSERT_EQUAL_INT16(3, block.result.count);
  // `aa bb` is twelve, six and twelve: thirty of forty.
  TEST_ASSERT_EQUAL_INT16(0, block.items[0].x);
  TEST_ASSERT_EQUAL_INT16(18, block.items[1].x);
  // The third word needs six more and would end at forty-eight, so it opens the second line.
  TEST_ASSERT_EQUAL_INT16(0, block.items[2].x);
  TEST_ASSERT_EQUAL_INT16(10, block.items[2].y);
  TEST_ASSERT_EQUAL_INT32(20, block.result.used);
}

// **The page says the byte of the word that did not fit**, and a reader that starts there goes on where the last
// page stopped: that is the position of a book (docs/46, §1).
static void test_the_page_ends_at_the_byte_of_the_word_that_did_not_fit() {
  const char* chapter = "<p>aa bb cc dd ee</p>";

  Block first;
  lay_out(chapter, 40, 25, first);
  TEST_ASSERT_EQUAL_INT16(4, first.result.count);
  TEST_ASSERT_FALSE(first.result.ended);
  // `ee` is at byte fifteen: `<p>` is three and three pairs of a word and a space are twelve more. The case
  // said sixteen and the comment above it said three and twelve, so the number was the one that was wrong
  // (found red on the 8/11).
  TEST_ASSERT_EQUAL_INT32(15, first.result.next);
  TEST_ASSERT_EQUAL_INT32(20, first.result.used);

  Block second;
  lay_out(chapter, 40, 25, second, step_one(), nullptr, first.result.next);
  TEST_ASSERT_EQUAL_INT16(1, second.result.count);
  TEST_ASSERT_EQUAL_STRING("ee", text_of(second, 0));
  TEST_ASSERT_EQUAL_INT16(0, second.items[0].y);
  TEST_ASSERT_TRUE(second.result.ended);
}

// **A title is its own face and its own size, and a block opens with air** (docs/46, A3), and the air of a block
// is paid once: the `</h1>` of before does not open a block of its own.
static void test_a_title_is_taller_and_opens_with_air() {
  Block block;
  lay_out("<h1>Titol</h1><p>text</p>", 200, 200, block, step_one());

  TEST_ASSERT_EQUAL_INT16(2, block.result.count);
  TEST_ASSERT_EQUAL_STRING("Titol", text_of(block, 0));
  TEST_ASSERT_EQUAL_UINT8(epub::kFaceTitle, block.items[0].face);
  TEST_ASSERT_EQUAL_UINT8(2, block.items[0].scale);
  TEST_ASSERT_EQUAL_INT16(0, block.items[0].y);
  // A line of the title is twenty pixels, and the paragraph opens with half a line of the body: twenty-five.
  TEST_ASSERT_EQUAL_STRING("text", text_of(block, 1));
  TEST_ASSERT_EQUAL_UINT8(epub::kFaceBody, block.items[1].face);
  TEST_ASSERT_EQUAL_INT16(25, block.items[1].y);
  TEST_ASSERT_EQUAL_INT32(35, block.result.used);
}

// **A quotation is inset and a caption goes in the middle**, which are the two blocks of a book that do not
// start at the edge (docs/46, A3).
static void test_a_quotation_is_inset_and_a_caption_is_centred() {
  Block quoted;
  lay_out("<blockquote><p>dit</p></blockquote>", 200, 200, quoted);
  TEST_ASSERT_EQUAL_INT16(24, quoted.items[0].x);

  Block caption;
  lay_out("<figure><figcaption>cap</figcaption></figure>", 200, 200, caption);
  TEST_ASSERT_EQUAL_UINT8(epub::kFaceSmall, caption.items[0].face);
  // `cap` is eighteen pixels: the middle of the box that is left between the inset and the edge is at 103.
  TEST_ASSERT_EQUAL_INT16(103, caption.items[0].x);
}

// **A table is a grid of columns and not a list of paragraphs** (docs/46, A3): two cells of a row share the line,
// and the row of a header is underlined.
static void test_a_table_is_a_grid_of_columns() {
  Block grid;
  lay_out("<table><tr><td>a</td><td>b</td></tr></table>", 120, 200, grid);
  TEST_ASSERT_EQUAL_INT16(2, grid.result.count);
  TEST_ASSERT_EQUAL_STRING("a", text_of(grid, 0));
  TEST_ASSERT_EQUAL_INT16(0, grid.items[0].x);
  // The second column starts at half the page, and on the same line.
  TEST_ASSERT_EQUAL_STRING("b", text_of(grid, 1));
  TEST_ASSERT_EQUAL_INT16(60, grid.items[1].x);
  TEST_ASSERT_EQUAL_INT16(0, grid.items[1].y);

  Block header;
  lay_out("<table><tr><th>a</th><th>b</th></tr></table>", 120, 200, header);
  TEST_ASSERT_EQUAL_INT16(3, header.result.count);
  // The header is heavier, and the rule under it is the only frame a table of a book has.
  TEST_ASSERT_EQUAL_UINT8(epub::kInkBold, static_cast<uint8_t>(header.items[0].ink & epub::kInkBold));
  TEST_ASSERT_EQUAL_UINT8(epub::Item::kRule, header.items[2].kind);
  TEST_ASSERT_EQUAL_INT16(0, header.items[2].x);
  TEST_ASSERT_EQUAL_INT16(9, header.items[2].y);
  TEST_ASSERT_EQUAL_INT16(120, header.items[2].w);
}

// **A floated picture takes a band and the words go beside it** (docs/46, A3), which is the one thing of a page
// that is not a column of lines.
static void test_a_floated_picture_leaves_a_band_for_the_words() {
  Picture size = {30, 40};
  epub::Pictures pictures;
  pictures.ctx = &size;
  pictures.measure = picture_measure;

  Block block;
  lay_out("<p>a<img src=\"x.png\" style=\"float:left\"/>bb</p>", 200, 200, block, step_one(), &pictures);

  TEST_ASSERT_EQUAL_INT16(3, block.result.count);
  TEST_ASSERT_TRUE_MESSAGE(block.result.has_picture, "the page says it holds a picture");
  TEST_ASSERT_EQUAL_UINT8(epub::Item::kPicture, block.items[1].kind);
  TEST_ASSERT_EQUAL_STRING("x.png", text_of(block, 1));
  TEST_ASSERT_EQUAL_INT16(0, block.items[1].x);
  TEST_ASSERT_EQUAL_INT16(30, block.items[1].w);
  TEST_ASSERT_EQUAL_INT16(40, block.items[1].h);
  // The picture closes the line it interrupts and the words go on beside it: thirty of picture and ten of air.
  TEST_ASSERT_EQUAL_STRING("bb", text_of(block, 2));
  TEST_ASSERT_EQUAL_INT16(40, block.items[2].x);
}

// **A picture is not cut in half**: it opens the next page, which is what a page of paper does with a plate
// (docs/46, A3).
static void test_a_picture_that_does_not_fit_opens_the_next_page() {
  Picture size = {60, 40};
  epub::Pictures pictures;
  pictures.ctx = &size;
  pictures.measure = picture_measure;

  // Three lines of ten and a picture of forty in a page of sixty: the picture lands at thirty and does not fit.
  Block block;
  lay_out("<p>aa bb cc dd ee ff gg</p><img src=\"plate.png\"/>", 40, 60, block, step_one(), &pictures);

  TEST_ASSERT_FALSE(block.result.ended);
  TEST_ASSERT_EQUAL_INT16(7, block.result.count);
  // The byte of the picture is where the next page starts.
  const char* chapter = "<p>aa bb cc dd ee ff gg</p><img src=\"plate.png\"/>";
  const int32_t picture_at = static_cast<int32_t>(strstr(chapter, "<img") - chapter);
  TEST_ASSERT_EQUAL_INT32(picture_at, block.result.next);
  TEST_ASSERT_FALSE(block.result.has_picture);
}

// **The ink of a word travels to its place** (docs/46, A4): a bold is a bit of the item and a `sup` is a step off
// the baseline of the line.
static void test_the_ink_of_a_word_travels_to_its_place() {
  Block block;
  lay_out("<p><b>bo</b> a<sup>2</sup></p>", 200, 200, block);

  TEST_ASSERT_EQUAL_INT16(3, block.result.count);
  TEST_ASSERT_EQUAL_UINT8(epub::kInkBold, static_cast<uint8_t>(block.items[0].ink & epub::kInkBold));
  TEST_ASSERT_EQUAL_UINT8(0, block.items[1].ink);
  // A third of a line of ten is three, and the line does not grow for it.
  TEST_ASSERT_EQUAL_UINT8(epub::kInkRiseUp, static_cast<uint8_t>(block.items[2].ink & epub::kInkRiseMask));
  TEST_ASSERT_EQUAL_INT16(-3, block.items[2].y);
  TEST_ASSERT_EQUAL_INT32(10, block.result.used);
}

// **A word that is wider than the box is placed anyway**, and the page after it is the one that wraps: a face
// that says a word does not fit is not a reason to end a page without a word on it (docs/46, A3).
static void test_a_word_wider_than_the_box_is_placed_anyway() {
  Block block;
  lay_out("<p>abcdefgh ij</p>", 16, 100, block);

  TEST_ASSERT_EQUAL_INT16(2, block.result.count);
  TEST_ASSERT_EQUAL_INT16(0, block.items[0].x);
  // Forty-eight pixels of word in a box of sixteen: it hangs out of the page and the next word goes below.
  TEST_ASSERT_EQUAL_INT16(0, block.items[1].x);
  TEST_ASSERT_EQUAL_INT16(10, block.items[1].y);
}

// **A pool that is full ends the page at the word it could not hold**, and says its byte: the caller sized the
// block and the page is honest about it (docs/46, A7).
static void test_a_pool_that_is_full_ends_the_page_at_a_word() {
  Block block;
  block.sheet.words_cap = 6;  // `aaa` and its zero are four, and the next word needs four more

  const char* chapter = "<p>aaa bbb</p>";
  epub::Tokens tokens(chapter, static_cast<int32_t>(strlen(chapter)));
  TEST_ASSERT_TRUE(epub::layout(tokens, 200, 200, step_one(), metrics(), nullptr, block.sheet,
                                block.result));

  TEST_ASSERT_EQUAL_INT16(1, block.result.count);
  TEST_ASSERT_FALSE(block.result.ended);
  TEST_ASSERT_EQUAL_INT32(7, block.result.next);
}

// **A sheet that cannot hold a page says so**, and so does a box of nothing: the caller has nothing to show and
// must not paint (docs/46, A3).
static void test_a_sheet_with_no_room_is_refused() {
  Block block;
  block.sheet.items_cap = 0;

  epub::Tokens tokens("<p>a</p>", 8);
  TEST_ASSERT_FALSE(epub::layout(tokens, 200, 200, step_one(), metrics(), nullptr, block.sheet,
                                 block.result));

  epub::Tokens small("<p>a</p>", 8);
  TEST_ASSERT_FALSE(epub::layout(small, 15, 200, step_one(), metrics(), nullptr, block.sheet,
                                 block.result));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_page_is_words_with_a_place);
  RUN_TEST(test_a_line_wraps_where_the_word_does_not_fit);
  RUN_TEST(test_the_page_ends_at_the_byte_of_the_word_that_did_not_fit);
  RUN_TEST(test_a_title_is_taller_and_opens_with_air);
  RUN_TEST(test_a_quotation_is_inset_and_a_caption_is_centred);
  RUN_TEST(test_a_table_is_a_grid_of_columns);
  RUN_TEST(test_a_floated_picture_leaves_a_band_for_the_words);
  RUN_TEST(test_a_picture_that_does_not_fit_opens_the_next_page);
  RUN_TEST(test_the_ink_of_a_word_travels_to_its_place);
  RUN_TEST(test_a_word_wider_than_the_box_is_placed_anyway);
  RUN_TEST(test_a_pool_that_is_full_ends_the_page_at_a_word);
  RUN_TEST(test_a_sheet_with_no_room_is_refused);
  return UNITY_END();
}
