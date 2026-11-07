// **The XHTML of a chapter, word by word** (docs/46, A2) — the implementation, and the one table of this file
// that is a decision and not a translation: the letters of Latin-1 and the punctuation that stands in for the
// marks a Latin-1 face cannot carry.
#include "epub/html.h"

#include <string.h>

namespace epub {

namespace {

char lower_char(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool is_alnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

void lower_copy(const char* in, int32_t len, char* out, int32_t cap) {
  int32_t n = 0;
  for (int32_t i = 0; i < len && n < cap - 1; ++i) out[n++] = lower_char(in[i]);
  out[n] = 0;
}

// **The value of one attribute, searched the way the script of the paper searched it.** The name is matched
// case-insensitively anywhere in the tag and has to be followed by optional spaces and `=`, which is what the
// Lua patterns did; the value is copied **as it is**, because an entry of a zip is not lowercased (and
// `xlink:href` matches `href`, which is wanted).
bool attr_value(const char* raw, int32_t raw_len, const char* name, char* out, int32_t cap) {
  const int32_t name_len = static_cast<int32_t>(strlen(name));

  for (int32_t i = 0; i + name_len < raw_len; ++i) {
    int32_t k = 0;
    while (k < name_len && lower_char(raw[i + k]) == name[k]) ++k;
    if (k != name_len) continue;

    int32_t j = i + name_len;
    while (j < raw_len && is_space(raw[j])) ++j;
    if (j >= raw_len || raw[j] != '=') continue;
    ++j;
    while (j < raw_len && is_space(raw[j])) ++j;
    if (j >= raw_len) continue;

    const char quote = raw[j];
    if (quote != '"' && quote != '\'') continue;
    ++j;

    int32_t n = 0;
    while (j < raw_len && raw[j] != quote) {
      if (n < cap - 1) out[n++] = raw[j];
      ++j;
    }
    out[n] = 0;
    return true;
  }

  return false;
}

// **`float: left` (or right) inside a style.** The value is lowercased first: a float is the only thing a page
// takes from a style while the CSS of the book is ignored, and the case of `LEFT` is not information.
bool style_floats(const char* style, bool* right) {
  char low[96];
  lower_copy(style, static_cast<int32_t>(strlen(style)), low, static_cast<int32_t>(sizeof(low)));

  const char* p = strstr(low, "float");
  if (p == nullptr) return false;

  p += 5;
  while (*p == ' ') ++p;
  if (*p != ':') return false;
  ++p;
  while (*p == ' ') ++p;

  if (strncmp(p, "left", 4) == 0) {
    *right = false;
    return true;
  }
  if (strncmp(p, "right", 5) == 0) {
    *right = true;
    return true;
  }

  return false;
}

// **The blocks a page has to start again for.** The list is the paper project's, which is the list of the
// tags a book of this house actually uses: the paragraphs, the titles, the lists, the quotation, the figure
// and its caption, the code block, and the two halves of a definition list (`dt` is the term and `dd` is what
// it means, indented under it —they were missing once and a whole definition list came out as one paragraph—).
bool is_block_tag(const char* name) {
  static const char* const kBlocks[] = {
      "p",    "div",   "br",   "hr",    "li",     "tr",     "td",         "th",
      "table", "ul",   "ol",   "h1",    "h2",     "h3",     "h4",         "h5",
      "h6",   "section", "article", "header", "footer", "blockquote", "pre", "figure",
      "figcaption", "dl", "dt", "dd",
  };

  for (unsigned i = 0; i < sizeof(kBlocks) / sizeof(kBlocks[0]); ++i) {
    if (strcmp(name, kBlocks[i]) == 0) return true;
  }

  return false;
}

// **One code of the text, and the byte of the face that stands for it** —or 0 when it paints nothing—.
struct Stand_in {
  uint32_t code;
  uint8_t byte;
};

// **The marks a Latin-1 face cannot carry** (0.128.0). A book is written in UTF-8 and its punctuation is the
// typographer's: the dashes, the curly quotes, the ellipsis and the bullet. The face of the book is Latin-1
// and the pipeline is one byte per character, so each of them is turned into the letter that means it —a long
// dash painted as `?` would be worse than a short one—. The soft hyphen paints nothing and is dropped, and
// the no-break space becomes a space: a word of a page is broken where the book breaks it, and this reader
// does not break a line on a space anyway.
const Stand_in kStandIns[] = {
    {0x2010, '-'},  {0x2011, '-'},  {0x2013, '-'},  {0x2014, '-'},  {0x2212, '-'},
    {0x2018, '\''}, {0x2019, '\''}, {0x201A, '\''}, {0x201B, '\''}, {0x2032, '\''},
    {0x201C, '"'},  {0x201D, '"'},  {0x201E, '"'},  {0x201F, '"'},  {0x2033, '"'},
    {0x2026, '.'},   {0x2022, 0xB7}, {0x00A0, ' '},  {0x00AD, 0},    {0x2007, ' '},
    {0x2009, ' '},   {0x200A, ' '},  {0x202F, ' '},  {0x20AC, 'E'},  {0x2116, 'N'},
    {0x2122, 'T'},   {0x2039, 0xAB}, {0x203A, 0xBB}, {0x0152, 'O'},  {0x0153, 'o'},
    {0x0160, 'S'},   {0x0161, 's'},  {0x0178, 'Y'},  {0x017D, 'Z'},  {0x017E, 'z'},
    {0x0192, 'f'},   {0x02C6, '^'},  {0x02DC, '~'},  {0x02C7, 'v'},  {0x02D8, 'v'},
    {0x02D9, '.'},   {0x02DA, 'o'},
};

// **The C1 range, which in an old book is Windows-1252 and not Latin-1.** A chapter of the nineties writes
// its apostrophe as `&#146;` and its dash as `&#150;`, and those numbers are the bytes of cp1252; a reader
// that painted them as question marks would fill a page of an old book with question marks. Zero is a byte
// that cp1252 does not use either.
const uint32_t kCp1252[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 0x80
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,       // 0x88
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 0x90
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,  // 0x98
};

struct Entity {
  const char* name;
  uint32_t code;  // 0: a mark that paints nothing (`&shy;`)
};

// **The entities of the XML and the Latin-1 ones**, which is all a book of this house needs. An entity nobody
// knows stays as it was written —right for a name that is not real— but `&eacute;` was one of them once and a
// Catalan book painted `caf&eacute;` on the glass, so every letter of Latin-1 is here.
const Entity kEntities[] = {
    {"amp", '&'},        {"lt", '<'},         {"gt", '>'},         {"quot", '"'},
    {"apos", '\''},      {"nbsp", 0xA0},      {"shy", 0},          {"mdash", 0x2014},
    {"ndash", 0x2013},   {"hellip", 0x2026},  {"middot", 0xB7},    {"laquo", 0xAB},
    {"raquo", 0xBB},     {"ldquo", 0x201C},   {"rdquo", 0x201D},   {"lsquo", 0x2018},
    {"rsquo", 0x2019},   {"copy", 0xA9},      {"reg", 0xAE},       {"deg", 0xB0},
    {"euro", 0x20AC},    {"sect", 0xA7},      {"para", 0xB6},      {"plusmn", 0xB1},
    {"times", 0xD7},     {"divide", 0xF7},    {"bull", 0x2022},    {"aacute", 0xE1},
    {"agrave", 0xE0},    {"acirc", 0xE2},     {"atilde", 0xE3},    {"auml", 0xE4},
    {"aring", 0xE5},     {"aelig", 0xE6},     {"ccedil", 0xE7},    {"egrave", 0xE8},
    {"eacute", 0xE9},    {"ecirc", 0xEA},     {"euml", 0xEB},      {"igrave", 0xEC},
    {"iacute", 0xED},    {"icirc", 0xEE},     {"iuml", 0xEF},      {"ntilde", 0xF1},
    {"ograve", 0xF2},    {"oacute", 0xF3},    {"ocirc", 0xF4},     {"otilde", 0xF5},
    {"ouml", 0xF6},      {"oslash", 0xF8},    {"ugrave", 0xF9},    {"uacute", 0xFA},
    {"ucirc", 0xFB},     {"uuml", 0xFC},      {"yacute", 0xFD},    {"yuml", 0xFF},
    {"szlig", 0xDF},     {"Aacute", 0xC1},    {"Agrave", 0xC0},    {"Acirc", 0xC2},
    {"Atilde", 0xC3},    {"Auml", 0xC4},      {"Aring", 0xC5},     {"AElig", 0xC6},
    {"Ccedil", 0xC7},    {"Egrave", 0xC8},    {"Eacute", 0xC9},    {"Ecirc", 0xCA},
    {"Euml", 0xCB},      {"Igrave", 0xCC},    {"Iacute", 0xCD},    {"Icirc", 0xCE},
    {"Iuml", 0xCF},      {"Ntilde", 0xD1},    {"Ograve", 0xD2},    {"Oacute", 0xD3},
    {"Ocirc", 0xD4},     {"Otilde", 0xD5},    {"Ouml", 0xD6},      {"Oslash", 0xD8},
    {"Ugrave", 0xD9},    {"Uacute", 0xDA},    {"Ucirc", 0xDB},     {"Uuml", 0xDC},
    {"Yacute", 0xDD},    {"OElig", 0x0152},   {"oelig", 0x0153},   {"scaron", 0x0161},
    {"Scaron", 0x0160},  {"Yuml", 0x0178},    {"fnof", 'f'},       {"permil", '%'},
    {"prime", '\''},     {"Prime", '"'},      {"dagger", '+'},     {"Dagger", '+'},
    {"sbquo", 0x201A},   {"bdquo", 0x201E},   {"lsaquo", 0x2039},  {"rsaquo", 0x203A},
    {"trade", 0x2122},   {"larr", '<'},       {"rarr", '>'},       {"harr", '-'},
    {"uarr", '^'},       {"darr", 'v'},       {"infin", '8'},      {"ne", '!'},
    {"le", '<'},         {"ge", '>'},         {"minus", '-'},      {"emsp", ' '},
    {"ensp", ' '},       {"thinsp", ' '},     {"zwnj", 0},         {"zwj", 0},
    {"lrm", 0},          {"rlm", 0},
};

// The code of a named or numeric entity, or false when this reader does not know it.
bool entity_code(const char* name, int32_t len, uint32_t* code) {
  if (len > 0 && name[0] == '#') {
    uint32_t value = 0;
    int32_t i = 1;

    if (i < len && (name[i] == 'x' || name[i] == 'X')) {
      ++i;
      if (i >= len) return false;
      for (; i < len; ++i) {
        const char c = name[i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
          digit = static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
          digit = static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
          digit = static_cast<uint32_t>(c - 'A' + 10);
        } else {
          return false;
        }
        value = value * 16 + digit;
        if (value > 0x10FFFF) return false;
      }
    } else {
      if (i >= len) return false;
      for (; i < len; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
        value = value * 10 + static_cast<uint32_t>(name[i] - '0');
        if (value > 0x10FFFF) return false;
      }
    }

    // A control code and the two halves of a surrogate pair are not text: a book that carries one is a book
    // damaged in the making, and this reader says so by leaving the entity as it was written.
    if (value < 32 || (value >= 0xD800 && value <= 0xDFFF)) return false;

    *code = value;
    return true;
  }

  for (unsigned i = 0; i < sizeof(kEntities) / sizeof(kEntities[0]); ++i) {
    const int32_t name_len = static_cast<int32_t>(strlen(kEntities[i].name));
    if (name_len == len && memcmp(kEntities[i].name, name, static_cast<size_t>(len)) == 0) {
      *code = kEntities[i].code;
      return true;
    }
  }

  return false;
}

// **A UTF-8 sequence of the chapter**: how many bytes it takes and the code inside, or 0 when the byte is not
// the start of a well-formed one. The overlong forms, the surrogates and the codes past Unicode are refused
// by the same rules the entity reader applies, because both end in the same place —one code of the face—.
int32_t utf8_at(const char* data, int32_t size, int32_t at, uint32_t* code) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  const uint8_t first = bytes[at];

  if (first < 0x80) {
    *code = first;
    return 1;
  }

  int32_t length = 0;
  uint32_t value = 0;

  if ((first & 0xE0) == 0xC0) {
    length = 2;
    value = first & 0x1Fu;
  } else if ((first & 0xF0) == 0xE0) {
    length = 3;
    value = first & 0x0Fu;
  } else if ((first & 0xF8) == 0xF0) {
    length = 4;
    value = first & 0x07u;
  } else {
    return 0;
  }

  if (at + length > size) return 0;

  for (int32_t i = 1; i < length; ++i) {
    const uint8_t next = bytes[at + i];
    if ((next & 0xC0) != 0x80) return 0;
    value = (value << 6) | (next & 0x3Fu);
  }

  if (length == 2 && value < 0x80) return 0;
  if (length == 3 && value < 0x800) return 0;
  if (length == 4 && value < 0x10000) return 0;
  if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 0;

  *code = value;
  return length;
}

}  // namespace

bool block_is(const Token& token, const char* name) {
  if (token.block == nullptr || name == nullptr) return false;

  const int32_t len = static_cast<int32_t>(strlen(name));

  return token.block_len == len && memcmp(token.block, name, static_cast<size_t>(len)) == 0;
}

Tokens::Tokens(const char* data, int32_t size, int32_t from)
    : data_(data),
      size_(size < 0 ? 0 : size),
      pos_(from),
      tag_at_(0),
      in_tag_(false),
      tag_len_(0),
      skip_(nullptr),
      tail_len_(0),
      word_len_(0),
      word_at_(0),
      word_block_len_(0),
      word_bold_(false),
      word_italic_(false),
      word_rise_(0),
      bold_(0),
      italic_(0),
      sup_(0),
      sub_(0),
      block_len_(0),
      have_block_(false),
      pre_(false),
      href_len_(0),
      pic_at_(0),
      pic_after_(0),
      pic_floats_(false),
      pic_right_(false),
      pic_waiting_(false),
      break_waiting_(false),
      break_rule_(false),
      mark_head_(0),
      mark_count_(0) {
  tag_[0] = 0;
  word_[0] = 0;
  word_block_[0] = 0;
  block_[0] = 0;
  href_[0] = 0;
  tail_[0] = 0;

  if (pos_ < 0) pos_ = 0;
  if (pos_ > size_) pos_ = size_;
}

char Tokens::peek() const { return (pos_ < size_) ? data_[pos_] : 0; }

void Tokens::start_word(int32_t at) {
  if (word_len_ > 0) return;

  word_at_ = at;
  word_block_len_ = have_block_ ? block_len_ : 0;

  if (word_block_len_ > 0) memcpy(word_block_, block_, static_cast<size_t>(word_block_len_));

  // **The ink is read when the word begins**, like the block: a tag that arrives in the middle of a word
  // belongs to the next one.
  word_bold_ = bold_ > 0;
  word_italic_ = italic_ > 0;
  word_rise_ = static_cast<int8_t>(sup_ > 0 ? -1 : (sub_ > 0 ? 1 : 0));

  have_block_ = false;
}

void Tokens::append_byte(char c) {
  if (word_len_ < kMaxWord - 1) word_[word_len_++] = c;
}

// **One code of the text as one byte of the face**: the letters of Latin-1 as they are, the marks it cannot
// carry as the letter that stands for them, and `?` for everything else —which a book of this house never
// carries and a broken one may—.
void Tokens::append_code(uint32_t code) {
  if (code == 0) return;

  // A code of the C1 range is a byte of Windows-1252, and an old book writes its dashes and its apostrophes
  // that way: `&#150;` is a dash and not a question mark (see `kCp1252`).
  if (code >= 0x80 && code <= 0x9F) {
    const uint32_t mapped = kCp1252[code - 0x80];
    if (mapped == 0) {
      append_byte('?');
      return;
    }
    code = mapped;
  }

  for (unsigned i = 0; i < sizeof(kStandIns) / sizeof(kStandIns[0]); ++i) {
    if (kStandIns[i].code != code) continue;
    if (kStandIns[i].byte != 0) append_byte(static_cast<char>(kStandIns[i].byte));
    return;
  }

  if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
    append_byte(static_cast<char>(code));
    return;
  }

  append_byte('?');
}

void Tokens::entity() {
  const int32_t amp_at = pos_ - 1;

  char name[16];
  int32_t n = 0;

  while (n < 12 && pos_ < size_) {
    const char c = data_[pos_];
    if (c == ';' || c == '<' || is_space(c)) break;
    name[n++] = c;
    ++pos_;
  }

  start_word(amp_at);

  const bool closed = (pos_ < size_ && data_[pos_] == ';');

  if (closed) ++pos_;

  uint32_t code = 0;

  if (!entity_code(name, n, &code)) {
    // An entity nobody knows stays as it was written, which is what a reader of an old book wants to see.
    append_byte('&');
    for (int32_t i = 0; i < n; ++i) append_byte(name[i]);
    if (closed) append_byte(';');
    return;
  }

  append_code(code);
}

void Tokens::push_mark(Token::Cell kind, bool header, int32_t at) {
  if (mark_count_ >= kMarkSlots) return;

  Mark& mark = mark_[(mark_head_ + mark_count_) % kMarkSlots];

  mark.kind = kind;
  mark.header = header;
  mark.at = at;

  ++mark_count_;
}

void Tokens::handle_tag() {
  tag_[tag_len_ < 255 ? tag_len_ : 255] = 0;

  const int32_t tag_len = tag_len_ > 255 ? 255 : tag_len_;

  char low[256];
  lower_copy(tag_, tag_len, low, static_cast<int32_t>(sizeof(low)));

  const char* p = low;
  while (*p == ' ') ++p;

  const bool closing = (*p == '/');
  if (closing) ++p;
  while (*p == ' ') ++p;

  char name[16];
  int32_t n = 0;

  while (n < 15 && (is_alnum(*p) || *p == ':' || *p == '-' || *p == '_')) {
    name[n++] = *p++;
  }
  name[n] = 0;

  if (!closing && strcmp(name, "script") == 0) {
    skip_ = "script";
    tail_len_ = 0;
    return;
  }
  if (!closing && strcmp(name, "style") == 0) {
    skip_ = "style";
    tail_len_ = 0;
    return;
  }
  if (!closing && strcmp(name, "head") == 0) {
    // The title of the document lives here, and it is not a paragraph.
    skip_ = "head";
    tail_len_ = 0;
    return;
  }

  if (!closing && (strcmp(name, "br") == 0 || strcmp(name, "hr") == 0)) {
    // **A line end and a rule are tokens of their own.** As a pending block they would be lost: two `<br>` in
    // a row are two line ends, and an `<hr>` followed by a `<p>` would be overwritten by the `p`.
    break_rule_ = (name[0] == 'h');
    break_waiting_ = true;
    return;
  }

  if (strcmp(name, "pre") == 0) {
    // **A `<pre>` is where the line ends are the author's**: inside it a newline is a line of the page and not
    // a space between words. It is the only block whose white space is information, and a poem or a block of
    // code lives or dies by it (the spaces still collapse).
    pre_ = !closing;
  }

  // **The grid of a table**, which the `block` of a word cannot say: two cells one after the other are the
  // same block (`td`), so a page has no way to know where one ends and the next begins. A word that was half
  // read goes before the mark: `next()` looks at the queue after the word in progress, so the last word of a
  // cell is handed on inside its own cell.
  if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
    push_mark(closing ? Token::kCellClose : Token::kCellOpen, strcmp(name, "th") == 0, tag_at_);
  } else if (strcmp(name, "tr") == 0) {
    if (closing) push_mark(Token::kRowClose, false, tag_at_);
  } else if (strcmp(name, "table") == 0) {
    if (closing) push_mark(Token::kTableClose, false, tag_at_);
  }

  // **The marks that live inside a line.** What is kept here is only what the book says; what a page does
  // with it is the page's.
  if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0) {
    bold_ += closing ? -1 : 1;
    if (bold_ < 0) bold_ = 0;
    return;
  }
  if (strcmp(name, "i") == 0 || strcmp(name, "em") == 0) {
    italic_ += closing ? -1 : 1;
    if (italic_ < 0) italic_ = 0;
    return;
  }
  if (strcmp(name, "sup") == 0) {
    sup_ += closing ? -1 : 1;
    if (sup_ < 0) sup_ = 0;
    return;
  }
  if (strcmp(name, "sub") == 0) {
    sub_ += closing ? -1 : 1;
    if (sub_ < 0) sub_ = 0;
    return;
  }

  if (!closing && (strcmp(name, "img") == 0 || strcmp(name, "image") == 0)) {
    picture_from_tag(tag_, tag_len);
    return;
  }

  if (is_block_tag(name)) {
    memcpy(block_, name, static_cast<size_t>(n) + 1);
    block_len_ = n;
    have_block_ = true;
  }
}

void Tokens::picture_from_tag(const char* raw, int32_t raw_len) {
  char href[kMaxHref];

  if (!attr_value(raw, raw_len, "src", href, static_cast<int32_t>(sizeof(href))) &&
      !attr_value(raw, raw_len, "href", href, static_cast<int32_t>(sizeof(href)))) {
    // An `<img>` with no source is not a picture: nothing is painted and nothing is said.
    return;
  }

  int32_t len = static_cast<int32_t>(strlen(href));

  for (int32_t i = 0; i < len; ++i) {
    if (href[i] == '#') {
      len = i;
      break;
    }
  }

  if (len <= 0) return;

  // **The side**: `float:left` in the style, `align="left"` of EPUB 2, or a class with `float` in its name
  // —which is the only hint there is while the CSS of the book is ignored—.
  bool right = false;
  bool side = false;

  char value[64];

  if (attr_value(raw, raw_len, "style", value, static_cast<int32_t>(sizeof(value)))) {
    side = style_floats(value, &right);
  }

  if (!side && attr_value(raw, raw_len, "align", value, static_cast<int32_t>(sizeof(value)))) {
    char low[16];
    lower_copy(value, static_cast<int32_t>(strlen(value)), low, static_cast<int32_t>(sizeof(low)));

    if (strcmp(low, "left") == 0) {
      side = true;
      right = false;
    } else if (strcmp(low, "right") == 0) {
      side = true;
      right = true;
    }
  }

  if (!side && attr_value(raw, raw_len, "class", value, static_cast<int32_t>(sizeof(value)))) {
    char low[64];
    lower_copy(value, static_cast<int32_t>(strlen(value)), low, static_cast<int32_t>(sizeof(low)));

    if (strstr(low, "float") != nullptr) {
      if (strstr(low, "left") != nullptr) {
        side = true;
        right = false;
      } else if (strstr(low, "right") != nullptr) {
        side = true;
        right = true;
      }
    }
  }

  for (int32_t i = 0; i < len && i < kMaxHref - 1; ++i) href_[i] = href[i];

  href_len_ = len < kMaxHref - 1 ? len : kMaxHref - 1;
  pic_at_ = tag_at_;
  pic_after_ = pos_;
  pic_floats_ = side;
  pic_right_ = right;
  pic_waiting_ = true;
}

bool Tokens::take_word(Token& out) {
  if (word_len_ == 0) return false;

  out = Token();
  out.kind = Token::kWord;
  out.text = word_;
  out.text_len = word_len_;
  out.block = word_block_len_ > 0 ? word_block_ : nullptr;
  out.block_len = word_block_len_;
  out.at = word_at_;
  out.href = nullptr;
  out.href_len = 0;
  out.after = 0;
  out.floats = false;
  out.right_side = false;
  out.rule = false;
  out.bold = word_bold_;
  out.italic = word_italic_;
  out.rise = word_rise_;

  word_len_ = 0;
  word_block_len_ = 0;

  return true;
}

bool Tokens::next(Token& out) {
  for (;;) {
    // A word that is half read goes first: the picture, the `<br>` or the `<hr>` that interrupted it come in
    // the next call.
    if (word_len_ > 0 && (pic_waiting_ || break_waiting_ || mark_count_ > 0)) return take_word(out);

    // **The marks of a table come before a picture**: `</td><img>` inside a table is a cell that ends and
    // then a picture, and the other way round it would paint the picture in the wrong column.
    if (mark_count_ > 0) {
      const Mark& mark = mark_[mark_head_];

      out = Token();
      out.kind = Token::kCell;
      out.at = mark.at;
      out.after = mark.at;
      out.cell = mark.kind;
      out.header = mark.header;

      mark_head_ = (mark_head_ + 1) % kMarkSlots;
      --mark_count_;

      return true;
    }

    if (pic_waiting_) {
      pic_waiting_ = false;

      out = Token();
      out.kind = Token::kPicture;
      out.at = pic_at_;
      out.after = pic_after_;
      out.href = href_;
      out.href_len = href_len_;
      out.floats = pic_floats_;
      out.right_side = pic_right_;

      return true;
    }

    if (break_waiting_) {
      break_waiting_ = false;

      out = Token();
      out.kind = Token::kBreak;
      out.at = pos_;
      out.after = pos_;
      out.rule = break_rule_;

      return true;
    }

    if (pos_ >= size_) {
      if (word_len_ > 0) return take_word(out);
      return false;
    }

    const char c = data_[pos_++];

    if (skip_ != nullptr) {
      if (strcmp(skip_, "comment") == 0) {
        if (c == '-' && peek() == '-') {
          ++pos_;
          if (peek() == '>') ++pos_;
          skip_ = nullptr;
        }
        continue;
      }

      // **The end of the block that is being skipped**: `</script`, `</style` or `</head`. The tail keeps the
      // last characters seen, and eight are enough for the longest name of the three.
      if (tail_len_ < static_cast<int32_t>(sizeof(tail_)) - 1) tail_[tail_len_++] = lower_char(c);
      tail_[tail_len_] = 0;

      char probe[16];
      probe[0] = '<';
      probe[1] = '/';
      int32_t probe_len = 2;
      for (const char* name = skip_; *name != 0 && probe_len < 15; ++name) probe[probe_len++] = *name;
      probe[probe_len] = 0;

      if (strstr(tail_, probe) != nullptr) {
        char d = peek();
        while (d != 0 && d != '>') {
          ++pos_;
          d = peek();
        }
        if (d == '>') ++pos_;
        skip_ = nullptr;
        tail_len_ = 0;
      } else if (tail_len_ > 8) {
        memmove(tail_, tail_ + (tail_len_ - 8), 8);
        tail_len_ = 8;
        tail_[tail_len_] = 0;
      }
      continue;
    }

    if (in_tag_) {
      if (c == '>') {
        in_tag_ = false;
        handle_tag();
        continue;
      }
      if (tag_len_ < static_cast<int32_t>(sizeof(tag_)) - 1) tag_[tag_len_++] = c;
      continue;
    }

    if (c == '<') {
      tag_at_ = pos_ - 1;

      if (peek() == '!') {
        ++pos_;
        if (peek() == '-') {
          ++pos_;
          if (peek() == '-') {
            ++pos_;
            skip_ = "comment";
            tail_len_ = 0;
          } else {
            in_tag_ = true;
            tag_len_ = 0;
          }
        } else {
          in_tag_ = true;
          tag_len_ = 0;
        }
      } else {
        in_tag_ = true;
        tag_len_ = 0;
      }

      if (word_len_ > 0) return take_word(out);
      continue;
    }

    if (c == '&') {
      entity();
      continue;
    }

    if (is_space(c)) {
      // A `<pre>` is a poem or a table of lines, and its newlines are line ends; everywhere else a newline is
      // just a space.
      if (c == '\n' && pre_) {
        break_rule_ = false;
        break_waiting_ = true;
        continue;
      }

      if (word_len_ > 0) return take_word(out);
      continue;
    }

    // **A byte of a word, and the one place where the text of the book becomes a byte of the face.** A byte
    // above ASCII opens a UTF-8 sequence and takes as many bytes as it says; a byte that is not one is a
    // chapter already written in Latin-1 (some are, mislabelled), and a byte of Latin-1 is a code of the face
    // as it is.
    start_word(pos_ - 1);

    if (static_cast<uint8_t>(c) < 0x80) {
      append_byte(c);
      continue;
    }

    uint32_t code = 0;
    const int32_t used = utf8_at(data_, size_, pos_ - 1, &code);

    if (used == 0) {
      append_byte(c);
      continue;
    }

    append_code(code);
    pos_ = pos_ - 1 + used;
  }
}

}  // namespace epub
