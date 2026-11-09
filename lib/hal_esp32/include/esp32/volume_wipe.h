// =============================================================================
//  lunokOS :: a volume of this board, emptied
// =============================================================================
//  **The card and the flash are two filesystems with one walk between them**: FATFS on the card and littlefs on the
//  flash are both mounted in the IDF's VFS, so `opendir`/`unlink`/`rmdir` is the API of the two, and a volume that is
//  emptied is a volume that keeps its own format —the FAT of the card and the superblock of littlefs are not rebuilt—.
//  That is the contract of `IStorage::format` (docs/70), and it lives here once and not twice (docs/85).
//
//  **Why not `lfs_format` or `f_mkfs`**: both want the volume unmounted and put back, and the littlefs of this core
//  writes a superblock with a `name_max` that the same core then refuses to mount —the trap the boot of the flash
//  documents—, o sigui que a half-done format is a board without a disk until the next upload. Emptying the volume is
//  the operation that cannot leave the board without one, and it is what a person means by formatting a card.
//
//  **A link cannot be here and the walk does not ask**: FATFS has no symbolic links and the littlefs of this core has no
//  call to make one, so `stat` is the whole story on the board —and this IDF does not declare `lstat` at all—. The walk of
//  the **emulator** is another one, in `emu_storage.cpp`, and that one uses `lstat`: its flash is a directory of this
//  machine and `make_disk` builds it out of symlinks to the repository, which is where a walk with `stat` emptied the
//  folder of a real app (9/11, 0.171.1, docs/85).
#ifndef LUNOK_ESP32_VOLUME_WIPE_H
#define LUNOK_ESP32_VOLUME_WIPE_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hal {
namespace volume {

const size_t kWipePathCap = 160;

// **A folder and everything under it, gone**, counting what went: the folders are removed after their contents, and a
// refusal —a file somebody has open— is `false` and stops the count, because a format that says it emptied a volume and
// left half of it is worse than one that says it could not.
inline bool wipe(const char* path, uint32_t& removed) {
  DIR* dir = opendir(path);
  if (dir == nullptr) return false;

  bool ok = true;
  for (struct dirent* item = readdir(dir); item != nullptr && ok; item = readdir(dir)) {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;

    char child[kWipePathCap];
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
      char deeper[kWipePathCap + 2];
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

}  // namespace volume
}  // namespace hal

#endif  // LUNOK_ESP32_VOLUME_WIPE_H
