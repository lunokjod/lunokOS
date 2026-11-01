// The five by seven glyphs and the two functions that put them on a surface (docs/30).
#include "kernel/font.h"

#include "ttf/ttf.h"

#include <stddef.h>

namespace kernel {
namespace font {
namespace {

// Glyphs for the characters 0x20 to 0x5A —space, punctuation, digits and capitals—, five columns each
// and one byte per column: **bit 0 is the top row and bit 6 the bottom one**, so a column is read
// downwards from the least significant bit. Everything outside that range is drawn as a capital or as
// a space, which is what keeps the table at 295 bytes (docs/30).
struct Glyph {
  uint8_t column[5];
};

// clang-format off
const Glyph kGlyphs[] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00}},  // ' '
    {{0x00, 0x00, 0x5F, 0x00, 0x00}},  // '!'
    {{0x00, 0x07, 0x00, 0x07, 0x00}},  // '"'
    {{0x14, 0x7F, 0x14, 0x7F, 0x14}},  // '#'
    {{0x24, 0x2A, 0x7F, 0x2A, 0x12}},  // '$'
    {{0x23, 0x13, 0x08, 0x64, 0x62}},  // '%'
    {{0x36, 0x49, 0x55, 0x22, 0x50}},  // '&'
    {{0x00, 0x05, 0x03, 0x00, 0x00}},  // '\''
    {{0x00, 0x1C, 0x22, 0x41, 0x00}},  // '('
    {{0x00, 0x41, 0x22, 0x1C, 0x00}},  // ')'
    {{0x08, 0x2A, 0x1C, 0x2A, 0x08}},  // '*'
    {{0x08, 0x08, 0x3E, 0x08, 0x08}},  // '+'
    {{0x00, 0x50, 0x30, 0x00, 0x00}},  // ','
    {{0x08, 0x08, 0x08, 0x08, 0x08}},  // '-'
    {{0x00, 0x60, 0x60, 0x00, 0x00}},  // '.'
    {{0x20, 0x10, 0x08, 0x04, 0x02}},  // '/'
    {{0x3E, 0x51, 0x49, 0x45, 0x3E}},  // '0'
    {{0x00, 0x42, 0x7F, 0x40, 0x00}},  // '1'
    {{0x42, 0x61, 0x51, 0x49, 0x46}},  // '2'
    {{0x21, 0x41, 0x45, 0x4B, 0x31}},  // '3'
    {{0x18, 0x14, 0x12, 0x7F, 0x10}},  // '4'
    {{0x27, 0x45, 0x45, 0x45, 0x39}},  // '5'
    {{0x3C, 0x4A, 0x49, 0x49, 0x30}},  // '6'
    {{0x01, 0x71, 0x09, 0x05, 0x03}},  // '7'
    {{0x36, 0x49, 0x49, 0x49, 0x36}},  // '8'
    {{0x06, 0x49, 0x49, 0x29, 0x1E}},  // '9'
    {{0x00, 0x36, 0x36, 0x00, 0x00}},  // ':'
    {{0x00, 0x56, 0x36, 0x00, 0x00}},  // ';'
    {{0x08, 0x14, 0x22, 0x41, 0x00}},  // '<'
    {{0x14, 0x14, 0x14, 0x14, 0x14}},  // '='
    {{0x00, 0x41, 0x22, 0x14, 0x08}},  // '>'
    {{0x02, 0x01, 0x51, 0x09, 0x06}},  // '?'
    {{0x32, 0x49, 0x79, 0x41, 0x3E}},  // '@'
    {{0x7E, 0x11, 0x11, 0x11, 0x7E}},  // 'A'
    {{0x7F, 0x49, 0x49, 0x49, 0x36}},  // 'B'
    {{0x3E, 0x41, 0x41, 0x41, 0x22}},  // 'C'
    {{0x7F, 0x41, 0x41, 0x22, 0x1C}},  // 'D'
    {{0x7F, 0x49, 0x49, 0x49, 0x41}},  // 'E'
    {{0x7F, 0x09, 0x09, 0x09, 0x01}},  // 'F'
    {{0x3E, 0x41, 0x49, 0x49, 0x7A}},  // 'G'
    {{0x7F, 0x08, 0x08, 0x08, 0x7F}},  // 'H'
    {{0x00, 0x41, 0x7F, 0x41, 0x00}},  // 'I'
    {{0x20, 0x40, 0x41, 0x3F, 0x01}},  // 'J'
    {{0x7F, 0x08, 0x14, 0x22, 0x41}},  // 'K'
    {{0x7F, 0x40, 0x40, 0x40, 0x40}},  // 'L'
    {{0x7F, 0x02, 0x0C, 0x02, 0x7F}},  // 'M'
    {{0x7F, 0x04, 0x08, 0x10, 0x7F}},  // 'N'
    {{0x3E, 0x41, 0x41, 0x41, 0x3E}},  // 'O'
    {{0x7F, 0x09, 0x09, 0x09, 0x06}},  // 'P'
    {{0x3E, 0x41, 0x51, 0x21, 0x5E}},  // 'Q'
    {{0x7F, 0x09, 0x19, 0x29, 0x46}},  // 'R'
    {{0x46, 0x49, 0x49, 0x49, 0x31}},  // 'S'
    {{0x01, 0x01, 0x7F, 0x01, 0x01}},  // 'T'
    {{0x3F, 0x40, 0x40, 0x40, 0x3F}},  // 'U'
    {{0x1F, 0x20, 0x40, 0x20, 0x1F}},  // 'V'
    {{0x3F, 0x40, 0x38, 0x40, 0x3F}},  // 'W'
    {{0x63, 0x14, 0x08, 0x14, 0x63}},  // 'X'
    {{0x07, 0x08, 0x70, 0x08, 0x07}},  // 'Y'
    {{0x61, 0x51, 0x49, 0x45, 0x43}},  // 'Z'
};
// clang-format on

const char kFirst = ' ';
const char kLast = 'Z';
const uint16_t kCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

// **How the bits of the system's face are laid out**: five columns per glyph and bit 0 is the top row, which
// is how the table above is written and the reason it costs 295 bytes (docs/30).
bool builtin_lit(const uint8_t* bits, uint8_t index, uint8_t x, uint8_t y) {
  if (bits == nullptr || x >= kGlyphWidth || y >= kGlyphHeight) return false;
  const uint8_t* columns = bits + static_cast<uint32_t>(index) * kGlyphWidth;
  return ((columns[x] >> y) & 1u) != 0;
}

// **A lowercase letter is its capital and anything outside the table is a gap**: a bar with a box in it says
// less than a bar with a space, and the table stops at 'Z' (docs/30).
uint8_t builtin_code(uint8_t c) {
  if (c >= 'a' && c <= 'z') c = static_cast<uint8_t>(c - 'a' + 'A');
  if (c < static_cast<uint8_t>(kFirst) || c > static_cast<uint8_t>(kLast)) return 0;
  return c;
}

// **How the bits of a face from a file are laid out**: one byte per row, the leftmost column in bit 7, and
// `kMaxGlyphHeight` rows per glyph —which is where a file's rows land— (docs/30).
bool file_lit(const uint8_t* bits, uint8_t index, uint8_t x, uint8_t y) {
  if (bits == nullptr || x >= kMaxGlyphWidth || y >= kMaxGlyphHeight) return false;
  const uint8_t* rows = bits + static_cast<uint32_t>(index) * kMaxGlyphHeight;
  return ((rows[y] >> (7 - x)) & 1u) != 0;
}

// A face from a file carries exactly the codes it was given: no folding and no invention.
uint8_t file_code(uint8_t c) { return c; }

}  // namespace

const Face& system_face() {
  static const Face face = {kGlyphWidth, kGlyphHeight, 1u, static_cast<uint8_t>(kFirst),
                            static_cast<uint8_t>(kCount), reinterpret_cast<const uint8_t*>(kGlyphs),
                            &builtin_lit, &builtin_code, nullptr};
  return face;
}

uint16_t width(const Face& face, const char* text, uint8_t scale) {
  if (text == nullptr || text[0] == '\0' || scale == 0) return 0;
  // **A face of outlines is laid out glyph by glyph**: every one of them has its own advance in the file, and
  // the size of the body is what one step of `scale` means (docs/30).
  if (face.outline != nullptr) {
    const uint16_t px = static_cast<uint16_t>(kOutlineBody * scale);
    uint16_t total = 0;
    for (const char* at = text; *at != '\0'; ++at) {
      const uint8_t code = static_cast<uint8_t>(*at);
      if (code < 0x20 || code > 0x7F) continue;
      uint16_t glyph = 0;
      if (ttf::glyph_of(*face.outline, code, glyph) != ttf::kOk) continue;
      total = static_cast<uint16_t>(total + ttf::advance(*face.outline, glyph, px));
    }
    return total;
  }
  const uint16_t step = static_cast<uint16_t>((face.width + face.gap) * scale);
  uint16_t chars = 0;
  for (const char* at = text; *at != '\0'; ++at) ++chars;
  // The last glyph has no separation after it.
  return static_cast<uint16_t>(chars * step - face.gap * scale);
}

uint16_t width(const char* text, uint8_t scale) { return width(system_face(), text, scale); }

// ---- a face of outlines, drawn as runs (docs/30) ----
//
// The other two kinds of face are tables of bits and this one is not: a glyph of a TrueType file is a drawing
// that only becomes pixels at a size. So the block is drawn here, one glyph at a time, into a block of rows
// that is the biggest glyph this system draws in one piece, and what comes out of it are **the same runs as
// ever** —so the retall, the dany and the composition below do not learn anything new (docs/90).
namespace {

const uint16_t kOutlineMaxWidth = 64;
const uint16_t kOutlineMaxHeight = 64;
const uint16_t kOutlineRowBytes = kOutlineMaxWidth / 8;
uint8_t g_outline_rows[kOutlineRowBytes * kOutlineMaxHeight];

uint16_t outline_runs(const Face& face, const char* text, int32_t x, int32_t y, uint8_t scale,
                      const Clip& clip, RunFn fn, void* user) {
  if (text == nullptr || scale == 0 || fn == nullptr) return 0;
  const ttf::Face& font = *face.outline;
  const uint16_t px = static_cast<uint16_t>(kOutlineBody * scale);
  const uint32_t body = (font.units_per_em == 0) ? 1 : font.units_per_em;
  const int32_t ascent = static_cast<int32_t>((static_cast<int32_t>(font.ascent) * px) / static_cast<int32_t>(body));
  int32_t pen = x;
  for (const char* at = text; *at != '\0'; ++at) {
    const uint8_t code = static_cast<uint8_t>(*at);
    if (code < 0x20 || code > 0x7F) continue;
    uint16_t glyph = 0;
    if (ttf::glyph_of(font, code, glyph) != ttf::kOk) continue;
    ttf::Bitmap box;
    const ttf::Status drawn =
        ttf::render(font, glyph, px, g_outline_rows, static_cast<uint8_t>(kOutlineRowBytes),
                    kOutlineMaxWidth, kOutlineMaxHeight, box);
    if (drawn == ttf::kOk) {
      // **The box says where the ink lands**: `left` from the pen and `top` from the baseline, so the rows walk
      // down from there and every run of lit pixels is one rectangle of one row (docs/30).
      for (uint16_t row = 0; row < box.height; ++row) {
        const int32_t top = y + ascent + box.top + static_cast<int32_t>(row);
        uint16_t column = 0;
        while (column < box.width) {
          const uint8_t bits = g_outline_rows[row * kOutlineRowBytes + (column >> 3)];
          if ((bits & (0x80u >> (column & 7))) == 0) {
            ++column;
            continue;
          }
          uint16_t run = column;
          while (run < box.width &&
                 (g_outline_rows[row * kOutlineRowBytes + (run >> 3)] & (0x80u >> (run & 7))) != 0) {
            ++run;
          }
          int32_t left = pen + box.left + static_cast<int32_t>(column);
          int32_t right = pen + box.left + static_cast<int32_t>(run);
          if (left < clip.x0) left = clip.x0;
          if (right > clip.x1) right = clip.x1;
          if (left < right && top >= clip.y0 && top + 1 <= clip.y1) fn(user, left, top, right - left, 1);
          column = run;
        }
      }
    }
    pen += ttf::advance(font, glyph, px);
  }
  return width(face, text, scale);
}

}  // namespace

uint16_t runs(const Face& face, const char* text, int32_t x, int32_t y, uint8_t scale, const Clip& clip,
              RunFn fn, void* user) {
  if (face.outline != nullptr) return outline_runs(face, text, x, y, scale, clip, fn, user);
  if (text == nullptr || scale == 0 || fn == nullptr || face.bits == nullptr || face.lit == nullptr) {
    return 0;
  }
  const int32_t step = (face.width + face.gap) * scale;
  int32_t at_x = x;
  for (const char* at = text; *at != '\0'; ++at) {
    // **A code the face does not carry is a gap**: the text still advances, so a partial face is readable
    // and a letter nobody drew does not shift the rest (docs/30).
    const uint8_t code = (face.code != nullptr) ? face.code(static_cast<uint8_t>(*at)) : *at;
    if (has(face, code)) {
      const uint8_t index = static_cast<uint8_t>(code - face.first);
      for (uint8_t row = 0; row < face.height; ++row) {
        uint8_t column = 0;
        while (column < face.width) {
          if (!face.lit(face.bits, index, column, row)) {
            ++column;
            continue;
          }
          // One rectangle per run of lit columns: a glyph is a few rectangles and not thirty-five.
          uint8_t run = column;
          while (run < face.width && face.lit(face.bits, index, run, row)) ++run;
          // **The run is cut against the border of the box**, column by column, so a letter that hangs out
          // of a label loses the part that is outside and nothing else (docs/30).
          int32_t left = at_x + static_cast<int32_t>(column) * scale;
          int32_t right = at_x + static_cast<int32_t>(run) * scale;
          const int32_t top = y + static_cast<int32_t>(row) * scale;
          const int32_t bottom = top + scale;
          if (left < clip.x0) left = clip.x0;
          if (right > clip.x1) right = clip.x1;
          if (left < right && top >= clip.y0 && bottom <= clip.y1) {
            fn(user, left, top, right - left, scale);
          }
          column = run;
        }
      }
    }
    at_x += step;
  }
  return width(face, text, scale);
}

uint16_t runs(const char* text, int32_t x, int32_t y, uint8_t scale, const Clip& clip, RunFn fn,
              void* user) {
  return runs(system_face(), text, x, y, scale, clip, fn, user);
}

// ---- a face from a file (docs/30) ----

namespace {

// One line of the file, without its line break, and where the next one starts.
uint32_t take_line(const char* text, uint32_t length, uint32_t from, uint32_t& line_length) {
  uint32_t end = from;
  while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
  line_length = end - from;
  uint32_t next = end;
  while (next < length && (text[next] == '\n' || text[next] == '\r')) ++next;
  return next;
}

bool starts_with(const char* line, uint32_t length, const char* prefix) {
  uint32_t i = 0;
  for (; prefix[i] != '\0'; ++i) {
    if (i >= length || line[i] != prefix[i]) return false;
  }
  return true;
}

// **A number after a directive**, which is what `width 5` means: one to three digits and then the end of
// the line, or the file is not saying a number (docs/30).
bool number_after(const char* line, uint32_t length, uint32_t skip, int32_t& out) {
  uint32_t at = skip;
  while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
  if (at >= length) return false;
  int32_t value = 0;
  uint32_t digits = 0;
  while (at < length && line[at] >= '0' && line[at] <= '9' && digits < 4) {
    value = value * 10 + (line[at] - '0');
    ++at;
    ++digits;
  }
  if (digits == 0) return false;
  while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
  if (at != length) return false;
  out = value;
  return true;
}

}  // namespace

const char* load(const char* text, uint32_t length, uint8_t* bits, Face& out) {
  if (text == nullptr || bits == nullptr || length == 0) return "there is nothing to read";
  for (uint32_t i = 0; i < kFaceBytes; ++i) bits[i] = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t gap = 1;
  int32_t first = -1;
  int32_t count = 0;
  bool magic = false;
  int32_t code = -1;  // the glyph being read, or -1
  int32_t row = 0;
  uint32_t at = 0;
  while (at < length) {
    uint32_t line_length = 0;
    const char* line = text + at;
    at = take_line(text, length, at, line_length);
    if (line_length == 0 || line[0] == ';') continue;  // a blank line and a comment say nothing
    if (!magic) {
      if (!starts_with(line, line_length, "lunok-font 1")) return "not a lunok font";
      magic = true;
      continue;
    }
    // **The rows of a glyph come first**: while a glyph is open, a line is art and not a directive.
    if (code >= 0 && row < height) {
      const uint8_t index = static_cast<uint8_t>(code - first);
      for (uint32_t x = 0; x < line_length && x < static_cast<uint32_t>(width); ++x) {
        if (line[x] == '#') {
          bits[static_cast<uint32_t>(index) * kMaxGlyphHeight + row] |=
              static_cast<uint8_t>(1u << (7 - static_cast<uint8_t>(x)));
        }
      }
      ++row;
      continue;
    }
    if (code >= 0 && row < height) return "a glyph has fewer rows than the face is tall";
    if (code >= 0 && row < height) return "a glyph has fewer rows than the face is tall";
    if (starts_with(line, line_length, "width ")) {
      if (number_after(line, line_length, 6, width) == false) return "width is not a number";
      if (width < 1 || width > kMaxGlyphWidth) return "width is one to eight";
      if (code >= 0) return "width comes before the glyphs";
      continue;
    }
    if (starts_with(line, line_length, "height ")) {
      if (number_after(line, line_length, 7, height) == false) return "height is not a number";
      if (height < 1 || height > kMaxGlyphHeight) return "height is one to eight";
      if (code >= 0) return "height comes before the glyphs";
      continue;
    }
    if (starts_with(line, line_length, "gap ")) {
      if (number_after(line, line_length, 4, gap) == false) return "gap is not a number";
      if (gap < 0 || gap > 4) return "gap is zero to four";
      continue;
    }
    if (starts_with(line, line_length, "char ")) {
      int32_t wanted = 0;
      if (number_after(line, line_length, 5, wanted) == false) return "char is not a number";
      // **A glyph that was left half-read is not a face**: the next one would be drawn with somebody else's
      // rows, which is the kind of mistake a text file makes once and nobody finds (docs/30).
      if (code >= 0 && row < height) return "a glyph has fewer rows than the face is tall";
      if (width <= 0 || height <= 0) return "a glyph needs width and height first";
      if (wanted < 0x20 || wanted > 0x7F) return "a code is 0x20 to 0x7F";
      if (count >= kMaxGlyphs) return "a face is at most ninety-six glyphs";
      if (first < 0) {
        first = wanted;
      } else if (wanted != first + count) {
        // **The glyphs come in order, one by one**: a hole in the middle is a table with a lie in it, and
        // that is the kind of thing a face is not allowed to have (docs/30).
        return "the glyphs of a face come in order";
      }
      code = wanted;
      row = 0;
      ++count;
      continue;
    }
    return "a line that is not a directive";
  }
  if (!magic) return "not a lunok font";
  if (width <= 0 || height <= 0) return "a face needs a width and a height";
  if (count == 0) return "a face with no glyph";
  if (code >= 0 && row < height) return "the last glyph has fewer rows than the face is tall";
  out.width = static_cast<uint8_t>(width);
  out.height = static_cast<uint8_t>(height);
  out.gap = static_cast<uint8_t>(gap);
  out.first = static_cast<uint8_t>(first);
  out.count = static_cast<uint8_t>(count);
  out.bits = bits;
  out.lit = &file_lit;
  out.code = &file_code;
  out.outline = nullptr;
  return nullptr;
}

namespace {

// The sink of a surface: a run is one `fill_rect` of the level the caller asked for.
struct SurfaceSink {
  hal::ISurface* surface;
  uint8_t level;
};

void surface_run(void* user, int32_t x, int32_t y, int32_t w, int32_t h) {
  SurfaceSink* sink = static_cast<SurfaceSink*>(user);
  sink->surface->fill_rect(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                           static_cast<uint16_t>(w), static_cast<uint16_t>(h), sink->level);
}

}  // namespace

uint16_t draw(hal::ISurface& surface, uint16_t x, uint16_t y, const char* text, uint8_t scale,
              uint8_t level, const Clip& clip) {
  SurfaceSink sink = {&surface, level};
  return runs(text, x, y, scale, clip, surface_run, &sink);
}

// ---- a face from a BDF (docs/30) ----
//
// The Bitmap Distribution Format is the one an X server read: `STARTFONT`, a bounding box, and then one block
// per glyph with its rows in hexadecimal. It is the simplest real font format there is, and the same eight by
// eight block comes out of it as out of a `.lfont`, so nothing that draws a text learns which one it got.
namespace {

// The two helpers of the line-by-line readers of this file, which are defined with the first of them below.
uint32_t take_line(const char* text, uint32_t length, uint32_t from, uint32_t& line_length);
bool starts_with(const char* line, uint32_t length, const char* prefix);

// One hexadecimal byte of a BDF row: two digits and nothing after them.
bool hex_byte(const char* line, uint32_t length, uint8_t& out) {
  uint32_t at = 0;
  while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
  if (at + 2 > length) return false;
  const auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  const int high = digit(line[at]);
  const int low = digit(line[at + 1]);
  if (high < 0 || low < 0) return false;
  out = static_cast<uint8_t>((high << 4) | low);
  return true;
}

// The numbers of a line after its keyword, one by one: `BBX 8 8 0 0` is four of them.
uint32_t numbers_after(const char* line, uint32_t length, uint32_t skip, int32_t* out, uint32_t most) {
  uint32_t at = skip;
  uint32_t found = 0;
  while (found < most) {
    while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
    if (at >= length) break;
    bool negative = false;
    if (line[at] == '-') {
      negative = true;
      ++at;
    }
    int32_t value = 0;
    uint32_t digits = 0;
    while (at < length && line[at] >= '0' && line[at] <= '9') {
      value = value * 10 + (line[at] - '0');
      ++at;
      ++digits;
    }
    if (digits == 0) break;
    out[found++] = negative ? -value : value;
  }
  return found;
}

}  // namespace

const char* load_bdf(const char* text, uint32_t length, uint8_t* bits, Face& out) {
  if (text == nullptr || bits == nullptr || length == 0) return "there is nothing to read";
  for (uint32_t i = 0; i < kFaceBytes; ++i) bits[i] = 0;
  int32_t box_width = 0;
  int32_t box_height = 0;
  int32_t first = -1;
  int32_t count = 0;
  int32_t code = -1;
  bool magic = false;
  bool in_properties = false;
  int32_t rows_left = 0;
  int32_t row = 0;
  int32_t glyph_height = 0;
  uint32_t at = 0;
  while (at < length) {
    uint32_t line_length = 0;
    const char* line = text + at;
    at = take_line(text, length, at, line_length);
    if (line_length == 0) continue;
    if (!magic) {
      // A `COMMENT` may come first —the format allows it— and it says nothing about the file's being one.
      if (starts_with(line, line_length, "COMMENT")) continue;
      if (!starts_with(line, line_length, "STARTFONT")) return "not a bdf";
      magic = true;
      continue;
    }
    // The properties are lines of their own and none of them says anything about a glyph.
    if (starts_with(line, line_length, "STARTPROPERTIES")) {
      in_properties = true;
      continue;
    }
    if (starts_with(line, line_length, "ENDPROPERTIES")) {
      in_properties = false;
      continue;
    }
    if (in_properties || starts_with(line, line_length, "COMMENT")) continue;
    // **The rows of a glyph come first**: while a `BITMAP` is open, a line is hexadecimal and not a keyword.
    if (rows_left > 0) {
      uint8_t byte = 0;
      if (!hex_byte(line, line_length, byte)) return "a row of a bdf bitmap is not hexadecimal";
      if (row < kMaxGlyphHeight) {
        bits[static_cast<uint32_t>(code - first) * kMaxGlyphHeight + row] = byte;
      }
      ++row;
      --rows_left;
      continue;
    }
    if (starts_with(line, line_length, "FONTBOUNDINGBOX")) {
      int32_t numbers[4] = {0, 0, 0, 0};
      if (numbers_after(line, line_length, 15, numbers, 4) < 2) return "the bounding box is not a box";
      box_width = numbers[0];
      box_height = numbers[1];
      if (box_width < 1 || box_width > kMaxGlyphWidth) return "a bdf glyph is up to eight columns wide";
      if (box_height < 1 || box_height > kMaxGlyphHeight) return "a bdf glyph is up to eight rows tall";
      continue;
    }
    if (starts_with(line, line_length, "ENCODING")) {
      int32_t numbers[2] = {-1, -1};
      const uint32_t given = numbers_after(line, line_length, 8, numbers, 2);
      // `ENCODING -1 <n>` is what a font writes for a code that is only in its second table: the second
      // number is the one that counts (docs/30).
      const int32_t wanted = (given == 2 && numbers[0] < 0) ? numbers[1] : numbers[0];
      if (wanted < 0x20 || wanted > 0x7F) return "a code is 0x20 to 0x7F";
      if (count >= kMaxGlyphs) return "a face is at most ninety-six glyphs";
      if (first < 0) {
        first = wanted;
      } else if (wanted != first + count) {
        return "the glyphs of a face come in order";
      }
      code = wanted;
      ++count;
      continue;
    }
    if (starts_with(line, line_length, "BBX")) {
      int32_t numbers[4] = {0, 0, 0, 0};
      if (numbers_after(line, line_length, 3, numbers, 4) < 2) return "a glyph box is not a box";
      if (numbers[0] < 0 || numbers[0] > kMaxGlyphWidth) return "a bdf glyph is up to eight columns wide";
      if (numbers[1] < 0 || numbers[1] > kMaxGlyphHeight) return "a bdf glyph is up to eight rows tall";
      glyph_height = numbers[1];
      continue;
    }
    if (starts_with(line, line_length, "BITMAP")) {
      if (code < 0) return "a bitmap with no code before it";
      row = 0;
      // The rows are the glyph's own box when it says one, and the face's when it does not.
      rows_left = (glyph_height > 0) ? glyph_height : box_height;
      continue;
    }
    if (starts_with(line, line_length, "ENDCHAR")) {
      if (rows_left != 0) return "a glyph has fewer rows than it says";
      code = -1;
      row = 0;
      glyph_height = 0;
      continue;
    }
    // DWIDTH, SWIDTH, CHARS, ENDFONT and the rest say nothing this reader needs.
  }
  if (!magic) return "not a bdf";
  if (box_width <= 0 || box_height <= 0) return "a bdf needs a bounding box";
  if (count == 0) return "a face with no glyph";
  if (rows_left != 0) return "the last glyph has fewer rows than it says";
  out.width = static_cast<uint8_t>(box_width);
  out.height = static_cast<uint8_t>(box_height);
  out.gap = 1;
  out.first = static_cast<uint8_t>(first);
  out.count = static_cast<uint8_t>(count);
  out.bits = bits;
  out.lit = &file_lit;
  out.code = &file_code;
  out.outline = nullptr;
  return nullptr;
}

// ---- a face from a PCF (docs/30) ----
//
// PCF is a BDF compiled: the same tables in binary, and the numbers of a table in the byte order that the
// table itself says. What a page needs is three of them —the metrics, the bitmaps and the encodings— and
// **a table this reader does not know is refused rather than guessed**, which is the rule everywhere here.
namespace {

constexpr uint32_t kPcfMetricsTable = 0x04;
constexpr uint32_t kPcfBitmapsTable = 0x08;
constexpr uint32_t kPcfEncodingsTable = 0x20;
constexpr uint32_t kPcfByteOrderMsb = 0x04;
constexpr uint32_t kPcfBitOrderMsb = 0x08;
constexpr uint32_t kPcfCompressedMetrics = 0x0100;

// **The table directory is little-endian always**, because it has to be read before a table's own order is
// known; the numbers inside a table follow the table's format.
uint32_t pcf_u32(const char* at, bool big) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(at);
  if (big) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
  }
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int32_t pcf_i16(const char* at, bool big) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(at);
  const uint32_t raw = big ? ((static_cast<uint32_t>(p[0]) << 8) | static_cast<uint32_t>(p[1]))
                           : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8));
  return static_cast<int32_t>(static_cast<int16_t>(raw));
}

// The number at an offset of a table, in the order that table says, and only if the whole of it is there.
// **The check is a subtraction and not a sum**: a number of a file that says `0xFFFFFFFF` would wrap around to
// a small offset and be believed (the suite caught this in 0.40.0, docs/30).
bool pcf_number(const char* text, uint32_t length, uint32_t at, uint32_t size, bool big, int32_t& value) {
  if (at > length || length - at < size) return false;
  value = (size == 2) ? pcf_i16(text + at, big) : static_cast<int32_t>(pcf_u32(text + at, big));
  return true;
}

}  // namespace

const char* load_pcf(const char* text, uint32_t length, uint8_t* bits, Face& out) {
  if (text == nullptr || bits == nullptr || length == 0) return "there is nothing to read";
  if (length < 8 || static_cast<uint8_t>(text[0]) != 0x01 || text[1] != 'f' || text[2] != 'c' ||
      text[3] != 'p') {
    return "not a pcf";
  }
  for (uint32_t i = 0; i < kFaceBytes; ++i) bits[i] = 0;
  const uint32_t tables = pcf_u32(text + 4, false);
  if (tables == 0 || 8 + tables * 16 > length) return "the table directory is cut";
  int32_t metrics_at = -1;
  int32_t bitmaps_at = -1;
  int32_t encodings_at = -1;
  for (uint32_t i = 0; i < tables; ++i) {
    const uint32_t entry = 8 + i * 16;
    const uint32_t type = pcf_u32(text + entry, false);
    const uint32_t offset = pcf_u32(text + entry + 12, false);
    if (offset > length || length - offset < 4) return "a table starts past the end of the file";
    if (type == kPcfMetricsTable) {
      metrics_at = static_cast<int32_t>(offset);
    } else if (type == kPcfBitmapsTable) {
      bitmaps_at = static_cast<int32_t>(offset);
    } else if (type == kPcfEncodingsTable) {
      encodings_at = static_cast<int32_t>(offset);
    }
  }
  if (metrics_at < 0 || bitmaps_at < 0 || encodings_at < 0) {
    return "a pcf needs its metrics, its bitmaps and its encodings";
  }
  const bool metrics_big = (pcf_u32(text + metrics_at, false) & kPcfByteOrderMsb) != 0;
  const uint32_t bitmaps_format = pcf_u32(text + bitmaps_at, false);
  const bool bitmaps_big = (bitmaps_format & kPcfByteOrderMsb) != 0;
  const bool encodings_big = (pcf_u32(text + encodings_at, false) & kPcfByteOrderMsb) != 0;
  // **The bits of a glyph are read most significant first**, which is the way this system's own face is
  // stored; a file that says otherwise is refused instead of mirrored (docs/30).
  if ((bitmaps_format & kPcfBitOrderMsb) == 0) return "a pcf with its bits the other way is not read";
  const uint32_t glyph_pad = 1u << (bitmaps_format & 0x03u);
  if ((bitmaps_format & 0x30u) != 0) return "a pcf whose glyphs are a scan unit wide is not read";

  // ---- the encodings: one byte of code, or nothing ----
  int32_t range[5] = {0, 0, 0, 0, 0};
  for (uint32_t i = 0; i < 5; ++i) {
    if (!pcf_number(text, length, static_cast<uint32_t>(encodings_at) + 4 + i * 2, 2, encodings_big,
                    range[i])) {
      return "the encodings are cut";
    }
  }
  if (range[2] != 0 || range[3] != 0) return "a pcf of two-byte codes is not read";
  const int32_t low = range[0];
  const int32_t high = range[1];
  if (high < low) return "the encodings are not a range";
  if (static_cast<uint32_t>(high - low) + 1 > kMaxGlyphs) return "a face is at most ninety-six glyphs";

  // ---- the metrics, compressed to five signed bytes to a glyph or not ----
  const bool compressed = (pcf_u32(text + metrics_at, false) & kPcfCompressedMetrics) != 0;
  int32_t metrics_count = 0;
  uint32_t metrics_step = compressed ? 5 : 10;
  if (!pcf_number(text, length, static_cast<uint32_t>(metrics_at) + 4, compressed ? 2 : 4, metrics_big,
                  metrics_count)) {
    return "the metrics are cut";
  }
  const uint32_t metrics_first = static_cast<uint32_t>(metrics_at) + (compressed ? 6 : 8);

  // ---- the bitmaps ----
  int32_t glyph_count = 0;
  if (!pcf_number(text, length, static_cast<uint32_t>(bitmaps_at) + 4, 4, bitmaps_big, glyph_count)) {
    return "the bitmaps are cut";
  }
  if (glyph_count <= 0) return "a pcf with no glyph";
  if (static_cast<uint32_t>(glyph_count) > length / 4) return "the bitmaps are cut";
  const uint32_t offsets_first = static_cast<uint32_t>(bitmaps_at) + 8;
  const uint32_t data_first = offsets_first + static_cast<uint32_t>(glyph_count) * 4 + 16;
  if (data_first > length) return "the bitmaps are cut";

  int32_t first = -1;
  int32_t count = 0;
  uint32_t widest = 0;
  uint32_t tallest = 0;
  for (int32_t code = low; code <= high; ++code) {
    // **The face this system draws is printable ascii** and nothing else, so what is outside it is not a
    // hole and is not read (docs/30).
    if (code < 0x20 || code > 0x7F) continue;
    int32_t glyph = 0;
    if (!pcf_number(text, length, static_cast<uint32_t>(encodings_at) + 14 + (code - low) * 2, 2,
                    encodings_big, glyph)) {
      return "the encodings are cut";
    }
    if (glyph < 0 || glyph == 0x7FFF) continue;  // a code the font does not have
    if (glyph >= glyph_count || glyph >= metrics_count) return "a code points past the glyphs";
    const uint32_t at = metrics_first + static_cast<uint32_t>(glyph) * metrics_step;
    if (at > length || length - at < metrics_step) return "the metrics are cut";
    int32_t metric[5] = {0, 0, 0, 0, 0};
    if (compressed) {
      // A compressed metric is the value with 0x80 added to it, and that is how a byte holds a minus one.
      for (uint32_t i = 0; i < 5; ++i) {
        metric[i] = static_cast<int32_t>(static_cast<uint8_t>(text[at + i])) - 0x80;
      }
    } else {
      for (uint32_t i = 0; i < 5; ++i) {
        metric[i] = pcf_i16(text + at + i * 2, metrics_big);
      }
    }
    const int32_t box_width = metric[1] - metric[0];  // right bearing less left bearing
    const int32_t box_height = (metric[3] > 0 ? metric[3] : 0) + (metric[4] > 0 ? metric[4] : 0);
    if (box_width < 0 || box_width > kMaxGlyphWidth) return "a pcf glyph is up to eight columns wide";
    if (box_height < 0 || box_height > kMaxGlyphHeight) return "a pcf glyph is up to eight rows tall";
    // The codes come in order, one by one, like they do in a `.lfont` and in a bdf.
    if (first < 0) {
      first = code;
    } else if (code != first + count) {
      return "the glyphs of a face come in order";
    }
    int32_t offset = 0;
    if (!pcf_number(text, length, offsets_first + static_cast<uint32_t>(glyph) * 4, 4, bitmaps_big,
                    offset)) {
      return "the bitmaps are cut";
    }
    const uint32_t row_bytes = (static_cast<uint32_t>(box_width) + 7) / 8;
    if (row_bytes > 0) {
      const uint32_t stride = (row_bytes + glyph_pad - 1) / glyph_pad * glyph_pad;
      for (uint32_t row = 0; row < static_cast<uint32_t>(box_height); ++row) {
        const uint32_t byte_at = data_first + static_cast<uint32_t>(offset) + row * stride;
        if (byte_at + row_bytes > length) return "a glyph is cut";
        // **The leftmost pixel is the top bit of the first byte**, which is the layout of `file_lit`.
        bits[static_cast<uint32_t>(count) * kMaxGlyphHeight + row] =
            static_cast<uint8_t>(text[byte_at]);
      }
    }
    if (static_cast<uint32_t>(box_width) > widest) widest = static_cast<uint32_t>(box_width);
    if (static_cast<uint32_t>(box_height) > tallest) tallest = static_cast<uint32_t>(box_height);
    ++count;
  }
  if (count == 0) return "a face with no glyph";
  out.width = static_cast<uint8_t>(widest > 0 ? widest : 1);
  out.height = static_cast<uint8_t>(tallest > 0 ? tallest : 1);
  out.gap = 1;
  out.first = static_cast<uint8_t>(first);
  out.count = static_cast<uint8_t>(count);
  out.bits = bits;
  out.lit = &file_lit;
  out.code = &file_code;
  out.outline = nullptr;
  return nullptr;
}

const char* load_ttf(const char* text, uint32_t length, ttf::Face& storage, Face& out) {
  if (text == nullptr || length == 0) return "there is nothing to read";
  const char* why = ttf::open(reinterpret_cast<const uint8_t*>(text), length, storage);
  if (why != nullptr) return why;
  // **A face of outlines has no cell and no codes of its own**: the size is the body of the file at scale one,
  // the advance is the file's, and every glyph is drawn when a text is drawn (docs/30).
  out.width = 0;
  out.height = kOutlineBody;
  out.gap = 0;
  out.first = 0x20;
  out.count = 0;
  out.bits = nullptr;
  out.lit = nullptr;
  out.code = nullptr;
  out.outline = &storage;
  return nullptr;
}

bool looks_like_ttf(const char* text, uint32_t length) {
  if (text == nullptr || length < 4) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
  const bool truetype = p[0] == 0x00 && p[1] == 0x01 && p[2] == 0x00 && p[3] == 0x00;
  const bool sfnt_true = p[0] == 't' && p[1] == 'r' && p[2] == 'u' && p[3] == 'e';
  const bool cff = p[0] == 'O' && p[1] == 'T' && p[2] == 'T' && p[3] == 'O';
  return truetype || sfnt_true || cff;
}

const char* load_any(const char* text, uint32_t length, uint8_t* bits, Face& out) {
  if (text == nullptr || length == 0) return "there is nothing to read";
  // **A PCF is binary and its first four bytes are its own**, so there is no first line to read in it.
  if (length >= 4 && static_cast<uint8_t>(text[0]) == 0x01 && text[1] == 'f' && text[2] == 'c' &&
      text[3] == 'p') {
    return load_pcf(text, length, bits, out);
  }
  // **What the file says of itself decides the reader**, and nothing else: the first line of a lunok font and
  // the first line of a bdf cannot be mistaken for one another (docs/30). Blank lines, the `;` comments of a
  // lunok font and the `COMMENT` lines of a bdf come before that first line like they do in both readers.
  uint32_t at = 0;
  while (at < length) {
    uint32_t line_length = 0;
    const char* line = text + at;
    at = take_line(text, length, at, line_length);
    if (line_length == 0 || line[0] == ';' || starts_with(line, line_length, "COMMENT")) continue;
    if (starts_with(line, line_length, "STARTFONT")) return load_bdf(text, length, bits, out);
    if (starts_with(line, line_length, "lunok-font")) return load(text, length, bits, out);
    break;
  }
  return "not a font: neither a lunok font, a bdf nor a pcf";
}

}  // namespace font
}  // namespace kernel
