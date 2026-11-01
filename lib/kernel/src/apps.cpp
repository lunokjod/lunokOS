#include "kernel/apps.h"

#include <string.h>

#include "json/json.h"
#include "kernel/log.h"
#include "kernel/vfs.h"

namespace kernel {
namespace apps {
namespace {

// A manifest is small by definition; anything bigger is not a manifest.
const size_t kManifestCap = 1024;

// A board name is one width everywhere: the entry's array and the JSON reader have
// to agree, or a name would be cut on one side and complete on the other.
static_assert(sizeof(Entry::boards[0]) == lunok::json::kStringItemCap,
              "board names have to be the same width in the manifest and the entry");

bool manifest_path(char* out, size_t cap, const char* id) {
  static const char kPrefix[] = "/apps/";
  static const char kSuffix[] = "/manifest.json";
  const size_t prefix_len = sizeof(kPrefix) - 1;
  const size_t suffix_len = sizeof(kSuffix);  // includes the terminator
  const size_t id_len = strlen(id);
  if (prefix_len + id_len + suffix_len > cap) return false;

  size_t used = 0;
  memcpy(out + used, kPrefix, prefix_len);
  used += prefix_len;
  memcpy(out + used, id, id_len);
  used += id_len;
  memcpy(out + used, kSuffix, suffix_len);
  return true;
}

// A band answers to a set of board names (one on a chip, the ones it stands in for on
// the bench) and to a chip family. An app runs here when its `boards` list names one
// of them, or the family (docs/40, docs/50).
bool answers_here(const Entry& entry, hal::IPlatform& band) {
  const uint32_t count = band.board_count();
  for (uint32_t i = 0; i < count; ++i) {
    const char* name = band.board_name(i);
    if (name == nullptr) continue;
    if (compatible(entry, name, band.family())) return true;
  }
  return false;
}

// Reads one app into out, which lives on the caller's stack. Returns false and
// says why when the manifest is not usable; the walk then carries on.
bool load(const char* id, Entry& out) {
  const size_t id_len = strlen(id);
  if (id_len == 0 || id_len >= sizeof(out.id)) return false;

  char path[96];
  if (!manifest_path(path, sizeof(path), id)) {
    log_printf(pal::kLogWarn, "APPS", "%s has a name that is too long", id);
    return false;
  }

  char text[kManifestCap];
  size_t len = 0;
  if (!vfs::read_file(path, text, sizeof(text) - 1, len)) {
    log_printf(pal::kLogWarn, "APPS", "%s has no readable manifest", id);
    return false;
  }
  text[len] = '\0';

  memset(&out, 0, sizeof(out));
  memcpy(out.id, id, id_len + 1);
  if (!lunok::json::get_string(text, len, "title", out.title, sizeof(out.title))) {
    log_printf(pal::kLogWarn, "APPS", "%s has no title", id);
    return false;
  }
  if (!lunok::json::get_string(text, len, "entry", out.entry_file, sizeof(out.entry_file))) {
    log_printf(pal::kLogWarn, "APPS", "%s has no entry", id);
    return false;
  }
  // The entry has to stay inside the app and has to be there: an app that is
  // listed and cannot be run is a lie (docs/40).
  if (out.entry_file[0] == '/' || strstr(out.entry_file, "..") != nullptr) {
    log_printf(pal::kLogWarn, "APPS", "%s has an entry outside the app", id);
    return false;
  }
  char entry_path[128];
  if (!manifest_path(entry_path, sizeof(entry_path), id)) return false;
  // manifest_path ends in /manifest.json; cut it and append the entry instead.
  char* slash = strrchr(entry_path, '/');
  if (slash == nullptr) return false;
  const size_t room = sizeof(entry_path) - static_cast<size_t>(slash - entry_path) - 2;
  if (strlen(out.entry_file) > room) {
    log_printf(pal::kLogWarn, "APPS", "%s has an entry name that is too long", id);
    return false;
  }
  slash[1] = '\0';
  strcat(entry_path, out.entry_file);
  hal::Stat info;
  if (!vfs::stat(entry_path, info) || info.type != hal::NodeType::kFile) {
    log_printf(pal::kLogWarn, "APPS", "%s has no entry file (%s)", id, entry_path);
    return false;
  }
  // Version is optional: an app without one is still an app.
  lunok::json::get_string(text, len, "version", out.version, sizeof(out.version));
  // Which boards it runs on is part of being an app: a manifest that does not say
  // it is not an app at all —not listed, not run, not offered as a handler (docs/40).
  // A list that names another board is a different thing: the app is there, and it
  // says what it needs (see `works_here` below).
  if (!lunok::json::get_strings(text, len, "boards", out.boards, Entry::kMaxBoards,
                                out.board_count)) {
    log_printf(pal::kLogWarn, "APPS", "%s does not say which boards it runs on", id);
    return false;
  }
  // The boards of this band, which is the board itself on a chip and the ones it
  // stands in for on the bench: an app that needs nothing of a particular board can
  // be tried on the PC without saying so, and one that names a board this band is not
  // is listed with what it needs and refused when somebody asks to run it, which is
  // more useful than hiding it (docs/40, docs/85).
  hal::IPlatform& band = hal::platform();
  out.works_here = answers_here(out, band);
  // opens is optional too: most apps do not open a file another app gives them.
  char raw_opens[16];
  if (lunok::json::get_string(text, len, "opens", raw_opens, sizeof(raw_opens))) {
    if (!normalise_extension(raw_opens, out.opens, sizeof(out.opens))) {
      log_printf(pal::kLogWarn, "APPS", "%s declares an opens that is not an extension", id);
    }
  }
  return true;
}

class WalkSink : public hal::ListSink {
 public:
  WalkSink(const char* only_id, Visitor visitor, void* context)
      : only_(only_id), visitor_(visitor), context_(context), visited_(0) {}

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.type != hal::NodeType::kDir) return true;
    if (only_ != nullptr && strcmp(entry.name, only_) != 0) return true;

    Entry loaded;
    if (!load(entry.name, loaded)) return true;
    ++visited_;
    if (!visitor_(loaded, context_)) return false;
    // Asking for one app: once found and visited, there is nothing else to do.
    return only_ == nullptr;
  }

  uint32_t visited() const { return visited_; }

 private:
  const char* only_;
  Visitor visitor_;
  void* context_;
  uint32_t visited_;
};

bool count_visitor(const Entry& entry, void* context) {
  (void)entry;
  (void)context;
  return true;
}

}  // namespace

bool compatible(const Entry& entry, const char* board_id, const char* family) {
  if (board_id == nullptr || family == nullptr) return false;
  for (uint32_t i = 0; i < entry.board_count && i < Entry::kMaxBoards; ++i) {
    const char* name = entry.boards[i];
    if (strcmp(name, board_id) == 0) return true;
    if (family[0] != '\0' && strcmp(name, family) == 0) return true;
  }
  return false;
}

uint32_t for_each(const char* only_id, Visitor visitor, void* context) {
  if (visitor == nullptr) return 0;
  WalkSink sink(only_id, visitor, context);
  if (!vfs::list("/apps", sink)) {
    log_printf(pal::kLogWarn, "APPS", "cannot list /apps");
    return 0;
  }
  return sink.visited();
}

uint32_t count() { return for_each(nullptr, &count_visitor, nullptr); }

bool read(const char* id, Entry& out) {
  if (id == nullptr || id[0] == '\0') return false;
  return load(id, out);
}

bool normalise_extension(const char* text, char* out, size_t cap) {
  if (text == nullptr || out == nullptr || cap < 2) return false;
  while (*text == '.' || *text == ' ') ++text;  // "epub" and ".epub" are the same
  size_t used = 0;
  for (; *text != '\0'; ++text) {
    char c = *text;
    if (c == ' ' || c == '.') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
    if (used + 1 >= cap) return false;
    out[used++] = c;
  }
  out[used] = '\0';
  return used > 0;
}

bool extension_of(const char* path, char* out, size_t cap) {
  if (path == nullptr) return false;
  const char* dot = nullptr;
  const char* slash = nullptr;
  for (const char* p = path; *p != '\0'; ++p) {
    if (*p == '.') dot = p;
    if (*p == '/') slash = p;
  }
  // A dot in a directory name is not an extension: the last dot has to come after
  // the last slash, and there has to be something after it.
  if (dot == nullptr || (slash != nullptr && dot < slash)) return false;
  return normalise_extension(dot, out, cap);
}

namespace {

struct HandlerSearch {
  const char* ext;
  Entry* found;
  uint32_t matches;
};

bool handler_visitor(const Entry& entry, void* context) {
  HandlerSearch* search = static_cast<HandlerSearch*>(context);
  if (entry.opens[0] == '\0') return true;
  // A handler this board cannot run is not a handler: it is listed in `apps` with
  // what it needs, and it would fail the moment somebody opened the file (docs/40).
  if (!entry.works_here) return true;
  if (strcmp(entry.opens, search->ext) != 0) return true;
  ++search->matches;
  if (search->found->id[0] == '\0') *search->found = entry;  // the first one wins
  return true;  // keep walking: how many there are is part of the answer
}

}  // namespace

bool find_for_extension(const char* ext, Entry& out, uint32_t& matches) {
  memset(&out, 0, sizeof(out));
  matches = 0;
  if (ext == nullptr || ext[0] == '\0') return false;
  HandlerSearch search;
  search.ext = ext;
  search.found = &out;
  search.matches = 0;
  for_each(nullptr, &handler_visitor, &search);
  matches = search.matches;
  return matches > 0;
}

}  // namespace apps
}  // namespace kernel
