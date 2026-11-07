// **The XHTML of a chapter, word by word and with the byte of each word** (docs/46, A2).
//
// A book is read a page at a time and a page has to start **exactly** where the one before it ended, because
// the position of a reader is a byte inside a chapter and not a page number. So the writer of the pages walks
// the chapter and this file hands it one token at a time, each one with the byte it starts at.
//
// **What a token is**: a word (with the block that opens it, and the ink it is in), a picture (with the entry
// inside the zip and the side it floats to), a line end or a rule (`<br>`, `<hr>`), or one of the four marks
// of the grid of a table. And what is not a token at all: a `<script>`, a `<style>`, a `<head>` and a comment
// are skipped whole, because a book carries a page of JavaScript in front of every chapter and none of it is a
// paragraph.
//
// **The text comes out as ONE BYTE PER CODE**, which is the one decision of this file that the paper project
// did differently. There, a word kept the UTF-8 the book was written in and the layout turned it into a glyph
// when it painted. Here the face of this system carries **Latin-1** —`tools/ttf_latin1.py` cuts it that way—
// and the whole pipeline of text is one byte per character (`ttf::glyph_of(face, uint8_t code)`), so the
// turning happens once, here, where the chapter is already being walked byte by byte:
//
//   - `&eacute;`, `&#233;` and a raw `é` of the file all become the byte `0xE9`;
//   - the punctuation Latin-1 does not have becomes the letter that stands for it —`—` is `-`, `“` is `"`,
//     `…` is `.`, `•` is `·`—, because a dash painted as a question mark is worse than a short dash;
//   - a code the face cannot carry at all is `?`, and that is said once instead of silently.
//
// **Nothing here allocates and nothing here reads a file.** The bytes of the chapter are the caller's —the
// system inflated the entry into the memory it chose— and the buffers of the reader are in the object, which
// is what makes this a piece of C++11 with no board and no filesystem in it, provable on the computer
// (docs/46, A7). `text`, `href` and `block` point inside this object and are valid **until the next call to
// `next()`**: whoever keeps one copies it.
#ifndef LUNOK_EPUB_HTML_H
#define LUNOK_EPUB_HTML_H

#include <stdint.h>

namespace epub {

// **What came out of the chapter**, and the four marks a table is made of.
struct Token {
  enum Kind : uint8_t {
    kWord = 0,  // a word: `text`, `block`, `bold`, `italic`, `rise`
    kPicture,   // an image: `href`, `floats`, `right_side`
    kBreak,     // `<br>` (a line ends) or `<hr>` (`rule`)
    kCell,      // one of the four marks of a table
  };

  // **The grid of a table** (24/10): a word knows the block it lives in (`td`), and that is not enough —two
  // cells one after the other are the same block, so a page cannot tell where one ends and the next begins—.
  // These four marks are where a cell and a row begin and end.
  enum Cell : uint8_t {
    kCellOpen = 0,  // `<td>` or `<th>`: a cell starts here
    kCellClose,     // `</td>` or `</th>`: its words are over
    kRowClose,      // `</tr>`: the row is full
    kTableClose,    // `</table>`: the grid is over
  };

  Kind kind = kWord;

  const char* text = nullptr;   // word: the bytes, one per code
  int32_t text_len = 0;
  const char* href = nullptr;   // picture: the entry inside the zip
  int32_t href_len = 0;
  const char* block = nullptr;  // word: the block that opens it, or nullptr
  int32_t block_len = 0;

  int32_t at = 0;     // word: its first byte. Picture, break: its `<`
  int32_t after = 0;  // picture: the byte after its tag

  bool floats = false;      // picture: the book wants it beside the words
  bool right_side = false;  // picture: it floats to the right
  bool rule = false;        // break: it is an `<hr>` and not a `<br>`
  Cell cell = kCellOpen;    // cell: which mark it is
  bool header = false;      // cell: it is a `<th>` and not a `<td>`

  // **The ink inside a line** (24/10): what the book says, and nothing else. What a page does with a bold or
  // an italic is the page's business —the faces of this system have no bold and no italic of their own, so
  // the page thickens and slants them itself— and `rise` is `sup` (-1) and `sub` (+1).
  bool bold = false;
  bool italic = false;
  int8_t rise = 0;

  // **The word is inside a quotation, a figure or a definition** (0.130.0): a page insets those and not the
  // paragraphs they hold, and the block that opened the word cannot say it —the paragraph inside a quotation
  // opens a `p` like any other—.
  bool inset = false;
};

// **The reader of one chapter.** It starts at the byte `from` —which is what a page saves— and walks forward;
// every call to `next()` gives one token and advances, and at the end of the chapter it says false and keeps
// saying false.
class Tokens {
 public:
  // A longer word is **cut and not lost**: a word of two hundred bytes in a book with no spaces is a book
  // that was written badly, and a page that disappears because of it is worse.
  static const int32_t kMaxWord = 128;
  // The entry of a picture, which is a path inside the zip and not a URL: a hundred and sixty bytes is a name
  // of a folder and a file, and the ones longer than that are refused by name by the zip reader too.
  static const int32_t kMaxHref = 160;

  Tokens(const char* data, int32_t size, int32_t from = 0);

  // The next token. False when the chapter ends.
  bool next(Token& out);

  // **The byte the reader is at**: the offset a page saves, and the one the next page starts at.
  int32_t position() const { return pos_; }

  // **The chapter itself**, so that a second reader can walk it from anywhere. A table needs to know how many
  // columns it has **before** the first cell is painted —the width of a cell is the width of the page divided
  // by the columns— and the row is already arriving; a probe over the same bytes, from here, is the cheapest
  // way to ask.
  const char* data() const { return data_; }
  int32_t size() const { return size_; }

 private:
  // One `<td>`/`</td>`/`</tr>`/`</table>` waiting to be handed on.
  struct Mark {
    Token::Cell kind;
    bool header;
    int32_t at;
  };

  char peek() const;
  void handle_tag();
  bool take_word(Token& out);
  void start_word(int32_t at);
  void append_byte(char c);
  void append_code(uint32_t code);
  void entity();
  void picture_from_tag(const char* raw, int32_t raw_len);
  void push_mark(Token::Cell kind, bool header, int32_t at);

  const char* data_;
  int32_t size_;
  int32_t pos_;

  int32_t tag_at_;  // where the tag being read opened
  bool in_tag_;
  char tag_[256];
  int32_t tag_len_;
  const char* skip_;  // "script", "style", "head" or "comment", or nullptr
  char tail_[16];
  int32_t tail_len_;

  char word_[kMaxWord];
  int32_t word_len_;
  int32_t word_at_;
  char word_block_[16];
  int32_t word_block_len_;
  bool word_bold_;
  bool word_italic_;
  int8_t word_rise_;
  bool word_inset_;

  // **How deep the reader is inside `b`, `i`, `sup` and `sub`**. A count and not a flag: a book nests them
  // (`<b>saying <i>this</i></b>`) and a flag that one closing tag clears leaves the rest of the paragraph in
  // the wrong ink.
  int32_t bold_;
  int32_t italic_;
  int32_t sup_;
  int32_t sub_;

  // **How deep the word is inside a quotation, a figure or a definition** (0.130.0), counted the same way and
  // for the same reason: a `<blockquote>` holds paragraphs, and the paragraph inside one is a `p` like any
  // other. Without this count an inset quote would never be inset, which is how the paper project read every
  // book that writes `<blockquote><p>…</p></blockquote>`.
  int32_t inset_;

  char block_[16];
  int32_t block_len_;
  bool have_block_;
  bool pre_;  // inside `<pre>`: a newline is a line end

  char href_[kMaxHref];
  int32_t href_len_;
  int32_t pic_at_;
  int32_t pic_after_;
  bool pic_floats_;
  bool pic_right_;
  bool pic_waiting_;

  bool break_waiting_;
  bool break_rule_;

  // **The marks of a table that are waiting, in order.** A queue and not a flag because `</td><td>` is two
  // marks in a row: with one flag the second would overwrite the first and a cell of every row would go
  // missing. Four slots are enough —a cell opens, a cell closes, a row closes and a table closes are never all
  // pending at once— and a push that finds it full drops the mark instead of corrupting the queue.
  static const int32_t kMarkSlots = 4;
  Mark mark_[kMarkSlots];
  int32_t mark_head_;
  int32_t mark_count_;
};

// Whether the token opens the block `name`, compared as a C string.
bool block_is(const Token& token, const char* name);

}  // namespace epub

#endif  // LUNOK_EPUB_HTML_H
