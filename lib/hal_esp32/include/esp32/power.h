// The sleep of an ESP32 and the speed of its processor.
//
// Both are the chip's and not the board's: the RTC timer, the two depths and the
// three frequencies are the same on every ESP32 of this family. What the board
// decides is whether a key is wired to a wake source, and that is the one thing
// asked for here (docs/50, docs/60).
#ifndef LUNOK_ESP32_POWER_H
#define LUNOK_ESP32_POWER_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

class Esp32Power : public IPower {
 public:
  // `wake_pin` is the GPIO the board wires to EXT0, or a negative number when the
  // board has no key that wakes the chip (the Paper's keys switch the power off
  // instead, docs/50).
  explicit Esp32Power(int wake_pin);

  // The chip has no fuel gauge of its own, and no board of this project reads one yet
  // —the Paper has the divider on GPIO35 and it is not calibrated—: what is not
  // measured is not invented, and `power.battery()` says nil, which is not zero volts
  // (docs/90).
  bool battery(int& mv, int& pct, bool& charging) override;

  bool sleep(SleepMode mode, int64_t at_epoch_ms) override;

  // Said by the wiring: the RTC timer for the alarms, and the key when there is one.
  const char* wakes() override;

  uint32_t cpu_mhz() override;
  bool set_cpu_mhz(uint32_t mhz) override;

 private:
  int wake_pin_;
};

}  // namespace hal

#endif  // LUNOK_ESP32_POWER_H
