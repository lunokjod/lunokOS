// The file system as the interpreter sees it (docs/30), with the rules of
// docs/70: an app writes only inside its own ground, `/data/<id>/`, and inside its own
// settings, `/cfg/` —which is `cfg/<id>/` on the unit the person chose—, and every write
// is either the whole file or nothing.
//
// The kernel knows the VFS and the interpreter does not, so the reading
// operations and the writing ones are handed over at boot.
#include "kernel/meter.h"
#include "kernel/fs_ops.h"

#include <stdio.h>
#include <string.h>

#include "pal/pal.h"

#include "kernel/cfg.h"
#include "kernel/log.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"

namespace kernel {
namespace {

const char kData[] = "/data";
const char kCfg[] = "/cfg";
const char kRes[] = "/res";
const char kApps[] = "/apps";
const char kSd[] = "/sd";
const char kRtc[] = "/rtc";
const char kFram[] = "/fram";

// The pool of open files. The number is the HAL's own pool size: a ninth file
// cannot open because there is no slot, and saying E_NO_HANDLES is better than
// whatever the backend would answer (docs/70).
const uint32_t kMaxOpenFiles = 8;

struct OpenFile {
  bool used;
  hal::IStorage* storage;
  hal::FileSlot slot;
  // Where the file is and how big it is. The backend does not say either without
  // being asked, and a seek needs both (docs/70).
  int64_t position;
  int64_t size;
};

OpenFile g_open[kMaxOpenFiles];
uint32_t g_open_count = 0;

// The id of the app that is running, or "" when this is not an app.
const char* ground_app() { return luabind::app_id(); }

// Is `path` `root` itself, or inside it?
bool under(const char* path, const char* root) {
  const size_t len = strlen(root);
  if (len == 0 || strncmp(path, root, len) != 0) return false;
  return path[len] == '\0' || path[len] == '/';
}

// A path the VFS understands: absolute, short enough to work with here, and with `/cfg`
// already turned into the app's own directory.
bool absolute(const char* in, char* out, size_t cap) {
  if (in == nullptr || in[0] == '\0') return false;
  char path[128];
  const int written = (in[0] == '/') ? snprintf(path, sizeof(path), "%s", in)
                                     : snprintf(path, sizeof(path), "/%s", in);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(path)) return false;

  // ".." is refused and not walked back: the VFS refuses it too, but here the answer can
  // say E_INVAL, which is what an app asking to climb out of its room should be told
  // (docs/70).
  if (strstr(path, "..") != nullptr) return false;

  // `/cfg` and `/res` are doors and not disks: the unit behind `/cfg` is the kernel's to
  // choose, and `/res` —an alias of the disk for a person— is the app's own `apps/<id>/res/`
  // once the interpreter asks. Each app sees only its own room behind either (docs/70).
  // `/cfg/x` from an app is `/cfg/<id>/x` and `/res/x` is `/apps/<id>/res/x` before the VFS
  // resolves; from the prompt they stay as written, because the shell is not an app.
  const char* app = ground_app();
  if (app[0] != '\0' && under(path, kCfg)) {
    const int placed = snprintf(out, cap, "%s/%s%s", kCfg, app, path + strlen(kCfg));
    return placed > 0 && static_cast<size_t>(placed) < cap;
  }
  if (app[0] != '\0' && under(path, kRes)) {
    const int placed = snprintf(out, cap, "%s/%s/res%s", kApps, app, path + strlen(kRes));
    return placed > 0 && static_cast<size_t>(placed) < cap;
  }
  snprintf(out, cap, "%s", path);
  return true;
}

// "/cfg/<id>" or "/cfg/<id>/..." — its own settings, on whichever unit answered.
// `absolute` has already put the id there, so this never sees a bare "/cfg".
bool inside_cfg(const char* path) {
  const char* app = ground_app();
  if (app[0] == '\0') return false;
  char prefix[64];
  const int written = snprintf(prefix, sizeof(prefix), "%s/%s", kCfg, app);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(prefix)) return false;
  const size_t len = static_cast<size_t>(written);
  if (strncmp(path, prefix, len) != 0) return false;
  return path[len] == '\0' || path[len] == '/';
}

// "/data/<id>" or "/data/<id>/..." — the only place an app may write.
bool inside_ground(const char* path) {
  const char* app = ground_app();
  if (app[0] == '\0') return false;
  char prefix[64];
  const int written = snprintf(prefix, sizeof(prefix), "%s/%s", kData, app);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(prefix)) return false;
  const size_t len = static_cast<size_t>(written);
  if (strncmp(path, prefix, len) != 0) return false;
  return path[len] == '\0' || path[len] == '/';
}

// The RTC memory and the FRAM are not drives of the system like `/littlefs` or `/nvs`: they are raw
// memory, with no layout and no keys, and **the app that writes decides the layout**. An app may
// read and write both, which is the whole reason those memories exist (docs/50, docs/70).
bool on_raw_memory(const char* path) { return under(path, kRtc) || under(path, kFram); }

// Where an app may go. The drives of the system are not its business: `/littlefs` is the
// disk a person writes to from the prompt and `/nvs` is the system's bookkeeping. An app
// has its places at the root and nowhere else (docs/70):
//
//   /data/<id>/   its own ground: read and write, and the disk is the limit
//   /cfg/         its own settings, on the unit the person chose: read and write
//   /rtc, /fram   raw memory: read and write, and the layout is the app's
//   /res/         its own static files (`apps/<id>/res/`): read, they are what it ships with
//   /apps/        the apps of any drive: read, a manifest is public
//   /sd/          the removable media: read, never write, a card is the person's
//
// Anything else —another app's ground, /littlefs/apps/..., /nvs/session— answers E_INVAL
// instead of being hidden: an app should know it asked for something that is not its.
bool allowed(const char* path, bool writing, luabind::FsError& err) {
  if (!writing) {
    if (inside_ground(path) || inside_cfg(path) || on_raw_memory(path) || under(path, kApps) ||
        under(path, kSd)) {
      return true;
    }
    err = luabind::FsError::kInval;
    return false;
  }
  if ((!inside_ground(path) && !inside_cfg(path) && !on_raw_memory(path)) ||
      !vfs::is_writable(path)) {
    err = luabind::FsError::kInval;
    return false;
  }
  return true;
}

bool to_storage(const char* full, hal::IStorage*& storage, char* rel, size_t rel_cap) {
  return vfs::resolve(full, storage, rel, rel_cap);
}

// Creates every directory on the way to `rel`. It is what lets an app write in
// its own ground the first time, without telling it to make three directories
// first; one that is already there is not an error (docs/70).
void make_dirs(hal::IStorage* storage, char* rel) {
  for (char* p = rel + 1; *p != '\0'; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    storage->mkdir(rel);
    *p = '/';
  }
}

int32_t read_at(const char* path, int64_t offset, char* buf, uint32_t len,
                luabind::FsError& err) {
  // A whole file read: open, read and close are one cost to the app and one row here (docs/70).
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return -1;
  }
  if (!allowed(full, false, err)) return -1;

  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!to_storage(full, storage, rel, sizeof(rel))) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  // **One row for the whole read of a file**, open and close included: what an app pays is the read
  // (docs/30, docs/70).
  meter::Scope scope(meter::storage_id(meter::kOpRead, vfs::unit_of(storage)));
  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  if (offset > 0 && !storage->seek(slot, static_cast<uint32_t>(offset))) {
    storage->close(slot);
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  const int32_t got = storage->read(slot, buf, len);
  storage->close(slot);
  if (got < 0) {
    err = luabind::FsError::kUnsupported;
    return -1;
  }
  return got;
}

int64_t size(const char* path, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return -1;
  }
  if (!allowed(full, false, err)) return -1;

  hal::Stat info;
  if (!vfs::stat(full, info) || info.type != hal::NodeType::kFile) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  return static_cast<int64_t>(info.size);
}

bool is_dir(const char* path, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return false;
  }
  if (!allowed(full, false, err)) return false;

  hal::Stat info;
  if (!vfs::stat(full, info)) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  return info.type == hal::NodeType::kDir;
}

// The VFS lists through a sink of its own; this one only passes the entries on.
class VisitorSink : public hal::ListSink {
 public:
  explicit VisitorSink(luabind::DirVisitor& visitor) : visitor_(visitor) {}

  bool on_entry(const hal::ListEntry& entry) override {
    visitor_.visit(entry.name, static_cast<int>(entry.type), entry.size, entry.mtime);
    return true;
  }

 private:
  luabind::DirVisitor& visitor_;
};

bool list(const char* path, luabind::DirVisitor& visitor, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return false;
  }
  if (!allowed(full, false, err)) return false;

  VisitorSink sink(visitor);
  if (!vfs::list(full, sink)) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  return true;
}

// Why a write failed, asked only when it failed: the room on the device is the
// expensive question (176 ms on this board, because it walks the whole file
// system), and a write that fits does not need to know it (docs/75).
luabind::FsError failure_code(const char* full, size_t len) {
  uint64_t total = 0;
  uint64_t free_bytes = 0;
  if (vfs::space(full, total, free_bytes) && free_bytes < len) {
    return luabind::FsError::kNoSpc;
  }
  return luabind::FsError::kUnsupported;
}

// The file is written next to where it will be and renamed into place, so a
// write that dies halfway leaves the old file untouched (docs/70).
int64_t write_atomic(const char* path, const char* data, size_t len, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return -1;
  }
  if (!allowed(full, true, err)) return -1;

  // There is no quota: **the limit is the disk**, and when it says no the answer is the
  // disk's (`E_NOSPC`, asked only after the write failed and it has to explain itself).
  // The time of a write is kept because on this flash it is 60-80 ms and guessing where
  // it goes is not measuring (docs/75).
  const int64_t started_ms = pal::system().millis();

  char temp[128];
  const int written = snprintf(temp, sizeof(temp), "%s.tmp", full);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(temp)) {
    err = luabind::FsError::kTooBig;
    return -1;
  }

  hal::IStorage* storage = nullptr;
  char temp_rel[128];
  if (!to_storage(temp, storage, temp_rel, sizeof(temp_rel))) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  make_dirs(storage, temp_rel);
  hal::FileSlot slot;
  if (!storage->open(temp_rel, hal::OpenMode::kWrite, slot)) {
    err = failure_code(full, len);
    return -1;
  }
  const int32_t put = storage->write(slot, data, static_cast<uint32_t>(len));
  storage->close(slot);
  if (put < 0 || static_cast<size_t>(put) != len) {
    storage->remove(temp_rel);
    err = failure_code(full, len);
    return -1;
  }

  hal::IStorage* target = nullptr;
  char target_rel[128];
  if (!to_storage(full, target, target_rel, sizeof(target_rel)) || target != storage) {
    storage->remove(temp_rel);
    err = luabind::FsError::kInval;
    return -1;
  }
  if (!storage->rename(temp_rel, target_rel)) {
    storage->remove(temp_rel);
    err = failure_code(full, len);
    return -1;
  }
  const int64_t done_ms = pal::system().millis();
  log_printf(pal::kLogDebug, "FS", "write %u B in %lld ms", static_cast<unsigned>(len),
             static_cast<long long>(done_ms - started_ms));
  return static_cast<int64_t>(len);
}

bool mkdir(const char* path, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return false;
  }
  if (!allowed(full, true, err)) return false;

  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!to_storage(full, storage, rel, sizeof(rel))) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  meter::Scope scope(meter::storage_id(meter::kOpWrite, vfs::unit_of(storage)));
  make_dirs(storage, rel);
  hal::Stat info;
  if (vfs::stat(full, info) && info.type == hal::NodeType::kDir) return true;  // already there
  if (!storage->mkdir(rel)) {
    err = luabind::FsError::kUnsupported;
    return false;
  }
  return true;
}

bool remove(const char* path, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return false;
  }
  if (!allowed(full, true, err)) return false;

  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!to_storage(full, storage, rel, sizeof(rel))) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  meter::Scope scope(meter::storage_id(meter::kOpWrite, vfs::unit_of(storage)));
  if (!storage->remove(rel)) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  return true;
}

bool rename_file(const char* from, const char* to, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  char from_full[128];
  char to_full[128];
  if (!absolute(from, from_full, sizeof(from_full)) ||
      !absolute(to, to_full, sizeof(to_full))) {
    err = luabind::FsError::kInval;
    return false;
  }
  if (!allowed(from_full, true, err)) return false;
  if (!allowed(to_full, true, err)) return false;

  hal::IStorage* from_store = nullptr;
  hal::IStorage* to_store = nullptr;
  char from_rel[128];
  char to_rel[128];
  if (!to_storage(from_full, from_store, from_rel, sizeof(from_rel)) ||
      !to_storage(to_full, to_store, to_rel, sizeof(to_rel))) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  if (from_store != to_store) {
    err = luabind::FsError::kInval;  // a rename does not cross mounts
    return false;
  }
  meter::Scope scope(meter::storage_id(meter::kOpWrite, vfs::unit_of(from_store)));
  if (!from_store->rename(from_rel, to_rel)) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  return true;
}

// --- an open file ---------------------------------------------------------

int32_t find_free_slot() {
  for (uint32_t i = 0; i < kMaxOpenFiles; ++i) {
    if (!g_open[i].used) return static_cast<int32_t>(i) + 1;  // 0 means no handle
  }
  return -1;
}

OpenFile* file_at(int32_t handle) {
  if (handle <= 0 || static_cast<uint32_t>(handle) > kMaxOpenFiles) return nullptr;
  OpenFile& file = g_open[static_cast<uint32_t>(handle) - 1];
  return file.used ? &file : nullptr;
}

bool mode_from_name(const char* mode, hal::OpenMode& out) {
  if (mode == nullptr) return false;
  if (strcmp(mode, "r") == 0) {
    out = hal::OpenMode::kRead;
    return true;
  }
  if (strcmp(mode, "w") == 0) {
    out = hal::OpenMode::kWrite;
    return true;
  }
  if (strcmp(mode, "a") == 0) {
    out = hal::OpenMode::kAppend;
    return true;
  }
  if (strcmp(mode, "r+") == 0) {
    out = hal::OpenMode::kReadWrite;
    return true;
  }
  return false;
}

int32_t open_file(const char* path, const char* mode, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  hal::OpenMode how;
  if (!mode_from_name(mode, how)) {
    err = luabind::FsError::kInval;
    return -1;
  }

  // Reading needs nothing more than a place an app may read; the other modes write, so
  // only its own ground is allowed (docs/70).
  char full[128];
  if (!absolute(path, full, sizeof(full))) {
    err = luabind::FsError::kInval;
    return -1;
  }
  const bool writing = (how != hal::OpenMode::kRead);
  if (!allowed(full, writing, err)) return -1;

  const int32_t handle = find_free_slot();
  if (handle < 0) {
    err = luabind::FsError::kNoHandles;
    return -1;
  }

  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!to_storage(full, storage, rel, sizeof(rel))) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  if (writing) make_dirs(storage, rel);

  // A file that is not there is answered before the backend is asked to open it:
  // the ESP32 core logs an error for every open of a missing file, even a read
  // (measured on the board, 30/10). It also keeps the two failures apart: not
  // there (E_NOENT) and no handle left (E_NO_HANDLES) are not the same (docs/70).
  hal::Stat info;
  const bool exists = vfs::stat(full, info);
  if (!exists && !writing) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }

  OpenFile& file = g_open[static_cast<uint32_t>(handle) - 1];
  {
    // The open itself, against the unit it opens on (docs/30).
    meter::Scope scope(meter::storage_id(writing ? meter::kOpWrite : meter::kOpRead,
                                          vfs::unit_of(storage)));
    if (!storage->open(rel, how, file.slot)) {
      err = writing ? failure_code(full, 0) : luabind::FsError::kNoHandles;
      return -1;
    }
  }
  file.storage = storage;
  file.used = true;
  // The backend does not fill in the size of an open file, so it is taken from
  // the stat done above; a file opened for writing starts empty (docs/70).
  if (how == hal::OpenMode::kWrite) {
    file.size = 0;
  } else {
    file.size = (exists && info.type == hal::NodeType::kFile) ? static_cast<int64_t>(info.size) : 0;
  }
  file.position = (how == hal::OpenMode::kAppend) ? file.size : 0;
  ++g_open_count;
  return handle;
}

int32_t file_read(int32_t handle, char* buf, uint32_t len, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  OpenFile* file = file_at(handle);
  if (file == nullptr) {
    err = luabind::FsError::kInval;
    return -1;
  }
  const int32_t got = [&]() {
    meter::Scope scope(meter::storage_id(meter::kOpRead, vfs::unit_of(file->storage)));
    return file->storage->read(file->slot, buf, len);
  }();
  if (got < 0) {
    err = luabind::FsError::kUnsupported;
    return -1;
  }
  file->position += got;
  return got;
}

int64_t file_write(int32_t handle, const char* data, size_t len, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  OpenFile* file = file_at(handle);
  if (file == nullptr) {
    err = luabind::FsError::kInval;
    return -1;
  }
  const int32_t put = [&]() {
    meter::Scope scope(meter::storage_id(meter::kOpWrite, vfs::unit_of(file->storage)));
    return file->storage->write(file->slot, data, static_cast<uint32_t>(len));
  }();
  if (put < 0 || static_cast<size_t>(put) != len) {
    err = luabind::FsError::kUnsupported;
    return -1;
  }
  file->position += static_cast<int64_t>(len);
  if (file->position > file->size) file->size = file->position;
  return static_cast<int64_t>(len);
}

int64_t file_position(int32_t handle, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  OpenFile* file = file_at(handle);
  if (file == nullptr) {
    err = luabind::FsError::kInval;
    return -1;
  }
  return file->position;
}

bool file_seek(int32_t handle, int64_t position, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  OpenFile* file = file_at(handle);
  if (file == nullptr || position < 0) {
    err = luabind::FsError::kInval;
    return false;
  }
  bool moved = false;
  {
    meter::Scope scope(meter::storage_id(meter::kOpRead, vfs::unit_of(file->storage)));
    moved = file->storage->seek(file->slot, static_cast<uint32_t>(position));
  }
  if (!moved) {
    err = luabind::FsError::kInval;
    return false;
  }
  file->position = position;
  return true;
}

int64_t file_size(int32_t handle, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  OpenFile* file = file_at(handle);
  if (file == nullptr) {
    err = luabind::FsError::kInval;
    return -1;
  }
  return file->size;
}

bool file_close(int32_t handle, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  OpenFile* file = file_at(handle);
  if (file == nullptr) {
    err = luabind::FsError::kInval;
    return false;
  }
  file->storage->close(file->slot);
  file->used = false;
  file->storage = nullptr;
  if (g_open_count > 0) --g_open_count;
  return true;
}

}  // namespace

// The mounts, filled straight from the VFS: /littlefs is mounted four times over
// the same partition, and saying so is the truth, not a duplicate (docs/70).
uint32_t mounts(luabind::MountInfo* out, uint32_t max) {
  const uint32_t count = vfs::mount_count();
  uint32_t used = 0;
  for (uint32_t i = 0; i < count && used < max; ++i) {
    const char* path = vfs::mount_path(i);
    const char* kind = vfs::mount_kind(i);
    const size_t path_len = strlen(path);
    const size_t kind_len = strlen(kind);
    if (path_len >= sizeof(out[used].path) || kind_len >= sizeof(out[used].kind)) continue;
    memcpy(out[used].path, path, path_len + 1);
    memcpy(out[used].kind, kind, kind_len + 1);
    out[used].writable = vfs::mount_writable(i);
    ++used;
  }
  return used;
}

// Room on a mount. It walks the file system, so nobody calls it behind an app's
// back: fs.space is the call that asks for it (docs/70).
bool space_at(const char* path, uint64_t& total, uint64_t& free_bytes, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (!vfs::space(path, total, free_bytes)) {
    err = luabind::FsError::kUnsupported;
    return false;
  }
  return true;
}

// Where the settings go, as the kernel decided it at boot and as the person may have
// changed it since (docs/70). An app does not need any of these three: it writes /cfg and
// the rewrite in `absolute` does the rest.
const char* cfg_unit() { return cfg::unit(); }

bool cfg_set_unit(const char* unit, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (unit == nullptr) {
    err = luabind::FsError::kInval;
    return false;
  }
  // Refused, not ignored: a unit that is not mounted has no room for settings, and a
  // silent success would leave the person trusting a card that is not there.
  if (!cfg::set_unit(unit)) {
    err = luabind::FsError::kUnavail;
    return false;
  }
  return true;
}

int32_t cfg_dir(char* out, size_t cap, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (!cfg::dir_for_app(out, cap, ground_app())) {
    err = luabind::FsError::kUnavail;
    return -1;
  }
  return static_cast<int32_t>(strlen(out));
}

bool app_path(const char* in, char* out, size_t cap) { return absolute(in, out, cap); }

void install_fs_ops() {
  luabind::FileOps ops;
  ops.read_at = &read_at;
  ops.size = &size;
  ops.is_dir = &is_dir;
  ops.list = &list;
  ops.write_atomic = &write_atomic;
  ops.mkdir = &mkdir;
  ops.remove = &remove;
  ops.rename = &rename_file;
  ops.open = &open_file;
  ops.file_read = &file_read;
  ops.file_write = &file_write;
  ops.file_position = &file_position;
  ops.file_seek = &file_seek;
  ops.file_size = &file_size;
  ops.file_close = &file_close;
  ops.mounts = &mounts;
  ops.space = &space_at;
  ops.cfg_unit = &cfg_unit;
  ops.cfg_set_unit = &cfg_set_unit;
  ops.cfg_dir = &cfg_dir;
  luabind::set_file_ops(ops);
}

}  // namespace kernel
