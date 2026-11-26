// The calendar arithmetic of the system, and the only copy of it (docs/60, docs/90).
#include "kernel/calendar.h"

#include "hal/civil.h"

namespace kernel {
namespace calendar {
namespace {

// The days of the twelve months, February with twenty-eight and the leap year added below: a table is what a
// person reads and a rule of four lines is what a chip should have, and both are here once (docs/60).
const uint8_t kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// **Sakamoto's shift**: the offset of each month for the weekday count, which is the whole of the calendar rule and
// costs twelve bytes. It is integer arithmetic with no table of days per year, and it holds for every year the
// Gregorian calendar has (docs/60).
const uint8_t kSakamotoShift[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

// **The two settings, with the calendar this system was born with as their value** (0.170.0): a build that has read
// nothing yet answers the Gregorian calendar of the board's own rule, and `cfg::boot` puts these back before it reads
// the store so that a second boot of the same object —the tests— does not inherit the run before (docs/60).
uint8_t g_week_start = 1;

// The month as the Gregorian rule has it, which is the only rule there is: **the leap day is a fact of the calendar and
// not a setting of the board** (0.192.57), o sigui que el febrer té vint-i-nou quan toca i prou (docs/60).
uint8_t month_length(uint16_t year, uint8_t month) {
  if (month < 1 || month > 12) return 0;
  if (month != 2) return kMonthDays[month - 1];
  return static_cast<uint8_t>(hal::is_leap_year(year) ? 29 : 28);
}

}  // namespace

uint8_t days_in_month(uint16_t year, uint8_t month) { return month_length(year, month); }

uint8_t week_start() { return g_week_start; }

bool set_week_start(uint8_t first) {
  if (first < 1 || first > 7) return false;
  g_week_start = first;
  return true;
}

uint8_t weekday_of(uint16_t year, uint8_t month, uint8_t day) {
  // **A date that does not exist is no weekday at all**, and what exists is the Gregorian month: the twenty-ninth of
  // February falls on the day it falls, because the sky is not a setting of this board (docs/60).
  if (month < 1 || month > 12 || day < 1 || day > month_length(year, month)) return 0;
  uint32_t y = year;
  if (month < 3) --y;  // January and February belong to the year before, which is what makes February's length fit
  const uint32_t w = (y + y / 4 - y / 100 + y / 400 + kSakamotoShift[month - 1] + day) % 7;
  // Zero is Sunday in that count and seven is Sunday here, because a week of this system begins on Monday.
  return static_cast<uint8_t>((w == 0) ? 7 : w);
}

}  // namespace calendar
}  // namespace kernel
