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

// **The mark of a redirected id** (0.164.0): the same character `vfs.cpp` calls `kRedirect`, which is the one that
// resolves `/apps/*<folder>` to the disk the plain copy is not on. It is written twice because the two files are two
// layers —the door reads the mark to name the app, the filesystem to find it— and neither of them includes the other
// (docs/40, docs/70).
const char kRedirectMark = '*';

// **The word of a unit, as the name of a redirected app carries it** (0.164.0): `Editor-SD` is the card's copy and
// `Editor-Littlefs` the flash's, and the word is the unit's name as a person says it and not the mount point's.
const char* unit_word(bool on_card) { return on_card ? "SD" : "Littlefs"; }

// **The word is read from the name and not only written into it** (0.166.2): the owner's rule is that an id with a mark
// in front is resolved by looking at the last word behind a dash of the app's name —*«si l'id porta asterisc davant, has
// de mirar l'última paraula amb guió del nom de l'app "Editor-SD"»*—, so a name that already says `SD` or `Littlefs`
// decides the unit by itself: it keeps that word and is not given a second one, and when it says one disk and the door
// answered the other the two are named in the log instead of one of them being hidden (docs/40, docs/70).
bool name_says_unit(const char* name, bool& on_card) {
  const size_t len = strlen(name);
  for (uint32_t i = 0; i < 2; ++i) {
    const bool card = (i == 0);
    const char* word = unit_word(card);
    const size_t word_len = strlen(word);
    // A dash of its own in front of the word, and something behind it: `-SD` is the word and `SD` alone is a name.
    if (len < word_len + 2) continue;
    if (name[len - word_len - 1] != '-') continue;
    if (strcmp(name + len - word_len, word) != 0) continue;
    on_card = card;
    return true;
  }
  return false;
}

// A band answers to a set of board names (one on a chip, the ones it stands in for on
// the emulator) and to a chip family. An app runs here when its `boards` list names one
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

// **A manifest's `opens` is a list written in one string** (0.120.0): `"txt"` and `"txt md lua json"` are the same
// field with one name or with fifteen, which is how the apps of PaperOS write it. Every word goes through the same
// normalisation a single extension used to (`"TXT"`, `".txt"` and `"txt"` are one thing), and a word that is not an
// extension is dropped with a line that names it: an app that says `my file` handles nothing, and saying nothing about
// it would leave a person wondering why their app never opens anything (docs/40).
void add_opens(Entry& out, const char* raw, const char* id) {
  const char* word = raw;
  uint32_t named = 0;
  while (*word != '\0') {
    while (*word == ' ') ++word;
    if (*word == '\0') break;
    const char* end = word;
    while (*end != '\0' && *end != ' ') ++end;
    const size_t word_len = static_cast<size_t>(end - word);
    ++named;
    if (named > Entry::kMaxOpens) {
      // Counted and thrown away, as `boards` does it: how many there are is the difference between "it declares more
      // than this system carries" and "it declares nothing" (docs/40).
      word = end;
      continue;
    }
    // **A name that does not fit is not cut**: eleven bytes is what a row of the entry holds, and an extension with its
    // last letter missing would be a handler for a file nobody has, so it is dropped with the two numbers instead
    // (docs/40, docs/75).
    if (word_len + 1 > sizeof(out.opens[0])) {
      log_printf(pal::kLogWarn, "APPS", "%s names an extension of %u bytes and this system carries %u", id,
                 static_cast<unsigned>(word_len), static_cast<unsigned>(sizeof(out.opens[0]) - 1));
      word = end;
      continue;
    }
    char one[sizeof(out.opens[0])];
    memcpy(one, word, word_len);
    one[word_len] = '\0';
    if (!normalise_extension(one, out.opens[out.opens_count], sizeof(out.opens[0]))) {
      log_printf(pal::kLogWarn, "APPS", "%s declares an opens that is not an extension (%s)", id, one);
    } else {
      ++out.opens_count;
    }
    word = end;
  }
  if (named > Entry::kMaxOpens) {
    log_printf(pal::kLogWarn, "APPS", "%s names %u extensions and this system carries %u", id, static_cast<unsigned>(named),
               static_cast<unsigned>(Entry::kMaxOpens));
  }
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

  // **The manifest is read into the one buffer of the walk** (0.120.0): a kilobyte on the stack of the task that runs
  // an app, on top of the entry it is filling in (`on_entry` measured 1 104 B) and of the shell that asked for it
  // (`execute`, 2 640 B), was the deepest chain of the project —6,7 KB of the eight the task has—. `load` is never
  // re-entered: `read` and the walk are called one at a time, from the same task, and no visitor of `for_each` loads
  // another manifest (docs/40, docs/75).
  static char text[kManifestCap];
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
  // **A redirected app says which unit it is from at the end of its name** (0.164.0): the id `*editor` is the copy the
  // door presented with a mark, and its name is `Editor-SD` —a person reads a name and not a path, and the mark alone
  // does not say where the copy is—. **And a name that already says it is believed** (0.166.2): the word is read first
  // and written only when the name has none, so a manifest titled `Editor-SD` stays `Editor-SD` instead of gaining a
  // second word, and a name that disagrees with the disk the door answered is said in the log (docs/40, docs/70).
  if (id[0] == kRedirectMark) {
    const bool from_disk = comes_from_the_card(id);
    bool said = from_disk;
    if (name_says_unit(out.title, said)) {
      out.on_card = said;
      if (said != from_disk) {
        log_printf(pal::kLogWarn, "APPS", "%s is named %s and the door answered the other disk", id, out.title);
      }
    } else {
      out.on_card = from_disk;
      const char* word = unit_word(out.on_card);
      const size_t title_len = strlen(out.title);
      const size_t word_len = strlen(word);
      if (title_len + word_len + 2 <= sizeof(out.title)) {
        out.title[title_len] = '-';
        memcpy(out.title + title_len + 1, word, word_len + 1);
      }
    }
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
  // stands in for on the emulator: an app that needs nothing of a particular board can
  // be tried on the PC without saying so, and one that names a board this band is not
  // is listed with what it needs and refused when somebody asks to run it, which is
  // more useful than hiding it (docs/40, docs/85).
  hal::IPlatform& band = hal::platform();
  out.works_here = answers_here(out, band);
  // opens is optional too: most apps do not open a file another app gives them. A list of names is the same field
  // written with the spaces of the manifests of PaperOS, and an app that names none opens nothing (docs/40).
  char raw_opens[96];
  if (lunok::json::get_string(text, len, "opens", raw_opens, sizeof(raw_opens))) {
    add_opens(out, raw_opens, id);
  }
  // **The switches the app wants when it is handed a file** (0.120.0): `opensArgs`, `-ro` in PaperOS's Editor. The
  // system does not interpret them —only the app knows what its own switches mean—, so the only thing checked here is
  // that they look like switches: a value that does not begin with `-` is dropped with a line, because handing an app a
  // stray word as one of its own flags is worse than handing it nothing (docs/30, docs/40). It is read straight into
  // the entry and the bad one is cleared there: 32 bytes less on the stack of a frame that already holds a whole
  // manifest, which is the frame `apps` walks one app at a time (docs/75).
  if (lunok::json::get_string(text, len, "opensArgs", out.opens_arg, sizeof(out.opens_arg))) {
    char* start = out.opens_arg;
    while (*start == ' ') ++start;
    if (*start != '-') {
      log_printf(pal::kLogWarn, "APPS", "%s has an opensArgs that is not a switch (%s)", id, start);
      out.opens_arg[0] = '\0';
    } else if (start != out.opens_arg) {
      memmove(out.opens_arg, start, strlen(start) + 1);
    }
  }
  // **Whether the app is a launcher of its own** (0.153.0): `"launcher": true` is what keeps a launcher out of another
  // launcher's grid, and an app that says it is still an app —listed, run, and the handler of what it opens—. A value
  // that is neither `true` nor `false` is left out with a line instead of being guessed: `"launcher": "yes"` is not a
  // boolean and reading it as one would make a manifest say what it does not (docs/40).
  bool launcher = false;
  if (lunok::json::get_bool(text, len, "launcher", launcher)) {
    out.launcher = launcher;
  } else if (lunok::json::has_key(text, len, "launcher")) {
    log_printf(pal::kLogWarn, "APPS", "%s has a launcher that is not true or false", id);
  }
  // **Who wrote the app and what it is** (0.120.0): decoration, so nothing is refused here —a manifest without them is
  // still an app— and a text longer than its room is cut, which is what docs/40 promises. `\n` is kept, because the
  // descriptions of PaperOS are two lines and `apps info` prints them as two.
  lunok::json::get_string(text, len, "author", out.author, sizeof(out.author));
  lunok::json::get_string(text, len, "description", out.description, sizeof(out.description));
  // **The badge of the app** (0.120.0): a picture inside the app, checked exactly as the splash is checked, for the
  // same reason —a manifest that points out of its own folder is a manifest that lies about what it carries—. There is
  // no launcher on the glass yet, so nothing paints it; `apps info` is where it is said (docs/40).
  if (lunok::json::get_string(text, len, "icon", out.icon, sizeof(out.icon))) {
    if (out.icon[0] == '/' || strstr(out.icon, "..") != nullptr) {
      log_printf(pal::kLogWarn, "APPS", "%s names an icon outside the app", id);
      out.icon[0] = '\0';
    }
  }
  // **The picture of the app, if it says one** (0.114.0): the name of a file inside the app —`res/logo.png`—
  // that the system paints before the interpreter starts. A name that is not inside the app is dropped with a
  // line, because a manifest that points out of its own folder is a manifest that lies about what it carries;
  // a name that is not a picture is kept here and said when it is painted, which is where the failure is known
  // (docs/40).
  char raw_splash[48];
  if (lunok::json::get_string(text, len, "splash", raw_splash, sizeof(raw_splash))) {
    if (raw_splash[0] == '/' || strstr(raw_splash, "..") != nullptr) {
      log_printf(pal::kLogWarn, "APPS", "%s names a splash outside the app", id);
    } else {
      snprintf(out.splash, sizeof(out.splash), "%s", raw_splash);
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

// **An app handles a list and not a name** (0.120.0): the two things that read a handler —the search `open` does and
// the question the shell asks when there is more than one— go through here, so what a manifest declares means the same
// thing in both (docs/40).
bool handles(const Entry& entry, const char* ext) {
  if (ext == nullptr || ext[0] == '\0') return false;
  for (uint32_t i = 0; i < entry.opens_count && i < Entry::kMaxOpens; ++i) {
    if (strcmp(entry.opens[i], ext) == 0) return true;
  }
  return false;
}

size_t opens_text(const Entry& entry, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  size_t used = 0;
  out[0] = '\0';
  for (uint32_t i = 0; i < entry.opens_count && i < Entry::kMaxOpens; ++i) {
    const size_t need = strlen(entry.opens[i]) + ((used > 0) ? 1 : 0);
    if (used + need + 1 > cap) break;  // what does not fit is not said, and the cap is what docs/40 promises
    if (used > 0) out[used++] = ' ';
    for (const char* p = entry.opens[i]; *p != '\0'; ++p) out[used++] = *p;
  }
  out[used] = '\0';
  return used;
}

namespace {

struct HandlerSearch {
  const char* ext;
  Entry* found;
  uint32_t matches;
};

bool handler_visitor(const Entry& entry, void* context) {
  HandlerSearch* search = static_cast<HandlerSearch*>(context);
  if (entry.opens_count == 0) return true;
  // A handler this board cannot run is not a handler: it is listed in `apps` with
  // what it needs, and it would fail the moment somebody opened the file (docs/40).
  if (!entry.works_here) return true;
  if (!handles(entry, search->ext)) return true;
  ++search->matches;
  if (search->found->id[0] == '\0') *search->found = entry;  // the first one wins
  return true;  // keep walking: how many there are is part of the answer
}

}  // namespace

bool find_for_extension(const char* ext, Entry& out, uint32_t& matches) {
  memset(&out, 0, sizeof(out));
  matches = 0;
  if (ext == nullptr || ext[0] == '\0') return false;
  // **The name of a manifest is normalised and the one a person types is not** (0.152.4): `"TXT"`, `".txt"` and `"txt"`
  // are one thing when a manifest declares it (`normalise_extension`) and were three when a file asked for it, so a file
  // called `NOTES.TXT` opened nothing. The query goes through the same door as the declaration.
  char wanted[Entry::kMaxOpens ? 16 : 16];
  if (!normalise_extension(ext, wanted, sizeof(wanted))) return false;
  HandlerSearch search;
  search.ext = wanted;
  search.found = &out;
  search.matches = 0;
  for_each(nullptr, &handler_visitor, &search);
  matches = search.matches;
  return matches > 0;
}

// **The views of the app in front** (0.163.0): the number of pages the app has put on top of its first one, and
// nothing else. It is one byte for the whole system because there is one app in front at a time, and it is zero for
// an app that never says anything —which is the whole of the apps that have not been taught to say it, and they are
// then apps that a pinch closes entirely, which is the old behaviour and the one their page asks for (docs/30).
namespace {
uint8_t g_views = 0;
}  // namespace

uint8_t views() { return g_views; }

uint8_t view_push() {
  // **A stack that stops at the ceiling it has**: an app that pushes a hundred pages has a bug, and a byte that
  // wraps would take it back to «no views» and close it with a pinch. The ceiling is the sixteen of a small page and
  // it is said instead of wrapped (docs/30).
  if (g_views < 0xFF) ++g_views;
  return g_views;
}

uint8_t view_pop() {
  if (g_views > 0) --g_views;
  return g_views;
}

void reset_views() { g_views = 0; }

}  // namespace apps
}  // namespace kernel
