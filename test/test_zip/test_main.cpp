// **The ZIP container** (docs/46, A1): a zip of a book is walked from its two ends, its entries are
// found by name, and an entry comes back as the bytes it was made of.
//
// **The zip of these tests is built here, in memory, byte by byte**, which is the only way to try the
// frame without a compressor: the local header of every entry, the central directory and the record
// that closes it are written by the test itself, and the one deflated entry carries a stream that the
// real zlib produced from the same sentence —so what is tried is this reader against a compressor
// that is not mine—.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "inflate/inflate.h"
#include "zip/zip.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

const char kMimetype[] = "application/epub+zip";
const uint32_t kMimetypeCrc = 0x2CAB616Fu;  // crc32 of kMimetype, from python's zlib

const char kChapter[] =
    "<html><body><p>The same word at six sizes, and the paper is not a screen.</p></body></html>";
const uint32_t kChapterCrc = 0x447404B8u;

// `deflate` of kChapter, raw (no zlib wrapper), from zlib with the best compression: what an EPUB
// carries inside an entry.
const uint8_t kChapterPacked[] = {
    0x25, 0xCC, 0xDB, 0x09, 0x80, 0x30, 0x0C, 0x46, 0xE1, 0x55, 0xFE, 0x01, 0xC4, 0x2E, 0x10,
    0x32, 0x85, 0x0B, 0x44, 0x1B, 0xA8, 0x60, 0x2F, 0x34, 0x01, 0x2F, 0xD3, 0x5B, 0xF4, 0xE1,
    0x3C, 0x1D, 0xF8, 0x28, 0x79, 0x3E, 0x98, 0xD6, 0x1A, 0x6F, 0xA6, 0xC6, 0x4B, 0x52, 0x98,
    0x64, 0xC5, 0x59, 0x7B, 0x84, 0x38, 0x6C, 0xBF, 0x46, 0x8F, 0xDA, 0x04, 0x29, 0x11, 0x3E,
    0x7E, 0x93, 0xA6, 0x1D, 0xBB, 0xA1, 0x54, 0x87, 0xC0, 0xB6, 0xAE, 0x5A, 0x66, 0x0A, 0x8D,
    0x29, 0xFC, 0x4E, 0xF8, 0xD0, 0x17};

// **A source over a block of memory**, which is what a zip needs: the size and the bytes at an offset.
struct Bytes {
  const uint8_t* data;
  uint32_t len;
};

int64_t bytes_size(void* ctx) { return static_cast<int64_t>(static_cast<Bytes*>(ctx)->len); }

int32_t bytes_read(void* ctx, uint32_t offset, uint8_t* dst, uint32_t len) {
  Bytes* bytes = static_cast<Bytes*>(ctx);
  if (offset > bytes->len || len > bytes->len - offset) return -1;
  memcpy(dst, bytes->data + offset, len);
  return static_cast<int32_t>(len);
}

// **The zip the tests are built on**: a folder, a stored mimetype and a deflated chapter —which is the
// shape of an EPUB— built byte by byte so that every number is one this file wrote.
struct Builder {
  uint8_t bytes[2048];
  uint32_t len = 0;
  uint32_t central_at = 0;

  void u16(uint16_t v) {
    bytes[len++] = static_cast<uint8_t>(v & 0xFF);
    bytes[len++] = static_cast<uint8_t>((v >> 8) & 0xFF);
  }

  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v & 0xFFFF));
    u16(static_cast<uint16_t>((v >> 16) & 0xFFFF));
  }

  void name(const char* text) {
    const size_t n = strlen(text);
    memcpy(bytes + len, text, n);
    len += static_cast<uint32_t>(n);
  }

  void blob(const uint8_t* data, uint32_t size) {
    if (size > 0) memcpy(bytes + len, data, size);
    len += size;
  }

  // One entry: its local header, its name and its bytes. The offset of its local header is written
  // into the central directory at the end, which is what a zip does.
  void entry(const char* what, uint16_t method, uint32_t crc, uint32_t plain, const uint8_t* data,
             uint32_t packed, uint16_t flags = 0) {
    u32(0x04034b50u);
    u16(20);  // the version that is needed to read it
    u16(flags);
    u16(method);
    u16(0);  // the time and the date: this system does not use them and the test writes zero
    u16(0);
    u32(crc);
    u32(packed);
    u32(plain);
    u16(static_cast<uint16_t>(strlen(what)));
    u16(0);  // no extra field
    name(what);
    blob(data, packed);
  }

};

// The three entries of the book, in the order a zip writes them: each local header and its bytes,
// then the directory, then the record that closes the file.
uint32_t build_book(Builder& b) {
  b.len = 0;

  // 1. A stored entry, which is what `mimetype` is in every EPUB: it says what the file is before
  //    anything has to be decompressed.
  const uint32_t mimetype_at = b.len;
  b.entry("mimetype", 0, kMimetypeCrc, sizeof(kMimetype) - 1,
          reinterpret_cast<const uint8_t*>(kMimetype), sizeof(kMimetype) - 1);

  // 2. A folder: a name that ends in `/` and no bytes at all.
  const uint32_t folder_at = b.len;
  b.entry("META-INF/", 0, 0, 0, nullptr, 0);

  // 3. The chapter, deflated.
  const uint32_t chapter_at = b.len;
  b.entry("OEBPS/text/cap1.xhtml", 8, kChapterCrc, sizeof(kChapter) - 1, kChapterPacked,
          sizeof(kChapterPacked));

  // The central directory: one record per entry, with the offset of the local header each one wrote.
  const uint32_t directory_at = b.len;

  struct Central {
    const char* name;
    uint32_t at;
    uint16_t method;
    uint32_t crc;
    uint32_t plain;
    uint32_t packed;
  } records[3] = {
      {"mimetype", mimetype_at, 0, kMimetypeCrc, sizeof(kMimetype) - 1, sizeof(kMimetype) - 1},
      {"META-INF/", folder_at, 0, 0, 0, 0},
      {"OEBPS/text/cap1.xhtml", chapter_at, 8, kChapterCrc, sizeof(kChapter) - 1,
       sizeof(kChapterPacked)},
  };

  for (int i = 0; i < 3; ++i) {
    b.u32(0x02014b50u);
    b.u16(20);
    b.u16(20);
    b.u16(0);
    b.u16(records[i].method);
    b.u16(0);
    b.u16(0);
    b.u32(records[i].crc);
    b.u32(records[i].packed);
    b.u32(records[i].plain);
    b.u16(static_cast<uint16_t>(strlen(records[i].name)));
    b.u16(0);
    b.u16(0);
    b.u16(0);
    b.u16(0);
    b.u32(0);
    b.u32(records[i].at);
    b.name(records[i].name);
  }

  const uint32_t directory_size = b.len - directory_at;

  // And the record that closes it: how many entries, where the directory is and how long it is.
  b.u32(0x06054b50u);
  b.u16(0);  // this disk
  b.u16(0);  // the disk the directory is on
  b.u16(3);  // the entries on this disk
  b.u16(3);  // the entries in all
  b.u32(directory_size);
  b.u32(directory_at);
  b.u16(0);  // the comment

  return b.len;
}

zip::Source source_of(Builder& b, Bytes& bytes) {
  bytes.data = b.bytes;
  bytes.len = b.len;

  zip::Source source;
  source.ctx = &bytes;
  source.size = &bytes_size;
  source.read_at = &bytes_read;
  return source;
}

zip::Entry g_table[zip::kMaxEntries];
uint8_t g_out[4096];
uint8_t g_packed[4096];

}  // namespace

// **The two ends of the file are enough**: a zip opens by its closing record, the directory gives the
// name, the method and the two sizes of every entry, and a folder is an entry whose name ends in a
// slash. Nothing of the middle of the file is read to say that.
void test_a_zip_of_three_entries_is_walked(void) {
  Builder b;
  build_book(b);

  Bytes bytes;
  const zip::Source source = source_of(b, bytes);

  zip::Archive archive;
  uint16_t count = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));
  TEST_ASSERT_EQUAL_UINT16(3, count);
  TEST_ASSERT_EQUAL_UINT16(3, archive.count());

  TEST_ASSERT_EQUAL_STRING("mimetype", archive.at(0)->name);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kMimetype) - 1, archive.at(0)->size);
  TEST_ASSERT_EQUAL_UINT16(0, archive.at(0)->method);
  TEST_ASSERT_EQUAL_UINT32(kMimetypeCrc, archive.at(0)->crc);

  TEST_ASSERT_EQUAL_STRING("META-INF/", archive.at(1)->name);
  TEST_ASSERT_TRUE(archive.at(1)->dir);

  TEST_ASSERT_EQUAL_STRING("OEBPS/text/cap1.xhtml", archive.at(2)->name);
  TEST_ASSERT_FALSE(archive.at(2)->dir);
  TEST_ASSERT_EQUAL_UINT16(8, archive.at(2)->method);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kChapter) - 1, archive.at(2)->size);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kChapterPacked), archive.at(2)->packed);

  // And the entry is found by its name, which is what a book does with its chapters.
  TEST_ASSERT_EQUAL_INT32(2, archive.find("OEBPS/text/cap1.xhtml"));
  TEST_ASSERT_EQUAL_INT32(0, archive.find("mimetype"));
  TEST_ASSERT_EQUAL_INT32(-1, archive.find("OEBPS/text/cap9.xhtml"));
  TEST_ASSERT_NULL(archive.at(3));
}

// **A stored entry comes back as the bytes it is**: the compression of an EPUB is in its chapters, and
// `mimetype` is the one entry that is never compressed.
void test_a_stored_entry_comes_back_whole(void) {
  Builder b;
  build_book(b);

  Bytes bytes;
  const zip::Source source = source_of(b, bytes);

  zip::Archive archive;
  uint16_t count = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));

  uint32_t written = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.extract(0, g_out, sizeof(g_out), written, g_packed,
                                                  sizeof(g_packed)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kMimetype) - 1, written);
  TEST_ASSERT_EQUAL_STRING_LEN(kMimetype, reinterpret_cast<const char*>(g_out), written);

  // An empty entry is a legal entry: a folder gives no bytes and is not an error of the frame.
  uint32_t empty = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.extract(1, g_out, sizeof(g_out), empty, g_packed,
                                                  sizeof(g_packed)));
  TEST_ASSERT_EQUAL_UINT32(0, empty);
}

// **A deflated entry comes back whole**, and the stream is one the real zlib wrote: a chapter of a book
// is exactly this.
void test_a_deflated_entry_comes_back_whole(void) {
  Builder b;
  build_book(b);

  Bytes bytes;
  const zip::Source source = source_of(b, bytes);

  zip::Archive archive;
  uint16_t count = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));

  uint32_t written = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.extract(2, g_out, sizeof(g_out), written, g_packed,
                                                  sizeof(g_packed)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kChapter) - 1, written);
  TEST_ASSERT_EQUAL_STRING_LEN(kChapter, reinterpret_cast<const char*>(g_out), written);

  // The block of the compressed stream is the caller's, and it is written: an extraction that asked
  // for more than it was given is a `kNoRoom` and not a wrong page.
  TEST_ASSERT_EQUAL_INT(zip::kNoRoom, archive.extract(2, g_out, 8, written, g_packed,
                                                      sizeof(g_packed)));
  TEST_ASSERT_EQUAL_INT(zip::kNoRoom, archive.extract(2, g_out, sizeof(g_out), written, g_packed,
                                                      8));
}

// **A comment after the record does not hide it**, and a signature inside the comment is not the
// record: the reader wants the one whose comment lands at the end of the file.
void test_a_comment_with_a_signature_inside_does_not_hide_the_record(void) {
  Builder b;
  build_book(b);

  // Two records' worth of comment, and the signature of a closing record written inside it.
  const uint32_t comment = 120;
  Builder with;
  memcpy(with.bytes, b.bytes, b.len);
  with.len = b.len;

  // The length of the comment goes in the record that is already there.
  with.bytes[with.len - 2] = static_cast<uint8_t>(comment & 0xFF);
  with.bytes[with.len - 1] = static_cast<uint8_t>((comment >> 8) & 0xFF);

  for (uint32_t i = 0; i < comment; ++i) with.bytes[with.len + i] = 'x';
  // The four bytes of a closing record, in the middle of the comment.
  with.bytes[with.len + 8] = 0x50;
  with.bytes[with.len + 9] = 0x4B;
  with.bytes[with.len + 10] = 0x05;
  with.bytes[with.len + 11] = 0x06;
  with.len += comment;

  Bytes bytes;
  const zip::Source source = source_of(with, bytes);

  zip::Archive archive;
  uint16_t count = 0;
  TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));
  TEST_ASSERT_EQUAL_UINT16(3, count);
}

// **Everything that is not a zip this reader can walk is refused with its own word**, one at a time and
// with the file patched where the number lives.
void test_every_way_a_zip_can_be_refused(void) {
  Builder b;
  build_book(b);
  Bytes bytes;

  {
    // A file of noise: no closing record anywhere.
    Builder noise;
    noise.len = 0;
    for (int i = 0; i < 512; ++i) noise.bytes[noise.len++] = static_cast<uint8_t>(i * 7);
    const zip::Source source = source_of(noise, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kNotAZip, archive.open(source, g_table, zip::kMaxEntries, count));
  }

  {
    // A file shorter than the record itself.
    Builder tiny;
    tiny.len = 0;
    tiny.bytes[tiny.len++] = 'P';
    const zip::Source source = source_of(tiny, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kNotAZip, archive.open(source, g_table, zip::kMaxEntries, count));
  }

  {
    // **A table that cannot hold the entries** is the caller's number and not a damaged file.
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kNoRoom, archive.open(source, g_table, 2, count));
  }

  {
    // **A zip split in volumes**: the record says the entries are not all on this disk.
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    copy.bytes[copy.len - 22 + 4] = 1;  // this disk is the second one
    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kSplit, archive.open(source, g_table, zip::kMaxEntries, count));
  }

  {
    // **ZIP64**, which is a 0xFFFF where the count of entries goes: a book this board can hold does
    // not need it, and a 64-bit offset read as 32 would point at the middle of another entry.
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    copy.bytes[copy.len - 22 + 10] = 0xFF;
    copy.bytes[copy.len - 22 + 11] = 0xFF;
    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kZip64, archive.open(source, g_table, zip::kMaxEntries, count));
  }

  {
    // **A directory that is outside the file** is a file somebody cut, and it is not walked.
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    copy.bytes[copy.len - 22 + 16] = 0xF0;  // where it says it starts
    copy.bytes[copy.len - 22 + 17] = 0xFF;
    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kDirectory, archive.open(source, g_table, zip::kMaxEntries, count));
  }
}

// **An entry can be refused when it is extracted**: encrypted, a method this reader does not know, and
// a checksum that does not match what came out.
void test_an_entry_can_be_refused_when_it_is_extracted(void) {
  Builder b;
  build_book(b);
  Bytes bytes;

  // The flags of the stored entry, where the directory record keeps them: bit 0 is "encrypted".
  {
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    // The directory starts right after the three local entries; its first record is `mimetype`.
    // The flags are at +8 of the record, and the record is found by its signature.
    for (uint32_t i = 0; i + 4 < copy.len; ++i) {
      if (copy.bytes[i] == 0x50 && copy.bytes[i + 1] == 0x4B && copy.bytes[i + 2] == 0x01 &&
          copy.bytes[i + 3] == 0x02) {
        copy.bytes[i + 8] = 1;  // encrypted
        break;
      }
    }

    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));
    uint32_t written = 0;
    TEST_ASSERT_EQUAL_INT(zip::kEncrypted, archive.extract(0, g_out, sizeof(g_out), written, g_packed,
                                                           sizeof(g_packed)));
  }

  // A method that is not stored and not deflate: a zip of a book never carries one, and guessing would
  // paint nonsense.
  {
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    for (uint32_t i = 0; i + 4 < copy.len; ++i) {
      if (copy.bytes[i] == 0x50 && copy.bytes[i + 1] == 0x4B && copy.bytes[i + 2] == 0x01 &&
          copy.bytes[i + 3] == 0x02) {
        copy.bytes[i + 10] = 12;  // bzip2
        break;
      }
    }

    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));
    uint32_t written = 0;
    TEST_ASSERT_EQUAL_INT(zip::kMethod, archive.extract(0, g_out, sizeof(g_out), written, g_packed,
                                                        sizeof(g_packed)));
  }

  // **A byte turned around in a stored entry** has the size it says and the checksum it does not:
  // this is the case a card with a bad block gives, and it is not a page of a book.
  {
    Builder copy;
    memcpy(copy.bytes, b.bytes, b.len);
    copy.len = b.len;
    // The bytes of `mimetype` start thirty bytes and eight characters after its local header.
    copy.bytes[30 + 8 + 3] ^= 0xFF;

    const zip::Source source = source_of(copy, bytes);
    zip::Archive archive;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(zip::kOk, archive.open(source, g_table, zip::kMaxEntries, count));
    uint32_t written = 0;
    TEST_ASSERT_EQUAL_INT(zip::kCrc, archive.extract(0, g_out, sizeof(g_out), written, g_packed,
                                                     sizeof(g_packed)));
  }
}

// **A name longer than an entry holds** is a name that would be cut and would point at another entry,
// so the whole zip is refused: eighty names of the frame are not worth a wrong chapter.
void test_a_name_that_does_not_fit_is_refused(void) {
  Builder b;
  build_book(b);
  Bytes bytes;

  Builder copy;
  memcpy(copy.bytes, b.bytes, b.len);
  copy.len = b.len;

  // The length of the last name in the directory is patched to one byte more than an entry holds.
  for (uint32_t i = 0; i + 4 < copy.len; ++i) {
    if (copy.bytes[i] == 0x50 && copy.bytes[i + 1] == 0x4B && copy.bytes[i + 2] == 0x01 &&
        copy.bytes[i + 3] == 0x02) {
      // Walk to the last record and patch its name length; the last one is the chapter.
      uint32_t last = i;
      for (uint32_t j = i; j + 4 < copy.len; ++j) {
        if (copy.bytes[j] == 0x50 && copy.bytes[j + 1] == 0x4B && copy.bytes[j + 2] == 0x01 &&
            copy.bytes[j + 3] == 0x02) {
          last = j;
        }
      }
      copy.bytes[last + 28] = static_cast<uint8_t>(zip::kMaxName & 0xFF);
      copy.bytes[last + 29] = static_cast<uint8_t>(zip::kMaxName >> 8);
      break;
    }
  }

  const zip::Source source = source_of(copy, bytes);
  zip::Archive archive;
  uint16_t count = 0;
  TEST_ASSERT_EQUAL_INT(zip::kDirectory, archive.open(source, g_table, zip::kMaxEntries, count));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_zip_of_three_entries_is_walked);
  RUN_TEST(test_a_stored_entry_comes_back_whole);
  RUN_TEST(test_a_deflated_entry_comes_back_whole);
  RUN_TEST(test_a_comment_with_a_signature_inside_does_not_hide_the_record);
  RUN_TEST(test_every_way_a_zip_can_be_refused);
  RUN_TEST(test_an_entry_can_be_refused_when_it_is_extracted);
  RUN_TEST(test_a_name_that_does_not_fit_is_refused);
  return UNITY_END();
}
