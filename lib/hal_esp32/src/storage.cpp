// LittleFS backend for the ESP32.
//
// The partition is found by the label the board's table gives, which is why begin()
// gets the name explicitly. Handles come from a fixed pool: opening a file never
// allocates (docs/75).
// **El disc, amb el nucli d'Arduino o sense** (0.190.90): el camí d'Arduino passa per l'objecte `LittleFS` del nucli i el d'ESP-IDF pel
// component `esp_littlefs` i per les crides POSIX, que és sobre el que el nucli fa l'objecte. El que no canvia és res de la casa: el
// sistema VFS, el nom de la partició i la regla de no formatar mai el disc (docs/50, docs/70).
#if defined(ARDUINO)
#include <LittleFS.h>
#else
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <fcntl.h>
#include <esp_partition.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp32/storage.h"
#include "esp32/volume_wipe.h"
#include "hal/hal.h"
#include "hal/log.h"

namespace hal {
namespace {

const size_t kMaxFiles = 8;
const size_t kPathCap = 160;

// Where LittleFS is mounted in the system VFS. LittleFS.open() reads paths relative
// to it; POSIX calls (stat, mkdir, unlink, rename) need it in front.
const char kMountPoint[] = "/littlefs";

#if defined(ARDUINO)
using LunokFile = File;
#else
// **El mateix que `File` i només el que aquest fitxer li demana** (0.190.90): un descriptiu i quatre crides del sistema. Amb això la
// piscina de vuit fitxers, el comptador i els missatges de plena es queden exactament com eren (docs/70, docs/75).
struct LunokFile {
  int fd = -1;

  explicit operator bool() const { return fd >= 0; }
  size_t size() const {
    struct stat info;
    return (fd >= 0 && fstat(fd, &info) == 0) ? static_cast<size_t>(info.st_size) : 0;
  }
  int32_t read(uint8_t* buf, size_t len) { return (fd >= 0) ? static_cast<int32_t>(::read(fd, buf, len)) : -1; }
  size_t write(const uint8_t* buf, size_t len) {
    return (fd >= 0) ? ::write(fd, buf, len) : 0;
  }
  bool seek(size_t pos, int) { return fd >= 0 && lseek(fd, static_cast<off_t>(pos), SEEK_SET) == static_cast<off_t>(pos); }
  void close() {
    if (fd >= 0) ::close(fd);
    fd = -1;
  }
};
#endif

LunokFile g_files[kMaxFiles];
// **El nom de la partició, que a l'entorn d'ESP-IDF també demana l'espai lliure** (0.190.90): el muntatge el rep com a paràmetre i el
// deixa aquí, perquè `space` és una pregunta al mateix disc i no ha de tornar a endevinar-lo (docs/70).
const char* g_partition_label = nullptr;
bool g_used[kMaxFiles] = {false};

// **Dos noms per a un camí, i són dos mons** (0.190.92, el primer flaix amb disc): `LittleFS.open` del nucli d'Arduino llegeix
// **relatiu al seu punt de muntatge** (`/res/fonts/…`), i el VFS de la IDF vol el camí **sencer** (`/littlefs/res/fonts/…`). Amb el
// nom curt al camí POSIX, el disc es muntava i tots els fitxers deien `errno 2`: `ls` funcionava perquè fa servir `readdir` i les
// pàgines no, perquè obren (docs/70).
void fs_path(const char* rel, char* out, size_t cap) {
  size_t used = 0;
  out[used++] = '/';
  for (const char* p = rel; *p != '\0' && used + 1 < cap; ++p) out[used++] = *p;
  out[used] = '\0';
}

void whole_path(const char* rel, char* out, size_t cap) {
#if defined(ARDUINO)
  fs_path(rel, out, cap);
#else
  size_t used = 0;
  for (const char* p = kMountPoint; *p != '\0' && used + 1 < cap; ++p) out[used++] = *p;
  if (used + 1 < cap) out[used++] = '/';
  for (const char* p = rel; *p != '\0' && used + 1 < cap; ++p) out[used++] = *p;
  out[used] = '\0';
#endif
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
    whole_path(rel, path, sizeof(path));
#if defined(ARDUINO)
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
#else
    // **El mateix recorregut amb les crides del sistema** (0.190.90): el VFS és el mateix i el que canvia és qui el llegeix. El nom
    // surt sencer de `readdir` i el que la casa vol és l'últim tros, i el tipus i la mida són un `stat` de cada fill (docs/70).
    DIR* dir = opendir(path);
    if (dir == nullptr) return false;
    bool keep = true;
    for (struct dirent* one = readdir(dir); one != nullptr && keep; one = readdir(dir)) {
      if (strcmp(one->d_name, ".") == 0 || strcmp(one->d_name, "..") == 0) continue;
      ListEntry entry;
      entry.name = one->d_name;
      char full[kPathCap + 260];
      snprintf(full, sizeof(full), "%s%s%s", path, (path[strlen(path) - 1] == '/') ? "" : "/", one->d_name);
      struct stat info;
      const bool known = (::stat(full, &info) == 0);
      entry.type = (known && S_ISDIR(info.st_mode)) ? NodeType::kDir : NodeType::kFile;
      entry.size = (entry.type == NodeType::kDir || !known) ? 0 : static_cast<uint32_t>(info.st_size);
      entry.mtime = 0;
      keep = sink.on_entry(entry);
    }
    closedir(dir);
    return true;
#endif
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
    whole_path(rel, path, sizeof(path));
    errno = 0;
#if defined(ARDUINO)
    g_files[free_index] = LittleFS.open(path, mode_name(mode));
#else
    {
      const int flags = (mode == OpenMode::kRead)      ? O_RDONLY
                        : (mode == OpenMode::kWrite)   ? (O_WRONLY | O_CREAT | O_TRUNC)
                        : (mode == OpenMode::kAppend)  ? (O_WRONLY | O_CREAT | O_APPEND)
                                                       : O_RDWR;
      g_files[free_index].fd = ::open(path, flags, 0644);
    }
#endif
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
    LunokFile* file = file_of(slot);
    if (file == nullptr) return -1;
    return static_cast<int32_t>(file->read(static_cast<uint8_t*>(buf), len));
  }

  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override {
    LunokFile* file = file_of(slot);
    if (file == nullptr) return -1;
    return static_cast<int32_t>(file->write(static_cast<const uint8_t*>(buf), len));
  }

  bool seek(FileSlot& slot, uint32_t pos) override {
    LunokFile* file = file_of(slot);
    if (file == nullptr) return false;
#if defined(ARDUINO)
    return file->seek(pos, SeekSet);
#else
    return file->seek(pos, SEEK_SET);
#endif
  }

  bool close(FileSlot& slot) override {
    LunokFile* file = file_of(slot);
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
    whole_path(rel, path, sizeof(path));
#if defined(ARDUINO)
    return LittleFS.remove(path);
#else
    return ::unlink(path) == 0;
#endif
  }

  bool mkdir(const char* rel) override {
    char path[kPathCap];
    whole_path(rel, path, sizeof(path));
#if defined(ARDUINO)
    return LittleFS.mkdir(path);
#else
    return ::mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
  }

  bool rename(const char* from, const char* to) override {
    char from_path[kPathCap];
    char to_path[kPathCap];
    whole_path(from, from_path, sizeof(from_path));
    whole_path(to, to_path, sizeof(to_path));
#if defined(ARDUINO)
    return LittleFS.rename(from_path, to_path);
#else
    return ::rename(from_path, to_path) == 0;
#endif
  }

  // **Emptying the flash** (0.171.1): every file and every folder of the volume, counted, and **the superblock stays**. A
  // person asked for a page that formats the flash the way it formats a card, and the two are the same operation: the
  // filesystem is the one it was and what goes is what was in it (docs/50, docs/70). The boot keeps its rule and does not
  // format anything by itself.
  bool format(uint32_t& removed) override {
    char path[kPathCap];
    snprintf(path, sizeof(path), "%s/", kMountPoint);
    removed = 0;
    return volume::wipe(path, removed);
  }

  bool space(uint64_t& total, uint64_t& free) override {
#if defined(ARDUINO)
    total = LittleFS.totalBytes();
    const uint64_t used = LittleFS.usedBytes();
#else
    size_t bytes_total = 0;
    size_t bytes_used = 0;
    if (esp_littlefs_info(g_partition_label, &bytes_total, &bytes_used) != ESP_OK) return false;
    total = bytes_total;
    const uint64_t used = bytes_used;
#endif
    free = (total > used) ? (total - used) : 0;
    return true;
  }

  // **El flaix, rentat** (0.189.18): LittleFS writes the block of a file when the file is closed, and this system closes
  // every file inside the call that wrote it —`write_block` is the only writer of a session and it opens, writes and closes
  // in one go— so there is nothing held open here. **L'ESP-IDF no té un `sync()` que es pugui enllaçar** (declarat a
  // `unistd.h` i sense implementació, mesurat el 14/11): la resposta honesta és aquesta i no una crida que no fa res; el que
  // queda per a la targeta és el repòs de `power::settle_before_leaving` (docs/70, docs/75).
  bool flush() override { return true; }

 private:
  static LunokFile* file_of(FileSlot& slot) { return static_cast<LunokFile*>(slot.impl); }
};

Esp32Fs g_littlefs;

}  // namespace

IStorage* esp32_storage(const char* mount, const char* partition) {
  if (mount == nullptr || strcmp(mount, kMountPoint) != 0) return nullptr;
  if (partition == nullptr || partition[0] == '\0') return nullptr;

  static bool tried = false;
  static bool mounted = false;
  g_partition_label = partition;
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
#if defined(ARDUINO)
    mounted = LittleFS.begin(false, kMountPoint, 10, partition);
#else
    // **`format_if_mount_failed` és fals i ho ha de ser** (0.190.90): la regla de la casa és que el firmware no formata mai el disc, i el
    // component té aquesta porta oberta; el disc el fa `tools/make_disk.py` (docs/50, docs/70).
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kMountPoint;
    conf.partition_label = partition;
    conf.format_if_mount_failed = 0;
    conf.read_only = 0;
    mounted = (esp_vfs_littlefs_register(&conf) == ESP_OK);
#endif
  }
  return mounted ? &g_littlefs : nullptr;
}

}  // namespace hal
