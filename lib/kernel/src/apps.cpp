#include "kernel/apps.h"

#include <stdio.h>
#include <string.h>

#include "json/json.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/vfs.h"

namespace kernel {
namespace apps {

// **One door for the apps** (0.88.20): `/apps` is the virtual directory of the two disks —the flash's `apps/` and the
// card's, with `-SD` on the names the flash also has— and it is the only place this system looks for an app. A folder
// that is not an app is not presented there, a disk the system did not offer is never read, and the listing, the `run`
// and the files of an app all ask the same question (docs/40, docs/70).
const char kAppsDoor[] = "/apps";

namespace {

// A manifest is small by definition; anything bigger is not a manifest.
const size_t kManifestCap = 1024;

// A board name is one width everywhere: the entry's array and the JSON reader have
// to agree, or a name would be cut on one side and complete on the other.
static_assert(sizeof(Entry::boards[0]) == lunok::json::kStringItemCap,
              "board names have to be the same width in the manifest and the entry");

// **The path of one file of an app, given the disk it lives on** (`/littlefs/apps` or `/sd/apps`): a walk already
// knows which one it is listing, so the card is not asked twice for the same app (docs/40, docs/70).
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

// **Which disk answered for a name of the door** (0.88.20): the meter's unit of the storage that `/apps/<id>` resolves
// to, which is the same answer a read of it gets. It is the one question about the disks this module still asks, and it
// asks it to the door and never to `/littlefs/apps` or `/sd/apps` (docs/40, docs/70).
bool comes_from_the_card(const char* name) {
  char path[96];
  if (!under_root(path, sizeof(path), kAppsDoor, name, "")) return false;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  return vfs::unit_of(storage) == meter::kUnitSd;
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
// **The manifest is asked for by the id the door presents** (0.88.20): `/apps/<id>/manifest.json`, which the filesystem
// answers from the flash or from the card, so the caller does not have to know which one (docs/40, docs/70).
bool load(const char* id, Entry& out) {
  const size_t id_len = strlen(id);
  if (id_len == 0 || id_len >= sizeof(out.id)) return false;

  char path[128];
  if (!under_root(path, sizeof(path), kAppsDoor, id, "/manifest.json")) {
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
  if (!under_root(entry_path, sizeof(entry_path), kAppsDoor, id, "/")) return false;
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
  // **A manifest that says too much is not a manifest that says nothing** (0.88.11): the list is refused whole because
  // a board of it would be left out, and the line says which of the two happened —nine boards where this system carries
  // eight is a fact about the file, and telling the person it says nothing is a lie about their own app— (docs/40, docs/75).
  uint32_t boards_total = 0;
  const lunok::json::Strings boards_read =
      lunok::json::get_strings(text, len, "boards", out.boards, Entry::kMaxBoards, out.board_count, &boards_total);
  if (boards_read != lunok::json::Strings::kOk) {
    switch (boards_read) {
      case lunok::json::Strings::kTooMany:
        log_printf(pal::kLogWarn, "APPS", "%s names %u boards and this system carries %u", id,
                   static_cast<unsigned>(boards_total), static_cast<unsigned>(Entry::kMaxBoards));
        break;
      case lunok::json::Strings::kItemTooLong:
        log_printf(pal::kLogWarn, "APPS", "%s has a board name longer than this system carries", id);
        break;
      case lunok::json::Strings::kAbsent:
      case lunok::json::Strings::kNotArray:
        log_printf(pal::kLogWarn, "APPS", "%s does not say which boards it runs on", id);
        break;
      default:
        log_printf(pal::kLogWarn, "APPS", "%s has a list of boards that is not one", id);
        break;
    }
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
    // **The name the door presents is the id of the app** (0.88.20): `squares` is the copy of the flash and
    // `squares-SD` the copy of the card, and each one is run and read by its own name. There is no table of ids here any
    // more and no rule about which disk wins: the door already said what there is, once (docs/40, docs/70).
    Entry loaded;
    if (!load(entry.name, loaded)) return true;
    loaded.on_card = comes_from_the_card(entry.name);
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

// **Whether there is a card in the slot**: the row of the mount is the one that says so —a card the system could not
// mount is not a place to look for anything in—, and that is why the question goes to the VFS and not to a folder
// (docs/50, docs/70).
bool card_here() {
  hal::IStorage* storage = nullptr;
  char rel[4];
  return vfs::resolve("/sd", storage, rel, sizeof(rel));
}

uint32_t for_each(const char* only_id, Visitor visitor, void* context) {
  if (visitor == nullptr) return 0;
  // **One door and one walk** (0.88.20): `/apps` puts the flash's apps and the card's together —with `-SD` on the ones
  // the flash also has— and it is what the listing, the `run` and the files of an app ask. There is no second pass over
  // a disk here, no table of ids and no rule about which copy wins: what the door did not present does not exist for
  // this system, and a disk it did not offer is never read (docs/40, docs/70).
  WalkSink sink(only_id, visitor, context);
  if (!vfs::list(kAppsDoor, sink)) {
    // A door that does not resolve is worth a line; an empty one is not —a board can be flashed with no app at all.
    log_printf(pal::kLogWarn, "APPS", "cannot list %s", kAppsDoor);
  }
  return sink.visited();
}

// **The path of an app is the door** (0.88.20): `/apps/<id>`, whichever disk has it, which is also the path an app reads
// its own `res/` through. Nobody outside the VFS is told which disk an app came from —that is what `on_card` is for— so
// a path of a disk the system did not offer is never built (docs/40, docs/70).
bool root_of(const char* id, char* out, size_t cap) {
  if (id == nullptr || id[0] == '\0' || out == nullptr || cap == 0) return false;
  return under_root(out, cap, kAppsDoor, id, "");
}

// **What the door presents** (0.88.20): a folder of the flash or of the card that carries a `manifest.json` is an app of
// that disk, and one that carries none is not presented at all —neither listed, nor run, nor walked by everyone looking
// for a handler—. **A folder that carries one and names another board still is presented**: the listing says what it
// needs and the `run` refuses it, which is more use than hiding it (docs/40). Which disk is being asked is part of the
// question, because the same name on the two disks is two folders.
bool is_an_app(const char* disk, const char* name) {
  if (disk == nullptr || name == nullptr || name[0] == '\0') return false;
  return has_manifest(disk, name);
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
