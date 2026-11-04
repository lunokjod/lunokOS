#include "kernel/repository.h"

#include <string.h>

#include "kernel/vfs.h"

namespace kernel {
namespace repository {
namespace {

// Una paraula del document, i prou: el que aquest lector mira són les claus i els valors de cada objecte.
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

// **El fitxer es tanca pel magatzem que l'ha obert**: un `FileSlot` és del backend i només ell sap què hi ha a
// `impl` (docs/70).
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

// Una cadena de JSON. Els únics escapes que l'API de GitHub fa servir als noms són `\"` i `\\`, i un `\u`
// d'un nom d'app no existeix: el que arribi es copia tal qual i el nom queda igual de llegible.
bool Listing::read_string(char* out, size_t cap) {
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
    if (used + 1 < cap) out[used++] = static_cast<char>(value);
  }
  out[used] = '\0';
  return true;
}

// El valor següent com una paraula: una cadena entre cometes, o el que hi hagi fins a una coma, un dos-punts
// o un claudàtor. Les claus d'aquest document són cadenes i els valors que miren són cadenes.
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

// **Un objecte, una carpeta**: dins d'un objecte es guarden `name` i `type`, i quan el `type` diu `dir` el nom
// que s'havia guardat és una carpeta d'aquest directori. El nom es llença en començar un objecte nou, o sigui
// que un `type` que no és de la seva parella no pot fer sortir el nom d'un altre (docs/70).
bool Listing::next(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  char key[16];
  char value[kName];
  char depth = 0;
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
      if (!read_string(key, sizeof(key))) return false;
      skip_space();
      if (take() != ':') continue;
      if (!read_word(value, sizeof(value))) continue;
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
    // Un número, un `[`, una coma: res que aquest document necessiti.
    ++at_;
  }
}

// **Un objecte, una entrada**: el nom i el tipus, i l'adreça de descàrrega quan és un fitxer. És el mateix camí que
// `next` i el que afegeix és el `download_url`, que ve abans del `type` dins del mateix objecte (docs/70).
bool Listing::next_entry(Kind& kind, char* name, size_t name_cap, char* url, size_t url_cap) {
  if (name == nullptr || name_cap == 0) return false;
  if (url != nullptr && url_cap > 0) url[0] = '\0';
  char key[16];
  // **The value of a key is as long as the longest one here**, which is the download address and not a name: a
  // forty-eight byte buffer cut `download_url` in the middle and the file came back as a 404 of fourteen bytes
  // (found on the bench, 4/11, docs/30).
  char value[kUrl];
  char depth = 0;
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
      if (!read_string(key, sizeof(key))) return false;
      skip_space();
      if (take() != ':') continue;
      if (!read_word(value, sizeof(value))) continue;
      if (strcmp(key, "name") == 0) {
        size_t i = 0;
        for (; value[i] != '\0' && i + 1 < sizeof(pending_); ++i) pending_[i] = value[i];
        pending_[i] = '\0';
        have_pending_ = (i > 0);
      } else if (strcmp(key, "download_url") == 0) {
        // **Un nul és una carpeta**: l'API el posa així i no és una adreça buida (docs/30).
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
    // Un número, un `[`, una coma: res que aquest document necessiti.
    ++at_;
  }
}

bool entry_at(const char* path, uint32_t index, Kind& kind, char* name, size_t name_cap, char* url,
              size_t url_cap) {
  Listing listing;
  if (!listing.open(path)) return false;
  bool found = false;
  for (uint32_t i = 0; i <= index; ++i) {
    found = listing.next_entry(kind, name, name_cap, url, url_cap);
    if (!found) return false;
  }
  return found;
}

}  // namespace repository
}  // namespace kernel
