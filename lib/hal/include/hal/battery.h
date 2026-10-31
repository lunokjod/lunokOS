// What a battery voltage means.
//
// The number that comes from the pin is millivolts and it is a measurement. The percentage
// is not: it is a curve, and a single lithium cell is not a straight line from empty to
// full —most of the charge lives in the last volt, and a linear map says 45 % at 3,70 V
// when the cell is nearer 30—. So the curve is here, in one place, and a board that reads
// a divider uses it instead of inventing its own (docs/50).
#ifndef LUNOK_HAL_BATTERY_H
#define LUNOK_HAL_BATTERY_H

#include <stdint.h>

namespace hal {

// The percentage of a single cell from its open-circuit voltage.
//
// It is an approximation of a typical LiPo, not the sheet of the cell this board carries,
// and the voltage of a cell under load sags: a phone-shaped number to show on a screen,
// never a number to decide with. 4,2 V is full and 3,3 V is empty; outside that it is
// clamped, because a charger can read a little over full and that is not 105 %.
inline uint32_t battery_percent(int mv) {
  struct Step {
    int mv;
    uint32_t pct;
  };
  static const Step kCurve[] = {
      {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3800, 57},
      {3700, 42},  {3600, 27}, {3500, 14}, {3400, 6},  {3300, 0},
  };
  const uint32_t steps = sizeof(kCurve) / sizeof(kCurve[0]);
  if (mv >= kCurve[0].mv) return 100;
  if (mv <= kCurve[steps - 1].mv) return 0;
  // Between two points of the curve, straight: the cell is curved over hundreds of
  // millivolts, not over the tens between two rows (docs/50).
  for (uint32_t i = 1; i < steps; ++i) {
    if (mv < kCurve[i].mv) continue;
    const int top_mv = kCurve[i - 1].mv;
    const int bottom_mv = kCurve[i].mv;
    const uint32_t top = kCurve[i - 1].pct;
    const uint32_t bottom = kCurve[i].pct;
    const int span = top_mv - bottom_mv;
    const int above = mv - bottom_mv;
    return bottom + (top - bottom) * static_cast<uint32_t>(above) / static_cast<uint32_t>(span);
  }
  return 0;
}

}  // namespace hal

#endif  // LUNOK_HAL_BATTERY_H
