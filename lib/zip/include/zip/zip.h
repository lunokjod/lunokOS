// **The ZIP container** (docs/46, A1): the frame an EPUB travels in.
//
// A zip is a file with three parts: a **local header** and a stream of bytes for every entry, in the
// order the files were added; a **central directory** at the end with one record per entry —the name,
// the method, the two sizes, the checksum and where the local header is—; and a **record that closes
// it** with how many entries there are and where the directory starts. This reader walks the two ends
// and never the middle: a book of ten megabytes opens in three reads.
//
// **The compression is not here**: an entry is `stored` (the bytes are the bytes) or `deflate`, and
// the deflate is `lib/inflate` —the same one that reads a `.woff`—, which is why the only thing this
// file adds to a system that already had the decompressor is the structure of the frame.
//
// **Nothing here allocates.** The caller hands the table of entries and the block the compressed
// stream is copied into, and the two reasons are the same: this runs under the interpreter of an app
// and under the loop that has eight kilobytes of stack, and a library that decides how much memory to
// take is a library that cannot be told no (docs/46, A7). The one block of its own is a window for
// the search of the closing record, and it is static for that reason and not on the stack.
//
// What is refused, each with its own word, is what a reading board cannot honour: a zip split in
// volumes, **ZIP64** (a 64-bit offset in a file this board can hold is a file somebody damaged), an
// encrypted entry (DRM is another problem), a compression that is not 0 or 8, a name longer than an
// entry holds, an entry bigger than two gigabytes, a stream that inflates to something other than
// what its header promised, and a stream whose checksum does not match.
#ifndef LUNOK_ZIP_ZIP_H
#define LUNOK_ZIP_ZIP_H

#include <stdint.h>

namespace zip {

// **Where the bytes are**, which is the caller's business: a file on a disk, a card, a block of
// memory in a test. `size` is the whole length or a negative number; `read_at` is exactly that many
// bytes and never a short read —a piece of a file that comes back short is a source that failed, and
// the reader says `kBadData` instead of reading the wrong bytes—.
struct Source {
  void* ctx = nullptr;
  int64_t (*size)(void* ctx) = nullptr;
  int32_t (*read_at)(void* ctx, uint32_t offset, uint8_t* dst, uint32_t len) = nullptr;
};

// **What came out, and why when nothing did.** A plain enum like the HAL's and the image reader's:
// the answer is asked for on nearly every line of this file.
enum Status : uint8_t {
  kOk = 0,
  kNotAZip,   // there is no closing record, or the file is too small to have one
  kSplit,     // a zip in volumes: each piece is its own file and this reader reads one
  kZip64,     // a 64-bit size or offset, which this reader does not read
  kDirectory, // the central directory is outside the file, damaged, or says something impossible
  kNoRoom,    // the caller's table cannot hold the entries, or the caller's block the entry
  kBadData,   // a truncated stream, a local header that is not where the directory said
  kEncrypted, // the entry carries the encryption bit
  kMethod,    // a compression that is not stored or deflate
  kCrc,       // the checksum of the entry does not match its record
};

// **The longest name an entry can have**, terminator included. The storage layer names its segments
// with the same ceiling and for the same reason: a name is copied into a fixed place.
const uint16_t kMaxName = 128;

// The entries of a book are its chapters and its pictures: a thousand is a book of a thousand files,
// and the table is the caller's, so this is a ceiling and not a promise of memory.
const uint16_t kMaxEntries = 1024;

// **One entry of the central directory**, all of it copied out of the record: the name is the key, the
// two sizes and the checksum are what an extraction is verified against, and `local` is where the
// stream starts being described.
struct Entry {
  char name[kMaxName];
  uint32_t size;    // the bytes it inflates to
  uint32_t packed;  // the bytes the file holds
  uint32_t local;   // where its local header is
  uint32_t crc;     // the CRC-32 of the bytes it inflates to
  uint16_t method;  // 0 stored, 8 deflate
  uint16_t flags;   // bit 0 is "encrypted"
  bool dir;         // the name ends in `/`: it is a folder and it has no bytes
};

// **A zip open over a source.** The source has to stay alive while this is open: the local headers
// and the streams are still there to be read.
struct Archive {
  // Reads the closing record and the central directory and fills the table. `count` says how many
  // entries came out, and the table is filled from zero up to `cap`.
  Status open(const Source& source, Entry* table, uint16_t cap, uint16_t& count);

  uint16_t count() const { return count_; }

  // The entry at `index`, or nullptr past the end.
  const Entry* at(uint16_t index) const;

  // **The first entry whose name is exactly this one**, or -1. The names inside a zip carry `/` and
  // this system speaks `/`, so a name written with `\` was turned around when the table was read.
  int32_t find(const char* name) const;

  // **One entry, inflated into a block of the caller's** (`out`, `cap` bytes), with what is
  // compressed copied first into `packed` (`packed_cap` bytes) because the decompressor is one-shot
  // and its window is the output block. Returns `kNoRoom` when either block is too small, which is
  // the caller's to size: it knows whether it is reading a chapter or a picture.
  Status extract(uint16_t index, uint8_t* out, uint32_t cap, uint32_t& written, uint8_t* packed,
                 uint32_t packed_cap) const;

 private:
  Source source_;
  Entry* table_ = nullptr;
  uint16_t count_ = 0;
};

}  // namespace zip

#endif  // LUNOK_ZIP_ZIP_H
