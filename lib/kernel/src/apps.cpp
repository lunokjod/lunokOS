#include "kernel/apps.h"

#include <stdio.h>
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

// **The path of one file of an app, given the drive it lives on** (`/apps` or `/sd/apps`): a walk already knows
// which one it is listing, so the card is not asked twice for the same app (docs/40, docs/70).
bool under_root(char* out, size_t cap, const char* base, const char* id, const char* tail) {
  const int written = snprintf(out, cap, "%s/%s%s", base, id, tail);
  return written > 0 && static_cast<size_t>(written) < cap;
}

// **A manifest is what makes a directory an app**: the question "does this id live here?" is one stat (docs/40).
bool has_manifest(const char* base, const char* id) {
  char path[128];
  if (!under_root(path, sizeof(path), base, id, "/manifest.json")) return false;
  hal::Stat info;
  return vfs::stat(path, info) && info.type == hal::NodeType::kFile;
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
bool load(const char* base, const char* id, Entry& out) {
  const size_t id_len = strlen(id);
  if (id_len == 0 || id_len >= sizeof(out.id)) return false;

  char path[128];
  if (!under_root(path, sizeof(path), base, id, "/manifest.json")) {
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
  if (!under_root(entry_path, sizeof(entry_path), base, id, "/")) return false;
  // The path above ends at the app's own root: the entry goes after that slash (docs/40).
  const size_t room = sizeof(entry_path) - strlen(entry_path) - 1;
  if (strlen(out.entry_file) > room) {
    log_printf(pal::kLogWarn, "APPS", "%s has an entry name that is too long", id);
    return false;
  }
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

  // **The root this walk is listing**, so a manifest is looked for where the entry came from and not twice
  // (docs/40, docs/70).
  void set_base(const char* base) { base_ = base; }

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.type != hal::NodeType::kDir) return true;
    if (only_ != nullptr && strcmp(entry.name, only_) != 0) return true;
    // **An app of the card is the one that runs**: the card is walked first and the flash does not repeat it
    // (0.86.0, docs/40).
    if (already(entry.name)) return true;

    Entry loaded;
    if (!load(base_, entry.name, loaded)) return true;
    remember(entry.name);
    ++visited_;
    if (!visitor_(loaded, context_)) return false;
    // Asking for one app: once found and visited, there is nothing else to do.
    return only_ == nullptr;
  }

  uint32_t visited() const { return visited_; }

 private:
  // How many ids this walk remembers, which is how many apps a card may shadow. Past this the flash repeats one
  // —the list shows it twice and the card is still the one that runs—, and the number is one constant (docs/75).
  static const uint8_t kSeen = 24;

  bool already(const char* id) const {
    for (uint8_t i = 0; i < seen_count_; ++i) {
      if (strcmp(seen_[i], id) == 0) return true;
    }
    return false;
  }

  void remember(const char* id) {
    if (seen_count_ >= kSeen) return;
    snprintf(seen_[seen_count_], sizeof(seen_[0]), "%s", id);
    ++seen_count_;
  }

  const char* only_;
  Visitor visitor_;
  void* context_;
  uint32_t visited_;
  const char* base_ = "/apps";
  char seen_[kSeen][sizeof(Entry::id)];
  uint8_t seen_count_ = 0;
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
  // **The card first and the flash after it** (0.86.0): an app installed on the card is the one that runs, and the
  // ones the flash has that the card does not are visited all the same. A drive that is not mounted is an empty
  // list and not an error, and a folder of the card that is not an app is skipped by `load` (docs/40, docs/70).
  WalkSink sink(only_id, visitor, context);
  sink.set_base("/sd/apps");
  vfs::list("/sd/apps", sink);
  if (only_id != nullptr && sink.visited() > 0) return sink.visited();
  sink.set_base("/apps");
  if (!vfs::list("/apps", sink)) {
    log_printf(pal::kLogWarn, "APPS", "cannot list /apps");
  }
  return sink.visited();
}

// **On viu una app, si és que hi viu** (0.86.0): la targeta primer, la flash després, i per a una app que no hi és
// el camí de la flash, que és el que un missatge d'error ha d'ensenyar (docs/40, docs/70).
bool root_of(const char* id, char* out, size_t cap) {
  if (id == nullptr || id[0] == '\0' || out == nullptr || cap == 0) return false;
  const char* base = has_manifest("/sd/apps", id) ? "/sd/apps" : "/apps";
  return under_root(out, cap, base, id, "");
}

uint32_t count() { return for_each(nullptr, &count_visitor, nullptr); }

bool read(const char* id, Entry& out) {
  if (id == nullptr || id[0] == '\0') return false;
  char root[96];
  if (!root_of(id, root, sizeof(root))) return false;
  // The root is the whole path of the app; `load` wants the drive it lives on, which is that path without the id.
  char base[64];
  snprintf(base, sizeof(base), "%.*s", static_cast<int>(strlen(root) - strlen(id) - 1), root);
  return load(base, id, out);
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
