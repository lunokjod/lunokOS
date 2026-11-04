// **The listing of a directory of GitHub, read in pieces** (docs/30, docs/70).
//
// The browser page (`github.com/…/tree/main/apps`) is HTML and it is not what the system wants: it wants the list,
// and the contents API gives it as JSON, where a folder is an object with `"type": "dir"`. The answer of this
// repository is about **44 KB** —the address of every entry is most of it— and the board does not have to hold it
// anywhere: this reader walks the file with a window of two kilobytes and takes the names out one by one, building
// no tree and no table of names that grows (docs/75).
//
// It is not a general JSON reader and it does not want to be: it understands what this API writes —objects with keys
// and strings— and nothing more. A document that is not what it expects turns around with `false` and does not hang.
#ifndef LUNOK_KERNEL_REPOSITORY_H
#define LUNOK_KERNEL_REPOSITORY_H

#include <stddef.h>
#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace repository {

// **What an entry of a contents listing is**: a folder, and a file with an address to download it from as it is
// (docs/30, docs/70).
enum class Kind : uint8_t { kFile, kDir };

// **The names of the folders of a contents listing, one after the other.** `open` says whether the file could be
// opened; `next` returns the next name (without quotes) and `false` when there is no more or when the document is
// not what it looks like. The name is copied to `out` and the reader keeps no list of them.
class Listing {
 public:
  Listing() = default;
  ~Listing();
  Listing(const Listing&) = delete;
  Listing& operator=(const Listing&) = delete;

  bool open(const char* path);
  void close();
  bool next(char* out, size_t cap);

  // **The next entry, be it a file or a folder** (0.86.0): an install wants both and, for a file, the download
  // address this API gives next to the name. `next` is still the short way for a list of folders (docs/30, docs/70).
  bool next_entry(Kind& kind, char* name, size_t name_cap, char* url, size_t url_cap);

  // **The one listing the system walks at a time** (0.88.4): this object carries a window of two kilobytes, a name and
  // an address, which is about **2 300 B**, and a frame like that is not a frame of the board —the stack of the loop
  // task is sixteen kilobytes and the QR of four broke it once with a canary watchpoint (docs/75)—. There is one of
  // them in static memory, `entry_at` and the shell's listing walk it, and `open` throws away whatever the one before
  // left in it.
  static Listing& one();

 private:
  // The window of the file: two kilobytes, half a row of flash and more than one object of the listing.
  static const uint16_t kChunk = 2048;
  // **A directory name fits whole**: forty-eight bytes are a file name of any disk, and what does not fit is cut
  // instead of overflowing (docs/75).
  static const uint8_t kName = 48;
  // **A raw download address fits**: `raw.githubusercontent.com/<owner>/<repo>/main/apps/<app>/<name>` is a hundred
  // and sixteen bytes with names of thirty-two, and a hundred and forty-eight is margin (docs/30, docs/75).
  static const uint8_t kUrl = 148;
  // **A key of the answer fits whole**: the longest one this reader compares is `download_url`, which is twelve bytes,
  // and sixteen is that plus the terminator and three of margin —the API also sends `html_url`, `git_url` and
  // `svn_url` next to it, and a key that is read cut is a key that is compared against one of ours and can only fail,
  // without saying so— (the same lesson as the address that was cut at forty-eight and cost a 404, docs/30, docs/75).
  static const uint8_t kKey = 16;

  bool fill();
  int peek();
  int take();
  void skip_space();
  // A string with its quotes, with `\"` and `\\` as the only escapes (the ones this API uses). **`cut` says that the
  // text did not fit and something was dropped**, which is what a caller cannot find out any other way: a value cut in
  // the middle is an address that 404s and a key cut in the middle never matches (docs/30, docs/75).
  bool read_string(char* out, size_t cap, bool* cut = nullptr);
  // The next value as a word without quotes. False when the value is empty.
  bool read_word(char* out, size_t cap);

  hal::IStorage* storage_ = nullptr;
  hal::FileSlot slot_ = {};
  char buffer_[kChunk] = {};
  uint16_t at_ = 0;
  uint16_t len_ = 0;
  bool ended_ = false;
  char pending_[kName] = {};
  bool have_pending_ = false;
  // The download address of the object being read, when it has one: it comes before the `type` and it is kept the
  // same way the name is (docs/70).
  char pending_url_[kUrl] = {};
  bool have_url_ = false;
};

// **The n-th entry of a listing, from zero.** The install of an app walks the document one entry at a time and reads
// it again for each one: a plan of every file would be a table of addresses in RAM and the listing is a file that
// is read in tenths of a second (docs/75). False when the index goes past the end or the document is not what it
// looks like.
bool entry_at(const char* path, uint32_t index, Kind& kind, char* name, size_t name_cap, char* url,
              size_t url_cap);

// **The listing walked in order, one entry after the other** (0.88.5): `entry_at` answers an index by reading the
// document from its first byte, which is one open and one pass over the file **for every entry** —an app of twenty
// files paid twenty of each over forty-four kilobytes of JSON— while an install asks for them one behind the other and
// only needs to be where it left off. `start` opens the document, `next` gives the entry after the one before it and
// returns false at the end, and `stop` gives the file back. One walk at a time: they are the same reader `entry_at`
// uses (docs/75).
bool start(const char* path);
bool next(Kind& kind, char* name, size_t name_cap, char* url, size_t url_cap);
void stop();

}  // namespace repository
}  // namespace kernel

#endif  // LUNOK_KERNEL_REPOSITORY_H
