// The FM24C02 of the M5Paper: 2 Kbit of FRAM on the internal I2C, seen as one file of 256 bytes
// (docs/50, docs/70).
//
// It is a **FRAM and not an EEPROM**, and that is the one thing that changes the driver: a write is
// in the memory the moment the transaction ends —there is no 5 ms write cycle to wait for, which is
// the cost an EEPROM like the 24C02 pays after every byte— and the endurance is about 10^12 cycles,
// so a board can keep a counter there without counting the wear. What it keeps from the 24C02 it is
// compatible with is the frame: address **0x50** on the bus, **one byte of word address**, and the
// address counting up by itself while bytes go in or out, wrapping at 256.
//
// The chip **is** the file: there is no copy in RAM, so what is written is in the memory when the
// call returns and nothing has to be flushed —and it is still there after the power goes, which is
// what an app keeps in it (docs/70).
#ifndef LUNOK_ESP32_FM24C02_H
#define LUNOK_ESP32_FM24C02_H

#include "esp32/i2c.h"
#include "hal/hal.h"

namespace hal {

// 2 Kbit, from the board's own documentation: the FM24C02 stores `256 x 8` and is what the paper
// says a person can use (docs/50).
const uint32_t kFramBytes = 256;
// 1010 000 on the bus: the FM24C02 of this board has its A2..A0 lines to ground (docs/50).
const uint8_t kFramAddress = 0x50;

class Fm24c02 : public IStorage {
 public:
  explicit Fm24c02(II2c* bus) : bus_(bus) {}

  bool stat(const char* rel, Stat& out) override {
    if (!is_the_file(rel) || bus_ == nullptr) return false;
    out.type = NodeType::kFile;
    out.size = kFramBytes;
    out.mtime = 0;  // the memory has no clock of its own (docs/70)
    return true;
  }

  // One file has nothing inside it: a board that could list `/fram` would be saying it is a
  // directory.
  bool list(const char* rel, ListSink& sink) override {
    (void)rel;
    (void)sink;
    return false;
  }

  bool open(const char* rel, OpenMode mode, FileSlot& slot) override {
    if (!is_the_file(rel) || bus_ == nullptr) return false;
    for (uint32_t i = 0; i < kOpenSlots; ++i) {
      if (slots_[i].used) continue;
      slots_[i].used = true;
      slots_[i].pos = (mode == OpenMode::kAppend) ? kFramBytes : 0;
      slot.impl = &slots_[i];
      slot.size = kFramBytes;
      return true;
    }
    return false;  // the pool is full, which the caller is told instead of a crash
  }

  int32_t read(FileSlot& slot, void* buf, uint32_t len) override;
  int32_t write(FileSlot& slot, const void* buf, uint32_t len) override;

  bool seek(FileSlot& slot, uint32_t pos) override {
    Open* open = slot_of(slot);
    if (open == nullptr || pos > kFramBytes) return false;
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

  // Its room is its size and all of it is free, always, the same way the RTC memory answers: raw
  // memory has no keys and nothing is allocated out of it (docs/70).
  bool space(uint64_t& total, uint64_t& free) override {
    total = kFramBytes;
    free = kFramBytes;
    return true;
  }

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
  II2c* bus_;
  Open slots_[kOpenSlots];
};

// The file over the chip, or nullptr when nobody answers at 0x50: a board with no FM24C02 soldered
// has no `/fram`, and saying so is better than a mount that cannot be read (docs/70).
IStorage* esp32_fram(II2c* bus);

}  // namespace hal

#endif  // LUNOK_ESP32_FM24C02_H
