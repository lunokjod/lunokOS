// POSIX backend for the emulator: the flash is the directory named as a volume (the
// repository's `data` by default) or <fs_dir>/littlefs, and a card is the directory in the slot.
// Same interface and same fixed handle pool as the real board, so a file that
// works here works there.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include "emu/disk.h"
#include "emu/memories.h"
#include "emu/model.h"
#include "emu/options.h"
#include "emu_storage.h"
#include "hal/hal.h"
#include "hal/memory_file.h"

namespace hal {
namespace {

const size_t kMaxFiles = 8;
const size_t kPathCap = 256;

// A card in the slot of the bench is a **16 GB card**, because that is what a card is for the board
// and what the number has to say. The volume behind it is a directory of the PC's disk, so the space
// that is really left is the host's: when the host has less than the card, the difference is shown as
// **used on the card**, which is the only way the number stays coherent with the board (docs/85).
const uint64_t kCardBytes = 16ULL * 1024 * 1024 * 1024;

// How many bytes the files under a directory take, walking it: it is what "used" means for a partition
// that is a directory on the PC (docs/85).
uint64_t tree_bytes(const char* path) {
  DIR* dir = opendir(path);
  if (dir == nullptr) return 0;
  uint64_t total = 0;
  for (struct dirent* item = readdir(dir); item != nullptr; item = readdir(dir)) {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
    char child[512];
    snprintf(child, sizeof(child), "%s/%s", path, item->d_name);
    struct stat info;
    if (::stat(child, &info) != 0) continue;
    if (S_ISDIR(info.st_mode)) {
      total += tree_bytes(child);
    } else {
      total += static_cast<uint64_t>(info.st_size);
    }
  }
  closedir(dir);
  return total;
}

FILE* g_files[kMaxFiles] = {nullptr};

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
  // Two volumes of the same kind: the flash is the directory named as a volume (or
  // <fs_dir>/littlefs), and a card is the
  // directory the person named with `--sd`. Nothing else changes between them, which is
  // the point of the bench: a file that works here works there (docs/85).
  explicit PosixStorage(bool card) : card_(card) {}

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
    // The bytes are moved and the **cost of moving them** is paid by the volume that wraps this one:
    // a flash block, a sector of the card or a byte of the FRAM (docs/85, `emu/disk.h`).
    return static_cast<int32_t>(fread(buf, 1, len, file));
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
    char path[kPathCap];
    fs_path("", path, sizeof(path));
    // The space that is really left under the volume, and no more: on the PC it is the host's disk.
    struct statvfs info;
    uint64_t host_free = 0;
    if (::statvfs(path, &info) != 0) return false;
    host_free = static_cast<uint64_t>(info.f_bavail) * info.f_frsize;

    if (card_) {
      // A card in the slot of the bench is a **16 GB card**, and when the host has less than that, the
      // difference is shown as used on the card (docs/85).
      total = kCardBytes;
      free = (host_free < total) ? host_free : total;
      return true;
    }

    // The flash is a directory of the PC, but the board's is a partition of a size its own table says:
    // what the files take is used, and the PC running out of room counts as used too, which is the only
    // way an app that fills the disk sees it fill (docs/70, docs/85).
    const uint64_t partition = emu::model().disk_bytes;
    const uint64_t used = tree_bytes(path);
    total = partition;
    const uint64_t left = (used < partition) ? (partition - used) : 0;
    free = (host_free < left) ? host_free : left;
    return true;
  }

 private:
  void fs_path(const char* rel, char* out, size_t cap) const {
    const emu::Options& o = emu::options();
    // A card is the directory that was named. The flash is the directory named as a volume when
    // there is one —the repository's `data`, by default— and `<fs_dir>/littlefs` when there is
    // not, which is the copy a test makes for itself (docs/85).
    const char* base = card_ ? o.sd_dir : ((o.flash_dir != nullptr) ? o.flash_dir : o.fs_dir);
    const char* sub = (card_ || o.flash_dir != nullptr) ? "" : "/littlefs";
    snprintf(out, cap, "%s%s/%s", (base != nullptr) ? base : "", sub,
             (rel != nullptr) ? rel : "");
  }

  bool card_;  // false: the flash of the chip; true: the card in the slot
};

// **The time of the device, wrapped around the volume** (docs/85). A directory of the PC does not care
// what a read costs and the board does: every volume of this bench goes through here, and the numbers
// are the table in `emu/disk.h` —a block of the flash, a sector of the card, a byte of the FRAM—. The
// RTC memory is **not** wrapped: it is the RAM of the chip and a byte of it is a byte of RAM.
class TimedStorage : public IStorage {
 public:
  TimedStorage(IStorage* inner, emu::Disk disk) : inner_(inner), disk_(disk) {}

  bool stat(const char* rel, Stat& out) override {
    emu::disk_pay(disk_, 0, false);
    return inner_->stat(rel, out);
  }
  bool list(const char* rel, ListSink& sink) override {
    // A directory read is one transaction and, at least, the block its metadata sits in; a listing that
    // walks a whole directory pays that block once, which is what a cached directory costs (docs/85).
    emu::disk_pay(disk_, emu::disk_timing(disk_).block_bytes, false);
    return inner_->list(rel, sink);
  }
  bool open(const char* rel, OpenMode mode, FileSlot& slot) override {
    // The directory entry, which on both filesystems is a block nobody asked for by name.
    emu::disk_pay(disk_, emu::disk_timing(disk_).block_bytes, false);
    return inner_->open(rel, mode, slot);
  }
  int32_t read(FileSlot& slot, void* buf, uint32_t len) override {
    emu::disk_pay(disk_, len, false);
    return inner_->read(slot, buf, len);
  }
  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override {
    emu::disk_pay(disk_, len, true);
    return inner_->write(slot, buf, len);
  }
  bool seek(FileSlot& slot, uint32_t pos) override { return inner_->seek(slot, pos); }
  bool close(FileSlot& slot) override { return inner_->close(slot); }
  bool remove(const char* rel) override {
    emu::disk_pay(disk_, emu::disk_timing(disk_).block_bytes, true);
    return inner_->remove(rel);
  }
  bool mkdir(const char* rel) override {
    emu::disk_pay(disk_, emu::disk_timing(disk_).block_bytes, true);
    return inner_->mkdir(rel);
  }
  bool rename(const char* from, const char* to) override {
    emu::disk_pay(disk_, emu::disk_timing(disk_).block_bytes, true);
    return inner_->rename(from, to);
  }
  bool space(uint64_t& total, uint64_t& free) override {
    emu::disk_pay(disk_, 0, false);
    return inner_->space(total, free);
  }

 private:
  IStorage* inner_;
  emu::Disk disk_;
};

PosixStorage g_littlefs(false);
PosixStorage g_card(true);
// The two volumes of a filesystem, wrapped in the time of their device (docs/85).
TimedStorage g_littlefs_timed(&g_littlefs, emu::Disk::kFlash);
TimedStorage g_card_timed(&g_card, emu::Disk::kCard);

// The RTC memory of the bench is the one `emu/rtc.bin` holds, and `/rtc` is that same memory as one
// file of its size: the board and the bench show the same thing (docs/50, docs/85). Built on first
// use and not as a static, because the file it stands for is opened with the options of the run.
IStorage* rtc_storage() {
  static MemoryFile file(emu::rtc_ram(), emu::kRtcRamBytes);
  return &file;
}

// The FRAM of the model, which is `emu/fram.bin`: 256 bytes that are non-volatile, so `/fram` is a
// file the bench keeps between runs the same way the chip keeps it between two cuts of the power
// (docs/50, docs/85). A model with no FM24C02 has no `fram()` to give and therefore no mount.
IStorage* fram_storage() {
  uint8_t* bytes = emu::fram();
  if (bytes == nullptr) return nullptr;
  static MemoryFile file(bytes, emu::kFramBytes);
  // **The chip is I2C, and a byte of it is ninety microseconds** (docs/50, docs/85): /fram reads and
  // writes through the bus, and the volume is the chip and not a RAM nobody pays for.
  static TimedStorage timed(&file, emu::Disk::kFram);
  return &timed;
}

}  // namespace

IStorage* emu_storage(const char* mount) {
  if (mount == nullptr) return nullptr;
  if (strcmp(mount, "/littlefs") == 0) return &g_littlefs_timed;
  // The RTC memory is a file of the chip and it is there on every board (docs/50).
  if (strcmp(mount, "/rtc") == 0) return rtc_storage();
  // The FRAM is only on the model that carries the chip: the board decides and not the bench
  // (docs/50, docs/85).
  if (strcmp(mount, "/fram") == 0) return fram_storage();
  // A card is there only when the person put one, and a card is a **directory that exists**: the
  // slot is empty without `--sd`, and a directory that was never made is not a card either, which is
  // what a board with a bent contact looks like (docs/70, docs/85).
  if (strcmp(mount, "/sd") == 0) {
    // A board with no slot has no card, whatever directory the run was given: the model decides and
    // not the PC. An ATOM with a `/sd` is a board nobody has (docs/50, docs/85).
    if (!emu::model().slot) return nullptr;
    const char* card = emu::options().sd_dir;
    if (card != nullptr && access(card, F_OK) == 0) return &g_card_timed;
  }
  return nullptr;
}

}  // namespace hal
