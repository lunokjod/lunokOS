// A storage that **is one file** of a fixed size, over a buffer the board owns.
//
// The RTC RAM is the case it exists for: it is the only memory that survives a deep sleep and dies
// with the power, so a board can keep a few bytes between two runs without touching the flash —which
// wears— and without needing a chip (docs/50, docs/70). The buffer is handed in, so the same code
// serves the chip's memory on the board and the file that stands for it on the PC (docs/85).
//
// The file has no name of its own: it is the mount. `stat("")` is the file, reading and writing go
// through the slot, and there is nothing to list and nothing to create.
#ifndef LUNOK_HAL_MEMORY_FILE_H
#define LUNOK_HAL_MEMORY_FILE_H

#include <string.h>

#include "hal/hal.h"

namespace hal {

class MemoryFile : public IStorage {
 public:
  MemoryFile(uint8_t* bytes, uint32_t size) : bytes_(bytes), size_(size) {}

  bool stat(const char* rel, Stat& out) override {
    if (!is_the_file(rel)) return false;
    out.type = NodeType::kFile;
    out.size = size_;
    out.mtime = 0;  // the memory has no clock of its own (docs/70)
    return true;
  }

  // A file has nothing inside it: a board that could list `/rtc` would be saying it is a directory.
  bool list(const char* rel, ListSink& sink) override {
    (void)rel;
    (void)sink;
    return false;
  }

  bool open(const char* rel, OpenMode mode, FileSlot& slot) override {
    if (!is_the_file(rel)) return false;
    for (uint32_t i = 0; i < kOpenSlots; ++i) {
      if (slots_[i].used) continue;
      slots_[i].used = true;
      slots_[i].pos = (mode == OpenMode::kAppend) ? size_ : 0;
      slot.impl = &slots_[i];
      slot.size = size_;
      return true;
    }
    return false;  // the pool is full, which the caller is told instead of a crash
  }

  int32_t read(FileSlot& slot, void* buf, uint32_t len) override {
    Open* open = slot_of(slot);
    if (open == nullptr || buf == nullptr) return -1;
    if (open->pos >= size_) return 0;  // the end of the memory is the end of the file
    uint32_t room = size_ - open->pos;
    const uint32_t take = (len < room) ? len : room;
    memcpy(buf, bytes_ + open->pos, take);
    open->pos += take;
    return static_cast<int32_t>(take);
  }

  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override {
    Open* open = slot_of(slot);
    if (open == nullptr || buf == nullptr) return -1;
    if (open->pos >= size_) return 0;  // full: the memory does not grow
    uint32_t room = size_ - open->pos;
    const uint32_t put = (len < room) ? len : room;
    memcpy(bytes_ + open->pos, buf, put);
    open->pos += put;
    return static_cast<int32_t>(put);
  }

  bool seek(FileSlot& slot, uint32_t pos) override {
    Open* open = slot_of(slot);
    if (open == nullptr || pos > size_) return false;
    open->pos = pos;
    return true;
  }

  bool close(FileSlot& slot) override {
    Open* open = slot_of(slot);
    if (open == nullptr) return false;
    open->used = false;
    open->pos = 0;
    slot.impl = nullptr;
    slot.size = 0;
    return true;
  }

  // It is not a directory and the memory is not a filesystem: nothing is created, removed or
  // renamed, and saying no is part of the contract (docs/70).
  bool remove(const char* rel) override {
    (void)rel;
    return false;
  }
  bool mkdir(const char* rel) override {
    (void)rel;
    return false;
  }
  bool rename(const char* from, const char* to) override {
    (void)from;
    (void)to;
    return false;
  }

  // Its room is its size, and all of it is free, always: this is raw memory and not a store with
  // keys, so nothing is allocated out of it and nothing is spent by keeping a value —the app lays
  // the bytes out as it likes, and the whole window is there for it, every time (docs/70). Counting
  // entries here, the way the key-value store does, would be a number that means nothing.
  bool space(uint64_t& total, uint64_t& free) override {
    total = size_;
    free = size_;
    return true;
  }

  // The bytes themselves, for whoever has to look at them (the tests do).
  uint8_t* bytes() { return bytes_; }
  uint32_t size() const { return size_; }

 private:
  struct Open {
    bool used = false;
    uint32_t pos = 0;
  };

  static bool is_the_file(const char* rel) {
    return rel == nullptr || rel[0] == '\0' || (rel[0] == '/' && rel[1] == '\0');
  }

  Open* slot_of(FileSlot& slot) {
    if (slot.impl == nullptr) return nullptr;
    return static_cast<Open*>(slot.impl);
  }

  static const uint32_t kOpenSlots = 4;
  uint8_t* bytes_;
  uint32_t size_;
  Open slots_[kOpenSlots];
};

}  // namespace hal

#endif  // LUNOK_HAL_MEMORY_FILE_H
