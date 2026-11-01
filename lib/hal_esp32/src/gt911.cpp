// The GT911 driver: ask who is there, ask for a frame, and turn eight bytes into one finger. The
// chip keeps its own state, so this driver keeps none: what is read is what the glass has now
// (docs/50).
#include "esp32/gt911.h"

#include <stdio.h>

namespace hal {
namespace {

// The four registers this driver touches, all of them two bytes wide (docs/50).
const uint16_t kRegProductId = 0x8140;
const uint16_t kRegConfig = 0x8047;
const uint16_t kRegStatus = 0x814E;
const uint16_t kRegPoint = 0x814F;

// Inside the configuration block, which starts at `0x8047`: the version is its first byte, the two
// maxima are the frame the chip counts in, and `0x804C` is how many fingers it will report at once.
const uint8_t kConfigXMax = 1;
const uint8_t kConfigYMax = 3;
const uint8_t kConfigTouchNum = 5;
// 186 bytes, which is the length both vendor drivers read and write back with a fresh checksum
// (docs/50).
const uint8_t kConfigBytes = 186;

// A point is eight bytes: track id, x, y, the size of the contact, and one reserved byte.
const uint8_t kPointBytes = 8;
const uint8_t kFrameBytes = kGt911MaxPoints * kPointBytes;

// The values are little-endian in the frame, which is the opposite of the two-byte register
// addresses: the chip counts this way and both vendor drivers read it this way (docs/50).
uint16_t word_at(const uint8_t* data, uint8_t offset) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset + 1]) << 8) |
                               static_cast<uint16_t>(data[offset]));
}

}  // namespace

bool Gt911::probe() {
  if (bus_ == nullptr) return false;
  const uint8_t addresses[2] = {kGt911Address, kGt911AddressBackup};
  uint8_t id[4] = {0};
  bool found = false;
  for (uint8_t i = 0; i < 2 && !found; ++i) {
    // **Ask who is there before reading anything.** This unit answers at one of the two addresses and
    // the other one is nobody: reading its product id from the empty one is what put
    // `i2c: read of 14:8140 failed (ESP_FAIL)` in the boot log of a board where nothing was wrong
    // (measured 1/11, docs/50).
    if (!bus_->present(addresses[i])) continue;
    address_ = addresses[i];
    if (!bus_->read16(address_, kRegProductId, id, sizeof(id))) continue;
    // The product id is the three characters `911` and a zero: a chip that answers something else
    // is another device on the same two wires and not ours (docs/50).
    if (id[0] == '9' && id[1] == '1' && id[2] == '1') found = true;
  }
  if (!found) {
    snprintf(profile_, sizeof(profile_), "no answer at 0x%02x or 0x%02x", kGt911Address,
             kGt911AddressBackup);
    return false;
  }

  uint8_t config[kConfigBytes] = {0};
  if (!bus_->read16(address_, kRegConfig, config, sizeof(config))) {
    snprintf(profile_, sizeof(profile_), "GT911 at 0x%02x, no configuration", address_);
    return false;
  }
  width_ = word_at(config, kConfigXMax);
  height_ = word_at(config, kConfigYMax);
  const uint8_t points = config[kConfigTouchNum];
  // The configuration of the unit is what it will report: it says two on the Paper measured on 1/11,
  // and anything the chip cannot give is a number an app would draw a gesture for and never see. A
  // value outside the frame's room is the frame's room.
  points_ = (points >= 1 && points <= kGt911MaxPoints) ? points : kGt911MaxPoints;
  // A frame left over from before the reset is dropped here: the first read of the session must be
  // a finger that is on the glass now and not one from the boot (docs/50).
  release();
  snprintf(profile_, sizeof(profile_), "GT911 at 0x%02x, config %u, %ux%u, %u fingers", address_,
           config[0], static_cast<unsigned>(width_), static_cast<unsigned>(height_),
           static_cast<unsigned>(points_));
  return true;
}

void Gt911::release() {
  // Writing a zero to the status is what tells the chip the frame has been taken; it is also what it
  // wants when it has nothing to say (docs/50).
  const uint8_t frame[3] = {0x81, 0x4E, 0x00};
  bus_->write(address_, frame, sizeof(frame));
}

uint8_t Gt911::read(TouchPoint* out, uint8_t capacity) {
  if (bus_ == nullptr || out == nullptr || capacity == 0) return 0;
  uint8_t status = 0;
  if (!bus_->read16(address_, kRegStatus, &status, 1)) return 0;
  // Bit 7 is "there is a frame" and the low nibble is how many fingers it has. No frame, no fingers
  // or more fingers than the frame can hold all mean the same thing to a caller —nobody is
  // touching— and the chip is told to let go of it either way, or the next read would answer with
  // the finger of a minute ago (docs/50).
  const uint8_t points = static_cast<uint8_t>(status & 0x0F);
  if ((status & 0x80) == 0 || points == 0 || points > kGt911MaxPoints) {
    release();
    return 0;
  }
  uint8_t frame[kFrameBytes] = {0};
  if (!bus_->read16(address_, kRegPoint, frame, static_cast<size_t>(points) * kPointBytes)) {
    release();
    return 0;
  }
  release();
  const uint8_t count = (points < capacity) ? points : capacity;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* point = frame + static_cast<uint16_t>(i) * kPointBytes;
    out[i].id = point[0];
    out[i].x = word_at(point, 1);
    out[i].y = word_at(point, 3);
    out[i].size = word_at(point, 5);
  }
  return count;
}

ITouch* esp32_gt911(II2c* bus) {
  if (bus == nullptr) return nullptr;
  static Gt911 chip(bus);
  // Asked once: `touch()` is called from the capabilities and from every read of a frame, and a
  // probe is a configuration of 186 bytes (docs/50).
  static const bool there = chip.probe();
  // Said out loud once, because it is the only way to know which of the two addresses this unit
  // answers on and in which frame it counts, without a scope (docs/50, docs/95).
  static bool said = false;
  if (!said) {
    said = true;
    printf("touch: %s\n", chip.profile());
  }
  return there ? &chip : nullptr;
}

}  // namespace hal
