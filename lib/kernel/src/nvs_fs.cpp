#include "kernel/nvs_fs.h"

#include <stdio.h>
#include <string.h>

namespace kernel {
namespace {

// An NVS value is small by definition, and the biggest one the system keeps is
// the alarm queue (768 B). A value that does not fit is refused, never cut.
const size_t kValueCap = 1024;
const size_t kNameCap = 64;

char g_value[kValueCap];
bool g_open = false;
uint32_t g_size = 0;
uint32_t g_pos = 0;

// "alarms/queue" is namespace "alarms" and key "queue". The key-value store has
// two levels and no more, so a deeper path is not a file (docs/70).
bool split(const char* rel, char* ns, char* key) {
  if (rel == nullptr || rel[0] == '\0') return false;
  const char* slash = strchr(rel, '/');
  if (slash == nullptr) return false;

  const size_t ns_len = static_cast<size_t>(slash - rel);
  if (ns_len == 0 || ns_len + 1 > kNameCap) return false;

  const char* rest = slash + 1;
  const size_t key_len = strlen(rest);
  if (key_len == 0 || key_len + 1 > kNameCap) return false;
  if (strchr(rest, '/') != nullptr) return false;  // two levels, no more

  memcpy(ns, rel, ns_len);
  ns[ns_len] = '\0';
  memcpy(key, rest, key_len + 1);
  return true;
}

class NvsStorage : public hal::IStorage {
 public:
  explicit NvsStorage(hal::IKeyValue* store) : store_(store) {}

  bool stat(const char* rel, hal::Stat& out) override {
    if (store_ == nullptr || rel == nullptr) return false;
    out.type = hal::NodeType::kNone;
    out.size = 0;
    out.mtime = 0;

    if (rel[0] == '\0') {  // the mount itself: the namespaces live here
      out.type = hal::NodeType::kDir;
      return true;
    }
    if (strchr(rel, '/') == nullptr) {  // a namespace
      out.type = hal::NodeType::kDir;
      return true;
    }

    char ns[kNameCap];
    char key[kNameCap];
    if (!split(rel, ns, key)) return false;
    size_t len = 0;
    if (!value_size(ns, key, len)) return false;
    out.type = hal::NodeType::kFile;
    out.size = static_cast<uint32_t>(len);
    return true;
  }

  bool list(const char* rel, hal::ListSink& sink) override {
    if (store_ == nullptr) return false;

    if (rel == nullptr || rel[0] == '\0') {
      // The namespaces, as directories.
      const char* name = nullptr;
      bool first = true;
      while (store_->next_namespace(name, first)) {
        hal::ListEntry entry;
        entry.name = name;
        entry.type = hal::NodeType::kDir;
        entry.size = 0;
        entry.mtime = 0;
        if (!sink.on_entry(entry)) break;
      }
      return true;
    }

    if (strchr(rel, '/') != nullptr) return false;  // a file has nothing to list
    if (strlen(rel) >= kNameCap) return false;

    const char* name = nullptr;
    bool first = true;
    while (store_->next(rel, name, first)) {
      hal::ListEntry entry;
      size_t len = 0;
      entry.name = name;
      entry.type = hal::NodeType::kFile;
      entry.size = value_size(rel, name, len) ? static_cast<uint32_t>(len) : 0;
      entry.mtime = 0;
      if (!sink.on_entry(entry)) break;
    }
    return true;
  }

  // A value is read whole into the one buffer: an NVS file is small, and opening
  // a file must not allocate (docs/75). One NVS file is open at a time.
  bool open(const char* rel, hal::OpenMode mode, hal::FileSlot& slot) override {
    if (store_ == nullptr || mode != hal::OpenMode::kRead) return false;
    if (g_open) return false;

    char ns[kNameCap];
    char key[kNameCap];
    if (!split(rel, ns, key)) return false;

    size_t len = 0;
    if (!store_->get(ns, key, g_value, sizeof(g_value), len)) return false;
    if (len >= sizeof(g_value)) return false;  // too big to show, and not cut

    g_size = static_cast<uint32_t>(len);
    g_pos = 0;
    g_open = true;
    slot.size = g_size;
    slot.impl = nullptr;
    return true;
  }

  int32_t read(hal::FileSlot& slot, void* buf, uint32_t len) override {
    (void)slot;
    if (!g_open || buf == nullptr) return -1;
    if (g_pos >= g_size) return 0;
    size_t left = static_cast<size_t>(g_size - g_pos);
    if (left > len) left = len;
    memcpy(buf, g_value + g_pos, left);
    g_pos += static_cast<uint32_t>(left);
    return static_cast<int32_t>(left);
  }

  bool close(hal::FileSlot& slot) override {
    (void)slot;
    g_open = false;
    g_size = 0;
    g_pos = 0;
    return true;
  }

  // The write side of the mount arrives with the rest of M4; until then it is
  // read-only and says no where it is asked.
  int32_t write(hal::FileSlot&, const void*, uint32_t) override { return -1; }
  bool seek(hal::FileSlot&, uint32_t) override { return false; }
  bool remove(const char*) override { return false; }
  bool mkdir(const char*) override { return false; }
  bool rename(const char*, const char*) override { return false; }
  bool space(uint64_t&, uint64_t&) override { return false; }

 private:
  // The size of a value, which the store only gives by reading it. It borrows the
  // one buffer when no file is open, so the size of a file is never guessed.
  bool value_size(const char* ns, const char* key, size_t& len) {
    if (g_open) return false;
    len = 0;
    if (!store_->get(ns, key, g_value, sizeof(g_value), len)) return false;
    if (len >= sizeof(g_value)) return false;
    return true;
  }

  hal::IKeyValue* store_;
};

}  // namespace

hal::IStorage* nvs_storage(hal::IKeyValue* store) {
  if (store == nullptr) return nullptr;
  static NvsStorage storage(store);  // built once, on the first mount
  return &storage;
}

}  // namespace kernel
