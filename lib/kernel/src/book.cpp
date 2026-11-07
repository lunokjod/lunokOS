// **A book open** (docs/46, A5) — the file, the table of its entries, the chapter in memory and the page.
#include "kernel/book.h"

#include <string.h>

#include "hal/hal.h"
#include "kernel/font.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace book {

namespace {

// **The state of the one book**, and it is a static and not an object because there is one reader: the blocks are
// half a megabyte each and they belong to the board, not to whoever asked (docs/46, A7).
struct State {
  bool open = false;
  hal::IStorage* storage = nullptr;
  hal::FileSlot file;
  zip::Source source;
  zip::Archive archive;
  zip::Entry* table = nullptr;
  uint8_t* packed = nullptr;
  uint8_t* chapter = nullptr;
  epub::Item* items = nullptr;
  char* words = nullptr;
  uint32_t bytes = 0;  // the size of the file, which is what the zip asks for first
  int32_t chapter_bytes = 0;
  char mount[16];
  char rel[160];
};

State g;

// **The file behind the zip**: the zip asks for its size and for bytes at an offset, and a file of a mount asks
// to be seeked and read (docs/46, A1). A short read is a file that is not what its header said, and the zip reader
// is told so with a -1 instead of being given bytes that are not the ones it asked for.
int64_t file_size(void* ctx) { return static_cast<int64_t>(static_cast<State*>(ctx)->bytes); }

int32_t file_read_at(void* ctx, uint32_t offset, uint8_t* dst, uint32_t len) {
  State* state = static_cast<State*>(ctx);
  if (!state->storage->seek(state->file, offset)) return -1;
  const int32_t got = state->storage->read(state->file, dst, len);
  if (got < 0 || static_cast<uint32_t>(got) != len) return -1;

  return got;
}

// **The face of a role**: the one the caller gave, or the system's, which is why a book with no face of its own
// still reads (docs/46, C13).
const font::Face& face_in(const Fonts& fonts, uint8_t face) {
  if (face == epub::kFaceTitle && fonts.title != nullptr) return *fonts.title;
  if (face == epub::kFaceSmall && fonts.small != nullptr) return *fonts.small;
  if (fonts.body != nullptr) return *fonts.body;

  return font::system_face();
}

// The two numbers a page asks of a face: how wide a word is and how tall a line of it is, which is the height of
// the face times the step (docs/30).
int32_t width_of(void* ctx, uint8_t face, uint8_t scale, const char* text) {
  return font::width(face_in(*static_cast<const Fonts*>(ctx), face), text, scale);
}

int32_t line_of(void* ctx, uint8_t face, uint8_t scale) {
  const font::Face& chosen = face_in(*static_cast<const Fonts*>(ctx), face);
  const uint8_t step = (chosen.height != 0) ? chosen.height : font::kGlyphHeight;

  return static_cast<int32_t>(step) * scale;
}

// **The words of a refusal**, which are the zip's and are said as they are: a book that is not a book, a made-up
// zip64, an entry with a password, a compression this reader does not know and a checksum that does not match are
// five different things and a person has to be told which one it is (docs/46, A1).
const char* reason_of(zip::Status status) {
  switch (status) {
    case zip::kOk: return nullptr;
    case zip::kNotAZip: return "the file is not a book";
    case zip::kSplit: return "the book is a zip of several volumes";
    case zip::kZip64: return "the book needs zip64, which this reader does not read";
    case zip::kDirectory: return "the contents of the book are damaged";
    case zip::kNoRoom: return "the book has more files than this reader holds";
    case zip::kBadData: return "a piece of the book is cut";
    case zip::kEncrypted: return "the book is locked with a password";
    case zip::kMethod: return "the book is compressed in a way this reader does not know";
    case zip::kCrc: return "a piece of the book does not match its checksum";
  }

  return "the book cannot be opened";
}

// **The names that are a chapter of a book**, which is the fallback of docs/46 C2: an `.xhtml`, an `.html` or an
// `.htm` that is not the `META-INF` of the container.
bool looks_like_chapter(const char* name) {
  if (strncmp(name, "META-INF", 8) == 0) return false;

  const char* dot = strrchr(name, '.');
  if (dot == nullptr) return false;

  return strcmp(dot, ".xhtml") == 0 || strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0;
}

// Everything the book took, given back. It is what a failed `open` does too, so a book that is not a book leaves
// the extra memory as it found it.
void release_blocks() {
  if (g.words != nullptr) pal::extra_memory().release(g.words);
  if (g.items != nullptr) pal::extra_memory().release(g.items);
  if (g.chapter != nullptr) pal::extra_memory().release(g.chapter);
  if (g.packed != nullptr) pal::extra_memory().release(g.packed);
  if (g.table != nullptr) pal::extra_memory().release(g.table);

  g.words = nullptr;
  g.items = nullptr;
  g.chapter = nullptr;
  g.packed = nullptr;
  g.table = nullptr;
}

}  // namespace

bool is_open() { return g.open; }

void close() {
  if (g.open && g.storage != nullptr) g.storage->close(g.file);

  g.open = false;
  g.storage = nullptr;
  g.archive = zip::Archive();
  g.chapter_bytes = 0;
  g.mount[0] = 0;
  g.rel[0] = 0;
  release_blocks();
}

const char* open(const char* path) {
  close();

  if (path == nullptr || path[0] != '/') return "the path of a book has to be absolute";
  const char* second = strchr(path + 1, '/');
  if (second == nullptr || second[1] == 0) return "the path names no file";

  const int32_t mount_len = static_cast<int32_t>(second - path);
  if (mount_len >= static_cast<int32_t>(sizeof(g.mount))) return "the unit of the book has a name too long";
  memcpy(g.mount, path, static_cast<size_t>(mount_len));
  g.mount[mount_len] = 0;

  const size_t rel_len = strlen(second);
  if (rel_len >= sizeof(g.rel)) return "the name of the book is too long";
  memcpy(g.rel, second, rel_len + 1);

  hal::IStorage* storage = hal::platform().storage(g.mount);
  if (storage == nullptr) return "that unit is not mounted";

  hal::Stat info;
  if (!storage->stat(g.rel, info) || info.type != hal::NodeType::kFile) return "there is no such file";
  if (!storage->open(g.rel, hal::OpenMode::kRead, g.file)) return "the book cannot be opened";

  g.storage = storage;
  // **The size of the file is the one the mount said**, and not the one the slot carries: a slot is a handle and
  // its size is the backend's business, while a zip asks for the length of the file before it asks anything else.
  g.bytes = info.size;

  // **The blocks come before the table is read**, and the table is the one that says whether this is a book at
  // all: a file that is not one gives everything back.
  g.table = static_cast<zip::Entry*>(pal::extra_memory().allocate(sizeof(zip::Entry) * kEntries, true));
  g.packed = static_cast<uint8_t*>(pal::extra_memory().allocate(kChapterBytes, true));
  g.chapter = static_cast<uint8_t*>(pal::extra_memory().allocate(kChapterBytes, true));
  g.items = static_cast<epub::Item*>(pal::extra_memory().allocate(sizeof(epub::Item) * kItems, true));
  g.words = static_cast<char*>(pal::extra_memory().allocate(kWords, true));

  if (g.table == nullptr || g.packed == nullptr || g.chapter == nullptr || g.items == nullptr ||
      g.words == nullptr) {
    close();
    return "there is no memory for a book";
  }

  g.source.ctx = &g;
  g.source.size = file_size;
  g.source.read_at = file_read_at;

  uint16_t count = 0;
  const zip::Status status = g.archive.open(g.source, g.table, kEntries, count);
  if (status != zip::kOk) {
    close();
    return reason_of(status);
  }

  g.open = true;
  g.chapter_bytes = 0;

  return nullptr;
}

int32_t entries() { return g.open ? static_cast<int32_t>(g.archive.count()) : 0; }

const zip::Entry* entry(int32_t index) {
  if (!g.open || index < 0 || index >= static_cast<int32_t>(g.archive.count())) return nullptr;

  return g.archive.at(static_cast<uint16_t>(index));
}

int32_t find(const char* name) { return g.open ? g.archive.find(name) : -1; }

int32_t first_chapter() {
  if (!g.open) return -1;

  const int32_t total = static_cast<int32_t>(g.archive.count());
  for (int32_t i = 0; i < total; ++i) {
    const zip::Entry* one = g.archive.at(static_cast<uint16_t>(i));
    if (one == nullptr || one->dir) continue;
    if (looks_like_chapter(one->name)) return i;
  }

  return -1;
}

const char* chapter_at(int32_t index, int32_t& bytes) {
  bytes = 0;
  if (!g.open) return nullptr;

  const zip::Entry* one = entry(index);
  if (one == nullptr || one->dir) return nullptr;

  uint32_t written = 0;
  if (g.archive.extract(static_cast<uint16_t>(index), g.chapter, kChapterBytes, written, g.packed,
                        kChapterBytes) != zip::kOk) {
    return nullptr;
  }

  g.chapter_bytes = static_cast<int32_t>(written);
  bytes = g.chapter_bytes;

  return reinterpret_cast<const char*>(g.chapter);
}

const char* chapter(const char* name, int32_t& bytes) {
  bytes = 0;
  if (!g.open || name == nullptr) return nullptr;

  return chapter_at(g.archive.find(name), bytes);
}

bool page(int32_t from, int32_t width, int32_t height, const epub::Style& style, const Fonts& fonts, Page& out) {
  out.count = 0;
  out.ended = false;
  out.has_picture = false;
  out.next = 0;
  out.used = 0;
  out.items = g.items;
  out.words = g.words;

  if (!g.open || g.chapter == nullptr || g.chapter_bytes == 0) return false;

  epub::Tokens tokens(reinterpret_cast<const char*>(g.chapter), g.chapter_bytes, from);

  epub::Sheet sheet;
  sheet.items = g.items;
  sheet.items_cap = kItems;
  sheet.words = g.words;
  sheet.words_cap = kWords;

  Fonts given = fonts;
  epub::Metrics metrics;
  metrics.ctx = &given;
  metrics.text_width = width_of;
  metrics.line_height = line_of;

  epub::Result result;
  if (!epub::layout(tokens, width, height, style, metrics, nullptr, sheet, result)) return false;

  out.count = result.count;
  out.ended = result.ended;
  out.has_picture = result.has_picture;
  out.next = result.next;
  out.used = result.used;

  return true;
}

namespace {

// **The names of an entry, said to the interpreter**: it asks by index and the table of the book answers as long as
// the book is open (docs/46, B6).
const char* name_of(int32_t index) {
  const zip::Entry* one = entry(index);

  return (one != nullptr) ? one->name : nullptr;
}

int32_t chapter_bytes_of(const char* name) {
  int32_t bytes = 0;
  if (chapter(name, bytes) == nullptr) return -1;

  return bytes;
}

// **A page laid out and painted in a canvas of an app**: the plan is the paginator's and the pixels are the UI's,
// and they meet here because both halves are the kernel's (docs/46, A5, B3 and D2).
bool page_into(int16_t slot, int32_t from, luabind::BookStyle& style) {
  const ui::Canvas* canvas = ui::at(slot);
  if (canvas == nullptr) return false;

  epub::Style mine;
  mine.body_scale = style.body_scale;
  mine.title_scale = style.title_scale;
  mine.small_scale = style.small_scale;
  mine.indent = style.indent;

  Fonts faces;
  faces.body = &ui::font_face(style.body);
  faces.title = &ui::font_face(style.title);
  faces.small = &ui::font_face(style.small);

  Page laid;
  if (!page(from, canvas->width, canvas->height, mine, faces, laid)) return false;

  ui::PageFonts paint;
  paint.body = style.body;
  paint.title = style.title;
  paint.small = style.small;
  if (!ui::canvas_book_page(slot, laid.items, laid.words, laid.count, paint, 0, "gc16")) return false;

  style.count = laid.count;
  style.next = laid.next;
  style.ended = laid.ended;
  style.used = laid.used;
  style.has_picture = laid.has_picture;

  return true;
}

}  // namespace

void bind() {
  luabind::BookOps ops;
  ops.open = open;
  ops.close = close;
  ops.entries = entries;
  ops.name = name_of;
  ops.first_chapter = first_chapter;
  ops.chapter = chapter_bytes_of;
  ops.page = page_into;

  luabind::set_book_ops(ops);
}

}  // namespace book
}  // namespace kernel
