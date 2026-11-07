// **The XHTML of a chapter, word by word** (docs/46, A2): the byte of every word, the block that opens it,
// the entities, the pictures with their side, and the grid of a table.
//
// The chapters of these cases are written here, in the test, and they are short on purpose: what is being
// tried is the walk of the reader —what comes out, in which order, and at which byte— and not a real book.
// **The one thing every case looks at with a magnifying glass is the offset**, because the position of a
// reader is a byte inside a chapter and not a page number: a word at the wrong byte is a page that starts in
// the middle of a sentence.
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "epub/html.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// **The tokens of a chapter, copied out of the reader.** A token points inside the reader and is only good
// until the next call to `next()`, so a case that wants to look at three of them has to keep its own copies.
struct Reading {
  static const int32_t kMax = 40;

  epub::Token::Kind kind[kMax];
  char text[kMax][64];
  int32_t text_len[kMax];
  char block[kMax][16];
  char href[kMax][80];
  int32_t at[kMax];
  int32_t after[kMax];
  bool bold[kMax];
  bool italic[kMax];
  int8_t rise[kMax];
  bool rule[kMax];
  bool floats[kMax];
  bool right_side[kMax];
  epub::Token::Cell cell[kMax];
  bool header[kMax];
  int32_t count;
};

void copy_into(char* out, int32_t cap, const char* in, int32_t len) {
  int32_t n = 0;
  for (int32_t i = 0; i < len && n < cap - 1; ++i) out[n++] = in[i];
  out[n] = 0;
}

// Walks the chapter from `from` and keeps every token.
void read_all(const char* chapter, Reading& out, int32_t from = 0) {
  out.count = 0;

  epub::Tokens reader(chapter, static_cast<int32_t>(strlen(chapter)), from);
  epub::Token token;

  while (out.count < Reading::kMax && reader.next(token)) {
    const int32_t i = out.count++;

    out.kind[i] = token.kind;
    out.text_len[i] = token.text_len;
    out.at[i] = token.at;
    out.after[i] = token.after;
    out.bold[i] = token.bold;
    out.italic[i] = token.italic;
    out.rise[i] = token.rise;
    out.rule[i] = token.rule;
    out.floats[i] = token.floats;
    out.right_side[i] = token.right_side;
    out.cell[i] = token.cell;
    out.header[i] = token.header;

    out.text[i][0] = 0;
    out.block[i][0] = 0;
    out.href[i][0] = 0;
    if (token.text != nullptr) copy_into(out.text[i], sizeof(out.text[i]), token.text, token.text_len);
    if (token.block != nullptr) copy_into(out.block[i], sizeof(out.block[i]), token.block, token.block_len);
    if (token.href != nullptr) copy_into(out.href[i], sizeof(out.href[i]), token.href, token.href_len);
  }
}

}  // namespace

// **A paragraph becomes words, each with its byte, and the block opens only the first one** (docs/46, A2).
//
// The block is "what opens the word": a page takes the face and the air before a block from the first word
// of the block, and the rest of the paragraph is in the same one. That is why the second word of a `<p>` has
// no block of its own, and a page that asked every word would start a paragraph before each of them.
static void test_a_paragraph_becomes_words_with_their_byte() {
  Reading reading;
  read_all("<p>Hola mon</p>", reading);

  TEST_ASSERT_EQUAL_INT32(2, reading.count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kWord), static_cast<int>(reading.kind[0]));
  TEST_ASSERT_EQUAL_STRING_LEN("Hola", reading.text[0], reading.text_len[0]);
  TEST_ASSERT_EQUAL_INT32(3, reading.at[0]);
  TEST_ASSERT_EQUAL_STRING("p", reading.block[0]);
  // The second word is in the same paragraph, but it does not open it.
  TEST_ASSERT_EQUAL_STRING_LEN("mon", reading.text[1], reading.text_len[1]);
  TEST_ASSERT_EQUAL_INT32(8, reading.at[1]);
  TEST_ASSERT_EQUAL_STRING("", reading.block[1]);
  // And the reader says false at the end, as many times as it is asked.
  const char* chapter = "<p>Hola mon</p>";
  epub::Tokens reader(chapter, static_cast<int32_t>(strlen(chapter)));
  epub::Token token;
  TEST_ASSERT_TRUE(reader.next(token));
  TEST_ASSERT_TRUE(reader.next(token));
  TEST_ASSERT_FALSE(reader.next(token));
  TEST_ASSERT_FALSE(reader.next(token));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(strlen(chapter)), reader.position());
}

// **Each block announces itself on its first word**: a title is not a paragraph, and a list item is not a
// blockquote (docs/46, A2).
static void test_the_block_is_the_one_that_opens_the_word() {
  Reading reading;
  read_all("<h1>Titol</h1><blockquote><p>Dit</p></blockquote><ul><li>Un</li></ul>", reading);

  TEST_ASSERT_EQUAL_INT32(3, reading.count);
  TEST_ASSERT_EQUAL_STRING("Titol", reading.text[0]);
  TEST_ASSERT_EQUAL_STRING("h1", reading.block[0]);
  TEST_ASSERT_EQUAL_STRING("Dit", reading.text[1]);
  TEST_ASSERT_EQUAL_STRING("p", reading.block[1]);
  TEST_ASSERT_EQUAL_STRING("Un", reading.text[2]);
  TEST_ASSERT_EQUAL_STRING("li", reading.block[2]);
  // And the names are compared through the one function that does it, which is what a page uses.
  const char* chapter = "<h1>Titol</h1>";
  epub::Tokens reader(chapter, static_cast<int32_t>(strlen(chapter)));
  epub::Token token;
  TEST_ASSERT_TRUE(reader.next(token));
  TEST_ASSERT_TRUE(epub::block_is(token, "h1"));
  TEST_ASSERT_FALSE(epub::block_is(token, "h2"));
  TEST_ASSERT_FALSE(epub::block_is(token, ""));
}

// **The entities are the letters of Latin-1**, named and numeric, and a name nobody knows stays as it was
// written (docs/46, A2).
static void test_the_entities_become_the_letters_of_latin1() {
  Reading reading;
  read_all("<p>caf&eacute; &amp; te &#233; &#xE9; &laquo;dit&raquo; &mdash; &bull;</p>", reading);

  TEST_ASSERT_EQUAL_INT32(8, reading.count);
  // One byte per code, which is what the face of this system carries.
  TEST_ASSERT_EQUAL_STRING_LEN("caf\xE9", reading.text[0], reading.text_len[0]);
  TEST_ASSERT_EQUAL_INT32(5, reading.text_len[0]);
  TEST_ASSERT_EQUAL_STRING_LEN("&", reading.text[1], reading.text_len[1]);
  TEST_ASSERT_EQUAL_STRING_LEN("te", reading.text[2], reading.text_len[2]);
  // The same letter written in decimal and in hexadecimal is the same byte.
  TEST_ASSERT_EQUAL_STRING_LEN("\xE9", reading.text[3], reading.text_len[3]);
  TEST_ASSERT_EQUAL_STRING_LEN("\xE9", reading.text[4], reading.text_len[4]);
  TEST_ASSERT_EQUAL_STRING_LEN("\xAB" "dit\xBB", reading.text[5], reading.text_len[5]);
  // The marks Latin-1 cannot carry become the letter that stands for them: a long dash painted as a question
  // mark would be worse than a short one, and the bullet is the middle dot the face has.
  TEST_ASSERT_EQUAL_STRING_LEN("-", reading.text[6], reading.text_len[6]);
  TEST_ASSERT_EQUAL_STRING_LEN("\xB7", reading.text[7], reading.text_len[7]);
  // The soft hyphen paints nothing, so the word is not even broken by it.
  Reading hyphen;
  read_all("<p>co&shy;operar &foo; &#150;</p>", hyphen);
  TEST_ASSERT_EQUAL_INT32(3, hyphen.count);
  TEST_ASSERT_EQUAL_STRING_LEN("cooperar", hyphen.text[0], hyphen.text_len[0]);
  // A name this reader does not know stays as it was written.
  TEST_ASSERT_EQUAL_STRING_LEN("&foo;", hyphen.text[1], hyphen.text_len[1]);
  // **And a number of the C1 range is a byte of Windows-1252**, which is how a book of the nineties writes its
  // apostrophe and its dash: `&#150;` is an en dash and not a question mark (see `kCp1252`).
  TEST_ASSERT_EQUAL_STRING_LEN("-", hyphen.text[2], hyphen.text_len[2]);
  Reading old;
  read_all("<p>don&#146;t &#151; &#147;dit&#148; &#129;</p>", old);
  TEST_ASSERT_EQUAL_INT32(4, old.count);
  TEST_ASSERT_EQUAL_STRING_LEN("don't", old.text[0], old.text_len[0]);
  TEST_ASSERT_EQUAL_STRING_LEN("-", old.text[1], old.text_len[1]);
  TEST_ASSERT_EQUAL_STRING_LEN("\"dit\"", old.text[2], old.text_len[2]);
  // A byte that cp1252 does not use either is not text, and it is the one that says so.
  TEST_ASSERT_EQUAL_STRING_LEN("?", old.text[3], old.text_len[3]);
}

// **The chapter of a book is UTF-8, and the face is Latin-1**: the turning happens here, once, where the
// chapter is already being walked byte by byte (docs/46, D3).
static void test_the_utf8_of_the_file_becomes_one_byte() {
  // `café` and `—` and `…` and `“…”` written in the UTF-8 a chapter really carries.
  Reading reading;
  read_all("<p>caf\xC3\xA9 \xE2\x80\x94 b\xE2\x80\xA6 \xE2\x80\x9C" "dit" "\xE2\x80\x9D</p>", reading);

  TEST_ASSERT_EQUAL_INT32(4, reading.count);
  TEST_ASSERT_EQUAL_STRING_LEN("caf\xE9", reading.text[0], reading.text_len[0]);
  TEST_ASSERT_EQUAL_INT32(4, reading.text_len[0]);
  TEST_ASSERT_EQUAL_STRING_LEN("-", reading.text[1], reading.text_len[1]);
  TEST_ASSERT_EQUAL_STRING_LEN("b.", reading.text[2], reading.text_len[2]);
  TEST_ASSERT_EQUAL_STRING_LEN("\"dit\"", reading.text[3], reading.text_len[3]);
  // **A byte that is not UTF-8 is a chapter already written in Latin-1** (some are, mislabelled), and a byte
  // of Latin-1 is a code of the face as it is: it is kept, and it is not a question mark.
  Reading latin1;
  read_all("<p>caf\xE9</p>", latin1);
  TEST_ASSERT_EQUAL_INT32(1, latin1.count);
  TEST_ASSERT_EQUAL_STRING_LEN("caf\xE9", latin1.text[0], latin1.text_len[0]);
  // A code the faces of this system cannot carry at all is the one that says so.
  Reading far;
  read_all("<p>a\xF0\x9F\x98\x80" "b</p>", far);  // a smile, which is not a letter
  TEST_ASSERT_EQUAL_INT32(1, far.count);
  TEST_ASSERT_EQUAL_STRING_LEN("a?b", far.text[0], far.text_len[0]);
}

// **A book carries a page of JavaScript and a page of CSS in front of every chapter**, and none of it is a
// paragraph; the same of the `<head>` and of a comment (docs/46, A2).
static void test_script_style_head_and_comments_are_skipped_whole() {
  const char* chapter =
      "<html><head><title>Titol</title><style>p { color: red; }</style></head>"
      "<body><!-- a comment with <p>words</p> inside -->"
      "<script>var x = '<p>no</p>';</script><p>Si</p></body></html>";

  Reading reading;
  read_all(chapter, reading);

  TEST_ASSERT_EQUAL_INT32(1, reading.count);
  TEST_ASSERT_EQUAL_STRING("Si", reading.text[0]);
  TEST_ASSERT_EQUAL_STRING("p", reading.block[0]);
  // And the byte is the one of the paragraph, not of the script: what a page saves is where the reading goes
  // on, and that is after everything that painted nothing.
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(strstr(chapter, "<p>Si") - chapter) + 3, reading.at[0]);
}

// **A line end and a rule are tokens of their own**, and the word that was half read goes before them: two
// `<br>` in a row are two line ends, and a rule is not a paragraph (docs/46, A2).
static void test_a_line_end_and_a_rule_are_tokens_of_their_own() {
  Reading reading;
  read_all("<p>un<br>dos<br><br>tres</p><hr><p>quatre</p>", reading);

  TEST_ASSERT_EQUAL_INT32(8, reading.count);
  TEST_ASSERT_EQUAL_STRING("un", reading.text[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kBreak), static_cast<int>(reading.kind[1]));
  TEST_ASSERT_FALSE(reading.rule[1]);
  TEST_ASSERT_EQUAL_STRING("dos", reading.text[2]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kBreak), static_cast<int>(reading.kind[3]));
  // Two in a row are two, which is what a flag instead of a token would have lost.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kBreak), static_cast<int>(reading.kind[4]));
  TEST_ASSERT_EQUAL_STRING("tres", reading.text[5]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kBreak), static_cast<int>(reading.kind[6]));
  TEST_ASSERT_TRUE_MESSAGE(reading.rule[6], "an <hr> is a rule and not a line end");
  // And the paragraph after the rule goes on: a rule is not a block that eats the next one.
  TEST_ASSERT_EQUAL_STRING("quatre", reading.text[7]);
  TEST_ASSERT_EQUAL_STRING("p", reading.block[7]);
}

// **A picture is a token with the entry inside the zip and the side it floats to**, and the three ways a book
// says that side are read: the style, the `align` of EPUB 2 and the class (docs/46, A2).
static void test_a_picture_knows_where_it_is_and_which_side() {
  Reading reading;
  read_all("<p>a<img src=\"img/u1.png\" style=\"float: right\"/>b<img src=\"u2.png#x\" align=\"left\"/>"
           "<img src=\"u3.png\" class=\"float-left\"/><img src=\"u4.png\"/><img alt=\"no src\"/></p>",
           reading);

  TEST_ASSERT_EQUAL_INT32(6, reading.count);
  // The word that was half read goes before the picture it interrupts.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kWord), static_cast<int>(reading.kind[0]));
  TEST_ASSERT_EQUAL_STRING("a", reading.text[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kPicture), static_cast<int>(reading.kind[1]));
  TEST_ASSERT_EQUAL_STRING("img/u1.png", reading.href[1]);
  TEST_ASSERT_TRUE(reading.floats[1]);
  TEST_ASSERT_TRUE(reading.right_side[1]);
  TEST_ASSERT_EQUAL_INT32(4, reading.at[1]);
  TEST_ASSERT_EQUAL_STRING("b", reading.text[2]);
  // A fragment of an `href` is not part of the entry.
  TEST_ASSERT_EQUAL_STRING("u2.png", reading.href[3]);
  TEST_ASSERT_TRUE(reading.floats[3]);
  TEST_ASSERT_FALSE(reading.right_side[3]);
  TEST_ASSERT_EQUAL_STRING("u3.png", reading.href[4]);
  TEST_ASSERT_TRUE(reading.floats[4]);
  TEST_ASSERT_FALSE(reading.right_side[4]);
  // A picture with no side is a picture for the flow, and one with no source is not a picture at all.
  TEST_ASSERT_EQUAL_STRING("u4.png", reading.href[5]);
  TEST_ASSERT_FALSE(reading.floats[5]);
  // `after` is where the tag ends, which is what a page uses to go on after a picture it could not draw.
  const char* chapter = "<p>a<img src=\"u1.png\"/>b</p>";
  epub::Tokens reader(chapter, static_cast<int32_t>(strlen(chapter)));
  epub::Token token;
  TEST_ASSERT_TRUE(reader.next(token));
  TEST_ASSERT_TRUE(reader.next(token));
  TEST_ASSERT_EQUAL_INT32(23, token.after);
}

// **The ink of a line is counted and not flagged**: a book nests `<b>` and `<i>`, and a tag in the middle of
// a word belongs to the next one (docs/46, A2).
static void test_the_ink_inside_a_line_is_counted_not_flagged() {
  Reading reading;
  read_all("<p>ca<b>sa</b> <b>ne<i>ta</i> fort</b> te<sup>2</sup> a<sub>1</sub></p>", reading);

  TEST_ASSERT_EQUAL_INT32(9, reading.count);
  TEST_ASSERT_EQUAL_STRING("ca", reading.text[0]);
  TEST_ASSERT_FALSE(reading.bold[0]);
  TEST_ASSERT_EQUAL_STRING("sa", reading.text[1]);
  TEST_ASSERT_TRUE(reading.bold[1]);
  // `<p>` is three bytes and `ca` two, so the bold word starts after the `<b>`.
  TEST_ASSERT_EQUAL_INT32(8, reading.at[1]);
  // Inside a `<b>` and an `<i>`: the closing of the italic does not end the bold it is nested in.
  TEST_ASSERT_EQUAL_STRING("ne", reading.text[2]);
  TEST_ASSERT_TRUE(reading.bold[2]);
  TEST_ASSERT_FALSE(reading.italic[2]);
  TEST_ASSERT_EQUAL_STRING("ta", reading.text[3]);
  TEST_ASSERT_TRUE(reading.bold[3]);
  TEST_ASSERT_TRUE(reading.italic[3]);
  TEST_ASSERT_EQUAL_STRING("fort", reading.text[4]);
  TEST_ASSERT_TRUE(reading.bold[4]);
  TEST_ASSERT_FALSE_MESSAGE(reading.italic[4], "the italic ends where its tag ends");
  // And after the bold, the paragraph is plain again: the counters went back to zero.
  TEST_ASSERT_EQUAL_STRING("te", reading.text[5]);
  TEST_ASSERT_FALSE(reading.bold[5]);
  TEST_ASSERT_EQUAL_STRING_LEN("2", reading.text[6], 1);
  TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(reading.rise[6]));
  TEST_ASSERT_EQUAL_STRING("a", reading.text[7]);
  TEST_ASSERT_EQUAL_STRING_LEN("1", reading.text[8], 1);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(reading.rise[8]));
}

// **A `<pre>` is where the line ends are the author's**: a poem or a block of code lives or dies by it, and
// everywhere else a newline is just a space (docs/46, A2).
static void test_a_pre_keeps_the_line_ends_of_the_author() {
  Reading reading;
  read_all("<pre>one\ntwo   three</pre><p>one\ntwo</p>", reading);

  TEST_ASSERT_EQUAL_INT32(6, reading.count);
  TEST_ASSERT_EQUAL_STRING("one", reading.text[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kBreak), static_cast<int>(reading.kind[1]));
  TEST_ASSERT_FALSE(reading.rule[1]);
  TEST_ASSERT_EQUAL_STRING("two", reading.text[2]);
  // The spaces still collapse: a line of a poem is the words that are on it.
  TEST_ASSERT_EQUAL_STRING("three", reading.text[3]);
  // Outside a `<pre>` the same newline is one space between two words and nothing else: the same text after
  // the paragraph is two words and no line end.
  TEST_ASSERT_EQUAL_STRING("one", reading.text[4]);
  TEST_ASSERT_EQUAL_STRING("two", reading.text[5]);
  Reading outside;
  read_all("<p>one\ntwo</p>", outside);
  TEST_ASSERT_EQUAL_INT32(2, outside.count);
}

// **The grid of a table comes in marks**, because two cells one after the other are the same block: a page
// cannot tell where one ends and the next begins without them (docs/46, A2).
static void test_the_grid_of_a_table_comes_in_marks() {
  Reading reading;
  read_all("<table><tr><th>Un</th><td>Dos</td></tr></table>", reading);

  TEST_ASSERT_EQUAL_INT32(8, reading.count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kCell), static_cast<int>(reading.kind[0]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kCellOpen), static_cast<int>(reading.cell[0]));
  TEST_ASSERT_TRUE(reading.header[0]);
  TEST_ASSERT_EQUAL_STRING("Un", reading.text[1]);
  TEST_ASSERT_EQUAL_STRING("th", reading.block[1]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kCellClose), static_cast<int>(reading.cell[2]));
  // **The second cell opens before it closes the first**, which is the reason the marks are a queue.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kCellOpen), static_cast<int>(reading.cell[3]));
  TEST_ASSERT_FALSE(reading.header[3]);
  TEST_ASSERT_EQUAL_STRING("Dos", reading.text[4]);
  TEST_ASSERT_EQUAL_STRING("td", reading.block[4]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kCellClose), static_cast<int>(reading.cell[5]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kRowClose), static_cast<int>(reading.cell[6]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub::Token::kTableClose), static_cast<int>(reading.cell[7]));
}

// **A word longer than the buffer is cut and not lost**: a book written badly does not make a page disappear,
// and the words after it still arrive (docs/46, A2).
static void test_a_word_longer_than_the_buffer_is_cut() {
  char chapter[512];
  int32_t n = 0;

  const char* head = "<p>";
  for (const char* p = head; *p != 0; ++p) chapter[n++] = *p;
  for (int32_t i = 0; i < 200; ++i) chapter[n++] = 'x';
  const char* tail = " end</p>";
  for (const char* p = tail; *p != 0; ++p) chapter[n++] = *p;
  chapter[n] = 0;

  Reading reading;
  read_all(chapter, reading);

  TEST_ASSERT_EQUAL_INT32(2, reading.count);
  TEST_ASSERT_EQUAL_INT32(epub::Tokens::kMaxWord - 1, reading.text_len[0]);
  TEST_ASSERT_EQUAL_STRING("end", reading.text[1]);
}

// **A page starts where the one before it ended**: the reader accepts the byte, and a byte in the middle of a
// word is the middle of that word (docs/46, A1).
static void test_reading_from_a_byte_starts_there() {
  Reading reading;
  read_all("<p>Hola mon</p>", reading, 5);

  TEST_ASSERT_EQUAL_INT32(2, reading.count);
  TEST_ASSERT_EQUAL_STRING_LEN("la", reading.text[0], 2);
  TEST_ASSERT_EQUAL_INT32(5, reading.at[0]);
  TEST_ASSERT_EQUAL_STRING("mon", reading.text[1]);

  // And a byte past the end is the end, a negative one is the beginning, and neither reads anything it was
  // not given.
  Reading past;
  read_all("<p>Hola</p>", past, 99);
  TEST_ASSERT_EQUAL_INT32(0, past.count);
  Reading negated;
  read_all("<p>Hola</p>", negated, -5);
  TEST_ASSERT_EQUAL_INT32(1, negated.count);
  TEST_ASSERT_EQUAL_STRING("Hola", negated.text[0]);
  TEST_ASSERT_EQUAL_INT32(3, negated.at[0]);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_paragraph_becomes_words_with_their_byte);
  RUN_TEST(test_the_block_is_the_one_that_opens_the_word);
  RUN_TEST(test_the_entities_become_the_letters_of_latin1);
  RUN_TEST(test_the_utf8_of_the_file_becomes_one_byte);
  RUN_TEST(test_script_style_head_and_comments_are_skipped_whole);
  RUN_TEST(test_a_line_end_and_a_rule_are_tokens_of_their_own);
  RUN_TEST(test_a_picture_knows_where_it_is_and_which_side);
  RUN_TEST(test_the_ink_inside_a_line_is_counted_not_flagged);
  RUN_TEST(test_a_pre_keeps_the_line_ends_of_the_author);
  RUN_TEST(test_the_grid_of_a_table_comes_in_marks);
  RUN_TEST(test_a_word_longer_than_the_buffer_is_cut);
  RUN_TEST(test_reading_from_a_byte_starts_there);
  return UNITY_END();
}
