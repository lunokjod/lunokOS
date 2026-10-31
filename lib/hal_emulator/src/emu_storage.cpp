// POSIX backend for the emulator: /littlefs is the directory <fs_dir>/littlefs.
// Same interface and same fixed handle pool as the real board, so a file that
// works here works there.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "emu/options.h"
#include "emu_storage.h"
#include "hal/hal.h"

namespace hal {
namespace {

const size_t kMaxFiles = 8;
const size_t kPathCap = 256;

FILE* g_files[kMaxFiles] = {nullptr};

void fs_path(const char* rel, char* out, size_t cap) {
  size_t used = 0;
  const char* parts[3];
  parts[0] = emu::options().fs_dir;
  parts[1] = "/littlefs/";
  parts[2] = rel;
  for (int i = 0; i < 3; ++i) {
    for (const char* p = parts[i]; *p != '\0' && used + 1 < cap; ++p) out[used++] = *p;
  }
  out[used] = '\0';
}

const char* mode_name(OpenMode mode) {
  switch (mode) {
    case OpenMode::kWrite:
      return "wb";
    case OpenMode::kAppend:
      return "ab";
    case OpenMode::kReadWrite:
      return "r+b";
    case OpenMode::kRead:
      break;
  }
  return "rb";
}

class PosixStorage : public IStorage {
 public:
  bool stat(const char* rel, Stat& out) override {
    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    struct stat info;
    if (::stat(path, &info) != 0) return false;
    out.type = S_ISDIR(info.st_mode) ? NodeType::kDir : NodeType::kFile;
    out.size = S_ISDIR(info.st_mode) ? 0 : static_cast<uint32_t>(info.st_size);
    out.mtime = static_cast<int64_t>(info.st_mtime);
    return true;
  }

  bool list(const char* rel, ListSink& sink) override {
    char dir_path[kPathCap];
    fs_path(rel, dir_path, sizeof(dir_path));
    DIR* dir = opendir(dir_path);
    if (dir == nullptr) return false;

    bool keep = true;
    for (struct dirent* item = readdir(dir); item != nullptr && keep; item = readdir(dir)) {
      if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;

      char child[kPathCap];
      size_t at = 0;
      for (const char* p = dir_path; *p != '\0' && at + 1 < sizeof(child); ++p) child[at++] = *p;
      if (at + 1 < sizeof(child)) child[at++] = '/';
      for (const char* p = item->d_name; *p != '\0' && at + 1 < sizeof(child); ++p) child[at++] = *p;
      child[at] = '\0';

      ListEntry entry;
      entry.name = item->d_name;
      struct stat info;
      if (::stat(child, &info) == 0) {
        entry.type = S_ISDIR(info.st_mode) ? NodeType::kDir : NodeType::kFile;
        entry.size = S_ISDIR(info.st_mode) ? 0 : static_cast<uint32_t>(info.st_size);
        entry.mtime = static_cast<int64_t>(info.st_mtime);
      } else {
        entry.type = NodeType::kFile;
        entry.size = 0;
        entry.mtime = 0;
      }
      keep = sink.on_entry(entry);
    }
    closedir(dir);
    return true;
  }

  bool open(const char* rel, OpenMode mode, FileSlot& slot) override {
    int32_t free_index = -1;
    for (size_t i = 0; i < kMaxFiles; ++i) {
      if (g_files[i] == nullptr) {
        free_index = static_cast<int32_t>(i);
        break;
      }
    }
    if (free_index < 0) return false;

    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    g_files[free_index] = fopen(path, mode_name(mode));
    if (g_files[free_index] == nullptr) return false;

    slot.impl = g_files[free_index];
    slot.size = 0;
    return true;
  }

  int32_t read(FileSlot& slot, void* buf, uint32_t len) override {
    FILE* file = static_cast<FILE*>(slot.impl);
    if (file == nullptr) return -1;
    const size_t got = fread(buf, 1, len, file);
    return static_cast<int32_t>(got);
  }

  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override {
    FILE* file = static_cast<FILE*>(slot.impl);
    if (file == nullptr) return -1;
    const size_t put = fwrite(buf, 1, len, file);
    return static_cast<int32_t>(put);
  }

  bool seek(FileSlot& slot, uint32_t pos) override {
    FILE* file = static_cast<FILE*>(slot.impl);
    if (file == nullptr) return false;
    return fseek(file, static_cast<long>(pos), SEEK_SET) == 0;
  }

  bool close(FileSlot& slot) override {
    FILE* file = static_cast<FILE*>(slot.impl);
    if (file == nullptr) return false;
    fclose(file);
    for (size_t i = 0; i < kMaxFiles; ++i) {
      if (g_files[i] == file) g_files[i] = nullptr;
    }
    slot.impl = nullptr;
    slot.size = 0;
    return true;
  }

  bool remove(const char* rel) override {
    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    return ::remove(path) == 0;
  }

  bool mkdir(const char* rel) override {
    char path[kPathCap];
    fs_path(rel, path, sizeof(path));
    return ::mkdir(path, 0777) == 0;
  }

  bool rename(const char* from, const char* to) override {
    char from_path[kPathCap];
    char to_path[kPathCap];
    fs_path(from, from_path, sizeof(from_path));
    fs_path(to, to_path, sizeof(to_path));
    return ::rename(from_path, to_path) == 0;
  }

  bool space(uint64_t& total, uint64_t& free) override {
    (void)total;
    (void)free;
    return false;  // the host disk is not ours to report on
  }
};

PosixStorage g_littlefs;

}  // namespace

IStorage* emu_storage(const char* mount) {
  if (mount == nullptr || strcmp(mount, "/littlefs") != 0) return nullptr;
  return &g_littlefs;
}

}  // namespace hal
