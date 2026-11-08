// **A book open** (docs/46, A5) — the file, the table of its entries, the chapter in memory and the page.
#include "kernel/book.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "img/img.h"
#include "kernel/font.h"
#include "kernel/log.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"
#include "pal/pal.h"
#include "ttf/ttf.h"

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
  // **The small files of the book** (0.142.0, docs/46 B6): the container, the OPF, the index and a stylesheet. It is
  // asked for the first time an app reads one, like the blocks of a picture, and it is not the block of the chapter:
  // a page is laid out from the chapter and reading the OPF must not take it away (docs/46, A5).
  uint8_t* meta = nullptr;
  epub::Item* items = nullptr;
  char* words = nullptr;
  uint32_t bytes = 0;  // the size of the file, which is what the zip asks for first
  int32_t chapter_bytes = 0;
  char mount[16];
  char rel[160];

  // **The chapter that is open** and the one picture in a cache (docs/46, A5): the name of the chapter, because a
  // picture of a book is named relative to it, and the two blocks of a picture —the file as it comes out of the zip
  // and the pixels as the decoder hands them out— which are asked for the first time a book has one.
  char chapter_name[160];
  char picture_name[160];
  uint8_t* picture_file = nullptr;
  uint32_t picture_file_bytes = 0;
  uint8_t* picture_pixels = nullptr;
  int32_t picture_width = 0;
  int32_t picture_height = 0;
  bool picture_ready = false;

  // **The face a book carries** (0.150.0, docs/46 D6): the file as it comes out of the zip and, when that file is a
  // `.woff`, the sfnt the box has become. The second only exists when a book needs it, and both go back with the book.
  uint8_t* font_file = nullptr;
  uint32_t font_file_bytes = 0;
  uint8_t* font_sfnt = nullptr;
  uint32_t font_sfnt_bytes = 0;
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
  if (g.font_sfnt != nullptr) pal::extra_memory().release(g.font_sfnt);
  if (g.font_file != nullptr) pal::extra_memory().release(g.font_file);
  if (g.picture_pixels != nullptr) pal::extra_memory().release(g.picture_pixels);
  if (g.picture_file != nullptr) pal::extra_memory().release(g.picture_file);
  if (g.words != nullptr) pal::extra_memory().release(g.words);
  if (g.items != nullptr) pal::extra_memory().release(g.items);
  if (g.meta != nullptr) pal::extra_memory().release(g.meta);
  if (g.chapter != nullptr) pal::extra_memory().release(g.chapter);
  if (g.packed != nullptr) pal::extra_memory().release(g.packed);
  if (g.table != nullptr) pal::extra_memory().release(g.table);

  g.meta = nullptr;
  g.words = nullptr;
  g.items = nullptr;
  g.chapter = nullptr;
  g.packed = nullptr;
  g.table = nullptr;
  g.picture_pixels = nullptr;
  g.picture_file = nullptr;
  g.picture_file_bytes = 0;
  g.font_file = nullptr;
  g.font_file_bytes = 0;
  g.font_sfnt = nullptr;
  g.font_sfnt_bytes = 0;
  g.picture_width = 0;
  g.picture_height = 0;
  g.picture_ready = false;
  g.picture_name[0] = 0;
  g.chapter_name[0] = 0;
}

// **The entry of a picture, which is a name relative to the chapter it lives in** (docs/46, A5): `img/x.png` is
// `OEBPS/img/x.png` when the chapter is `OEBPS/ch01.xhtml`, and `../img/x.png` goes one folder up. A name is
// resolved and never guessed: what does not resolve is a picture that is not drawn and nothing else.
bool resolve_href(const char* href, char* out, int32_t cap) {
  if (href == nullptr || g.chapter_name[0] == 0) return false;

  char path[160];
  int32_t at = 0;
  const char* slash = strrchr(g.chapter_name, '/');
  if (slash != nullptr) {
    at = static_cast<int32_t>(slash - g.chapter_name) + 1;
    if (at >= static_cast<int32_t>(sizeof(path))) return false;
    memcpy(path, g.chapter_name, static_cast<size_t>(at));
  }
  path[at] = 0;

  const char* step = href;
  while (*step != 0) {
    const char* end = strchr(step, '/');
    const int32_t len = (end != nullptr) ? static_cast<int32_t>(end - step) : static_cast<int32_t>(strlen(step));

    if (len == 0 || (len == 1 && step[0] == '.')) {
      // this folder: nothing to do
    } else if (len == 2 && step[0] == '.' && step[1] == '.') {
      while (at > 0 && path[at - 1] != '/') --at;
      if (at > 0) --at;
      path[at] = 0;
    } else {
      if (at + len + 2 >= static_cast<int32_t>(sizeof(path))) return false;
      memcpy(path + at, step, static_cast<size_t>(len));
      at += len;
      path[at] = 0;
      if (end != nullptr) {
        path[at++] = '/';
        path[at] = 0;
      }
    }

    if (end == nullptr) break;
    step = end + 1;
  }

  if (at == 0 || at >= cap) return false;
  memcpy(out, path, static_cast<size_t>(at) + 1);

  return true;
}

// **The picture being decoded, and the box it is being put in**: the decoder hands the rows of the file and the
// page wants rows of the box, so the sink takes the row it needs and samples the columns it needs. It is the
// cheapest resampler there is and the one an e-ink page of a book deserves: a plate is a plate (docs/46, A5).
struct Fit {
  uint8_t* target;
  int32_t stride;
  int32_t width;
  int32_t height;
  int32_t source_width;
  int32_t source_height;
  int32_t last_row;
};

void fit_row(void* ctx, uint16_t y, const uint8_t* packed) {
  Fit* fit = static_cast<Fit*>(ctx);
  const int32_t row = static_cast<int32_t>(y) * fit->height / fit->source_height;
  if (row == fit->last_row || row < 0 || row >= fit->height) return;

  fit->last_row = row;
  uint8_t* line = fit->target + row * fit->stride;
  for (int32_t column = 0; column < fit->width; ++column) {
    const int32_t at = column * fit->source_width / fit->width;
    const uint8_t byte = packed[at / 2];
    const uint8_t level = static_cast<uint8_t>(((at & 1) == 0) ? (byte >> 4) : (byte & 0x0F));
    uint8_t& target = line[column / 2];
    if ((column & 1) == 0) {
      target = static_cast<uint8_t>((target & 0x0F) | (level << 4));
    } else {
      target = static_cast<uint8_t>((target & 0xF0) | level);
    }
  }
}

// **One picture, in the cache** (0.144.0): the entry is already resolved —against the OPF for the cover of a book,
// against the chapter that is open for a plate of a page— and this is the half the two callers share. The bytes of the
// entry, its size from the header, and the pixels in the box the page will leave for it: a page asks for a picture once
// and looks at it twice —to reserve the room and to paint it— and the second time it is here, which is what the cache is
// for (docs/46, A5).
bool load_picture_named(const char* name, int32_t max_w, int32_t max_h, int32_t& out_w, int32_t& out_h) {
  out_w = 0;
  out_h = 0;
  if (!g.open || name == nullptr || name[0] == 0) return false;

  if (g.picture_ready && strcmp(name, g.picture_name) == 0) {
    out_w = g.picture_width;
    out_h = g.picture_height;
    return true;
  }
  g.picture_ready = false;

  const int32_t index = g.archive.find(name);
  if (index < 0) return false;

  const zip::Entry* one = g.archive.at(static_cast<uint16_t>(index));
  if (one == nullptr || one->dir || one->size > kPictureFileBytes) return false;

  if (g.picture_file == nullptr) {
    g.picture_file = static_cast<uint8_t*>(pal::extra_memory().allocate(kPictureFileBytes, true));
    if (g.picture_file == nullptr) return false;
    g.picture_file_bytes = kPictureFileBytes;
  }

  uint32_t written = 0;
  if (g.archive.extract(static_cast<uint16_t>(index), g.picture_file, g.picture_file_bytes, written, g.packed,
                        kChapterBytes) != zip::kOk) {
    return false;
  }

  img::Info info;
  if (img::read_info(g.picture_file, written, info) != img::kOk || info.width == 0 || info.height == 0) return false;

  int32_t width = info.width;
  int32_t height = info.height;
  if (max_w > 0 && width > max_w) {
    height = height * max_w / width;
    width = max_w;
  }
  if (max_h > 0 && height > max_h) {
    width = width * max_h / height;
    height = max_h;
  }
  if (width <= 0 || height <= 0) return false;
  if (static_cast<uint32_t>((width + 1) / 2) * static_cast<uint32_t>(height) > kPicturePixels) return false;

  if (g.picture_pixels == nullptr) {
    g.picture_pixels = static_cast<uint8_t*>(pal::extra_memory().allocate(kPicturePixels, true));
    if (g.picture_pixels == nullptr) return false;
  }

  // **The scratch of the decoder is the block of the compressed stream**, which is free at this moment: a chapter
  // and a picture of a page are not decoded at once, and the block is the board's either way (docs/46, A7).
  if (img::scratch_bytes(g.picture_file, written) > kChapterBytes) return false;

  Fit fit;
  fit.target = g.picture_pixels;
  fit.stride = (width + 1) / 2;
  fit.width = width;
  fit.height = height;
  fit.source_width = info.width;
  fit.source_height = info.height;
  fit.last_row = -1;

  if (img::decode(g.picture_file, written, img::RowSink(fit_row, &fit), g.packed, kChapterBytes) != img::kOk) {
    return false;
  }

  const size_t name_len = strlen(name);
  if (name_len >= sizeof(g.picture_name)) return false;
  memcpy(g.picture_name, name, name_len + 1);
  g.picture_width = width;
  g.picture_height = height;
  g.picture_ready = true;

  out_w = width;
  out_h = height;

  return true;
}

// **A picture of a page**, whose name is relative to the chapter that is open (`img/x.png`, `../img/x.png`).
bool load_picture(const char* href, int32_t max_w, int32_t max_h, int32_t& out_w, int32_t& out_h) {
  out_w = 0;
  out_h = 0;
  if (!g.open || href == nullptr) return false;

  char name[160];
  if (!resolve_href(href, name, sizeof(name))) return false;

  return load_picture_named(name, max_w, max_h, out_w, out_h);
}

// **The shelf of the machine**: what a mount carries in its `/epub`, with the names of the books and nothing else.
// A sink of the listing and not a list of files: a folder of a book is a few dozen names and this is the block that
// holds them (docs/46, C1).
struct Shelf : hal::ListSink {
  static const int32_t kMax = 24;

  char names[kMax][64];
  int32_t count = 0;
  char folder[32];

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.type != hal::NodeType::kFile || entry.name == nullptr) return true;

    const char* dot = strrchr(entry.name, '.');
    if (dot == nullptr || strcmp(dot, ".epub") != 0) return true;
    if (count >= kMax) return false;

    const int32_t written = snprintf(names[count], sizeof(names[0]), "%s/%s", folder, entry.name);
    if (written <= 0 || written >= static_cast<int32_t>(sizeof(names[0]))) return true;
    ++count;

    return true;
  }
};

Shelf g_shelf;
bool g_shelf_filled = false;

// What the paginator asks about a picture: the box it takes inside a box (docs/46, A3).
bool measure_picture(void* ctx, const char* href, int32_t max_w, int32_t max_h, int32_t& w, int32_t& h) {
  (void)ctx;

  return load_picture(href, max_w, max_h, w, h);
}

}  // namespace

int32_t books() {
  // The two folders of a book, and a mount that is not there says nothing instead of failing the whole shelf: a
  // board with no card still has the flash (docs/46, C1).
  if (!g_shelf_filled) {
    g_shelf_filled = true;
    g_shelf.count = 0;

    static const char* const kFolders[] = {"/littlefs", "/sd"};
    for (int32_t i = 0; i < 2; ++i) {
      hal::IStorage* storage = hal::platform().storage(kFolders[i]);
      if (storage == nullptr) continue;

      const size_t len = strlen(kFolders[i]);
      if (len + 6 >= sizeof(g_shelf.folder)) continue;
      memcpy(g_shelf.folder, kFolders[i], len);
      memcpy(g_shelf.folder + len, "/epub", 6);

      storage->list("/epub", g_shelf);
    }
  }

  return g_shelf.count;
}

const char* book_at(int32_t index) {
  if (index < 0 || index >= books()) return nullptr;

  return g_shelf.names[index];
}

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

  // **The relative path loses its leading slash** (0.151.14): every storage of this system builds `<mount>/<rel>`, so the
  // slash that is part of the path became a second one — `/littlefs//epub/la-vaca-cega.epub` — and littlefs answered
  // `fopen failed` for every book of the flash, while FATFS happened to swallow it on the card and only the flash's book
  // was unreadable. `vfs::resolve` skips the slashes exactly like this, which is why `ls /littlefs/epub` always worked and
  // `epub.open` did not (measured on the board, 8/11, docs/70).
  const char* rel = second;
  while (*rel == '/') ++rel;
  const size_t rel_len = strlen(rel);
  if (rel_len >= sizeof(g.rel)) return "the name of the book is too long";
  memcpy(g.rel, rel, rel_len + 1);

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

  if (one != nullptr) {
    const size_t name_len = strlen(one->name);
    if (name_len < sizeof(g.chapter_name)) memcpy(g.chapter_name, one->name, name_len + 1);
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

// **A small file of the book, out of the zip and into its own block** (0.142.0, docs/46 B6): the compressed bytes go
// into the block of the compressed stream, which is scratch and belongs to nobody between two chapters, and what comes
// out is the `meta` block. The chapter that is open is not touched —that is the whole reason this block exists—.
bool read(const char* name, const char** data, uint32_t& size) {
  size = 0;
  if (data != nullptr) *data = nullptr;
  if (!g.open || name == nullptr || g.packed == nullptr || data == nullptr) return false;

  const int32_t index = g.archive.find(name);
  if (index < 0) return false;

  if (g.meta == nullptr) {
    g.meta = static_cast<uint8_t*>(pal::extra_memory().allocate(kMetaBytes, true));
    if (g.meta == nullptr) return false;
  }

  uint32_t written = 0;
  if (g.archive.extract(static_cast<uint16_t>(index), g.meta, kMetaBytes, written, g.packed,
                        kChapterBytes) != zip::kOk) {
    return false;
  }

  size = written;
  *data = reinterpret_cast<const char*>(g.meta);
  return true;
}

// **The room a cache may take on a flash before it is not worth writing**: a thumbnail of 256 × 256 is 32 KB at the
// most and the one of a book is a few kilobytes, so eight is the line under which the flash has other things to do
// (docs/46, C11).
const uint64_t kThumbRoom = 8u * 1024u;

// **The cache of a book, beside the book** (0.146.0, docs/46 C11): the cover scaled to `max` pixels on its longest side
// and written as an `ICS1` in the folder of the book —hidden, with the name of the book inside it— so that whoever lists
// the folder has the face of the book without opening it. Three rules of the paper are kept because they are right: a
// cache that is already there is not written again (it is a write on the flash and the book has not changed), the room
// is asked **before** the picture is scaled, and a flash that cannot say how much room it has is a flash to try anyway.
namespace {

// **A cache that is there and whole is what spares the write**: the size of a row and the size of the file come from
// the same two words of the header, so a file of the right size is one this system finished writing, and one that was
// cut in the middle —a power that went off— is written again (docs/46, C11).
bool cache_is_whole(const char* path, uint32_t size) {
  hal::FileSlot slot;
  if (!g.storage->open(path, hal::OpenMode::kRead, slot)) return false;
  uint8_t head[8];
  const int32_t got = g.storage->read(slot, head, sizeof(head));
  g.storage->close(slot);
  if (got != static_cast<int32_t>(sizeof(head)) || memcmp(head, "ICS1", 4) != 0) return false;

  const uint16_t width = static_cast<uint16_t>(head[4] | (head[5] << 8));
  const uint16_t height = static_cast<uint16_t>(head[6] | (head[7] << 8));
  if (width == 0 || height == 0 || (width & 1u) != 0) return false;
  return static_cast<uint32_t>(img::ics_bytes(width, height)) == size;
}

}  // namespace

bool thumbnail(const char* name, int32_t max) {
  if (!g.open || g.storage == nullptr || name == nullptr || name[0] == 0) return false;
  if (max <= 0) return false;
  if (max > kThumbMax) max = kThumbMax;

  // **The path of the cache**: the folder of the book and `.` + its name + `.cache.ics`, which is the name this system
  // has always given a cache and what hides it from a shelf (docs/46, C11).
  char path[200];
  const char* slash = strrchr(g.rel, '/');
  if (slash == nullptr) return false;
  const int32_t folder = static_cast<int32_t>(slash - g.rel) + 1;
  const int32_t written = snprintf(path, sizeof(path), "%.*s.%s.cache.ics", folder, g.rel, slash + 1);
  if (written <= 0 || written >= static_cast<int32_t>(sizeof(path))) return false;

  hal::Stat there;
  if (g.storage->stat(path, there) && there.type == hal::NodeType::kFile && there.size >= 9 &&
      cache_is_whole(path, there.size)) {
    log_printf(pal::kLogInfo, "BOOK", "the cache of the book is already at %s", path);
    return true;
  }

  uint64_t total = 0;
  uint64_t free_bytes = 0;
  if (g.storage->space(total, free_bytes) && free_bytes < kThumbRoom) {
    log_printf(pal::kLogInfo, "BOOK", "no room for the cache of %s (%u B free)", path,
               static_cast<unsigned>(free_bytes));
    return true;
  }

  int32_t width = 0;
  int32_t height = 0;
  if (!load_picture_named(name, max, max, width, height)) {
    log_printf(pal::kLogWarn, "BOOK", "the picture %s of the book cannot be read", name);
    return false;
  }

  hal::FileSlot slot;
  if (!g.storage->open(path, hal::OpenMode::kWrite, slot)) return false;

  uint8_t header[8];
  img::ics_header(static_cast<uint16_t>(width), static_cast<uint16_t>(height), header);
  bool ok = g.storage->write(slot, header, static_cast<uint32_t>(sizeof(header))) ==
            static_cast<int32_t>(sizeof(header));

  // **A row at a time**: the picture of a cache is in the block of the book and the file does not have to be in memory
  // to be written (docs/46, C11). The line is half a thumbnail at the widest, which is a hundred and twenty-eight bytes.
  uint8_t line[kThumbMax / 2];
  const uint32_t stride = static_cast<uint32_t>((width + 1) / 2);
  for (int32_t y = 0; ok && y < height; ++y) {
    const uint32_t bytes = img::ics_row(static_cast<uint16_t>(width), g.picture_pixels + y * stride, line,
                                        static_cast<uint32_t>(sizeof(line)));
    if (bytes == 0) {
      ok = false;
      break;
    }
    ok = g.storage->write(slot, line, static_cast<int32_t>(bytes)) == static_cast<int32_t>(bytes);
  }
  g.storage->close(slot);

  if (ok) {
    log_printf(pal::kLogInfo, "BOOK", "the cache of the book is written at %s, %dx%d", path, width, height);
  } else {
    // **Half a cache is not a cache**: a flash that filled up in the middle of the write leaves the beginning of a
    // picture there, and it would be read as an `ICS1` that is cut. It goes, and the next open tries again (docs/46, C11).
    g.storage->remove(path);
    log_printf(pal::kLogWarn, "BOOK", "the cache of the book cannot be written at %s", path);
  }

  return ok;
}

// **The face a book carries inside itself** (0.150.0, docs/46 D6): the font entry `name` as an sfnt, whether the file
// of the book already is one or it is a `.woff` that has to be opened table by table first. It is `read` with a bigger
// block and a different question: the app is given the bytes, and making a face out of them is the UI's business.
//
// **Two hundred and fifty-six kilobytes is more than any book's face**, and the sfnt of a woff is the bigger of the
// two: the compressions of a font are between a third and a half. A face that does not fit is refused with its own
// sentence, because an app has to be able to tell that from a book that carries no font at all.
bool font(const char* name, const uint8_t** data, uint32_t& size, const char*& why) {
  size = 0;
  why = nullptr;
  if (data == nullptr) {
    why = "there is nowhere to put the face of the book";
    return false;
  }
  *data = nullptr;
  if (!g.open || name == nullptr || name[0] == 0) {
    why = "the book is not open";
    return false;
  }

  const int32_t index = g.archive.find(name);
  if (index < 0) {
    why = "the book carries no such file";
    return false;
  }
  const zip::Entry* one = g.archive.at(static_cast<uint16_t>(index));
  if (one == nullptr || one->dir || one->size > kFontFileBytes) {
    why = "the face of the book is bigger than a hundred and twenty-eight kilobytes";
    return false;
  }

  if (g.font_file == nullptr) {
    g.font_file = static_cast<uint8_t*>(pal::extra_memory().allocate(kFontFileBytes, true));
    if (g.font_file == nullptr) {
      why = "there is no room for the face of the book";
      return false;
    }
    g.font_file_bytes = kFontFileBytes;
  }

  uint32_t written = 0;
  if (g.archive.extract(static_cast<uint16_t>(index), g.font_file, g.font_file_bytes, written, g.packed,
                        kChapterBytes) != zip::kOk) {
    why = "the face of the book cannot be taken out of it";
    return false;
  }

  // **A WOFF2 is not a WOFF**: it is Brotli and a different table model, and this system has no Brotli, so it is said
  // instead of being read wrongly (docs/46, D6).
  if (ttf::is_woff2(g.font_file, written)) {
    why = "the face of the book is a WOFF2, and this reader has no Brotli";
    return false;
  }
  if (!ttf::is_woff(g.font_file, written)) {
    // An sfnt, or bytes that are not a font at all: the UI is the one that says so, and it says it with its reason.
    *data = g.font_file;
    size = written;
    return true;
  }

  const uint32_t need = ttf::woff_sfnt_size(g.font_file, written);
  if (need == 0 || need > kFontSfntBytes) {
    why = "the face of the book is bigger than half a megabyte once it is open";
    return false;
  }
  if (g.font_sfnt == nullptr) {
    g.font_sfnt = static_cast<uint8_t*>(pal::extra_memory().allocate(kFontSfntBytes, true));
    if (g.font_sfnt == nullptr) {
      why = "there is no room for the face of the book";
      return false;
    }
    g.font_sfnt_bytes = kFontSfntBytes;
  }

  uint32_t got = 0;
  if (!ttf::woff_to_sfnt(g.font_file, written, g.font_sfnt, g.font_sfnt_bytes, got)) {
    why = "the face of the book is damaged";
    return false;
  }
  *data = g.font_sfnt;
  size = got;
  return true;
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

  epub::Pictures pictures;
  pictures.ctx = nullptr;
  pictures.measure = measure_picture;

  epub::Result result;
  if (!epub::layout(tokens, width, height, style, metrics, &pictures, sheet, result)) return false;

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

// A small file of the book, said to the interpreter: the bytes and their length, or nullptr (docs/46, B6).
const char* read_of(const char* name, uint32_t& size) {
  const char* data = nullptr;
  if (!read(name, &data, size)) return nullptr;

  return data;
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

  if (style.measure) {
    style.count = laid.count;
    style.next = laid.next;
    style.ended = laid.ended;
    style.used = laid.used;
    style.has_picture = laid.has_picture;
    return true;
  }

  ui::PageFonts paint;
  paint.body = style.body;
  paint.title = style.title;
  paint.small = style.small;
  // **The two tones of the page** (0.137.0, docs/46 C9): a page of a book is ink on paper —level 0 on the white the app
  // cleared its canvas to— and **the dark page is the same page with the two swapped**, the ink white and the paper
  // black. The layout does not change at all: the same words in the same places, which is the whole point of a page of a
  // book in the dark (docs/46, A5).
  const uint8_t ink = style.dark ? 15 : 0;
  if (!ui::canvas_book_page(slot, laid.items, laid.words, laid.count, paint, ink, "gc16")) return false;

  // **And the pictures last**, because a page never puts a word on top of one: a floated picture makes the words
  // go beside it and a plate makes them go under it, so the order of the paint is not the order of the paper
  // (docs/46, A5).
  for (int16_t i = 0; i < laid.count; ++i) {
    const epub::Item& item = laid.items[i];
    if (item.kind != epub::Item::kPicture) continue;

    int32_t w = 0;
    int32_t h = 0;
    if (!load_picture(laid.words + item.text, item.w, item.h, w, h)) continue;
    // **A plate on a dark page is inverted as it is copied** (0.137.0, docs/46 C9): what the cache holds is the picture
    // as the file says it, with its paper white, and a white paper on a black page is a hole in the page.
    ui::canvas_bitmap(slot, item.x, item.y, static_cast<uint16_t>(w), static_cast<uint16_t>(h), g.picture_pixels,
                      static_cast<uint16_t>((w + 1) / 2), "gc16", style.dark);
  }

  style.count = laid.count;
  style.next = laid.next;
  style.ended = laid.ended;
  style.used = laid.used;
  style.has_picture = laid.has_picture;

  return true;
}

// **A picture of the book on a canvas of an app, by its whole name** (0.144.0, docs/46 C7): the cover of a book is an
// entry of the zip whose name is already resolved against the OPF, and it goes in the middle of the canvas with the
// paper of the book around it. **A picture keeps its paper**: it is not inverted on a dark page, because a negative
// cover is not a cover (docs/46, C9).
bool picture_into(int16_t slot, const char* name) {
  const ui::Canvas* canvas = ui::at(slot);
  if (canvas == nullptr) return false;

  int32_t width = 0;
  int32_t height = 0;
  if (!load_picture_named(name, canvas->width, canvas->height, width, height)) return false;

  const int32_t x = (static_cast<int32_t>(canvas->width) - width) / 2;
  const int32_t y = (static_cast<int32_t>(canvas->height) - height) / 2;

  return ui::canvas_bitmap(slot, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<uint16_t>(width),
                           static_cast<uint16_t>(height), g.picture_pixels,
                           static_cast<uint16_t>((width + 1) / 2), "gl16");
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
  ops.read = read_of;
  ops.books = books;
  ops.book_at = book_at;
  ops.page = page_into;
  ops.picture = picture_into;
  ops.thumbnail = thumbnail;
  ops.font = font;

  luabind::set_book_ops(ops);
}

}  // namespace book
}  // namespace kernel
