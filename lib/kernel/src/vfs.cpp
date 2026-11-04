#include "kernel/vfs.h"

#include <stdio.h>
#include <string.h>

#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/nvs_fs.h"

namespace kernel {
namespace vfs {
namespace {

const size_t kMaxPathBytes = 128;
// The rows the table can hold. It was 8, which was exactly what the Paper had —littlefs, apps, res,
// data, sd, rtc, nvs and the `/cfg` door— so the FRAM did not fit and `/cfg` disappeared without a
// word (found on the bench the 31/10, writing a setting: `E_INVAL`). Ten leaves the room of two more
// mounts, and a table that is full now says so in the log instead of dropping a row (docs/70).
// Twelve since 3/11: the Paper has every row it can have —the flash, its three aliases, the faces,
// the card, the two memories, the key-value store and `/cfg`— and that is ten, which left no room
// at all; the faces of `/fonts` are the new row (docs/30, docs/70).
const uint32_t kMaxMounts = 12;

struct Mount {
  const char* path;  // virtual path, no trailing slash
  const char* rel;   // prefix inside the backend ("" or "apps")
  const char* kind;  // "littlefs", "rtc", "fram"…
  hal::IStorage* storage;
  bool writable;
  // **A mount can be one file and not a directory**: `/rtc` and `/fram` are memories, and a root
  // listing that calls them directories is a lie a person sees at once (docs/70, docs/85).
  bool file;
  uint32_t size;
};

Mount g_mounts[kMaxMounts];
uint32_t g_count = 0;

}  // namespace

// **Which unit is behind a storage.** The mount table is the only place that knows, and the meters ask
// it: a read of the card and a read of the flash are two rows and not one (docs/30, docs/70).
uint8_t unit_of(const hal::IStorage* storage) {
  for (uint32_t i = 0; i < g_count; ++i) {
    if (g_mounts[i].storage != storage) continue;
    const char* kind = g_mounts[i].kind;
    if (kind != nullptr) {
      if (strcmp(kind, "sd") == 0) return meter::kUnitSd;
      if (strcmp(kind, "nvs") == 0) return meter::kUnitNvs;
      if (strcmp(kind, "rtc") == 0) return meter::kUnitRtc;
      if (strcmp(kind, "fram") == 0) return meter::kUnitFram;
    }
    return meter::kUnitFlash;
  }
  return meter::kUnitFlash;
}

namespace {

void add(const char* path, const char* rel, const char* kind, hal::IStorage* storage,
         bool writable, bool file = false) {
  if (storage == nullptr) return;
  if (g_count >= kMaxMounts) {
    // A mount that does not fit is a path that stops existing, and a person would see the file
    // system lose a row for no reason: it is said, and the number is one to raise (docs/70).
    log_printf(pal::kLogError, "VFS", "no room for %s, the table has %u rows", path,
               static_cast<unsigned>(kMaxMounts));
    return;
  }
  Mount& m = g_mounts[g_count++];
  m.path = path;
  m.rel = rel;
  m.kind = kind;
  m.storage = storage;
  m.writable = writable;
  m.file = file;
  m.size = 0;
  if (file) {
    hal::Stat info;
    if (storage->stat(rel, info)) m.size = info.size;
  }
}

}  // namespace

namespace {

// The storage of a door of the table, or null when that door is not mounted: the two disks of `/apps` are asked for by
// the name of their row (docs/70).
hal::IStorage* storage_of(const char* door) {
  for (uint32_t i = 0; i < g_count; ++i) {
    if (strcmp(g_mounts[i].path, door) == 0) return g_mounts[i].storage;
  }
  return nullptr;
}

// **`apps/<name>` in a buffer of a known size**: a `snprintf` with an unbounded `%s` is a cut the compiler cannot see,
// and half a path is a file somewhere the system did not mean (docs/70).
bool join_apps(char* out, size_t cap, const char* name) {
  if (out == nullptr || name == nullptr) return false;
  const size_t len = strlen(name);
  if (len + 6 > cap) return false;  // "apps/", the name and the terminator
  memcpy(out, "apps/", 5);
  memcpy(out + 5, name, len + 1);
  return true;
}

// **The mark of the app that lives on the card** when the flash carries one with the same name (0.88.20): a person sees
// `squares` and `squares-SD` and can run the two of them.
const char kCardSuffix[] = "-SD";

// **The two halves of the door, whole** (0.88.20): the presenter is asked about a name and the disk it came from, because
// `squares` on the flash and `squares` on the card are two folders and only one of them may be an app.
const char kFlashApps[] = "/littlefs/apps";
const char kCardApps[] = "/sd/apps";

// **What an `/apps` listing presents**: the filesystem cannot know what an app is —a folder that carries a manifest, a
// board it does not run on— and the door of the apps is where the system says what there is, so the ones it does not
// want to present are not in the listing at all (0.88.20, docs/40, docs/70). A null presenter presents everything,
// which is what a test and a disk with no kernel see.
AppsPresenter g_presenter = nullptr;

// **What the system presents and nothing else**: the presenter is asked for every name of the door and the ones it
// refuses never reach the caller —an app the system does not want to show is not listed, and a disk does not have to be
// read twice to find that out— (0.88.20, docs/40, docs/70).
class PresentedSink : public hal::ListSink {
 public:
  PresentedSink(AppsPresenter presenter, const char* disk, hal::ListSink& inner)
      : presenter_(presenter), disk_(disk), inner_(inner) {}

  bool on_entry(const hal::ListEntry& entry) override {
    if (presenter_ != nullptr && entry.name != nullptr && !presenter_(disk_, entry.name)) return true;
    return inner_.on_entry(entry);
  }

 private:
  AppsPresenter presenter_;
  const char* disk_;
  hal::ListSink& inner_;
};

// **The card's half of `/apps`**: an app of the card whose name the door also presents from the flash is presented as
// `<name>-SD`, so a person sees both and can run both —`squares` is the one of the flash and `squares-SD` the one of the
// card— instead of one of them disappearing behind the other (0.88.20, docs/40, docs/70).
class CardSink : public hal::ListSink {
 public:
  CardSink(hal::IStorage* flash, hal::ListSink& inner) : flash_(flash), inner_(inner) {}

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.name == nullptr) return true;
    if (flash_ == nullptr) return inner_.on_entry(entry);
    // **The suffix says there are two of that name in the door**, so the flash's folder has to be one the door presents:
    // a folder of the flash that is not an app does not push the card's copy into `-SD`, because there is nothing to tell
    // the two apart from (docs/40, docs/70).
    char rel[kMaxPathBytes];
    hal::Stat info;
    if (!join_apps(rel, sizeof(rel), entry.name) || !flash_->stat(rel, info)) return inner_.on_entry(entry);
    if (g_presenter != nullptr && !g_presenter(kFlashApps, entry.name)) return inner_.on_entry(entry);
    const size_t len = strlen(entry.name);
    if (len + 4 > sizeof(renamed_)) return inner_.on_entry(entry);
    memcpy(renamed_, entry.name, len);
    memcpy(renamed_ + len, kCardSuffix, sizeof(kCardSuffix));  // "-SD" and the terminator
    hal::ListEntry copy = entry;
    copy.name = renamed_;
    return inner_.on_entry(copy);
  }

 private:
  hal::IStorage* flash_;
  hal::ListSink& inner_;
  char renamed_[64];
};

// **The two disks of `/apps` in one listing**: the flash first and the card after it, with the names of the card that
// the flash also has carrying `-SD` (0.88.20, docs/40, docs/70).
bool list_the_apps(hal::ListSink& sink) {
  hal::IStorage* flash = storage_of("/apps");
  hal::IStorage* card = storage_of("/sd");
  bool any = false;
  if (flash != nullptr) {
    PresentedSink shown(g_presenter, kFlashApps, sink);
    meter::Scope scope(meter::storage_id(meter::kOpList, meter::kUnitFlash));
    if (flash->list("apps", shown)) any = true;
  }
  if (card != nullptr) {
    CardSink suffix(flash, sink);
    PresentedSink shown(g_presenter, kCardApps, suffix);
    meter::Scope scope(meter::storage_id(meter::kOpList, meter::kUnitSd));
    if (card->list("apps", shown)) any = true;
  }
  return any;
}

}  // namespace

void set_apps_presenter(AppsPresenter presenter) { g_presenter = presenter; }

uint32_t init(hal::IPlatform& platform) {
  g_count = 0;

  hal::IStorage* littlefs = platform.storage("/littlefs");
  if (littlefs != nullptr) {
    // The disk itself is the person's: readable and writable from the prompt. The aliases
    // below carry the flags an app-facing view needs, but what an app may touch is decided
    // by the policy in fs_ops, not by these (docs/70).
    add("/littlefs", "", "littlefs", littlefs, true);
    // Aliases cost one table row each: they resolve here, they are not copies. `/res` is
    // one for a person —the sample files of the disk— and a door for an app: the
    // interpreter opens it onto that app's own `apps/<id>/res/` (docs/70). `/cfg` is not
    // even an alias: it goes wherever the settings live, and that is cfg::boot's to say.
    //
    // **`/apps` is the virtual directory of the two disks** (0.88.19): what a person lists there is `littlefs/apps`
    // and `sd/apps` in one, and the card goes first —an app of the card is the one that runs—. The folder is **made**
    // when the disk does not have it: the bench carries no app at all (0.88.2) and `repo install` writes one into a
    // disk that had none, so a row that only appeared when the folder was there at boot was a door that stayed shut
    // until the next boot —the install worked, `apps` listed the app (it walks the drive) and `run` answered `cannot
    // read: /apps/<id>/<entry>` (found on the bench, 4/11, docs/40, docs/70).
    hal::Stat apps_dir;
    littlefs->mkdir("apps");  // there is no folder to make when there already is one
    if (littlefs->stat("apps", apps_dir) && apps_dir.type == hal::NodeType::kDir) {
      add("/apps", "apps", "littlefs", littlefs, false);
    }
    add("/res", "res", "littlefs", littlefs, false);
    add("/data", "data", "littlefs", littlefs, true);
    // **The faces of the system are one row and not a copy per app** (3/11). A real face is ten to
    // twenty kilobytes and an app that wants one used to carry its own copy in `apps/<id>/res/`,
    // so the same DejaVu sat in two apps and the disk paid it twice. `/fonts` is the disk's
    // `res/fonts/` seen by everybody, read-only for an app like `/apps` (docs/30, docs/70).
    add("/fonts", "res/fonts", "littlefs", littlefs, false);
  } else {
    log_printf(pal::kLogError, "VFS", "/littlefs is mandatory and did not mount");
  }

  hal::IStorage* sd = platform.storage("/sd");
  if (sd != nullptr) {
    // **And the same folder on the card** (0.88.19): an app installed on the card lives in `sd/apps/<id>`, and a card
    // without the folder is a card an install has to make room on —which it does— but the door of the apps is also
    // what a listing goes through (docs/70).
    sd->mkdir("apps");
    add("/sd", "", "sd", sd, true);
  }

  // /rtc: the RTC memory as one file the size of the memory. It is the only memory that survives a
  // deep sleep and dies with the power, so what is written there is what a board keeps between two
  // runs without touching the flash (docs/50, docs/70).
  hal::IStorage* rtc = platform.storage("/rtc");
  if (rtc != nullptr) add("/rtc", "", "rtc", rtc, true, true);

  // /fram: the FRAM of the board, when it has the chip, as one file of its size. It is
  // non-volatile and needs no power, so it is where an app keeps the few bytes it cannot lose even
  // if the battery goes (docs/50, docs/70).
  hal::IStorage* fram = platform.storage("/fram");
  if (fram != nullptr) add("/fram", "", "fram", fram, true, true);

  // /nvs: the key-value store seen as two levels of files, a namespace per
  // directory (docs/70). Read-only until the write side of M4 is done.
  hal::IStorage* nvs = nvs_storage(platform.nvs());
  if (nvs != nullptr) add("/nvs", "", "nvs", nvs, false);

  return g_count;
}

uint32_t mount_count() { return g_count; }

void set_cfg_storage(hal::IStorage* storage, const char* kind) {
  static const char kPath[] = "/cfg";
  for (uint32_t i = 0; i < g_count; ++i) {
    if (strcmp(g_mounts[i].path, kPath) != 0) continue;
    if (storage == nullptr) {
      // The row goes away with the unit: a door to nowhere is not a mount.
      for (uint32_t j = i + 1; j < g_count; ++j) g_mounts[j - 1] = g_mounts[j];
      --g_count;
      if (i < g_count) g_mounts[g_count] = Mount();
      return;
    }
    g_mounts[i].kind = (kind != nullptr) ? kind : "";
    g_mounts[i].storage = storage;
    g_mounts[i].writable = true;
    return;
  }
  // Writable: an app's own settings are its own ground, and making it read-only would
  // leave every app unable to keep anything (docs/70).
  if (storage != nullptr) add(kPath, "cfg", (kind != nullptr) ? kind : "", storage, true);
}

const char* mount_path(uint32_t index) {
  if (index >= g_count) return "";
  return g_mounts[index].path;
}

const char* mount_kind(uint32_t index) {
  if (index >= g_count) return "";
  return g_mounts[index].kind;
}

bool mount_writable(uint32_t index) {
  if (index >= g_count) return false;
  return g_mounts[index].writable;
}

bool resolve(const char* path, hal::IStorage*& storage, char* rel_out, size_t rel_cap) {
  storage = nullptr;
  if (path == nullptr || rel_out == nullptr || rel_cap == 0) return false;

  // Paths are absolute by design. A path that does not start with a slash is taken
  // from the root, because the shell lets people type "ls apps" and it is not
  // useful to shout at them.
  char absolute[kMaxPathBytes];
  const char* full = path;
  if (path[0] != '/') {
    size_t used = 0;
    absolute[used++] = '/';
    for (const char* p = path; *p != '\0' && used + 1 < sizeof(absolute); ++p) {
      absolute[used++] = *p;
    }
    absolute[used] = '\0';
    full = absolute;
  }

  const size_t path_len = strlen(full);
  if (path_len >= kMaxPathBytes) return false;
  // "." and ".." never reach a backend: this is also the app sandbox (docs/70).
  if (strstr(full, "..") != nullptr) return false;

  const Mount* best = nullptr;
  size_t best_len = 0;
  for (uint32_t i = 0; i < g_count; ++i) {
    const Mount& m = g_mounts[i];
    const size_t len = strlen(m.path);
    if (len > path_len) continue;
    if (strncmp(full, m.path, len) != 0) continue;
    if (full[len] != '\0' && full[len] != '/') continue;
    if (len > best_len) {
      best = &m;
      best_len = len;
    }
  }
  if (best == nullptr) return false;

  const char* rest = full + best_len;
  while (*rest == '/') ++rest;
  // **A name of `/apps` is answered by the disk that has it**: the flash first —`squares` is the app of the flash— and
  // the card when the flash does not have that name, and a name that ends in `-SD` is the app of the card whose name is
  // the one without the suffix. It is the same rule the listing follows, so what a person lists is what a person can
  // open (0.88.20, docs/40, docs/70).
  if (rest[0] != '\0' && strcmp(best->path, "/apps") == 0) {
    hal::IStorage* flash = best->storage;
    hal::IStorage* card = storage_of("/sd");
    char rel[kMaxPathBytes];
    hal::Stat info;
    const bool in_flash = (flash != nullptr) && join_apps(rel, sizeof(rel), rest) && flash->stat(rel, info);
    if (!in_flash && card != nullptr) {
      bool in_card = join_apps(rel, sizeof(rel), rest) && card->stat(rel, info);
      if (!in_card) {
        // **The suffix is on the name and not on the end of the path** (0.88.20): `/apps/squares-SD/res/x` is the card's
        // `apps/squares/res/x`, and looking for `-SD` at the end of the whole path only stripped it for the folder itself
        // and not for anything inside it —the app listed and `run squares-SD` answering `has no readable manifest`—.
        const char* slash = strchr(rest, '/');
        const size_t name_len = (slash != nullptr) ? static_cast<size_t>(slash - rest) : strlen(rest);
        const size_t mark = sizeof(kCardSuffix) - 1;
        if (name_len > mark && strncmp(rest + name_len - mark, kCardSuffix, mark) == 0) {
          const size_t bare = name_len - mark;
          const size_t tail_len = strlen(rest + name_len);  // the "/..." after the name, empty at the folder
          char stripped[kMaxPathBytes];
          if (bare + tail_len < sizeof(stripped)) {
            memcpy(stripped, rest, bare);
            memcpy(stripped + bare, rest + name_len, tail_len + 1);
            in_card = join_apps(rel, sizeof(rel), stripped) && card->stat(rel, info);
          }
        }
      }
      if (in_card) {
        const size_t len = strlen(rel);
        if (len >= rel_cap) return false;
        memcpy(rel_out, rel, len + 1);
        storage = card;
        return true;
      }
    }
  }

  size_t used = 0;
  const char* rel = best->rel;
  while (*rel != '\0') {
    if (used + 1 >= rel_cap) return false;
    rel_out[used++] = *rel++;
  }
  if (used > 0 && *rest != '\0') {
    if (used + 1 >= rel_cap) return false;
    rel_out[used++] = '/';
  }
  while (*rest != '\0') {
    if (used + 1 >= rel_cap) return false;
    rel_out[used++] = *rest++;
  }
  rel_out[used] = '\0';

  storage = best->storage;
  return true;
}

// **The whole path of a path of the door, on the disk that answered** (0.89.0): `/apps/squares` is
// `/littlefs/apps/squares` and `/apps/squares-SD` is `/sd/apps/squares`. A door is not a place, and a message that has to
// say where something was —an app that was removed, a folder that was made— says the row of the table that **is a disk**:
// a mount with nothing in front of it inside its backend. The rows with a prefix (`/apps`, `/res`, `/data`, `/fonts`) are
// doors and are skipped, so the same storage cannot answer with the alias instead of the disk (docs/70).
bool on_disk(const char* path, char* out, size_t cap) {
  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  if (!resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  for (uint32_t i = 0; i < g_count; ++i) {
    const Mount& m = g_mounts[i];
    if (m.storage != storage) continue;
    if (m.rel != nullptr && m.rel[0] != '\0') continue;
    const int made = snprintf(out, cap, "%s/%s", m.path, rel);
    return made > 0 && static_cast<size_t>(made) < cap;
  }
  return false;
}

// **Every door of the filesystem is metered**, because a card and a flash cost very different things:
// a read of 4 KB from the flash and a read of 4 KB from the card are not the same number and only a
// meter per door can say which one a slow app is paying (docs/30, docs/70).
bool stat(const char* path, hal::Stat& out) {
  // The root is the mount table itself, not a backend: it is a directory as long
  // as there is something mounted.
  if (path != nullptr && path[0] == '/' && path[1] == '\0') {
    out.type = hal::NodeType::kDir;
    out.size = 0;
    out.mtime = 0;
    return g_count > 0;
  }
  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  if (!resolve(path, storage, rel, sizeof(rel))) return false;
  meter::Scope scope(meter::storage_id(meter::kOpStat, unit_of(storage)));
  return storage->stat(rel, out);
}

bool list(const char* path, hal::ListSink& sink) {
  // Asking for the root means asking which mounts exist. There is no backend to
  // pass it to: the names come from the table.
  if (path != nullptr && path[0] == '/' && path[1] == '\0') {
    for (uint32_t i = 0; i < g_count; ++i) {
      hal::ListEntry entry;
      entry.name = g_mounts[i].path + 1;  // the table stores "/name"
      entry.type = g_mounts[i].file ? hal::NodeType::kFile : hal::NodeType::kDir;
      entry.size = g_mounts[i].size;
      entry.mtime = 0;
      if (!sink.on_entry(entry)) break;
    }
    return g_count > 0;
  }

  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  // **The door of the apps is the two disks in one listing** (0.88.19): asking for it is not asking one backend, and
  // what a person sees there is the apps of the card and the apps of the flash without one repeated (docs/40, docs/70).
  if (path != nullptr && (strcmp(path, "/apps") == 0 || strcmp(path, "/apps/") == 0)) return list_the_apps(sink);
  if (!resolve(path, storage, rel, sizeof(rel))) {
    log_printf(pal::kLogWarn, "VFS", "cannot resolve %s", path);
    return false;
  }
  meter::Scope scope(meter::storage_id(meter::kOpList, unit_of(storage)));
  return storage->list(rel, sink);
}

bool is_writable(const char* path) {
  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  if (!resolve(path, storage, rel, sizeof(rel))) return false;
  for (uint32_t i = 0; i < g_count; ++i) {
    const size_t len = strlen(g_mounts[i].path);
    if (strncmp(path, g_mounts[i].path, len) != 0) continue;
    if (g_mounts[i].storage != storage) continue;
    return g_mounts[i].writable;
  }
  return false;
}

bool space(const char* path, uint64_t& total, uint64_t& free_bytes) {
  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  if (!resolve(path, storage, rel, sizeof(rel))) return false;
  meter::Scope scope(meter::storage_id(meter::kOpStat, unit_of(storage)));
  return storage->space(total, free_bytes);
}

bool read_file(const char* path, char* buf, size_t cap, size_t& out_len) {
  out_len = 0;
  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  if (!resolve(path, storage, rel, sizeof(rel))) return false;

  // Asking the backend to open a file that is not there makes the ESP32 core log
  // "does not exist, no permits for creation" even for a plain read (measured on
  // the board, 30/10, arduino-esp32 2.0.x vfs_api.cpp). A question is not an
  // error, so it is answered here first.
  hal::Stat info;
  if (!stat(path, info) || info.type != hal::NodeType::kFile) return false;

  // **The whole file, open, read and close included**: what an app pays to read a file is one number and
  // not three (docs/30).
  meter::Scope scope(meter::storage_id(meter::kOpRead, unit_of(storage)));
  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) return false;

  size_t total = 0;
  while (total < cap) {
    const int32_t n = storage->read(slot, buf + total, static_cast<uint32_t>(cap - total));
    if (n <= 0) break;
    total += static_cast<size_t>(n);
  }
  storage->close(slot);
  out_len = total;
  return true;
}

}  // namespace vfs
}  // namespace kernel
