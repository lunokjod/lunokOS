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

// The curve of the cell, in one place for both directions: a percentage is turned into millivolts by
// the emulator when it lends the battery of the machine it runs on to the board it imitates, and back by
// the board when it reads its own divider (docs/50, docs/85).
namespace battery_curve {

struct Step {
  int mv;
  uint32_t pct;
};

// An approximation of a typical LiPo, not the sheet of the cell this board carries, and the voltage of
// a cell under load sags: a phone-shaped number to show on a screen, never a number to decide with.
// 4,2 V is full and 3,3 V is empty; outside that it is clamped, because a charger can read a little
// over full and that is not 105 %.
inline const Step* steps(uint32_t& count) {
  static const Step kCurve[] = {
      {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3800, 57},
      {3700, 42},  {3600, 27}, {3500, 14}, {3400, 6},  {3300, 0},
  };
  count = sizeof(kCurve) / sizeof(kCurve[0]);
  return kCurve;
}

}  // namespace battery_curve

// The percentage of a single cell from its open-circuit voltage.
inline uint32_t battery_percent(int mv) {
  uint32_t count = 0;
  const battery_curve::Step* curve = battery_curve::steps(count);
  if (mv >= curve[0].mv) return 100;
  if (mv <= curve[count - 1].mv) return 0;
  // Between two points of the curve, straight: the cell is curved over hundreds of
  // millivolts, not over the tens between two rows (docs/50).
  for (uint32_t i = 1; i < count; ++i) {
    if (mv < curve[i].mv) continue;
    const int top_mv = curve[i - 1].mv;
    const int bottom_mv = curve[i].mv;
    const uint32_t top = curve[i - 1].pct;
    const uint32_t bottom = curve[i].pct;
    const int span = top_mv - bottom_mv;
    const int above = mv - bottom_mv;
    return bottom + (top - bottom) * static_cast<uint32_t>(above) / static_cast<uint32_t>(span);
  }
  return 0;
}

// **The millivolts a percentage is**, which is the same curve read the other way: it is what a battery
// that speaks in percentages needs to become one of these cells, which is how the emulator lends the
// laptop's battery to the Paper (docs/85). Straight between two rows, exactly as `battery_percent`
// does it, so a percentage that goes out comes back.
inline int battery_mv(uint32_t pct) {
  uint32_t count = 0;
  const battery_curve::Step* curve = battery_curve::steps(count);
  if (pct >= 100) return curve[0].mv;
  if (pct == 0) return curve[count - 1].mv;
  for (uint32_t i = 1; i < count; ++i) {
    // **The first row at or below the asked percentage** is the segment it falls in: the rows go down from
    // 100 to 0, so the ones above it are skipped.
    if (curve[i].pct > pct) continue;
    const uint32_t top = curve[i - 1].pct;
    const uint32_t bottom = curve[i].pct;
    const int top_mv = curve[i - 1].mv;
    const int bottom_mv = curve[i].mv;
    const uint32_t span = top - bottom;
    const uint32_t above = top - pct;
    return top_mv - static_cast<int>(static_cast<uint32_t>(top_mv - bottom_mv) * above / span);
  }
  return curve[count - 1].mv;
}

}  // namespace hal

#endif  // LUNOK_HAL_BATTERY_H
