// The key-value store of the ESP32, over the `nvs` partition.
//
// A namespace is an NVS namespace and a key is a key; the value is opaque bytes on
// both sides, like the emulator's one-file-per-key store. Nothing here knows what is
// being saved (docs/70).
#include <Preferences.h>
#include <nvs.h>
#include <string.h>

#include "esp32/nvs.h"

namespace hal {
namespace {

// The data partition in the partition table.
const char kPartition[] = "nvs";

// A namespace and a key have to be plain names: they end up in the NVS index, and
// the point of refusing them here is that a typo is caught where it is made.
bool plain_name(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  for (const char* p = name; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\' || *p == '.') return false;
  }
  return true;
}

// A copy that cannot be truncated silently: an NVS key is short by design, and one
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

class Esp32Nvs : public IKeyValue {
 public:
  bool get(const char* ns, const char* key, void* buf, size_t cap, size_t& len) override {
    if (buf == nullptr || cap == 0 || !plain_name(ns) || !plain_name(key)) return false;

    // The IDF's own open and not Arduino's `Preferences`: Preferences logs
    // `nvs_open failed: NOT_FOUND` when the namespace does not exist yet, and a board
    // that has never saved anything is not an error — it printed two of those lines
    // on every `provision` (measured on the board, 31/10).
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
    // Only a blob is a value here: a key of another type belongs to the IDF or to
    // somebody else (the `phy` namespace keeps integers), and `nvs_get_blob` says
    // TYPE_MISMATCH and logs nothing (docs/70).
    size_t stored = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &stored);
    if (err != ESP_OK || stored == 0 || stored > cap) {
      nvs_close(handle);
      return false;
    }
    err = nvs_get_blob(handle, key, buf, &stored);
    nvs_close(handle);
    if (err != ESP_OK) return false;
    len = stored;
    return true;
  }

  bool set(const char* ns, const char* key, const void* buf, size_t len) override {
    if (buf == nullptr || len == 0 || !plain_name(ns) || !plain_name(key)) return false;

    Preferences prefs;
    if (!prefs.begin(ns, false)) return false;
    const size_t put = prefs.putBytes(key, buf, len);
    prefs.end();
    return put == len;
  }

  bool erase(const char* ns, const char* key) override {
    if (!plain_name(ns) || !plain_name(key)) return false;

    Preferences prefs;
    if (!prefs.begin(ns, false)) return false;
    // Erasing a key that is not there is not an error worth a line in the log, and
    // Preferences::remove does log one when the IDF says NOT_FOUND.
    const bool there = prefs.isKey(key);
    const bool removed = there ? prefs.remove(key) : false;
    prefs.end();
    return removed;
  }

  // Iteration goes through the IDF's own iterator: Arduino's Preferences has no way
  // to walk a namespace. In this IDF the iterator is the return value of
  // nvs_entry_find and nvs_entry_next, and it is released when it runs out.
  bool next(const char* ns, const char*& key, bool& first) override {
    if (!plain_name(ns)) return false;

    if (first) {
      release();
      first = false;
      iterator_ = nvs_entry_find(kPartition, ns, NVS_TYPE_ANY);
    } else if (iterator_ != nullptr) {
      iterator_ = nvs_entry_next(iterator_);
    }

    while (iterator_ != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(iterator_, &info);  // void in this IDF
      if (copy_name(key_buf_, sizeof(key_buf_), info.key)) {
        key = key_buf_;
        return true;
      }
      iterator_ = nvs_entry_next(iterator_);  // a key that does not fit is skipped
    }
    release();
    return false;
  }

  // The namespaces themselves: the same iterator, asked for with a null namespace
  // name, which hands back one entry per key. The names are remembered so a
  // namespace with three keys is listed once (docs/70).
  bool next_namespace(const char*& ns, bool& first) override {
    if (first) {
      release();
      first = false;
      seen_count_ = 0;
      iterator_ = nvs_entry_find(kPartition, nullptr, NVS_TYPE_ANY);
    } else if (iterator_ != nullptr) {
      iterator_ = nvs_entry_next(iterator_);
    }

    while (iterator_ != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(iterator_, &info);
      if (copy_name(key_buf_, sizeof(key_buf_), info.namespace_name) &&
          !already_seen(key_buf_)) {
        remember(key_buf_);
        ns = key_buf_;
        return true;
      }
      iterator_ = nvs_entry_next(iterator_);
    }
    release();
    return false;
  }

  // What the partition holds: used entries of total entries. A full store answers
  // ESP_ERR_NVS_NOT_ENOUGH_SPACE to the next save, and this is how a person sees it
  // coming (docs/70).
  bool stats(uint32_t& used, uint32_t& total) override {
    nvs_stats_t info;
    memset(&info, 0, sizeof(info));
    if (nvs_get_stats(nullptr, &info) != ESP_OK) return false;  // nullptr: the default partition
    used = info.used_entries;
    total = info.total_entries;
    return true;
  }

 private:
  bool already_seen(const char* name) const {
    for (uint32_t i = 0; i < seen_count_; ++i) {
      if (strcmp(seen_[i], name) == 0) return true;
    }
    return false;
  }

  void remember(const char* name) {
    if (seen_count_ >= kSeenMax) return;
    copy_name(seen_[seen_count_], sizeof(seen_[0]), name);
    ++seen_count_;
  }

  static const uint32_t kSeenMax = 4;  // more than the system uses, by design
  char seen_[kSeenMax][16] = {{0}};
  uint32_t seen_count_ = 0;
  void release() {
    if (iterator_ != nullptr) {
      nvs_release_iterator(iterator_);
      iterator_ = nullptr;
    }
  }

  nvs_iterator_t iterator_ = nullptr;
  char key_buf_[32] = {0};
};

Esp32Nvs g_nvs;

}  // namespace

IKeyValue* esp32_nvs() { return &g_nvs; }

}  // namespace hal
