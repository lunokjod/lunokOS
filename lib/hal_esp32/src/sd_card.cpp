// A card in the board's slot: FATFS mounted through the IDF's VFS, and POSIX calls on
// top because that is what a mounted filesystem gives for free.
//
// There is no Arduino SD class here on purpose: it brings `<Arduino.h>` and its own bus,
// and this slot shares the bus with the panel (docs/50).
#include "hal/log.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <sdmmc_cmd.h>

#include "esp32/sd_card.h"

namespace hal {
namespace {

const size_t kPathCap = 160;
// FATFS refuses the sixth open. It is a bound and not a pool: the slots live in the
// layer, so the caller does not have to keep an array (docs/75).
const uint8_t kMaxOpenFiles = 5;

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

// **A folder and everything under it, gone**, counting what went: the same walk the emulator's volume does, with POSIX
// because the card is a FATFS mounted in the VFS (docs/70).
bool wipe(const char* path, uint32_t& removed) {
  DIR* dir = opendir(path);
  if (dir == nullptr) return false;

  bool ok = true;
  for (struct dirent* item = readdir(dir); item != nullptr && ok; item = readdir(dir)) {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;

    char child[kPathCap];
    const int written = snprintf(child, sizeof(child), "%s%s", path, item->d_name);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(child)) {
      ok = false;
      break;
    }
    struct stat info;
    if (::stat(child, &info) != 0) continue;
    if (S_ISDIR(info.st_mode)) {
      // Room for the slash: a child of the longest path and its separator, so the compiler can see that the copy fits
      // (the board builds with `-Werror=format-truncation`).
      char deeper[kPathCap + 2];
      snprintf(deeper, sizeof(deeper), "%s/", child);
      if (!wipe(deeper, removed)) {
        ok = false;
      } else if (::rmdir(child) == 0) {
        ++removed;
      } else {
        ok = false;
      }
    } else if (::remove(child) == 0) {
      ++removed;
    } else {
      ok = false;
    }
  }
  closedir(dir);
  return ok;
}

class SdCard : public IStorage {
 public:
  explicit SdCard(const char* mount) {
    snprintf(mount_, sizeof(mount_), "%s", mount);
    size_t len = strlen(mount_);
    while (len > 1 && mount_[len - 1] == '/') mount_[--len] = '\0';
  }

  bool stat(const char* rel, Stat& out) override {
    char path[kPathCap];
    if (!full_path(rel, path, sizeof(path))) return false;
    struct stat info;
    if (::stat(path, &info) != 0) return false;
    fill(info, out);
    return true;
  }

  bool list(const char* rel, ListSink& sink) override {
    char path[kPathCap];
    if (!full_path(rel, path, sizeof(path))) return false;
    DIR* dir = ::opendir(path);
    if (dir == nullptr) return false;

    bool keep = true;
    struct dirent* item = nullptr;
    while (keep && (item = ::readdir(dir)) != nullptr) {
      if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
      char child[kPathCap];
      const int written = snprintf(child, sizeof(child), "%s/%s", path, item->d_name);
      if (written <= 0 || static_cast<size_t>(written) >= sizeof(child)) continue;
      struct stat info;
      if (::stat(child, &info) != 0) continue;  // gone between the read and the ask
      ListEntry entry;
      entry.name = item->d_name;
      entry.type = S_ISDIR(info.st_mode) ? NodeType::kDir : NodeType::kFile;
      entry.size = S_ISDIR(info.st_mode) ? 0 : static_cast<uint32_t>(info.st_size);
      entry.mtime = static_cast<int64_t>(info.st_mtime);
      keep = sink.on_entry(entry);
    }
    ::closedir(dir);
    return true;
  }

  bool open(const char* rel, OpenMode mode, FileSlot& slot) override {
    char path[kPathCap];
    if (!full_path(rel, path, sizeof(path))) return false;
    FILE* file = ::fopen(path, mode_name(mode));
    if (file == nullptr) return false;
    struct stat info;
    slot.impl = file;
    slot.size = (::stat(path, &info) == 0) ? static_cast<uint32_t>(info.st_size) : 0;
    return true;
  }

  int32_t read(FileSlot& slot, void* buf, uint32_t len) override {
    FILE* file = file_of(slot);
    if (file == nullptr) return -1;
    const size_t got = ::fread(buf, 1, len, file);
    return static_cast<int32_t>(got);
  }

  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override {
    FILE* file = file_of(slot);
    if (file == nullptr) return -1;
    const size_t put = ::fwrite(buf, 1, len, file);
    // A card that says it wrote fewer bytes is a card that is full or going: the count
    // is the answer and the caller decides (docs/70).
    return static_cast<int32_t>(put);
  }

  bool seek(FileSlot& slot, uint32_t pos) override {
    FILE* file = file_of(slot);
    if (file == nullptr) return false;
    return ::fseek(file, static_cast<long>(pos), SEEK_SET) == 0;
  }

  bool close(FileSlot& slot) override {
    FILE* file = file_of(slot);
    if (file == nullptr) return false;
    const bool ok = ::fclose(file) == 0;
    slot.impl = nullptr;
    slot.size = 0;
    return ok;
  }

  bool remove(const char* rel) override {
    char path[kPathCap];
    if (!full_path(rel, path, sizeof(path))) return false;
    return ::unlink(path) == 0;
  }

  bool mkdir(const char* rel) override {
    char path[kPathCap];
    if (!full_path(rel, path, sizeof(path))) return false;
    return ::mkdir(path, 0777) == 0;
  }

  bool rename(const char* from, const char* to) override {
    char from_path[kPathCap];
    char to_path[kPathCap];
    if (!full_path(from, from_path, sizeof(from_path))) return false;
    if (!full_path(to, to_path, sizeof(to_path))) return false;
    return ::rename(from_path, to_path) == 0;
  }

  // The numbers of the card itself, which is what a person with a full card asks for.
  //
  // This IDF has no `statvfs` in its VFS and no `esp_vfs_fat_info` (that one arrives in
  // IDF 5), so the volume is asked through FATFS and by its drive letter: the first FAT
  // volume the VFS registers is '0', and this project mounts exactly one —the card—
  // (docs/70). The day there is a second, the letter has to come from the mount and not
  // from a constant.
  // **Emptying the card**: the volume stays mounted and keeps its FAT, and what goes is every file and folder of its
  // root. It is what a page means by formatting a card with the calls this HAL has —a real `f_mkfs` needs the volume
  // unmounted and put back, and there is no board here to try that on— and the contract of `format` says the same
  // (docs/50, docs/70).
  bool format(uint32_t& removed) override {
    char path[kPathCap];
    snprintf(path, sizeof(path), "%s/", mount_);
    removed = 0;
    return wipe(path, removed);
  }

  bool space(uint64_t& total, uint64_t& free_bytes) override {
    const char drive[3] = {'0', ':', '\0'};
    FATFS* fs = nullptr;
    DWORD free_clusters = 0;
    if (f_getfree(drive, &free_clusters, &fs) != FR_OK || fs == nullptr) return false;
#if FF_MAX_SS != FF_MIN_SS
    const uint64_t per_cluster = static_cast<uint64_t>(fs->csize) * fs->ssize;
#else
    const uint64_t per_cluster = static_cast<uint64_t>(fs->csize) * FF_MIN_SS;
#endif
    const uint64_t clusters = static_cast<uint64_t>(fs->n_fatent) - 2;
    total = clusters * per_cluster;
    free_bytes = static_cast<uint64_t>(free_clusters) * per_cluster;
    return true;
  }

 private:
  static void fill(const struct stat& info, Stat& out) {
    const bool dir = S_ISDIR(info.st_mode);
    out.type = dir ? NodeType::kDir : NodeType::kFile;
    out.size = dir ? 0 : static_cast<uint32_t>(info.st_size);
    // A card carries the time of the write, unlike LittleFS (docs/70).
    out.mtime = static_cast<int64_t>(info.st_mtime);
  }

  bool full_path(const char* rel, char* out, size_t cap) const {
    const int written = snprintf(out, cap, "%s/%s", mount_, (rel != nullptr) ? rel : "");
    return written > 0 && static_cast<size_t>(written) < cap;
  }

  static FILE* file_of(FileSlot& slot) { return static_cast<FILE*>(slot.impl); }

  char mount_[16];
};

// One bus for the slot and, on the Paper, for the panel too: whoever comes first takes
// it and the other adds a device. A bus that is already up is not an error (docs/50).
bool take_bus(const SdPins& pins) {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = pins.mosi;
  bus.miso_io_num = pins.miso;
  bus.sclk_io_num = pins.sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4000;
  const esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(pins.host), &bus,
                                           SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    hal::logf(pal::kLogWarn, "SD", "the SPI bus did not come up (%s)", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool mount_card(const char* mount, const SdPins& pins) {
  if (!take_bus(pins)) return false;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = pins.host;
  // The clock the board's wiring takes, and never more: a card that mounts and then times
  // out on the first long read is a card driven too fast (docs/90).
  host.max_freq_khz = (pins.max_khz > 0) ? pins.max_khz : SDMMC_FREQ_DEFAULT;

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = static_cast<gpio_num_t>(pins.cs);
  slot.host_id = static_cast<spi_host_device_t>(pins.host);

  esp_vfs_fat_sdmmc_mount_config_t config = {};
  config.format_if_mount_failed = false;  // a card is somebody's data, never formatted here
  config.max_files = kMaxOpenFiles;
  config.allocation_unit_size = 16 * 1024;

  sdmmc_card_t* card = nullptr;
  const esp_err_t err = esp_vfs_fat_sdspi_mount(mount, &host, &slot, &config, &card);
  if (err != ESP_OK || card == nullptr) {
    // A slot with no card in it is not a failure of the system: the mount is simply not
    // there and the capability stays off (docs/70).
    hal::logf(pal::kLogWarn, "SD", "no card mounted at %s (%s)", mount, esp_err_to_name(err));
    return false;
  }
  const uint64_t bytes = static_cast<uint64_t>(card->csd.capacity) * card->csd.sector_size;
  hal::logf(pal::kLogInfo, "SD", "%s mounted, %u MB", mount, static_cast<unsigned>(bytes / (1024u * 1024u)));
  return true;
}

}  // namespace

IStorage* esp32_sd(const char* mount, const SdPins& pins) {
  if (mount == nullptr || mount[0] != '/') return nullptr;

  static bool tried = false;
  static SdCard* card = nullptr;
  if (!tried) {
    tried = true;  // one attempt per boot, and the reason is in the header
    if (mount_card(mount, pins)) {
      static SdCard instance(mount);
      card = &instance;
    }
  }
  return card;
}

}  // namespace hal
