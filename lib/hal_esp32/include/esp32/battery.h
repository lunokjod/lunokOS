// A battery on an ADC pin behind a divider.
//
// The ADC is the chip's and the divider is the board's, so this asks for both: one pin and
// one factor, and the answer is millivolts. Nothing here decides what a percentage is —
// that is a curve, not a measurement, and it lives in `hal/battery.h` (docs/50).
#ifndef LUNOK_ESP32_BATTERY_H
#define LUNOK_ESP32_BATTERY_H

#include <stdint.h>

namespace hal {

struct BatteryPins {
  int pin;          // the ADC pin the divider comes to (GPIO35 on the Paper)
  uint32_t factor;  // the divider as a multiplier: 2 when it halves the voltage
};

// Reads the battery in millivolts, already multiplied back.
//
// False when the reading cannot be used: the pin is not an ADC pin of unit 1, the chip
// refuses to characterise it, or the number is out of any single cell's range (under a
// volt is a divider that is not there, not an empty battery). A false here is
// `power.battery()` saying `nil`, which is not the same as zero volts (docs/30).
bool esp32_battery(const BatteryPins& pins, int& mv);

}  // namespace hal

#endif  // LUNOK_ESP32_BATTERY_H
