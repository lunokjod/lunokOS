// LittleFS backend for the M5 ATOM.
//
// The partition is the "littlefs" one from partitions.csv and it is found by that
// label, which is why begin() gets the name explicitly. Handles come from a fixed
// pool: opening a file never allocates (docs/75).
#include <LittleFS.h>
#include <string.h>
#include <sys/stat.h>

#include "atom_storage.h"
#include "hal/hal.h"

namespace hal {
namespace {

const size_t kMaxFiles = 8;
const size_t kPathCap = 160;

// Where LittleFS is mounted in the system VFS. LittleFS.open() reads paths
// relative to it; POSIX calls (stat, mkdir, unlink, rename) need it in front.
const char kMountPoint[] = "/littlefs";

File g_files[kMaxFiles];
bool g_used[kMaxFiles] = {false};

void fs_path(const char* rel, char* out, size_t cap) {
  size_t used = 0;
  out[used++] = '/';
  for (const char* p = rel; *p != '\0' && used + 1 < cap; ++p) out[used++] = *p;
  out[used] = '\0';
}

const char* mode_name(OpenMode mode) {
  switch (mode) {
    case OpenMode::kWrite:
      return "w";
    case OpenMode::kAppend:
      return "a";
    case OpenMode::kReadWrite:
      return "r+";
    case OpenMode::kRead:
      break;
  }
  return "r";
}

class LittleFsStorage : public IStorage {
 public:
  bool stat(const char* rel, Stat& out) override {
    // POSIX stat and LittleFS.open() do not take the same path: the Arduino
    // object reads paths relative to its mount point (/apps/x), while the system
    // VFS wants the whole thing (/littlefs/apps/x). Getting that wrong made every
    // entry file "not found" while the directories still listed (docs/70).
    char path[kPathCap];
    const int written = snprintf(path, sizeof(path), "%s/%s", kMountPoint, rel);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(path)) return false;
    // POSIX stat, and not LittleFS.exists(): exists() opens the file for reading
    // and the IDF logs "does not exist, no permits for creation" for a path that
    // is simply not there yet, which is the normal question here (docs/70).
    struct stat info;
    if (::stat(path, &info) != 0) return false;
    const bool dir = S_ISDIR(info.st_mode);
    out.type = dir ? NodeType::kDir : NodeType::kFile;
    out.size = dir ? 0 : static_cast<uint32_t>(info.st_size);
    out.mtime = 0;
    return true;
  }

  bool list(const char* rel, ListSink& sink) override {
    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) return false;

    bool keep = true;
    while (keep) {
      File child = dir.openNextFile();
      if (!child) break;
      ListEntry entry;
      const char* full = child.name();
      const char* slash = strrchr(full, '/');
      entry.name = (slash != nullptr) ? slash + 1 : full;
      entry.type = child.isDirectory() ? NodeType::kDir : NodeType::kFile;
      entry.size = child.isDirectory() ? 0 : static_cast<uint32_t>(child.size());
      entry.mtime = 0;
      keep = sink.on_entry(entry);
      child.close();
    }
    dir.close();
    return true;
  }

  bool open(const char* rel, OpenMode mode, FileSlot& slot) override {
    int32_t free_index = -1;
    for (size_t i = 0; i < kMaxFiles; ++i) {
      if (!g_used[i]) {
        free_index = static_cast<int32_t>(i);
        break;
      }
    }
    if (free_index < 0) return false;

    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    g_files[free_index] = LittleFS.open(path, mode_name(mode));
    if (!g_files[free_index]) return false;

    g_used[free_index] = true;
    slot.impl = &g_files[free_index];
    slot.size = static_cast<uint32_t>(g_files[free_index].size());
    return true;
  }

  int32_t read(FileSlot& slot, void* buf, uint32_t len) override {
    File* file = file_of(slot);
    if (file == nullptr) return -1;
    return static_cast<int32_t>(file->read(static_cast<uint8_t*>(buf), len));
  }

  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override {
    File* file = file_of(slot);
    if (file == nullptr) return -1;
    return static_cast<int32_t>(file->write(static_cast<const uint8_t*>(buf), len));
  }

  bool seek(FileSlot& slot, uint32_t pos) override {
    File* file = file_of(slot);
    if (file == nullptr) return false;
    return file->seek(pos, SeekSet);
  }

  bool close(FileSlot& slot) override {
    File* file = file_of(slot);
    if (file == nullptr) return false;
    file->close();
    for (size_t i = 0; i < kMaxFiles; ++i) {
      if (&g_files[i] == file) g_used[i] = false;
    }
    slot.impl = nullptr;
    slot.size = 0;
    return true;
  }

  bool remove(const char* rel) override {
    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    return LittleFS.remove(path);
  }

  bool mkdir(const char* rel) override {
    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    return LittleFS.mkdir(path);
  }

  bool rename(const char* from, const char* to) override {
    char from_path[kPathCap];
    char to_path[kPathCap];
    fs_path(from, from_path, sizeof(from_path));
    fs_path(to, to_path, sizeof(to_path));
    return LittleFS.rename(from_path, to_path);
  }

  bool space(uint64_t& total, uint64_t& free) override {
    total = LittleFS.totalBytes();
    const uint64_t used = LittleFS.usedBytes();
    free = (total > used) ? (total - used) : 0;
    return true;
  }

 private:
  static File* file_of(FileSlot& slot) { return static_cast<File*>(slot.impl); }
};

LittleFsStorage g_littlefs;

}  // namespace

IStorage* atom_storage(const char* mount) {
  if (mount == nullptr || strcmp(mount, "/littlefs") != 0) return nullptr;

  static bool tried = false;
  static bool mounted = false;
  if (!tried) {
    tried = true;
    // The partition is found by its label, so the label has to match the one in
    // partitions.csv. Never rely on the core's default label.
    mounted = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    if (!mounted) {
      // A partition that was never written has no filesystem on it. Formatting it
      // is the only way forward, and the caller logs that nothing was found.
      mounted = LittleFS.begin(true, "/littlefs", 10, "littlefs");
    }
  }
  return mounted ? &g_littlefs : nullptr;
}

}  // namespace hal
