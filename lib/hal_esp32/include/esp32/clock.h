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

// The chip's own wake timer, armed and taken back down in one place. ESP-IDF keeps the armed
// timer in a structure that a boot wipes, and it logs an **error** when a source that is not
// armed is disarmed —which is exactly what a board whose clock chip keeps the alarm does after
// every arming—, so this remembers whether the timer is ours to take down and only then touches
// it. `esp32_arm_timer_wake` returns false when the chip refuses the deadline.
bool esp32_arm_timer_wake(int64_t at_epoch_ms);
void esp32_disarm_timer_wake();

// The one clock of the board: the epoch, the monotonic counter, the armed wake and
// where the epoch comes from.
IClock* esp32_clock();

}  // namespace hal

#endif  // LUNOK_ESP32_CLOCK_H
