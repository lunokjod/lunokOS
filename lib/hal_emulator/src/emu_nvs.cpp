// The emulator's key-value store: one file per key.
//
// `<state_dir>/nvs/<namespace>/<key>` holds exactly the bytes that were set, so the
// value is as opaque here as it is inside the ESP32's NVS blob. A namespace is a
// directory and a key is a file, which is what makes `ls` on the emulator state
// enough to see what the system kept (docs/70 and docs/85).
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "emu/disk.h"
#include "emu/options.h"
#include "emu_nvs.h"
#include "hal/hal.h"

namespace hal {
namespace {

const size_t kPathCap = 320;

// **How the NVS really writes**: Espressif's key-value store appends entries of thirty-two bytes to a
// page of 4 KB and **erases the page only when it is full**, which is why setting a key costs a page
// programme of 0.7 ms and not the sector of 45 ms that a littlefs commit pays. The page counter is the
// store's, not one file's: every key of every namespace goes in the same page (docs/70, docs/85).
const uint32_t kNvsEntryBytes = 32;
const uint32_t kNvsPageEntries = 126;  // 4 KB less the header of the page, over 32 bytes
uint32_t g_nvs_entries = 0;

// What one write of the store costs, and the erase when the page turns over.
void nvs_pay() {
  bool erasing = false;
  if (++g_nvs_entries > kNvsPageEntries) {
    g_nvs_entries = 1;
    erasing = true;
  }
  emu::disk_pay_write(emu::Disk::kFlash, kNvsEntryBytes, erasing);
}

void join(char* out, size_t cap, const char* a, const char* b, const char* c = nullptr) {
  size_t used = 0;
  const char* parts[3] = {a, b, c};
  for (int i = 0; i < 3; ++i) {
    if (parts[i] == nullptr) continue;
    for (const char* p = parts[i]; *p != '\0' && used + 1 < cap; ++p) out[used++] = *p;
  }
  out[used] = '\0';
}

void make_dirs(const char* path) {
  char partial[kPathCap];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  for (size_t i = 1; i <= len; ++i) {
    if (partial[i] == '/' || partial[i] == '\0') {
      const char saved = partial[i];
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // it fails when it is already there, which is fine
      partial[i] = saved;
    }
  }
}

// Where a namespace lives, creating it: the store is written by the system, not
// by a person, so nobody has to make the directory first. It lives under `state_dir` and not
// under the flash volume: on a real board the NVS is a partition of its own and it is not a file
// a person sees in `/littlefs` (docs/85).
bool ns_root(char* out, size_t cap) {
  const emu::Options& o = emu::options();
  const char* base = (o.state_dir != nullptr) ? o.state_dir : o.fs_dir;
  join(out, cap, (base != nullptr) ? base : "", "/nvs");
  return true;
}

bool ns_dir(char* out, size_t cap, const char* ns, bool create) {
  char root[kPathCap];
  ns_root(root, sizeof(root));
  if (create) make_dirs(root);
  join(out, cap, root, "/", ns);
  if (create) make_dirs(out);
  return true;
}

// A namespace and a key have to be plain names: they become a path, and `..` in
// a path is the classic way to write outside the store.
bool plain_name(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  for (const char* p = name; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\' || *p == '.') return false;
  }
  return true;
}

bool key_path(char* out, size_t cap, const char* ns, const char* key, bool create) {
  if (!plain_name(ns) || !plain_name(key)) return false;
  char dir[kPathCap];
  if (!ns_dir(dir, sizeof(dir), ns, create)) return false;
  join(out, cap, dir, "/", key);
  return true;
}

// A copy that cannot be truncated silently and cannot make the compiler guess: a
// directory entry name is up to 256 bytes and a key is short by design, so a name
// that does not fit is not a key of ours (docs/70).
bool copy_name(char* dst, size_t cap, const char* src) {
  size_t i = 0;
  for (; src[i] != '\0'; ++i) {
    if (i + 1 >= cap) return false;
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return true;
}

class HostNvs : public IKeyValue {
 public:
  bool get(const char* ns, const char* key, void* buf, size_t cap, size_t& len) override {
    char path[kPathCap];
    if (buf == nullptr || cap == 0 || !key_path(path, sizeof(path), ns, key, false)) return false;
    // **The key-value store is a partition of the flash**, so a read of it is a block of the nor and a
    // write is a page programme plus the sector it took (docs/70, docs/85).
    emu::disk_pay(emu::Disk::kFlash, emu::disk_timing(emu::Disk::kFlash).block_bytes, false);
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return false;
    const size_t got = fread(buf, 1, cap, file);
    fclose(file);
    len = got;
    return true;
  }

  bool set(const char* ns, const char* key, const void* buf, size_t len) override {
    char path[kPathCap];
    if (buf == nullptr || !key_path(path, sizeof(path), ns, key, true)) return false;
    // An entry of the store and not a file of the volume: the page programme, and the sector only when
    // the page is full (docs/70).
    nvs_pay();
    FILE* file = fopen(path, "wb");
    if (file == nullptr) return false;
    const size_t put = fwrite(buf, 1, len, file);
    fclose(file);
    return put == len;
  }

  bool erase(const char* ns, const char* key) override {
    char path[kPathCap];
    if (!key_path(path, sizeof(path), ns, key, false)) return false;
    if (::remove(path) != 0) return false;
    // A deletion is an entry too: the store writes a tombstone instead of moving the bytes around.
    nvs_pay();
    return true;
  }

  // Iteration keeps one directory open between calls, which is what the
  // interface asks for: first = true starts again, and the key stays valid until
  // the next call.
  bool next(const char* ns, const char*& key, bool& first) override {
    if (first) {
      if (dir_ != nullptr) closedir(dir_);
      dir_ = nullptr;
      char path[kPathCap];
      if (!plain_name(ns) ||
          !ns_dir(path, sizeof(path), ns, false)) {
        return false;
      }
      dir_ = opendir(path);
      if (dir_ == nullptr) return false;
      first = false;
    }
    if (dir_ == nullptr) return false;

    for (;;) {
      struct dirent* entry = readdir(dir_);
      if (entry == nullptr) {
        closedir(dir_);
        dir_ = nullptr;
        return false;
      }
      if (entry->d_name[0] == '.') continue;  // "." and ".."
      if (!copy_name(key_buf_, sizeof(key_buf_), entry->d_name)) continue;
      key = key_buf_;
      return true;
    }
  }

  // A namespace is a directory of `<state_dir>/nvs`, so listing them is listing it.
  bool next_namespace(const char*& ns, bool& first) override {
    if (first) {
      if (ns_dir_ != nullptr) closedir(ns_dir_);
      ns_dir_ = nullptr;
      char root[kPathCap];
      ns_root(root, sizeof(root));
      ns_dir_ = opendir(root);
      if (ns_dir_ == nullptr) return false;
      first = false;
    }
    if (ns_dir_ == nullptr) return false;

    for (;;) {
      struct dirent* entry = readdir(ns_dir_);
      if (entry == nullptr) {
        closedir(ns_dir_);
        ns_dir_ = nullptr;
        return false;
      }
      if (entry->d_name[0] == '.') continue;
      if (!copy_name(ns_buf_, sizeof(ns_buf_), entry->d_name)) continue;
      ns = ns_buf_;
      return true;
    }
  }

  // One file per key, so counting is walking two levels. A namespace that is not there
  // yet is not an error: nothing has been saved, which is zero entries. There is no
  // ceiling to report —a file costs what it costs on the PC's disk— and `total` says that
  // by being 0 (docs/85).
  bool stats(uint32_t& used, uint32_t& total) override {
    used = 0;
    total = 0;
    char root[kPathCap];
    ns_root(root, sizeof(root));
    DIR* namespaces = opendir(root);
    if (namespaces == nullptr) return true;
    for (;;) {
      struct dirent* entry = readdir(namespaces);
      if (entry == nullptr) break;
      if (entry->d_name[0] == '.') continue;
      char dir[kPathCap];
      join(dir, sizeof(dir), root, "/", entry->d_name);
      DIR* keys = opendir(dir);
      if (keys == nullptr) continue;
      for (;;) {
        struct dirent* key = readdir(keys);
        if (key == nullptr) break;
        if (key->d_name[0] == '.') continue;
        ++used;
      }
      closedir(keys);
    }
    closedir(namespaces);
    return true;
  }

 private:
  DIR* dir_ = nullptr;
  DIR* ns_dir_ = nullptr;
  char key_buf_[64] = {0};
  char ns_buf_[64] = {0};
};

HostNvs g_nvs;

}  // namespace

IKeyValue* emu_nvs() { return &g_nvs; }

}  // namespace hal
