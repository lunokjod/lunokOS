#include "kernel/vfs.h"

#include <string.h>

#include "kernel/log.h"
#include "kernel/nvs_fs.h"

namespace kernel {
namespace vfs {
namespace {

const size_t kMaxPathBytes = 128;
const uint32_t kMaxMounts = 8;

struct Mount {
  const char* path;  // virtual path, no trailing slash
  const char* rel;   // prefix inside the backend ("" or "apps")
  const char* kind;  // "littlefs" or "sd"
  hal::IStorage* storage;
  bool writable;
};

Mount g_mounts[kMaxMounts];
uint32_t g_count = 0;

void add(const char* path, const char* rel, const char* kind, hal::IStorage* storage,
         bool writable) {
  if (storage == nullptr || g_count >= kMaxMounts) return;
  Mount& m = g_mounts[g_count++];
  m.path = path;
  m.rel = rel;
  m.kind = kind;
  m.storage = storage;
  m.writable = writable;
}

}  // namespace

uint32_t init(hal::IPlatform& platform) {
  g_count = 0;

  hal::IStorage* littlefs = platform.storage("/littlefs");
  if (littlefs != nullptr) {
    add("/littlefs", "", "littlefs", littlefs, false);
    // Aliases cost one table row each: they resolve here, they are not copies.
    add("/apps", "apps", "littlefs", littlefs, false);
    add("/res", "res", "littlefs", littlefs, false);
    add("/cfg", "cfg", "littlefs", littlefs, false);
    add("/data", "data", "littlefs", littlefs, true);
  } else {
    log_printf(pal::kLogError, "vfs: /littlefs is mandatory and did not mount");
  }

  hal::IStorage* sd = platform.storage("/sd");
  if (sd != nullptr) add("/sd", "", "sd", sd, true);

  // /nvs: the key-value store seen as two levels of files, a namespace per
  // directory (docs/70). Read-only until the write side of M4 is done.
  hal::IStorage* nvs = nvs_storage(platform.nvs());
  if (nvs != nullptr) add("/nvs", "", "nvs", nvs, false);

  return g_count;
}

uint32_t mount_count() { return g_count; }

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

  size_t used = 0;
  const char* rel = best->rel;
  while (*rel != '\0') {
    if (used + 1 >= rel_cap) return false;
    rel_out[used++] = *rel++;
  }
  const char* rest = full + best_len;
  while (*rest == '/') ++rest;
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
  return storage->stat(rel, out);
}

bool list(const char* path, hal::ListSink& sink) {
  // Asking for the root means asking which mounts exist. There is no backend to
  // pass it to: the names come from the table.
  if (path != nullptr && path[0] == '/' && path[1] == '\0') {
    for (uint32_t i = 0; i < g_count; ++i) {
      hal::ListEntry entry;
      entry.name = g_mounts[i].path + 1;  // the table stores "/name"
      entry.type = hal::NodeType::kDir;
      entry.size = 0;
      entry.mtime = 0;
      if (!sink.on_entry(entry)) break;
    }
    return g_count > 0;
  }

  hal::IStorage* storage = nullptr;
  char rel[kMaxPathBytes];
  if (!resolve(path, storage, rel, sizeof(rel))) {
    log_printf(pal::kLogWarn, "vfs: cannot resolve %s", path);
    return false;
  }
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
