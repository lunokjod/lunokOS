// The GT911 of the emulator: the frame of the last scan, the bus it costs, and nothing that a PC cannot
// have (docs/50, docs/85).
#include "emu/gt911.h"


#include "emu/options.h"
#include "hal/log.h"
#include "pal/pal.h"

namespace emu {
namespace {

// **The bus of this board runs at 100 kHz** —the speed every chip on it asks for— and every byte is
// nine bits with the acknowledge, so a byte is ninety microseconds (docs/50). The driver adds a
// transaction of its own to each step of the read, which is the second number and the only one that is
// not on the wire.
const uint32_t kBusHz = 100000;
const uint32_t kBitsPerByte = 9;
const uint32_t kTransactionUs = 40;

// What a read puts on the wire, in bytes: an address and two of register before each step, the status,
// the eight bytes of a point, and the three of the zero that releases the frame (docs/50).
const uint32_t kStatusBytes = 4;
const uint32_t kReleaseBytes = 4;
const uint32_t kPointBytes = 8;
const uint32_t kAddressAndRegister = 3;

// **How often the chip reports**: 10 ms, which is the datasheet's number for this glass and a rate
// nobody has counted on the board yet (docs/95, E-6). `--touch-scan` is where the measurement goes the
// day somebody drags a finger and counts the frames (docs/85).
const uint32_t kScanMs = 10;

// The size of the contact this unit reports, which is a measurement and not a guess: two fingers on the
// glass gave `s28` and `s30` on 1/11 (docs/95, E-6).
const uint16_t kSizeOfSlotZero = 28;
const uint16_t kSizeOfSlotOne = 30;

uint32_t bus_us(uint32_t bytes) { return bytes * kBitsPerByte * 1000000u / kBusHz; }

}  // namespace

uint32_t Gt911Panel::read_us(uint8_t fingers) {
  // Nobody on the glass: the status is read and the chip is told to let go of the frame it does not
  // have, and the frame of the points is never asked for.
  if (fingers == 0) return bus_us(kStatusBytes + kReleaseBytes) + 2 * kTransactionUs;
  return bus_us(kStatusBytes + kAddressAndRegister + kPointBytes * fingers + kReleaseBytes) +
         3 * kTransactionUs;
}

uint32_t Gt911Panel::probe_us() {
  // The four bytes of the product id, the 186 of the configuration, and the zero that drops a frame
  // left over from before the reset (docs/50).
  return bus_us(kStatusBytes + kAddressAndRegister + 186 + kReleaseBytes) + 3 * kTransactionUs;
}

uint32_t Gt911Panel::scan_ms() const {
  const int32_t asked = options().touch_scan_ms;
  return (asked > 0) ? static_cast<uint32_t>(asked) : kScanMs;
}

bool Gt911Panel::finger_down(uint8_t slot, uint16_t x, uint16_t y) {
  if (slot >= kMaxFingers) return false;
  // A point that is not on the glass is not a finger: the emulator refuses it instead of putting it
  // somewhere and letting a test believe what it read (docs/85).
  if (x >= kFrameWidth || y >= kFrameHeight) return false;
  fingers_[slot].down = true;
  fingers_[slot].x = x;
  fingers_[slot].y = y;
  // **A finger that touched the glass is a frame the chip has to hand over**, even when it is lifted again
  // before the driver asks: the chip scans by itself and keeps the frame of its last scan (docs/50, docs/85).
  latched_ = true;
  latched_slot_ = slot;
  return true;
}

bool Gt911Panel::finger_move(uint8_t slot, uint16_t x, uint16_t y) {
  if (slot >= kMaxFingers || !fingers_[slot].down) return false;
  if (x >= kFrameWidth || y >= kFrameHeight) return false;
  fingers_[slot].x = x;
  fingers_[slot].y = y;
  latched_ = true;
  latched_slot_ = slot;
  return true;
}

bool Gt911Panel::finger_up(uint8_t slot) {
  if (slot >= kMaxFingers || !fingers_[slot].down) return false;
  fingers_[slot].down = false;
  return true;
}

bool Gt911Panel::finger_at(uint8_t slot, uint16_t& x, uint16_t& y) const {
  if (slot >= kMaxFingers || !fingers_[slot].down) return false;
  x = fingers_[slot].x;
  y = fingers_[slot].y;
  return true;
}

uint8_t Gt911Panel::fingers_down() const {
  uint8_t count = 0;
  for (uint8_t slot = 0; slot < kMaxFingers; ++slot) {
    if (fingers_[slot].down) ++count;
  }
  return count;
}

void Gt911Panel::reset() {
  for (uint8_t slot = 0; slot < kMaxFingers; ++slot) fingers_[slot] = Finger();
  taken_ms_ = 0xFFFFFFFFu;
  frames_ = 0;
  missed_ = 0;
  latched_ = false;
  latched_slot_ = 0;
}

uint8_t Gt911Panel::read(hal::TouchPoint* out, uint8_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  // **The bus first, and the emulator pays it**: three transactions of a hundred-kilohertz bus is what a
  // turn costs on the board, and a read that is free here would make a caller that reads ten times a
  // turn look reasonable (docs/50, docs/85). With the waits off (`--no-delay`, every test) the read is
  // the model and not the clock; the scan window below stays either way, because a frame that was
  // taken is a fact of the chip and not a delay.
  const uint8_t down = fingers_down();
  if (options().delays) pal::delay_us(read_us(down));

  // **One frame per scan.** The chip scans by itself and sets the bit that says it has something to
  // hand over; the driver takes it away with a zero. A read that arrives in a window whose frame was
  // already taken finds nothing —the hardware answers "nobody" while a finger is down— and that is the
  // problem a caller polling faster than the glass has to survive (docs/50).
  const uint32_t now = static_cast<uint32_t>(pal::system().millis());
  const uint32_t scan = scan_ms();
  const uint32_t boundary = now - (now % scan);
  if (taken_ms_ == boundary) {
    ++missed_;
    return 0;
  }
  taken_ms_ = boundary;
  // **A tap that began and ended between two reads is still the frame of the last scan**: the chip scans the
  // glass every ten milliseconds and holds what it found until a read takes it, which is the whole reason a
  // finger of a hundred milliseconds is always seen and a finger of a microsecond is not a finger at all
  // (docs/50, docs/85).
  if (down == 0) {
    if (!latched_) return 0;
    latched_ = false;
    ++frames_;
    const Finger& last = fingers_[latched_slot_];
    out[0].id = latched_slot_;
    out[0].x = last.x;
    out[0].y = last.y;
    out[0].size = (latched_slot_ == 0) ? kSizeOfSlotZero : kSizeOfSlotOne;
    return 1;
  }
  latched_ = false;

  ++frames_;
  const uint8_t count = (down < capacity) ? down : capacity;
  uint8_t written = 0;
  for (uint8_t slot = 0; slot < kMaxFingers && written < count; ++slot) {
    if (!fingers_[slot].down) continue;
    out[written].id = slot;
    out[written].x = fingers_[slot].x;
    out[written].y = fingers_[slot].y;
    // The contact of this unit, which the chip measures and does not report as a point (docs/95).
    out[written].size = (slot == 0) ? kSizeOfSlotZero : kSizeOfSlotOne;
    ++written;
  }
  return written;
}

Gt911Panel* touch_panel() {
  static Gt911Panel panel;
  static bool greeted = false;
  if (!greeted) {
    greeted = true;
    // **The probe, paid once and said once**: the chip is asked who it is and in which frame it counts
    // before anything is read from it, which on the board is 186 bytes of configuration and the line
    // that says which of the two addresses answered (docs/50, docs/95).
    if (options().delays) pal::delay_us(Gt911Panel::probe_us());
    hal::logf(pal::kLogInfo, "TOUCH", "GT911 at 0x5d, config 66, 540x960, 2 fingers");
  }
  return &panel;
}

}  // namespace emu
