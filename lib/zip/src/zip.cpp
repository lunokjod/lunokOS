// The ZIP frame: the two ends of the file and nothing of the middle (docs/46, A1).
//
// The traps this file pays for, one by one:
//
//   * **The closing record is searched from the end**, not from the beginning: a zip may carry up to
//     65 535 bytes of comment after it, so the record is somewhere in the last 65 557 bytes and the
//     copy that matters is the LAST one —a comment that happens to spell the signature is not the
//     record—.
//   * **The comment length has to land exactly at the end of the file.** Four bytes inside a comment
//     match the signature too; the length is what tells a record from a coincidence.
//   * **The central directory is the only truth.** A local header can lie —some writers put zeros in
//     the sizes and a data descriptor after the stream—, so the sizes come from the directory and the
//     local header is only used to find where the stream starts.
//   * **A stored entry whose two sizes disagree is damaged.** Stored means the bytes are the bytes;
//     if the counts differ, one of them is a lie.
//   * **The window of DEFLATE is the output block itself**, which is why `inflate::raw` is one-shot
//     and why the compressed bytes are copied into a block of the caller's first.
#include "zip/zip.h"

#include <string.h>

#include "inflate/inflate.h"

namespace zip {
namespace {

const uint32_t kEndSignature = 0x06054b50u;    // "PK\5\6"
const uint32_t kCentralSignature = 0x02014b50u;  // "PK\1\2"
const uint32_t kLocalSignature = 0x04034b50u;  // "PK\3\4"

// The closing record is 22 bytes and the comment may be 65 535 more, which is the whole tail this
// reader ever looks at.
const uint32_t kEndSize = 22;
const uint32_t kMaxComment = 65535;

// **The window of the search of the closing record**, and it is static on purpose: this library is
// called from under the interpreter of an app, and the loop that runs it has eight kilobytes of stack
// (docs/46, A7). There is one zip open at a time in this system —one book—, so one window is enough,
// and it is a kilobyte because the walk is a handful of trips even for a commented file.
uint8_t g_window[1024];

// The size of a record of the central directory before its name, and of a local header.
const uint32_t kCentralRecord = 46;
const uint32_t kLocalRecord = 30;

uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Exactly `len` bytes, or nothing: the size of the source was checked already, so a short read is a
// source that failed halfway and not a file that ended.
bool read_exact(const Source& source, uint32_t offset, uint8_t* dst, uint32_t len) {
  if (len == 0) return true;
  if (source.read_at == nullptr) return false;
  return source.read_at(source.ctx, offset, dst, len) == static_cast<int32_t>(len);
}

}  // namespace

Status Archive::open(const Source& source, Entry* table, uint16_t cap, uint16_t& count) {
  count = 0;
  count_ = 0;
  table_ = table;

  if (source.size == nullptr || source.read_at == nullptr) return kBadData;

  const int64_t size = source.size(source.ctx);
  if (size < static_cast<int64_t>(kEndSize)) return kNotAZip;

  const uint32_t total = static_cast<uint32_t>(size);
  if (total > 0x7FFFFFFFu) return kZip64;  // a reader of 32-bit offsets does not walk a bigger file

  // **The closing record, found from the end.** The window walks backwards and overlaps by one record,
  // so a record that falls between two windows is seen whole in one of them.
  int64_t end = -1;
  uint32_t from = total;

  for (;;) {
    if (from == 0) break;

    const uint32_t want = (from < sizeof(g_window)) ? from : static_cast<uint32_t>(sizeof(g_window));
    const uint32_t at = from - want;

    if (!read_exact(source, at, g_window, want)) return kBadData;

    // Every position where a record could start inside this window. The record ends at the end of
    // the file —its comment is the last thing in the zip—, and that is the whole of the test.
    for (int64_t i = static_cast<int64_t>(want) - static_cast<int64_t>(kEndSize); i >= 0; --i) {
      if (le32(g_window + i) != kEndSignature) continue;

      const uint32_t comment = le16(g_window + i + 20);
      const int64_t after = static_cast<int64_t>(at) + i + static_cast<int64_t>(kEndSize) +
                            static_cast<int64_t>(comment);

      // A record whose comment does not land at the end of the file is a signature inside something
      // else, and there may be several inside a comment.
      if (after != static_cast<int64_t>(total)) continue;

      end = static_cast<int64_t>(at) + i;
      break;
    }

    if (end >= 0) break;

    // The next window goes back far enough to overlap this one: nothing is missed at the seam.
    if (want < sizeof(g_window)) break;
    from = at + kEndSize - 1;
  }

  if (end < 0) return kNotAZip;

  uint8_t record[kEndSize];
  if (!read_exact(source, static_cast<uint32_t>(end), record, kEndSize)) return kBadData;

  const uint16_t disk = le16(record + 4);
  const uint16_t directory_disk = le16(record + 6);
  const uint16_t entries_here = le16(record + 8);
  const uint16_t entries_total = le16(record + 10);
  const uint32_t directory_size = le32(record + 12);
  const uint32_t directory_at = le32(record + 16);

  if (disk != 0 || directory_disk != 0) return kSplit;
  if (entries_here == 0xFFFFu || entries_total == 0xFFFFu || directory_size == 0xFFFFFFFFu ||
      directory_at == 0xFFFFFFFFu) {
    return kZip64;
  }
  if (entries_here != entries_total) return kDirectory;
  if (entries_total > cap) return kNoRoom;
  if (static_cast<uint64_t>(directory_at) + directory_size > total) return kDirectory;

  // **The directory is walked record by record** and not copied whole: a record is forty-six bytes
  // and a name of at most a hundred and twenty-seven, so the whole of it fits a piece of the stack
  // that is smaller than anything the board would notice.
  uint32_t at = directory_at;
  const uint32_t end_of_directory = directory_at + directory_size;

  for (uint16_t i = 0; i < entries_total; ++i) {
    uint8_t head[kCentralRecord];
    if (at + kCentralRecord > end_of_directory) return kDirectory;
    if (!read_exact(source, at, head, kCentralRecord)) return kBadData;
    if (le32(head) != kCentralSignature) return kDirectory;

    const uint16_t flags = le16(head + 8);
    const uint16_t method = le16(head + 10);
    const uint32_t crc = le32(head + 16);
    const uint32_t packed = le32(head + 20);
    const uint32_t plain = le32(head + 24);
    const uint16_t name_len = le16(head + 28);
    const uint16_t extra = le16(head + 30);
    const uint16_t comment = le16(head + 32);
    const uint32_t local = le32(head + 42);

    // A 64-bit size hides behind these, and so does a 64-bit offset.
    if (packed == 0xFFFFFFFFu || plain == 0xFFFFFFFFu || local == 0xFFFFFFFFu) return kZip64;
    if (plain > 0x7FFFFFFFu || packed > 0x7FFFFFFFu || local > 0x7FFFFFFFu) return kZip64;

    if (name_len == 0 || name_len >= kMaxName) return kDirectory;

    const uint32_t next = at + kCentralRecord + name_len + extra + comment;
    if (next > end_of_directory) return kDirectory;

    Entry& entry = table[i];
    memset(&entry, 0, sizeof(entry));

    if (!read_exact(source, at + kCentralRecord, reinterpret_cast<uint8_t*>(entry.name), name_len)) {
      return kBadData;
    }
    entry.name[name_len] = '\0';

    // A writer of a Windows PC writes the folder separator of its system; this one speaks `/`.
    for (uint16_t k = 0; k < name_len; ++k) {
      if (entry.name[k] == '\\') entry.name[k] = '/';
    }

    entry.size = plain;
    entry.packed = packed;
    entry.local = local;
    entry.crc = crc;
    entry.method = method;
    entry.flags = flags;
    entry.dir = (entry.name[name_len - 1] == '/');

    at = next;
  }

  source_ = source;
  count_ = entries_total;
  count = entries_total;

  return kOk;
}

const Entry* Archive::at(uint16_t index) const {
  if (table_ == nullptr || index >= count_) return nullptr;
  return &table_[index];
}

int32_t Archive::find(const char* name) const {
  if (name == nullptr) return -1;

  for (uint16_t i = 0; i < count_; ++i) {
    if (strcmp(table_[i].name, name) == 0) return static_cast<int32_t>(i);
  }

  return -1;
}

Status Archive::extract(uint16_t index, uint8_t* out, uint32_t cap, uint32_t& written,
                        uint8_t* packed, uint32_t packed_cap) const {
  written = 0;

  const Entry* entry = at(index);
  if (entry == nullptr) return kDirectory;

  // **A folder is an entry with no bytes**: extracting it gives zero bytes and is not an error, which
  // is what the zip of a book does with `META-INF/` when the script walks every entry it found.
  if ((entry->flags & 1u) != 0) return kEncrypted;

  if (entry->size > cap) return kNoRoom;
  if (out == nullptr && entry->size > 0) return kNoRoom;

  // An empty entry still needs an address: a stream that produces zero bytes is a legal stream and
  // the decompressor refuses a null output. One byte of the stack is enough for it.
  uint8_t empty = 0;
  if (out == nullptr) out = &empty;

  if (static_cast<uint64_t>(entry->local) + kLocalRecord > static_cast<uint64_t>(source_.size(source_.ctx))) {
    return kBadData;
  }

  // **The local header says where its name and its extra field end**, and the stream starts after
  // them: it is read for that and for nothing else, because its sizes are the ones that lie.
  uint8_t head[kLocalRecord];
  if (!read_exact(source_, entry->local, head, kLocalRecord)) return kBadData;
  if (le32(head) != kLocalSignature) return kBadData;

  const uint32_t name_len = le16(head + 26);
  const uint32_t extra = le16(head + 28);
  const uint64_t start = static_cast<uint64_t>(entry->local) + kLocalRecord + name_len + extra;
  const uint64_t end = start + entry->packed;

  if (end > static_cast<uint64_t>(source_.size(source_.ctx))) return kBadData;

  if (entry->method == 0) {
    // Stored: the bytes are the bytes, so the two sizes that disagree mean a damaged entry.
    if (entry->packed != entry->size) return kBadData;
    if (!read_exact(source_, static_cast<uint32_t>(start), out, entry->size)) return kBadData;
    written = entry->size;

    // **The bytes are checked against the checksum their own record promises**: a stored entry can
    // come out of a card with a bit turned around and still have the size it says.
    if (inflate::crc32(out, written) != entry->crc) return kCrc;
    return kOk;
  }

  if (entry->method != 8) return kMethod;

  // **A deflate stream is never much bigger than what it holds**: stored blocks cost five bytes per
  // 64 KB, so a stream four times its own contents is not a stream this reader should copy —it is a
  // bomb wearing the size of a chapter—.
  if (static_cast<uint64_t>(entry->packed) >
      static_cast<uint64_t>(entry->size) + static_cast<uint64_t>(entry->size) / 1000u + 4096u) {
    return kBadData;
  }

  if (entry->packed > packed_cap || (packed == nullptr && entry->packed > 0)) return kNoRoom;
  if (entry->packed > 0 &&
      !read_exact(source_, static_cast<uint32_t>(start), packed, entry->packed)) {
    return kBadData;
  }

  uint32_t made = 0;
  const inflate::Status status = inflate::raw(packed, entry->packed, out, cap, made);
  if (status != inflate::kOk) {
    return (status == inflate::kNoRoom) ? kNoRoom : kBadData;
  }

  if (made != entry->size) return kBadData;

  // And the same check as a stored entry: a stream that inflates to the right length and the wrong
  // bytes is a stream that a card wrote badly.
  if (inflate::crc32(out, made) != entry->crc) return kCrc;

  written = made;
  return kOk;
}

}  // namespace zip
