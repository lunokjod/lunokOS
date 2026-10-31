// Reading keys that a board wired to its own pins.
//
// **The ESP32 has no keys.** What the chip has is GPIOs, and it is the board that
// decides which of them a key closes to ground, on which face it sits and whether it
// is a key on its own or one of a dial. So the table is the board's and the reading is
// here, and a board with one button and a board with three keys share this code
// instead of copying it (docs/50).
#ifndef LUNOK_ESP32_GPIO_KEYS_H
#define LUNOK_ESP32_GPIO_KEYS_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

// One key of a board: the pin it closes to ground and where it is on the case.
struct KeySpot {
  uint8_t pin;
  Face face;
  Spot spot;
  KeyKind kind;
  uint32_t group;
};

// Active low, no interrupt, and the 10 ms of debounce the maker's own libraries use:
// the pin is read in every turn of the kernel and a reading that does not hold is not
// a press (docs/50).
class GpioKeys : public IInput {
 public:
  // The table is not copied: it has to outlive this object, which it does, because it
  // is a constant of the board library.
  GpioKeys(const KeySpot* table, uint32_t count, uint32_t debounce_ms = 10);

  uint32_t count() override { return count_; }
  bool pressed(uint32_t key) override;
  int64_t held_ms(uint32_t key) override;
  Face face(uint32_t key) override;
  Spot spot(uint32_t key) override;
  KeyKind kind(uint32_t key) override;
  uint32_t group(uint32_t key) override;

 private:
  // A fixed array and not a list: the keys are read in every turn and a turn must not
  // allocate (docs/75). More than the kernel keeps for itself, by design.
  static const uint32_t kMaxKeys = 8;

  void ensure_ready();
  void sample();
  bool read_pin(uint32_t key) const;

  const KeySpot* table_;
  uint32_t count_;
  uint32_t debounce_ms_;
  bool ready_ = false;
  bool raw_[kMaxKeys] = {false};
  bool stable_[kMaxKeys] = {false};
  int64_t changed_ms_[kMaxKeys] = {0};
  int64_t pressed_since_ms_[kMaxKeys] = {0};
};

}  // namespace hal

#endif  // LUNOK_ESP32_GPIO_KEYS_H
