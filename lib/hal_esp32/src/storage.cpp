// LittleFS backend for the ESP32.
//
// The partition is found by the label the board's table gives, which is why begin()
// gets the name explicitly. Handles come from a fixed pool: opening a file never
// allocates (docs/75).
#include <LittleFS.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp32/storage.h"
#include "hal/hal.h"
#include "hal/log.h"

namespace hal {
namespace {

const size_t kMaxFiles = 8;
const size_t kPathCap = 160;

// Where LittleFS is mounted in the system VFS. LittleFS.open() reads paths relative
// to it; POSIX calls (stat, mkdir, unlink, rename) need it in front.
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

class Esp32Fs : public IStorage {
 public:
  bool stat(const char* rel, Stat& out) override {
    // POSIX stat and LittleFS.open() do not take the same path: the Arduino object
    // reads paths relative to its mount point (/apps/x), while the system VFS wants
    // the whole thing (/littlefs/apps/x). Getting that wrong made every entry file
    // "not found" while the directories still listed (docs/70).
    char path[kPathCap];
    const int written = snprintf(path, sizeof(path), "%s/%s", kMountPoint, rel);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(path)) return false;
    // POSIX stat, and not LittleFS.exists(): exists() opens the file for reading and
    // the IDF logs "does not exist, no permits for creation" for a path that is
    // simply not there yet, which is the normal question here (docs/70).
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
    uint32_t in_use = 0;
    for (size_t i = 0; i < kMaxFiles; ++i) {
      if (g_used[i]) {
        ++in_use;
      } else if (free_index < 0) {
        free_index = static_cast<int32_t>(i);
      }
    }
    if (free_index < 0) {
      // **Eight files is the pool and the ninth is refused here** (docs/70): said
      // with the path, because an app that cannot open a file has no other way of
      // knowing that the reason was this pool and not the disk.
      logf(pal::kLogWarn, "FS", "no free handle for %s (%u of %u open)", rel,
           static_cast<unsigned>(in_use), static_cast<unsigned>(kMaxFiles));
      return false;
    }

    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    errno = 0;
    g_files[free_index] = LittleFS.open(path, mode_name(mode));
    if (!g_files[free_index]) {
      // **The core says `fopen(...) failed` and nothing else** (0.151.18), and the
      // three reasons behind it send a person to three different places: the file
      // is not there, the file system has no room, or the chip could not give the
      // `FILE`. **The heap here is the board's own RAM and not the sum with the
      // PSRAM** (0.151.19): the number that explained a whole afternoon of
      // `ENOMEM` was the first one, and `esp_get_free_heap_size()` counted the
      // extra memory in and said a megabyte and a half (docs/75).
      logf(pal::kLogWarn, "FS",
           "cannot open %s for %s: errno %d, %u of %u open, %u B of board RAM, %u B of psram", path,
           mode_name(mode), errno, static_cast<unsigned>(in_use), static_cast<unsigned>(kMaxFiles),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
      return false;
    }

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

Esp32Fs g_littlefs;

}  // namespace

IStorage* esp32_storage(const char* mount, const char* partition) {
  if (mount == nullptr || strcmp(mount, kMountPoint) != 0) return nullptr;
  if (partition == nullptr || partition[0] == '\0') return nullptr;

  static bool tried = false;
  static bool mounted = false;
  if (!tried) {
    tried = true;
    // The partition is found by its label, so the label has to match the one in the
    // board's table. Never rely on the core's default label.
    //
    // The disk is built by tools/make_disk.py and written by the uploader: this code
    // must never format it. The core's format-on-fail path writes a superblock with
    // name_max 255, which the same core then refuses to mount (it wants 32), so it
    // turns "no disk" into "a disk that never mounts" and throws away whatever the
    // uploader had left there. A board without a disk says so and waits for the image.
    mounted = LittleFS.begin(false, kMountPoint, 10, partition);
  }
  return mounted ? &g_littlefs : nullptr;
}

}  // namespace hal
