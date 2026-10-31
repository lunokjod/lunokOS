// The clock of the ESP32: the SoC's own counter, kept by the RTC memory through a
// deep sleep.
//
// It is not a clock chip with a battery behind it: it counts while there is power
// and is lost when the power goes. A board that carries a real RTC chip on top of
// this one (the Paper has a BM8563, docs/90) keeps counting with this until that
// chip is read, and says so through `source()` (docs/60).
#ifndef LUNOK_ESP32_CLOCK_H
#define LUNOK_ESP32_CLOCK_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

// The wall clock as the chip has it: milliseconds since the epoch when somebody has
// set it, and a number counted from the boot when nobody has. The HAL's sleep and
// the radio's NTP both need the same answer, which is why it is here and not inside
// the clock object (docs/60).
int64_t esp32_wall_ms();

// The one clock of the board: the epoch, the monotonic counter, the armed wake and
// where the epoch comes from.
IClock* esp32_clock();

}  // namespace hal

#endif  // LUNOK_ESP32_CLOCK_H
