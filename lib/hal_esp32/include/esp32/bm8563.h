// The BM8563: the clock chip the Paper carries with a coin cell behind it.
//
// It is what makes the hour survive the cable being pulled, which is the one thing the
// ESP32's own RTC domain cannot do. The chip counts civil time —years, months, days— and has
// no idea of a time zone or of an epoch: this driver is the one that turns it into
// milliseconds since 1970, in UTC (hal/civil.h, docs/60).
#ifndef LUNOK_ESP32_BM8563_H
#define LUNOK_ESP32_BM8563_H

#include <stdint.h>

#include "esp32/i2c.h"

namespace hal {

class Bm8563 {
 public:
  // `bus` may be nullptr: that is a board whose bus did not come up, and every answer here
  // is then false instead of a crash (docs/50).
  explicit Bm8563(II2c* bus, uint8_t address = 0x51);

  // What the chip says, in milliseconds since 1970 UTC.
  //
  // False when the bus does not answer, when the chip's control registers are the ones a
  // power-on reset leaves (the oscillator stopped), or when the **VL bit** says the chip
  // lost its supply: a clock that stopped is not a clock that says 1970, and reporting that
  // number as an epoch would be a lie (docs/60).
  bool read_epoch_ms(int64_t& epoch_ms);

  // Writes the hour and clears VL: from here on the chip is the truth and the next boot
  // reads it. False when the bus does not answer, and the caller says so.
  bool write_epoch_ms(int64_t epoch_ms);

  // Arms the chip so that its interrupt line goes off at `epoch_ms`, using whatever the chip
  // has that reaches that far: its **countdown timer** (one second per tick up to 255 s, one
  // minute per tick up to 255 min) or, beyond four hours, its **clock alarm** (hour and minute,
  // rounded up, so the board is never woken before its hour). hal/alarm.h decides which, and
  // that is where the reasons are (docs/50).
  //
  // On this board that line is not a GPIO: it reaches the power circuit, so this is what turns
  // the board back on after the power is cut. False when the bus does not answer, when the chip
  // does not know the hour, or when the wait is further away than the chip can express: then no
  // alarm is armed, and a board that goes off on it stays off.
  bool arm_at(int64_t epoch_ms);

  // Takes the interrupt back down, stops the countdown and forgets the alarm. Runs at every
  // boot: an alarm that has already done its job must not fire again a day later.
  bool disarm();

  // Whether the chip's interrupt is what brought the board back —the alarm or the countdown,
  // both pull the same line—. On this board it is the only way to tell that from a press of
  // the power button: both look like a boot (docs/60). Read it **before** `disarm()`, because
  // disarming is what forgets it.
  bool interrupt_fired();

 private:
  // Marks the chip as trustworthy and starts its oscillator, which is what a chip out of a
  // factory reset does not do on its own.
  bool start();

  II2c* bus_;
  uint8_t address_;
};

}  // namespace hal

#endif  // LUNOK_ESP32_BM8563_H
