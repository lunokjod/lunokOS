// **One page of a book, composed** (docs/46, A3) — the arithmetic of a page: the pen, the line, the air of a
// block, the band of a floated picture and the grid of a table. What it does not do is paint: it says where
// every word, every picture and every rule goes, and the kernel paints them (see `epub/page.h`).
#include "epub/page.h"

#include <string.h>

namespace epub {

namespace {

// **The bands a page can have at once.** A figure and a picture beside it are two, and a page with eight
// pictures floating in it does not exist in a book of this house; a band that does not fit is not paid and the
// words go under the picture.
const int32_t kMaxBands = 8;

bool heading(const Token& token) {
  return block_is(token, "h1") || block_is(token, "h2") || block_is(token, "h3");
}

// **The ink of a word**, which the tokenizer found inside the line: the bold, the italic and the step of a
// `sup`/`sub` (docs/46, A4).
uint8_t ink_of(const Token& token) {
  uint8_t ink = 0;
  if (token.bold) ink |= kInkBold;
  if (token.italic) ink |= kInkItalic;
  if (token.rise < 0) ink |= kInkRiseUp;
  if (token.rise > 0) ink |= kInkRiseDown;
  return ink;
}

// **A page that is being composed.**
//
// The pen is a `y` in pixels and not a count of lines: a title is taller than a word of the body and a picture
// takes the room it has, so the only number that means anything is how much paper is left.
class Page {
 public:
  Page(Tokens& tokens, int32_t width, int32_t height, const Style& style, const Metrics& metrics,
       const Pictures* pictures, const Sheet& sheet)
      : tokens_(tokens),
        width_(width),
        height_(height),
        style_(style),
        metrics_(metrics),
        pictures_(pictures),
        sheet_(sheet) {}

  bool run(Result& out);

 private:
  // The rows a floated picture covers and the box it leaves for the words beside it.
  struct Band {
    int32_t y0;
    int32_t y1;
    int32_t x0;
    int32_t x1;
    bool left;
  };

  int32_t put_text(const char* text, int32_t len);
  Item* add_item(Item::Kind kind);
  bool finish_at(int32_t at);

  uint8_t face_of(const Token& token) const;
  uint8_t scale_of(uint8_t face) const;
  int32_t line_of(uint8_t face, uint8_t scale) const;
  int32_t body_line() const { return line_of(kFaceBody, style_.body_scale); }

  void box_at(int32_t top, int32_t h, int32_t& x0, int32_t& x1) const;
  bool open_line(int32_t at);
  void flush_line();
  int32_t gap_for(const Token& token) const;
  bool add_word(Token& token);

  bool place_picture(const Token& token);
  bool place_rule(const Token& token);
  bool place_underline(int32_t x, int32_t y, int32_t w);

  void start_table();
  bool place_cell(const Token& token);

  Tokens& tokens_;
  int32_t width_;
  int32_t height_;
  const Style& style_;
  const Metrics& metrics_;
  const Pictures* pictures_;
  const Sheet& sheet_;

  int32_t words_len_ = 0;
  int16_t count_ = 0;

  int32_t y_ = 0;
  int32_t x0_ = 0;
  int32_t x1_ = 0;
  int32_t line_x_ = 0;
  int32_t line_w_ = 0;
  int16_t line_first_ = 0;
  int16_t line_words_ = 0;
  bool line_open_ = false;
  uint8_t face_ = kFaceBody;
  uint8_t scale_ = 0;
  int16_t indent_ = 0;
  bool centre_ = false;

  int32_t nbands_ = 0;
  Band bands_[kMaxBands];

  bool has_picture_ = false;
  int32_t next_ = 0;
  bool ended_ = false;

  // **The state of a table being laid out.** A table is the only block whose geometry a page cannot know one
  // word at a time: the columns come from how many cells the row has, and a row ends where its tallest cell ends.
  bool in_table_ = false;
  int32_t table_cols_ = 1;
  int32_t table_x_ = 0;
  int32_t table_w_ = 0;
  int32_t col_w_ = 0;
  int32_t cell_index_ = 0;
  int32_t row_top_ = 0;
  int32_t row_h_ = 0;
  int32_t row_at_ = 0;
  bool row_header_ = false;
};

// **The words of a page are the page's own copy**, and that is not a detail: a word of a chapter is a slice of a
// buffer that is not ended by a zero, and both measuring it and painting it are done with a string. The copy is
// a few bytes and it is the caller's block (docs/46, A7).
int32_t Page::put_text(const char* text, int32_t len) {
  if (text == nullptr || len < 0) return -1;
  if (words_len_ + len + 1 > sheet_.words_cap) return -1;

  const int32_t at = words_len_;
  memcpy(sheet_.words + at, text, static_cast<size_t>(len));
  sheet_.words[at + len] = 0;
  words_len_ = at + len + 1;

  return at;
}

Item* Page::add_item(Item::Kind kind) {
  if (count_ >= sheet_.items_cap) return nullptr;

  Item& item = sheet_.items[count_++];
  item = Item();
  item.kind = kind;

  return &item;
}

// **The page ends here**, and this is the byte the next one starts at: the word that did not fit, or the picture
// that would have been cut in half.
bool Page::finish_at(int32_t at) {
  next_ = at;
  ended_ = false;
  return false;
}

uint8_t Page::face_of(const Token& token) const {
  if (heading(token)) return kFaceTitle;
  if (block_is(token, "figcaption")) return kFaceSmall;

  return kFaceBody;
}

uint8_t Page::scale_of(uint8_t face) const {
  if (face == kFaceTitle) return style_.title_scale;
  if (face == kFaceSmall) return style_.small_scale;

  return style_.body_scale;
}

int32_t Page::line_of(uint8_t face, uint8_t scale) const {
  if (metrics_.line_height == nullptr) return 0;

  return metrics_.line_height(metrics_.ctx, face, scale);
}

// **The box the words of a line have**, which is the page and whatever a floated picture takes from it.
void Page::box_at(int32_t top, int32_t h, int32_t& x0, int32_t& x1) const {
  int32_t left = indent_;
  int32_t right = width_;

  for (int32_t i = 0; i < nbands_; ++i) {
    const Band& band = bands_[i];

    if (top < band.y1 && top + h > band.y0) {
      if (band.left) {
        if (band.x1 > left) left = band.x1;
      } else if (band.x0 < right) {
        right = band.x0;
      }
    }
  }

  // A band leaves at least twenty pixels of words, or the page would be a picture with a column of letters down
  // one side.
  if (right < left + 20) right = (left + 20 > width_) ? width_ : left + 20;

  x0 = left;
  x1 = right;
}

// **A line opens where the pen is**, and if it does not fit the page is over and the byte is the one of the word
// that opens it.
//
// The one exception is the first line of a page: a box shorter than a line would end every page without a word
// on it and the reader would never move, so the first line is placed whatever the box says.
bool Page::open_line(int32_t at) {
  const int32_t line = line_of(face_, scale_);

  if (count_ > 0 && y_ + line > height_) return finish_at(at);

  if (!in_table_) box_at(y_, line, x0_, x1_);

  line_x_ = x0_;
  line_w_ = 0;
  line_words_ = 0;
  line_first_ = count_;
  line_open_ = true;

  return true;
}

// **A line is closed and the pen goes down one line.** A caption is the only block that goes in the middle: it
// belongs to a picture and a picture is centred too, and the items of the line are the ones that move.
void Page::flush_line() {
  if (!line_open_) return;

  if (centre_ && line_words_ > 0) {
    const int32_t centred = indent_ + (width_ - indent_ - line_w_) / 2;
    const int32_t shift = (centred > line_x_) ? (centred - line_x_) : 0;

    for (int16_t i = line_first_; i < count_; ++i) {
      sheet_.items[i].x = static_cast<int16_t>(sheet_.items[i].x + shift);
    }
  }

  if (line_words_ > 0) y_ += line_of(face_, scale_);

  line_open_ = false;
  line_words_ = 0;
  line_w_ = 0;
}

// **The air before a block**, which is a fraction of the line of the body and the paper's own typography: a title
// opens with two thirds, a list item with a quarter, the term of a definition with a half and what it means with
// a sixth (which is what makes a `dl` read as a list and not as a paragraph with a gap in it).
int32_t Page::gap_for(const Token& token) const {
  const int32_t line = body_line();

  if (heading(token)) return (line * 2) / 3;
  if (block_is(token, "li")) return line / 4;
  if (block_is(token, "figcaption")) return line / 4;
  if (block_is(token, "dt")) return line / 2;
  if (block_is(token, "dd")) return line / 6;

  return line / 2;
}

bool Page::add_word(Token& token) {
  // **The word is copied before it is measured**, because a text is measured and painted as a string and a word
  // of a chapter is a slice with no zero after it.
  //
  // The dot of a list item is glued to its word: it has to wrap with it and be measured with it, which is what a
  // bullet of a list means.
  int32_t at_text = -1;

  if (block_is(token, "li") && !in_table_) {
    if (words_len_ + token.text_len + 4 > sheet_.words_cap) return finish_at(token.at);

    at_text = words_len_;
    sheet_.words[at_text] = static_cast<char>(0xB7);  // the middle dot, which the face of a book carries
    sheet_.words[at_text + 1] = ' ';
    sheet_.words[at_text + 2] = ' ';
    memcpy(sheet_.words + at_text + 3, token.text, static_cast<size_t>(token.text_len));
    sheet_.words[at_text + 3 + token.text_len] = 0;
    words_len_ = at_text + 3 + token.text_len + 1;
  } else {
    at_text = put_text(token.text, token.text_len);
    if (at_text < 0) return finish_at(token.at);
  }

  const char* word = sheet_.words + at_text;
  const uint8_t face = face_;
  const uint8_t scale = scale_;
  const int32_t w = metrics_.text_width(metrics_.ctx, face, scale, word);
  const int32_t space = metrics_.text_width(metrics_.ctx, face, scale, " ");

  if (!line_open_) {
    if (!open_line(token.at)) return false;
  } else if (line_words_ > 0 && line_x_ + line_w_ + space + w > x1_) {
    flush_line();
    if (!open_line(token.at)) return false;
  }

  const int32_t between = (line_words_ > 0) ? space : 0;
  const int32_t x = line_x_ + line_w_ + between;

  Item* item = add_item(Item::kWord);
  if (item == nullptr) return finish_at(token.at);

  // **The step off the baseline** is the ink's and the line's: a `sup` goes up a third of the line and a `sub`
  // down a fifth, and the line does not grow for it (docs/46, A4).
  const int32_t line = line_of(face, scale);
  int32_t off = 0;
  if ((item->ink = ink_of(token)) != 0) {
    if ((item->ink & kInkRiseMask) == kInkRiseUp) {
      off = -(line / 3);
    } else if ((item->ink & kInkRiseMask) == kInkRiseDown) {
      off = line / 5;
    }
  }
  // A header of a table is ink like the rest of the row and heavier: `th` is what a book uses to say that this
  // line names the columns.
  if (in_table_ && row_header_) item->ink |= kInkBold;

  item->text = at_text;
  item->x = static_cast<int16_t>(x);
  item->y = static_cast<int16_t>(y_ + off);
  item->face = face;
  item->scale = scale;

  line_w_ += between + w;
  ++line_words_;

  return true;
}

// **A picture closes the line it interrupts and then takes its place.** A float shares the line with the words,
// so it is never wider than a part of the text; a picture across the whole page has no words beside it and is a
// block of its own.
bool Page::place_picture(const Token& token) {
  flush_line();

  const int32_t box_w = token.floats ? (width_ * kFloatShare) / 100 : width_;
  int32_t box_h = token.floats ? (height_ / 2) : (height_ - 2 * body_line());

  if (box_h < 32) box_h = 32;
  if (box_w < 8) return true;  // no room for a picture: nothing to place

  if (pictures_ == nullptr || pictures_->measure == nullptr) return true;

  const int32_t at_href = put_text(token.href, token.href_len);
  if (at_href < 0) return true;  // the pool is full: this picture is not placed and the page goes on

  int32_t w = 0;
  int32_t h = 0;
  if (!pictures_->measure(pictures_->ctx, sheet_.words + at_href, box_w, box_h, w, h) || w <= 0 || h <= 0) {
    // A picture that cannot be read is not placed and the page goes on: the rest of the book is not the fault of
    // one entry.
    return true;
  }

  // **A picture is not cut in half**: it opens the next page, which is what a page of paper does with a plate.
  if (y_ + h > height_) return finish_at(token.at);

  Item* item = add_item(Item::kPicture);
  if (item == nullptr) return finish_at(token.at);

  item->text = at_href;
  item->w = static_cast<int16_t>(w);
  item->h = static_cast<int16_t>(h);
  item->y = static_cast<int16_t>(y_);

  if (token.floats) {
    const int32_t left = token.right_side ? (width_ - w) : 0;
    item->x = static_cast<int16_t>(left);

    if (nbands_ < kMaxBands) {
      Band& band = bands_[nbands_++];
      band.left = !token.right_side;
      band.y0 = y_;
      band.y1 = y_ + h + kPictureGap;
      band.x0 = token.right_side ? (left - kPictureGap) : 0;
      band.x1 = token.right_side ? width_ : (w + kPictureGap);
    }

    // The pen does not move: the words go beside the picture.
  } else {
    item->x = static_cast<int16_t>((width_ - w) / 2);
    y_ += h + kPictureGap;
  }

  has_picture_ = true;

  return true;
}

bool Page::place_rule(const Token& token) {
  flush_line();

  const int32_t line = body_line();

  if (y_ + line > height_) return finish_at(token.at);

  Item* item = add_item(Item::kRule);
  if (item == nullptr) return finish_at(token.at);

  item->x = 0;
  item->y = static_cast<int16_t>(y_ + line / 2);
  item->w = static_cast<int16_t>(width_);
  item->h = 1;

  y_ += line;

  return true;
}

// **The underline of a header of a table**, which is the only rule of the grid: a table of a book has no frames,
// the columns are the frame.
bool Page::place_underline(int32_t x, int32_t y, int32_t w) {
  Item* item = add_item(Item::kRule);
  if (item == nullptr) return false;

  item->x = static_cast<int16_t>(x);
  item->y = static_cast<int16_t>(y);
  item->w = static_cast<int16_t>(w);
  item->h = 1;

  return true;
}

// **A table is a grid and not a list of paragraphs.**
//
// The width of a cell is the width of the page divided by the columns, and the row is already arriving when the
// first cell has to be placed: that is why the columns are COUNTED FIRST, with a probe over the same bytes of
// the chapter (`Tokens::data()/size()`). The probe also says how many cells the widest row has, so a row that is
// wider than the first one still gets its columns.
void Page::start_table() {
  in_table_ = true;
  table_x_ = 0;
  table_w_ = width_;

  int32_t columns = 1;
  // **The probe starts inside the first cell**, which is the one that is being placed: the cells it counts from
  // here are the second, the third and so on, and a row of two cells has two columns and not one.
  int32_t cells = 1;

  Tokens probe(tokens_.data(), tokens_.size(), tokens_.position());
  Token looking;

  for (int32_t i = 0; i < kProbeTokens; ++i) {
    if (!probe.next(looking)) break;
    if (looking.kind != Token::kCell) continue;

    if (looking.cell == Token::kCellOpen) {
      ++cells;
      if (cells > columns) columns = cells;
      if (columns >= kMaxColumns) break;
    } else if (looking.cell == Token::kRowClose) {
      cells = 0;
    } else if (looking.cell == Token::kTableClose) {
      break;
    }
  }

  if (columns < 1) columns = 1;
  if (columns > kMaxColumns) columns = kMaxColumns;

  table_cols_ = columns;
  col_w_ = table_w_ / columns;

  cell_index_ = 0;
  row_top_ = y_;
  row_h_ = 0;
}

// **One mark of the grid, one line of the table.** The pen of the page is the pen of the CELL: a cell is laid out
// by the same `add_word()`/`flush_line()` as any other line of the book, and what changes is only the box it
// wraps in and where the row starts.
bool Page::place_cell(const Token& token) {
  const int32_t line = body_line();

  switch (token.cell) {
    case Token::kCellOpen: {
      flush_line();

      if (!in_table_) start_table();

      if (cell_index_ == 0) {
        row_top_ = y_;
        row_at_ = token.at;
        row_h_ = 0;
        row_header_ = token.header;
      }

      ++cell_index_;

      // A row wider than the columns that fit: the extra cells go on in the last column, where they are seen,
      // instead of being thrown away.
      if (cell_index_ > table_cols_) cell_index_ = table_cols_;

      const int32_t left = table_x_ + (cell_index_ - 1) * col_w_;
      const int32_t right =
          (cell_index_ == table_cols_) ? (table_x_ + table_w_) : (left + col_w_);

      y_ = row_top_;
      x0_ = left;
      x1_ = right;
      line_x_ = left;
      line_w_ = 0;
      line_words_ = 0;
      line_open_ = false;
      indent_ = 0;
      face_ = kFaceBody;
      scale_ = style_.body_scale;
      centre_ = false;

      return true;
    }

    case Token::kCellClose:
      // The words of the cell are over: what is left is the height it took, which the row keeps if it is the
      // tallest one so far.
      flush_line();

      if (y_ - row_top_ > row_h_) row_h_ = y_ - row_top_;

      return true;

    case Token::kRowClose:
      flush_line();

      if (y_ - row_top_ > row_h_) row_h_ = y_ - row_top_;

      if (row_header_ && row_h_ > 0) {
        const int32_t under = row_top_ + row_h_ - 1;
        if (under >= 0 && under < height_ && !place_underline(table_x_, under, table_w_)) {
          return finish_at(token.at);
        }
      }

      y_ = row_top_ + row_h_;
      cell_index_ = 0;
      line_open_ = false;

      // **And a row that did not fit is the first one of the next page.** The page ends where the row began,
      // which is what the pictures do: half a table on the glass and half on the next page is a table the reader
      // cannot read.
      if (y_ + line > height_ && row_top_ > 0) return finish_at(row_at_);

      return true;

    case Token::kTableClose:
      flush_line();

      in_table_ = false;
      cell_index_ = 0;
      row_header_ = false;
      x0_ = 0;
      x1_ = width_;
      indent_ = 0;
      face_ = kFaceBody;
      scale_ = style_.body_scale;

      // The air after a table is the air of a block, and it is not paid at the bottom of the page.
      if (y_ > 0 && y_ + line / 2 <= height_) y_ += line / 2;

      return true;
  }

  return true;
}

bool Page::run(Result& out) {
  for (;;) {
    Token token;

    if (!tokens_.next(token)) {
      flush_line();
      ended_ = true;
      break;
    }

    if (token.kind == Token::kPicture) {
      if (!place_picture(token)) break;
      continue;
    }

    if (token.kind == Token::kBreak) {
      if (token.rule) {
        if (!place_rule(token)) break;
      } else {
        flush_line();
      }
      continue;
    }

    if (token.kind == Token::kCell) {
      if (!place_cell(token)) break;
      continue;
    }

    // **A new block brings its face, its air and —for a quotation, a figure or a definition— its inset.** A
    // caption keeps the inset of its figure and goes to the middle.
    //
    // Inside a table there is no block air: every cell changes the block (`td`, `th`, `td`) and a gap before each
    // one would turn the grid into a column of paragraphs.
    if (token.block != nullptr && !in_table_) {
      flush_line();

      face_ = face_of(token);
      scale_ = scale_of(face_);

      // **The inset is the token's** (0.130.0): a quotation, a figure and a definition inset the words inside
      // them, and the paragraph that holds those words is a `p` like any other, so it cannot say it.
      indent_ = token.inset ? style_.indent : 0;

      centre_ = block_is(token, "figcaption");

      // The air before a block is never paid at the top of a page: there the block starts at the edge.
      if (y_ > 0) {
        const int32_t gap = gap_for(token);
        if (y_ + gap + line_of(face_, scale_) <= height_) y_ += gap;
      }
    }

    if (!add_word(token)) break;
  }

  out.count = count_;
  out.ended = ended_;
  out.has_picture = has_picture_;
  out.next = ended_ ? 0 : next_;
  out.used = y_;

  return true;
}

}  // namespace

bool layout(Tokens& tokens, int32_t width, int32_t height, const Style& style, const Metrics& metrics,
            const Pictures* pictures, const Sheet& sheet, Result& result) {
  if (width < 16 || height < 16) return false;
  if (metrics.text_width == nullptr || metrics.line_height == nullptr) return false;
  if (sheet.items == nullptr || sheet.items_cap <= 0) return false;
  if (sheet.words == nullptr || sheet.words_cap <= 0) return false;

  Page page(tokens, width, height, style, metrics, pictures, sheet);

  return page.run(result);
}

}  // namespace epub
