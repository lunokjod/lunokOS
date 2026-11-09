#include "kernel/repository.h"

#include <string.h>

#include "kernel/vfs.h"

namespace kernel {
namespace repository {
namespace {

// A word of the document and nothing more: what this reader looks at are the keys and the values of every object.
bool is_space(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

}  // namespace

Listing::~Listing() { close(); }

bool Listing::open(const char* path) {
  close();
  if (path == nullptr) return false;
  char rel[128];
  if (!vfs::resolve(path, storage_, rel, sizeof(rel)) || storage_ == nullptr) return false;
  hal::Stat info;
  if (!storage_->stat(rel, info) || info.type != hal::NodeType::kFile) {
    storage_ = nullptr;
    return false;
  }
  if (!storage_->open(rel, hal::OpenMode::kRead, slot_)) {
    storage_ = nullptr;
    return false;
  }
  at_ = 0;
  len_ = 0;
  ended_ = false;
  have_pending_ = false;
  return true;
}

// **The file is closed by the store that opened it**: a `FileSlot` belongs to the backend and only it knows what is
// in `impl` (docs/70).
void Listing::close() {
  if (slot_.impl != nullptr && storage_ != nullptr) {
    storage_->close(slot_);
  }
  slot_ = hal::FileSlot{};
  storage_ = nullptr;
  at_ = 0;
  len_ = 0;
  ended_ = false;
  have_pending_ = false;
}

bool Listing::fill() {
  if (ended_ || storage_ == nullptr || slot_.impl == nullptr) return false;
  const int32_t got = storage_->read(slot_, buffer_, sizeof(buffer_));
  at_ = 0;
  if (got <= 0) {
    ended_ = true;
    len_ = 0;
    return false;
  }
  len_ = static_cast<uint16_t>(got);
  return true;
}

int Listing::peek() {
  if (at_ >= len_ && !fill()) return -1;
  return static_cast<uint8_t>(buffer_[at_]);
}

int Listing::take() {
  const int c = peek();
  if (c >= 0) ++at_;
  return c;
}

void Listing::skip_space() {
  while (true) {
    const int c = peek();
    if (c < 0 || !is_space(c)) return;
    ++at_;
  }
}

// A JSON string. The only escapes this API uses inside a name are `\"` and `\\`, and a `\u` in the name of an app
// does not exist: whatever arrives is copied as it is and the name stays as readable as it was. **What does not fit is
// dropped and said**: `cut` is the only way a caller can find out, and a text that was cut in the middle is a value
// that lies or a key that never matches (docs/30, docs/75).
bool Listing::read_string(char* out, size_t cap, bool* cut) {
  if (cut != nullptr) *cut = false;
  skip_space();
  if (take() != '"') return false;
  size_t used = 0;
  while (true) {
    const int c = take();
    if (c < 0) return false;
    if (c == '"') break;
    int value = c;
    if (c == '\\') {
      const int next = take();
      if (next < 0) return false;
      if (next == 'n') value = '\n';
      else if (next == 't') value = '\t';
      else if (next == 'r') value = '\r';
      else value = next;
    }
    if (used + 1 < cap) {
      out[used++] = static_cast<char>(value);
    } else if (cut != nullptr) {
      *cut = true;
    }
  }
  out[used] = '\0';
  return true;
}

// The next value as a word: a string between quotes, or whatever is there up to a comma, a colon or a bracket. The
// keys of this document are strings and the values it looks at are strings.
bool Listing::read_word(char* out, size_t cap) {
  skip_space();
  const int c = peek();
  if (c < 0) return false;
  if (c == '"') return read_string(out, cap);
  size_t used = 0;
  while (true) {
    const int next = peek();
    if (next < 0 || next == ',' || next == ':' || next == '}' || next == ']' || is_space(next)) break;
    ++at_;
    if (used + 1 < cap) out[used++] = static_cast<char>(next);
  }
  out[used] = '\0';
  return used > 0;
}

// **One object, one folder**: inside an object `name` and `type` are kept, and when the `type` says `dir` the name
// that had been kept is a folder of this directory. The name is thrown away when a new object starts, so a `type`
// that does not belong to its pair cannot make the name of another one come out (docs/70).
bool Listing::next(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  char key[kKey];
  char value[kName];
  // **The depth of the document is an `int` and not a `char`** (0.88.7): a `char` has a sign on this board, and a
  // document of a hundred and thirty open objects took it past `CHAR_MAX` and into numbers no document has —signed
  // overflow, which is undefined behaviour—. With a negative depth the two resets below stop happening (`depth == 0`
  // never comes) and the `name` of one object becomes a candidate for the `type` of another one that has no name of
  // its own, which is an entry that is not in the answer (found in the review of 4/11, docs/30, docs/75).
  int depth = 0;
  while (true) {
    skip_space();
    const int c = peek();
    if (c < 0) return false;
    if (c == '{') {
      ++depth;
      if (depth == 1) have_pending_ = false;
      ++at_;
      continue;
    }
    if (c == '}') {
      if (depth > 0) --depth;
      if (depth == 0) have_pending_ = false;
      ++at_;
      continue;
    }
    if (c == '"') {
      // **A key that did not fit is not one of ours and it is not compared**: it is read whole out of the document and
      // dropped, and the walk goes on with its value skipped. A key cut at fifteen bytes can only ever fail a
      // comparison, and failing it without a word is how a reader stops seeing a field that is there (docs/30, docs/75).
      bool cut = false;
      if (!read_string(key, sizeof(key), &cut)) return false;
      skip_space();
      if (take() != ':') continue;
      if (!read_word(value, sizeof(value))) continue;
      if (cut) continue;
      if (strcmp(key, "name") == 0) {
        size_t i = 0;
        for (; value[i] != '\0' && i + 1 < sizeof(pending_); ++i) pending_[i] = value[i];
        pending_[i] = '\0';
        have_pending_ = (i > 0);
      } else if (strcmp(key, "type") == 0 && strcmp(value, "dir") == 0 && have_pending_) {
        size_t i = 0;
        for (; pending_[i] != '\0' && i + 1 < cap; ++i) out[i] = pending_[i];
        out[i] = '\0';
        have_pending_ = false;
        return i > 0;
      }
      continue;
    }
    // A number, a `[`, a comma: nothing this document needs.
    ++at_;
  }
}

// **One object, one entry**: the name and the type, and the address to download it from when it is a file. It is the
// same walk as `next` and what it adds is the `download_url`, which comes before the `type` inside the same object
// (docs/70).
bool Listing::next_entry(Kind& kind, char* name, size_t name_cap, char* url, size_t url_cap) {
  if (name == nullptr || name_cap == 0) return false;
  if (url != nullptr && url_cap > 0) url[0] = '\0';
  char key[kKey];
  // **The value of a key is as long as the longest one here**, which is the download address and not a name: a
  // forty-eight byte buffer cut `download_url` in the middle and the file came back as a 404 of fourteen bytes
  // (found on the emulator, 4/11, docs/30).
  char value[kUrl];
  // An `int` for the same reason as in `next` above.
  int depth = 0;
  while (true) {
    skip_space();
    const int c = peek();
    if (c < 0) return false;
    if (c == '{') {
      ++depth;
      if (depth == 1) {
        have_pending_ = false;
        have_url_ = false;
      }
      ++at_;
      continue;
    }
    if (c == '}') {
      if (depth > 0) --depth;
      if (depth == 0) {
        have_pending_ = false;
        have_url_ = false;
      }
      ++at_;
      continue;
    }
    if (c == '"') {
      // **A key that did not fit is not compared and does not take the object's address with it**: it is dropped whole
      // and the walk goes on with the values of this object as if the key were not there, which is what an unknown key
      // of the API is (docs/30, docs/75).
      bool cut = false;
      if (!read_string(key, sizeof(key), &cut)) return false;
      skip_space();
      if (take() != ':') continue;
      if (!read_word(value, sizeof(value))) continue;
      if (cut) continue;
      if (strcmp(key, "name") == 0) {
        size_t i = 0;
        for (; value[i] != '\0' && i + 1 < sizeof(pending_); ++i) pending_[i] = value[i];
        pending_[i] = '\0';
        have_pending_ = (i > 0);
      } else if (strcmp(key, "download_url") == 0) {
        // **A null is a folder**: that is how this API writes it, and it is not an empty address (docs/30).
        size_t i = 0;
        if (strcmp(value, "null") != 0) {
          for (; value[i] != '\0' && i + 1 < sizeof(pending_url_); ++i) pending_url_[i] = value[i];
        }
        pending_url_[i] = '\0';
        have_url_ = (i > 0);
      } else if (strcmp(key, "type") == 0 && have_pending_) {
        const bool file = (strcmp(value, "file") == 0);
        if (!file && strcmp(value, "dir") != 0) continue;
        size_t i = 0;
        for (; pending_[i] != '\0' && i + 1 < name_cap; ++i) name[i] = pending_[i];
        name[i] = '\0';
        kind = file ? Kind::kFile : Kind::kDir;
        if (file && url != nullptr && url_cap > 0) {
          size_t u = 0;
          for (; have_url_ && pending_url_[u] != '\0' && u + 1 < url_cap; ++u) url[u] = pending_url_[u];
          url[u] = '\0';
        }
        have_pending_ = false;
        have_url_ = false;
        return i > 0;
      }
      continue;
    }
    // A number, a `[`, a comma: nothing this document needs.
    ++at_;
  }
}

// **The one listing the system walks at a time**: the object is about 2 300 B of window, name and address, and that is
// static memory and not a frame. It is made the first time somebody asks for it and never given back, which is what the
// board wants: the stack of the loop task is sixteen kilobytes and a reader of two and a half on top of the shell, the
// radio and the install is the road to `canary watchpoint` (docs/75).
Listing& Listing::one() {
  static Listing shared;
  return shared;
}

bool entry_at(const char* path, uint32_t index, Kind& kind, char* name, size_t name_cap, char* url,
              size_t url_cap) {
  Listing& listing = Listing::one();
  if (!listing.open(path)) return false;
  bool found = true;
  for (uint32_t i = 0; i <= index; ++i) {
    found = listing.next_entry(kind, name, name_cap, url, url_cap);
    if (!found) break;
  }
  // **The reader is shared and it does not keep a file open between callers**: the file it was given is closed here,
  // which is what a static object with a handle inside owes to the next one that asks it (docs/70, docs/75).
  listing.close();
  return found;
}

// **The walk in order**: the document is opened once and every `next` comes back where the one before left it, so a
// listing of twenty files is one pass and not twenty (docs/75). It is the same reader as `entry_at`, which is why the
// two cannot be used at the same time —nothing in the system does— and why `stop` gives the file back the way
// `entry_at` does.
bool start(const char* path) {
  Listing& listing = Listing::one();
  return listing.open(path);
}

bool next(Kind& kind, char* name, size_t name_cap, char* url, size_t url_cap) {
  return Listing::one().next_entry(kind, name, name_cap, url, url_cap);
}

void stop() { Listing::one().close(); }

}  // namespace repository
}  // namespace kernel
