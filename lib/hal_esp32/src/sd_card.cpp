// A card in the board's slot: FATFS mounted through the IDF's VFS, and POSIX calls on
// top because that is what a mounted filesystem gives for free.
//
// There is no Arduino SD class here on purpose: it brings `<Arduino.h>` and its own bus,
// and this slot shares the bus with the panel (docs/50).
#include "hal/log.h"
#include <cerrno>
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
#include "esp32/volume_wipe.h"

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
    errno = 0;
    FILE* file = ::fopen(path, mode_name(mode));
    if (file == nullptr) {
      // **La targeta diu per què, o l'app no ho pot saber** (0.190.24): this layer used to answer `false` and nothing else,
      // and a `fs.write` that fails here reaches an app as `E_IO` —the disk failed— with no way of knowing whether the
      // card was full, the FAT was cross, every file this mount allows was open, or the bus was busy (docs/50, docs/70,
      // docs/95). The LittleFS layer has said its errno since 0.151.18; this is the same line for the card.
      logf(pal::kLogWarn, "SD", "cannot open %s for %s: errno %d (%u file(s) at once is the cap of this mount)", path,
           mode_name(mode), errno, static_cast<unsigned>(kMaxOpenFiles));
      return false;
    }
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
    errno = 0;
    const size_t put = ::fwrite(buf, 1, len, file);
    // A card that says it wrote fewer bytes is a card that is full or going: the count
    // is the answer and the caller decides (docs/70). **I es diu** (0.190.24): a short write used to reach an app as a
    // code with no line behind it, and the errno is the only thing that tells a full card from a bus that gave up.
    if (put != len) {
      logf(pal::kLogWarn, "SD", "wrote %u of %u B: errno %d", static_cast<unsigned>(put),
           static_cast<unsigned>(len), errno);
    }
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
    errno = 0;
    if (::rename(from_path, to_path) == 0) return true;
    const int first = errno;
    // **FatFs no reemplaça el destí i POSIX sí** (0.190.25, i el log de l'amo del 21/11 amb l'app Battery: *«cannot rename
    // /sd/data/battery/samples.txt.tmp to /sd/data/battery/samples.txt: errno 17»*). `f_rename` answers `FR_EXIST` when the
    // name is already taken —it registers a new object in the folder and the folder refuses a second one—, o sigui que **el
    // segon desament de qualsevol fitxer d'aquesta targeta** passava pel remei del nucli, que esborrava el destí i tornava a
    // provar. El remei és bo i aquest és el lloc on es paga sense dir-ho dues vegades: the destination is taken away and the
    // rename is tried once more, **and it is not an error until that one fails**. A failed rename that is fixed on the next
    // line is not news; a log line on every save of a board that is working is a line nobody reads (docs/50, docs/70, docs/95).
    if (first != EEXIST) {
      logf(pal::kLogDebug, "SD", "cannot rename %s to %s: errno %d", from_path, to_path, first);
      return false;
    }
    // **I només es toca el destí si hi és**: un `unlink` que falla per una altra cosa que no ser-hi és un disc que no
    // respon, i aleshores el `.tmp` s'ha de quedar on és (docs/70).
    if (::unlink(to_path) != 0 && errno != ENOENT) {
      logf(pal::kLogDebug, "SD", "cannot take away %s to rename over it: errno %d", to_path, errno);
      return false;
    }
    errno = 0;
    if (::rename(from_path, to_path) != 0) {
      logf(pal::kLogDebug, "SD", "cannot rename %s to %s: errno %d", from_path, to_path, errno);
      return false;
    }
    return true;
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
    return volume::wipe(path, removed);
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

  // **La targeta, rentada** (0.189.18): every file is opened, written and closed in one call of this system, so FatFs has
  // already written the data and the table of the folder by the time `close` answers and there is no descriptor left to
  // flush. **What no call can do from here is the card's own rest**: its controller copies its buffer into the flash some
  // milliseconds after the last sector arrived, and that is what the margin of `power::settle_before_leaving` is for —on
  // this board there is no `sync()` to link against either— (docs/75).
  bool flush() override { return true; }

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

// **Els altres xips del bus, quiets abans de res** (0.190.83, l'amo: *«la targeta SD no es munta al T5S3? fes-ho!»*): on
// the T5S3 the LoRa and the slot are on the same SPI2, and a radio whose CS is left floating drives MISO and corrupts the
// card's own commands —the mount then fails with `sdmmc_io_reset: unexpected return: 0x104`, which is what the board was
// doing one boot out of two—. El que fa això és el que diu el seu exemple: cada pin com a sortida, i al nivell que el deixa
// fora del bus (docs/50, docs/70).
void quiet_the_other_devices(const SdPins& pins) {
  for (size_t i = 0; i < pins.quiet_count && pins.quiet != nullptr && pins.quiet_level != nullptr; ++i) {
    const int pin = pins.quiet[i];
    if (pin < 0) continue;
    gpio_config_t line = {};
    line.pin_bit_mask = 1ULL << pin;
    line.mode = GPIO_MODE_OUTPUT;
    line.pull_up_en = GPIO_PULLUP_DISABLE;
    line.pull_down_en = GPIO_PULLDOWN_DISABLE;
    line.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&line);
    gpio_set_level(static_cast<gpio_num_t>(pin), pins.quiet_level[i]);
  }
}

bool mount_card(const char* mount, const SdPins& pins) {
  quiet_the_other_devices(pins);
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

  // **Tres intents i el rellotge baixant** (0.190.83): a card that answers the probe and then gives up is a card driven too
  // fast for these wires, i el que s'ha de fer és tornar-ho a provar més a poc a poc i no quedar-se sense disc tot el run. The
  // first attempt is the board's clock, the second is half of it and the third is the probing one (400 kHz), which no card
  // refuses (docs/70, docs/90).
  sdmmc_card_t* card = nullptr;
  esp_err_t err = ESP_FAIL;
  const int attempts = 3;
  for (int i = 0; i < attempts; ++i) {
    if (i == 1) host.max_freq_khz = (pins.max_khz > 0) ? (pins.max_khz / 2) : (SDMMC_FREQ_DEFAULT / 2);
    if (i == 2) host.max_freq_khz = SDMMC_FREQ_PROBING;
    // **Els altres xips tornen a quedar quiets a cada intent**: the first attempt may have left the radio's line in another
    // state, and a retry that does not put it back is a retry of the same mistake (docs/50).
    quiet_the_other_devices(pins);
    err = esp_vfs_fat_sdspi_mount(mount, &host, &slot, &config, &card);
    if (err == ESP_OK && card != nullptr) {
      if (i > 0) hal::logf(pal::kLogInfo, "SD", "%s came up on attempt %d at %d kHz", mount, i + 1, host.max_freq_khz);
      break;
    }
    card = nullptr;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (err != ESP_OK || card == nullptr) {
    // A slot with no card in it is not a failure of the system: the mount is simply not
    // there and the capability stays off (docs/70).
    hal::logf(pal::kLogWarn, "SD", "no card mounted at %s after %d attempts (%s)", mount, attempts, esp_err_to_name(err));
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
