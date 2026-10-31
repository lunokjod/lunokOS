// The SHT30 of the M5Paper: how warm it is and how wet, on the internal I2C (docs/50).
//
// The chip **measures when it is asked** and not by itself, and the request is a two-byte command
// rather than a register. What comes back is six bytes: two 16-bit values, each with its own CRC. So
// the driver is three lines of arithmetic and one honest sentence about time.
//
// **How long asking costs, said here and not hidden**: with clock stretching on, the chip holds the
// clock line low while it measures and this driver's read waits with it — about six milliseconds at
// the repeatability chosen here. That wait happens inside one I2C transaction, so it is a turn of the
// kernel that is a few milliseconds longer and not a thread that is blocked, and it is the price of
// not inventing a promise for a number that a person reads once a minute (docs/50).
//
// **The wait has to be allowed by the bus, and by default it is not**: this IDF v4.4.6 sets the
// hardware ceiling to ten bus cycles (100 µs at 100 kHz), so a sensor that holds the clock for six
// milliseconds is reported as ESP_ERR_TIMEOUT and the bus is reset —which is what the Paper said on
// 1/11 (`i2c: read of 44 failed (ESP_ERR_TIMEOUT)`, `env no`)—. `esp32/i2c.cpp` lifts that ceiling
// to 13.1 ms, and **medium repeatability is what fits under it**: high repeatability is 15 ms and
// would not (docs/50).
#ifndef LUNOK_ESP32_SHT30_H
#define LUNOK_ESP32_SHT30_H

#include <stdint.h>

#include "esp32/i2c.h"
#include "hal/hal.h"

namespace hal {

// 1001 000 on the bus: the SHT30's address is fixed and there is no pin to change it (docs/50).
const uint8_t kSht30Address = 0x44;

// The command: single shot, **medium** repeatability, clock stretching **enabled**.
//
// Medium and not high because this is a room and not a laboratory: 0,2 °C against 0,1 °C and six
// milliseconds of wait against fifteen. Stretching and not the other way, because it is what lets the
// bus do the waiting instead of a delay in the kernel (docs/50).
const uint8_t kSht30CommandHigh = 0x2C;
const uint8_t kSht30CommandLow = 0x0D;

// The CRC of one value, which is the chip's own: polynomial 0x31, start 0xFF, no reflection, two
// bytes in. A measurement whose CRC does not match is a measurement that did not happen (docs/50).
uint8_t sht30_crc(uint8_t msb, uint8_t lsb);

class Sht30 : public ISensor {
 public:
  explicit Sht30(II2c* bus) : bus_(bus) {}

  bool read(float& celsius, float& humidity) override;

  // Whether the chip answers at all: a whole measurement asked once, when the board boots. A board
  // that says it has a sensor has to have tried it, or the capability is a promise nobody checked
  // (docs/50).
  bool probe();

 private:
  II2c* bus_;
};

// The chip on this bus, or nullptr when it does not answer (docs/50).
ISensor* esp32_sht30(II2c* bus);

}  // namespace hal

#endif  // LUNOK_ESP32_SHT30_H
