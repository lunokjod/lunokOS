// **A book open, end to end** (docs/46, A5): a real `.epub` on the disk of the test, its table of entries, a
// chapter inflated into the memory of the book and a page laid out from the face of a book.
//
// The book of these cases is written here, byte by byte, and it is a zip with the three entries a reader needs —
// the `mimetype`, and two chapters— all of them **stored**, because what is being tried is the road from a file
// to a page and not a compressor, which `lib/inflate` already has a suite for.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unity.h>

#include "emu/options.h"
#include "hal/hal.h"
#include "inflate/inflate.h"
#include "kernel/book.h"
#include "kernel/font.h"
#include "kernel/kernel.h"
#include "pal/pal.h"
#include "pal_host.h"
#include "ttf/ttf.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

const char kChapterOne[] =
    "<html><body><p>The same word at six sizes.</p>"
    "<h1>A title</h1><p>And the paragraph after it.</p></body></html>";
const char kChapterTwo[] = "<html><body><p>The second chapter.</p></body></html>";

// **A zip with stored entries, written by the test**, which is the smallest book a reader can be given.
struct Zip {
  struct Kept {
    char name[48];
    uint32_t crc;
    uint32_t size;
    uint32_t local;
  };

  uint8_t bytes[8192];
  int32_t len;
  Kept kept[8];
  int32_t count;

  Zip() : len(0), count(0) {}

  void u16(uint16_t value) {
    bytes[len++] = static_cast<uint8_t>(value & 0xFF);
    bytes[len++] = static_cast<uint8_t>((value >> 8) & 0xFF);
  }

  void u32(uint32_t value) {
    for (int32_t i = 0; i < 4; ++i) bytes[len++] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
  }

  void raw(const void* data, int32_t size) {
    memcpy(bytes + len, data, static_cast<size_t>(size));
    len += size;
  }

  void add(const char* name, const char* data) {
    Kept& one = kept[count++];
    const int32_t name_len = static_cast<int32_t>(strlen(name));

    memcpy(one.name, name, static_cast<size_t>(name_len) + 1);
    one.size = static_cast<uint32_t>(strlen(data));
    one.crc = inflate::crc32(reinterpret_cast<const uint8_t*>(data), one.size);
    one.local = static_cast<uint32_t>(len);

    u32(0x04034B50u);  // a local header
    u16(20);           // the version that is needed
    u16(0);            // no flags
    u16(0);            // method 0: stored
    u16(0);            // time
    u16(0);            // date
    u32(one.crc);
    u32(one.size);
    u32(one.size);
    u16(static_cast<uint16_t>(name_len));
    u16(0);
    raw(name, name_len);
    raw(data, static_cast<int32_t>(one.size));
  }

  void finish() {
    const uint32_t directory = static_cast<uint32_t>(len);

    for (int32_t i = 0; i < count; ++i) {
      const Kept& one = kept[i];
      const int32_t name_len = static_cast<int32_t>(strlen(one.name));

      u32(0x02014B50u);  // a record of the central directory
      u16(20);
      u16(20);
      u16(0);
      u16(0);
      u16(0);
      u16(0);
      u32(one.crc);
      u32(one.size);
      u32(one.size);
      u16(static_cast<uint16_t>(name_len));
      u16(0);
      u16(0);
      u16(0);
      u16(0);
      u32(0);
      u32(one.local);
      raw(one.name, name_len);
    }

    const uint32_t directory_size = static_cast<uint32_t>(len) - directory;

    u32(0x06054B50u);  // the record that closes it
    u16(0);
    u16(0);
    u16(static_cast<uint16_t>(count));
    u16(static_cast<uint16_t>(count));
    u32(directory_size);
    u32(directory);
    u16(0);
  }
};

char g_root[128];
bool g_booted = false;

void make_dirs(const char* path) {
  char partial[256];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;

  for (size_t i = 1; i <= len; ++i) {
    if (path[i] != '/' && path[i] != 0) continue;
    memcpy(partial, path, i);
    partial[i] = 0;
    mkdir(partial, 0700);
  }
}

// **The kernel, booted on a disk of its own**, so the book of the test is the only book on it (docs/85).
bool boot() {
  if (g_booted) return true;

  snprintf(g_root, sizeof(g_root), "/tmp/lunok-book-tests-%d", static_cast<int>(getpid()));

  char flash[160];
  snprintf(flash, sizeof(flash), "%s/littlefs", g_root);
  make_dirs(g_root);
  make_dirs(flash);

  emu::Options opts;
  opts.fs_dir = g_root;
  emu::set_options(opts);

  if (!pal::init()) return false;

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  if (storage == nullptr) return false;
  storage->mkdir("/epub");

  kernel::boot(hal::platform());
  g_booted = true;

  return true;
}

// Writes `bytes` at that path of the flash of the test.
bool write_file(const char* path, const uint8_t* bytes, int32_t len) {
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  if (storage == nullptr) return false;

  hal::FileSlot slot;
  if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
  const int32_t wrote = storage->write(slot, bytes, static_cast<uint32_t>(len));
  storage->close(slot);

  return wrote == len;
}

// The path of the book of the test, written once.
const char* tiny_book() {
  static const char* path = "/littlefs/epub/tiny.epub";
  static bool written = false;

  if (written) return path;

  Zip zip;
  zip.add("mimetype", "application/epub+zip");
  zip.add("OEBPS/ch1.xhtml", kChapterOne);
  zip.add("OEBPS/ch2.xhtml", kChapterTwo);
  zip.finish();

  if (!write_file("/epub/tiny.epub", zip.bytes, zip.len)) return nullptr;

  written = true;
  return path;
}

// The face of a book, read from the repository as the test runs on the computer.
kernel::font::Face g_face;
bool load_book_face() {
  static char file[32768];
  static bool tried = false;
  static bool ok = false;

  if (tried) return ok;
  tried = true;

  FILE* handle = fopen("data/res/fonts/dejavuserif-latin1.ttf", "rb");
  if (handle == nullptr) return false;
  const size_t got = fread(file, 1, sizeof(file), handle);
  fclose(handle);

  static ttf::Face storage;
  const char* why = kernel::font::load_ttf(file, static_cast<uint32_t>(got), storage, g_face);
  ok = (why == nullptr);
  return ok;
}

}  // namespace

// **A book opens, says what it carries and closes**: the table of entries is the truth and the chapters are found
// by name (docs/46, A5).
static void test_a_book_opens_and_says_what_it_carries() {
  TEST_ASSERT_TRUE_MESSAGE(boot(), "the kernel boots on a disk of the test");
  const char* path = tiny_book();
  TEST_ASSERT_NOT_NULL(path);

  TEST_ASSERT_NULL_MESSAGE(kernel::book::open(path), "a book opens");
  TEST_ASSERT_TRUE(kernel::book::is_open());
  TEST_ASSERT_EQUAL_INT32(3, kernel::book::entries());

  TEST_ASSERT_EQUAL_INT32(0, kernel::book::find("mimetype"));
  const zip::Entry* mimetype = kernel::book::entry(0);
  TEST_ASSERT_NOT_NULL(mimetype);
  TEST_ASSERT_EQUAL_STRING("mimetype", mimetype->name);
  TEST_ASSERT_EQUAL_UINT32(20, mimetype->size);
  TEST_ASSERT_EQUAL_INT32(-1, kernel::book::find("OEBPS/nothing.xhtml"));

  // **The first chapter is the first name that looks like one** (docs/46, C2): the `mimetype` is not a chapter.
  const int32_t first = kernel::book::first_chapter();
  TEST_ASSERT_EQUAL_INT32(1, first);
  TEST_ASSERT_EQUAL_STRING("OEBPS/ch1.xhtml", kernel::book::entry(first)->name);

  kernel::book::close();
  TEST_ASSERT_FALSE(kernel::book::is_open());
}

// **A chapter comes out of the book**, inflated and whole: what the tokenizer will walk (docs/46, A5).
static void test_a_chapter_comes_out_of_the_book() {
  TEST_ASSERT_TRUE(boot());
  const char* path = tiny_book();
  TEST_ASSERT_NOT_NULL(path);
  TEST_ASSERT_NULL(kernel::book::open(path));

  int32_t bytes = 0;
  const char* chapter = kernel::book::chapter("OEBPS/ch1.xhtml", bytes);
  TEST_ASSERT_NOT_NULL(chapter);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(strlen(kChapterOne)), bytes);
  TEST_ASSERT_EQUAL_STRING_LEN(kChapterOne, chapter, bytes);

  // A name the book does not carry is nothing, and so is a chapter asked for with no book open.
  TEST_ASSERT_NULL(kernel::book::chapter("OEBPS/nothing.xhtml", bytes));
  TEST_ASSERT_EQUAL_INT32(0, bytes);

  kernel::book::close();
  TEST_ASSERT_NULL(kernel::book::chapter_at(1, bytes));
}

// **A page of the book is laid out from the byte the reader was at** (docs/46, A3 and A5): the words, the title of
// its own face and its own step, and the byte the next page starts at.
static void test_a_page_of_the_book_is_laid_out() {
  TEST_ASSERT_TRUE(boot());
  TEST_ASSERT_TRUE_MESSAGE(load_book_face(), "the face of a book is on the disk of the repository");
  const char* path = tiny_book();
  TEST_ASSERT_NOT_NULL(path);
  TEST_ASSERT_NULL(kernel::book::open(path));

  int32_t bytes = 0;
  TEST_ASSERT_NOT_NULL(kernel::book::chapter("OEBPS/ch1.xhtml", bytes));

  kernel::book::Fonts fonts;
  fonts.body = &g_face;
  fonts.title = &g_face;
  fonts.small = &g_face;

  epub::Style style;  // the steps of a page of a book: four for the words and five for the title

  kernel::book::Page page;
  TEST_ASSERT_TRUE(kernel::book::page(0, 400, 700, style, fonts, page));
  // The chapter holds six words, a title of two and a paragraph of five, and the page has room for all of them.
  TEST_ASSERT_EQUAL_INT16(13, page.count);
  TEST_ASSERT_EQUAL_STRING("The", page.words + page.items[0].text);
  TEST_ASSERT_EQUAL_UINT8(epub::kFaceBody, page.items[0].face);
  TEST_ASSERT_EQUAL_UINT8(4, page.items[0].scale);
  // The chapter has a title after the paragraph, so the page is over and the title is on it.
  TEST_ASSERT_TRUE(page.ended);
  TEST_ASSERT_GREATER_THAN(0, page.used);

  // **A box that is too narrow for the whole paragraph ends the page at a word**, and the page after it starts at
  // that byte: that is the reading position of a book (docs/46, §1).
  kernel::book::Page first;
  TEST_ASSERT_TRUE(kernel::book::page(0, 60, 60, style, fonts, first));
  TEST_ASSERT_FALSE(first.ended);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(strstr(kChapterOne, "same") - kChapterOne), first.next);
  TEST_ASSERT_GREATER_THAN(0, first.count);

  kernel::book::Page second;
  TEST_ASSERT_TRUE(kernel::book::page(first.next, 60, 60, style, fonts, second));
  TEST_ASSERT_EQUAL_STRING("same", second.words + second.items[0].text);

  kernel::book::close();
}

// **A file that is not a book says why**, in the words of a person, and it leaves the memory where it found it
// (docs/46, A5).
static void test_a_file_that_is_not_a_book_says_why() {
  TEST_ASSERT_TRUE(boot());
  TEST_ASSERT_NOT_NULL(tiny_book());

  TEST_ASSERT_NOT_NULL_MESSAGE(kernel::book::open("epub/tiny.epub"), "a relative path is not a book");
  TEST_ASSERT_NOT_NULL_MESSAGE(kernel::book::open("/littlefs/epub/nothing.epub"), "there is no such file");
  TEST_ASSERT_NOT_NULL_MESSAGE(kernel::book::open("/fram/tiny.epub"), "that unit is not mounted");

  // A file of the disk that is not a zip: the reader of the frame refuses it and the book says so.
  const char not_a_book[] = "this is not a book, it is a sentence";
  TEST_ASSERT_TRUE(write_file("/epub/plain.epub", reinterpret_cast<const uint8_t*>(not_a_book),
                              static_cast<int32_t>(sizeof(not_a_book) - 1)));
  TEST_ASSERT_NOT_NULL_MESSAGE(kernel::book::open("/littlefs/epub/plain.epub"), "a file that is not a zip");
  TEST_ASSERT_FALSE(kernel::book::is_open());

  // **And the books of the failed opens took nothing**: a real one still opens and still has its entries.
  TEST_ASSERT_NULL(kernel::book::open("/littlefs/epub/tiny.epub"));
  TEST_ASSERT_EQUAL_INT32(3, kernel::book::entries());
  kernel::book::close();
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_book_opens_and_says_what_it_carries);
  RUN_TEST(test_a_chapter_comes_out_of_the_book);
  RUN_TEST(test_a_page_of_the_book_is_laid_out);
  RUN_TEST(test_a_file_that_is_not_a_book_says_why);
  return UNITY_END();
}
